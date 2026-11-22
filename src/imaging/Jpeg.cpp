// SPDX-License-Identifier: GPL-3.0-or-later
#include "Jpeg.h"

#include <algorithm>

#include <turbojpeg.h>

namespace lensscan {

namespace {
struct TjHandle {
    explicit TjHandle(bool compress) : h(compress ? tjInitCompress() : tjInitDecompress()) {}
    ~TjHandle()
    {
        if (h)
            tjDestroy(h);
    }
    tjhandle h;
};
} // namespace

bool looksLikeJpeg(const uint8_t *data, size_t size)
{
    if (!data || size < 1024 || data[0] != 0xff || data[1] != 0xd8)
        return false;
    // UVC payloads are often padded with zeros after EOI.
    const size_t tail = size > 4096 ? size - 4096 : 2;
    for (size_t i = size - 1; i > tail; --i)
        if (data[i - 1] == 0xff && data[i] == 0xd9)
            return true;
    return false;
}

QSize jpegSize(const QByteArray &jpeg)
{
    TjHandle tj(false);
    int w = 0, h = 0, sub = 0, cs = 0;
    if (!tj.h || tjDecompressHeader3(tj.h, reinterpret_cast<const unsigned char *>(jpeg.constData()),
                                     static_cast<unsigned long>(jpeg.size()), &w, &h, &sub, &cs) != 0)
        return {};
    return {w, h};
}

QImage decodeJpeg(const uint8_t *data, size_t size, int maxWidth, bool strict)
{
    if (!looksLikeJpeg(data, size))
        return {};
    TjHandle tj(false);
    int w = 0, h = 0, sub = 0, cs = 0;
    if (!tj.h || tjDecompressHeader3(tj.h, data, static_cast<unsigned long>(size), &w, &h, &sub, &cs) != 0)
        return {};
    int sw = w, sh = h;
    if (maxWidth > 0 && w > maxWidth) {
        int n = 0;
        tjscalingfactor *f = tjGetScalingFactors(&n);
        // Factors are sorted from largest to smallest; keep the smallest that stays >= maxWidth.
        for (int i = 0; i < n; ++i) {
            const int cw = TJSCALED(w, f[i]);
            if (f[i].num <= f[i].denom && cw >= maxWidth) {
                sw = cw;
                sh = TJSCALED(h, f[i]);
            }
        }
    }
    QImage img(sw, sh, QImage::Format_RGB888);
    if (img.isNull())
        return {};
    const int r = tjDecompress2(tj.h, data, static_cast<unsigned long>(size), img.bits(), sw,
                                static_cast<int>(img.bytesPerLine()), sh, TJPF_RGB, TJFLAG_FASTDCT);
    if (r != 0 && (strict || tjGetErrorCode(tj.h) == TJERR_FATAL))
        return {};
    return img;
}

QByteArray encodeJpeg(const QImage &image, int quality)
{
    QImage src = image;
    int pf = TJPF_RGB, sub = TJSAMP_420;
    if (src.format() == QImage::Format_Grayscale8) {
        pf = TJPF_GRAY;
        sub = TJSAMP_GRAY;
    } else if (src.format() != QImage::Format_RGB888) {
        src = src.convertToFormat(QImage::Format_RGB888);
    }
    TjHandle tj(true);
    unsigned char *buf = nullptr;
    unsigned long len = 0;
    if (!tj.h || tjCompress2(tj.h, src.constBits(), src.width(), static_cast<int>(src.bytesPerLine()),
                             src.height(), pf, &buf, &len, sub, quality, TJFLAG_ACCURATEDCT) != 0) {
        tjFree(buf);
        return {};
    }
    QByteArray out(reinterpret_cast<const char *>(buf), static_cast<int>(len));
    tjFree(buf);
    return out;
}

QByteArray jpegWithDpi(const QByteArray &jpeg, int dpi)
{
    if (dpi <= 0 || jpeg.size() < 4 || uchar(jpeg[0]) != 0xFF || uchar(jpeg[1]) != 0xD8)
        return jpeg;
    const quint16 d = quint16(std::clamp(dpi, 1, 65535));
    QByteArray out = jpeg;
    // APP0 "JFIF\0": version (2 bytes), units, X density, Y density.
    if (out.size() >= 18 && uchar(out[2]) == 0xFF && uchar(out[3]) == 0xE0 && out.mid(6, 5) == QByteArray("JFIF", 5)) {
        out[13] = 1; // dots per inch
        out[14] = char(d >> 8);
        out[15] = char(d & 0xFF);
        out[16] = char(d >> 8);
        out[17] = char(d & 0xFF);
        return out;
    }
    const char app0[] = {char(0xFF), char(0xE0), 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 1,
                         char(d >> 8), char(d & 0xFF), char(d >> 8), char(d & 0xFF), 0, 0};
    out.insert(2, app0, int(sizeof(app0)));
    return out;
}

QByteArray jpegWithComment(const QByteArray &jpeg, const QByteArray &text)
{
    if (jpeg.size() < 4 || uchar(jpeg[0]) != 0xFF || uchar(jpeg[1]) != 0xD8 || text.size() > 65000)
        return jpeg;
    int at = 2;
    if (uchar(jpeg[2]) == 0xFF && uchar(jpeg[3]) == 0xE0 && jpeg.size() > 6)
        at = 4 + ((uchar(jpeg[4]) << 8) | uchar(jpeg[5])); // after APP0
    const int len = int(text.size()) + 2;
    QByteArray seg;
    seg.append(char(0xFF)).append(char(0xFE)).append(char(len >> 8)).append(char(len & 0xFF)).append(text);
    QByteArray out = jpeg;
    out.insert(at, seg);
    return out;
}

} // namespace lensscan
