// SPDX-License-Identifier: GPL-3.0-or-later
#include "Exporters.h"

#include "imaging/ScanProcessor.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QObject>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

#include <tiffio.h>

#include <vector>

namespace lensscan {

QImage loadScan(const QString &path)
{
    QImageReader r(path);
    r.setAutoTransform(true);
    QImage img = r.read();
    if (img.isNull())
        return img;
    if (img.isGrayscale() || img.format() == QImage::Format_Mono)
        return img.convertToFormat(QImage::Format_Grayscale8);
    return img.convertToFormat(QImage::Format_RGB888);
}

bool isBinaryGray(const QImage &img)
{
    if (img.format() != QImage::Format_Grayscale8)
        return false;
    for (int y = 0; y < img.height(); ++y) {
        const uchar *p = img.constScanLine(y);
        for (int x = 0; x < img.width(); ++x)
            if (p[x] != 0 && p[x] != 255)
                return false;
    }
    return true;
}

static bool fail(QString *error, const QString &msg)
{
    if (error)
        *error = msg;
    return false;
}

bool exportPdf(const QStringList &files, const QString &outPath, const PdfOptions &opt,
               const ExportProgress &progress, QString *error)
{
    if (files.isEmpty())
        return fail(error, QObject::tr("No file selected."));
    QPdfWriter pdf(outPath);
    pdf.setCreator(QStringLiteral("LensScan"));
    pdf.setTitle(opt.title.isEmpty() ? QFileInfo(outPath).completeBaseName() : opt.title);
    pdf.setResolution(opt.dpi);
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter;
    for (int i = 0; i < files.size(); ++i) {
        const QImage img = loadScan(files[i]);
        if (img.isNull())
            return fail(error, QObject::tr("Cannot read %1").arg(files[i]));
        QSizeF mm;
        if (opt.pageSize == PdfOptions::PageSize::A4)
            mm = img.width() > img.height() ? QSizeF(297, 210) : QSizeF(210, 297);
        else
            mm = QSizeF(img.width() * 25.4 / opt.dpi, img.height() * 25.4 / opt.dpi);
        pdf.setPageSize(QPageSize(mm, QPageSize::Millimeter, QString(), QPageSize::ExactMatch));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        if (i == 0) {
            if (!painter.begin(&pdf))
                return fail(error, QObject::tr("Cannot write %1").arg(outPath));
        } else {
            pdf.newPage();
        }
        const QRect page(0, 0, pdf.width(), pdf.height());
        const QSize fitted = img.size().scaled(page.size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((page.width() - fitted.width()) / 2, (page.height() - fitted.height()) / 2), fitted);
        painter.drawImage(target, img);
        if (progress && !progress(i + 1)) {
            painter.end();
            QFile::remove(outPath);
            return fail(error, QObject::tr("Cancelled."));
        }
    }
    painter.end();
    return true;
}

QStringList tiffOutputPaths(const QString &outPath, int pages, bool multiPage)
{
    if (multiPage)
        return {outPath};
    const QFileInfo fi(outPath);
    QString suffix = fi.suffix();
    if (suffix.isEmpty())
        suffix = QStringLiteral("tif");
    QStringList out;
    for (int i = 1; i <= pages; ++i)
        out << fi.dir().filePath(QStringLiteral("%1_%2.%3").arg(fi.completeBaseName()).arg(i, 3, 10, QLatin1Char('0')).arg(suffix));
    return out;
}

namespace {

// Writes one page into the current TIFF directory.
bool writeTiffPage(TIFF *tif, QImage img, const TiffOptions &opt, int page, int pages)
{
    using C = TiffOptions::Compression;
    const int dpi = imageDpi(img) > 0 ? imageDpi(img) : opt.dpi;
    if (opt.compression == C::Group4 && !isBinaryGray(img))
        img = applyColorMode(img, ColorMode::BlackWhite);
    if (opt.gray && img.format() != QImage::Format_Grayscale8)
        img = img.convertToFormat(QImage::Format_Grayscale8);
    const bool bw = isBinaryGray(img) && opt.compression != C::Jpeg;
    const bool gray = img.format() == QImage::Format_Grayscale8;
    const uint32_t w = uint32_t(img.width()), h = uint32_t(img.height());
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, w);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, h);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
    TIFFSetField(tif, TIFFTAG_XRESOLUTION, float(dpi));
    TIFFSetField(tif, TIFFTAG_YRESOLUTION, float(dpi));
    if (pages > 1) {
        TIFFSetField(tif, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
        TIFFSetField(tif, TIFFTAG_PAGENUMBER, uint16_t(page), uint16_t(pages));
    }
    TIFFSetField(tif, TIFFTAG_SOFTWARE, "LensScan");
    bool ok = true;
    if (bw) {
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 1);
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISWHITE);
        TIFFSetField(tif, TIFFTAG_COMPRESSION, opt.compression == C::None  ? COMPRESSION_NONE
                                               : opt.compression == C::Lzw ? COMPRESSION_LZW
                                                                           : COMPRESSION_CCITTFAX4);
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, opt.compression == C::None || opt.compression == C::Lzw ? 64 : h);
        std::vector<uint8_t> row((w + 7) / 8);
        for (uint32_t y = 0; y < h && ok; ++y) {
            std::fill(row.begin(), row.end(), 0);
            const uchar *p = img.constScanLine(int(y));
            for (uint32_t x = 0; x < w; ++x)
                if (p[x] == 0) // black = 1 with MINISWHITE
                    row[x >> 3] |= uint8_t(0x80 >> (x & 7));
            ok = TIFFWriteScanline(tif, row.data(), y, 0) >= 0;
        }
        return ok;
    }
    if (!gray && img.format() != QImage::Format_RGB888)
        img = img.convertToFormat(QImage::Format_RGB888);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, gray ? 1 : 3);
    if (opt.compression == C::None || opt.compression == C::Lzw) {
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, gray ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB);
        TIFFSetField(tif, TIFFTAG_COMPRESSION, opt.compression == C::Lzw ? COMPRESSION_LZW : COMPRESSION_NONE);
        if (opt.compression == C::Lzw)
            TIFFSetField(tif, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL);
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 32);
    } else {
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, gray ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_YCBCR);
        TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_JPEG);
        TIFFSetField(tif, TIFFTAG_JPEGQUALITY, opt.jpegQuality);
        if (!gray)
            TIFFSetField(tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 16);
    }
    for (uint32_t y = 0; y < h && ok; ++y)
        ok = TIFFWriteScanline(tif, const_cast<uchar *>(img.constScanLine(int(y))), y, 0) >= 0;
    return ok;
}

} // namespace

bool exportTiff(const QStringList &files, const QString &outPath, const TiffOptions &opt,
                const ExportProgress &progress, QString *error)
{
    if (files.isEmpty())
        return fail(error, QObject::tr("No file selected."));
    const QStringList outs = tiffOutputPaths(outPath, int(files.size()), opt.multiPage);
    TIFF *tif = nullptr;
    const auto abort = [&](const QString &msg) {
        if (tif)
            TIFFClose(tif);
        for (const QString &o : outs)
            QFile::remove(o);
        return fail(error, msg);
    };
    for (int i = 0; i < files.size(); ++i) {
        const QString out = opt.multiPage ? outs.first() : outs[i];
        if (!tif) {
            tif = TIFFOpen(QFile::encodeName(out).constData(), "w");
            if (!tif)
                return abort(QObject::tr("Cannot write %1").arg(out));
        }
        const QImage img = loadScan(files[i]);
        if (img.isNull())
            return abort(QObject::tr("Cannot read %1").arg(files[i]));
        if (!writeTiffPage(tif, img, opt, i, opt.multiPage ? int(files.size()) : 1) || !TIFFWriteDirectory(tif))
            return abort(QObject::tr("Cannot write %1").arg(out));
        if (!opt.multiPage) {
            TIFFClose(tif);
            tif = nullptr;
        }
        if (progress && !progress(i + 1))
            return abort(QObject::tr("Cancelled."));
    }
    if (tif)
        TIFFClose(tif);
    return true;
}

bool exportImages(const QStringList &files, const QString &outDir, const QString &format, int jpegQuality,
                  const ExportProgress &progress, QString *error)
{
    if (!QDir().mkpath(outDir))
        return fail(error, QObject::tr("Cannot create the folder %1").arg(outDir));
    for (int i = 0; i < files.size(); ++i) {
        const QImage img = loadScan(files[i]);
        if (img.isNull())
            return fail(error, QObject::tr("Cannot read %1").arg(files[i]));
        QString target = QDir(outDir).filePath(QFileInfo(files[i]).completeBaseName() + QLatin1Char('.') + format);
        if (QFileInfo(target) == QFileInfo(files[i]))
            continue; // already in that format, same place
        if (QFileInfo::exists(target))
            target = uniqueScanPath(outDir, QFileInfo(files[i]).completeBaseName(), format);
        if (!saveImage(img, target, jpegQuality))
            return fail(error, QObject::tr("Cannot write %1").arg(target));
        if (progress && !progress(i + 1))
            return fail(error, QObject::tr("Cancelled."));
    }
    return true;
}

} // namespace lensscan
