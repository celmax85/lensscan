// SPDX-License-Identifier: GPL-3.0-or-later
#include "PreviewWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

namespace lensscan {

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    setMouseTracking(false);
}

void PreviewWidget::setImage(const QImage &img)
{
    m_image = img;
    update();
}

void PreviewWidget::setPageOutline(const QPolygonF &normalized)
{
    setPageOutlines(normalized.isEmpty() ? QVector<QPolygonF>() : QVector<QPolygonF>{normalized});
}

void PreviewWidget::setPageOutlines(const QVector<QPolygonF> &normalized)
{
    m_outlines = normalized;
    update();
}

void PreviewWidget::setSelectionEnabled(bool on)
{
    m_selectionEnabled = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void PreviewWidget::setSelection(const QRectF &normalized)
{
    m_selection = normalized;
    update();
}

void PreviewWidget::setMessage(const QString &text)
{
    m_message = text;
    update();
}

void PreviewWidget::flash()
{
    m_flash = 160;
    update();
    auto *t = new QTimer(this);
    t->setInterval(30);
    connect(t, &QTimer::timeout, this, [this, t] {
        m_flash = qMax(0, m_flash - 40);
        update();
        if (!m_flash)
            t->deleteLater();
    });
    t->start();
}

QRectF PreviewWidget::imageRect() const
{
    if (m_image.isNull())
        return {};
    const QSizeF s = QSizeF(m_image.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

QPointF PreviewWidget::toNormalized(const QPointF &pos) const
{
    const QRectF r = imageRect();
    if (r.isEmpty())
        return {};
    return QPointF(qBound(0.0, (pos.x() - r.x()) / r.width(), 1.0), qBound(0.0, (pos.y() - r.y()) / r.height(), 1.0));
}

void PreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x13, 0x14, 0x19));
    const QRectF r = imageRect();
    if (!m_image.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(r, m_image);
        p.setRenderHint(QPainter::Antialiasing);
        auto map = [&](const QPointF &n) { return QPointF(r.x() + n.x() * r.width(), r.y() + n.y() * r.height()); };
        if (!m_selectionEnabled) {
            p.setPen(QPen(QColor(0x2e, 0xcc, 0x71), 2.5));
            p.setBrush(QColor(0x2e, 0xcc, 0x71, 30));
            for (const QPolygonF &outline : m_outlines) {
                if (outline.size() < 3)
                    continue;
                QPolygonF poly;
                for (const QPointF &n : outline)
                    poly << map(n);
                p.drawPolygon(poly);
            }
        }
        if (m_selectionEnabled && !m_selection.isEmpty()) {
            const QRectF sel(map(m_selection.topLeft()), map(m_selection.bottomRight()));
            QPainterPath outside;
            outside.addRect(r);
            outside.addRect(sel);
            p.fillPath(outside, QColor(0, 0, 0, 120));
            p.setPen(QPen(QColor(0x3d, 0x8b, 0xfd), 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(sel);
        }
    }
    if (m_flash)
        p.fillRect(rect(), QColor(255, 255, 255, m_flash));
    if (!m_message.isEmpty()) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.15);
        p.setFont(f);
        const QRectF box = rect().adjusted(40, 0, -40, 0);
        QRectF tr = p.boundingRect(box, Qt::AlignCenter | Qt::TextWordWrap, m_message);
        tr.adjust(-16, -10, 16, 10);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(tr, 8, 8);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }
    if (!m_image.isNull())
        Q_EMIT framePainted();
}

void PreviewWidget::mousePressEvent(QMouseEvent *e)
{
    if (!m_selectionEnabled || e->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(e);
    m_dragging = true;
    m_dragStart = toNormalized(e->position());
    m_selection = QRectF(m_dragStart, QSizeF());
    update();
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return QWidget::mouseMoveEvent(e);
    m_selection = QRectF(m_dragStart, toNormalized(e->position())).normalized();
    update();
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return QWidget::mouseReleaseEvent(e);
    m_dragging = false;
    if (m_selection.width() < 0.02 || m_selection.height() < 0.02)
        m_selection = {};
    update();
    Q_EMIT selectionChanged(m_selection);
}

} // namespace lensscan
