// SPDX-License-Identifier: GPL-3.0-or-later
// Batch tools of the file manager: blank page detection, brightness / contrast /
// sharpness adjustment, text watermark, edge trimming, crop, renaming and import.
#pragma once

#include <QColor>
#include <QImage>
#include <QPair>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QPolygonF>
#include <QVector>

namespace lensscan {

// Fraction of the page (0..1) covered by ink: marks clearly darker than the local
// paper, ignoring a 4% margin (shadows, book edges) and isolated specks.
double inkCoverage(const QImage &page);
// sensitivity 0..100: higher finds more pages blank (accepts more specks).
bool isBlankPage(const QImage &page, int sensitivity = 50);

struct Adjustment {
    int brightness = 0; // -100..100
    int contrast = 0;   // -100..100
    int sharpness = 0;  // 0..100 (unsharp mask strength)
    bool isNull() const { return brightness == 0 && contrast == 0 && sharpness == 0; }
};
QImage adjustImage(const QImage &image, const Adjustment &a);

enum class WatermarkPosition { Center, Diagonal, TopLeft, TopRight, BottomLeft, BottomRight, Tiled };
struct Watermark {
    QString text;
    QImage image;          // when set, used instead of the text (logo, signature, stamp)
    int imageWidthPercent = 25; // image width as % of the page width
    QColor color = QColor(200, 30, 30);
    int opacity = 35;      // percent
    int sizePercent = 6;   // text height as % of the page height
    WatermarkPosition position = WatermarkPosition::Diagonal;
};
QImage addWatermark(const QImage &image, const Watermark &w);

// Overlapping edge cropping for book pages: keeps `width` pixels measured from the
// spine side (the right edge of a left-hand page, the left edge of a right-hand page).
QImage cropToPageWidth(const QImage &page, int width, bool leftHandPage);

// Several documents on one photo (receipts, cards, photos): their outlines, largest
// first, and the straightened crops.
QVector<QPolygonF> detectDocuments(const QImage &image, int maxDocuments = 12);
QVector<QImage> splitDocuments(const QImage &image);

// Removes n pixels from each side (keeps at least 16 x 16).
QImage trimEdges(const QImage &image, int pixels);
// normalized: crop rectangle in 0..1 page coordinates.
QImage cropNormalized(const QImage &image, const QRectF &normalized);

// "prefix" + zero-padded serial number + original extension, in the given order.
QStringList renamedPaths(const QStringList &files, const QString &prefix, int start, int digits);
// Renames in two steps (through temporary names) so swapping names never collides.
// Returns an empty string on success, otherwise the error.
QString renameFiles(const QStringList &files, const QStringList &newPaths);

// Copies images into dir under unused names; formats other than JPEG / PNG / BMP
// (TIFF, WebP...) are converted to PNG. Returns the new paths; failures go to *errors.
QStringList importImages(const QStringList &files, const QString &dir, QStringList *errors = nullptr);

} // namespace lensscan
