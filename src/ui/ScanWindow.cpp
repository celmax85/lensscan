// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScanWindow.h"

#include "PreviewWidget.h"
#include "app/Shortcuts.h"
#include "imaging/BatchTools.h"
#include "imaging/BookProcessor.h"
#include "imaging/PageDetector.h"
#include "ui/Icons.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace lensscan {

namespace {
QToolButton *topButton(IconId id, const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("topButton"));
    b->setIcon(icon(id, Qt::white));
    b->setIconSize(QSize(20, 20));
    b->setToolTip(tip);
    if (!text.isEmpty()) {
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("sectionLabel"));
    return l;
}
} // namespace

ScanWindow::ScanWindow(AppSettings &settings, QWidget *parent) : QWidget(parent), m_settings(settings)
{
    setWindowTitle(tr("LensScan - Scan"));
    setWindowIcon(icon(IconId::App));
    setObjectName(QStringLiteral("scanWindow"));
    resize(1280, 820);
    setMinimumSize(900, 600);

    m_camera = new CameraThread(this);
    connect(m_camera, &CameraThread::cameraOpened, this, &ScanWindow::onCameraOpened);
    connect(m_camera, &CameraThread::cameraLost, this, &ScanWindow::onCameraLost);
    connect(m_camera, &CameraThread::previewFrame, this, &ScanWindow::onPreviewFrame);
    connect(m_camera, &CameraThread::captured, this, &ScanWindow::onCaptured);
    connect(m_camera, &CameraThread::captureFailed, this, &ScanWindow::onCaptureFailed);
    connect(m_camera, &CameraThread::controlsAvailable, this, &ScanWindow::controlsAvailable);
    connect(&m_detectWatcher, &QFutureWatcher<QVector<QPolygonF>>::finished, this, [this] {
        if (m_settings.pageMode == PageMode::FlatPage || m_settings.pageMode == PageMode::Book ||
            m_settings.pageMode == PageMode::MultiDocument)
            m_preview->setPageOutlines(m_detectWatcher.result());
    });

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTopBar());

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    auto *center = new QVBoxLayout;
    center->setContentsMargins(0, 0, 0, 0);
    center->setSpacing(0);
    m_preview = new PreviewWidget(this);
    connect(m_preview, &PreviewWidget::framePainted, m_camera, &CameraThread::frameShown);
    center->addWidget(m_preview, 1);
    center->addWidget(buildBottomBar());
    body->addLayout(center, 1);

    body->addWidget(buildToolPane());

    m_pages = new QListWidget(this);
    m_pages->setObjectName(QStringLiteral("pageList"));
    m_pages->setViewMode(QListView::IconMode);
    m_pages->setFlow(QListView::TopToBottom);
    m_pages->setWrapping(false);
    m_pages->setIconSize(QSize(120, 120));
    m_pages->setFixedWidth(160);
    m_pages->setSpacing(6);
    m_pages->setMovement(QListView::Static);
    m_pages->setToolTip(tr("Pages scanned in this session. Double-click to open."));
    connect(m_pages, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem *it) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(Qt::UserRole).toString()));
    });
    body->addWidget(m_pages);
    root->addLayout(body, 1);

    reloadSettings();
    onCameraLost(tr("Looking for the camera..."));
}

ScanWindow::~ScanWindow()
{
    stopCamera();
    for (auto *w : m_jobs)
        w->waitForFinished();
    m_detectWatcher.waitForFinished();
}

QWidget *ScanWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("topBar"));
    bar->setFixedHeight(48);
    auto *l = new QHBoxLayout(bar);
    l->setContentsMargins(8, 0, 8, 0);
    l->setSpacing(6);

    auto *back = topButton(IconId::Back, tr("Back"),
                           tr("Back to the file manager to edit pages and export documents"), bar);
    connect(back, &QToolButton::clicked, this, &ScanWindow::close);
    auto &sc = Shortcuts::instance();
    sc.bind(QStringLiteral("scan/back"), this, [this] { close(); });
    sc.bind(QStringLiteral("scan/scan"), this, [this] { scan(); });
    sc.bind(QStringLiteral("scan/rotateLeft"), this, [this] { rotate(-90); });
    sc.bind(QStringLiteral("scan/rotateRight"), this, [this] { rotate(90); });
    l->addWidget(back);
    l->addSpacing(16);

    m_deviceLabel = new QLabel(bar);
    m_deviceLabel->setObjectName(QStringLiteral("topLabel"));
    l->addWidget(m_deviceLabel);
    l->addStretch(1);

    m_folderLabel = new QLabel(bar);
    m_folderLabel->setObjectName(QStringLiteral("topLabel"));
    m_folderLabel->setMaximumWidth(380);
    l->addWidget(m_folderLabel);
    auto *setPath = topButton(IconId::Folder, QString(), tr("Choose where scans are saved"), bar);
    connect(setPath, &QToolButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Scan folder"), m_settings.outputDir);
        if (!d.isEmpty()) {
            m_settings.outputDir = d;
            m_settings.save();
            updateFolderLabel();
        }
    });
    l->addWidget(setPath);
    auto *openPath = topButton(IconId::FolderOpen, QString(), tr("Open the scan folder"), bar);
    connect(openPath, &QToolButton::clicked, this, [this] {
        QDir().mkpath(m_settings.outputDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.outputDir));
    });
    l->addWidget(openPath);
    l->addSpacing(12);

    m_countLabel = new QLabel(bar);
    m_countLabel->setObjectName(QStringLiteral("topLabel"));
    l->addWidget(m_countLabel);
    l->addSpacing(12);

    auto *settings = topButton(IconId::Settings, QString(), tr("Settings"), bar);
    connect(settings, &QToolButton::clicked, this, &ScanWindow::settingsRequested);
    l->addWidget(settings);
    auto *help = topButton(IconId::Help, QString(), tr("Help"), bar);
    connect(help, &QToolButton::clicked, this, &ScanWindow::helpRequested);
    l->addWidget(help);
    return bar;
}

QWidget *ScanWindow::buildToolPane()
{
    auto *pane = new QWidget(this);
    pane->setObjectName(QStringLiteral("toolPane"));
    pane->setFixedWidth(250);
    auto *l = new QVBoxLayout(pane);
    l->setContentsMargins(14, 12, 14, 12);
    l->setSpacing(4);

    l->addWidget(sectionLabel(tr("Color mode"), pane));
    m_colorGroup = new QButtonGroup(this);
    const struct {
        ColorMode mode;
        const char *text;
        const char *tip;
    } colors[] = {
        {ColorMode::Auto, QT_TR_NOOP("Auto optimize"),
         QT_TR_NOOP("Removes shadows and whitens the paper while keeping colors.")},
        {ColorMode::Color, QT_TR_NOOP("Color"),
         QT_TR_NOOP("Keeps all the color information with a light contrast boost. For colorful pages.")},
        {ColorMode::BlackWhite, QT_TR_NOOP("Black and white"),
         QT_TR_NOOP("Only black and white, smallest files. For black text on white paper.")},
        {ColorMode::Gray, QT_TR_NOOP("Grayscale"),
         QT_TR_NOOP("Converts the page to shades of gray: keeps detail and reduces file size.")},
        {ColorMode::WhitePaperStamp, QT_TR_NOOP("White paper and stamps"),
         QT_TR_NOOP("Cleans the background to pure white and keeps stamps vivid.")},
        {ColorMode::Watermark, QT_TR_NOOP("ID / watermark"),
         QT_TR_NOOP("Keeps watermarks and background patterns. For ID documents and certificates.")},
        {ColorMode::None, QT_TR_NOOP("No optimization"), QT_TR_NOOP("Saves the colors exactly as captured.")},
    };
    for (const auto &c : colors) {
        auto *rb = new QRadioButton(tr(c.text), pane);
        rb->setToolTip(tr(c.tip));
        m_colorGroup->addButton(rb, int(c.mode));
        l->addWidget(rb);
    }
    connect(m_colorGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.colorMode = ColorMode(id);
        m_settings.save();
    });

    l->addSpacing(12);
    l->addWidget(sectionLabel(tr("Page handling"), pane));
    m_pageGroup = new QButtonGroup(this);
    auto addPage = [&](int id, const QString &text, const QString &tip, bool enabled) {
        auto *rb = new QRadioButton(text, pane);
        rb->setToolTip(tip);
        rb->setEnabled(enabled);
        m_pageGroup->addButton(rb, id);
        l->addWidget(rb);
    };
    addPage(int(PageMode::FlatPage), tr("Flat page"), tr("Detects the page, crops the edges and straightens it."), true);
    addPage(int(PageMode::MultiDocument), tr("Multiple documents"),
            tr("Several documents side by side (receipts, cards, photos): each one is cropped, straightened and "
               "saved as its own file."),
            true);
    // Flat page and multiple document options.
    m_flatOptions = new QWidget(pane);
    auto *fl = new QVBoxLayout(m_flatOptions);
    fl->setContentsMargins(22, 2, 0, 4);
    fl->setSpacing(2);
    auto addFlat = [&](const QString &text, const QString &tip, bool AppSettings::*field) {
        auto *cb = new QCheckBox(text, m_flatOptions);
        cb->setToolTip(tip);
        cb->setChecked(m_settings.*field);
        connect(cb, &QCheckBox::toggled, this, [this, field](bool on) {
            m_settings.*field = on;
            m_settings.save();
        });
        fl->addWidget(cb);
    };
    addFlat(tr("Auto-rotate pages"),
            tr("Turn pages whose text is sideways or upside down (needs the Tesseract orientation data, "
               "tesseract-ocr-osd)"),
            &AppSettings::autoOrient);
    addFlat(tr("White rounded corners"),
            tr("Cards and ID documents: paint the desk visible in the rounded corners white"),
            &AppSettings::roundCorners);
    addFlat(tr("Complete torn pages"),
            tr("Fill torn or missing corners and edges of the page with the paper color"),
            &AppSettings::completePage);
    l->addWidget(m_flatOptions);
    addPage(int(PageMode::Book), tr("Curved book"),
            tr("Open book: flattens the curved pages, splits them at the spine and erases the fingers holding "
               "the book."),
            true);
    // Book options, shown in book mode.
    m_bookOptions = new QWidget(pane);
    auto *bl = new QVBoxLayout(m_bookOptions);
    bl->setContentsMargins(22, 0, 0, 4);
    bl->setSpacing(2);
    auto *split = new QCheckBox(tr("Separate pages"), m_bookOptions);
    split->setToolTip(tr("Save the left and right pages as two files"));
    split->setChecked(m_settings.bookSplit);
    connect(split, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.bookSplit = on;
        m_settings.save();
    });
    auto *fingers = new QCheckBox(tr("Remove fingers"), m_bookOptions);
    fingers->setToolTip(tr("Erase the fingers holding the pages at their outer edges"));
    fingers->setChecked(m_settings.removeFingers);
    connect(fingers, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.removeFingers = on;
        m_settings.save();
    });
    auto *order = new QComboBox(m_bookOptions);
    order->addItem(tr("Left page first"), false);
    order->addItem(tr("Right page first"), true);
    order->setToolTip(tr("Page order for books read from right to left"));
    order->setCurrentIndex(m_settings.bookRightFirst ? 1 : 0);
    connect(order, qOverload<int>(&QComboBox::activated), this, [this, order](int i) {
        m_settings.bookRightFirst = order->itemData(i).toBool();
        m_settings.save();
    });
    bl->addWidget(split);
    bl->addWidget(fingers);
    bl->addWidget(order);
    l->addWidget(m_bookOptions);
    addPage(int(PageMode::Area), tr("Area scan"), tr("Drag a rectangle on the preview; only that area is kept."), true);
    addPage(int(PageMode::Original), tr("Keep original"), tr("No cropping and no processing of the page."), true);
    connect(m_pageGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.pageMode = PageMode(id);
        m_settings.save();
        updateModeUi();
    });

    l->addSpacing(12);
    l->addWidget(sectionLabel(tr("Resolution"), pane));
    m_resolution = new QComboBox(pane);
    m_resolution->setToolTip(tr("Resolution of the saved scan"));
    connect(m_resolution, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        m_settings.captureSize = m_resolution->itemData(i).toSize();
        m_settings.save();
        m_camera->setCaptureSize(m_settings.captureSize);
    });
    l->addWidget(m_resolution);

    l->addSpacing(12);
    m_beep = new QCheckBox(tr("Scan sound"), pane);
    m_beep->setToolTip(tr("Play a sound after each scan"));
    connect(m_beep, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.beep = on;
        m_settings.save();
    });
    l->addWidget(m_beep);
    auto *autoScan = new QCheckBox(tr("Auto scan on page turn"), pane);
    autoScan->setToolTip(tr("Scans by itself each time you turn the page: once the hand has left and "
                            "the new page has been still for about one second."));
    connect(autoScan, &QCheckBox::toggled, this, [this](bool on) {
        m_autoScan = on;
        m_autoTrigger.reset();
        m_clock.start();
    });
    l->addWidget(autoScan);
    l->addStretch(1);
    return pane;
}

QWidget *ScanWindow::buildBottomBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("cameraPane"));
    bar->setFixedHeight(96);
    auto *l = new QHBoxLayout(bar);
    l->setContentsMargins(16, 8, 16, 8);

    l->addStretch(1);

    auto mk = [&](IconId id, const QString &tip) {
        auto *b = new QToolButton(bar);
        b->setObjectName(QStringLiteral("roundButton"));
        b->setIcon(icon(id));
        b->setIconSize(QSize(26, 26));
        b->setFixedSize(48, 48);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    m_rotLeft = mk(IconId::RotateLeft, tr("Rotate 90° left"));
    connect(m_rotLeft, &QToolButton::clicked, this, [this] { rotate(-90); });
    l->addWidget(m_rotLeft);
    l->addSpacing(18);

    m_scanButton = new QPushButton(bar);
    m_scanButton->setObjectName(QStringLiteral("scanButton"));
    m_scanButton->setIcon(icon(IconId::Scan, Qt::white));
    m_scanButton->setIconSize(QSize(36, 36));
    m_scanButton->setFixedSize(76, 76);
    m_scanButton->setToolTip(
        QStringLiteral("%1 (%2)").arg(tr("Scan"), QKeySequence(Qt::Key_Space).toString(QKeySequence::NativeText)));
    m_scanButton->setCursor(Qt::PointingHandCursor);
    connect(m_scanButton, &QPushButton::clicked, this, &ScanWindow::scan);
    l->addWidget(m_scanButton);
    l->addSpacing(18);

    m_rotRight = mk(IconId::RotateRight, tr("Rotate 90° right"));
    connect(m_rotRight, &QToolButton::clicked, this, [this] { rotate(90); });
    l->addWidget(m_rotRight);
    l->addStretch(1);
    return bar;
}

void ScanWindow::reloadSettings()
{
    if (auto *b = m_colorGroup->button(int(m_settings.colorMode)))
        b->setChecked(true);
    if (auto *b = m_pageGroup->button(int(m_settings.pageMode)))
        b->setChecked(true);
    m_beep->setChecked(m_settings.beep);
    m_camera->setCaptureSize(m_settings.captureSize);
    updateFolderLabel();
    updateCounters();
    updateModeUi();
}

void ScanWindow::updateFolderLabel()
{
    const QString path = QDir::toNativeSeparators(m_settings.outputDir);
    m_folderLabel->setToolTip(path);
    m_folderLabel->setText(m_folderLabel->fontMetrics().elidedText(path, Qt::ElideMiddle, 360));
}

void ScanWindow::updateCounters()
{
    m_countLabel->setText(tr("Processed: %1   Scanned: %2").arg(m_processed).arg(m_scanned));
}

void ScanWindow::updateModeUi()
{
    const bool area = m_settings.pageMode == PageMode::Area;
    m_preview->setSelectionEnabled(area);
    m_bookOptions->setVisible(m_settings.pageMode == PageMode::Book);
    m_flatOptions->setVisible(m_settings.pageMode == PageMode::FlatPage ||
                              m_settings.pageMode == PageMode::MultiDocument);
    if (m_settings.pageMode != PageMode::FlatPage && m_settings.pageMode != PageMode::Book &&
        m_settings.pageMode != PageMode::MultiDocument)
        m_preview->setPageOutline({});
}

void ScanWindow::startCamera()
{
    if (!m_camera->isRunning())
        m_camera->start();
}

void ScanWindow::stopCamera()
{
    if (m_camera->isRunning()) {
        m_camera->stop();
        m_camera->wait();
    }
    m_cameraReady = false;
}

void ScanWindow::setCameraControl(uint32_t id, int32_t value) { m_camera->setControl(id, value); }

void ScanWindow::onCameraOpened(const CameraDescription &cam)
{
    m_cam = cam;
    m_cameraReady = true;
    m_busy = false;
    m_deviceLabel->setText(cam.name);
    m_preview->setMessage(QString());

    m_resolution->clear();
    for (const QSize &s : cam.captureSizes)
        m_resolution->addItem(QStringLiteral("%1 × %2  (%3 MP)")
                                  .arg(s.width())
                                  .arg(s.height())
                                  .arg(QLocale().toString(s.width() * s.height() / 1e6, 'f', 1)),
                              s);
    int idx = m_resolution->findData(m_settings.captureSize);
    m_resolution->setCurrentIndex(idx >= 0 ? idx : 0);
    m_scanButton->setEnabled(true);
}

void ScanWindow::onCameraLost(const QString &reason)
{
    m_cameraReady = false;
    m_busy = false;
    m_deviceLabel->setText(tr("No camera"));
    m_preview->setImage(QImage());
    m_preview->setPageOutline({});
    m_preview->setMessage(reason);
    m_scanButton->setEnabled(false);
}

void ScanWindow::onPreviewFrame(const QImage &frame)
{
    const QImage img = rotateImage(frame, m_settings.rotation);
    m_preview->setImage(img);
    if (m_autoScan && m_cameraReady && !m_busy && m_autoTrigger.feed(img, m_clock.elapsed()))
        scan();
    const PageMode mode = m_settings.pageMode;
    if ((mode == PageMode::FlatPage || mode == PageMode::Book || mode == PageMode::MultiDocument) &&
        !m_detectWatcher.isRunning() &&
        (++m_frameCounter % 3) == 0) {
        const QImage small = img.scaledToWidth(480, Qt::FastTransformation);
        m_detectWatcher.setFuture(QtConcurrent::run([small, mode] {
            QVector<QPolygonF> outlines;
            if (mode == PageMode::MultiDocument) {
                outlines = detectDocuments(small);
            } else if (mode == PageMode::Book) {
                const BookGeometry g = detectBook(small);
                if (g.valid)
                    outlines = {g.leftPage, g.rightPage};
            } else {
                outlines = {PageDetector::detect(small)};
            }
            for (QPolygonF &q : outlines)
                for (QPointF &p : q)
                    p = QPointF(p.x() / small.width(), p.y() / small.height());
            return outlines;
        }));
    }
}

void ScanWindow::scan()
{
    if (!m_cameraReady || m_busy || !isVisible())
        return;
    if (m_settings.pageMode == PageMode::Area && m_preview->selection().isEmpty()) {
        m_preview->setMessage(tr("Select the area to scan with the mouse first."));
        QTimer::singleShot(2500, this, [this] { m_preview->setMessage(QString()); });
        return;
    }
    m_busy = true;
    m_scanButton->setEnabled(false);
    m_preview->setMessage(tr("Scanning..."));
    m_camera->requestCapture();
}

void ScanWindow::onCaptured(const QByteArray &jpeg)
{
    m_busy = false;
    m_scanButton->setEnabled(true);
    m_autoTrigger.scanned(); // the page just scanned is the new reference
    m_preview->setMessage(QString());
    m_preview->flash();
    if (m_settings.beep)
        playBeep();
    ++m_scanned;
    updateCounters();

    ScanOptions opt = m_settings.scanOptions();
    opt.area = m_preview->selection();
    auto *w = new QFutureWatcher<QVector<ScanResult>>(this);
    m_jobs.append(w);
    connect(w, &QFutureWatcher<QVector<ScanResult>>::finished, this, &ScanWindow::onScanProcessed);
    w->setFuture(QtConcurrent::run([jpeg, opt] { return processAndSavePages(jpeg, opt); }));
}

void ScanWindow::onCaptureFailed(const QString &reason)
{
    qWarning("scan failed: %s", qPrintable(reason));
    m_busy = false;
    m_scanButton->setEnabled(m_cameraReady);
    m_preview->setMessage(tr("Scan failed: %1").arg(reason));
    QTimer::singleShot(4000, this, [this] {
        if (m_cameraReady)
            m_preview->setMessage(QString());
    });
}

void ScanWindow::onScanProcessed()
{
    auto *w = static_cast<QFutureWatcher<QVector<ScanResult>> *>(sender());
    m_jobs.removeAll(w);
    const QVector<ScanResult> results = w->result();
    w->deleteLater();
    for (const ScanResult &r : results) {
        if (!r.error.isEmpty()) {
            qWarning("saving the scan failed: %s", qPrintable(r.error));
            m_preview->setMessage(r.error);
            QTimer::singleShot(4000, this, [this] { m_preview->setMessage(QString()); });
            return;
        }
        ++m_processed;
        auto *it = new QListWidgetItem(QIcon(QPixmap::fromImage(r.thumbnail)), QFileInfo(r.path).fileName());
        it->setData(Qt::UserRole, r.path);
        it->setToolTip(QDir::toNativeSeparators(r.path));
        m_pages->addItem(it);
        m_pages->scrollToItem(it);
        Q_EMIT scanSaved(r.path);
    }
    updateCounters();
}

void ScanWindow::rotate(int delta)
{
    m_settings.rotation = ((m_settings.rotation + delta) % 360 + 360) % 360;
    m_settings.save();
    m_preview->setSelection({});
    m_preview->setPageOutline({});
}

void ScanWindow::playBeep()
{
    // The desktop's camera shutter sound when available, the system bell otherwise.
    const QString player = QStandardPaths::findExecutable(QStringLiteral("canberra-gtk-play"));
    if (!player.isEmpty() &&
        QProcess::startDetached(player, {QStringLiteral("-i"), QStringLiteral("camera-shutter")}))
        return;
    QApplication::beep();
}

void ScanWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        scan();
        return;
    }
    QWidget::keyPressEvent(e);
}

void ScanWindow::closeEvent(QCloseEvent *e)
{
    stopCamera();
    // Scans still being processed are saved before the main window looks at the folder.
    for (auto *job : m_jobs)
        job->waitForFinished();
    e->accept();
    Q_EMIT backRequested();
}

} // namespace lensscan
