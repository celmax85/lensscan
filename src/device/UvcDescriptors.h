// SPDX-License-Identifier: GPL-3.0-or-later
// Parsing of raw USB configuration descriptors to locate UVC extension units.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lensscan {

using Guid = std::array<uint8_t, 16>;

struct ExtensionUnit {
    uint8_t unitId = 0;
    Guid guid{};
    uint8_t numControls = 0;
    std::vector<uint8_t> bmControls;
};

// Formats a GUID as {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} using the USB/Windows
// mixed-endian layout (first three groups little-endian).
std::string guidToString(const Guid &g);
// Parses "{28f03370-6311-4a2e-ba2c-6890eb334016}" (braces optional). Returns false on error.
bool guidFromString(const std::string &s, Guid &out);

// Walks a raw descriptor blob (as exposed by /sys/bus/usb/devices/<dev>/descriptors)
// and returns all VideoControl extension units found.
std::vector<ExtensionUnit> parseExtensionUnits(const std::vector<uint8_t> &raw);

// Well-known GUIDs observed in the CZUR Windows driver stack (LibCDL.dll).
extern const Guid kSonixXuGuid;      // {28f03370-6311-4a2e-ba2c-6890eb334016}
extern const Guid kCzurXu2Guid;      // {bd5321b4-d635-ca45-b203-4e0149b301bc}
extern const Guid kCzurInstructGuid; // {63610682-5070-49ab-b8cc-b3855e8d221d}

} // namespace lensscan
