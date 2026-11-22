// SPDX-License-Identifier: GPL-3.0-or-later
// Color modes offered by the scan window (same set as the original software):
// auto, color, gray, black & white, white paper with stamps, ID/watermark, none.
#pragma once

#include <QImage>
#include <QString>

namespace lensscan {

enum class ColorMode { Auto, Color, Gray, BlackWhite, WhitePaperStamp, Watermark, None };

QString colorModeKey(ColorMode m);              // stable settings key
ColorMode colorModeFromKey(const QString &key); // unknown -> Auto

// Returns RGB888 for color modes and Grayscale8 for Gray / BlackWhite.
QImage applyColorMode(const QImage &image, ColorMode mode);

} // namespace lensscan
