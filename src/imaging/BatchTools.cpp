// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatchTools.h"

#include "PageDetector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSet>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace lensscan {

double inkCoverage(const QImage &page)
{
    if (page.isNull())
        return 0;
    QImage g = page.convertToFormat(QImage::Format_Grayscale8);
    if (g.width() > 800 || g.height() > 800)
        g = g.scaled(800, 800, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const cv::Mat m = matView(g);
    // Paper level: a large median-like background (dilate removes text, blur smooths).
    cv::Mat bg, diff, ink;
    const int k = std::max(15, std::min(m.cols, m.rows) / 20) | 1;
    cv::dilate(m, bg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k, k)));
    cv::blur(bg, bg, cv::Size(k, k));
    cv::subtract(bg, m, diff);
    cv::threshold(diff, ink, 40, 255, cv::THRESH_BINARY);
    cv::morphologyEx(ink, ink, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2)));
    const int mx = m.cols * 4 / 100, my = m.rows * 4 / 100;
    const cv::Mat inner = ink(cv::Rect(mx, my, m.cols - 2 * mx, m.rows - 2 * my));
    return double(cv::countNonZero(inner)) / double(inner.total());
}

bool isBlankPage(const QImage &page, int sensitivity)
{
    // A line of text covers ~0.3% of a page; 50 -> threshold 0.1%.
    const double limit = 0.0002 * std::pow(10.0, std::clamp(sensitivity, 0, 100) / 50.0 * 0.7);
    return inkCoverage(page) < limit;
}

QImage adjustImage(const QImage &image, const Adjustment &a)
{
    if (image.isNull() || a.isNull())
        return image;
    const bool gray = image.format() == QImage::Format_Grayscale8;
    QImage src = gray ? image : image.convertToFormat(QImage::Format_RGB888);
    cv::Mat m = matView(src).clone();
    if (a.brightness || a.contrast) {
        // Contrast pivots on mid-grey; brightness shifts by up to +-128.
        const double c = a.contrast >= 0 ? 1.0 + a.contrast / 50.0 : 1.0 + a.contrast / 125.0;
        const double b = a.brightness * 1.28;
        m.convertTo(m, -1, c, 128 * (1 - c) + b);
    }
    if (a.sharpness > 0) {
        cv::Mat blur;
        const double sigma = std::max(1.0, std::min(m.cols, m.rows) / 1200.0);
        cv::GaussianBlur(m, blur, cv::Size(), sigma);
        const double amount = a.sharpness / 50.0; // 0..2
        cv::addWeighted(m, 1 + amount, blur, -amount, 0, m);
    }
    return matToImage(m);
}

QImage addWatermark(const QImage &image, const Watermark &w)
{
    const bool useImage = !w.image.isNull();
    if (image.isNull() || (!useImage && w.text.trimmed().isEmpty()))
        return image;
    QImage out = image.convertToFormat(QImage::Format_RGB888);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (useImage) {
        const int iw = std::max(8, out.width() * std::clamp(w.imageWidthPercent, 2, 100) / 100);
        const QImage logo = w.image.scaledToWidth(iw, Qt::SmoothTransformation);
        const int margin = out.height() / 30;
        p.setOpacity(std::clamp(w.opacity, 1, 100) / 100.0);
        auto drawAt = [&](const QPointF &c, double angle) {
            p.save();
            p.translate(c);
            p.rotate(angle);
            p.drawImage(QPointF(-logo.width() / 2.0, -logo.height() / 2.0), logo);
            p.restore();
        };
        const double hw = logo.width() / 2.0 + margin, hh = logo.height() / 2.0 + margin;
        switch (w.position) {
        case WatermarkPosition::Center: drawAt(out.rect().center(), 0); break;
        case WatermarkPosition::Diagonal:
            drawAt(out.rect().center(), -std::atan2(double(out.height()), double(out.width())) * 180 / M_PI);
            break;
        case WatermarkPosition::TopLeft: drawAt(QPointF(hw, hh), 0); break;
        case WatermarkPosition::TopRight: drawAt(QPointF(out.width() - hw, hh), 0); break;
        case WatermarkPosition::BottomLeft: drawAt(QPointF(hw, out.height() - hh), 0); break;
        case WatermarkPosition::BottomRight: drawAt(QPointF(out.width() - hw, out.height() - hh), 0); break;
        case WatermarkPosition::Tiled:
            for (int y = logo.height(), row = 0; y < out.height() + logo.height(); y += logo.height() * 2, ++row)
                for (int x = (row % 2) * logo.width(); x < out.width() + logo.width(); x += logo.width() * 2)
                    drawAt(QPointF(x, y), 0);
            break;
        }
        p.end();
        return image.format() == QImage::Format_Grayscale8 ? out.convertToFormat(QImage::Format_Grayscale8) : out;
    }
    QFont f(QStringLiteral("Sans Serif"));
    f.setBold(true);
    f.setPixelSize(std::max(8, out.height() * std::clamp(w.sizePercent, 1, 40) / 100));
    p.setFont(f);
    QColor c = w.color;
    c.setAlphaF(std::clamp(w.opacity, 1, 100) / 100.0);
    p.setPen(c);
    const QFontMetrics fm(f);
    const QRect tb = fm.boundingRect(w.text);
    const int margin = out.height() / 30;
    const QRect page = out.rect();
    auto drawAt = [&](const QPointF &center, double angle) {
        p.save();
        p.translate(center);
        p.rotate(angle);
        p.drawText(QPointF(-tb.width() / 2.0, fm.ascent() / 2.0 - fm.descent() / 2.0), w.text);
        p.restore();
    };
    const double diag = -std::atan2(double(out.height()), double(out.width())) * 180 / M_PI;
    switch (w.position) {
    case WatermarkPosition::Center:
        drawAt(page.center(), 0);
        break;
    case WatermarkPosition::Diagonal:
        drawAt(page.center(), diag);
        break;
    case WatermarkPosition::TopLeft:
        drawAt(QPointF(margin + tb.width() / 2.0, margin + fm.height() / 2.0), 0);
        break;
    case WatermarkPosition::TopRight:
        drawAt(QPointF(out.width() - margin - tb.width() / 2.0, margin + fm.height() / 2.0), 0);
        break;
    case WatermarkPosition::BottomLeft:
        drawAt(QPointF(margin + tb.width() / 2.0, out.height() - margin - fm.height() / 2.0), 0);
        break;
    case WatermarkPosition::BottomRight:
        drawAt(QPointF(out.width() - margin - tb.width() / 2.0, out.height() - margin - fm.height() / 2.0), 0);
        break;
    case WatermarkPosition::Tiled: {
        const int sx = tb.width() + fm.height() * 3, sy = fm.height() * 4;
        for (int y = sy / 2, row = 0; y < out.height() + sy; y += sy, ++row)
            for (int x = (row % 2) * sx / 2; x < out.width() + sx; x += sx)
                drawAt(QPointF(x, y), -30);
        break;
    }
    }
    p.end();
    return image.format() == QImage::Format_Grayscale8 ? out.convertToFormat(QImage::Format_Grayscale8) : out;
}

QImage cropToPageWidth(const QImage &page, int width, bool leftHandPage)
{
    if (width <= 0 || width >= page.width())
        return page;
    return leftHandPage ? page.copy(page.width() - width, 0, width, page.height()) : page.copy(0, 0, width, page.height());
}

QVector<QPolygonF> detectDocuments(const QImage &image, int maxDocuments)
{
    QVector<QPolygonF> out;
    if (image.isNull())
        return out;
    const double scale = std::min(1.0, 1000.0 / std::max(image.width(), image.height()));
    QImage small = image.convertToFormat(QImage::Format_RGB888)
                       .scaled(int(image.width() * scale), int(image.height() * scale), Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    const cv::Mat rgb = matView(small);
    // Documents differ from the desk by brightness or colour: distance to the desk
    // colour (the median of the image border).
    cv::Mat lab;
    cv::cvtColor(rgb, lab, cv::COLOR_RGB2Lab);
    std::vector<cv::Vec3b> border;
    for (int x = 0; x < lab.cols; x += 2) {
        border.push_back(lab.at<cv::Vec3b>(0, x));
        border.push_back(lab.at<cv::Vec3b>(lab.rows - 1, x));
    }
    for (int y = 0; y < lab.rows; y += 2) {
        border.push_back(lab.at<cv::Vec3b>(y, 0));
        border.push_back(lab.at<cv::Vec3b>(y, lab.cols - 1));
    }
    cv::Vec3d desk;
    for (int c = 0; c < 3; ++c) {
        std::vector<uchar> v;
        for (const auto &b : border)
            v.push_back(b[c]);
        std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
        desk[c] = v[v.size() / 2];
    }
    cv::Mat dist(lab.size(), CV_8U);
    for (int y = 0; y < lab.rows; ++y)
        for (int x = 0; x < lab.cols; ++x) {
            const cv::Vec3b p = lab.at<cv::Vec3b>(y, x);
            const double d = std::hypot(std::hypot(p[0] - desk[0], 1.5 * (p[1] - desk[1])), 1.5 * (p[2] - desk[2]));
            dist.at<uchar>(y, x) = cv::saturate_cast<uchar>(d);
        }
    cv::GaussianBlur(dist, dist, cv::Size(5, 5), 0);
    cv::Mat mask;
    cv::threshold(dist, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(9, 9)));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    std::sort(contours.begin(), contours.end(),
              [](const auto &a, const auto &b) { return cv::contourArea(a) > cv::contourArea(b); });
    const double minArea = 0.01 * mask.total();
    for (const auto &c : contours) {
        if (int(out.size()) >= maxDocuments || cv::contourArea(c) < minArea)
            break;
        const cv::RotatedRect r = cv::minAreaRect(c);
        if (cv::contourArea(c) < 0.75 * r.size.area()) // not a sheet / card
            continue;
        cv::Point2f pts[4];
        r.points(pts);
        // Order: top-left, top-right, bottom-right, bottom-left.
        std::vector<cv::Point2f> v(pts, pts + 4);
        std::sort(v.begin(), v.end(), [](auto a, auto b) { return a.x + a.y < b.x + b.y; });
        const cv::Point2f tl = v[0], br = v[3];
        const cv::Point2f tr = v[1].x - v[1].y > v[2].x - v[2].y ? v[1] : v[2];
        const cv::Point2f bl = v[1].x - v[1].y > v[2].x - v[2].y ? v[2] : v[1];
        QPolygonF q;
        for (const cv::Point2f &p : {tl, tr, br, bl})
            q << QPointF(std::clamp(p.x / scale, 0.0, double(image.width() - 1)),
                         std::clamp(p.y / scale, 0.0, double(image.height() - 1)));
        out << q;
    }
    return out;
}

QVector<QImage> splitDocuments(const QImage &image)
{
    QVector<QImage> out;
    for (const QPolygonF &q : detectDocuments(image))
        out << PageDetector::warp(image, q);
    return out;
}

QImage trimEdges(const QImage &image, int pixels)
{
    const int n = std::clamp(pixels, 0, std::max(0, (std::min(image.width(), image.height()) - 16) / 2));
    return n ? image.copy(image.rect().adjusted(n, n, -n, -n)) : image;
}

QImage cropNormalized(const QImage &image, const QRectF &r)
{
    const QRect px = QRectF(r.x() * image.width(), r.y() * image.height(), r.width() * image.width(),
                            r.height() * image.height())
                         .toAlignedRect()
                         .intersected(image.rect());
    return px.width() >= 8 && px.height() >= 8 ? image.copy(px) : image;
}

QStringList renamedPaths(const QStringList &files, const QString &prefix, int start, int digits)
{
    QStringList out;
    int n = start;
    for (const QString &f : files) {
        const QFileInfo fi(f);
        out << fi.dir().filePath(QStringLiteral("%1%2.%3")
                                     .arg(prefix)
                                     .arg(n++, std::clamp(digits, 1, 9), 10, QLatin1Char('0'))
                                     .arg(fi.suffix().toLower()));
    }
    return out;
}

QString renameFiles(const QStringList &files, const QStringList &newPaths)
{
    if (files.size() != newPaths.size())
        return QStringLiteral("internal error");
    // Targets must not clash with each other or with files outside the set.
    QSet<QString> sources(files.begin(), files.end()), seen;
    for (const QString &t : newPaths) {
        if (seen.contains(t))
            return QObject::tr("Two pages would get the same name: %1").arg(QFileInfo(t).fileName());
        seen.insert(t);
        if (QFileInfo::exists(t) && !sources.contains(t))
            return QObject::tr("%1 already exists").arg(QFileInfo(t).fileName());
    }
    QStringList temp;
    for (int i = 0; i < files.size(); ++i) {
        const QString t = files[i] + QStringLiteral(".lensscan-rename");
        if (!QFile::rename(files[i], t)) {
            for (int k = 0; k < i; ++k) // undo
                QFile::rename(temp[k], files[k]);
            return QObject::tr("Cannot rename %1").arg(QFileInfo(files[i]).fileName());
        }
        temp << t;
    }
    for (int i = 0; i < files.size(); ++i)
        if (!QFile::rename(temp[i], newPaths[i]))
            return QObject::tr("Cannot rename %1").arg(QFileInfo(files[i]).fileName());
    return {};
}

QStringList importImages(const QStringList &files, const QString &dir, QStringList *errors)
{
    QStringList out;
    QDir().mkpath(dir);
    for (const QString &f : files) {
        const QFileInfo fi(f);
        QString suffix = fi.suffix().toLower();
        const bool keep = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg") ||
                          suffix == QLatin1String("png") || suffix == QLatin1String("bmp");
        if (!keep)
            suffix = QStringLiteral("png");
        QString target = QDir(dir).filePath(fi.completeBaseName() + QLatin1Char('.') + suffix);
        for (int i = 2; QFileInfo::exists(target); ++i)
            target = QDir(dir).filePath(QStringLiteral("%1_%2.%3").arg(fi.completeBaseName()).arg(i).arg(suffix));
        bool ok;
        if (keep) {
            ok = QFile::copy(f, target);
        } else {
            QImageReader r(f);
            r.setAutoTransform(true);
            const QImage img = r.read();
            ok = !img.isNull() && img.save(target, "PNG");
        }
        if (ok)
            out << target;
        else if (errors)
            *errors << fi.fileName();
    }
    return out;
}

} // namespace lensscan
