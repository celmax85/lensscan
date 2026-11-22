// SPDX-License-Identifier: GPL-3.0-or-later
#include "UsbReset.h"

#include "V4l2Device.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace lensscan {

bool usbResetDevice(const std::string &usbSysPath, std::string &error)
{
    int bus = 0, dev = 0;
    std::ifstream(usbSysPath + "/busnum") >> bus;
    std::ifstream(usbSysPath + "/devnum") >> dev;
    if (!bus || !dev) {
        error = "cannot read bus/device number from " + usbSysPath;
        return false;
    }
    char path[64];
    std::snprintf(path, sizeof path, "/dev/bus/usb/%03d/%03d", bus, dev);
    const int fd = ::open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        error = std::string("open ") + path + ": " + std::strerror(errno);
        return false;
    }
    const int r = ::ioctl(fd, USBDEVFS_RESET, 0);
    const int err = errno;
    ::close(fd);
    if (r < 0) {
        error = std::string("USBDEVFS_RESET: ") + std::strerror(err);
        return false;
    }
    return true;
}

std::string waitForVideoNode(uint16_t vid, uint16_t pid, int timeoutMs)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    // Give the kernel time to tear down the old node first.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    while (std::chrono::steady_clock::now() < end) {
        for (const auto &d : V4l2Device::enumerate())
            if (d.vid == vid && d.pid == pid)
                return d.node;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return {};
}

} // namespace lensscan
