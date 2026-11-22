// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImageViewer.h"

#include "export/Exporters.h"
#include "ui/Icons.h"

#include <QApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace lensscan {

// Paints the page scaled to its own size (the scroll area handles panning).
class ViewerCanvas : public QWidget {
public:
    explicit ViewerCanvas(QWidget *parent) : QWidget(parent) {}
    void setImage(const QImage &img) { m_image = img; update(); }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, width() < m_image.width());
        if (!m_image.isNull())
            p.drawImage(rect(), m_image);
        Q_UNUSED(e)
    }

private:
    QImage m_image;
};

namespace {

constexpr double kMinZoom = 0.02, kMaxZoom = 8.0;

QToolButton *toolButton(IconId id, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon(id));
    b->setIconSize(QSize(22, 22));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

} // namespace

ImageViewer::ImageViewer(const QStringList &files, int current, QWidget *parent) : QDialog(parent)
{
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1000, 800);

    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    auto *bar = new QWidget(this);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(8, 4, 8, 4);
    m_first = toolButton(IconId::First, tr("First page (Home)"), bar);
    m_prev = toolButton(IconId::Previous, tr("Previous page (Left, Page Up)"), bar);
    m_next = toolButton(IconId::Next, tr("Next page (Right, Page Down)"), bar);
    m_last = toolButton(IconId::Last, tr("Last page (End)"), bar);
    connect(m_first, &QToolButton::clicked, this, [this] { showPage(0); });
    connect(m_prev, &QToolButton::clicked, this, [this] { showPage(m_index - 1); });
    connect(m_next, &QToolButton::clicked, this, [this] { showPage(m_index + 1); });
    connect(m_last, &QToolButton::clicked, this, [this] { showPage(int(m_files.size()) - 1); });
    bl->addWidget(m_first);
    bl->addWidget(m_prev);
    m_pageLabel = new QLabel(bar);
    m_pageLabel->setMinimumWidth(80);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    bl->addWidget(m_pageLabel);
    bl->addWidget(m_next);
    bl->addWidget(m_last);
    bl->addStretch(1);

    auto *actual = toolButton(IconId::ActualSize, tr("Actual size, 1:1 (1)"), bar);
    connect(actual, &QToolButton::clicked, this, [this] {
        m_fit = Fit::None;
        setZoom(1.0);
    });
    auto *fitW = toolButton(IconId::FitWidth, tr("Fit the width (W)"), bar);
    connect(fitW, &QToolButton::clicked, this, [this] { setFit(Fit::Width); });
    auto *fitH = toolButton(IconId::FitHeight, tr("Fit the height (H)"), bar);
    connect(fitH, &QToolButton::clicked, this, [this] { setFit(Fit::Height); });
    auto *fitP = toolButton(IconId::FitPage, tr("Whole page (F)"), bar);
    connect(fitP, &QToolButton::clicked, this, [this] { setFit(Fit::Page); });
    auto *zoomOut = toolButton(IconId::ZoomOut, tr("Zoom out (-)"), bar);
    connect(zoomOut, &QToolButton::clicked, this, [this] {
        m_fit = Fit::None;
        setZoom(m_zoom / 1.25);
    });
    auto *zoomIn = toolButton(IconId::ZoomIn, tr("Zoom in (+)"), bar);
    connect(zoomIn, &QToolButton::clicked, this, [this] {
        m_fit = Fit::None;
        setZoom(m_zoom * 1.25);
    });
    for (auto *b : {actual, fitW, fitH, fitP, zoomOut})
        bl->addWidget(b);
    m_zoomLabel = new QLabel(bar);
    m_zoomLabel->setMinimumWidth(52);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    bl->addWidget(m_zoomLabel);
    bl->addWidget(zoomIn);
    bl->addStretch(1);
    auto *edit = new QPushButton(icon(IconId::Crop), tr("Edit..."), bar);
    edit->setToolTip(tr("Crop or rotate this page"));
    edit->setFocusPolicy(Qt::NoFocus);
    connect(edit, &QPushButton::clicked, this, [this] {
        if (!currentFile().isEmpty())
            Q_EMIT editRequested(currentFile());
    });
    bl->addWidget(edit);
    l->addWidget(bar);

    m_scroll = new QScrollArea(this);
    m_scroll->setAlignment(Qt::AlignCenter);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setBackgroundRole(QPalette::Dark);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #3a3d45; }"));
    m_canvas = new ViewerCanvas(m_scroll);
    m_scroll->setWidget(m_canvas);
    m_scroll->viewport()->installEventFilter(this);
    m_canvas->installEventFilter(this);
    m_scroll->viewport()->setCursor(Qt::OpenHandCursor);
    l->addWidget(m_scroll, 1);

    setFiles(files, current);
}

void ImageViewer::setFiles(const QStringList &files, int current)
{
    m_files = files;
    showPage(std::clamp(current, 0, std::max(0, int(files.size()) - 1)));
}

void ImageViewer::showPage(int index)
{
    if (m_files.isEmpty()) {
        m_image = QImage();
        m_canvas->setImage(m_image);
        updateLabels();
        return;
    }
    m_index = std::clamp(index, 0, int(m_files.size()) - 1);
    m_image = loadScan(m_files[m_index]);
    m_canvas->setImage(m_image);
    setWindowTitle(tr("%1 - LensScan").arg(QFileInfo(m_files[m_index]).fileName()));
    applyFit();
    updateLabels();
}

void ImageViewer::setFit(Fit f)
{
    m_fit = f;
    applyFit();
}

void ImageViewer::applyFit()
{
    if (m_image.isNull())
        return;
    const QSize vp = m_scroll->viewport()->size();
    const int sb = m_scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    double z = m_zoom;
    switch (m_fit) {
    case Fit::None:
        break;
    case Fit::Page:
        z = std::min(double(vp.width()) / m_image.width(), double(vp.height()) / m_image.height());
        break;
    case Fit::Width: // leave room for the vertical scroll bar it will need
        z = double(vp.width() - (m_image.height() * vp.width() / m_image.width() > vp.height() ? sb : 0)) /
            m_image.width();
        break;
    case Fit::Height:
        z = double(vp.height() - (m_image.width() * vp.height() / m_image.height() > vp.width() ? sb : 0)) /
            m_image.height();
        break;
    }
    setZoom(z);
}

void ImageViewer::setZoom(double zoom, const QPointF &anchor)
{
    if (m_image.isNull())
        return;
    const QPointF a = anchor.x() < 0 ? QPointF(m_scroll->viewport()->width() / 2.0, m_scroll->viewport()->height() / 2.0)
                                     : anchor;
    // Image point under the anchor, kept in place.
    const QPointF canvasPos = m_canvas->mapFrom(m_scroll->viewport(), a.toPoint());
    const QPointF imagePos = canvasPos / std::max(m_zoom, 1e-6);
    m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    m_canvas->resize(std::max(1, int(std::lround(m_image.width() * m_zoom))),
                     std::max(1, int(std::lround(m_image.height() * m_zoom))));
    const QPointF newCanvasPos = imagePos * m_zoom;
    m_scroll->horizontalScrollBar()->setValue(int(newCanvasPos.x() - a.x()));
    m_scroll->verticalScrollBar()->setValue(int(newCanvasPos.y() - a.y()));
    updateLabels();
}

void ImageViewer::updateLabels()
{
    const int n = int(m_files.size());
    m_pageLabel->setText(n ? QStringLiteral("%1 / %2").arg(m_index + 1).arg(n) : QString());
    m_zoomLabel->setText(QStringLiteral("%1 %").arg(std::lround(m_zoom * 100)));
    m_first->setEnabled(m_index > 0);
    m_prev->setEnabled(m_index > 0);
    m_next->setEnabled(m_index + 1 < n);
    m_last->setEnabled(m_index + 1 < n);
}

void ImageViewer::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    applyFit();
}

void ImageViewer::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        showPage(m_index - 1);
        return;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        showPage(m_index + 1);
        return;
    case Qt::Key_Home:
        showPage(0);
        return;
    case Qt::Key_End:
        showPage(int(m_files.size()) - 1);
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_fit = Fit::None;
        setZoom(m_zoom * 1.25);
        return;
    case Qt::Key_Minus:
        m_fit = Fit::None;
        setZoom(m_zoom / 1.25);
        return;
    case Qt::Key_1:
        m_fit = Fit::None;
        setZoom(1.0);
        return;
    case Qt::Key_W:
        setFit(Fit::Width);
        return;
    case Qt::Key_H:
        setFit(Fit::Height);
        return;
    case Qt::Key_F:
        setFit(Fit::Page);
        return;
    default:
        QDialog::keyPressEvent(e);
    }
}

bool ImageViewer::eventFilter(QObject *obj, QEvent *e)
{
    if (obj != m_scroll->viewport() && obj != m_canvas)
        return QDialog::eventFilter(obj, e);
    switch (e->type()) {
    case QEvent::Wheel: {
        auto *w = static_cast<QWheelEvent *>(e);
        if (w->modifiers() & Qt::ControlModifier) {
            m_fit = Fit::None;
            setZoom(m_zoom * std::pow(1.2, w->angleDelta().y() / 120.0),
                    m_scroll->viewport()->mapFromGlobal(w->globalPosition().toPoint()));
            return true;
        }
        return false;
    }
    case QEvent::MouseButtonPress: {
        auto *m = static_cast<QMouseEvent *>(e);
        if (m->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragStart = m->globalPosition().toPoint();
            m_scrollStart = QPoint(m_scroll->horizontalScrollBar()->value(), m_scroll->verticalScrollBar()->value());
            m_scroll->viewport()->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        return false;
    }
    case QEvent::MouseMove:
        if (m_dragging) {
            const QPoint d = static_cast<QMouseEvent *>(e)->globalPosition().toPoint() - m_dragStart;
            m_scroll->horizontalScrollBar()->setValue(m_scrollStart.x() - d.x());
            m_scroll->verticalScrollBar()->setValue(m_scrollStart.y() - d.y());
            return true;
        }
        return false;
    case QEvent::MouseButtonRelease:
        if (m_dragging) {
            m_dragging = false;
            m_scroll->viewport()->setCursor(Qt::OpenHandCursor);
            return true;
        }
        return false;
    case QEvent::MouseButtonDblClick:
        setFit(m_fit == Fit::Page ? Fit::Width : Fit::Page);
        return true;
    default:
        return false;
    }
}

} // namespace lensscan
