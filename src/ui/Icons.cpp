// SPDX-License-Identifier: GPL-3.0-or-later
#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace lensscan {

namespace {

void drawArrowHead(QPainter &p, QPointF tip, double angleDeg)
{
    p.save();
    p.translate(tip);
    p.rotate(angleDeg);
    p.drawLine(QPointF(0, 0), QPointF(-4, -3));
    p.drawLine(QPointF(0, 0), QPointF(-4, 3));
    p.restore();
}

void drawDocument(QPainter &p)
{
    QPainterPath d;
    d.moveTo(6, 3);
    d.lineTo(14, 3);
    d.lineTo(19, 8);
    d.lineTo(19, 21);
    d.lineTo(6, 21);
    d.closeSubpath();
    p.drawPath(d);
    p.drawPolyline(QPolygonF({QPointF(14, 3), QPointF(14, 8), QPointF(19, 8)}));
}

void paint(QPainter &p, IconId id, const QColor &c)
{
    QPen pen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (id) {
    case IconId::App:
    case IconId::Camera: // document camera: base, arm, head
        p.drawLine(QPointF(3, 20), QPointF(21, 20));
        p.drawLine(QPointF(6, 20), QPointF(6, 6));
        p.drawLine(QPointF(6, 6), QPointF(15, 4));
        p.drawRoundedRect(QRectF(13, 3, 7, 4), 1, 1);
        if (id == IconId::App) {
            p.setPen(QPen(c, 1.2, Qt::DashLine));
            p.drawLine(QPointF(16.5, 8), QPointF(13, 18));
            p.drawLine(QPointF(16.5, 8), QPointF(20, 18));
        }
        break;
    case IconId::Scan:
        p.drawRoundedRect(QRectF(3, 6, 18, 13), 2, 2);
        p.drawEllipse(QPointF(12, 12.5), 3.6, 3.6);
        p.drawLine(QPointF(8, 6), QPointF(9.5, 3.5));
        p.drawLine(QPointF(9.5, 3.5), QPointF(14.5, 3.5));
        p.drawLine(QPointF(14.5, 3.5), QPointF(16, 6));
        break;
    case IconId::Back:
        p.drawLine(QPointF(20, 12), QPointF(5, 12));
        drawArrowHead(p, QPointF(4, 12), 180);
        p.drawLine(QPointF(4, 12), QPointF(10, 6));
        p.drawLine(QPointF(4, 12), QPointF(10, 18));
        break;
    case IconId::Settings: {
        p.drawEllipse(QPointF(12, 12), 3, 3);
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(12, 12);
            p.rotate(i * 45);
            p.drawLine(QPointF(0, -6.5), QPointF(0, -9));
            p.restore();
        }
        p.drawEllipse(QPointF(12, 12), 6.5, 6.5);
        break;
    }
    case IconId::Help:
        p.drawEllipse(QPointF(12, 12), 9, 9);
        {
            QPainterPath q;
            q.moveTo(9.3, 9.5);
            q.cubicTo(9.3, 6.5, 14.7, 6.5, 14.7, 9.5);
            q.cubicTo(14.7, 11.5, 12, 11.5, 12, 14);
            p.drawPath(q);
        }
        p.setBrush(c);
        p.drawEllipse(QPointF(12, 17), 0.6, 0.6);
        break;
    case IconId::Folder:
    case IconId::FolderOpen: {
        QPainterPath f;
        f.moveTo(3, 6);
        f.lineTo(9, 6);
        f.lineTo(11, 8);
        f.lineTo(21, 8);
        f.lineTo(21, 19);
        f.lineTo(3, 19);
        f.closeSubpath();
        p.drawPath(f);
        if (id == IconId::FolderOpen) {
            p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
            drawArrowHead(p, QPointF(12, 11), -90);
        }
        break;
    }
    case IconId::RotateLeft:
    case IconId::RotateRight: {
        const bool mirror = id == IconId::RotateRight; // base drawing turns left
        p.save();
        if (mirror) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        p.drawArc(QRectF(4, 5, 15, 15), 100 * 16, -280 * 16);
        p.drawLine(QPointF(9.5, 5.6), QPointF(13, 3));
        p.drawLine(QPointF(9.5, 5.6), QPointF(13, 8.5));
        p.restore();
        break;
    }
    case IconId::Trash:
        p.drawLine(QPointF(4, 6), QPointF(20, 6));
        p.drawLine(QPointF(9, 6), QPointF(10, 3.5));
        p.drawLine(QPointF(10, 3.5), QPointF(14, 3.5));
        p.drawLine(QPointF(14, 3.5), QPointF(15, 6));
        p.drawPolyline(QPolygonF({QPointF(6, 6), QPointF(7, 21), QPointF(17, 21), QPointF(18, 6)}));
        p.drawLine(QPointF(10, 10), QPointF(10, 17));
        p.drawLine(QPointF(14, 10), QPointF(14, 17));
        break;
    case IconId::Pdf:
    case IconId::Tiff:
    case IconId::Txt:
    case IconId::Docx:
    case IconId::Xlsx: {
        drawDocument(p);
        QFont f;
        f.setPixelSize(5);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(5, 11, 15, 8), Qt::AlignCenter,
                   id == IconId::Pdf    ? QStringLiteral("PDF")
                   : id == IconId::Tiff ? QStringLiteral("TIF")
                   : id == IconId::Txt  ? QStringLiteral("TXT")
                   : id == IconId::Xlsx ? QStringLiteral("XLS")
                                        : QStringLiteral("DOC"));
        break;
    }
    case IconId::Image:
        p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        p.drawPolyline(QPolygonF({QPointF(4, 17), QPointF(9.5, 11), QPointF(13, 14.5), QPointF(15.5, 12),
                                  QPointF(20, 16.5)}));
        p.drawEllipse(QPointF(15.5, 8.8), 1.4, 1.4);
        break;
    case IconId::Palette:
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.setBrush(c);
        for (QPointF pt : {QPointF(8, 10), QPointF(12, 7.5), QPointF(16, 10), QPointF(15.5, 15)})
            p.drawEllipse(pt, 1.3, 1.3);
        break;
    case IconId::Crop:
        p.drawPolyline(QPolygonF({QPointF(7, 2), QPointF(7, 17), QPointF(22, 17)}));
        p.drawPolyline(QPolygonF({QPointF(2, 7), QPointF(17, 7), QPointF(17, 22)}));
        break;
    case IconId::Tools: // sliders
        for (int i = 0; i < 3; ++i) {
            const double y = 6 + 6 * i, x = i == 1 ? 15 : 9;
            p.drawLine(QPointF(3, y), QPointF(21, y));
            p.setBrush(QColor(0xf0, 0xf0, 0xf0));
            p.drawEllipse(QPointF(x, y), 2.2, 2.2);
        }
        break;
    case IconId::Import: // tray with an arrow coming in
        p.drawPolyline(QPolygonF({QPointF(3, 14), QPointF(3, 21), QPointF(21, 21), QPointF(21, 14)}));
        p.drawLine(QPointF(12, 3), QPointF(12, 16));
        p.drawPolyline(QPolygonF({QPointF(7.5, 11.5), QPointF(12, 16), QPointF(16.5, 11.5)}));
        break;
    case IconId::Print: // printer: paper tray, body and output sheet
        p.drawPolyline(QPolygonF({QPointF(7, 8), QPointF(7, 3), QPointF(17, 3), QPointF(17, 8)}));
        p.drawRoundedRect(QRectF(3, 8, 18, 9), 1.5, 1.5);
        p.drawRect(QRectF(7, 14, 10, 7));
        p.drawLine(QPointF(9, 17), QPointF(15, 17));
        break;
    case IconId::Barcode: { // vertical bars of different widths
        const double xs[] = {4, 6.5, 8, 10.5, 13, 14.5, 17, 19.5};
        const double ws[] = {1.2, 0.8, 2.0, 0.8, 1.2, 2.0, 0.8, 1.2};
        for (int i = 0; i < 8; ++i)
            p.fillRect(QRectF(xs[i], 5, ws[i], 14), c);
        break;
    }
    case IconId::Info: // "i" in a circle
        p.drawEllipse(QRectF(3, 3, 18, 18));
        p.drawLine(QPointF(12, 10.5), QPointF(12, 17));
        p.setBrush(c);
        p.drawEllipse(QPointF(12, 7.3), 0.9, 0.9);
        break;
    case IconId::Insert: // page with a plus sign
        drawDocument(p);
        p.drawLine(QPointF(12, 10), QPointF(12, 18));
        p.drawLine(QPointF(8, 14), QPointF(16, 14));
        break;
    case IconId::Video: // screen on a stand with a play triangle
        p.drawRoundedRect(QRectF(2.5, 4, 19, 13), 2, 2);
        p.drawLine(QPointF(12, 17), QPointF(12, 20.5));
        p.drawLine(QPointF(8, 20.5), QPointF(16, 20.5));
        p.setBrush(c);
        p.drawPolygon(QPolygonF({QPointF(10, 7.5), QPointF(15, 10.5), QPointF(10, 13.5)}));
        break;
    case IconId::Move: // four-way arrow
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        drawArrowHead(p, QPointF(12, 3), -90);
        drawArrowHead(p, QPointF(12, 21), 90);
        drawArrowHead(p, QPointF(3, 12), 180);
        drawArrowHead(p, QPointF(21, 12), 0);
        break;
    case IconId::Pen:
        p.drawPolygon(QPolygonF({QPointF(15.5, 4.5), QPointF(19.5, 8.5), QPointF(8.5, 19.5), QPointF(3.5, 20.5),
                                 QPointF(4.5, 15.5)}));
        p.drawLine(QPointF(13.5, 6.5), QPointF(17.5, 10.5));
        break;
    case IconId::Highlighter:
        p.drawPolygon(QPolygonF({QPointF(14, 3.5), QPointF(20.5, 10), QPointF(12.5, 18), QPointF(6, 11.5)}));
        p.drawPolyline(QPolygonF({QPointF(7.5, 13), QPointF(4.5, 19.5), QPointF(9, 18.5), QPointF(11, 16.5)}));
        p.setPen(QPen(c, 2.6, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(13, 21.5), QPointF(21, 21.5));
        break;
    case IconId::Line:
        p.drawLine(QPointF(4, 20), QPointF(20, 4));
        break;
    case IconId::Arrow:
        p.drawLine(QPointF(4, 20), QPointF(20, 4));
        drawArrowHead(p, QPointF(20, 4), -45);
        p.drawLine(QPointF(20, 4), QPointF(13.5, 4.5));
        p.drawLine(QPointF(20, 4), QPointF(19.5, 10.5));
        break;
    case IconId::Rect:
        p.drawRect(QRectF(3.5, 6, 17, 12));
        break;
    case IconId::Ellipse:
        p.drawEllipse(QRectF(3, 5.5, 18, 13));
        break;
    case IconId::Eraser:
        p.drawPolygon(QPolygonF({QPointF(14, 3.5), QPointF(21, 10.5), QPointF(12, 19.5), QPointF(8, 19.5),
                                 QPointF(3.5, 15), QPointF(14, 3.5)}));
        p.drawLine(QPointF(8.5, 9), QPointF(16, 16.5));
        p.drawLine(QPointF(12, 19.5), QPointF(21, 19.5));
        break;
    case IconId::Undo: {
        QPainterPath path;
        path.moveTo(5, 9);
        path.lineTo(15, 9);
        path.cubicTo(22, 9, 22, 20, 15, 20);
        path.lineTo(9, 20);
        p.drawPath(path);
        drawArrowHead(p, QPointF(5, 9), 180);
        break;
    }
    case IconId::FlipH:
        p.setPen(QPen(c, 1.4, Qt::DashLine));
        p.drawLine(QPointF(12, 2), QPointF(12, 22));
        p.setPen(pen);
        p.drawPolygon(QPolygonF({QPointF(9.5, 6), QPointF(9.5, 18), QPointF(3, 18)}));
        p.setBrush(c);
        p.drawPolygon(QPolygonF({QPointF(14.5, 6), QPointF(14.5, 18), QPointF(21, 18)}));
        break;
    case IconId::FlipV:
        p.setPen(QPen(c, 1.4, Qt::DashLine));
        p.drawLine(QPointF(2, 12), QPointF(22, 12));
        p.setPen(pen);
        p.drawPolygon(QPolygonF({QPointF(6, 9.5), QPointF(18, 9.5), QPointF(18, 3)}));
        p.setBrush(c);
        p.drawPolygon(QPolygonF({QPointF(6, 14.5), QPointF(18, 14.5), QPointF(18, 21)}));
        break;
    case IconId::ZoomReset:
        p.drawEllipse(QPointF(10, 10), 6.5, 6.5);
        p.drawLine(QPointF(14.8, 14.8), QPointF(21, 21));
        p.drawLine(QPointF(7, 10), QPointF(13, 10));
        p.drawLine(QPointF(10, 7), QPointF(10, 13));
        break;
    case IconId::Freeze: // pause bars in a frame
        p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
        p.setBrush(c);
        p.drawRect(QRectF(8.5, 8, 2, 8));
        p.drawRect(QRectF(13.5, 8, 2, 8));
        break;
    case IconId::Screenshot:
        p.drawPolyline(QPolygonF({QPointF(3, 8), QPointF(3, 3), QPointF(8, 3)}));
        p.drawPolyline(QPolygonF({QPointF(16, 3), QPointF(21, 3), QPointF(21, 8)}));
        p.drawPolyline(QPolygonF({QPointF(21, 16), QPointF(21, 21), QPointF(16, 21)}));
        p.drawPolyline(QPolygonF({QPointF(8, 21), QPointF(3, 21), QPointF(3, 16)}));
        p.drawEllipse(QPointF(12, 12), 3.5, 3.5);
        break;
    case IconId::Record:
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.setBrush(QColor(0xe5, 0x39, 0x35));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(12, 12), 5, 5);
        break;
    case IconId::Stop:
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.setBrush(QColor(0xe5, 0x39, 0x35));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(8, 8, 8, 8), 1.5, 1.5);
        break;
    case IconId::Fullscreen:
        p.drawPolyline(QPolygonF({QPointF(3, 9), QPointF(3, 3), QPointF(9, 3)}));
        p.drawPolyline(QPolygonF({QPointF(15, 3), QPointF(21, 3), QPointF(21, 9)}));
        p.drawPolyline(QPolygonF({QPointF(21, 15), QPointF(21, 21), QPointF(15, 21)}));
        p.drawPolyline(QPolygonF({QPointF(9, 21), QPointF(3, 21), QPointF(3, 15)}));
        break;
    case IconId::Split: // open book with a dashed cut in the middle
        p.drawRoundedRect(QRectF(3, 5, 8, 14), 1, 1);
        p.drawRoundedRect(QRectF(13, 5, 8, 14), 1, 1);
        p.setPen(QPen(c, 1.4, Qt::DashLine));
        p.drawLine(QPointF(12, 2), QPointF(12, 22));
        break;
    case IconId::Reprocess: // page with a circular arrow
        p.drawRoundedRect(QRectF(4, 3, 12, 16), 1.5, 1.5);
        p.drawArc(QRectF(11, 11, 10, 10), 60 * 16, 270 * 16);
        drawArrowHead(p, QPointF(18.5, 11.7), 20);
        break;
    case IconId::First:
    case IconId::Last: {
        p.save();
        if (id == IconId::Last) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        p.drawLine(QPointF(6, 6), QPointF(6, 18));
        p.drawPolyline(QPolygonF({QPointF(16, 6), QPointF(10, 12), QPointF(16, 18)}));
        p.restore();
        break;
    }
    case IconId::Previous:
        p.drawPolyline(QPolygonF({QPointF(15, 5), QPointF(8, 12), QPointF(15, 19)}));
        break;
    case IconId::Next:
        p.drawPolyline(QPolygonF({QPointF(9, 5), QPointF(16, 12), QPointF(9, 19)}));
        break;
    case IconId::ActualSize: { // "1:1"
        QFont f = p.font();
        f.setPixelSize(10);
        f.setBold(true);
        p.setFont(f);
        p.drawRoundedRect(QRectF(2.5, 4.5, 19, 15), 2, 2);
        p.drawText(QRectF(2.5, 4.5, 19, 15), Qt::AlignCenter, QStringLiteral("1:1"));
        break;
    }
    case IconId::FitWidth:
        p.drawRoundedRect(QRectF(6, 3, 12, 18), 1.5, 1.5);
        p.drawLine(QPointF(2, 12), QPointF(22, 12));
        drawArrowHead(p, QPointF(2, 12), 180);
        drawArrowHead(p, QPointF(22, 12), 0);
        break;
    case IconId::FitHeight:
        p.drawRoundedRect(QRectF(5, 6, 14, 12), 1.5, 1.5);
        p.drawLine(QPointF(12, 2), QPointF(12, 22));
        drawArrowHead(p, QPointF(12, 2), -90);
        drawArrowHead(p, QPointF(12, 22), 90);
        break;
    case IconId::FitPage:
        p.drawRoundedRect(QRectF(6, 6, 12, 12), 1.5, 1.5);
        p.drawPolyline(QPolygonF({QPointF(3, 8), QPointF(3, 3), QPointF(8, 3)}));
        p.drawPolyline(QPolygonF({QPointF(16, 3), QPointF(21, 3), QPointF(21, 8)}));
        p.drawPolyline(QPolygonF({QPointF(21, 16), QPointF(21, 21), QPointF(16, 21)}));
        p.drawPolyline(QPolygonF({QPointF(8, 21), QPointF(3, 21), QPointF(3, 16)}));
        break;
    case IconId::ZoomIn:
    case IconId::ZoomOut:
        p.drawEllipse(QPointF(10, 10), 6.5, 6.5);
        p.drawLine(QPointF(14.8, 14.8), QPointF(21, 21));
        p.drawLine(QPointF(7, 10), QPointF(13, 10));
        if (id == IconId::ZoomIn)
            p.drawLine(QPointF(10, 7), QPointF(10, 13));
        break;
    case IconId::Pause:
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(8.5, 7.5, 2.4, 9));
        p.drawRect(QRectF(13.1, 7.5, 2.4, 9));
        break;
    case IconId::Text: // a serif "T"
        p.drawLine(QPointF(5, 5), QPointF(19, 5));
        p.drawLine(QPointF(5, 5), QPointF(5, 7.5));
        p.drawLine(QPointF(19, 5), QPointF(19, 7.5));
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(9, 19), QPointF(15, 19));
        break;
    case IconId::Triangle:
        p.drawPolygon(QPolygonF({QPointF(12, 4), QPointF(21, 19.5), QPointF(3, 19.5)}));
        break;
    case IconId::LineStyle: // solid, dashed and dotted lines
        p.drawLine(QPointF(3, 6), QPointF(21, 6));
        p.setPen(QPen(c, 1.8, Qt::DashLine, Qt::FlatCap));
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        p.setPen(QPen(c, 1.8, Qt::DotLine, Qt::RoundCap));
        p.drawLine(QPointF(3, 18), QPointF(21, 18));
        break;
    case IconId::ScreenRecord: // monitor with a record dot
        p.drawRoundedRect(QRectF(2.5, 4, 19, 13), 1.5, 1.5);
        p.drawLine(QPointF(9, 21), QPointF(15, 21));
        p.drawLine(QPointF(12, 17), QPointF(12, 21));
        p.setBrush(QColor(0xe5, 0x39, 0x35));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(12, 10.5), 3.2, 3.2);
        break;
    case IconId::Files: // stacked thumbnails
        p.drawRoundedRect(QRectF(3, 3, 8, 8), 1, 1);
        p.drawRoundedRect(QRectF(13, 3, 8, 8), 1, 1);
        p.drawRoundedRect(QRectF(3, 13, 8, 8), 1, 1);
        p.drawRoundedRect(QRectF(13, 13, 8, 8), 1, 1);
        break;
    case IconId::SelectAll:
        p.drawRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
        p.drawPolyline(QPolygonF({QPointF(7.5, 12), QPointF(10.5, 15), QPointF(16.5, 9)}));
        break;
    }
}

} // namespace

QIcon icon(IconId id, const QColor &color)
{
    QIcon ic;
    for (int size : {16, 24, 32, 48, 64, 128}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 24.0, size / 24.0);
        paint(p, id, color);
        p.end();
        ic.addPixmap(pm, QIcon::Normal);
        QPixmap dis(size, size);
        dis.fill(Qt::transparent);
        QPainter pd(&dis);
        pd.setRenderHint(QPainter::Antialiasing);
        pd.scale(size / 24.0, size / 24.0);
        paint(pd, id, QColor(160, 160, 160));
        pd.end();
        ic.addPixmap(dis, QIcon::Disabled);
    }
    return ic;
}

} // namespace lensscan
