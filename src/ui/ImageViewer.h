// SPDX-License-Identifier: GPL-3.0-or-later
// Built-in page viewer: one page at a time at 1:1, fitted to the width, the height or
// the whole window, with zoom, panning and previous / next page.
#pragma once

#include <QDialog>
#include <QImage>
#include <QStringList>

class QLabel;
class QScrollArea;
class QToolButton;

namespace lensscan {

class ViewerCanvas;

class ImageViewer : public QDialog {
    Q_OBJECT
public:
    enum class Fit { None, Page, Width, Height };

    ImageViewer(const QStringList &files, int current, QWidget *parent = nullptr);
    void setFiles(const QStringList &files, int current);
    QString currentFile() const { return m_files.value(m_index); }

Q_SIGNALS:
    void editRequested(const QString &file);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void showPage(int index);
    void setFit(Fit f);
    void setZoom(double zoom, const QPointF &anchor = QPointF(-1, -1)); // anchor in viewport px
    void applyFit();
    void updateLabels();

    QStringList m_files;
    int m_index = 0;
    QImage m_image;
    Fit m_fit = Fit::Page;
    double m_zoom = 1.0;
    QScrollArea *m_scroll;
    ViewerCanvas *m_canvas;
    QLabel *m_pageLabel, *m_zoomLabel;
    QToolButton *m_first = nullptr, *m_prev = nullptr, *m_next = nullptr, *m_last = nullptr;
    QPoint m_dragStart;
    QPoint m_scrollStart;
    bool m_dragging = false;
};

} // namespace lensscan
