// SPDX-License-Identifier: GPL-3.0-or-later
// Page clean-up tools: handwriting removal, punch hole filling, paper corner
// completion (rounded ID card corners, torn page corners), mirror flip, and the file
// naming used to insert pages at a given place of the scan folder.
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lensscan {

struct HandwritingOptions {
    bool blue = true;   // ballpoint / fountain pen ink
    bool red = false;   // red pen; also removes red stamps
    bool green = false;
    int strength = 50;  // 0..100: higher also removes paler, less saturated strokes
    bool cleanBackground = false; // whitens a tinted or unevenly lit paper afterwards
};
// Erases colored pen strokes and replaces them with the surrounding paper. Black and
// gray printed text is kept, also where a stroke crosses it. Black pen cannot be told
// apart from print by color and is kept.
QImage eraseHandwriting(const QImage &image, const HandwritingOptions &o);

// Fills round punch holes (dark disks near the page edges, also cut by the edge)
// whose radius in pixels is within [minRadius, maxRadius]. *found gets their number.
QImage fillPunchHoles(const QImage &image, int minRadius, int maxRadius, int *found = nullptr);

// Paints dark background (desk, mat) that shows inside a cropped page with the paper
// color: only in the four corners (rounded ID cards) or, with wholeBorder, anywhere
// along the edges (torn or missing corners and edges).
QImage fillPaperCorners(const QImage &page, bool wholeBorder);

QImage flipImage(const QImage &image, bool horizontal);

// Precise crop: pixels removed from each side. With cut = false the image keeps its
// size and the margins are painted white instead.
struct Margins {
    int top = 0, bottom = 0, left = 0, right = 0;
    bool isNull() const { return top == 0 && bottom == 0 && left == 0 && right == 0; }
};
QImage cropMargins(const QImage &image, const Margins &m, bool cut);

// Two facing pages on one image (an open book or magazine, a double page): the pages,
// left one first. Flattened when an open book is found; otherwise the image is cut at
// the gutter (the darkest vertical band near the middle). One image when the picture
// is clearly a single page (portrait).
QVector<QImage> splitFacingPages(const QImage &image, bool removeFingers = false);

// Order of the file list: case-insensitive by name, case-sensitive on ties.
bool scanNameLess(const QString &a, const QString &b);
// File names for `suffixes.size()` new pages (suffix with the dot, ".jpg") that sort
// right before sortedNames[index] (index == size(): at the end), in that order, and
// do not exist yet. Empty when no such names were found.
QStringList insertionNames(const QStringList &sortedNames, int index, const QStringList &suffixes);

} // namespace lensscan
