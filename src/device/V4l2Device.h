// SPDX-License-Identifier: GPL-3.0-or-later
// Thin RAII wrapper over a V4L2 capture node (uvcvideo), Qt-free.
#pragma once

#include "UvcDescriptors.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lensscan {

struct DeviceInfo {
    std::string node;      // /dev/video2
    std::string name;      // "CZUR MIC: CZUR E012A"
    std::string busInfo;   // usb-0000:00:14.0-3
    std::string usbSysPath; // /sys/bus/usb/devices/1-3
    uint16_t vid = 0;
    uint16_t pid = 0;
    std::vector<ExtensionUnit> extensionUnits;
};

struct FrameSize {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<double> fps; // discrete intervals, descending
};

struct PixelFormat {
    uint32_t fourcc = 0;
    std::string description;
    std::vector<FrameSize> sizes; // sorted by area, descending
};

struct ControlInfo {
    uint32_t id = 0;
    std::string name;
    int type = 0; // V4L2_CTRL_TYPE_*
    int32_t minimum = 0, maximum = 0, step = 1, defaultValue = 0, value = 0;
    bool inactive = false;
    std::vector<std::pair<int32_t, std::string>> menu;
};

std::string fourccToString(uint32_t fourcc);

class V4l2Device {
public:
    V4l2Device() = default;
    ~V4l2Device();
    V4l2Device(const V4l2Device &) = delete;
    V4l2Device &operator=(const V4l2Device &) = delete;

    // Lists V4L2 nodes that can capture video (metadata nodes are skipped).
    static std::vector<DeviceInfo> enumerate();

    bool open(const std::string &node);
    void close();
    bool isOpen() const { return m_fd >= 0; }
    int fd() const { return m_fd; }
    const std::string &lastError() const { return m_error; }
    int lastErrno() const { return m_errno; }

    std::vector<PixelFormat> formats() const;
    // Negotiates format; returns the size actually chosen by the driver.
    bool setFormat(uint32_t fourcc, uint32_t width, uint32_t height, double fps = 0);
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    uint32_t fourcc() const { return m_fourcc; }

    std::vector<ControlInfo> controls() const;
    bool getControl(uint32_t id, int32_t &value) const;
    bool setControl(uint32_t id, int32_t value);

    // Raw UVC extension unit access (UVCIOC_CTRL_QUERY). query = UVC_GET_CUR etc.
    bool xuQuery(uint8_t unit, uint8_t selector, uint8_t query, uint8_t *data, uint16_t size) const;

    bool startStreaming(unsigned bufferCount = 4);
    void stopStreaming();
    bool isStreaming() const { return m_streaming; }
    // Waits up to timeoutMs for a frame. The callback receives the mapped buffer;
    // it must copy what it needs before returning. Returns false on timeout/error.
    bool readFrame(int timeoutMs, const std::function<void(const uint8_t *, size_t)> &cb);

private:
    bool fail(const std::string &what);
    int xioctl(unsigned long req, void *arg) const;

    int m_fd = -1;
    std::string m_error;
    int m_errno = 0;
    uint32_t m_width = 0, m_height = 0, m_fourcc = 0;
    bool m_streaming = false;
    struct Buffer {
        void *start = nullptr;
        size_t length = 0;
    };
    std::vector<Buffer> m_buffers;
};

} // namespace lensscan
