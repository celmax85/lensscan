// SPDX-License-Identifier: GPL-3.0-or-later
// Scan window: live preview, color mode and page handling options, rotation,
// scan button (also triggered by the Space key),
// and the list of pages scanned in this session.
#pragma once

#include "app/AppSettings.h"
#include "device/CameraThread.h"
#include "imaging/AutoScan.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QPolygonF>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QToolButton;
class QPushButton;

namespace lensscan {

class PreviewWidget;

class ScanWindow : public QWidget {
    Q_OBJECT
public:
    explicit ScanWindow(AppSettings &settings, QWidget *parent = nullptr);
    ~ScanWindow() override;

    void startCamera();
    void stopCamera();
    void reloadSettings(); // after the settings dialog changed something

Q_SIGNALS:
    void backRequested();
    void scanSaved(const QString &path);
    void settingsRequested();
    void helpRequested();
    void controlsAvailable(const QVector<lensscan::ControlInfo> &controls);

public Q_SLOTS:
    void scan();
    void setCameraControl(uint32_t id, int32_t value);

protected:
    void closeEvent(QCloseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QWidget *buildTopBar();
    QWidget *buildToolPane();
    QWidget *buildBottomBar();
    void onCameraOpened(const CameraDescription &cam);
    void onCameraLost(const QString &reason);
    void onPreviewFrame(const QImage &frame);
    void onCaptured(const QByteArray &jpeg);
    void onCaptureFailed(const QString &reason);
    void onScanProcessed();
    void updateModeUi();
    void updateCounters();
    void updateFolderLabel();
    void playBeep();
    void rotate(int delta);

    AppSettings &m_settings;
    CameraThread *m_camera = nullptr;
    CameraDescription m_cam;
    bool m_cameraReady = false;
    bool m_busy = false;
    int m_scanned = 0;
    int m_processed = 0;
    int m_frameCounter = 0;
    // Auto scan on page turn (off at each start, so the camera never fires unexpectedly).
    bool m_autoScan = false;
    AutoScanTrigger m_autoTrigger;
    QElapsedTimer m_clock;

    PreviewWidget *m_preview = nullptr;
    QLabel *m_folderLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_deviceLabel = nullptr;
    QButtonGroup *m_colorGroup = nullptr;
    QButtonGroup *m_pageGroup = nullptr;
    QComboBox *m_resolution = nullptr;
    QCheckBox *m_beep = nullptr;
    QListWidget *m_pages = nullptr;
    QPushButton *m_scanButton = nullptr;
    QToolButton *m_rotLeft = nullptr;
    QToolButton *m_rotRight = nullptr;

    QFutureWatcher<QVector<QPolygonF>> m_detectWatcher;
    QList<QFutureWatcher<QVector<ScanResult>> *> m_jobs;
    QWidget *m_bookOptions = nullptr;
    QWidget *m_flatOptions = nullptr;
};

} // namespace lensscan
