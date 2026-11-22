// SPDX-License-Identifier: GPL-3.0-or-later
// JPEG helpers built on libjpeg-turbo: frame validation and fast scaled decoding
// (DCT-domain downscaling, so a 3264x2448 frame decodes to preview size cheaply).
#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>

namespace lensscan {

// Cheap structural check: SOI marker at the start and EOI near the end.
// The CZUR camera sometimes delivers truncated or garbage MJPEG frames.
bool looksLikeJpeg(const uint8_t *data, size_t size);

// Reads width/height from the header. Returns an invalid size on error.
QSize jpegSize(const QByteArray &jpeg);

// Decodes to RGB888, shrinking by the largest libjpeg-turbo scaling factor that
// keeps the result at least maxWidth wide (0 = full size). Returns a null image if
// the data is not a decodable JPEG; with strict=true, warnings (corrupt data) fail too.
QImage decodeJpeg(const uint8_t *data, size_t size, int maxWidth = 0, bool strict = false);
inline QImage decodeJpeg(const QByteArray &jpeg, int maxWidth = 0, bool strict = false)
{
    return decodeJpeg(reinterpret_cast<const uint8_t *>(jpeg.constData()), size_t(jpeg.size()), maxWidth, strict);
}

// Returns the JPEG with its JFIF header saying `dpi` dots per inch (a JFIF header is
// added when the data has none, as camera MJPEG frames).
QByteArray jpegWithDpi(const QByteArray &jpeg, int dpi);

// Adds a comment (COM) segment after the JFIF header.
QByteArray jpegWithComment(const QByteArray &jpeg, const QByteArray &text);

// Encodes RGB888 or Grayscale8 images. Other formats are converted first.
QByteArray encodeJpeg(const QImage &image, int quality);

} // namespace lensscan
