// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppSettings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace lensscan {

QString AppSettings::defaultOutputDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("LensScan"));
}

AppSettings AppSettings::load()
{
    QSettings s;
    AppSettings a;
    a.outputDir = s.value(QStringLiteral("output/dir"), defaultOutputDir()).toString();
    a.filePrefix = s.value(QStringLiteral("output/prefix"), a.filePrefix).toString();
    a.jpegQuality = qBound(50, s.value(QStringLiteral("output/jpegQuality"), a.jpegQuality).toInt(), 100);
    a.namePattern = s.value(QStringLiteral("output/namePattern")).toString().left(40);
    a.nameDigits = qBound(1, s.value(QStringLiteral("output/nameDigits"), a.nameDigits).toInt(), 9);
    a.imageFormat = scanSuffix(s.value(QStringLiteral("output/format"), a.imageFormat).toString());
    a.dpi = s.value(QStringLiteral("output/dpi"), a.dpi).toInt();
    if (a.dpi != 0)
        a.dpi = qBound(72, a.dpi, 1000);
    a.highCamera = s.value(QStringLiteral("camera/highPosition"), false).toBool();
    a.keepOriginals = s.value(QStringLiteral("output/keepOriginals"), true).toBool();
    a.colorMode = colorModeFromKey(s.value(QStringLiteral("scan/colorMode")).toString());
    a.pageMode = pageModeFromKey(s.value(QStringLiteral("scan/pageMode")).toString());
    a.rotation = (s.value(QStringLiteral("scan/rotation"), 0).toInt() / 90 % 4) * 90;
    a.bookSplit = s.value(QStringLiteral("book/split"), a.bookSplit).toBool();
    a.bookRightFirst = s.value(QStringLiteral("book/rightFirst"), a.bookRightFirst).toBool();
    a.removeFingers = s.value(QStringLiteral("book/removeFingers"), a.removeFingers).toBool();
    a.captureSize = s.value(QStringLiteral("scan/captureSize")).toSize();
    a.autoOrient = s.value(QStringLiteral("flat/autoOrient"), a.autoOrient).toBool();
    a.roundCorners = s.value(QStringLiteral("flat/roundCorners"), a.roundCorners).toBool();
    a.completePage = s.value(QStringLiteral("flat/completePage"), a.completePage).toBool();
    a.videoSize = s.value(QStringLiteral("presentation/videoSize")).toSize();
    const auto pick = [&s](const char *key, const QString &def, std::initializer_list<const char *> allowed) {
        const QString v = s.value(QLatin1String(key), def).toString().toLower();
        for (const char *a : allowed)
            if (v == QLatin1String(a))
                return v;
        return def;
    };
    a.snapshotDir = s.value(QStringLiteral("presentation/snapshotDir")).toString();
    a.videoDir = s.value(QStringLiteral("presentation/videoDir")).toString();
    a.screenDir = s.value(QStringLiteral("presentation/screenDir")).toString();
    a.snapshotFormat = pick("presentation/snapshotFormat", a.snapshotFormat, {"png", "jpg", "bmp"});
    a.videoFormat = pick("presentation/videoFormat", a.videoFormat, {"mp4", "mkv"});
    a.screenFormat = pick("presentation/screenFormat", a.screenFormat, {"mp4", "mkv"});
    a.beep = s.value(QStringLiteral("scan/beep"), a.beep).toBool();
    a.pdfPageSize = s.value(QStringLiteral("pdf/pageSize")).toString() == QLatin1String("a4")
                        ? PdfOptions::PageSize::A4
                        : PdfOptions::PageSize::FromImage;
    a.pdfDpi = qBound(72, s.value(QStringLiteral("pdf/dpi"), a.pdfDpi).toInt(), 1200);
    a.language = s.value(QStringLiteral("ui/language")).toString();
    a.ocrLanguages = s.value(QStringLiteral("ocr/languages")).toString();
    a.orderBeforeExport = s.value(QStringLiteral("documents/orderBeforeExport"), false).toBool();
    a.cameraControls = s.value(QStringLiteral("camera/controls")).toMap();
    if (a.filePrefix.trimmed().isEmpty())
        a.filePrefix = QStringLiteral("Scan");
    return a;
}

void AppSettings::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("output/dir"), outputDir);
    s.setValue(QStringLiteral("output/prefix"), filePrefix);
    s.setValue(QStringLiteral("output/jpegQuality"), jpegQuality);
    s.setValue(QStringLiteral("output/namePattern"), namePattern);
    s.setValue(QStringLiteral("output/nameDigits"), nameDigits);
    s.setValue(QStringLiteral("output/format"), imageFormat);
    s.setValue(QStringLiteral("output/dpi"), dpi);
    s.setValue(QStringLiteral("camera/highPosition"), highCamera);
    s.setValue(QStringLiteral("output/keepOriginals"), keepOriginals);
    s.setValue(QStringLiteral("presentation/snapshotDir"), snapshotDir);
    s.setValue(QStringLiteral("presentation/videoDir"), videoDir);
    s.setValue(QStringLiteral("presentation/screenDir"), screenDir);
    s.setValue(QStringLiteral("presentation/snapshotFormat"), snapshotFormat);
    s.setValue(QStringLiteral("presentation/videoFormat"), videoFormat);
    s.setValue(QStringLiteral("presentation/screenFormat"), screenFormat);
    s.setValue(QStringLiteral("scan/colorMode"), colorModeKey(colorMode));
    s.setValue(QStringLiteral("scan/pageMode"), pageModeKey(pageMode));
    s.setValue(QStringLiteral("scan/rotation"), rotation);
    s.setValue(QStringLiteral("book/split"), bookSplit);
    s.setValue(QStringLiteral("book/rightFirst"), bookRightFirst);
    s.setValue(QStringLiteral("book/removeFingers"), removeFingers);
    s.setValue(QStringLiteral("scan/captureSize"), captureSize);
    s.setValue(QStringLiteral("flat/autoOrient"), autoOrient);
    s.setValue(QStringLiteral("flat/roundCorners"), roundCorners);
    s.setValue(QStringLiteral("flat/completePage"), completePage);
    s.setValue(QStringLiteral("presentation/videoSize"), videoSize);
    s.setValue(QStringLiteral("scan/beep"), beep);
    s.setValue(QStringLiteral("pdf/pageSize"),
               pdfPageSize == PdfOptions::PageSize::A4 ? QStringLiteral("a4") : QStringLiteral("image"));
    s.setValue(QStringLiteral("pdf/dpi"), pdfDpi);
    s.setValue(QStringLiteral("ui/language"), language);
    s.setValue(QStringLiteral("ocr/languages"), ocrLanguages);
    s.setValue(QStringLiteral("documents/orderBeforeExport"), orderBeforeExport);
    s.setValue(QStringLiteral("camera/controls"), cameraControls);
}

ScanOptions AppSettings::scanOptions() const
{
    ScanOptions o;
    o.colorMode = colorMode;
    o.pageMode = pageMode;
    o.rotation = rotation;
    o.bookSplit = bookSplit;
    o.bookRightFirst = bookRightFirst;
    o.removeFingers = removeFingers;
    o.autoOrient = autoOrient;
    o.roundCorners = roundCorners;
    o.completePage = completePage;
    o.jpegQuality = jpegQuality;
    o.outputDir = outputDir;
    o.filePrefix = filePrefix;
    o.namePattern = namePattern;
    o.nameDigits = nameDigits;
    o.imageFormat = imageFormat;
    o.dpi = dpi;
    o.highCamera = highCamera;
    o.keepOriginal = keepOriginals;
    return o;
}

} // namespace lensscan
