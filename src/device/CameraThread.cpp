// SPDX-License-Identifier: GPL-3.0-or-later
#include "CameraThread.h"

#include <algorithm>

#include "device/DeviceDb.h"
#include "imaging/Jpeg.h"

#include <QMutexLocker>
#include <cerrno>
#include <linux/videodev2.h>

namespace lensscan {

namespace {
constexpr int kPreviewMaxWidth = 1600;
constexpr int kCaptureAttempts = 3;   // stream restarts per capture
constexpr int kFramesPerAttempt = 5;

QSize pickPreviewSize(const std::vector<PixelFormat> &formats)
{
    // Largest MJPEG size no wider than 1600 px that still runs at >= 15 fps.
    for (const auto &f : formats) {
        if (f.fourcc != V4L2_PIX_FMT_MJPEG)
            continue;
        for (const auto &s : f.sizes) // sorted by area, descending
            if (int(s.width) <= kPreviewMaxWidth && (s.fps.empty() || s.fps.front() >= 15))
                return QSize(int(s.width), int(s.height));
        if (!f.sizes.empty())
            return QSize(int(f.sizes.back().width), int(f.sizes.back().height));
    }
    return {};
}

QVector<int> mjpegFps(const std::vector<PixelFormat> &formats)
{
    QVector<int> out;
    for (const auto &f : formats)
        if (f.fourcc == V4L2_PIX_FMT_MJPEG)
            for (const auto &s : f.sizes)
                out.append(s.fps.empty() ? 0 : int(s.fps.front() + 0.5));
    return out;
}

QVector<QSize> mjpegSizes(const std::vector<PixelFormat> &formats)
{
    QVector<QSize> out;
    for (const auto &f : formats)
        if (f.fourcc == V4L2_PIX_FMT_MJPEG)
            for (const auto &s : f.sizes)
                out.append(QSize(int(s.width), int(s.height)));
    return out;
}
} // namespace

CameraThread::CameraThread(QObject *parent) : QThread(parent)
{
    qRegisterMetaType<CameraDescription>();
    qRegisterMetaType<QVector<ControlInfo>>();
}

CameraThread::~CameraThread()
{
    stop();
    wait();
}

void CameraThread::stop() { m_stop = true; }
void CameraThread::requestCapture() { m_captureRequested = true; }
void CameraThread::frameShown()
{
    if (m_framesInFlight > 0)
        --m_framesInFlight;
}
void CameraThread::setPreviewWidth(int width) { m_previewWidth = qMax(320, width); }

void CameraThread::setPreviewSize(const QSize &size)
{
    {
        QMutexLocker lock(&m_mutex);
        m_requestedPreviewSize = size;
    }
    m_previewSizeDirty = true;
}

QSize CameraThread::wantedPreviewSize()
{
    QSize s;
    {
        QMutexLocker lock(&m_mutex);
        s = m_requestedPreviewSize;
    }
    return s.isValid() && m_cam.captureSizes.contains(s) ? s : QSize();
}

void CameraThread::setCaptureSize(const QSize &size)
{
    QMutexLocker lock(&m_mutex);
    m_requestedCaptureSize = size;
}

void CameraThread::setControl(uint32_t id, int32_t value)
{
    QMutexLocker lock(&m_mutex);
    for (auto &p : m_pendingControls)
        if (p.first == id) {
            p.second = value;
            m_controlsDirty = true;
            return;
        }
    m_pendingControls.append({id, value});
    m_controlsDirty = true;
}

bool CameraThread::sleepInterruptible(int ms)
{
    for (int t = 0; t < ms && !m_stop; t += 50)
        msleep(50);
    return !m_stop;
}

QString CameraThread::describeError() const
{
    QString msg = QString::fromStdString(m_dev.lastError());
    if (m_dev.lastErrno() == EPROTO)
        msg += QLatin1Char('\n') + tr("The camera refused to start streaming. This usually means the USB port "
                                      "does not deliver enough power: plug the camera into another USB port, "
                                      "directly on the computer.");
    else if (m_dev.lastErrno() == EACCES || m_dev.lastErrno() == EPERM)
        msg += QLatin1Char('\n') + tr("Permission denied. Install the LensScan udev rule, then unplug and "
                                      "replug the camera.");
    else if (m_dev.lastErrno() == EBUSY)
        msg += QLatin1Char('\n') + tr("Another application is using the camera. Close it and try again.");
    return msg;
}

bool CameraThread::openCamera()
{
    const auto devs = V4l2Device::enumerate();
    const DeviceInfo *pick = nullptr;
    for (const auto &d : devs)
        if (isCzurDevice(d)) {
            pick = &d;
            break;
        }
    if (!pick)
        return false; // only CZUR cameras for now; webcams are not document cameras
    if (!m_dev.open(pick->node)) {
        Q_EMIT cameraLost(describeError());
        return false;
    }
    const auto formats = m_dev.formats();
    m_cam = {};
    m_cam.node = QString::fromStdString(pick->node);
    m_cam.name = QString::fromStdString(pick->name);
    m_cam.usbSysPath = QString::fromStdString(pick->usbSysPath);
    m_cam.czur = true;
    if (auto model = lookupModel(pick->vid, pick->pid))
        m_cam.name = QString::fromUtf8(model->name);
    m_cam.previewSize = pickPreviewSize(formats);
    m_cam.captureSizes = mjpegSizes(formats);
    m_cam.captureFps = mjpegFps(formats);
    if (const QSize wanted = wantedPreviewSize(); wanted.isValid())
        m_cam.previewSize = wanted;
    m_cam.captureSize = m_cam.captureSizes.isEmpty() ? QSize() : m_cam.captureSizes.front();
    if (!m_cam.previewSize.isValid()) {
        m_dev.close();
        Q_EMIT cameraLost(tr("The camera does not offer MJPEG video."));
        return false;
    }
    return true;
}

bool CameraThread::startPreview()
{
    m_dev.stopStreaming();
    if (!m_dev.setFormat(V4L2_PIX_FMT_MJPEG, uint32_t(m_cam.previewSize.width()),
                         uint32_t(m_cam.previewSize.height())) ||
        !m_dev.startStreaming(4))
        return false;
    return true;
}

void CameraThread::closeCamera(const QString &reason)
{
    m_dev.stopStreaming();
    m_dev.close();
    m_framesInFlight = 0;
    Q_EMIT cameraLost(reason);
}

void CameraThread::applyPendingControls()
{
    if (!m_controlsDirty.exchange(false))
        return;
    QVector<QPair<uint32_t, int32_t>> todo;
    {
        QMutexLocker lock(&m_mutex);
        todo.swap(m_pendingControls);
    }
    for (const auto &c : todo)
        m_dev.setControl(c.first, c.second);
}

void CameraThread::doCapture()
{
    QSize size;
    {
        QMutexLocker lock(&m_mutex);
        size = m_requestedCaptureSize;
    }
    if (!size.isValid() || !m_cam.captureSizes.contains(size))
        size = m_cam.captureSize;

    // At full resolution and 15 fps, a well-lit, detailed page makes MJPEG frames
    // larger than the USB 2 isochronous bandwidth: the camera then drops the start of
    // every frame and only frame tails (no SOI) arrive. The camera has no frame-rate or
    // JPEG-quality control, but its sharpening strongly drives the frame size
    // (measured: sharpness 4 -> 1.5 MB, broken; 2 -> 1.1 MB, 0 -> 0.9 MB, intact).
    // So when no valid frame comes, retry with less in-camera sharpening (the
    // processing pipeline sharpens in software anyway) and restore it afterwards.
    int32_t sharpness = -1;
    const bool haveSharpness = m_dev.getControl(V4L2_CID_SHARPNESS, sharpness);
    const int32_t sharpnessSteps[kCaptureAttempts] = {sharpness, std::min<int32_t>(sharpness, 2), 0};

    QByteArray result;
    int frames = 0, notJpeg = 0, badDecode = 0;
    for (int attempt = 0; attempt < kCaptureAttempts && result.isEmpty() && !m_stop; ++attempt) {
        if (attempt > 0 && haveSharpness) {
            if (sharpnessSteps[attempt] == sharpnessSteps[attempt - 1])
                continue;
            m_dev.setControl(V4L2_CID_SHARPNESS, sharpnessSteps[attempt]);
        }
        m_dev.stopStreaming();
        if (!m_dev.setFormat(V4L2_PIX_FMT_MJPEG, uint32_t(size.width()), uint32_t(size.height())) ||
            !m_dev.startStreaming(3)) {
            Q_EMIT captureFailed(describeError());
            return;
        }
        for (int i = 0; i < kFramesPerAttempt && result.isEmpty() && !m_stop; ++i) {
            m_dev.readFrame(3000, [&](const uint8_t *p, size_t n) {
                ++frames;
                if (!looksLikeJpeg(p, n)) {
                    ++notJpeg;
                    return;
                }
                if (decodeJpeg(p, n, 0, true).size() != size) {
                    ++badDecode;
                    return;
                }
                result = QByteArray(reinterpret_cast<const char *>(p), int(n));
            });
        }
    }
    if (haveSharpness)
        m_dev.setControl(V4L2_CID_SHARPNESS, sharpness);
    if (result.isEmpty()) {
        qWarning("capture %dx%d: %d frames, %d not JPEG, %d undecodable, error: %s", size.width(), size.height(),
                 frames, notJpeg, badDecode, m_dev.lastError().c_str());
        Q_EMIT captureFailed(tr("No valid image received from the camera."));
    } else {
        Q_EMIT captured(result);
    }
}

void CameraThread::run()
{
    m_stop = false;
    QString lastReason;
    while (!m_stop) {
        if (!m_dev.isOpen()) {
            if (!openCamera()) {
                if (lastReason.isEmpty()) {
                    lastReason = tr("No CZUR camera found. Connect the camera with its USB cable.");
                    Q_EMIT cameraLost(lastReason);
                }
                sleepInterruptible(1000);
                continue;
            }
            if (!startPreview()) {
                const QString reason = describeError();
                m_dev.close();
                Q_EMIT cameraLost(reason);
                lastReason = reason;
                sleepInterruptible(5000); // do not hammer a camera that refuses STREAMON
                continue;
            }
            lastReason.clear();
            Q_EMIT cameraOpened(m_cam);
            const auto ctrls = m_dev.controls();
            Q_EMIT controlsAvailable(QVector<ControlInfo>(ctrls.begin(), ctrls.end()));
        }

        applyPendingControls();

        if (m_previewSizeDirty.exchange(false)) {
            QSize wanted = wantedPreviewSize();
            if (!wanted.isValid())
                wanted = pickPreviewSize(m_dev.formats());
            if (wanted.isValid() && wanted != m_cam.previewSize) {
                const QSize old = m_cam.previewSize;
                m_cam.previewSize = wanted;
                if (!startPreview()) {
                    m_cam.previewSize = old; // the camera refused: back to the previous size
                    if (!startPreview()) {
                        closeCamera(describeError());
                        sleepInterruptible(3000);
                        continue;
                    }
                }
                m_framesInFlight = 0;
                Q_EMIT previewSizeChanged(m_cam.previewSize);
            }
        }

        if (m_captureRequested.exchange(false)) {
            doCapture();
            if (!startPreview()) {
                closeCamera(describeError());
                sleepInterruptible(3000);
            }
            continue;
        }

        int timeouts = 0;
        bool gotFrame = false;
        while (!gotFrame && !m_stop && !m_captureRequested) {
            gotFrame = m_dev.readFrame(200, [&](const uint8_t *p, size_t n) {
                if (m_framesInFlight > 1)
                    return; // GUI is behind: drop this frame
                const QImage img = decodeJpeg(p, n, m_previewWidth);
                if (img.isNull())
                    return;
                ++m_framesInFlight;
                Q_EMIT previewFrame(img);
            });
            if (gotFrame)
                break;
            const int err = m_dev.lastErrno();
            if (err == ENODEV || err == EIO) {
                closeCamera(tr("The camera was disconnected."));
                break;
            }
            if (err == ETIMEDOUT && ++timeouts >= 25) { // 5 s without a frame
                closeCamera(tr("The camera stopped sending images. Reconnecting..."));
                sleepInterruptible(1000);
                break;
            }
            if (m_controlsDirty || m_previewSizeDirty)
                break;
        }
    }
    m_dev.stopStreaming();
    m_dev.close();
}

} // namespace lensscan
