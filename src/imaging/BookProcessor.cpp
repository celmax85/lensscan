// SPDX-License-Identifier: GPL-3.0-or-later
#include "BookProcessor.h"

#include "PageDetector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lensscan {

namespace {

constexpr int kAnalysisWidth = 900;
constexpr int kPolyDegree = 4;

// Polynomial in a normalised variable t = (x - x0) / span, for good conditioning.
struct Poly {
    double x0 = 0, span = 1;
    std::vector<double> c; // c[0] + c[1] t + ...
    double operator()(double x) const
    {
        const double t = (x - x0) / span;
        double y = 0;
        for (size_t i = c.size(); i-- > 0;)
            y = y * t + c[i];
        return y;
    }
};

Poly fitOnce(const std::vector<double> &x, const std::vector<double> &y, double x0, double span)
{
    const int n = int(x.size()), m = kPolyDegree + 1;
    cv::Mat A(n, m, CV_64F), b(n, 1, CV_64F), sol;
    for (int i = 0; i < n; ++i) {
        const double t = (x[size_t(i)] - x0) / span;
        double p = 1;
        for (int j = 0; j < m; ++j, p *= t)
            A.at<double>(i, j) = p;
        b.at<double>(i) = y[size_t(i)];
    }
    cv::solve(A, b, sol, cv::DECOMP_SVD);
    Poly poly;
    poly.x0 = x0;
    poly.span = span;
    poly.c.assign(sol.begin<double>(), sol.end<double>());
    return poly;
}

// Least squares, then again without the outliers (a finger or a bookmark on the edge).
Poly robustFit(const std::vector<double> &x, const std::vector<double> &y)
{
    const double x0 = x.front(), span = std::max(1.0, x.back() - x.front());
    Poly p = fitOnce(x, y, x0, span);
    std::vector<double> r(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        r[i] = y[i] - p(x[i]);
    double var = 0;
    for (double v : r)
        var += v * v;
    const double limit = std::max(3.0, 2.5 * std::sqrt(var / double(r.size())));
    std::vector<double> kx, ky;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs(r[i]) < limit) {
            kx.push_back(x[i]);
            ky.push_back(y[i]);
        }
    return kx.size() > size_t(kPolyDegree * 4) ? fitOnce(kx, ky, x0, span) : p;
}

struct PageFit {
    int x0 = 0, x1 = 0; // column range in the rotated analysis image
    Poly top, bottom;
};

struct Analysis {
    bool ok = false;
    double scale = 1; // analysis px per image px
    double angle = 0; // rotation applied (degrees, OpenCV convention)
    cv::Size smallSize;
    int spineX = 0;
    PageFit pages[2];
};

Analysis analyze(const cv::Mat &rgb)
{
    Analysis a;
    a.scale = std::min(1.0, double(kAnalysisWidth) / rgb.cols);
    cv::Mat small, gray, mask;
    cv::resize(rgb, small, cv::Size(), a.scale, a.scale, cv::INTER_AREA);
    a.smallSize = small.size();
    cv::cvtColor(small, gray, cv::COLOR_RGB2GRAY);
    cv::medianBlur(gray, gray, 7);
    cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(15, 15)));

    // Keep the largest bright component: the book.
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(mask, labels, stats, centroids);
    int best = 0, bestArea = 0;
    for (int i = 1; i < n; ++i)
        if (stats.at<int>(i, cv::CC_STAT_AREA) > bestArea) {
            bestArea = stats.at<int>(i, cv::CC_STAT_AREA);
            best = i;
        }
    if (best == 0 || bestArea < 0.1 * mask.total())
        return a;
    mask = labels == best;

    // Turn the book so its long side (across both pages) is horizontal.
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    const auto &c = *std::max_element(contours.begin(), contours.end(), [](auto &l, auto &r) {
        return cv::contourArea(l) < cv::contourArea(r);
    });
    const cv::RotatedRect box = cv::minAreaRect(c);
    // An open book is a compact shape; a part of a printed sheet or clutter is not.
    if (bestArea < 0.85 * box.size.area())
        return a;
    double angle = box.angle;
    if (box.size.width < box.size.height)
        angle += 90;
    while (angle > 45)
        angle -= 90;
    while (angle < -45)
        angle += 90;
    a.angle = angle;
    const cv::Mat rs = cv::getRotationMatrix2D(cv::Point2f(small.cols / 2.f, small.rows / 2.f), angle, 1);
    cv::warpAffine(mask, mask, rs, small.size(), cv::INTER_NEAREST);

    // Page height per column.
    const int W = mask.cols, H = mask.rows;
    std::vector<double> top(size_t(W), 0), bottom(size_t(W), 0), height(size_t(W), 0);
    for (int x = 0; x < W; ++x) {
        int t = -1, b = -1;
        for (int y = 0; y < H; ++y)
            if (mask.at<uchar>(y, x)) {
                t = y;
                break;
            }
        for (int y = H - 1; y >= 0; --y)
            if (mask.at<uchar>(y, x)) {
                b = y;
                break;
            }
        if (t >= 0) {
            top[size_t(x)] = t;
            bottom[size_t(x)] = b;
            height[size_t(x)] = b - t;
        }
    }
    std::vector<double> sorted = height;
    std::sort(sorted.begin(), sorted.end());
    const double tall = sorted[size_t(0.95 * (W - 1))];
    if (tall < 0.2 * H)
        return a;
    // Columns where the book has most of its height: excludes fingers sticking out.
    int xl = -1, xr = -1;
    for (int x = 0; x < W; ++x)
        if (height[size_t(x)] > 0.6 * tall) {
            if (xl < 0)
                xl = x;
            xr = x;
        }
    if (xr - xl < W / 4)
        return a;

    // Spine: where the book is thinnest, in the middle 40%.
    const int s0 = xl + int(0.3 * (xr - xl)), s1 = xl + int(0.7 * (xr - xl));
    double bestH = 1e9;
    int spine = (s0 + s1) / 2;
    for (int x = s0 + 4; x <= s1 - 4; ++x) {
        double sum = 0;
        for (int k = -4; k <= 4; ++k)
            sum += height[size_t(x + k)];
        if (sum < bestH) {
            bestH = sum;
            spine = x;
        }
    }
    // A real spine is a dip in page height: a flat sheet has none, or only at one end
    // of the search window (perspective).
    auto windowSum = [&](int x) {
        double sum = 0;
        for (int k = -4; k <= 4; ++k)
            sum += height[size_t(std::clamp(x + k, 0, W - 1))];
        return sum;
    };
    if (windowSum(s0) < 1.01 * bestH || windowSum(s1) < 1.01 * bestH)
        return a;
    const double widthRatio = double(spine - xl) / std::max(1, xr - spine);
    if (widthRatio < 0.55 || widthRatio > 1.8)
        return a;
    a.spineX = spine;

    const int ranges[2][2] = {{xl, spine}, {spine, xr}};
    for (int p = 0; p < 2; ++p) {
        std::vector<double> xs, ts, bs;
        for (int x = ranges[p][0] + 3; x < ranges[p][1] - 3; ++x) {
            xs.push_back(x);
            ts.push_back(top[size_t(x)]);
            bs.push_back(bottom[size_t(x)]);
        }
        if (xs.size() < 40)
            return a;
        a.pages[p] = {ranges[p][0], ranges[p][1], robustFit(xs, ts), robustFit(xs, bs)};
        // Page edges must follow smooth curves (fingers aside, removed by the robust fit).
        std::vector<double> err;
        for (size_t i = 0; i < xs.size(); ++i)
            err.push_back(std::abs(ts[i] - a.pages[p].top(xs[i])) + std::abs(bs[i] - a.pages[p].bottom(xs[i])));
        std::nth_element(err.begin(), err.begin() + long(err.size() * 3 / 4), err.end());
        if (err[err.size() * 3 / 4] > 0.02 * tall)
            return a;
    }
    a.ok = true;
    return a;
}

cv::Mat toRgb(const QImage &image, QImage &keep)
{
    keep = image.format() == QImage::Format_RGB888 ? image : image.convertToFormat(QImage::Format_RGB888);
    return matView(keep);
}

} // namespace

BookGeometry detectBook(const QImage &image)
{
    BookGeometry g;
    if (image.isNull())
        return g;
    QImage keep;
    const cv::Mat rgb = toRgb(image, keep);
    const Analysis a = analyze(rgb);
    if (!a.ok)
        return g;
    // Map analysis (rotated, scaled) coordinates back to the image.
    const cv::Mat rs =
        cv::getRotationMatrix2D(cv::Point2f(a.smallSize.width / 2.f, a.smallSize.height / 2.f), a.angle, 1);
    cv::Mat inv;
    cv::invertAffineTransform(rs, inv);
    auto back = [&](double x, double y) {
        const double ox = inv.at<double>(0, 0) * x + inv.at<double>(0, 1) * y + inv.at<double>(0, 2);
        const double oy = inv.at<double>(1, 0) * x + inv.at<double>(1, 1) * y + inv.at<double>(1, 2);
        return QPointF(ox / a.scale, oy / a.scale);
    };
    QPolygonF polys[2];
    for (int p = 0; p < 2; ++p) {
        const PageFit &f = a.pages[p];
        for (int x = f.x0; x <= f.x1; x += 4)
            polys[p] << back(x, f.top(x));
        for (int x = f.x1; x >= f.x0; x -= 4)
            polys[p] << back(x, f.bottom(x));
    }
    g.valid = true;
    g.angle = a.angle;
    g.leftPage = polys[0];
    g.rightPage = polys[1];
    g.spine = QLineF(back(a.spineX, a.pages[1].top(a.spineX)), back(a.spineX, a.pages[1].bottom(a.spineX)));
    return g;
}

QVector<QImage> processBook(const QImage &image, const BookOptions &opt)
{
    if (image.isNull())
        return {};
    QImage keep;
    const cv::Mat rgb = toRgb(image, keep);
    const Analysis a = analyze(rgb);
    if (!a.ok)
        return {};

    cv::Mat upright;
    const cv::Mat r = cv::getRotationMatrix2D(cv::Point2f(rgb.cols / 2.f, rgb.rows / 2.f), a.angle, 1);
    cv::warpAffine(rgb, upright, r, rgb.size(), cv::INTER_CUBIC, cv::BORDER_REPLICATE);

    const double f = opt.focalPx > 0 ? opt.focalPx : 0.95 * rgb.cols;
    const double cx = rgb.cols / 2.0, sc = a.scale;
    QVector<QImage> out;
    for (int p = 0; p < 2; ++p) {
        const PageFit &fit = a.pages[p];
        constexpr int N = 2000;
        std::vector<double> X(N), T(N), B(N), S(N, 0);
        double maxH = 0;
        for (int i = 0; i < N; ++i) {
            const double xs = fit.x0 + (fit.x1 - fit.x0) * double(i) / (N - 1); // analysis px
            X[size_t(i)] = xs / sc;
            T[size_t(i)] = fit.top(xs) / sc;
            B[size_t(i)] = fit.bottom(xs) / sc;
            maxH = std::max(maxH, B[size_t(i)] - T[size_t(i)]);
        }
        // Surface in half-page-height units: X = (x - cx) / h, Z = -f / h.
        for (int i = 1; i < N; ++i) {
            const double h0 = std::max(1.0, (B[size_t(i - 1)] - T[size_t(i - 1)]) / 2);
            const double h1 = std::max(1.0, (B[size_t(i)] - T[size_t(i)]) / 2);
            const double dx = (X[size_t(i)] - cx) / h1 - (X[size_t(i - 1)] - cx) / h0;
            const double dz = -f / h1 + f / h0;
            S[size_t(i)] = S[size_t(i - 1)] + std::hypot(dx, dz);
        }
        const int outH = int(maxH);
        const int outW = int(S.back() * outH / 2);
        if (outW < 32 || outH < 32)
            return {};
        cv::Mat mapx(outH, outW, CV_32F), mapy(outH, outW, CV_32F);
        size_t j = 0;
        for (int u = 0; u < outW; ++u) {
            const double s = S.back() * u / std::max(1, outW - 1);
            while (j + 2 < size_t(N) && S[j + 1] < s)
                ++j;
            const double t = S[j + 1] > S[j] ? std::clamp((s - S[j]) / (S[j + 1] - S[j]), 0.0, 1.0) : 0.0;
            const double xx = X[j] + t * (X[j + 1] - X[j]);
            const double tt = T[j] + t * (T[j + 1] - T[j]);
            const double bb = B[j] + t * (B[j + 1] - B[j]);
            for (int v = 0; v < outH; ++v) {
                const double k = double(v) / std::max(1, outH - 1);
                mapx.at<float>(v, u) = float(xx);
                mapy.at<float>(v, u) = float(tt + k * (bb - tt));
            }
        }
        cv::Mat page;
        cv::remap(upright, page, mapx, mapy, cv::INTER_CUBIC, cv::BORDER_REPLICATE);
        QImage img = matToImage(page);
        if (opt.removeFingers)
            removeFingers(img, p == 0);
        out << img;
    }
    return out;
}

int removeFingers(QImage &page, bool outerLeft)
{
    if (page.format() != QImage::Format_RGB888)
        page = page.convertToFormat(QImage::Format_RGB888);
    page.bits(); // detach: the pixels are edited in place below
    cv::Mat m = matView(page);
    const int w = m.cols, h = m.rows;
    cv::Mat ycc, skin, gray;
    cv::cvtColor(m, ycc, cv::COLOR_RGB2YCrCb);
    cv::inRange(ycc, cv::Scalar(40, 135, 80), cv::Scalar(255, 180, 130), skin);
    // Hands hold a book by its outer edge.
    const int band = int(0.18 * w);
    cv::Mat keep = cv::Mat::zeros(skin.size(), CV_8U);
    keep(outerLeft ? cv::Rect(0, 0, band, h) : cv::Rect(w - band, 0, band, h)).setTo(255);
    skin &= keep;
    cv::morphologyEx(skin, skin, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(skin, labels, stats, centroids);
    cv::Mat mask = cv::Mat::zeros(skin.size(), CV_8U);
    for (int i = 1; i < n; ++i) {
        const int x = stats.at<int>(i, cv::CC_STAT_LEFT), bw = stats.at<int>(i, cv::CC_STAT_WIDTH);
        const bool touches = outerLeft ? x <= 2 : x + bw >= w - 2;
        if (touches && stats.at<int>(i, cv::CC_STAT_AREA) > 0.0005 * w * h)
            mask.setTo(255, labels == i);
    }
    const int erased = cv::countNonZero(mask);
    if (!erased)
        return 0;
    cv::dilate(mask, mask, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(15, 15)));
    // Paper colour: median of the bright pixels outside the fingers.
    cv::cvtColor(m, gray, cv::COLOR_RGB2GRAY);
    cv::Mat paperMask = (gray > 150) & (mask == 0);
    cv::Scalar paper = cv::Scalar(245, 245, 245);
    if (cv::countNonZero(paperMask) > 100) {
        std::vector<cv::Mat> ch;
        cv::split(m, ch);
        for (int c = 0; c < 3; ++c) {
            std::vector<uchar> v;
            v.reserve(size_t(cv::countNonZero(paperMask)));
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (paperMask.at<uchar>(y, x))
                        v.push_back(ch[size_t(c)].at<uchar>(y, x));
            std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
            paper[c] = v[v.size() / 2];
        }
    }
    m.setTo(paper, mask);
    return erased;
}

} // namespace lensscan
