// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent user preferences (QSettings, ~/.config/LensScan/LensScan.conf).
#pragma once

#include "export/Exporters.h"
#include "imaging/ScanProcessor.h"

#include <QSize>
#include <QString>
#include <QVariantMap>

namespace lensscan {

struct AppSettings {
    QString outputDir;
    QString filePrefix = QStringLiteral("Scan");
    int jpegQuality = 92;
    QString namePattern;  // empty = prefix, date and time; otherwise with '#' = number
    int nameDigits = 4;
    QString imageFormat = QStringLiteral("jpg");
    int dpi = 300;        // 0 = automatic, from the camera height
    bool highCamera = false; // camera arm at the A3 height
    bool keepOriginals = true; // keep camera pictures to process scans again
    ColorMode colorMode = ColorMode::Auto;
    PageMode pageMode = PageMode::FlatPage;
    int rotation = 0;
    QSize captureSize; // empty = largest
    bool beep = true;
    bool bookSplit = true;
    bool bookRightFirst = false;
    bool removeFingers = true;
    bool autoOrient = false;
    bool roundCorners = false;
    bool completePage = false;
    QSize videoSize; // presentation stream size, empty = automatic
    // Presentation output; empty folders = the scan folder.
    QString snapshotDir, videoDir, screenDir;
    QString snapshotFormat = QStringLiteral("png"); // png, jpg, bmp
    QString videoFormat = QStringLiteral("mp4");    // mp4, mkv
    QString screenFormat = QStringLiteral("mp4");
    PdfOptions::PageSize pdfPageSize = PdfOptions::PageSize::FromImage;
    int pdfDpi = 300;
    QString language; // empty = system
    bool orderBeforeExport = false; // page order window before PDF / TIFF / OCR
    QString ocrLanguages; // Tesseract syntax, "fra+eng"; empty = automatic
    QVariantMap cameraControls; // V4L2 control name -> value

    static QString defaultOutputDir();
    static AppSettings load();
    void save() const;
    ScanOptions scanOptions() const;
    QString snapshotFolder() const { return snapshotDir.isEmpty() ? outputDir : snapshotDir; }
    QString videoFolder() const { return videoDir.isEmpty() ? outputDir : videoDir; }
    QString screenFolder() const { return screenDir.isEmpty() ? outputDir : screenDir; }
};

} // namespace lensscan
