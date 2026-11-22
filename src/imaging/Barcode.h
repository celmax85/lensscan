// SPDX-License-Identifier: GPL-3.0-or-later
// Barcode and QR code reading (zbar): EAN / UPC, Code 128, Code 39, Code 93,
// Interleaved 2 of 5, Codabar, DataBar, QR code.
#pragma once

#include <QImage>
#include <QPolygon>
#include <QString>
#include <QVector>

namespace lensscan {

struct BarcodeResult {
    QString type;    // "QR-Code", "EAN-13", "CODE-128"...
    QString text;
    QPolygon outline; // in image pixels
};

QVector<BarcodeResult> readBarcodes(const QImage &image);

} // namespace lensscan
