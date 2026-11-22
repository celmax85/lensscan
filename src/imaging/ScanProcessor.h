// SPDX-License-Identifier: GPL-3.0-or-later
// Turns a raw camera JPEG into the saved scan: page handling (auto-crop, area,
// original), rotation, color mode, then writes the file. Runs off the GUI thread.
#pragma once

#include "ColorModes.h"

#include <QByteArray>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>

namespace lensscan {

enum class PageMode { FlatPage, Book, Area, Original, MultiDocument };

QString pageModeKey(PageMode m);
PageMode pageModeFromKey(const QString &key);

struct ScanOptions {
    ColorMode colorMode = ColorMode::Auto;
    PageMode pageMode = PageMode::FlatPage;
    QRectF area;          // normalized 0..1 in the rotated (displayed) frame, Area mode
    int rotation = 0;     // degrees clockwise: 0, 90, 180, 270
    int jpegQuality = 92;
    QString outputDir;
    QString filePrefix = QStringLiteral("Scan");
    // File naming: empty = "<prefix>_<date>_<time>_<n>"; otherwise a pattern where
    // '#' is replaced by the next free serial number ("Invoice_#" -> Invoice_0007.jpg).
    QString namePattern;
    int nameDigits = 4;
    QString imageFormat = QStringLiteral("jpg"); // "jpg", "png" or "bmp"
    int dpi = 300; // resolution written into the files; 0 = from the camera height
    bool highCamera = false; // camera arm at the A3 height (larger view, lower resolution)
    bool keepOriginal = true; // keep the camera picture, to reprocess the scan later
    // Book mode
    bool bookSplit = true;       // one file per page; otherwise both pages side by side
    bool bookRightFirst = false; // right-to-left books
    bool removeFingers = true;
    // Flat page and multiple documents
    bool autoOrient = false;   // turn pages whose text is sideways or upside down
    bool roundCorners = false; // white corners for rounded cards (no desk in the corners)
    bool completePage = false; // fill torn or missing corners and edges with paper
};

struct ScanResult {
    QString path;
    QImage thumbnail; // ~256 px
    QString error;
};

QImage rotateImage(const QImage &img, int degreesClockwise);

// Applies page mode, rotation and color mode to a decoded frame. Book mode can give
// two pages; the other modes give one image.
QVector<QImage> processScanPages(const QImage &frame, const ScanOptions &opt);
QImage processScan(const QImage &frame, const ScanOptions &opt); // first page only

// Decodes, processes and saves in opt.imageFormat; black & white results are saved as
// PNG instead of JPEG. Unprocessed JPEG scans keep the camera's bytes.
QVector<ScanResult> processAndSavePages(const QByteArray &jpeg, const ScanOptions &opt);
ScanResult processAndSave(const QByteArray &jpeg, const ScanOptions &opt); // first page only

// Writes img in the format of the path's extension. dpi > 0 sets the resolution
// stored in the file; 0 keeps the one the image was read with, if any.
bool saveImage(const QImage &img, const QString &path, int jpegQuality, int dpi = 0);

void setImageDpi(QImage &img, int dpi);
int imageDpi(const QImage &img); // 0 when the image carries no resolution

// Both pages side by side on a white background, aligned at the top.
QImage joinPages(const QVector<QImage> &pages);

// Camera pictures kept so a scan can be processed again later in another page mode.
// They live in <scan folder>/.lensscan/originals; a scan names its picture in the
// "LensScanOriginal" text field of the file (JPEG comment, PNG text chunk).
QString originalsDir(const QString &scanDir);
// The camera picture a scan was made from and the rotation it was scanned with;
// empty when none was kept.
QString originalOf(const QString &scanPath, int *rotation = nullptr);

// Resolution of a scan: opt.dpi, or with dpi 0 an estimate from the width of the
// camera's view at its arm height (about 330 mm at the A4 height, 460 mm at A3).
int scanDpi(const ScanOptions &opt, int frameWidth);

// "<prefix>_<date>_<time>_<n>.<suffix>" that does not exist yet.
QString uniqueScanPath(const QString &dir, const QString &prefix, const QString &suffix);
// The path for a new scan following opt's naming rule. The name is reserved for this
// process, so scans saved at the same time never get the same name.
QString newScanPath(const ScanOptions &opt, const QString &suffix);
// File name a pattern gives for a number: ("Invoice_#", 4, 7) -> "Invoice_0007".
QString patternName(const QString &pattern, int digits, int number);
// The scan format's suffix: "jpg", "png" or "bmp" (anything else gives "jpg").
QString scanSuffix(const QString &format);

} // namespace lensscan
