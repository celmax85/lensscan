// SPDX-License-Identifier: GPL-3.0-or-later
// Sonix UVC bridge register access, as used by the vendor's LibCDL.dll:
//   XU {28f03370-6311-4a2e-ba2c-6890eb334016}, selector 1, 4-byte payload.
//   read : SET_CUR [addrLo addrHi 00 FF] then GET_CUR -> byte[2] is the value
//   write: SET_CUR [addrLo addrHi val 00]
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace lensscan {

class V4l2Device;

constexpr uint8_t kSonixAsicSelector = 1;

std::array<uint8_t, 4> sonixReadRequest(uint16_t addr);
std::array<uint8_t, 4> sonixWriteRequest(uint16_t addr, uint8_t value);

class SonixAsic {
public:
    SonixAsic(const V4l2Device &dev, uint8_t unitId) : m_dev(dev), m_unit(unitId) {}
    std::optional<uint8_t> read(uint16_t addr) const;
    bool write(uint16_t addr, uint8_t value) const;

private:
    const V4l2Device &m_dev;
    uint8_t m_unit;
};

} // namespace lensscan
