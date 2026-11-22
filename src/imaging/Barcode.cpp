// SPDX-License-Identifier: GPL-3.0-or-later
#include "Barcode.h"

#include <QSet>

#include <zbar.h>

#include <cstring>
#include <vector>

namespace lensscan {

namespace {

QVector<BarcodeResult> scanGray(const QImage &gray, double toSource)
{
    QVector<BarcodeResult> out;
    const int w = gray.width(), h = gray.height();
    std::vector<unsigned char> buf(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        std::memcpy(buf.data() + size_t(y) * size_t(w), gray.constScanLine(y), size_t(w));
    zbar::ImageScanner scanner;
    scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);
    zbar::Image img(unsigned(w), unsigned(h), "Y800", buf.data(), static_cast<unsigned long>(buf.size()));
    if (scanner.scan(img) <= 0)
        return out;
    for (auto s = img.symbol_begin(); s != img.symbol_end(); ++s) {
        BarcodeResult r;
        r.type = QString::fromStdString(s->get_type_name());
        r.text = QString::fromUtf8(s->get_data().data(), int(s->get_data().size()));
        for (int i = 0; i < s->get_location_size(); ++i)
            r.outline << QPoint(int(s->get_location_x(unsigned(i)) * toSource),
                                int(s->get_location_y(unsigned(i)) * toSource));
        out << r;
    }
    img.set_data(nullptr, 0);
    return out;
}

} // namespace

QVector<BarcodeResult> readBarcodes(const QImage &image)
{
    if (image.isNull())
        return {};
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    QVector<BarcodeResult> found = scanGray(gray, 1.0);
    // Large, blurry codes are often found at half size.
    if (std::max(gray.width(), gray.height()) > 1600) {
        const QImage half = gray.scaled(gray.width() / 2, gray.height() / 2, Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation);
        QSet<QString> seen;
        for (const auto &r : found)
            seen.insert(r.type + QLatin1Char('\n') + r.text);
        for (const auto &r : scanGray(half, double(gray.width()) / half.width()))
            if (!seen.contains(r.type + QLatin1Char('\n') + r.text))
                found << r;
    }
    return found;
}

} // namespace lensscan
