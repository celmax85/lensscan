// SPDX-License-Identifier: GPL-3.0-or-later
// Open-book scanning: finds the two facing pages, flattens their curvature, splits
// them at the spine and erases the fingers holding the pages.
//
// Method (our own, written for LensScan): the book is segmented from the desk, turned
// so its spine is vertical, and the top and bottom page edges are fitted with
// polynomials on each side of the spine (found where the book is thinnest). Under a
// pinhole camera the apparent page height at column x is h(x) = f*H/(D - Z(x)), so the
// page surface can be rebuilt in 3D from h(x) alone: X = (x - cx)/h, Z = -f/h (in units
// of the page height). Each page is then resampled along that surface's arc length,
// which restores both straight text lines and the true page width near the spine.
#pragma once

#include <QImage>
#include <QLineF>
#include <QPolygonF>
#include <QVector>

namespace lensscan {

struct BookOptions {
    bool removeFingers = true;
    // Camera focal length in pixels of the image; 0 = the CZUR Lens default
    // (about 0.95 x image width, i.e. a ~55 degree horizontal field of view).
    double focalPx = 0;
};

struct BookGeometry {
    bool valid = false;
    double angle = 0;          // degrees the image is rotated to make the spine vertical
    // Outlines in the original image coordinates, for the live preview overlay.
    QPolygonF leftPage, rightPage;
    QLineF spine;
};

// Fast enough for the live preview on a ~800 px frame.
BookGeometry detectBook(const QImage &image);

// Returns the flattened pages, left page first; empty when no book was found.
QVector<QImage> processBook(const QImage &image, const BookOptions &opt = {});

// Fills skin-coloured blobs that touch the outer edge of a page with the paper colour.
// outerLeft: the page's outer edge is on its left (a left-hand page). Returns the
// number of pixels erased.
int removeFingers(QImage &page, bool outerLeft);

} // namespace lensscan
