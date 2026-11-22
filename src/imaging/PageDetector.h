// SPDX-License-Identifier: GPL-3.0-or-later
// Finds the sheet of paper lying under the camera and flattens it with a
// perspective transform (auto-crop + deskew). Pure OpenCV, no learned model.
#pragma once

#include <QImage>
#include <QPointF>
#include <QPolygonF>

#include <opencv2/core.hpp>

namespace lensscan {

// Wraps a QImage (RGB888 or Grayscale8) without copying.
cv::Mat matView(const QImage &img);
QImage matToImage(const cv::Mat &m); // deep copy

class PageDetector {
public:
    // Returns 4 corners in image coordinates ordered TL, TR, BR, BL, or an empty
    // polygon when no page-like quadrilateral covers at least minAreaRatio of the frame.
    static QPolygonF detect(const QImage &image, double minAreaRatio = 0.08);
    static QImage warp(const QImage &image, const QPolygonF &quad);
};

} // namespace lensscan
