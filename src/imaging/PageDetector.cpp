// SPDX-License-Identifier: GPL-3.0-or-later
#include "PageDetector.h"

#include <opencv2/imgproc.hpp>

#include <cstring>

#include <algorithm>
#include <cmath>
#include <functional>

namespace lensscan {

cv::Mat matView(const QImage &img)
{
    const int type = img.format() == QImage::Format_Grayscale8 ? CV_8UC1 : CV_8UC3;
    return cv::Mat(img.height(), img.width(), type, const_cast<uchar *>(img.constBits()),
                   size_t(img.bytesPerLine()));
}

QImage matToImage(const cv::Mat &m)
{
    if (m.type() == CV_8UC1) {
        QImage out(m.cols, m.rows, QImage::Format_Grayscale8);
        for (int y = 0; y < m.rows; ++y)
            std::memcpy(out.scanLine(y), m.ptr(y), size_t(m.cols));
        return out;
    }
    QImage out(m.cols, m.rows, QImage::Format_RGB888);
    for (int y = 0; y < m.rows; ++y)
        std::memcpy(out.scanLine(y), m.ptr(y), size_t(m.cols) * 3);
    return out;
}

namespace {

std::vector<cv::Point2f> orderCorners(std::vector<cv::Point2f> p)
{
    // TL has the smallest x+y, BR the largest; TR the smallest y-x, BL the largest.
    std::vector<cv::Point2f> o(4);
    auto sum = [](const cv::Point2f &a) { return a.x + a.y; };
    auto diff = [](const cv::Point2f &a) { return a.y - a.x; };
    o[0] = *std::min_element(p.begin(), p.end(), [&](auto &a, auto &b) { return sum(a) < sum(b); });
    o[2] = *std::max_element(p.begin(), p.end(), [&](auto &a, auto &b) { return sum(a) < sum(b); });
    o[1] = *std::min_element(p.begin(), p.end(), [&](auto &a, auto &b) { return diff(a) < diff(b); });
    o[3] = *std::max_element(p.begin(), p.end(), [&](auto &a, auto &b) { return diff(a) < diff(b); });
    return o;
}

bool quadFromContour(const std::vector<cv::Point> &c, double minArea, std::vector<cv::Point2f> &out, bool allowRect)
{
    const double area = cv::contourArea(c);
    if (area < minArea)
        return false;
    std::vector<cv::Point> approx;
    const double peri = cv::arcLength(c, true);
    for (double eps : {0.02, 0.03, 0.05}) {
        cv::approxPolyDP(c, approx, eps * peri, true);
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            out.assign(approx.begin(), approx.end());
            return true;
        }
    }
    if (!allowRect)
        return false;
    // Not a clean quadrilateral (hands, curled corner): fall back to the rotated box,
    // but only if the contour fills most of it.
    const cv::RotatedRect r = cv::minAreaRect(c);
    if (area < 0.8 * r.size.area())
        return false;
    cv::Point2f pts[4];
    r.points(pts);
    out.assign(pts, pts + 4);
    return true;
}

// A quadrilateral with a corner on the frame border is the frame itself or a page
// that does not fit in the view: neither can be cropped reliably.
bool hugsFrame(const std::vector<cv::Point2f> &q, const cv::Size &s)
{
    const float mx = 0.012f * s.width, my = 0.012f * s.height;
    int l = 0, t = 0, r = 0, b = 0;
    for (const auto &p : q) {
        l += p.x < mx;
        t += p.y < my;
        r += p.x > s.width - 1 - mx;
        b += p.y > s.height - 1 - my;
    }
    return l + t + r + b > 0;
}

// The camera looks straight down, so a real sheet stays close to a rectangle.
bool roughlyRectangular(const std::vector<cv::Point2f> &q)
{
    const auto o = orderCorners(q);
    for (int i = 0; i < 4; ++i) {
        const cv::Point2f a = o[(i + 3) % 4] - o[i], b = o[(i + 1) % 4] - o[i];
        const double cosang = a.dot(b) / (std::hypot(a.x, a.y) * std::hypot(b.x, b.y) + 1e-9);
        if (std::abs(cosang) > 0.42) // outside 65..115 degrees
            return false;
    }
    return true;
}

// Turns a convex hull into a quadrilateral if it is close enough to one.
bool hullToQuad(const std::vector<cv::Point> &hull, std::vector<cv::Point2f> &out)
{
    const double peri = cv::arcLength(hull, true);
    std::vector<cv::Point> approx;
    for (double eps : {0.02, 0.03, 0.05}) {
        cv::approxPolyDP(hull, approx, eps * peri, true);
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            out.assign(approx.begin(), approx.end());
            return true;
        }
    }
    return false;
}

// Fraction of each side of q that runs along an edge; returns the weakest side.
double edgeSupport(const cv::Mat &dilatedEdges, const std::vector<cv::Point2f> &q)
{
    double weakest = 1.0;
    for (size_t i = 0; i < 4; ++i) {
        const cv::Point2f a = q[i], b = q[(i + 1) % 4];
        const int n = int(std::hypot(b.x - a.x, b.y - a.y)) + 1;
        int hit = 0;
        for (int t = 0; t < n; ++t) {
            const int x = std::clamp(int(std::lround(a.x + (b.x - a.x) * t / n)), 0, dilatedEdges.cols - 1);
            const int y = std::clamp(int(std::lround(a.y + (b.y - a.y) * t / n)), 0, dilatedEdges.rows - 1);
            hit += dilatedEdges.at<uchar>(y, x) != 0;
        }
        weakest = std::min(weakest, double(hit) / n);
    }
    return weakest;
}

// Regions enclosed by edges. A two-tone page (a red block next to a white strip) or a
// page lying on another sheet splits into several regions, so the hulls of unions of
// up to three regions are tried too. A candidate counts only if all four of its sides
// lie on detected edges: text or pictures inside the page do not matter.
void regionCandidates(const cv::Mat &edges, double minArea,
                      const std::function<void(const std::vector<cv::Point2f> &, double)> &accept)
{
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    std::vector<std::pair<double, std::vector<cv::Point>>> parts;
    for (const auto &c : contours) {
        std::vector<cv::Point> hull;
        cv::convexHull(c, hull);
        const double a = cv::contourArea(hull);
        if (a >= 0.25 * minArea)
            parts.emplace_back(a, std::move(hull));
    }
    std::sort(parts.begin(), parts.end(), [](auto &a, auto &b) { return a.first > b.first; });
    if (parts.size() > 10)
        parts.resize(10);
    cv::Mat support;
    cv::dilate(edges, support, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));
    const int n = int(parts.size());
    auto tryUnion = [&](std::initializer_list<int> idx) {
        std::vector<cv::Point> pts, hull;
        for (int i : idx)
            pts.insert(pts.end(), parts[size_t(i)].second.begin(), parts[size_t(i)].second.end());
        cv::convexHull(pts, hull);
        if (cv::contourArea(hull) < minArea)
            return;
        std::vector<cv::Point2f> q;
        if (hullToQuad(hull, q) && edgeSupport(support, q) >= 0.8)
            accept(q, cv::contourArea(q));
    };
    for (int i = 0; i < n; ++i) {
        tryUnion({i});
        for (int j = i + 1; j < n; ++j) {
            tryUnion({i, j});
            for (int k = j + 1; k < n; ++k)
                tryUnion({i, j, k});
        }
    }
}

} // namespace

QPolygonF PageDetector::detect(const QImage &image, double minAreaRatio)
{
    if (image.isNull())
        return {};
    const QImage src = image.format() == QImage::Format_Grayscale8 || image.format() == QImage::Format_RGB888
                           ? image
                           : image.convertToFormat(QImage::Format_RGB888);
    const cv::Mat full = matView(src);
    const double scale = std::min(1.0, 512.0 / std::max(full.cols, full.rows));
    cv::Mat small, gray;
    cv::resize(full, small, cv::Size(), scale, scale, cv::INTER_AREA);
    if (small.channels() == 3)
        cv::cvtColor(small, gray, cv::COLOR_RGB2GRAY);
    else
        gray = small;
    // Median blur flattens paper texture and fabric desks while keeping page edges.
    cv::medianBlur(gray, gray, 7);
    const double minArea = minAreaRatio * gray.total();

    std::vector<cv::Point2f> best;
    double bestArea = 0;
    auto consider = [&](const cv::Mat &mask, bool allowRect) {
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        std::sort(contours.begin(), contours.end(),
                  [](auto &a, auto &b) { return cv::contourArea(a) > cv::contourArea(b); });
        for (size_t i = 0; i < std::min<size_t>(contours.size(), 5); ++i) {
            std::vector<cv::Point2f> q;
            if (!quadFromContour(contours[i], minArea, q, allowRect) || hugsFrame(q, gray.size()) || !roughlyRectangular(q))
                continue;
            const double a = cv::contourArea(q);
            if (a > bestArea) {
                bestArea = a;
                best = q;
            }
            break;
        }
    };

    auto accept = [&](const std::vector<cv::Point2f> &q, double a) {
        if (a > bestArea && !hugsFrame(q, gray.size()) && roughlyRectangular(q)) {
            bestArea = a;
            best = q;
        }
    };

    // 1) Edges: works on any background with some contrast.
    cv::Mat edges;
    cv::Mat tmp;
    const double otsu = cv::threshold(gray, tmp, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::Canny(gray, edges, otsu * 0.5, otsu);
    cv::dilate(edges, edges, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
    consider(edges, false);

    // 1b) Colour edges: a coloured page (e.g. a red flyer) can have the same grey level
    // as the desk. Edges of the Lab chroma channels catch it; keep the largest sheet.
    if (small.channels() == 3) {
        cv::Mat lab, colorEdges = edges.clone();
        cv::cvtColor(small, lab, cv::COLOR_RGB2Lab);
        std::vector<cv::Mat> ch;
        cv::split(lab, ch);
        for (int c = 1; c < 3; ++c) {
            cv::medianBlur(ch[c], ch[c], 7);
            const double t = cv::threshold(ch[c], tmp, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
            const double hi = std::max(20.0, std::abs(t - 128.0));
            cv::Mat e;
            cv::Canny(ch[c], e, hi * 0.5, hi);
            cv::dilate(e, e, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
            colorEdges |= e;
        }
        consider(colorEdges, false);
        edges = colorEdges;
    }
    // 1c) Regions between edges (two-tone pages, overlapping sheets).
    regionCandidates(edges, minArea, accept);

    // 2) Brightness: the sheet is usually lighter than the desk.
    if (best.empty()) {
        cv::Mat mask;
        cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(9, 9)));
        consider(mask, true);
    }
    if (best.empty())
        return {};
    QPolygonF out;
    for (const auto &p : orderCorners(best))
        out << QPointF(p.x / scale, p.y / scale);
    return out;
}

QImage PageDetector::warp(const QImage &image, const QPolygonF &quad)
{
    if (quad.size() != 4)
        return image;
    auto dist = [](QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); };
    const int w = int(std::max(dist(quad[0], quad[1]), dist(quad[3], quad[2])));
    const int h = int(std::max(dist(quad[0], quad[3]), dist(quad[1], quad[2])));
    if (w < 16 || h < 16)
        return image;
    std::vector<cv::Point2f> srcPts, dstPts{{0, 0}, {float(w - 1), 0}, {float(w - 1), float(h - 1)}, {0, float(h - 1)}};
    for (const QPointF &p : quad)
        srcPts.emplace_back(float(p.x()), float(p.y()));
    const QImage src = image.format() == QImage::Format_Grayscale8 || image.format() == QImage::Format_RGB888
                           ? image
                           : image.convertToFormat(QImage::Format_RGB888);
    cv::Mat out;
    cv::warpPerspective(matView(src), out, cv::getPerspectiveTransform(srcPts, dstPts), cv::Size(w, h),
                        cv::INTER_CUBIC, cv::BORDER_REPLICATE);
    return matToImage(out);
}

} // namespace lensscan
