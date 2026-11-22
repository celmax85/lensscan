// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "ocr/Ocr.h"

#include "BatchDialogs.h"
#include "ImageViewer.h"
#include "ToolDialogs.h"
#include "PresentationWindow.h"
#include "app/Shortcuts.h"
#include "ScanWindow.h"
#include "SettingsDialog.h"
#include "export/Exporters.h"
#include "imaging/ColorModes.h"
#include "imaging/ScanProcessor.h"
#include "imaging/BatchTools.h"
#include "imaging/DocumentTools.h"
#include "ui/Icons.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QShortcut>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <QPageLayout>
#include <QPageSize>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QLocale>

namespace lensscan {

namespace {
const QStringList kImageFilters = {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
                                   QStringLiteral("*.bmp"), QStringLiteral("*.JPG"), QStringLiteral("*.JPEG"),
                                   QStringLiteral("*.PNG")};
constexpr int kThumb = 168;

QPushButton *sideButton(IconId id, const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QPushButton(icon(id), text, parent);
    b->setObjectName(QStringLiteral("sideButton"));
    b->setIconSize(QSize(18, 18));
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QToolButton *topButton(IconId id, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("topButton"));
    b->setIcon(icon(id, Qt::white));
    b->setIconSize(QSize(20, 20));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QIcon placeholderIcon()
{
    QPixmap pm(kThumb, kThumb);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QColor(0xc8, 0xc8, 0xc8));
    p.setBrush(QColor(0xf4, 0xf4, 0xf4));
    p.drawRect(QRect(kThumb / 6, 4, kThumb * 2 / 3, kThumb - 8));
    return QIcon(pm);
}
} // namespace

MainWindow::MainWindow(AppSettings &settings, QWidget *parent) : QMainWindow(parent), m_settings(settings)
{
    setWindowTitle(QStringLiteral("LensScan"));
    setWindowIcon(icon(IconId::App));
    resize(1100, 760);
    setMinimumSize(800, 570);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTopBar());
    auto *body = new QHBoxLayout;
    body->setSpacing(0);
    // The side panel scrolls on small screens rather than squeezing its buttons.
    auto *sideScroll = new QScrollArea(central);
    sideScroll->setWidget(buildSidePanel());
    sideScroll->setWidgetResizable(true);
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Wide enough for the longest translated label, plus room for the scroll bar.
    QWidget *side = sideScroll->widget();
    side->ensurePolished();
    for (QWidget *c : side->findChildren<QWidget *>())
        c->ensurePolished();
    side->setFixedWidth(std::max(200, side->sizeHint().width()));
    sideScroll->setFixedWidth(side->width() + style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 2);
    sideScroll->setStyleSheet(QStringLiteral("QScrollArea{background:#f0f0f0;border-right:1px solid #d8d8d8;}"));
    body->addWidget(sideScroll);
    body->addWidget(buildCenter(), 1);
    root->addLayout(body, 1);

    auto *status = new QWidget(central);
    status->setObjectName(QStringLiteral("statusBar"));
    auto *sl = new QHBoxLayout(status);
    sl->setContentsMargins(12, 4, 12, 4);
    m_taskLabel = new QLabel(status);
    sl->addWidget(m_taskLabel, 1);
    sl->addWidget(new QLabel(tr("Version %1").arg(QStringLiteral(LENSSCAN_VERSION)), status));
    root->addWidget(status);
    setCentralWidget(central);
    setTaskState(tr("No background task"), false);

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(400);
    connect(&m_reloadTimer, &QTimer::timeout, this, &MainWindow::reloadFiles);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_reloadTimer.start(); });
    connect(&m_thumbWatcher, &QFutureWatcher<QPair<QString, QImage>>::resultReadyAt, this, [this](int i) {
        const auto r = m_thumbWatcher.resultAt(i);
        if (r.second.isNull())
            return;
        const QIcon ic(QPixmap::fromImage(r.second));
        m_thumbCache.insert(r.first, {QFileInfo(r.first).lastModified().toMSecsSinceEpoch(), ic});
        for (int k = 0; k < m_list->count(); ++k)
            if (m_list->item(k)->data(Qt::UserRole).toString() == r.first) {
                m_list->item(k)->setIcon(ic);
                break;
            }
    });

    m_scanWindow = new ScanWindow(m_settings);
    connect(m_scanWindow, &ScanWindow::backRequested, this, [this] {
        show();
        raise();
        activateWindow();
        finishPendingScan();
        reloadFiles();
    });
    connect(m_scanWindow, &ScanWindow::settingsRequested, this, &MainWindow::showSettings);
    connect(m_scanWindow, &ScanWindow::helpRequested, this, &MainWindow::showHelp);
    connect(m_scanWindow, &ScanWindow::controlsAvailable, this, &MainWindow::applySavedControls);

    reloadFiles();
}

MainWindow::~MainWindow()
{
    m_cancelThumbs = true;
    m_thumbWatcher.cancel();
    m_thumbWatcher.waitForFinished();
    delete m_scanWindow;
}

QWidget *MainWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("topBar"));
    bar->setFixedHeight(48);
    auto *l = new QHBoxLayout(bar);
    l->setContentsMargins(12, 0, 8, 0);
    auto *logo = new QLabel(bar);
    logo->setPixmap(icon(IconId::App, Qt::white).pixmap(26, 26));
    l->addWidget(logo);
    auto *title = new QLabel(QStringLiteral("LensScan"), bar);
    title->setObjectName(QStringLiteral("appTitle"));
    l->addWidget(title);
    l->addStretch(1);
    auto *settings = topButton(IconId::Settings, tr("Scanner settings"), bar);
    connect(settings, &QToolButton::clicked, this, &MainWindow::showSettings);
    l->addWidget(settings);
    auto *help = topButton(IconId::Help, tr("Help"), bar);
    connect(help, &QToolButton::clicked, this, &MainWindow::showHelp);
    l->addWidget(help);
    return bar;
}

QWidget *MainWindow::buildSidePanel()
{
    auto *pane = new QWidget(this);
    pane->setObjectName(QStringLiteral("sidePanel"));
    auto *l = new QVBoxLayout(pane);
    l->setContentsMargins(12, 14, 12, 12);
    l->setSpacing(4);

    auto *scan = new QPushButton(icon(IconId::Scan, Qt::white), tr("Scan"), pane);
    scan->setObjectName(QStringLiteral("bigScanButton"));
    scan->setIconSize(QSize(24, 24));
    scan->setToolTip(tr("Open the camera to scan pages (F5)"));
    scan->setCursor(Qt::PointingHandCursor);
    connect(scan, &QPushButton::clicked, this, &MainWindow::openScanWindow);
    l->addWidget(scan);
    auto *present = sideButton(IconId::Video, tr("Presentation"),
                               tr("Live view of the camera to show documents: zoom, drawing, screenshots and "
                                  "video recording with sound"),
                               pane);
    connect(present, &QPushButton::clicked, this, &MainWindow::openPresentation);
    l->addWidget(present);
    l->addSpacing(12);

    auto *batch = new QLabel(tr("Batch processing"), pane);
    batch->setObjectName(QStringLiteral("sectionLabel"));
    batch->setToolTip(tr("Apply color mode, rotation or deletion to the selected pages"));
    l->addWidget(batch);
    auto *color = sideButton(IconId::Palette, tr("Color mode"), tr("Convert the color mode of the selected pages"), pane);
    connect(color, &QPushButton::clicked, this, &MainWindow::batchColorMode);
    auto *rotL = sideButton(IconId::RotateLeft, tr("Rotate left"), tr("Rotate the selected pages 90° left"), pane);
    connect(rotL, &QPushButton::clicked, this, [this] { batchRotate(-90); });
    auto *rotR = sideButton(IconId::RotateRight, tr("Rotate right"), tr("Rotate the selected pages 90° right"), pane);
    connect(rotR, &QPushButton::clicked, this, [this] { batchRotate(90); });
    auto *editB = sideButton(IconId::Crop, tr("Edit page"), tr("Crop or rotate the selected page by hand"), pane);
    connect(editB, &QPushButton::clicked, this, [this] {
        const QStringList f = selectedFiles();
        if (f.isEmpty())
            runTask(tr("Edit page"), f, nullptr, false);
        else
            editPage(f.first());
    });
    auto *tools = sideButton(IconId::Tools, tr("More tools"), tr("Adjustments, watermark, renaming, blank pages"), pane);
    auto *toolsMenu = new QMenu(tools);
    toolsMenu->addAction(tr("Brightness / contrast / sharpness..."), this, &MainWindow::batchAdjust);
    toolsMenu->addAction(tr("Watermark..."), this, &MainWindow::batchWatermark);
    toolsMenu->addAction(tr("Trim edges..."), this, &MainWindow::batchTrimEdges);
    toolsMenu->addAction(icon(IconId::Crop), tr("Precise crop..."), this, &MainWindow::batchPreciseCrop);
    toolsMenu->addAction(icon(IconId::Split), tr("Split facing pages..."), this, &MainWindow::batchSplitPages);
    toolsMenu->addAction(icon(IconId::Reprocess), tr("Process again (flat page, book, original)..."), this,
                         &MainWindow::batchReprocess);
    toolsMenu->addAction(tr("Crop overlapping edges (book pages)..."), this, &MainWindow::batchOverlapCrop);
    toolsMenu->addAction(tr("Separate multiple documents..."), this, &MainWindow::batchSplitDocuments);
    toolsMenu->addAction(tr("Erase handwriting..."), this, &MainWindow::batchEraseHandwriting);
    toolsMenu->addAction(tr("Fill punch holes..."), this, &MainWindow::batchFillHoles);
    toolsMenu->addAction(tr("Auto-rotate pages (text orientation)"), this, &MainWindow::batchAutoOrient);
    toolsMenu->addAction(icon(IconId::FlipH), tr("Flip horizontally"), this, [this] { batchFlip(true); });
    toolsMenu->addAction(icon(IconId::FlipV), tr("Flip vertically"), this, [this] { batchFlip(false); });
    toolsMenu->addSeparator();
    toolsMenu->addAction(tr("Rename..."), this, &MainWindow::batchRename);
    toolsMenu->addAction(tr("Detect blank pages..."), this, &MainWindow::detectBlankPages);
    toolsMenu->addAction(icon(IconId::Barcode), tr("Read barcodes and QR codes..."), this, &MainWindow::readBarcodes);
    toolsMenu->addAction(icon(IconId::Info), tr("Image information..."), this, &MainWindow::showImageInfo);
    toolsMenu->addSeparator();
    toolsMenu->addAction(icon(IconId::Insert), tr("Insert pages before the selection..."), this, &MainWindow::insertPages);
    toolsMenu->addAction(icon(IconId::Scan), tr("Rescan the selected page..."), this, &MainWindow::rescanPage);
    toolsMenu->addAction(tr("Import images..."), this, &MainWindow::importFiles);
    connect(tools, &QPushButton::clicked, this,
            [tools, toolsMenu] { toolsMenu->exec(tools->mapToGlobal(QPoint(tools->width() - 8, 0))); });
    auto *del = sideButton(IconId::Trash, tr("Delete"), tr("Move the selected pages to the trash"), pane);
    connect(del, &QPushButton::clicked, this, &MainWindow::batchDelete);
    for (auto *b : {color, rotL, rotR, editB, tools, del}) {
        l->addWidget(b);
        m_taskButtons << b;
    }
    l->addSpacing(10);

    auto *out = new QLabel(tr("Document output"), pane);
    out->setObjectName(QStringLiteral("sectionLabel"));
    out->setToolTip(tr("Combine the selected pages into a document"));
    l->addWidget(out);
    auto *pdf = sideButton(IconId::Pdf, tr("PDF"), tr("Save the selected pages into one PDF file"), pane);
    connect(pdf, &QPushButton::clicked, this, &MainWindow::exportPdf);
    auto *tiff = sideButton(IconId::Tiff, tr("TIFF"),
                            tr("Save the selected pages as TIFF: one multi-page file or one file per page"), pane);
    connect(tiff, &QPushButton::clicked, this, &MainWindow::exportTiff);
    auto *png = sideButton(IconId::Image, tr("PNG"), tr("Convert the selected pages to PNG files"), pane);
    connect(png, &QPushButton::clicked, this, [this] { exportImages(QStringLiteral("png")); });
    auto *jpg = sideButton(IconId::Image, tr("JPEG"), tr("Convert the selected pages to JPEG files"), pane);
    connect(jpg, &QPushButton::clicked, this, [this] { exportImages(QStringLiteral("jpg")); });
    auto *bmp = sideButton(IconId::Image, tr("BMP"), tr("Convert the selected pages to BMP files"), pane);
    connect(bmp, &QPushButton::clicked, this, [this] { exportImages(QStringLiteral("bmp")); });
    auto *print = sideButton(IconId::Print, tr("Print"), tr("Print the selected pages, with a preview"), pane);
    connect(print, &QPushButton::clicked, this, &MainWindow::printPages);
    for (auto *b : {pdf, tiff, png, jpg, bmp, print}) {
        l->addWidget(b);
        m_taskButtons << b;
    }
    l->addSpacing(10);

    auto *ocr = new QLabel(tr("Text recognition (OCR)"), pane);
    ocr->setObjectName(QStringLiteral("sectionLabel"));
    ocr->setToolTip(tr("Recognize the text of the selected pages. Languages: Settings > Documents"));
    l->addWidget(ocr);
    auto *spdf = sideButton(IconId::Pdf, tr("Searchable PDF"),
                            tr("PDF with the page images and their text, which can be searched and copied"), pane);
    connect(spdf, &QPushButton::clicked, this, [this] { exportOcr(QStringLiteral("pdf")); });
    auto *docx = sideButton(IconId::Docx, tr("Word"), tr("Editable Word document (.docx) with the recognized text"),
                            pane);
    connect(docx, &QPushButton::clicked, this, [this] { exportOcr(QStringLiteral("docx")); });
    auto *xlsx = sideButton(IconId::Xlsx, tr("Excel"),
                            tr("Excel workbook (.xlsx): the tables of the pages, one sheet per page"), pane);
    connect(xlsx, &QPushButton::clicked, this, [this] { exportOcr(QStringLiteral("xlsx")); });
    auto *txt = sideButton(IconId::Txt, tr("Text"), tr("Plain text file (.txt) with the recognized text"), pane);
    connect(txt, &QPushButton::clicked, this, [this] { exportOcr(QStringLiteral("txt")); });
    for (auto *b : {spdf, docx, xlsx, txt}) {
        l->addWidget(b);
        m_taskButtons << b;
    }
    l->addStretch(1);

    auto &sc = Shortcuts::instance();
    sc.bind(QStringLiteral("main/scan"), this, [this] { openScanWindow(); });
    sc.bind(QStringLiteral("main/presentation"), this, [this] { openPresentation(); });
    sc.bind(QStringLiteral("main/delete"), this, [this] { batchDelete(); });
    sc.bind(QStringLiteral("main/rotateLeft"), this, [this] { batchRotate(-90); });
    sc.bind(QStringLiteral("main/rotateRight"), this, [this] { batchRotate(90); });
    sc.bind(QStringLiteral("main/pdf"), this, [this] { exportPdf(); });
    sc.bind(QStringLiteral("main/settings"), this, [this] { showSettings(); });
    sc.bind(QStringLiteral("main/print"), this, [this] { printPages(); });
    sc.bind(QStringLiteral("main/insert"), this, [this] { insertPages(); });
    sc.bind(QStringLiteral("main/info"), this, [this] { showImageInfo(); });
    sc.bind(QStringLiteral("main/edit"), this, [this] {
        if (!selectedFiles().isEmpty())
            editPage(selectedFiles().first());
    });
    return pane;
}

QWidget *MainWindow::buildCenter()
{
    auto *w = new QWidget(this);
    w->setObjectName(QStringLiteral("filePane"));
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(12, 10, 12, 6);

    auto *head = new QHBoxLayout;
    m_selectAll = new QCheckBox(tr("Select all"), w);
    connect(m_selectAll, &QCheckBox::clicked, this, [this](bool on) {
        if (on)
            m_list->selectAll();
        else
            m_list->clearSelection();
    });
    head->addWidget(m_selectAll);
    m_selectionLabel = new QLabel(w);
    head->addWidget(m_selectionLabel);
    head->addStretch(1);
    m_folderLabel = new QLabel(w);
    m_folderLabel->setObjectName(QStringLiteral("folderLabel"));
    head->addWidget(m_folderLabel);
    auto *import = new QToolButton(w);
    import->setIcon(icon(IconId::Import));
    import->setToolTip(tr("Import images into the scan folder"));
    import->setAutoRaise(true);
    connect(import, &QToolButton::clicked, this, &MainWindow::importFiles);
    head->addWidget(import);
    auto *change = new QToolButton(w);
    change->setIcon(icon(IconId::Folder));
    change->setToolTip(tr("Choose the scan folder"));
    change->setAutoRaise(true);
    connect(change, &QToolButton::clicked, this, &MainWindow::changeFolder);
    head->addWidget(change);
    auto *open = new QToolButton(w);
    open->setIcon(icon(IconId::FolderOpen));
    open->setToolTip(tr("Open the scan folder in the file manager"));
    open->setAutoRaise(true);
    connect(open, &QToolButton::clicked, this, [this] {
        QDir().mkpath(m_settings.outputDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.outputDir));
    });
    head->addWidget(open);
    l->addLayout(head);

    m_list = new QListWidget(w);
    m_list->setObjectName(QStringLiteral("fileList"));
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kThumb, kThumb));
    m_list->setGridSize(QSize(kThumb + 28, kThumb + 44));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setUniformItemSizes(true);
    m_list->setWordWrap(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &MainWindow::updateSelectionInfo);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *it) { viewPage(it->data(Qt::UserRole).toString()); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (!m_list->itemAt(pos))
            return;
        QMenu menu(this);
        menu.addAction(icon(IconId::Image), tr("View"), this, [this] {
            if (!selectedFiles().isEmpty())
                viewPage(selectedFiles().first());
        });
        menu.addAction(icon(IconId::Crop), tr("Edit..."), this, [this] {
            if (!selectedFiles().isEmpty())
                editPage(selectedFiles().first());
        });
        menu.addAction(tr("Open with another application"), this, [this] {
            for (const QString &f : selectedFiles())
                QDesktopServices::openUrl(QUrl::fromLocalFile(f));
        });
        menu.addSeparator();
        menu.addAction(icon(IconId::RotateLeft), tr("Rotate left"), this, [this] { batchRotate(-90); });
        menu.addAction(icon(IconId::RotateRight), tr("Rotate right"), this, [this] { batchRotate(90); });
        menu.addAction(icon(IconId::Palette), tr("Color mode..."), this, &MainWindow::batchColorMode);
        menu.addAction(tr("Brightness / contrast / sharpness..."), this, &MainWindow::batchAdjust);
        menu.addAction(tr("Watermark..."), this, &MainWindow::batchWatermark);
        menu.addAction(tr("Rename..."), this, &MainWindow::batchRename);
        menu.addAction(icon(IconId::Insert), tr("Insert pages before..."), this, &MainWindow::insertPages);
        menu.addAction(icon(IconId::Scan), tr("Rescan this page..."), this, &MainWindow::rescanPage);
        menu.addAction(icon(IconId::Info), tr("Image information..."), this, &MainWindow::showImageInfo);
        menu.addSeparator();
        menu.addAction(icon(IconId::Pdf), tr("Save as PDF..."), this, &MainWindow::exportPdf);
        menu.addAction(icon(IconId::Print), tr("Print..."), this, &MainWindow::printPages);
        menu.addSeparator();
        menu.addAction(icon(IconId::Trash), tr("Delete"), this, &MainWindow::batchDelete);
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    l->addWidget(m_list, 1);
    return w;
}

void MainWindow::reloadFiles()
{
    QDir().mkpath(m_settings.outputDir);
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_watcher.addPath(m_settings.outputDir);
    const QString path = QDir::toNativeSeparators(m_settings.outputDir);
    m_folderLabel->setText(m_folderLabel->fontMetrics().elidedText(path, Qt::ElideMiddle, 420));
    m_folderLabel->setToolTip(path);

    QSet<QString> selected;
    for (const QString &f : selectedFiles())
        selected.insert(f);

    QFileInfoList files;
    for (const QString &name : folderImages())
        files << QFileInfo(QDir(m_settings.outputDir).filePath(name));
    m_list->clear();
    static const QIcon placeholder = placeholderIcon();
    for (const QFileInfo &fi : files) {
        auto *it = new QListWidgetItem(fi.fileName());
        it->setData(Qt::UserRole, fi.absoluteFilePath());
        it->setToolTip(QStringLiteral("%1\n%2 KB").arg(fi.fileName()).arg(fi.size() / 1024));
        const auto c = m_thumbCache.value(fi.absoluteFilePath());
        it->setIcon(c.first == fi.lastModified().toMSecsSinceEpoch() ? c.second : placeholder);
        m_list->addItem(it);
        if (selected.contains(fi.absoluteFilePath()))
            it->setSelected(true);
    }
    updateSelectionInfo();
    loadThumbnails();
    if (m_viewer) {
        QStringList all;
        for (int i = 0; i < m_list->count(); ++i)
            all << m_list->item(i)->data(Qt::UserRole).toString();
        const QString current = m_viewer->currentFile();
        m_viewer->setFiles(all, std::max(0, int(all.indexOf(current))));
    }
}

QStringList MainWindow::folderImages() const
{
    QStringList names = QDir(m_settings.outputDir).entryList(kImageFilters, QDir::Files);
    names.removeDuplicates();
    std::sort(names.begin(), names.end(), scanNameLess);
    return names;
}

void MainWindow::loadThumbnails()
{
    m_thumbWatcher.cancel();
    m_thumbWatcher.waitForFinished();
    QStringList todo;
    for (int i = 0; i < m_list->count(); ++i) {
        const QString f = m_list->item(i)->data(Qt::UserRole).toString();
        if (m_thumbCache.value(f).first != QFileInfo(f).lastModified().toMSecsSinceEpoch())
            todo << f;
    }
    if (todo.isEmpty())
        return;
    m_thumbWatcher.setFuture(QtConcurrent::mapped(todo, [](const QString &f) {
        QImageReader r(f);
        r.setAutoTransform(true);
        const QSize s = r.size();
        if (s.isValid())
            r.setScaledSize(s.scaled(kThumb * 2, kThumb * 2, Qt::KeepAspectRatio));
        QImage img = r.read();
        if (!img.isNull())
            img = img.scaled(kThumb, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return qMakePair(f, img);
    }));
}

void MainWindow::updateSelectionInfo()
{
    const int n = m_list->selectedItems().size();
    m_selectionLabel->setText(tr("%1 page(s), %2 selected").arg(m_list->count()).arg(n));
    m_selectAll->setChecked(n > 0 && n == m_list->count());
}

QStringList MainWindow::selectedFiles(bool allIfNone) const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i) // display order, not click order
        if (m_list->item(i)->isSelected() || (allIfNone && m_list->selectedItems().isEmpty()))
            out << m_list->item(i)->data(Qt::UserRole).toString();
    return out;
}

void MainWindow::setTaskState(const QString &text, bool busy)
{
    m_taskRunning = busy;
    m_taskLabel->setText(text);
    for (auto *b : m_taskButtons)
        b->setEnabled(!busy);
}

void MainWindow::runTask(const QString &label, const QStringList &files,
                         const std::function<bool(const QString &, QString *)> &perFile, bool refresh)
{
    if (files.isEmpty()) {
        QMessageBox::information(this, label, tr("Select one or more pages first (Ctrl+A selects all)."));
        return;
    }
    setTaskState(tr("%1: running...").arg(label), true);
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w, label, refresh] {
        const QString err = w->result();
        w->deleteLater();
        setTaskState(err.isEmpty() ? tr("%1: done").arg(label) : tr("%1: failed").arg(label), false);
        if (!err.isEmpty())
            QMessageBox::warning(this, label, err);
        if (refresh) {
            for (const QString &k : m_thumbCache.keys())
                if (!QFileInfo::exists(k))
                    m_thumbCache.remove(k);
            reloadFiles();
        }
    });
    w->setFuture(QtConcurrent::run([files, perFile] {
        for (const QString &f : files) {
            QString err;
            if (!perFile(f, &err))
                return err;
        }
        return QString();
    }));
}

void MainWindow::runExport(const QString &label, const std::function<bool(QString *)> &job, const QString &result)
{
    setTaskState(tr("%1: running...").arg(label), true);
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w, label, result] {
        const QString err = w->result();
        w->deleteLater();
        setTaskState(err.isEmpty() ? tr("%1: done").arg(label) : tr("%1: failed").arg(label), false);
        if (!err.isEmpty()) {
            QMessageBox::warning(this, label, err);
            return;
        }
        QMessageBox box(QMessageBox::Information, label, tr("Saved to:\n%1").arg(QDir::toNativeSeparators(result)),
                        QMessageBox::Close, this);
        auto *open = box.addButton(tr("Open"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl::fromLocalFile(result));
    });
    w->setFuture(QtConcurrent::run([job] {
        QString err;
        job(&err);
        return err;
    }));
}

void MainWindow::batchColorMode()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty()) {
        runTask(tr("Color mode"), files, nullptr, false);
        return;
    }
    const QList<QPair<ColorMode, QString>> modes = {
        {ColorMode::Auto, tr("Auto optimize")},       {ColorMode::Color, tr("Color")},
        {ColorMode::BlackWhite, tr("Black and white")}, {ColorMode::Gray, tr("Grayscale")},
        {ColorMode::WhitePaperStamp, tr("White paper and stamps")}, {ColorMode::Watermark, tr("ID / watermark")},
    };
    QStringList names;
    for (const auto &m : modes)
        names << m.second;
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, tr("Color mode"),
                                                 tr("Convert %n page(s) to:", nullptr, int(files.size())), names, 0,
                                                 false, &ok);
    if (!ok)
        return;
    const ColorMode mode = modes.at(int(names.indexOf(choice))).first;
    const int quality = m_settings.jpegQuality;
    runTask(tr("Color mode"), files, [mode, quality](const QString &f, QString *err) {
        const QImage img = loadScan(f);
        if (img.isNull()) {
            *err = QObject::tr("Cannot read %1").arg(f);
            return false;
        }
        if (!saveImage(applyColorMode(img, mode), f, quality)) {
            *err = QObject::tr("Cannot write %1").arg(f);
            return false;
        }
        return true;
    }, true);
}

void MainWindow::batchRotate(int degrees)
{
    const int quality = m_settings.jpegQuality;
    runTask(degrees < 0 ? tr("Rotate left") : tr("Rotate right"), selectedFiles(),
            [degrees, quality](const QString &f, QString *err) {
                const QImage img = loadScan(f);
                if (img.isNull()) {
                    *err = QObject::tr("Cannot read %1").arg(f);
                    return false;
                }
                if (!saveImage(rotateImage(img, degrees), f, quality)) {
                    *err = QObject::tr("Cannot write %1").arg(f);
                    return false;
                }
                return true;
            },
            true);
}

void MainWindow::batchDelete()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty() || m_taskRunning)
        return;
    if (QMessageBox::question(this, tr("Delete"), tr("Move %n page(s) to the trash?", nullptr, int(files.size()))) !=
        QMessageBox::Yes)
        return;
    runTask(tr("Delete"), files, [](const QString &f, QString *err) {
        if (QFile::moveToTrash(f) || QFile::remove(f))
            return true;
        *err = QObject::tr("Cannot delete %1").arg(f);
        return false;
    }, true);
}

void MainWindow::runImageTask(const QString &label, const QStringList &files,
                              const std::function<QImage(const QImage &)> &op)
{
    const int quality = m_settings.jpegQuality;
    runTask(label, files, [op, quality](const QString &f, QString *err) {
        const QImage img = loadScan(f);
        if (img.isNull()) {
            *err = QObject::tr("Cannot read %1").arg(f);
            return false;
        }
        if (!saveImage(op(img), f, quality)) {
            *err = QObject::tr("Cannot write %1").arg(f);
            return false;
        }
        return true;
    }, true);
}

QIcon MainWindow::thumbnail(const QString &file) const
{
    static const QIcon placeholder = placeholderIcon();
    const auto c = m_thumbCache.value(file);
    return c.second.isNull() ? placeholder : c.second;
}

void MainWindow::editPage(const QString &file)
{
    if (m_taskRunning)
        return;
    const QImage img = loadScan(file);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Edit page"), tr("Cannot read %1").arg(file));
        return;
    }
    PageEditorDialog dlg(img, QFileInfo(file).fileName(), this);
    if (dlg.exec() != QDialog::Accepted || dlg.result() == img)
        return;
    const QImage out = dlg.result();
    runImageTask(tr("Edit page"), {file}, [out](const QImage &) { return out; });
}

void MainWindow::batchAdjust()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Adjust"), files, nullptr, false);
    AdjustDialog dlg(loadScan(files.first()), int(files.size()), this);
    if (dlg.exec() != QDialog::Accepted || dlg.adjustment().isNull())
        return;
    const Adjustment a = dlg.adjustment();
    runImageTask(tr("Adjust"), files, [a](const QImage &img) { return adjustImage(img, a); });
}

void MainWindow::batchWatermark()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Watermark"), files, nullptr, false);
    WatermarkDialog dlg(loadScan(files.first()), int(files.size()), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Watermark w = dlg.watermark();
    runImageTask(tr("Watermark"), files, [w](const QImage &img) { return addWatermark(img, w); });
}

void MainWindow::batchTrimEdges()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Trim edges"), files, nullptr, false);
    bool ok = false;
    const int px = QInputDialog::getInt(this, tr("Trim edges"),
                                        tr("Pixels to remove on each side of %n page(s):", nullptr, int(files.size())),
                                        20, 1, 1000, 1, &ok);
    if (ok)
        runImageTask(tr("Trim edges"), files, [px](const QImage &img) { return trimEdges(img, px); });
}

void MainWindow::batchSplitDocuments()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Separate documents"), files, nullptr, false);
    const int quality = m_settings.jpegQuality;
    runTask(tr("Separate documents"), files, [quality](const QString &f, QString *err) {
        const QImage img = loadScan(f);
        if (img.isNull()) {
            *err = QObject::tr("Cannot read %1").arg(f);
            return false;
        }
        const QVector<QImage> docs = splitDocuments(img);
        const QFileInfo fi(f);
        for (int i = 0; i < docs.size(); ++i) {
            QString target;
            for (int k = i + 1;; ++k) {
                target = fi.dir().filePath(QStringLiteral("%1_doc%2.%3").arg(fi.completeBaseName()).arg(k).arg(fi.suffix()));
                if (!QFileInfo::exists(target))
                    break;
            }
            if (!saveImage(docs[i], target, quality)) {
                *err = QObject::tr("Cannot write %1").arg(target);
                return false;
            }
        }
        return true;
    }, true);
}

void MainWindow::batchOverlapCrop()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Crop overlapping edges"), files, nullptr, false);
    int narrowest = 0;
    for (const QString &f : files) {
        QImageReader r(f);
        r.setAutoTransform(true);
        const int w = r.size().width();
        if (w > 0 && (narrowest == 0 || w < narrowest))
            narrowest = w;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Crop overlapping edges"));
    auto *l = new QVBoxLayout(&dlg);
    auto *info = new QLabel(tr("Book pages are cut to the same width, measured from the spine; the extra pixels "
                               "on the outer side (the neighbouring page, the desk) are removed."),
                            &dlg);
    info->setWordWrap(true);
    l->addWidget(info);
    auto *form = new QFormLayout;
    auto *width = new QSpinBox(&dlg);
    width->setRange(16, 20000);
    width->setSuffix(tr(" px"));
    width->setValue(narrowest > 0 ? narrowest : 1000);
    auto *first = new QComboBox(&dlg);
    first->addItem(tr("Left page (spine on its right)"), true);
    first->addItem(tr("Right page (spine on its left)"), false);
    form->addRow(tr("Page width"), width);
    form->addRow(tr("First selected page"), first);
    l->addLayout(form);
    l->addWidget(new QLabel(tr("The following pages alternate left / right."), &dlg));
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    l->addWidget(box);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int w = width->value();
    const bool firstLeft = first->currentData().toBool();
    QHash<QString, bool> left;
    for (int i = 0; i < files.size(); ++i)
        left.insert(files[i], (i % 2 == 0) == firstLeft);
    const int quality = m_settings.jpegQuality;
    runTask(tr("Crop overlapping edges"), files, [w, left, quality](const QString &f, QString *err) {
        const QImage img = loadScan(f);
        if (img.isNull()) {
            *err = QObject::tr("Cannot read %1").arg(f);
            return false;
        }
        if (!saveImage(cropToPageWidth(img, w, left.value(f)), f, quality)) {
            *err = QObject::tr("Cannot write %1").arg(f);
            return false;
        }
        return true;
    }, true);
}

void MainWindow::batchRename()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty() || m_taskRunning)
        return runTask(tr("Rename"), files, nullptr, false);
    RenameDialog dlg(files, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString err = renameFiles(files, dlg.newPaths());
    if (!err.isEmpty())
        QMessageBox::warning(this, tr("Rename"), err);
    setTaskState(err.isEmpty() ? tr("%1: done").arg(tr("Rename")) : tr("%1: failed").arg(tr("Rename")), false);
    reloadFiles();
}

void MainWindow::detectBlankPages()
{
    const QStringList files = selectedFiles(true);
    if (files.isEmpty() || m_taskRunning)
        return;
    const QString label = tr("Blank page detection");
    setTaskState(tr("%1: running...").arg(label), true);
    auto *w = new QFutureWatcher<QStringList>(this);
    connect(w, &QFutureWatcher<QStringList>::finished, this, [this, w, label] {
        const QStringList blank = w->result();
        w->deleteLater();
        setTaskState(tr("%1: done").arg(label), false);
        if (blank.isEmpty()) {
            QMessageBox::information(this, label, tr("No blank page found."));
            return;
        }
        BlankPagesDialog dlg(blank, [this](const QString &f) { return thumbnail(f); }, this);
        if (dlg.exec() != QDialog::Accepted || dlg.checkedFiles().isEmpty())
            return;
        runTask(tr("Delete"), dlg.checkedFiles(), [](const QString &f, QString *err) {
            if (QFile::moveToTrash(f) || QFile::remove(f))
                return true;
            *err = QObject::tr("Cannot delete %1").arg(f);
            return false;
        }, true);
    });
    w->setFuture(QtConcurrent::run([files] {
        QStringList blank;
        for (const QString &f : files) {
            QImageReader r(f);
            r.setAutoTransform(true);
            const QSize s = r.size();
            if (s.isValid())
                r.setScaledSize(s.scaled(1000, 1000, Qt::KeepAspectRatio));
            if (isBlankPage(r.read()))
                blank << f;
        }
        return blank;
    }));
}

void MainWindow::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import images"), QDir::homePath(),
        tr("Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp);;All files (*)"));
    if (files.isEmpty())
        return;
    QStringList errors;
    importImages(files, m_settings.outputDir, &errors);
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Import images"),
                             tr("These files could not be imported:\n%1").arg(errors.join(QLatin1Char('\n'))));
    reloadFiles();
}

QStringList MainWindow::exportOrder(const QStringList &files)
{
    if (!m_settings.orderBeforeExport || files.size() < 2)
        return files;
    PageOrderDialog dlg(files, [this](const QString &f) { return thumbnail(f); }, this);
    return dlg.exec() == QDialog::Accepted ? dlg.orderedFiles() : QStringList();
}

void MainWindow::exportPdf()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty()) {
        runTask(tr("PDF"), files, nullptr, false);
        return;
    }
    const QString def = QDir(m_settings.outputDir)
                            .filePath(QStringLiteral("Document_%1.pdf")
                                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    const QStringList ordered = exportOrder(files);
    if (ordered.isEmpty())
        return;
    QString out = QFileDialog::getSaveFileName(this, tr("Save as PDF"), def, tr("PDF documents (*.pdf)"));
    if (out.isEmpty())
        return;
    if (!out.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
        out += QLatin1String(".pdf");
    PdfOptions opt;
    opt.dpi = m_settings.pdfDpi;
    opt.pageSize = m_settings.pdfPageSize;
    runExport(tr("PDF"), [ordered, out, opt](QString *err) { return lensscan::exportPdf(ordered, out, opt, nullptr, err); },
              out);
}

void MainWindow::exportTiff()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty()) {
        runTask(tr("TIFF"), files, nullptr, false);
        return;
    }
    const QString def = QDir(m_settings.outputDir)
                            .filePath(QStringLiteral("Document_%1.tif")
                                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    const QStringList ordered = exportOrder(files);
    if (ordered.isEmpty())
        return;
    TiffDialog dlg(int(ordered.size()), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    TiffOptions opt = dlg.options();
    opt.dpi = m_settings.dpi > 0 ? m_settings.dpi : 300;
    QString out = QFileDialog::getSaveFileName(this, tr("Save as TIFF"), def, tr("TIFF images (*.tif *.tiff)"));
    if (out.isEmpty())
        return;
    if (!out.endsWith(QLatin1String(".tif"), Qt::CaseInsensitive) &&
        !out.endsWith(QLatin1String(".tiff"), Qt::CaseInsensitive))
        out += QLatin1String(".tif");
    // One file per page: show the folder once done.
    const QString result = opt.multiPage ? out : QFileInfo(out).absolutePath();
    runExport(tr("TIFF"), [ordered, out, opt](QString *err) { return lensscan::exportTiff(ordered, out, opt, nullptr, err); },
              result);
}

void MainWindow::exportOcr(const QString &format)
{
    const QStringList files = selectedFiles();
    const QString label = format == QLatin1String("pdf")    ? tr("Searchable PDF")
                          : format == QLatin1String("docx") ? tr("Word")
                          : format == QLatin1String("xlsx") ? tr("Excel")
                                                            : tr("Text");
    if (files.isEmpty()) {
        runTask(label, files, nullptr, false);
        return;
    }
    if (ocrAvailableLanguages().isEmpty()) {
        QMessageBox::warning(this, label,
                             tr("Text recognition needs Tesseract language data, which is not installed.\n\n"
                                "Install it with your package manager, for example:\n"
                                "sudo apt install tesseract-ocr-fra tesseract-ocr-eng"));
        return;
    }
    QString langs = m_settings.ocrLanguages.isEmpty() ? ocrDefaultLanguages() : m_settings.ocrLanguages;
    const QString filter = format == QLatin1String("pdf")    ? tr("PDF documents (*.pdf)")
                           : format == QLatin1String("docx") ? tr("Word documents (*.docx)")
                           : format == QLatin1String("xlsx") ? tr("Excel workbooks (*.xlsx)")
                                                             : tr("Text files (*.txt)");
    const QString suffix = QLatin1Char('.') + format;
    const QString def = QDir(m_settings.outputDir)
                            .filePath(QStringLiteral("Document_%1%2")
                                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")),
                                               suffix));
    const QStringList ordered = exportOrder(files);
    if (ordered.isEmpty())
        return;
    QString out = QFileDialog::getSaveFileName(this, label, def, filter);
    if (out.isEmpty())
        return;
    if (!out.endsWith(suffix, Qt::CaseInsensitive))
        out += suffix;
    const QString title = QFileInfo(out).completeBaseName();
    runExport(label, [ordered, out, format, langs, title](QString *err) {
        if (format == QLatin1String("pdf"))
            return ocrToPdf(ordered, out, langs, title, nullptr, err);
        if (format == QLatin1String("docx"))
            return ocrToDocx(ordered, out, langs, nullptr, err);
        if (format == QLatin1String("xlsx"))
            return ocrToXlsx(ordered, out, langs, nullptr, err);
        return ocrToText(ordered, out, langs, nullptr, err);
    }, out);
}

void MainWindow::exportImages(const QString &format)
{
    const QStringList files = selectedFiles();
    const QString label = format.toUpper();
    if (files.isEmpty()) {
        runTask(label, files, nullptr, false);
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Save %1 files to").arg(label), m_settings.outputDir);
    if (dir.isEmpty())
        return;
    const int q = m_settings.jpegQuality;
    runExport(label, [files, dir, format, q](QString *err) {
        return lensscan::exportImages(files, dir, format, q, nullptr, err);
    }, dir);
}

void MainWindow::changeFolder()
{
    const QString d = QFileDialog::getExistingDirectory(this, tr("Scan folder"), m_settings.outputDir);
    if (d.isEmpty())
        return;
    m_settings.outputDir = d;
    m_settings.save();
    m_scanWindow->reloadSettings();
    reloadFiles();
}

void MainWindow::openScanWindow()
{
    if (m_scanWindow->isVisible()) {
        m_scanWindow->raise();
        return;
    }
    m_scanWindow->reloadSettings();
    m_scanWindow->setGeometry(geometry().adjusted(-40, -30, 40, 30).intersected(screen()->availableGeometry()));
    m_scanWindow->show();
    m_scanWindow->activateWindow();
    m_scanWindow->startCamera();
    hide();
}

void MainWindow::openPresentation()
{
    if (m_presentation) {
        m_presentation->raise();
        m_presentation->activateWindow();
        return;
    }
    if (m_scanWindow->isVisible())
        m_scanWindow->close();
    m_presentation = new PresentationWindow(m_settings);
    connect(m_presentation, &PresentationWindow::closed, this, [this] {
        show();
        raise();
        activateWindow();
        reloadFiles();
    });
    m_presentation->setGeometry(geometry().adjusted(-40, -30, 40, 30).intersected(screen()->availableGeometry()));
    m_presentation->show();
    m_presentation->activateWindow();
    hide();
}

void MainWindow::applySavedControls(const QVector<ControlInfo> &controls)
{
    m_controls = controls;
    for (auto &c : m_controls) {
        const auto it = m_settings.cameraControls.constFind(QString::fromStdString(c.name));
        if (it != m_settings.cameraControls.constEnd() && it->toInt() != c.value) {
            c.value = it->toInt();
            m_scanWindow->setCameraControl(c.id, c.value);
        }
    }
}

void MainWindow::showSettings()
{
    QWidget *parent = m_scanWindow->isVisible() ? static_cast<QWidget *>(m_scanWindow) : this;
    SettingsDialog dlg(m_settings, m_controls, parent);
    connect(&dlg, &SettingsDialog::controlChanged, this, [this](uint32_t id, const QString &name, int32_t value) {
        m_scanWindow->setCameraControl(id, value);
        m_settings.cameraControls.insert(name, value);
        for (auto &c : m_controls)
            if (c.id == id)
                c.value = value;
    });
    const QString oldDir = m_settings.outputDir;
    dlg.exec();
    m_settings.save();
    m_scanWindow->reloadSettings();
    if (oldDir != m_settings.outputDir)
        reloadFiles();
}

void MainWindow::showHelp()
{
    QWidget *parent = m_scanWindow->isVisible() ? static_cast<QWidget *>(m_scanWindow) : this;
    QMessageBox box(parent);
    box.setWindowTitle(tr("About LensScan"));
    box.setIconPixmap(icon(IconId::App).pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<h3>LensScan %1</h3>"
                   "<p>Scanning software for CZUR Lens document cameras on Linux.</p>"
                   "<p><b>Scanning:</b> click <i>Scan</i>, then press the scan button or the Space key. Pages are "
                   "saved in the scan folder. The button on the camera base switches the lamp on and off.</p>"
                   "<p><b>Documents:</b> select pages in the main window, then choose PDF, TIFF, PNG, JPEG, BMP, "
                   "Word, Excel or text. Double-click a page to view it.</p>"
                   "<p><b>Camera not found?</b> Unplug and replug the camera after installing the package. If the "
                   "preview does not start, plug the camera into another USB port directly on the computer.</p>"
                   "<p>Free software under the GNU GPL version 3 or later. Not affiliated with or endorsed by "
                   "CZUR Tech Co., Ltd.; CZUR is a trademark of its owner.</p>")
                    .arg(QStringLiteral(LENSSCAN_VERSION)));
    box.exec();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_taskRunning &&
        QMessageBox::question(this, tr("Quit"), tr("A background task is still running. Quit anyway?")) !=
            QMessageBox::Yes) {
        e->ignore();
        return;
    }
    m_scanWindow->stopCamera();
    e->accept();
    qApp->quit();
}

// --- Clean-up tools -------------------------------------------------------------

void MainWindow::batchEraseHandwriting()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Erase handwriting"), files, nullptr, false);
    HandwritingDialog dlg(loadScan(files.first()), int(files.size()), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const HandwritingOptions o = dlg.options();
    runImageTask(tr("Erase handwriting"), files, [o](const QImage &img) { return eraseHandwriting(img, o); });
}

void MainWindow::batchFillHoles()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Fill punch holes"), files, nullptr, false);
    PunchHoleDialog dlg(loadScan(files.first()), int(files.size()), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int lo = dlg.minRadius(), hi = dlg.maxRadius();
    runImageTask(tr("Fill punch holes"), files, [lo, hi](const QImage &img) { return fillPunchHoles(img, lo, hi); });
}

void MainWindow::batchAutoOrient()
{
    const QStringList files = selectedFiles();
    const QString label = tr("Auto-rotate pages");
    if (files.isEmpty())
        return runTask(label, files, nullptr, false);
    const QString data = tessdataDir();
    if (data.isEmpty() || !QFile::exists(data + QStringLiteral("/osd.traineddata"))) {
        QMessageBox::warning(this, label,
                             tr("The page orientation is found by Tesseract, whose orientation data is not "
                                "installed.\n\nInstall it with your package manager, for example:\n"
                                "sudo apt install tesseract-ocr-osd"));
        return;
    }
    const int quality = m_settings.jpegQuality;
    runTask(label, files, [quality](const QString &f, QString *err) {
        const QImage img = loadScan(f);
        if (img.isNull()) {
            *err = QObject::tr("Cannot read %1").arg(f);
            return false;
        }
        const int r = detectUprightRotation(img);
        if (r > 0 && !saveImage(rotateImage(img, r), f, quality)) {
            *err = QObject::tr("Cannot write %1").arg(f);
            return false;
        }
        return true;
    }, true);
}

void MainWindow::batchFlip(bool horizontal)
{
    runImageTask(horizontal ? tr("Flip horizontally") : tr("Flip vertically"), selectedFiles(),
                 [horizontal](const QImage &img) { return flipImage(img, horizontal); });
}

void MainWindow::readBarcodes()
{
    const QStringList files = selectedFiles(true);
    const QString label = tr("Barcode reading");
    if (files.isEmpty() || m_taskRunning)
        return;
    setTaskState(tr("%1: running...").arg(label), true);
    auto *w = new QFutureWatcher<QVector<FoundBarcode>>(this);
    connect(w, &QFutureWatcher<QVector<FoundBarcode>>::finished, this, [this, w, label] {
        const QVector<FoundBarcode> codes = w->result();
        w->deleteLater();
        setTaskState(tr("%1: done").arg(label), false);
        if (codes.isEmpty()) {
            QMessageBox::information(this, label, tr("No barcode or QR code found."));
            return;
        }
        BarcodeDialog dlg(codes, this);
        dlg.exec();
    });
    w->setFuture(QtConcurrent::run([files] {
        QVector<FoundBarcode> out;
        for (const QString &f : files)
            for (const BarcodeResult &r : lensscan::readBarcodes(loadScan(f)))
                out.append({f, r});
        return out;
    }));
}

void MainWindow::showImageInfo()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Image information"), files, nullptr, false);
    const QString f = files.first();
    const QFileInfo fi(f);
    QImageReader r(f);
    r.setAutoTransform(true);
    const QByteArray format = r.format().toUpper();
    const QImage img = r.read();
    const QLocale loc;
    auto row = [](const QString &k, const QString &v) {
        return QStringLiteral("<tr><td><b>%1</b>&nbsp;&nbsp;</td><td>%2</td></tr>").arg(k.toHtmlEscaped(), v.toHtmlEscaped());
    };
    QString html = QStringLiteral("<table>");
    html += row(tr("Name"), fi.fileName());
    html += row(tr("Folder"), QDir::toNativeSeparators(fi.absolutePath()));
    html += row(tr("Format"), QString::fromLatin1(format));
    html += row(tr("File size"), loc.formattedDataSize(fi.size()));
    html += row(tr("Modified"), loc.toString(fi.lastModified(), QLocale::ShortFormat));
    if (!img.isNull()) {
        html += row(tr("Size"), tr("%1 × %2 pixels").arg(img.width()).arg(img.height()));
        const double mp = img.width() * double(img.height()) / 1e6;
        html += row(tr("Megapixels"), loc.toString(mp, 'f', 1));
        const int dpiX = qRound(img.dotsPerMeterX() * 0.0254), dpiY = qRound(img.dotsPerMeterY() * 0.0254);
        // Qt reports 72 dpi (2835 dots/m) when the file stores no resolution.
        if (dpiX > 0 && img.dotsPerMeterX() != 2835)
            html += row(tr("Resolution"), dpiX == dpiY ? tr("%1 dpi").arg(dpiX) : tr("%1 × %2 dpi").arg(dpiX).arg(dpiY));
        else
            html += row(tr("Resolution"), tr("not stored in the file"));
        html += row(tr("Colors"), img.isGrayscale() ? (img.depth() == 1 ? tr("black and white") : tr("grayscale"))
                                                    : tr("color"));
        if (dpiX > 0 && img.dotsPerMeterX() != 2835)
            html += row(tr("Print size"), tr("%1 × %2 mm")
                                              .arg(qRound(img.width() * 1000.0 / img.dotsPerMeterX()))
                                              .arg(qRound(img.height() * 1000.0 / img.dotsPerMeterY())));
    }
    html += QStringLiteral("</table>");
    QMessageBox box(QMessageBox::Information, tr("Image information"), html, QMessageBox::Close, this);
    box.setTextFormat(Qt::RichText);
    box.setIconPixmap(thumbnail(f).pixmap(96, 96));
    box.exec();
}

// --- Printing -----------------------------------------------------------------------

void MainWindow::printPages()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Print"), files, nullptr, false);
    const QStringList ordered = exportOrder(files);
    if (ordered.isEmpty())
        return;
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QFileInfo(ordered.first()).completeBaseName());
    if (QLocale().measurementSystem() == QLocale::ImperialUSSystem)
        printer.setPageSize(QPageSize(QPageSize::Letter));
    else
        printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(5, 5, 5, 5), QPageLayout::Millimeter);
    QPrintPreviewDialog dlg(&printer, this);
    dlg.setWindowTitle(tr("Print preview"));
    dlg.resize(980, 760);
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, [ordered](QPrinter *p) {
        QPainter painter(p);
        const QRect area(QPoint(0, 0), p->pageLayout().paintRectPixels(p->resolution()).size());
        bool first = true;
        for (const QString &f : ordered) {
            QImage img = loadScan(f);
            if (img.isNull())
                continue;
            if (!first)
                p->newPage();
            first = false;
            // Turn the picture to the page orientation, then fit it in the printable area.
            if ((img.width() > img.height()) != (area.width() > area.height()))
                img = rotateImage(img, 90);
            const QSize s = img.size().scaled(area.size(), Qt::KeepAspectRatio);
            const QRect target(area.x() + (area.width() - s.width()) / 2, area.y() + (area.height() - s.height()) / 2,
                               s.width(), s.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(target, img);
        }
    });
    dlg.exec();
}

// --- Insert and rescan --------------------------------------------------------------

QString MainWindow::placeBefore(const QStringList &newFiles, const QString &before)
{
    if (newFiles.isEmpty())
        return {};
    QStringList names = folderImages();
    for (const QString &f : newFiles)
        names.removeAll(QFileInfo(f).fileName());
    const int index = before.isEmpty() ? int(names.size()) : int(names.indexOf(QFileInfo(before).fileName()));
    if (index < 0)
        return tr("%1 is no longer in the scan folder.").arg(QFileInfo(before).fileName());
    QStringList suffixes;
    for (const QString &f : newFiles)
        suffixes << QLatin1Char('.') + QFileInfo(f).suffix().toLower();
    const QStringList wanted = insertionNames(names, index, suffixes);
    if (wanted.isEmpty())
        return tr("No file name sorts before %1. Rename the pages first (More tools > Rename).")
            .arg(QFileInfo(before).fileName());
    QStringList targets;
    for (const QString &n : wanted)
        targets << QDir(m_settings.outputDir).filePath(n);
    return renameFiles(newFiles, targets);
}

void MainWindow::insertPages()
{
    const QStringList sel = selectedFiles();
    const QString label = tr("Insert pages");
    if (sel.isEmpty() || m_taskRunning) {
        QMessageBox::information(this, label, tr("Select the page before which the new pages go."));
        return;
    }
    const QString target = sel.first();
    QMessageBox box(QMessageBox::Question, label,
                    tr("Insert new pages before %1:").arg(QFileInfo(target).fileName()), QMessageBox::Cancel, this);
    auto *scan = box.addButton(tr("Scan new pages"), QMessageBox::AcceptRole);
    auto *local = box.addButton(tr("Image files..."), QMessageBox::AcceptRole);
    box.exec();
    if (box.clickedButton() == scan) {
        m_pending = PendingScan::Insert;
        m_pendingTarget = target;
        const QStringList now = folderImages();
        m_filesBeforeScan = QSet<QString>(now.begin(), now.end());
        openScanWindow();
        m_scanWindow->setWindowTitle(tr("LensScan - scanning pages to insert before %1").arg(QFileInfo(target).fileName()));
    } else if (box.clickedButton() == local) {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, label, QDir::homePath(), tr("Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp);;All files (*)"));
        if (files.isEmpty())
            return;
        QStringList errors;
        const QStringList added = importImages(files, m_settings.outputDir, &errors);
        const QString err = placeBefore(added, target);
        if (!err.isEmpty())
            errors << err;
        if (!errors.isEmpty())
            QMessageBox::warning(this, label, errors.join(QLatin1Char('\n')));
        reloadFiles();
    }
}

void MainWindow::rescanPage()
{
    const QStringList sel = selectedFiles();
    const QString label = tr("Rescan page");
    if (sel.size() != 1 || m_taskRunning) {
        QMessageBox::information(this, label, tr("Select the one page to scan again."));
        return;
    }
    if (QMessageBox::question(this, label,
                              tr("Scan %1 again?\n\nThe next scan replaces this page (the old file goes to the "
                                 "trash). If the scan gives several pages, the others are inserted after it.")
                                  .arg(QFileInfo(sel.first()).fileName())) != QMessageBox::Yes)
        return;
    m_pending = PendingScan::Rescan;
    m_pendingTarget = sel.first();
    const QStringList now = folderImages();
    m_filesBeforeScan = QSet<QString>(now.begin(), now.end());
    openScanWindow();
    m_scanWindow->setWindowTitle(tr("LensScan - scanning a page to replace %1").arg(QFileInfo(sel.first()).fileName()));
}

void MainWindow::finishPendingScan()
{
    const PendingScan kind = m_pending;
    m_pending = PendingScan::None;
    m_scanWindow->setWindowTitle(ScanWindow::tr("LensScan - Scan"));
    if (kind == PendingScan::None)
        return;
    const QDir dir(m_settings.outputDir);
    QFileInfoList added;
    for (const QString &n : folderImages())
        if (!m_filesBeforeScan.contains(n))
            added << QFileInfo(dir.filePath(n));
    m_filesBeforeScan.clear();
    if (added.isEmpty())
        return;
    // Scan order: by time, then by name (both pages of a book have the same time).
    std::stable_sort(added.begin(), added.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() != b.lastModified() ? a.lastModified() < b.lastModified()
                                                    : scanNameLess(a.fileName(), b.fileName());
    });
    QStringList newFiles;
    for (const QFileInfo &fi : added)
        newFiles << fi.absoluteFilePath();
    QString err;
    if (kind == PendingScan::Insert) {
        err = placeBefore(newFiles, m_pendingTarget);
    } else {
        const QFileInfo old(m_pendingTarget);
        // The page after the old one: the new pages go before it.
        QStringList names = folderImages();
        for (const QString &f : newFiles)
            names.removeAll(QFileInfo(f).fileName());
        const int i = int(names.indexOf(old.fileName()));
        const QString next = i >= 0 && i + 1 < names.size() ? dir.filePath(names.at(i + 1)) : QString();
        if (old.exists() && !QFile::moveToTrash(old.absoluteFilePath()) && !QFile::remove(old.absoluteFilePath()))
            err = tr("Cannot delete %1").arg(old.fileName());
        if (err.isEmpty()) {
            // The first new page takes the old name (with its own extension).
            const QString first = dir.filePath(old.completeBaseName() + QLatin1Char('.') +
                                               QFileInfo(newFiles.first()).suffix().toLower());
            if (QFileInfo::exists(first) || !QFile::rename(newFiles.first(), first))
                err = placeBefore({newFiles.first()}, next);
            if (err.isEmpty() && newFiles.size() > 1)
                err = placeBefore(newFiles.mid(1), next);
        }
    }
    if (!err.isEmpty())
        QMessageBox::warning(this, kind == PendingScan::Insert ? tr("Insert pages") : tr("Rescan page"), err);
}

namespace {

// Saves pages[0..] over `targets` (same order) and the extra pages right after the
// last target, under names that sort before the page that follows it. Targets left
// without a page go to the trash. Returns an error message, empty on success.
QString replaceWithPages(const QStringList &targets, const QVector<QImage> &pages, int quality)
{
    if (pages.isEmpty() || targets.isEmpty())
        return {};
    const int common = int(std::min(targets.size(), qsizetype(pages.size())));
    for (int i = 0; i < common; ++i)
        if (!saveImage(pages[i], targets[i], quality))
            return QObject::tr("Cannot write %1").arg(targets[i]);
    for (int i = common; i < targets.size(); ++i)
        if (!QFile::moveToTrash(targets[i]) && !QFile::remove(targets[i]))
            return QObject::tr("Cannot delete %1").arg(targets[i]);
    if (pages.size() <= targets.size())
        return {};
    const QFileInfo last(targets.last());
    QDir dir = last.dir();
    QStringList names = dir.entryList(kImageFilters, QDir::Files);
    names.removeDuplicates();
    std::sort(names.begin(), names.end(), scanNameLess);
    const int index = int(names.indexOf(last.fileName())) + 1;
    QStringList suffixes;
    for (int i = common; i < pages.size(); ++i)
        suffixes << QLatin1Char('.') + last.suffix().toLower();
    const QStringList wanted = insertionNames(names, index, suffixes);
    if (wanted.isEmpty())
        return QObject::tr("No file name sorts after %1. Rename the pages first (More tools > Rename).")
            .arg(last.fileName());
    for (int i = common; i < pages.size(); ++i) {
        const QString path = dir.filePath(wanted[i - common]);
        if (!saveImage(pages[i], path, quality))
            return QObject::tr("Cannot write %1").arg(path);
    }
    return {};
}

} // namespace

void MainWindow::viewPage(const QString &file)
{
    QStringList all;
    for (int i = 0; i < m_list->count(); ++i)
        all << m_list->item(i)->data(Qt::UserRole).toString();
    const int index = std::max(0, int(all.indexOf(file)));
    if (!m_viewer) {
        m_viewer = new ImageViewer(all, index, this);
        connect(m_viewer, &ImageViewer::editRequested, this, &MainWindow::editPage);
        m_viewer->show();
    } else {
        m_viewer->setFiles(all, index);
        m_viewer->raise();
        m_viewer->activateWindow();
    }
}

void MainWindow::batchPreciseCrop()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Precise crop"), files, nullptr, false);
    PreciseCropDialog dlg(files, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QHash<QString, Margins> margins;
    for (int i = 0; i < files.size(); ++i)
        margins.insert(files[i], dlg.margins(i));
    const bool cut = dlg.cut();
    const int quality = m_settings.jpegQuality;
    runTask(tr("Precise crop"), files, [margins, cut, quality](const QString &f, QString *err) {
        const Margins m = margins.value(f);
        if (m.isNull())
            return true;
        const QImage img = loadScan(f);
        if (img.isNull()) {
            *err = QObject::tr("Cannot read %1").arg(f);
            return false;
        }
        if (!saveImage(cropMargins(img, m, cut), f, quality)) {
            *err = QObject::tr("Cannot write %1").arg(f);
            return false;
        }
        return true;
    }, true);
}

void MainWindow::batchSplitPages()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return runTask(tr("Split facing pages"), files, nullptr, false);
    PageSplitDialog dlg(int(files.size()), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const bool separate = dlg.separateFiles(), fingers = dlg.removeFingers();
    const int quality = m_settings.jpegQuality;
    runTask(tr("Split facing pages"), files, [separate, fingers, quality](const QString &f, QString *err) {
        const QImage img = loadScan(f);
        if (img.isNull()) {
            *err = QObject::tr("Cannot read %1").arg(f);
            return false;
        }
        QVector<QImage> pages = splitFacingPages(img, fingers);
        if (pages.size() < 2)
            return true; // a single page: nothing to split
        for (QImage &p : pages) {
            for (const QString &k : img.textKeys())
                p.setText(k, img.text(k));
            p.setDotsPerMeterX(img.dotsPerMeterX());
            p.setDotsPerMeterY(img.dotsPerMeterY());
        }
        if (!separate)
            pages = {joinPages(pages)};
        *err = replaceWithPages({f}, pages, quality);
        return err->isEmpty();
    }, true);
}

void MainWindow::batchReprocess()
{
    const QStringList files = selectedFiles();
    const QString label = tr("Process again");
    if (files.isEmpty())
        return runTask(label, files, nullptr, false);
    // Pages made from the same camera picture (both pages of a book) are one group.
    QHash<QString, QStringList> groups; // first file -> files of the group
    QHash<QString, QString> groupOriginal;
    QHash<QString, int> groupRotation;
    QHash<QString, QString> leaderOf;   // original -> first file
    QStringList leaders;
    int withOriginal = 0;
    for (const QString &f : files) {
        int rotation = 0;
        const QString original = originalOf(f, &rotation);
        if (!original.isEmpty())
            ++withOriginal;
        if (!original.isEmpty() && leaderOf.contains(original)) {
            groups[leaderOf.value(original)] << f;
            continue;
        }
        if (!original.isEmpty())
            leaderOf.insert(original, f);
        leaders << f;
        groups.insert(f, {f});
        groupOriginal.insert(f, original);
        groupRotation.insert(f, rotation);
    }
    ReprocessDialog dlg(int(files.size()), withOriginal, m_settings.colorMode, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    ScanOptions opt = m_settings.scanOptions();
    opt.pageMode = dlg.pageMode();
    opt.colorMode = dlg.colorMode();
    const int quality = m_settings.jpegQuality;
    runTask(label, leaders, [groups, groupOriginal, groupRotation, opt, quality](const QString &f, QString *err) {
        const QString original = groupOriginal.value(f);
        if (original.isEmpty() && opt.pageMode == PageMode::Original)
            return true; // nothing to go back to
        const QImage frame = loadScan(original.isEmpty() ? f : original);
        if (frame.isNull()) {
            *err = QObject::tr("Cannot read %1").arg(original.isEmpty() ? f : original);
            return false;
        }
        ScanOptions o = opt;
        o.rotation = original.isEmpty() ? 0 : groupRotation.value(f);
        QVector<QImage> pages = processScanPages(frame, o);
        const QImage keep = loadScan(f); // resolution and text fields of the old page
        for (QImage &p : pages) {
            for (const QString &k : keep.textKeys())
                if (k.startsWith(QLatin1String("LensScan")))
                    p.setText(k, keep.text(k));
            p.setDotsPerMeterX(keep.dotsPerMeterX());
            p.setDotsPerMeterY(keep.dotsPerMeterY());
        }
        *err = replaceWithPages(groups.value(f), pages, quality);
        return err->isEmpty();
    }, true);
}

} // namespace lensscan
