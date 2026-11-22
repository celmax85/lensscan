// SPDX-License-Identifier: GPL-3.0-or-later
// Port-level USB reset (USBDEVFS_RESET). Recovers the CZUR bridge when it starts
// refusing SET_INTERFACE (VIDIOC_STREAMON -> EPROTO). Needs write access to
// /dev/bus/usb/BBB/DDD, granted by packaging/udev/70-lensscan.rules.
#pragma once

#include <cstdint>
#include <string>

namespace lensscan {

// usbSysPath: /sys/bus/usb/devices/1-4. Returns false and fills error on failure.
bool usbResetDevice(const std::string &usbSysPath, std::string &error);

// After a reset the V4L2 node is recreated; waits until a capture node for
// vid:pid exists again and returns it (empty on timeout).
std::string waitForVideoNode(uint16_t vid, uint16_t pid, int timeoutMs);

} // namespace lensscan
