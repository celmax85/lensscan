// SPDX-License-Identifier: GPL-3.0-or-later
#include "ColorModes.h"

#include "PageDetector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <vector>

namespace lensscan {

QString colorModeKey(ColorMode m)
{
    switch (m) {
    case ColorMode::Auto: return QStringLiteral("auto");
    case ColorMode::Color: return QStringLiteral("color");
    case ColorMode::Gray: return QStringLiteral("gray");
    case ColorMode::BlackWhite: return QStringLiteral("bw");
    case ColorMode::WhitePaperStamp: return QStringLiteral("stamp");
    case ColorMode::Watermark: return QStringLiteral("watermark");
    case ColorMode::None: return QStringLiteral("none");
    }
    return QStringLiteral("auto");
}

ColorMode colorModeFromKey(const QString &key)
{
    for (ColorMode m : {ColorMode::Auto, ColorMode::Color, ColorMode::Gray, ColorMode::BlackWhite,
                        ColorMode::WhitePaperStamp, ColorMode::Watermark, ColorMode::None})
        if (colorModeKey(m) == key)
            return m;
    return ColorMode::Auto;
}

namespace {

// Divides each channel by a smooth estimate of the paper background, which removes
// shadows and uneven lighting and turns the paper white.
cv::Mat flattenBackground(const cv::Mat &src)
{
    const double s = std::min(1.0, 800.0 / std::max(src.cols, src.rows));
    cv::Mat small, bg;
    cv::resize(src, small, cv::Size(), s, s, cv::INTER_AREA);
    const int k = std::max(7, int(std::min(small.cols, small.rows) / 30) | 1);
    // Closing removes dark text strokes, leaving the paper.
    cv::morphologyEx(small, bg, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k)));
    cv::GaussianBlur(bg, bg, cv::Size(k, k), 0);
    cv::resize(bg, bg, src.size(), 0, 0, cv::INTER_LINEAR);
    cv::Mat out;
    cv::divide(src, bg, out, 255.0);
    return out;
}

// Colour-preserving variant: evens out the lamp's light fall-off without turning large
// coloured areas white. The paper level is the local maximum brightness over a quarter
// of the page (so a coloured block is measured against the white paper around it), and
// one gain per pixel is applied to all channels, capped so dark areas are not blown out.
cv::Mat flattenLighting(const cv::Mat &src)
{
    if (src.channels() != 3)
        return flattenBackground(src);
    const double s = std::min(1.0, 400.0 / std::max(src.cols, src.rows));
    cv::Mat small, gray, bg;
    cv::resize(src, small, cv::Size(), s, s, cv::INTER_AREA);
    cv::cvtColor(small, gray, cv::COLOR_RGB2GRAY);
    const int k = std::max(9, int(std::min(gray.cols, gray.rows) / 4) | 1);
    cv::dilate(gray, bg, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k)));
    cv::GaussianBlur(bg, bg, cv::Size(k, k), 0);
    cv::Mat gain;
    bg.convertTo(gain, CV_32F);
    gain = 245.0f / cv::max(gain, 1.0f);
    gain = cv::min(cv::max(gain, 1.0f), 1.8f);
    cv::resize(gain, gain, src.size(), 0, 0, cv::INTER_LINEAR);
    cv::Mat f, out;
    src.convertTo(f, CV_32FC3);
    cv::Mat g3;
    cv::merge(std::vector<cv::Mat>{gain, gain, gain}, g3);
    cv::multiply(f, g3, f);
    f.convertTo(out, CV_8UC3);
    return out;
}

// Linear stretch so that the given percentiles map to 0 and 255, per channel.
void stretch(cv::Mat &m, double lowPct, double highPct)
{
    std::vector<cv::Mat> ch;
    cv::split(m, ch);
    for (auto &c : ch) {
        int hist[256] = {};
        for (int y = 0; y < c.rows; ++y) {
            const uchar *p = c.ptr(y);
            for (int x = 0; x < c.cols; ++x)
                ++hist[p[x]];
        }
        const double total = double(c.total());
        int lo = 0, hi = 255;
        double acc = 0;
        for (int i = 0; i < 256; ++i) {
            acc += hist[i];
            if (acc >= total * lowPct / 100) {
                lo = i;
                break;
            }
        }
        acc = 0;
        for (int i = 255; i >= 0; --i) {
            acc += hist[i];
            if (acc >= total * (100 - highPct) / 100) {
                hi = i;
                break;
            }
        }
        if (hi - lo < 10)
            continue;
        c.convertTo(c, CV_8U, 255.0 / (hi - lo), -lo * 255.0 / (hi - lo));
    }
    cv::merge(ch, m);
}

void sharpen(cv::Mat &m, double amount)
{
    cv::Mat blur;
    cv::GaussianBlur(m, blur, cv::Size(0, 0), 1.5);
    cv::addWeighted(m, 1 + amount, blur, -amount, 0, m);
}

cv::Mat toGray(const cv::Mat &m)
{
    if (m.channels() == 1)
        return m.clone();
    cv::Mat g;
    cv::cvtColor(m, g, cv::COLOR_RGB2GRAY);
    return g;
}

} // namespace

QImage applyColorMode(const QImage &image, ColorMode mode)
{
    if (mode == ColorMode::None || image.isNull())
        return image;
    const QImage src = image.format() == QImage::Format_Grayscale8 || image.format() == QImage::Format_RGB888
                           ? image
                           : image.convertToFormat(QImage::Format_RGB888);
    cv::Mat m = matView(src).clone();

    switch (mode) {
    case ColorMode::Auto: {
        m = flattenLighting(m);
        stretch(m, 0.5, 99.9);
        sharpen(m, 0.4);
        break;
    }
    case ColorMode::Color:
        stretch(m, 0.3, 99.7);
        sharpen(m, 0.3);
        break;
    case ColorMode::Gray: {
        m = toGray(m);
        stretch(m, 0.5, 99.5);
        sharpen(m, 0.3);
        break;
    }
    case ColorMode::BlackWhite: {
        cv::Mat g = flattenBackground(toGray(m));
        const int block = std::max(15, (std::min(g.cols, g.rows) / 40) | 1);
        cv::adaptiveThreshold(g, m, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, block, 12);
        break;
    }
    case ColorMode::WhitePaperStamp: {
        m = flattenBackground(m);
        stretch(m, 0.5, 99.0);
        // Boost saturation so red stamps stay vivid on the whitened paper.
        cv::Mat hsv;
        cv::cvtColor(m, hsv, cv::COLOR_RGB2HSV);
        std::vector<cv::Mat> ch;
        cv::split(hsv, ch);
        ch[1].convertTo(ch[1], CV_8U, 1.4);
        cv::merge(ch, hsv);
        cv::cvtColor(hsv, m, cv::COLOR_HSV2RGB);
        break;
    }
    case ColorMode::Watermark:
        // Keep faint backgrounds and watermarks: gentle stretch only.
        stretch(m, 0.1, 99.9);
        sharpen(m, 0.2);
        break;
    case ColorMode::None:
        break;
    }
    return matToImage(m);
}

} // namespace lensscan
