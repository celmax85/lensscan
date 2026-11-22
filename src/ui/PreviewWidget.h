// SPDX-License-Identifier: GPL-3.0-or-later
// Live camera view: draws the latest frame, the detected page outline, a status
// message, and (in area mode) lets the user drag a selection rectangle.
#pragma once

#include <QImage>
#include <QPolygonF>
#include <QRectF>
#include <QWidget>

namespace lensscan {

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget *parent = nullptr);

    void setImage(const QImage &img);
    QImage image() const { return m_image; }
    void setPageOutline(const QPolygonF &normalized); // empty = none
    void setPageOutlines(const QVector<QPolygonF> &normalized);
    void setSelectionEnabled(bool on);
    QRectF selection() const { return m_selection; }  // normalized, empty if none
    void setSelection(const QRectF &normalized);
    void setMessage(const QString &text);              // overlay text, empty = none
    void flash();                                      // brief white flash after a capture

Q_SIGNALS:
    void selectionChanged(const QRectF &normalized);
    void framePainted();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QRectF imageRect() const;
    QPointF toNormalized(const QPointF &widgetPos) const;

    QImage m_image;
    QVector<QPolygonF> m_outlines;
    QRectF m_selection;
    QPointF m_dragStart;
    bool m_dragging = false;
    bool m_selectionEnabled = false;
    QString m_message;
    int m_flash = 0;
};

} // namespace lensscan
