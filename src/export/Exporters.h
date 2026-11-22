// SPDX-License-Identifier: GPL-3.0-or-later
// Document output: PDF (one page per image), multi-page TIFF, image conversion.
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>

namespace lensscan {

// Return false from the callback to cancel. Argument: number of files done.
using ExportProgress = std::function<bool(int done)>;

struct PdfOptions {
    enum class PageSize { FromImage, A4 } pageSize = PageSize::FromImage;
    int dpi = 300;  // used with FromImage: physical size = pixels / dpi
    QString title;
};

// Loads a scan as RGB888, or Grayscale8 when it has no color.
QImage loadScan(const QString &path);
bool isBinaryGray(const QImage &img);

bool exportPdf(const QStringList &files, const QString &outPath, const PdfOptions &opt,
               const ExportProgress &progress, QString *error);
struct TiffOptions {
    // Auto: Group 4 for black & white pages, JPEG for the others. Group4 turns every
    // page into black & white.
    enum class Compression { Auto, None, Lzw, Jpeg, Group4 } compression = Compression::Auto;
    bool gray = false;      // convert color pages to grayscale
    bool multiPage = true;  // one file; otherwise <name>_001.tif, <name>_002.tif...
    int jpegQuality = 90;
    int dpi = 300;          // for pages that carry no resolution
};
// Paths of the files exportTiff writes for n pages.
QStringList tiffOutputPaths(const QString &outPath, int pages, bool multiPage);
bool exportTiff(const QStringList &files, const QString &outPath, const TiffOptions &opt,
                const ExportProgress &progress, QString *error);
// format: "png", "jpg" or "bmp". Files are written into outDir with the same base name.
bool exportImages(const QStringList &files, const QString &outDir, const QString &format, int jpegQuality,
                  const ExportProgress &progress, QString *error);

} // namespace lensscan
