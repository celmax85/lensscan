// SPDX-License-Identifier: GPL-3.0-or-later
// Owns the V4L2 device in a worker thread: low-resolution MJPEG preview, full-
// resolution capture on request (format switched in place on the same fd), frame
// validation, automatic reconnection, and V4L2 control changes.
#pragma once

#include "device/V4l2Device.h"

#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>

namespace lensscan {

struct CameraDescription {
    QString node;
    QString name;
    QString usbSysPath;
    bool czur = false;
    QSize previewSize;
    QSize captureSize;
    QVector<QSize> captureSizes;
    QVector<int> captureFps; // highest frame rate of each capture size (0 = unknown)
};

class CameraThread : public QThread {
    Q_OBJECT
public:
    explicit CameraThread(QObject *parent = nullptr);
    ~CameraThread() override;

    // All of these are safe to call from the GUI thread.
    void stop();
    void requestCapture();
    void setCaptureSize(const QSize &size); // empty = largest
    void setControl(uint32_t id, int32_t value);
    void frameShown(); // GUI finished painting the last preview frame
    void setPreviewWidth(int width);
    // Stream size of the live view (one of captureSizes); empty = automatic (<= 1600 px
    // wide at >= 15 fps). The stream restarts at the new size.
    void setPreviewSize(const QSize &size);

Q_SIGNALS:
    void cameraOpened(const lensscan::CameraDescription &camera);
    void cameraLost(const QString &reason);
    void controlsAvailable(const QVector<lensscan::ControlInfo> &controls);
    void previewFrame(const QImage &frame);
    void captured(const QByteArray &jpeg);
    void captureFailed(const QString &reason);
    void previewSizeChanged(const QSize &size);

protected:
    void run() override;

private:
    bool openCamera();
    bool startPreview();
    QSize wantedPreviewSize();
    void closeCamera(const QString &reason);
    void doCapture();
    void applyPendingControls();
    QString describeError() const;
    bool sleepInterruptible(int ms);

    V4l2Device m_dev;
    CameraDescription m_cam;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_captureRequested{false};
    std::atomic<int> m_framesInFlight{0};
    std::atomic<int> m_previewWidth{1280};
    std::atomic<bool> m_controlsDirty{false};
    std::atomic<bool> m_previewSizeDirty{false};
    QSize m_requestedPreviewSize; // guarded by m_mutex
    QMutex m_mutex;
    QSize m_requestedCaptureSize;
    QVector<QPair<uint32_t, int32_t>> m_pendingControls;
};

} // namespace lensscan

Q_DECLARE_METATYPE(lensscan::CameraDescription)
Q_DECLARE_METATYPE(lensscan::ControlInfo)
