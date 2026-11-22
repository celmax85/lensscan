// SPDX-License-Identifier: GPL-3.0-or-later
#include "SonixAsic.h"

#include "V4l2Device.h"

#include <linux/usb/video.h>

namespace lensscan {

std::array<uint8_t, 4> sonixReadRequest(uint16_t addr)
{
    return {uint8_t(addr & 0xff), uint8_t(addr >> 8), 0x00, 0xff};
}

std::array<uint8_t, 4> sonixWriteRequest(uint16_t addr, uint8_t value)
{
    return {uint8_t(addr & 0xff), uint8_t(addr >> 8), value, 0x00};
}

std::optional<uint8_t> SonixAsic::read(uint16_t addr) const
{
    auto req = sonixReadRequest(addr);
    if (!m_dev.xuQuery(m_unit, kSonixAsicSelector, UVC_SET_CUR, req.data(), 4))
        return std::nullopt;
    std::array<uint8_t, 4> resp{};
    if (!m_dev.xuQuery(m_unit, kSonixAsicSelector, UVC_GET_CUR, resp.data(), 4))
        return std::nullopt;
    return resp[2];
}

bool SonixAsic::write(uint16_t addr, uint8_t value) const
{
    auto req = sonixWriteRequest(addr, value);
    return m_dev.xuQuery(m_unit, kSonixAsicSelector, UVC_SET_CUR, req.data(), 4);
}

} // namespace lensscan
