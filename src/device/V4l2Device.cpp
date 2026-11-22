// SPDX-License-Identifier: GPL-3.0-or-later
#include "V4l2Device.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <limits.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace lensscan {

namespace {

std::string readTextFile(const std::string &path)
{
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

std::vector<uint8_t> readBinaryFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

bool fileExists(const std::string &p) { return ::access(p.c_str(), F_OK) == 0; }

// Walks up from the V4L2 sysfs device to the USB device directory (the one with idVendor).
std::string findUsbDeviceDir(const std::string &videoName)
{
    char resolved[PATH_MAX];
    const std::string link = "/sys/class/video4linux/" + videoName + "/device";
    if (!::realpath(link.c_str(), resolved))
        return {};
    std::string dir = resolved;
    for (int i = 0; i < 4 && dir.size() > 1; ++i) {
        if (fileExists(dir + "/idVendor"))
            return dir;
        dir = dir.substr(0, dir.find_last_of('/'));
    }
    return {};
}

} // namespace

std::string fourccToString(uint32_t f)
{
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i)
        s[i] = static_cast<char>((f >> (8 * i)) & 0xff);
    return s;
}

V4l2Device::~V4l2Device() { close(); }

int V4l2Device::xioctl(unsigned long req, void *arg) const
{
    int r;
    do {
        r = ::ioctl(m_fd, req, arg);
    } while (r < 0 && errno == EINTR);
    return r;
}

bool V4l2Device::fail(const std::string &what)
{
    m_errno = errno;
    m_error = what + ": " + std::strerror(m_errno);
    return false;
}

std::vector<DeviceInfo> V4l2Device::enumerate()
{
    std::vector<DeviceInfo> out;
    DIR *d = ::opendir("/sys/class/video4linux");
    if (!d)
        return out;
    std::vector<std::string> names;
    while (dirent *e = ::readdir(d)) {
        if (std::strncmp(e->d_name, "video", 5) == 0)
            names.emplace_back(e->d_name);
    }
    ::closedir(d);
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        return std::atoi(a.c_str() + 5) < std::atoi(b.c_str() + 5);
    });

    for (const auto &n : names) {
        DeviceInfo info;
        info.node = "/dev/" + n;
        int fd = ::open(info.node.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue;
        v4l2_capability cap{};
        const bool ok = ::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
        ::close(fd);
        if (!ok)
            continue;
        const uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        if (!(caps & V4L2_CAP_VIDEO_CAPTURE))
            continue;
        info.name = reinterpret_cast<const char *>(cap.card);
        info.busInfo = reinterpret_cast<const char *>(cap.bus_info);
        info.usbSysPath = findUsbDeviceDir(n);
        if (!info.usbSysPath.empty()) {
            info.vid = static_cast<uint16_t>(std::stoul(readTextFile(info.usbSysPath + "/idVendor"), nullptr, 16));
            info.pid = static_cast<uint16_t>(std::stoul(readTextFile(info.usbSysPath + "/idProduct"), nullptr, 16));
            info.extensionUnits = parseExtensionUnits(readBinaryFile(info.usbSysPath + "/descriptors"));
        }
        out.push_back(std::move(info));
    }
    return out;
}

bool V4l2Device::open(const std::string &node)
{
    close();
    m_fd = ::open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0)
        return fail("open " + node);
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(VIDIOC_G_FMT, &fmt) == 0) {
        m_width = fmt.fmt.pix.width;
        m_height = fmt.fmt.pix.height;
        m_fourcc = fmt.fmt.pix.pixelformat;
    }
    return true;
}

void V4l2Device::close()
{
    stopStreaming();
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

std::vector<PixelFormat> V4l2Device::formats() const
{
    std::vector<PixelFormat> out;
    if (m_fd < 0)
        return out;
    for (uint32_t i = 0;; ++i) {
        v4l2_fmtdesc fd{};
        fd.index = i;
        fd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(VIDIOC_ENUM_FMT, &fd) < 0)
            break;
        PixelFormat pf;
        pf.fourcc = fd.pixelformat;
        pf.description = reinterpret_cast<const char *>(fd.description);
        for (uint32_t j = 0;; ++j) {
            v4l2_frmsizeenum fs{};
            fs.index = j;
            fs.pixel_format = fd.pixelformat;
            if (xioctl(VIDIOC_ENUM_FRAMESIZES, &fs) < 0 || fs.type != V4L2_FRMSIZE_TYPE_DISCRETE)
                break;
            FrameSize size{fs.discrete.width, fs.discrete.height, {}};
            for (uint32_t k = 0;; ++k) {
                v4l2_frmivalenum fi{};
                fi.index = k;
                fi.pixel_format = fd.pixelformat;
                fi.width = size.width;
                fi.height = size.height;
                if (xioctl(VIDIOC_ENUM_FRAMEINTERVALS, &fi) < 0 || fi.type != V4L2_FRMIVAL_TYPE_DISCRETE)
                    break;
                if (fi.discrete.numerator)
                    size.fps.push_back(double(fi.discrete.denominator) / fi.discrete.numerator);
            }
            std::sort(size.fps.rbegin(), size.fps.rend());
            pf.sizes.push_back(std::move(size));
        }
        std::sort(pf.sizes.begin(), pf.sizes.end(), [](const FrameSize &a, const FrameSize &b) {
            return uint64_t(a.width) * a.height > uint64_t(b.width) * b.height;
        });
        out.push_back(std::move(pf));
    }
    return out;
}

bool V4l2Device::setFormat(uint32_t fourcc, uint32_t width, uint32_t height, double fps)
{
    if (m_streaming) {
        m_error = "cannot change format while streaming";
        return false;
    }
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(VIDIOC_S_FMT, &fmt) < 0)
        return fail("VIDIOC_S_FMT");
    m_width = fmt.fmt.pix.width;
    m_height = fmt.fmt.pix.height;
    m_fourcc = fmt.fmt.pix.pixelformat;
    if (fps > 0) {
        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1000;
        parm.parm.capture.timeperframe.denominator = static_cast<uint32_t>(fps * 1000);
        xioctl(VIDIOC_S_PARM, &parm); // best effort
    }
    return true;
}

std::vector<ControlInfo> V4l2Device::controls() const
{
    std::vector<ControlInfo> out;
    if (m_fd < 0)
        return out;
    v4l2_queryctrl q{};
    q.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    while (xioctl(VIDIOC_QUERYCTRL, &q) == 0) {
        if (!(q.flags & V4L2_CTRL_FLAG_DISABLED) && q.type != V4L2_CTRL_TYPE_CTRL_CLASS) {
            ControlInfo c;
            c.id = q.id;
            c.name = reinterpret_cast<const char *>(q.name);
            c.type = q.type;
            c.minimum = q.minimum;
            c.maximum = q.maximum;
            c.step = q.step ? q.step : 1;
            c.defaultValue = q.default_value;
            c.inactive = q.flags & V4L2_CTRL_FLAG_INACTIVE;
            getControl(q.id, c.value);
            if (q.type == V4L2_CTRL_TYPE_MENU) {
                for (int32_t i = q.minimum; i <= q.maximum; ++i) {
                    v4l2_querymenu m{};
                    m.id = q.id;
                    m.index = static_cast<uint32_t>(i);
                    if (xioctl(VIDIOC_QUERYMENU, &m) == 0)
                        c.menu.emplace_back(i, reinterpret_cast<const char *>(m.name));
                }
            }
            out.push_back(std::move(c));
        }
        q.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }
    return out;
}

bool V4l2Device::getControl(uint32_t id, int32_t &value) const
{
    v4l2_control c{id, 0};
    if (xioctl(VIDIOC_G_CTRL, &c) < 0)
        return false;
    value = c.value;
    return true;
}

bool V4l2Device::setControl(uint32_t id, int32_t value)
{
    v4l2_control c{id, value};
    if (xioctl(VIDIOC_S_CTRL, &c) < 0)
        return fail("VIDIOC_S_CTRL");
    return true;
}

bool V4l2Device::xuQuery(uint8_t unit, uint8_t selector, uint8_t query, uint8_t *data, uint16_t size) const
{
    if (m_fd < 0)
        return false;
    uvc_xu_control_query q{};
    q.unit = unit;
    q.selector = selector;
    q.query = query;
    q.size = size;
    q.data = data;
    return xioctl(UVCIOC_CTRL_QUERY, &q) == 0;
}

bool V4l2Device::startStreaming(unsigned bufferCount)
{
    if (m_streaming)
        return true;
    v4l2_requestbuffers req{};
    req.count = bufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(VIDIOC_REQBUFS, &req) < 0)
        return fail("VIDIOC_REQBUFS");
    m_buffers.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (xioctl(VIDIOC_QUERYBUF, &b) < 0)
            return fail("VIDIOC_QUERYBUF");
        void *p = ::mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, b.m.offset);
        if (p == MAP_FAILED)
            return fail("mmap");
        m_buffers[i] = {p, b.length};
        if (xioctl(VIDIOC_QBUF, &b) < 0)
            return fail("VIDIOC_QBUF");
    }
    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(VIDIOC_STREAMON, &t) < 0)
        return fail("VIDIOC_STREAMON");
    m_streaming = true;
    return true;
}

void V4l2Device::stopStreaming()
{
    if (m_fd < 0)
        return;
    if (m_streaming) {
        v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(VIDIOC_STREAMOFF, &t);
        m_streaming = false;
    }
    for (auto &b : m_buffers)
        if (b.start)
            ::munmap(b.start, b.length);
    if (!m_buffers.empty()) {
        m_buffers.clear();
        v4l2_requestbuffers req{};
        req.count = 0;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        xioctl(VIDIOC_REQBUFS, &req);
    }
}

bool V4l2Device::readFrame(int timeoutMs, const std::function<void(const uint8_t *, size_t)> &cb)
{
    if (!m_streaming)
        return false;
    pollfd p{m_fd, POLLIN, 0};
    const int r = ::poll(&p, 1, timeoutMs);
    if (r <= 0) {
        if (r == 0) {
            m_errno = ETIMEDOUT;
            m_error = "timeout";
        }
        else
            fail("poll");
        return false;
    }
    v4l2_buffer b{};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    b.memory = V4L2_MEMORY_MMAP;
    if (xioctl(VIDIOC_DQBUF, &b) < 0)
        return fail("VIDIOC_DQBUF");
    const bool good = !(b.flags & V4L2_BUF_FLAG_ERROR) && b.bytesused > 0;
    if (good && cb)
        cb(static_cast<const uint8_t *>(m_buffers[b.index].start), b.bytesused);
    if (xioctl(VIDIOC_QBUF, &b) < 0)
        return fail("VIDIOC_QBUF");
    if (!good)
        m_error = "corrupted frame";
    return good;
}

} // namespace lensscan
