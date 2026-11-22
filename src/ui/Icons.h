// SPDX-License-Identifier: GPL-3.0-or-later
// Line icons drawn with QPainter on a 24x24 grid (original artwork, GPL with the
// rest of the code). No image files and no SVG module needed.
#pragma once

#include <QColor>
#include <QIcon>

namespace lensscan {

enum class IconId {
    App, Scan, Back, Settings, Help, Folder, FolderOpen, RotateLeft, RotateRight, Trash,
    Pdf, Tiff, Txt, Docx, Xlsx, Image, Palette, Crop, SelectAll, Camera, Tools, Import, Print, Barcode, Info, Insert,
    Split, Reprocess,
    // image viewer
    First, Previous, Next, Last, ActualSize, FitWidth, FitHeight, FitPage, ZoomIn, ZoomOut,
    // presentation window
    Video, Move, Pen, Highlighter, Line, Arrow, Rect, Ellipse, Eraser, Undo, FlipH, FlipV, ZoomReset,
    Freeze, Screenshot, Record, Stop, Fullscreen, Pause, Text, Triangle, LineStyle, ScreenRecord, Files
};

QIcon icon(IconId id, const QColor &color = QColor(0x3a, 0x40, 0x55));

} // namespace lensscan
