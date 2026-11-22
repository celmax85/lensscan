// SPDX-License-Identifier: GPL-3.0-or-later
#include "DocumentTools.h"

#include "BookProcessor.h"
#include "PageDetector.h"

#include <QPainter>

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lensscan {

namespace {

int oddAtLeast(int v, int minimum)
{
    v = std::max(v, minimum);
    return v | 1;
}

// Smooth estimate of the paper under dark marks no larger than `feature` pixels: a
// maximum filter (removes the marks) then a blur, computed at reduced size.
cv::Mat paperBackground(const cv::Mat &src, int feature)
{
    const int longSide = std::max(src.cols, src.rows);
    const double scale = longSide > 1000 ? 1000.0 / longSide : 1.0;
    cv::Mat small;
    cv::resize(src, small, cv::Size(), scale, scale, cv::INTER_AREA);
    const int k = oddAtLeast(int(std::ceil(2.2 * feature * scale)) + 2, 7);
    cv::dilate(small, small, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k, k)));
    cv::medianBlur(small, small, 5);
    cv::blur(small, small, cv::Size(k, k));
    cv::Mat out;
    cv::resize(small, out, src.size(), 0, 0, cv::INTER_LINEAR);
    return out;
}

cv::Mat whiten(const cv::Mat &rgb)
{
    const cv::Mat bg = paperBackground(rgb, std::max(rgb.cols, rgb.rows) / 60);
    cv::Mat out(rgb.size(), CV_8UC3);
    for (int y = 0; y < rgb.rows; ++y) {
        const uchar *s = rgb.ptr(y), *b = bg.ptr(y);
        uchar *d = out.ptr(y);
        for (int i = 0; i < rgb.cols * 3; ++i)
            d[i] = uchar(std::min(255, s[i] * 255 / std::max(1, int(b[i]))));
    }
    return out;
}

uchar medianOf(std::vector<uchar> &v)
{
    if (v.empty())
        return 255;
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

} // namespace

QImage eraseHandwriting(const QImage &image, const HandwritingOptions &o)
{
    const QImage src = image.convertToFormat(QImage::Format_RGB888);
    if (src.isNull())
        return image;
    cv::Mat m = matView(src).clone();
    if (o.blue || o.red || o.green) {
        cv::Mat hsv;
        cv::cvtColor(m, hsv, cv::COLOR_RGB2HSV); // H 0..179
        const cv::Mat bg = paperBackground(m, std::max(m.cols, m.rows) / 150);
        const int satMin = std::clamp(110 - o.strength * 8 / 10, 25, 110);
        cv::Mat core(m.size(), CV_8U, cv::Scalar(0));
        for (int y = 0; y < m.rows; ++y) {
            const uchar *p = hsv.ptr(y);
            uchar *c = core.ptr(y);
            for (int x = 0; x < m.cols; ++x, p += 3) {
                const int h = p[0], s = p[1], v = p[2];
                if (s < satMin || v < 35)
                    continue;
                if ((o.blue && h >= 90 && h <= 150) || (o.red && (h <= 10 || h >= 160)) ||
                    (o.green && h >= 35 && h < 90))
                    c[x] = 255;
            }
        }
        cv::morphologyEx(core, core, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2)));
        cv::Mat grown;
        cv::dilate(core, grown, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
        for (int y = 0; y < m.rows; ++y) {
            const uchar *c = core.ptr(y), *g = grown.ptr(y), *p = hsv.ptr(y), *b = bg.ptr(y);
            uchar *d = m.ptr(y);
            for (int x = 0; x < m.cols; ++x) {
                if (!g[x])
                    continue;
                // The halo around a stroke is replaced too, except dark neutral print.
                const int bgV = std::max({b[3 * x], b[3 * x + 1], b[3 * x + 2]});
                const bool print = p[3 * x + 1] < 60 && p[3 * x + 2] < bgV - 70;
                if (c[x] || !print) {
                    d[3 * x] = b[3 * x];
                    d[3 * x + 1] = b[3 * x + 1];
                    d[3 * x + 2] = b[3 * x + 2];
                }
            }
        }
    }
    if (o.cleanBackground)
        m = whiten(m);
    return matToImage(m);
}

QImage fillPunchHoles(const QImage &image, int minRadius, int maxRadius, int *found)
{
    if (found)
        *found = 0;
    const QImage src = image.convertToFormat(QImage::Format_RGB888);
    if (src.isNull() || maxRadius < 2)
        return image;
    minRadius = std::max(2, std::min(minRadius, maxRadius));
    cv::Mat m = matView(src).clone();
    cv::Mat gray;
    cv::cvtColor(m, gray, cv::COLOR_RGB2GRAY);
    const cv::Mat bgGray = paperBackground(gray, maxRadius);
    cv::Mat diff, mask;
    cv::subtract(bgGray, gray, diff);
    cv::threshold(diff, mask, 50, 255, cv::THRESH_BINARY);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    const int W = m.cols, H = m.rows;
    const double band = std::max(3.0 * maxRadius, 0.15 * std::min(W, H));
    cv::Mat fill(m.size(), CV_8U, cv::Scalar(0));
    int count = 0;
    for (const auto &c : contours) {
        const double area = cv::contourArea(c);
        if (area < M_PI * minRadius * minRadius * 0.3)
            continue;
        cv::Point2f center;
        float r = 0;
        cv::minEnclosingCircle(c, center, r);
        if (r < minRadius || r > maxRadius * 1.05)
            continue;
        const double edgeDist = std::min({double(center.x), double(center.y), W - 1.0 - center.x, H - 1.0 - center.y});
        if (edgeDist > band)
            continue;
        const cv::Rect bb = cv::boundingRect(c);
        const bool cut = bb.x <= 1 || bb.y <= 1 || bb.x + bb.width >= W - 1 || bb.y + bb.height >= H - 1;
        std::vector<cv::Point> hull;
        cv::convexHull(c, hull);
        const double convexity = area / std::max(1.0, cv::contourArea(hull));
        const double ratio = area / (M_PI * r * r);
        const double perim = cv::arcLength(c, true);
        const double circularity = 4 * M_PI * area / std::max(1.0, perim * perim);
        const bool hole = convexity > 0.9 && (cut ? ratio >= 0.3 : ratio >= 0.6 && circularity >= 0.6);
        if (!hole)
            continue;
        cv::drawContours(fill, std::vector<std::vector<cv::Point>>{c}, 0, cv::Scalar(255), cv::FILLED);
        ++count;
    }
    if (found)
        *found = count;
    if (count == 0)
        return src;
    cv::dilate(fill, fill, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7)));
    const cv::Mat bg = paperBackground(m, maxRadius);
    bg.copyTo(m, fill);
    return matToImage(m);
}

QImage fillPaperCorners(const QImage &page, bool wholeBorder)
{
    const QImage src = page.convertToFormat(QImage::Format_RGB888);
    if (src.width() < 32 || src.height() < 32)
        return page;
    cv::Mat m = matView(src).clone();
    const int W = m.cols, H = m.rows;
    cv::Mat gray;
    cv::cvtColor(m, gray, cv::COLOR_RGB2GRAY);

    // Paper color: per-channel median of the central half of the page.
    std::vector<uchar> ch[3], g;
    const int step = std::max(1, std::min(W, H) / 200);
    for (int y = H / 4; y < H * 3 / 4; y += step) {
        const uchar *p = m.ptr(y), *q = gray.ptr(y);
        for (int x = W / 4; x < W * 3 / 4; x += step) {
            for (int k = 0; k < 3; ++k)
                ch[k].push_back(p[3 * x + k]);
            g.push_back(q[x]);
        }
    }
    const cv::Vec3b paper(medianOf(ch[0]), medianOf(ch[1]), medianOf(ch[2]));
    const int paperGray = medianOf(g);
    const int limit = paperGray - std::max(50, paperGray * 3 / 10);
    if (limit <= 0)
        return src; // dark page: nothing to tell the background from

    cv::Mat dark;
    cv::threshold(gray, dark, limit, 255, cv::THRESH_BINARY_INV);
    if (!wholeBorder) {
        const int c = std::max(8, std::min(W, H) / 10);
        cv::Mat keep(m.size(), CV_8U, cv::Scalar(0));
        for (const cv::Rect &r : {cv::Rect(0, 0, c, c), cv::Rect(W - c, 0, c, c), cv::Rect(0, H - c, c, c),
                                  cv::Rect(W - c, H - c, c, c)})
            keep(r).setTo(255);
        cv::bitwise_and(dark, keep, dark);
    }
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(dark, labels, stats, centroids, 8, CV_32S);
    std::vector<uchar> chosen(size_t(std::max(n, 1)), 0);
    bool any = false;
    const double minArea = 0.0003 * W * H;
    for (int i = 1; i < n; ++i) {
        const int x = stats.at<int>(i, cv::CC_STAT_LEFT), y = stats.at<int>(i, cv::CC_STAT_TOP);
        const int w = stats.at<int>(i, cv::CC_STAT_WIDTH), h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        const bool left = x == 0, top = y == 0, right = x + w == W, bottom = y + h == H;
        bool ok;
        if (wholeBorder)
            ok = (left || top || right || bottom) && area >= minArea && (w < W * 6 / 10 || h < H * 6 / 10);
        else
            ok = (left || right) && (top || bottom) && area >= 4;
        if (ok) {
            chosen[size_t(i)] = 1;
            any = true;
        }
    }
    if (!any)
        return src;
    cv::Mat fill(m.size(), CV_8U, cv::Scalar(0));
    for (int y = 0; y < H; ++y) {
        const int *l = labels.ptr<int>(y);
        uchar *f = fill.ptr(y);
        for (int x = 0; x < W; ++x)
            if (l[x] > 0 && chosen[size_t(l[x])])
                f[x] = 255;
    }
    cv::dilate(fill, fill, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
    m.setTo(cv::Scalar(paper[0], paper[1], paper[2]), fill);
    return matToImage(m);
}

QImage flipImage(const QImage &image, bool horizontal)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return image.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
#else
    return image.mirrored(horizontal, !horizontal);
#endif
}

bool scanNameLess(const QString &a, const QString &b)
{
    const int c = QString::compare(a, b, Qt::CaseInsensitive);
    return c != 0 ? c < 0 : QString::compare(a, b, Qt::CaseSensitive) < 0;
}

QStringList insertionNames(const QStringList &sortedNames, int index, const QStringList &suffixes)
{
    const int count = int(suffixes.size());
    if (count == 0 || index < 0 || index > sortedNames.size())
        return {};
    const QString lo = index > 0 ? sortedNames.at(index - 1) : QString();
    const QString hi = index < sortedNames.size() ? sortedNames.at(index) : QString();
    QSet<QString> taken;
    for (const QString &n : sortedNames)
        taken.insert(n.toLower());

    QStringList prefixes;
    if (!lo.isEmpty())
        prefixes << QFileInfo(lo).completeBaseName();
    if (!hi.isEmpty()) {
        // Before "Scan_0001": "Scan_0000_001".
        const QString base = QFileInfo(hi).completeBaseName();
        static const QRegularExpression lastNumber(QStringLiteral("(\\d+)(?!.*\\d)"));
        const auto match = lastNumber.match(base);
        if (match.hasMatch() && match.captured(1).toLongLong() > 0) {
            const QString digits = match.captured(1);
            const QString dec = QStringLiteral("%1").arg(digits.toLongLong() - 1, int(digits.size()), 10, QLatin1Char('0'));
            prefixes << base.left(match.capturedStart(1)) + dec + base.mid(match.capturedEnd(1));
        }
        prefixes << QStringLiteral("0") << QStringLiteral("!");
    }
    if (lo.isEmpty() && hi.isEmpty())
        prefixes << QStringLiteral("Scan");

    for (const QString &prefix : prefixes) {
        for (int start = 1; start < 1000; start += count) {
            QStringList out;
            bool ok = true;
            for (int i = 0; i < count && ok; ++i) {
                const QString n =
                    QStringLiteral("%1_%2%3").arg(prefix).arg(start + i, 3, 10, QLatin1Char('0')).arg(suffixes.at(i));
                ok = !taken.contains(n.toLower()) && (lo.isEmpty() || scanNameLess(lo, n)) &&
                     (hi.isEmpty() || scanNameLess(n, hi)) && (out.isEmpty() || scanNameLess(out.last(), n));
                out << n;
            }
            if (ok)
                return out;
            // Past the upper bound already: more numbers will not help.
            if (!hi.isEmpty() && !scanNameLess(QStringLiteral("%1_%2").arg(prefix).arg(start, 3, 10, QLatin1Char('0')), hi))
                break;
        }
    }
    return {};
}

QImage cropMargins(const QImage &image, const Margins &m, bool cut)
{
    const int l = std::clamp(m.left, 0, image.width() - 16), r = std::clamp(m.right, 0, image.width() - 16 - l);
    const int t = std::clamp(m.top, 0, image.height() - 16), b = std::clamp(m.bottom, 0, image.height() - 16 - t);
    if (l + r <= 0 && t + b <= 0)
        return image;
    const QRect keep(l, t, image.width() - l - r, image.height() - t - b);
    if (cut)
        return image.copy(keep);
    QImage out = image.format() == QImage::Format_Grayscale8 ? image : image.convertToFormat(QImage::Format_RGB888);
    QPainter p(&out);
    QRegion outside(out.rect());
    outside -= QRegion(keep);
    for (const QRect &rect : outside)
        p.fillRect(rect, Qt::white);
    return out;
}

QVector<QImage> splitFacingPages(const QImage &image, bool removeFingers)
{
    if (image.isNull())
        return {};
    BookOptions bo;
    bo.removeFingers = removeFingers;
    const QVector<QImage> book = processBook(image, bo);
    if (book.size() == 2)
        return book;
    if (image.width() < image.height() * 11 / 10)
        return {image}; // portrait: a single page
    // Gutter: darkest column (shadow of the fold), smoothed, in the middle 30 %.
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const int w = gray.width(), h = gray.height();
    std::vector<double> col(size_t(w), 0.0);
    const int y0 = h / 10, y1 = h - h / 10;
    for (int y = y0; y < y1; y += 2) {
        const uchar *p = gray.constScanLine(y);
        for (int x = 0; x < w; ++x)
            col[size_t(x)] += p[x];
    }
    const int radius = std::max(2, w / 200);
    int best = w / 2;
    double bestValue = 1e300, sum = 0;
    int count = 0;
    for (int x = w * 35 / 100; x <= w * 65 / 100; ++x) {
        double v = 0;
        for (int k = -radius; k <= radius; ++k)
            v += col[size_t(std::clamp(x + k, 0, w - 1))];
        sum += v;
        ++count;
        if (v < bestValue) {
            bestValue = v;
            best = x;
        }
    }
    // No clear fold shadow: cut in the middle.
    if (count > 0 && bestValue > 0.97 * sum / count)
        best = w / 2;
    return {image.copy(0, 0, best, h), image.copy(best, 0, w - best, h)};
}

} // namespace lensscan
