// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresentationWindow.h"

#include "Icons.h"
#include "app/Shortcuts.h"
#include "imaging/ScanProcessor.h"
#include "media/ScreenCastPortal.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFutureWatcher>
#include <QImageReader>
#include <QListWidget>
#include <QPointer>
#include <QProcess>
#include <QUrl>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>

#include <cmath>

namespace lensscan {

namespace {

constexpr double kMaxZoom = 8.0;

QToolButton *barButton(IconId id, const QString &tip, QWidget *parent, bool checkable = false)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon(id, Qt::white));
    b->setIconSize(QSize(20, 20));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setCheckable(checkable);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QIcon colorIcon(const QColor &c)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::white, 1.5));
    p.setBrush(c);
    p.drawEllipse(QRectF(2, 2, 16, 16));
    return QIcon(pm);
}

QString withShortcut(const QString &tip, const QString &id)
{
    const QKeySequence k = Shortcuts::instance().key(id);
    return k.isEmpty() ? tip : QStringLiteral("%1 (%2)").arg(tip, k.toString(QKeySequence::NativeText));
}

QFont textFont(const Annotation &a)
{
    QFont f;
    f.setPixelSize(std::max(8, int(std::lround(a.width * 5 + 10)))); // content pixels
    f.setBold(true);
    return f;
}

// Box of a text annotation, its first point being the top-left corner.
QRectF textRect(const Annotation &a)
{
    if (a.points.isEmpty())
        return {};
    const QFontMetricsF fm(textFont(a));
    return fm.boundingRect(QRectF(a.points.first(), QSizeF(1e6, 1e6)), Qt::AlignLeft | Qt::AlignTop, a.text);
}

QPainterPath annotationPath(const Annotation &a)
{
    QPainterPath path;
    if (a.points.isEmpty())
        return path;
    const QPointF p0 = a.points.first(), p1 = a.points.last();
    switch (a.tool) {
    case DrawTool::Pen:
    case DrawTool::Highlighter:
        path.moveTo(p0);
        if (a.points.size() == 1)
            path.lineTo(p0 + QPointF(0.01, 0));
        for (int i = 1; i < a.points.size(); ++i)
            path.lineTo(a.points[i]);
        break;
    case DrawTool::Line:
        path.moveTo(p0);
        path.lineTo(p1);
        break;
    case DrawTool::Arrow: {
        path.moveTo(p0);
        path.lineTo(p1);
        const QLineF l(p1, p0);
        if (l.length() > 1) {
            const double head = std::min(l.length() * 0.5, a.width * 4 + 10);
            QLineF w1 = l;
            w1.setLength(head);
            w1.setAngle(l.angle() + 28);
            QLineF w2 = l;
            w2.setLength(head);
            w2.setAngle(l.angle() - 28);
            path.moveTo(w1.p2());
            path.lineTo(p1);
            path.lineTo(w2.p2());
        }
        break;
    }
    case DrawTool::Rect:
        path.addRect(QRectF(p0, p1).normalized());
        break;
    case DrawTool::Ellipse:
        path.addEllipse(QRectF(p0, p1).normalized());
        break;
    case DrawTool::Triangle: {
        const QRectF r = QRectF(p0, p1).normalized();
        path.moveTo(r.center().x(), r.top());
        path.lineTo(r.bottomRight());
        path.lineTo(r.bottomLeft());
        path.closeSubpath();
        break;
    }
    case DrawTool::Text:
        path.addRect(textRect(a));
        break;
    case DrawTool::Move:
    case DrawTool::Eraser:
        break;
    }
    return path;
}

} // namespace

// ---------------------------------------------------------------------------

PresentationCanvas::PresentationCanvas(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(false);
    setMinimumSize(320, 240);
    setTool(DrawTool::Move);
}

QImage PresentationCanvas::transformed(const QImage &raw) const
{
    QImage img = raw;
    if (m_rotation != 0)
        img = img.transformed(QTransform().rotate(m_rotation));
    if (m_flipH || m_flipV)
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        img = img.flipped((m_flipH ? Qt::Horizontal : Qt::Orientations()) | (m_flipV ? Qt::Vertical : Qt::Orientations()));
#else
        img = img.mirrored(m_flipH, m_flipV);
#endif
    return img;
}

void PresentationCanvas::setFrame(const QImage &frame)
{
    if (m_frozen && !m_content.isNull()) {
        Q_EMIT framePainted(); // let the camera keep streaming
        return;
    }
    const bool first = m_content.isNull();
    const QSize old = m_content.size();
    m_content = transformed(frame);
    if (first || old != m_content.size())
        m_center = QPointF(m_content.width() / 2.0, m_content.height() / 2.0);
    m_message.clear();
    Q_EMIT changed();
    update();
}

void PresentationCanvas::setTool(DrawTool t)
{
    m_tool = t;
    setCursor(t == DrawTool::Move ? Qt::OpenHandCursor : t == DrawTool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
}

void PresentationCanvas::mapAnnotations(const std::function<QPointF(const QPointF &)> &f)
{
    for (auto &h : m_undo)
        for (auto &a : h)
            for (auto &p : a.points)
                p = f(p);
    for (auto &a : m_items)
        for (auto &p : a.points)
            p = f(p);
}

void PresentationCanvas::rotate(int degrees)
{
    const double w = m_content.width(), h = m_content.height();
    if (degrees > 0)
        mapAnnotations([h](const QPointF &p) { return QPointF(h - p.y(), p.x()); });
    else
        mapAnnotations([w](const QPointF &p) { return QPointF(p.y(), w - p.x()); });
    m_rotation = (m_rotation + degrees + 360) % 360;
    if (!m_content.isNull())
        m_content = m_content.transformed(QTransform().rotate(degrees));
    resetZoom();
}

void PresentationCanvas::flip(bool horizontal)
{
    const double w = m_content.width(), h = m_content.height();
    // Mirroring after a quarter turn is the other mirror before it.
    const bool swap = m_rotation % 180 != 0;
    if (horizontal != swap)
        m_flipH = !m_flipH;
    else
        m_flipV = !m_flipV;
    if (horizontal)
        mapAnnotations([w](const QPointF &p) { return QPointF(w - p.x(), p.y()); });
    else
        mapAnnotations([h](const QPointF &p) { return QPointF(p.x(), h - p.y()); });
    if (!m_content.isNull())
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        m_content = m_content.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
#else
        m_content = m_content.mirrored(horizontal, !horizontal);
#endif
    Q_EMIT changed();
    update();
}

void PresentationCanvas::resetZoom()
{
    m_zoom = 1.0;
    m_center = QPointF(m_content.width() / 2.0, m_content.height() / 2.0);
    Q_EMIT zoomChanged(m_zoom);
    Q_EMIT changed();
    update();
}

void PresentationCanvas::pushUndo()
{
    m_undo.append(m_items);
    if (m_undo.size() > 200)
        m_undo.removeFirst();
}

void PresentationCanvas::undo()
{
    if (m_undo.isEmpty())
        return;
    m_items = m_undo.takeLast();
    Q_EMIT changed();
    update();
}

void PresentationCanvas::clearAnnotations()
{
    if (m_items.isEmpty())
        return;
    pushUndo();
    m_items.clear();
    Q_EMIT changed();
    update();
}

void PresentationCanvas::setMessage(const QString &text)
{
    m_message = text;
    update();
}

void PresentationCanvas::flash()
{
    m_flash = 4;
    QTimer::singleShot(120, this, [this] {
        m_flash = 0;
        update();
    });
    update();
}

QRectF PresentationCanvas::visibleRect() const
{
    const QSizeF s(m_content.width() / m_zoom, m_content.height() / m_zoom);
    return QRectF(m_center.x() - s.width() / 2, m_center.y() - s.height() / 2, s.width(), s.height());
}

QRectF PresentationCanvas::targetRect() const
{
    const QRectF v = visibleRect();
    if (v.isEmpty())
        return rect();
    const double s = std::min(width() / v.width(), height() / v.height());
    const QSizeF t(v.width() * s, v.height() * s);
    return QRectF((width() - t.width()) / 2, (height() - t.height()) / 2, t.width(), t.height());
}

double PresentationCanvas::viewScale() const
{
    const QRectF v = visibleRect();
    return v.isEmpty() ? 1.0 : targetRect().width() / v.width();
}

QPointF PresentationCanvas::toContent(const QPointF &pos) const
{
    const QRectF v = visibleRect(), t = targetRect();
    const double s = viewScale();
    return v.topLeft() + (pos - t.topLeft()) / s;
}

void PresentationCanvas::clampCenter()
{
    const QRectF v = visibleRect();
    const double hw = v.width() / 2, hh = v.height() / 2;
    m_center.setX(std::clamp(m_center.x(), hw, std::max(hw, m_content.width() - hw)));
    m_center.setY(std::clamp(m_center.y(), hh, std::max(hh, m_content.height() - hh)));
}

void PresentationCanvas::drawAnnotations(QPainter &p, const QVector<Annotation> &items) const
{
    for (const Annotation &a : items) {
        QColor c = a.color;
        double w = a.width;
        if (a.tool == DrawTool::Highlighter) {
            c.setAlpha(90);
            w *= 3;
        }
        if (a.tool == DrawTool::Text) {
            if (a.points.isEmpty())
                continue;
            p.setPen(c);
            p.setFont(textFont(a));
            p.drawText(textRect(a), Qt::AlignLeft | Qt::AlignTop, a.text);
            continue;
        }
        // Round caps would fill the gaps of dotted lines: flat caps for them.
        const Qt::PenCapStyle cap =
            a.tool == DrawTool::Highlighter || a.style != Qt::SolidLine ? Qt::FlatCap : Qt::RoundCap;
        p.setPen(QPen(c, w, a.style, cap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(annotationPath(a));
    }
}

void PresentationCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x14, 0x16, 0x1d));
    if (!m_content.isNull()) {
        const QRectF t = targetRect(), v = visibleRect();
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 3);
        p.drawImage(t, m_content, v);
        p.save();
        p.setClipRect(t);
        p.translate(t.topLeft());
        p.scale(viewScale(), viewScale());
        p.translate(-v.topLeft());
        p.setRenderHint(QPainter::Antialiasing);
        QVector<Annotation> all = m_items;
        if (m_drawing)
            all.append(m_current);
        drawAnnotations(p, all);
        p.restore();
        if (m_frozen) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 150));
            const QRectF badge(t.left() + 12, t.top() + 12, 110, 28);
            p.drawRoundedRect(badge, 6, 6);
            p.setPen(Qt::white);
            p.drawText(badge, Qt::AlignCenter, tr("Frozen"));
        }
    }
    if (!m_message.isEmpty()) {
        p.setPen(QColor(0xe8, 0xea, 0xf0));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.3);
        p.setFont(f);
        p.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }
    if (m_flash > 0)
        p.fillRect(rect(), QColor(255, 255, 255, 160));
    Q_EMIT framePainted();
}

QImage PresentationCanvas::renderView() const
{
    if (m_content.isNull())
        return {};
    QImage out(m_content.size(), QImage::Format_RGB32);
    QPainter p(&out);
    const QRectF v = visibleRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(out.rect()), m_content, v);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(m_zoom, m_zoom);
    p.translate(-v.topLeft());
    drawAnnotations(p, m_items);
    return out;
}

void PresentationCanvas::eraseAt(const QPointF &pos)
{
    const double radius = 10 / viewScale();
    for (int i = m_items.size() - 1; i >= 0; --i) {
        QPainterPathStroker st;
        st.setWidth(std::max(m_items[i].width * (m_items[i].tool == DrawTool::Highlighter ? 3 : 1), 1.0) + 2 * radius);
        st.setCapStyle(Qt::RoundCap);
        const bool hit = m_items[i].tool == DrawTool::Text
                             ? textRect(m_items[i]).adjusted(-radius, -radius, radius, radius).contains(pos)
                             : st.createStroke(annotationPath(m_items[i])).contains(pos);
        if (hit) {
            if (!m_erased) {
                pushUndo();
                m_erased = true;
            }
            m_items.removeAt(i);
            Q_EMIT changed();
            update();
        }
    }
}

void PresentationCanvas::mousePressEvent(QMouseEvent *e)
{
    if (m_content.isNull())
        return;
    const QPointF c = toContent(e->position());
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && m_tool == DrawTool::Move)) {
        m_panning = true;
        m_panStart = e->position();
        m_panCenter = m_center;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (m_tool == DrawTool::Eraser) {
        m_erased = false;
        m_drawing = false;
        eraseAt(c);
        return;
    }
    if (m_tool == DrawTool::Text) {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Text"), tr("Text to write on the picture:"),
                                                            QString(), &ok);
        if (!ok || text.trimmed().isEmpty())
            return;
        pushUndo();
        Annotation a{m_tool, m_color, m_penWidth / viewScale(), {c}, Qt::SolidLine, {}};
        a.text = text;
        m_items.append(a);
        Q_EMIT changed();
        update();
        return;
    }
    m_drawing = true;
    m_current = Annotation{m_tool, m_color, m_penWidth / viewScale(), {c}, Qt::SolidLine, {}};
    m_current.style = m_tool == DrawTool::Highlighter ? Qt::SolidLine : m_penStyle;
    if (m_tool != DrawTool::Pen && m_tool != DrawTool::Highlighter)
        m_current.points.append(c);
    update();
}

void PresentationCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_panning) {
        m_center = m_panCenter - (e->position() - m_panStart) / viewScale();
        clampCenter();
        Q_EMIT changed();
        update();
        return;
    }
    if (!(e->buttons() & Qt::LeftButton))
        return;
    const QPointF c = toContent(e->position());
    if (m_tool == DrawTool::Eraser) {
        eraseAt(c);
        return;
    }
    if (!m_drawing)
        return;
    if (m_tool == DrawTool::Pen || m_tool == DrawTool::Highlighter) {
        if (QLineF(m_current.points.last(), c).length() * viewScale() >= 1.5)
            m_current.points.append(c);
    } else {
        m_current.points.last() = c;
    }
    update();
}

void PresentationCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        m_panning = false;
        setTool(m_tool);
        return;
    }
    if (e->button() != Qt::LeftButton || !m_drawing)
        return;
    m_drawing = false;
    pushUndo();
    m_items.append(m_current);
    Q_EMIT changed();
    update();
}

void PresentationCanvas::wheelEvent(QWheelEvent *e)
{
    if (m_content.isNull())
        return;
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0)
        return;
    const QPointF before = toContent(e->position());
    m_zoom = std::clamp(m_zoom * std::pow(1.2, steps), 1.0, kMaxZoom);
    // Keep the point under the cursor in place.
    const QPointF after = toContent(e->position());
    m_center += before - after;
    clampCenter();
    Q_EMIT zoomChanged(m_zoom);
    Q_EMIT changed();
    update();
}

// ---------------------------------------------------------------------------

PresentationWindow::PresentationWindow(AppSettings &settings, QWidget *parent)
    : QWidget(parent), m_settings(settings)
{
    setWindowTitle(tr("LensScan — Presentation"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    m_canvas = new PresentationCanvas(this);
    l->addWidget(buildTopBar());
    auto *middle = new QHBoxLayout;
    middle->setSpacing(0);
    middle->addWidget(m_canvas, 1);
    m_media = new QListWidget(this);
    m_media->setViewMode(QListView::IconMode);
    m_media->setIconSize(QSize(160, 110));
    m_media->setGridSize(QSize(180, 150));
    m_media->setResizeMode(QListView::Adjust);
    m_media->setMovement(QListView::Static);
    m_media->setWordWrap(true);
    m_media->setFixedWidth(200);
    m_media->setContextMenuPolicy(Qt::CustomContextMenu);
    m_media->setToolTip(tr("Screenshots and videos. Double-click to open."));
    m_media->setStyleSheet(QStringLiteral("QListWidget { background: #1d2029; color: #e8eaf0; border: none; }"));
    m_media->hide();
    middle->addWidget(m_media);
    l->addLayout(middle, 1);
    connect(m_media, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem *it) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(Qt::UserRole).toString()));
    });
    connect(m_media, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *it = m_media->itemAt(pos);
        if (!it)
            return;
        const QString path = it->data(Qt::UserRole).toString();
        QMenu menu(this);
        menu.addAction(tr("Open"), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        menu.addAction(tr("Show in folder"), this, [path] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        });
        menu.addSeparator();
        menu.addAction(icon(IconId::Trash), tr("Delete"), this, [this, path] {
            if (QMessageBox::question(this, tr("Delete"), tr("Move %1 to the trash?").arg(QFileInfo(path).fileName())) !=
                QMessageBox::Yes)
                return;
            if (!QFile::moveToTrash(path) && !QFile::remove(path))
                QMessageBox::warning(this, tr("Delete"), tr("Cannot delete %1").arg(path));
            refreshMedia();
        });
        menu.exec(m_media->viewport()->mapToGlobal(pos));
    });
    connect(this, &PresentationWindow::fileSaved, this, [this] {
        if (m_media->isVisible())
            refreshMedia();
    });
    l->addWidget(buildToolBar());
    m_canvas->setMessage(tr("Connecting to the camera..."));

    m_camera = new CameraThread(this);
    m_camera->setPreviewSize(m_settings.videoSize);
    m_camera->setPreviewWidth(std::max(1920, m_settings.videoSize.width()));
    connect(m_camera, &CameraThread::cameraOpened, this, [this](const CameraDescription &cam) {
        m_cameraName = cam.name;
        m_videoSize->clear();
        m_videoSize->addItem(tr("Automatic"), QSize());
        for (int i = 0; i < cam.captureSizes.size(); ++i) {
            const QSize s = cam.captureSizes[i];
            const int fps = cam.captureFps.value(i);
            if (s.width() < 640 || (fps > 0 && fps < 5))
                continue;
            m_videoSize->addItem(fps > 0 ? tr("%1 × %2 (%3 fps)").arg(s.width()).arg(s.height()).arg(fps)
                                         : tr("%1 × %2").arg(s.width()).arg(s.height()),
                                 s);
            if (s == m_settings.videoSize)
                m_videoSize->setCurrentIndex(m_videoSize->count() - 1);
        }
        m_videoSize->setEnabled(!m_recordTimer.isActive());
        updateDeviceLabel(cam.previewSize);
    });
    connect(m_camera, &CameraThread::previewSizeChanged, this, &PresentationWindow::updateDeviceLabel);
    connect(m_camera, &CameraThread::cameraLost, this, [this](const QString &reason) {
        m_deviceLabel->setText(tr("No camera"));
        m_canvas->setMessage(reason);
        if (m_recordTimer.isActive())
            toggleRecording();
    });
    connect(m_camera, &CameraThread::previewFrame, this, &PresentationWindow::onFrame);
    connect(m_camera, &CameraThread::controlsAvailable, this, &PresentationWindow::applyControls);
    connect(m_canvas, &PresentationCanvas::framePainted, m_camera, &CameraThread::frameShown);
    connect(m_canvas, &PresentationCanvas::changed, this, [this] { m_dirty = true; });
    connect(m_canvas, &PresentationCanvas::zoomChanged, this,
            [this](double z) { m_zoomLabel->setText(QStringLiteral("%1 %").arg(qRound(z * 100))); });

    m_recordTimer.setInterval(40);
    connect(&m_recordTimer, &QTimer::timeout, this, [this] {
        if (!m_recorder.isRecording()) { // ffmpeg went away
            toggleRecording();
            return;
        }
        if (m_dirty && m_recorder.isCapturing() && !m_recorder.isScreen()) {
            m_dirty = false;
            m_recorder.addFrame(m_canvas->renderView());
        }
        updateRecordUi();
    });
    connect(&m_stopWatcher, &QFutureWatcher<QPair<bool, QString>>::finished, this, [this] {
        const auto r = m_stopWatcher.result();
        finishRecording(r.first, r.second);
    });

    // Microphones are listed by ffmpeg, in the background.
    connect(&m_sourcesWatcher, &QFutureWatcher<QList<AudioSource>>::finished, this, [this] {
        const QList<AudioSource> sources = m_sourcesWatcher.result();
        const QString saved = QSettings().value(QStringLiteral("presentation/audioSource")).toString();
        m_audio->clear();
        m_audio->addItem(tr("No sound"), QString());
        int select = -1;
        for (const AudioSource &s : sources) {
            m_audio->addItem(s.camera ? tr("%1 (camera)").arg(s.description) : s.description, s.name);
            if (s.name == saved)
                select = m_audio->count() - 1;
            else if (select < 0 && s.camera && saved.isEmpty())
                select = m_audio->count() - 1;
        }
        if (select < 0 && saved.isEmpty() && m_audio->count() > 1)
            select = 1;
        if (saved == QLatin1String("-"))
            select = 0;
        m_audio->setCurrentIndex(std::max(select, 0));
        m_audio->setEnabled(true);
    });
    if (!Recorder::ffmpegPath().isEmpty())
        m_sourcesWatcher.setFuture(QtConcurrent::run([] { return Recorder::audioSources(); }));

    auto &sc = Shortcuts::instance();
    sc.bind(QStringLiteral("present/freeze"), this, [this] { toggleFreeze(); });
    sc.bind(QStringLiteral("present/screenshot"), this, [this] { screenshot(); });
    sc.bind(QStringLiteral("present/record"), this, [this] {
        if (m_recordButton->isEnabled())
            toggleRecording();
    });
    sc.bind(QStringLiteral("present/pointer"), this, [this] { setTool(DrawTool::Move); });
    sc.bind(QStringLiteral("present/pen"), this, [this] { setTool(DrawTool::Pen); });
    sc.bind(QStringLiteral("present/highlighter"), this, [this] { setTool(DrawTool::Highlighter); });
    sc.bind(QStringLiteral("present/eraser"), this, [this] { setTool(DrawTool::Eraser); });
    sc.bind(QStringLiteral("present/undo"), this, [this] { m_canvas->undo(); });
    sc.bind(QStringLiteral("present/clear"), this, [this] { m_canvas->clearAnnotations(); });
    sc.bind(QStringLiteral("present/zoomReset"), this, [this] { m_canvas->resetZoom(); });
    sc.bind(QStringLiteral("present/fullscreen"), this, [this] { toggleFullScreen(); });
    sc.bind(QStringLiteral("present/back"), this, [this] { back(); });

    updateRecordUi();
    m_camera->start();
}

PresentationWindow::~PresentationWindow()
{
    if (m_camera->isRunning()) {
        m_camera->stop();
        m_camera->wait();
    }
    m_stopWatcher.waitForFinished();
    m_sourcesWatcher.waitForFinished();
}

QWidget *PresentationWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("topBar"));
    bar->setFixedHeight(48);
    auto *l = new QHBoxLayout(bar);
    l->setContentsMargins(8, 0, 8, 0);
    auto *backB = barButton(IconId::Back, withShortcut(tr("Back to the file manager"), QStringLiteral("present/back")), bar);
    backB->setText(tr("Back"));
    backB->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(backB, &QToolButton::clicked, this, &PresentationWindow::close);
    l->addWidget(backB);
    l->addSpacing(16);
    m_deviceLabel = new QLabel(tr("No camera"), bar);
    m_deviceLabel->setObjectName(QStringLiteral("topLabel"));
    l->addWidget(m_deviceLabel);
    l->addStretch(1);

    m_recLabel = new QLabel(bar);
    m_recLabel->setObjectName(QStringLiteral("topLabel"));
    l->addWidget(m_recLabel);
    l->addSpacing(8);
    auto *res = new QLabel(tr("Resolution:"), bar);
    res->setObjectName(QStringLiteral("topLabel"));
    l->addWidget(res);
    m_videoSize = new QComboBox(bar);
    m_videoSize->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_videoSize->setEnabled(false);
    m_videoSize->setToolTip(tr("Size of the camera picture for the live view, screenshots and videos. "
                               "Large sizes give a sharper picture at a lower frame rate."));
    connect(m_videoSize, qOverload<int>(&QComboBox::activated), this, [this] {
        const QSize s = m_videoSize->currentData().toSize();
        m_settings.videoSize = s;
        m_settings.save();
        m_camera->setPreviewWidth(std::max(1920, s.width()));
        m_camera->setPreviewSize(s);
    });
    l->addWidget(m_videoSize);
    l->addSpacing(8);
    auto *mic = new QLabel(tr("Sound:"), bar);
    mic->setObjectName(QStringLiteral("topLabel"));
    l->addWidget(mic);
    m_audio = new QComboBox(bar);
    m_audio->setMinimumWidth(180);
    m_audio->setEnabled(false);
    m_audio->addItem(tr("No sound"), QString());
    m_audio->setToolTip(tr("Microphone recorded with the video"));
    connect(m_audio, qOverload<int>(&QComboBox::activated), this, [this] {
        const QString name = m_audio->currentData().toString();
        QSettings().setValue(QStringLiteral("presentation/audioSource"), name.isEmpty() ? QStringLiteral("-") : name);
    });
    l->addWidget(m_audio);
    l->addSpacing(8);

    auto *shot = barButton(IconId::Screenshot, withShortcut(tr("Save a picture of the view"), QStringLiteral("present/screenshot")), bar);
    shot->setText(tr("Screenshot"));
    shot->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(shot, &QToolButton::clicked, this, &PresentationWindow::screenshot);
    l->addWidget(shot);
    m_recordButton = barButton(IconId::Record, QString(), bar);
    m_recordButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_recordButton, &QToolButton::clicked, this, &PresentationWindow::toggleRecording);
    l->addWidget(m_recordButton);
    m_pauseButton = barButton(IconId::Pause, tr("Pause the recording"), bar, true);
    connect(m_pauseButton, &QToolButton::clicked, this, &PresentationWindow::togglePause);
    l->addWidget(m_pauseButton);
    m_screenButton = barButton(IconId::ScreenRecord, QString(), bar);
    m_screenButton->setText(tr("Record screen"));
    m_screenButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_screenButton, &QToolButton::clicked, this, &PresentationWindow::toggleScreenRecording);
    l->addWidget(m_screenButton);
    m_mediaButton = barButton(IconId::Files, tr("Show the screenshots and videos"), bar, true);
    connect(m_mediaButton, &QToolButton::toggled, this, [this](bool on) {
        m_media->setVisible(on);
        if (on)
            refreshMedia();
    });
    l->addWidget(m_mediaButton);
    auto *full = barButton(IconId::Fullscreen, withShortcut(tr("Full screen"), QStringLiteral("present/fullscreen")), bar);
    connect(full, &QToolButton::clicked, this, &PresentationWindow::toggleFullScreen);
    l->addWidget(full);
    return bar;
}

QWidget *PresentationWindow::buildToolBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("topBar"));
    bar->setFixedHeight(46);
    bar->setStyleSheet(QStringLiteral("#topBar QToolButton:checked { background: #4c5680; }"));
    auto *l = new QHBoxLayout(bar);
    l->setContentsMargins(8, 0, 8, 0);
    l->setSpacing(2);

    m_tools = new QButtonGroup(this);
    m_tools->setExclusive(true);
    const struct {
        DrawTool tool;
        IconId icon;
        const char *tip;
        const char *shortcut;
    } tools[] = {
        {DrawTool::Move, IconId::Move, QT_TR_NOOP("Move and zoom (mouse wheel)"), "present/pointer"},
        {DrawTool::Pen, IconId::Pen, QT_TR_NOOP("Pen"), "present/pen"},
        {DrawTool::Highlighter, IconId::Highlighter, QT_TR_NOOP("Highlighter"), "present/highlighter"},
        {DrawTool::Line, IconId::Line, QT_TR_NOOP("Line"), nullptr},
        {DrawTool::Arrow, IconId::Arrow, QT_TR_NOOP("Arrow"), nullptr},
        {DrawTool::Rect, IconId::Rect, QT_TR_NOOP("Rectangle"), nullptr},
        {DrawTool::Ellipse, IconId::Ellipse, QT_TR_NOOP("Ellipse"), nullptr},
        {DrawTool::Triangle, IconId::Triangle, QT_TR_NOOP("Triangle"), nullptr},
        {DrawTool::Text, IconId::Text, QT_TR_NOOP("Text (click where it goes)"), nullptr},
        {DrawTool::Eraser, IconId::Eraser, QT_TR_NOOP("Eraser"), "present/eraser"},
    };
    for (const auto &t : tools) {
        const QString tip = tr(t.tip);
        auto *b = barButton(t.icon, t.shortcut ? withShortcut(tip, QString::fromLatin1(t.shortcut)) : tip, bar, true);
        m_tools->addButton(b, int(t.tool));
        l->addWidget(b);
    }
    m_tools->button(int(DrawTool::Move))->setChecked(true);
    connect(m_tools, &QButtonGroup::idClicked, this, [this](int id) { setTool(DrawTool(id)); });

    m_colorButton = barButton(IconId::Pen, tr("Color"), bar);
    m_colorButton->setPopupMode(QToolButton::InstantPopup);
    auto *colors = new QMenu(m_colorButton);
    const struct {
        QColor c;
        const char *name;
    } presets[] = {{QColor(0xe5, 0x39, 0x35), QT_TR_NOOP("Red")},   {QColor(0xfd, 0xd8, 0x35), QT_TR_NOOP("Yellow")},
                   {QColor(0x43, 0xa0, 0x47), QT_TR_NOOP("Green")}, {QColor(0x1e, 0x88, 0xe5), QT_TR_NOOP("Blue")},
                   {QColor(0x21, 0x21, 0x21), QT_TR_NOOP("Black")}, {QColor(0xff, 0xff, 0xff), QT_TR_NOOP("White")}};
    for (const auto &p : presets) {
        const QColor c = p.c;
        colors->addAction(colorIcon(c), tr(p.name), this, [this, c] { chooseColor(c); });
    }
    colors->addSeparator();
    colors->addAction(tr("Other color..."), this, [this] {
        const QColor c = QColorDialog::getColor(Qt::red, this, tr("Annotation color"));
        if (c.isValid())
            chooseColor(c);
    });
    m_colorButton->setMenu(colors);
    l->addWidget(m_colorButton);
    m_colorButton->setIcon(colorIcon(QColor(0xe5, 0x39, 0x35)));

    auto *width = new QComboBox(bar);
    width->addItem(tr("Thin"), 2);
    width->addItem(tr("Medium"), 4);
    width->addItem(tr("Thick"), 8);
    width->addItem(tr("Very thick"), 14);
    width->setCurrentIndex(1);
    width->setToolTip(tr("Line width"));
    width->setFocusPolicy(Qt::NoFocus);
    connect(width, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, width] { m_canvas->setPenWidth(width->currentData().toInt()); });
    l->addWidget(width);
    auto *style = new QComboBox(bar);
    style->addItem(icon(IconId::LineStyle), tr("Solid line"), int(Qt::SolidLine));
    style->addItem(tr("Dashed line"), int(Qt::DashLine));
    style->addItem(tr("Dotted line"), int(Qt::DotLine));
    style->addItem(tr("Dash and dot"), int(Qt::DashDotLine));
    style->setToolTip(tr("Line style of the pen, lines and shapes"));
    style->setFocusPolicy(Qt::NoFocus);
    connect(style, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, style] { m_canvas->setPenStyle(Qt::PenStyle(style->currentData().toInt())); });
    l->addWidget(style);
    auto *undo = barButton(IconId::Undo, withShortcut(tr("Undo"), QStringLiteral("present/undo")), bar);
    connect(undo, &QToolButton::clicked, m_canvas, &PresentationCanvas::undo);
    l->addWidget(undo);
    auto *clear = barButton(IconId::Trash, withShortcut(tr("Remove all annotations"), QStringLiteral("present/clear")), bar);
    connect(clear, &QToolButton::clicked, m_canvas, &PresentationCanvas::clearAnnotations);
    l->addWidget(clear);

    l->addStretch(1);

    m_rotL = barButton(IconId::RotateLeft, tr("Rotate left"), bar);
    connect(m_rotL, &QToolButton::clicked, this, [this] { m_canvas->rotate(-90); });
    l->addWidget(m_rotL);
    m_rotR = barButton(IconId::RotateRight, tr("Rotate right"), bar);
    connect(m_rotR, &QToolButton::clicked, this, [this] { m_canvas->rotate(90); });
    l->addWidget(m_rotR);
    auto *fh = barButton(IconId::FlipH, tr("Mirror left / right"), bar);
    connect(fh, &QToolButton::clicked, this, [this] { m_canvas->flip(true); });
    l->addWidget(fh);
    auto *fv = barButton(IconId::FlipV, tr("Mirror top / bottom"), bar);
    connect(fv, &QToolButton::clicked, this, [this] { m_canvas->flip(false); });
    l->addWidget(fv);
    auto *zr = barButton(IconId::ZoomReset, withShortcut(tr("Show the whole picture"), QStringLiteral("present/zoomReset")), bar);
    connect(zr, &QToolButton::clicked, m_canvas, &PresentationCanvas::resetZoom);
    l->addWidget(zr);
    m_zoomLabel = new QLabel(QStringLiteral("100 %"), bar);
    m_zoomLabel->setObjectName(QStringLiteral("topLabel"));
    m_zoomLabel->setMinimumWidth(48);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setToolTip(tr("Zoom: mouse wheel over the picture"));
    l->addWidget(m_zoomLabel);
    l->addSpacing(8);
    m_freezeButton = barButton(IconId::Freeze, withShortcut(tr("Freeze the picture"), QStringLiteral("present/freeze")), bar, true);
    m_freezeButton->setText(tr("Freeze"));
    m_freezeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_freezeButton, &QToolButton::clicked, this, &PresentationWindow::toggleFreeze);
    l->addWidget(m_freezeButton);
    return bar;
}

void PresentationWindow::onFrame(const QImage &frame)
{
    m_canvas->setFrame(frame);
}

void PresentationWindow::applyControls(const QVector<ControlInfo> &controls)
{
    for (const ControlInfo &c : controls) {
        const auto it = m_settings.cameraControls.constFind(QString::fromStdString(c.name));
        if (it != m_settings.cameraControls.constEnd() && it->toInt() != c.value)
            m_camera->setControl(c.id, it->toInt());
    }
}

void PresentationWindow::setTool(DrawTool t)
{
    if (auto *b = m_tools->button(int(t)))
        b->setChecked(true);
    m_canvas->setTool(t);
}

void PresentationWindow::chooseColor(const QColor &c)
{
    m_canvas->setColor(c);
    m_colorButton->setIcon(colorIcon(c));
    if (m_tools->checkedId() == int(DrawTool::Move) || m_tools->checkedId() == int(DrawTool::Eraser))
        setTool(DrawTool::Pen);
}

void PresentationWindow::toggleFreeze()
{
    m_canvas->setFrozen(!m_canvas->isFrozen());
    m_freezeButton->setChecked(m_canvas->isFrozen());
    m_canvas->update();
}

QString PresentationWindow::outputPath(const QString &dirPath, const QString &kind, const QString &suffix) const
{
    QDir dir(dirPath);
    dir.mkpath(QStringLiteral("."));
    const QString prefix = m_settings.filePrefix + QLatin1Char('_') + kind;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString path = dir.filePath(QStringLiteral("%1_%2.%3").arg(prefix, stamp, suffix));
    for (int i = 2; QFileInfo::exists(path); ++i)
        path = dir.filePath(QStringLiteral("%1_%2_%3.%4").arg(prefix, stamp).arg(i).arg(suffix));
    return path;
}

void PresentationWindow::screenshot()
{
    const QImage img = m_canvas->renderView();
    if (img.isNull())
        return;
    const QString path =
        outputPath(m_settings.snapshotFolder(), QStringLiteral("capture"), m_settings.snapshotFormat);
    if (!saveImage(img, path, m_settings.jpegQuality)) {
        QMessageBox::warning(this, tr("Screenshot"), tr("Cannot write %1").arg(path));
        return;
    }
    m_canvas->flash();
    m_recLabel->setText(tr("Saved %1").arg(QFileInfo(path).fileName()));
    QTimer::singleShot(4000, this, &PresentationWindow::updateRecordUi);
    Q_EMIT fileSaved(path);
}

void PresentationWindow::stopRecording()
{
    if (m_stopWatcher.isRunning() || !m_recordTimer.isActive())
        return;
    m_recordTimer.stop();
    m_recordButton->setEnabled(false);
    m_screenButton->setEnabled(false);
    m_pauseButton->setEnabled(false);
    m_recLabel->setText(tr("Saving the video..."));
    m_stopWatcher.setFuture(QtConcurrent::run([this] {
        QString err;
        const bool ok = m_recorder.stop(&err);
        return qMakePair(ok, err);
    }));
}

void PresentationWindow::toggleRecording()
{
    if (m_stopWatcher.isRunning())
        return;
    if (m_recordTimer.isActive()) {
        if (!m_recorder.isScreen())
            stopRecording();
        return;
    }
    const QSize size = m_canvas->contentSize();
    if (size.isEmpty())
        return;
    m_videoPath = outputPath(m_settings.videoFolder(), QStringLiteral("video"), m_settings.videoFormat);
    QString err;
    if (!m_recorder.start(m_videoPath, size, 25, m_audio->currentData().toString(), &err)) {
        QMessageBox::warning(this, tr("Recording"), err);
        return;
    }
    m_dirty = true;
    m_recordTimer.start();
    m_rotL->setEnabled(false);
    m_rotR->setEnabled(false);
    m_audio->setEnabled(false);
    m_videoSize->setEnabled(false);
    updateRecordUi();
}

void PresentationWindow::toggleScreenRecording()
{
    if (m_stopWatcher.isRunning())
        return;
    if (m_recordTimer.isActive()) {
        if (m_recorder.isScreen())
            stopRecording();
        return;
    }
    const QString problem = Recorder::screenRecordingProblem();
    if (!problem.isEmpty()) {
        QMessageBox::information(this, tr("Record screen"), problem);
        return;
    }
    if (!Recorder::isWayland()) {
        startScreenRecording(-1, 0);
        return;
    }
    if (!ScreenCastPortal::isAvailable()) {
        QMessageBox::information(this, tr("Record screen"),
                                 tr("The desktop does not offer screen sharing (xdg-desktop-portal). Install the "
                                    "portal of your desktop, for example xdg-desktop-portal-gnome or "
                                    "xdg-desktop-portal-kde."));
        return;
    }
    if (!m_portal) {
        m_portal = new ScreenCastPortal(this);
        connect(m_portal, &ScreenCastPortal::started, this, &PresentationWindow::startScreenRecording);
        connect(m_portal, &ScreenCastPortal::failed, this, [this](const QString &error) {
            m_screenButton->setEnabled(true);
            updateRecordUi();
            if (!error.isEmpty())
                QMessageBox::warning(this, tr("Record screen"), error);
        });
    }
    m_screenButton->setEnabled(false);
    m_recLabel->setText(tr("Choose the screen to record..."));
    m_portal->start();
}

void PresentationWindow::startScreenRecording(int pipewireFd, quint32 node)
{
    m_screenButton->setEnabled(true);
    m_videoPath = outputPath(m_settings.screenFolder(), QStringLiteral("screen"), m_settings.screenFormat);
    QString err;
    if (!m_recorder.startScreen(m_videoPath, 25, m_audio->currentData().toString(), pipewireFd, node, &err)) {
        if (m_portal)
            m_portal->close();
        updateRecordUi();
        QMessageBox::warning(this, tr("Record screen"), err);
        return;
    }
    m_recordTimer.start();
    m_audio->setEnabled(false);
    updateRecordUi();
}

void PresentationWindow::togglePause()
{
    if (!m_recordTimer.isActive())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    bool ok;
    if (m_recorder.isPaused()) {
        int fd = -1;
        if (m_portal && m_portal->isActive())
            fd = m_portal->openRemote(&err);
        ok = (fd >= 0 || !m_portal || !m_portal->isActive()) && m_recorder.resume(&err, fd);
        m_dirty = true;
    } else {
        ok = m_recorder.pause(&err);
    }
    QApplication::restoreOverrideCursor();
    updateRecordUi();
    if (!ok && !err.isEmpty())
        QMessageBox::warning(this, tr("Recording"), err);
}

void PresentationWindow::finishRecording(bool ok, const QString &error)
{
    m_rotL->setEnabled(true);
    m_rotR->setEnabled(true);
    m_videoSize->setEnabled(m_videoSize->count() > 0);
    m_audio->setEnabled(m_audio->count() > 1 || !m_sourcesWatcher.isRunning());
    m_screenButton->setEnabled(true);
    if (m_portal)
        m_portal->close(); // ends the screen sharing
    updateRecordUi();
    if (!ok) {
        QMessageBox::warning(this, tr("Recording"),
                             tr("The video could not be saved correctly.\n\n%1").arg(error));
        return;
    }
    m_recLabel->setText(tr("Saved %1").arg(QFileInfo(m_videoPath).fileName()));
    QTimer::singleShot(6000, this, &PresentationWindow::updateRecordUi);
    Q_EMIT fileSaved(m_videoPath);
}

void PresentationWindow::refreshMedia()
{
    // Files of the presentation mode: "<prefix>_capture_...", "_video_", "_screen_".
    QStringList dirs = {m_settings.snapshotFolder(), m_settings.videoFolder(), m_settings.screenFolder()};
    dirs.removeDuplicates();
    const QString prefix = m_settings.filePrefix;
    const QString ffmpeg = Recorder::ffmpegPath();
    QPointer<QListWidget> list = m_media;
    auto *watcher = new QFutureWatcher<QList<QPair<QString, QImage>>>(this);
    connect(watcher, &QFutureWatcher<QList<QPair<QString, QImage>>>::finished, this, [this, watcher, list] {
        const auto items = watcher->result();
        watcher->deleteLater();
        if (!list)
            return;
        list->clear();
        for (const auto &item : items) {
            auto *it = new QListWidgetItem(QFileInfo(item.first).fileName(), list);
            it->setData(Qt::UserRole, item.first);
            it->setToolTip(item.first);
            const QPixmap pm = item.second.isNull() ? icon(IconId::Record, Qt::white).pixmap(64, 64)
                                                    : QPixmap::fromImage(item.second);
            it->setIcon(QIcon(pm));
        }
        if (items.isEmpty()) {
            auto *it = new QListWidgetItem(tr("No screenshot or video yet"), list);
            it->setFlags(Qt::NoItemFlags);
        }
    });
    watcher->setFuture(QtConcurrent::run([dirs, prefix, ffmpeg] {
        QFileInfoList files;
        const QStringList kinds = {QStringLiteral("capture"), QStringLiteral("video"), QStringLiteral("screen")};
        for (const QString &d : dirs)
            for (const QFileInfo &fi : QDir(d).entryInfoList(
                     {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.bmp"),
                      QStringLiteral("*.mp4"), QStringLiteral("*.mkv")},
                     QDir::Files)) {
                for (const QString &k : kinds)
                    if (fi.fileName().startsWith(prefix + QLatin1Char('_') + k + QLatin1Char('_'))) {
                        files << fi;
                        break;
                    }
            }
        std::sort(files.begin(), files.end(),
                  [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() > b.lastModified(); });
        QList<QPair<QString, QImage>> out;
        for (const QFileInfo &fi : std::as_const(files)) {
            const QString path = fi.absoluteFilePath();
            QImage thumb;
            const QString suffix = fi.suffix().toLower();
            if (suffix == QLatin1String("mp4") || suffix == QLatin1String("mkv")) {
                if (!ffmpeg.isEmpty()) {
                    QProcess p;
                    p.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                                     QStringLiteral("-i"), path, QStringLiteral("-frames:v"), QStringLiteral("1"),
                                     QStringLiteral("-vf"), QStringLiteral("scale=160:-2"), QStringLiteral("-f"),
                                     QStringLiteral("image2pipe"), QStringLiteral("-c:v"), QStringLiteral("png"),
                                     QStringLiteral("-")});
                    if (p.waitForFinished(10000))
                        thumb.loadFromData(p.readAllStandardOutput());
                }
            } else {
                QImageReader r(path);
                const QSize s = r.size();
                if (s.isValid())
                    r.setScaledSize(s.scaled(160, 110, Qt::KeepAspectRatio));
                thumb = r.read();
            }
            out.append({path, thumb});
        }
        return out;
    }));
}

void PresentationWindow::updateRecordUi()
{
    const bool haveFfmpeg = !Recorder::ffmpegPath().isEmpty();
    const bool recording = m_recordTimer.isActive();
    const bool screen = recording && m_recorder.isScreen();
    const bool saving = m_stopWatcher.isRunning();
    m_pauseButton->setVisible(recording);
    m_pauseButton->setEnabled(recording && !saving);
    m_pauseButton->setChecked(m_recorder.isPaused());
    m_pauseButton->setIcon(icon(m_recorder.isPaused() ? IconId::Record : IconId::Pause, Qt::white));
    m_pauseButton->setToolTip(m_recorder.isPaused() ? tr("Resume the recording") : tr("Pause the recording"));
    if (recording) {
        const qint64 s = m_recorder.elapsedMs() / 1000;
        const QString time = QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
        m_recLabel->setText(m_recorder.isPaused()
                                ? tr("Paused %1").arg(time)
                                : QStringLiteral("<span style='color:#ff5252'>●</span> %1%2")
                                      .arg(screen ? tr("Screen") + QLatin1Char(' ') : QString(), time));
    } else if (!saving && m_screenButton->isEnabled()) {
        m_recLabel->clear();
    }
    // Camera recording button.
    const bool cameraRec = recording && !screen;
    m_recordButton->setIcon(icon(cameraRec ? IconId::Stop : IconId::Record, Qt::white));
    m_recordButton->setText(cameraRec ? tr("Stop") : tr("Record"));
    m_recordButton->setEnabled(haveFfmpeg && !saving && !screen);
    m_recordButton->setToolTip(!haveFfmpeg ? tr("Video recording needs the ffmpeg program (sudo apt install ffmpeg)")
                               : cameraRec ? withShortcut(tr("Stop the recording"), QStringLiteral("present/record"))
                                           : withShortcut(tr("Record a video of the view, with sound"),
                                                          QStringLiteral("present/record")));
    // Screen recording button.
    m_screenButton->setIcon(icon(screen ? IconId::Stop : IconId::ScreenRecord, Qt::white));
    m_screenButton->setText(screen ? tr("Stop") : tr("Record screen"));
    if (!saving && !(m_portal && !recording && !m_screenButton->isEnabled()))
        m_screenButton->setEnabled(haveFfmpeg && !cameraRec);
    m_screenButton->setToolTip(screen ? tr("Stop the screen recording")
                                      : tr("Record the computer screen with sound, for example to film a "
                                           "presentation made with other programs"));
}

void PresentationWindow::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void PresentationWindow::back()
{
    if (isFullScreen())
        showNormal();
    else
        close();
}

void PresentationWindow::closeEvent(QCloseEvent *e)
{
    if (m_recordTimer.isActive() || m_stopWatcher.isRunning()) {
        const bool active = m_recordTimer.isActive();
        m_recordTimer.stop();
        m_stopWatcher.waitForFinished();
        if (active) {
            QString err;
            const bool ok = m_recorder.stop(&err);
            if (ok)
                Q_EMIT fileSaved(m_videoPath);
        }
    }
    if (m_portal)
        m_portal->close();
    if (m_camera->isRunning()) {
        m_camera->stop();
        m_camera->wait();
    }
    Q_EMIT closed();
    e->accept();
}

void PresentationWindow::updateDeviceLabel(const QSize &size)
{
    m_deviceLabel->setText(m_cameraName);
    m_deviceLabel->setToolTip(QStringLiteral("%1 — %2 × %3").arg(m_cameraName).arg(size.width()).arg(size.height()));
    // "Automatic" shows the size it chose.
    if (m_videoSize->count() == 0)
        return;
    const bool automatic = !m_videoSize->currentData().toSize().isValid();
    m_videoSize->setItemText(0, automatic ? tr("Automatic (%1 × %2)").arg(size.width()).arg(size.height())
                                          : tr("Automatic"));
    m_videoSize->setMinimumWidth(m_videoSize->sizeHint().width());
}

} // namespace lensscan
