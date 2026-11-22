// SPDX-License-Identifier: GPL-3.0-or-later
// Known CZUR-compatible cameras and their quirks.
#pragma once

#include "V4l2Device.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lensscan {

struct ModelInfo {
    uint16_t vid;
    uint16_t pid;
    const char *name;
    uint32_t captureWidth;  // preferred full-resolution capture size
    uint32_t captureHeight;
};

constexpr uint16_t kCzurVendorId = 0x1e4f;

// Returns the table entry for vid:pid, if known.
std::optional<ModelInfo> lookupModel(uint16_t vid, uint16_t pid);
// True for devices we should offer by default (known model or CZUR vendor id).
bool isCzurDevice(const DeviceInfo &d);
// Unit id of the Sonix XU on that device, if present.
std::optional<uint8_t> sonixUnitId(const DeviceInfo &d);

} // namespace lensscan
