// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceDb.h"

namespace lensscan {

namespace {
constexpr ModelInfo kModels[] = {
    {0x1e4f, 0x1227, "CZUR Lens 1200 Pro (E012A)", 3264, 2448},
};
} // namespace

std::optional<ModelInfo> lookupModel(uint16_t vid, uint16_t pid)
{
    for (const auto &m : kModels)
        if (m.vid == vid && m.pid == pid)
            return m;
    return std::nullopt;
}

bool isCzurDevice(const DeviceInfo &d)
{
    return d.vid == kCzurVendorId || lookupModel(d.vid, d.pid).has_value();
}

std::optional<uint8_t> sonixUnitId(const DeviceInfo &d)
{
    for (const auto &xu : d.extensionUnits)
        if (xu.guid == kSonixXuGuid)
            return xu.unitId;
    return std::nullopt;
}

} // namespace lensscan
