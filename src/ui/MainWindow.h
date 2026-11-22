// SPDX-License-Identifier: GPL-3.0-or-later
// Main window: scan folder browser with thumbnails, batch processing (color
// mode, rotation, delete), document output (PDF, TIFF, PNG, JPEG), and the entry
// point to the scan window.
#pragma once

#include "app/AppSettings.h"
#include "device/V4l2Device.h"

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHash>
#include <QIcon>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <atomic>
#include <functional>

class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace lensscan {

class ScanWindow;
class PresentationWindow;
class ImageViewer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppSettings &settings, QWidget *parent = nullptr);
    ~MainWindow() override;

public Q_SLOTS:
    void openScanWindow();
    void openPresentation();
    void showSettings();
    void showHelp();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QWidget *buildTopBar();
    QWidget *buildSidePanel();
    QWidget *buildCenter();
    void reloadFiles();
    void loadThumbnails();
    void updateSelectionInfo();
    QStringList selectedFiles(bool allIfNone = false) const;
    void runTask(const QString &label, const QStringList &files,
                 const std::function<bool(const QString &file, QString *error)> &perFile, bool refresh);
    void runExport(const QString &label, const std::function<bool(QString *error)> &job, const QString &result);
    void batchColorMode();
    void batchRotate(int degrees);
    void batchDelete();
    // Runs op on every file (load, transform, save in place) in the background.
    void runImageTask(const QString &label, const QStringList &files, const std::function<QImage(const QImage &)> &op);
    void editPage(const QString &file);
    void batchAdjust();
    void batchWatermark();
    void batchTrimEdges();
    void batchRename();
    void batchSplitDocuments();
    void batchOverlapCrop();
    void detectBlankPages();
    void batchEraseHandwriting();
    void batchFillHoles();
    void batchAutoOrient();
    void batchFlip(bool horizontal);
    void batchPreciseCrop();
    void batchSplitPages();
    void batchReprocess();
    void viewPage(const QString &file);
    void readBarcodes();
    void showImageInfo();
    void printPages();
    void insertPages();
    void rescanPage();
    // New pages (in this order) get names that sort right before `before` (empty: at
    // the end). Returns an error message, empty on success.
    QString placeBefore(const QStringList &newFiles, const QString &before);
    void finishPendingScan();
    QStringList folderImages() const; // file names, in list order
    void importFiles();
    QStringList exportOrder(const QStringList &files); // empty = cancelled
    QIcon thumbnail(const QString &file) const;
    void exportPdf();
    void exportTiff();
    void exportOcr(const QString &format); // "pdf", "txt", "docx" or "xlsx"
    void exportImages(const QString &format);
    void changeFolder();
    void setTaskState(const QString &text, bool busy);
    void applySavedControls(const QVector<ControlInfo> &controls);

    AppSettings &m_settings;
    ScanWindow *m_scanWindow = nullptr;
    QPointer<PresentationWindow> m_presentation;
    QPointer<ImageViewer> m_viewer;
    QVector<ControlInfo> m_controls;

    QListWidget *m_list = nullptr;
    QLabel *m_folderLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QLabel *m_taskLabel = nullptr;
    QCheckBox *m_selectAll = nullptr;
    QList<QWidget *> m_taskButtons;

    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QHash<QString, QPair<qint64, QIcon>> m_thumbCache; // path -> (mtime, icon)
    QFutureWatcher<QPair<QString, QImage>> m_thumbWatcher;
    std::atomic<bool> m_cancelThumbs{false};
    bool m_taskRunning = false;

    enum class PendingScan { None, Insert, Rescan };
    PendingScan m_pending = PendingScan::None;
    QString m_pendingTarget;
    QSet<QString> m_filesBeforeScan;
};

} // namespace lensscan
