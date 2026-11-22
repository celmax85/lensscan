// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScanProcessor.h"

#include "BatchTools.h"
#include "BookProcessor.h"
#include <QPainter>
#include <algorithm>

#include "Jpeg.h"
#include "PageDetector.h"
#include "DocumentTools.h"
#include "ocr/Ocr.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTransform>

namespace lensscan {

QString pageModeKey(PageMode m)
{
    switch (m) {
    case PageMode::FlatPage: return QStringLiteral("flat");
    case PageMode::Book: return QStringLiteral("book");
    case PageMode::MultiDocument: return QStringLiteral("multi");
    case PageMode::Area: return QStringLiteral("area");
    case PageMode::Original: return QStringLiteral("original");
    }
    return QStringLiteral("flat");
}

PageMode pageModeFromKey(const QString &key)
{
    if (key == QLatin1String("multi"))
        return PageMode::MultiDocument;
    if (key == QLatin1String("book"))
        return PageMode::Book;
    if (key == QLatin1String("area"))
        return PageMode::Area;
    if (key == QLatin1String("original"))
        return PageMode::Original;
    return PageMode::FlatPage;
}

QImage rotateImage(const QImage &img, int degreesClockwise)
{
    const int d = ((degreesClockwise % 360) + 360) % 360;
    if (d == 0)
        return img;
    return img.transformed(QTransform().rotate(d));
}

// Both pages side by side, scaled to the same height.
QImage joinPages(const QVector<QImage> &pages)
{
    const int h = std::max(pages[0].height(), pages[1].height());
    QVector<QImage> scaled;
    int w = 0;
    for (const QImage &p : pages) {
        scaled << (p.height() == h ? p : p.scaledToHeight(h, Qt::SmoothTransformation));
        w += scaled.last().width();
    }
    QImage out(w, h, QImage::Format_RGB888);
    QPainter painter(&out);
    int x = 0;
    for (const QImage &p : scaled) {
        painter.drawImage(x, 0, p);
        x += p.width();
    }
    return out;
}

// Corner filling, color mode and orientation of a cropped flat page or document.
static QImage finishFlat(QImage img, const ScanOptions &opt)
{
    if (opt.completePage)
        img = fillPaperCorners(img, true);
    else if (opt.roundCorners)
        img = fillPaperCorners(img, false);
    img = applyColorMode(img, opt.colorMode);
    if (opt.autoOrient) {
        const int r = detectUprightRotation(img);
        if (r > 0)
            img = rotateImage(img, r);
    }
    return img;
}

QVector<QImage> processScanPages(const QImage &frame, const ScanOptions &opt)
{
    if (opt.pageMode == PageMode::MultiDocument) {
        QVector<QImage> docs = splitDocuments(rotateImage(frame, opt.rotation));
        if (docs.isEmpty()) {
            ScanOptions flat = opt;
            flat.pageMode = PageMode::FlatPage;
            return {processScan(frame, flat)};
        }
        for (QImage &d : docs)
            d = finishFlat(d, opt);
        return docs;
    }
    if (opt.pageMode != PageMode::Book)
        return {processScan(frame, opt)};
    const QImage img = rotateImage(frame, opt.rotation);
    BookOptions bo;
    bo.removeFingers = opt.removeFingers;
    QVector<QImage> pages = processBook(img, bo);
    if (pages.size() != 2) {
        // No open book found: treat it as a flat page.
        ScanOptions flat = opt;
        flat.pageMode = PageMode::FlatPage;
        return {processScan(frame, flat)};
    }
    if (opt.bookRightFirst)
        std::swap(pages[0], pages[1]);
    if (!opt.bookSplit)
        pages = {joinPages(opt.bookRightFirst ? QVector<QImage>{pages[1], pages[0]} : pages)};
    for (QImage &p : pages)
        p = applyColorMode(p, opt.colorMode);
    return pages;
}

QImage processScan(const QImage &frame, const ScanOptions &opt)
{
    if (opt.pageMode == PageMode::Book || opt.pageMode == PageMode::MultiDocument)
        return processScanPages(frame, opt).value(0);
    // Rotate first so that the area selected on the (rotated) preview matches.
    QImage img = rotateImage(frame, opt.rotation);
    switch (opt.pageMode) {
    case PageMode::Book:
    case PageMode::MultiDocument:
        break;
    case PageMode::FlatPage: {
        const QPolygonF quad = PageDetector::detect(img);
        if (quad.isEmpty())
            break;
        return finishFlat(PageDetector::warp(img, quad), opt);
    }
    case PageMode::Area: {
        const QRect r = QRectF(opt.area.x() * img.width(), opt.area.y() * img.height(),
                               opt.area.width() * img.width(), opt.area.height() * img.height())
                            .toAlignedRect()
                            .intersected(img.rect());
        if (r.width() > 8 && r.height() > 8)
            img = img.copy(r);
        break;
    }
    case PageMode::Original:
        break;
    }
    return applyColorMode(img, opt.colorMode);
}

namespace {

QMutex g_reservedMutex;
QSet<QString> g_reserved; // paths handed out by newScanPath / uniqueScanPath

bool taken(const QString &path)
{
    return g_reserved.contains(path) || QFileInfo::exists(path);
}

// Qt's resolution for an image that was given none.
int defaultDotsPerMeter()
{
    static const int dpm = QImage(1, 1, QImage::Format_RGB888).dotsPerMeterX();
    return dpm;
}

} // namespace

QString originalsDir(const QString &scanDir)
{
    return QDir(scanDir).filePath(QStringLiteral(".lensscan/originals"));
}

QString originalOf(const QString &scanPath, int *rotation)
{
    QImageReader r(scanPath);
    const QString name = QFileInfo(r.text(QStringLiteral("LensScanOriginal"))).fileName();
    if (rotation)
        *rotation = (r.text(QStringLiteral("LensScanRotation")).toInt() / 90 % 4 + 4) % 4 * 90;
    if (name.isEmpty())
        return {};
    const QString path = QDir(originalsDir(QFileInfo(scanPath).absolutePath())).filePath(name);
    return QFileInfo::exists(path) ? path : QString();
}

void setImageDpi(QImage &img, int dpi)
{
    if (dpi <= 0)
        return;
    const int dpm = qRound(dpi / 0.0254);
    img.setDotsPerMeterX(dpm);
    img.setDotsPerMeterY(dpm);
}

int imageDpi(const QImage &img)
{
    const int dpm = img.dotsPerMeterX();
    if (dpm <= 0 || dpm == defaultDotsPerMeter())
        return 0;
    return qRound(dpm * 0.0254);
}

int scanDpi(const ScanOptions &opt, int frameWidth)
{
    if (opt.dpi > 0)
        return opt.dpi;
    const double viewMm = opt.highCamera ? 460.0 : 330.0;
    return std::max(72, int(std::lround(frameWidth * 25.4 / viewMm)));
}

QString scanSuffix(const QString &format)
{
    const QString f = format.toLower();
    if (f == QLatin1String("png") || f == QLatin1String("bmp"))
        return f;
    return QStringLiteral("jpg");
}

QString uniqueScanPath(const QString &dir, const QString &prefix, const QString &suffix)
{
    QMutexLocker lock(&g_reservedMutex);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    for (int i = 1;; ++i) {
        const QString p = QDir(dir).filePath(
            QStringLiteral("%1_%2_%3.%4").arg(prefix, stamp).arg(i, 3, 10, QLatin1Char('0')).arg(suffix));
        if (!taken(p)) {
            g_reserved.insert(p);
            return p;
        }
    }
}

QString patternName(const QString &pattern, int digits, int number)
{
    QString p = pattern;
    if (!p.contains(QLatin1Char('#')))
        p += QLatin1Char('#');
    const int at = p.indexOf(QLatin1Char('#'));
    return p.left(at) + QStringLiteral("%1").arg(number, std::clamp(digits, 1, 9), 10, QLatin1Char('0')) +
           p.mid(at + 1).remove(QLatin1Char('#'));
}

QString newScanPath(const ScanOptions &opt, const QString &suffix)
{
    if (opt.namePattern.trimmed().isEmpty())
        return uniqueScanPath(opt.outputDir, opt.filePrefix, suffix);
    QString pattern = opt.namePattern.trimmed();
    if (!pattern.contains(QLatin1Char('#')))
        pattern += QLatin1Char('#');
    const int at = pattern.indexOf(QLatin1Char('#'));
    const QString before = pattern.left(at), after = pattern.mid(at + 1).remove(QLatin1Char('#'));
    const QRegularExpression re(QStringLiteral("^%1(\\d+)%2\\.[^.]+$")
                                    .arg(QRegularExpression::escape(before), QRegularExpression::escape(after)),
                                QRegularExpression::CaseInsensitiveOption);
    QMutexLocker lock(&g_reservedMutex);
    // Next number after the highest one in the folder (and among names being saved).
    qint64 highest = 0;
    QStringList names = QDir(opt.outputDir).entryList(QDir::Files);
    for (const QString &r : std::as_const(g_reserved))
        if (QFileInfo(r).absolutePath() == QDir(opt.outputDir).absolutePath())
            names << QFileInfo(r).fileName();
    for (const QString &n : std::as_const(names)) {
        const auto m = re.match(n);
        if (m.hasMatch())
            highest = std::max(highest, m.captured(1).toLongLong());
    }
    for (qint64 n = highest + 1;; ++n) {
        const QString p = QDir(opt.outputDir).filePath(patternName(pattern, opt.nameDigits, int(n)) +
                                                       QLatin1Char('.') + suffix);
        if (!taken(p)) {
            g_reserved.insert(p);
            return p;
        }
    }
}

bool saveImage(const QImage &img, const QString &path, int jpegQuality, int dpi)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    const int resolution = dpi > 0 ? dpi : imageDpi(img);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) {
        QByteArray data = encodeJpeg(img, jpegQuality);
        if (resolution > 0)
            data = jpegWithDpi(data, resolution);
        // Our text fields, as "key: value" comments (the form Qt reads back).
        for (const QString &key : img.textKeys())
            if (key.startsWith(QLatin1String("LensScan")) && data.size() > 2)
                data = jpegWithComment(data, (key + QStringLiteral(": ") + img.text(key)).toUtf8());
        if (data.isEmpty() || f.write(data) != data.size())
            return false;
    } else {
        QImage out = img;
        if (img.format() == QImage::Format_Grayscale8 && suffix == QLatin1String("png")) {
            // 1-bit PNG when the image is pure black and white: much smaller.
            bool binary = true;
            for (int y = 0; y < img.height() && binary; ++y) {
                const uchar *p = img.constScanLine(y);
                for (int x = 0; x < img.width(); ++x)
                    if (p[x] != 0 && p[x] != 255) {
                        binary = false;
                        break;
                    }
            }
            if (binary)
                out = img.convertToFormat(QImage::Format_Mono, Qt::ThresholdDither);
        }
        setImageDpi(out, resolution);
        if (!out.save(&f, suffix.toLatin1().constData()))
            return false;
    }
    return f.commit();
}

ScanResult processAndSave(const QByteArray &jpeg, const ScanOptions &opt)
{
    return processAndSavePages(jpeg, opt).value(0);
}

QVector<ScanResult> processAndSavePages(const QByteArray &jpeg, const ScanOptions &opt)
{
    ScanResult r;
    if (!QDir().mkpath(opt.outputDir)) {
        r.error = QObject::tr("Cannot create the folder %1").arg(opt.outputDir);
        return {r};
    }
    const QString format = scanSuffix(opt.imageFormat);
    const bool untouched = opt.pageMode == PageMode::Original && opt.colorMode == ColorMode::None &&
                           opt.rotation == 0 && format == QLatin1String("jpg");
    if (untouched) {
        r.path = newScanPath(opt, QStringLiteral("jpg"));
        const QByteArray data = jpegWithDpi(jpeg, scanDpi(opt, jpegSize(jpeg).width()));
        QSaveFile f(r.path);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
            r.error = QObject::tr("Cannot write %1").arg(r.path);
            return {r};
        }
        r.thumbnail = decodeJpeg(jpeg, 256).scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return {r};
    }
    const QImage frame = decodeJpeg(jpeg);
    if (frame.isNull()) {
        r.error = QObject::tr("The camera image could not be decoded.");
        return {r};
    }
    // Black & white pages compress far better (and losslessly) as 1-bit PNG.
    const QString suffix =
        opt.colorMode == ColorMode::BlackWhite && format == QLatin1String("jpg") ? QStringLiteral("png") : format;
    QString original;
    if (opt.keepOriginal && QDir().mkpath(originalsDir(opt.outputDir))) {
        const QString dir = originalsDir(opt.outputDir);
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
        QString path = QDir(dir).filePath(stamp + QStringLiteral(".jpg"));
        for (int i = 2; QFileInfo::exists(path); ++i)
            path = QDir(dir).filePath(QStringLiteral("%1_%2.jpg").arg(stamp).arg(i));
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly) && f.write(jpeg) == jpeg.size() && f.commit())
            original = QFileInfo(path).fileName();
    }
    QVector<ScanResult> results;
    for (QImage out : processScanPages(frame, opt)) {
        if (!original.isEmpty()) {
            out.setText(QStringLiteral("LensScanOriginal"), original);
            out.setText(QStringLiteral("LensScanRotation"), QString::number(opt.rotation));
        }
        ScanResult page;
        page.path = newScanPath(opt, suffix);
        if (!saveImage(out, page.path, opt.jpegQuality, scanDpi(opt, frame.width()))) {
            page.error = QObject::tr("Cannot write %1").arg(page.path);
            page.path.clear();
            return {page};
        }
        page.thumbnail = out.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        results << page;
    }
    return results;
}

} // namespace lensscan
