// SPDX-License-Identifier: GPL-3.0-or-later
#include "UvcDescriptors.h"

#include <cstdio>
#include <cctype>

namespace lensscan {

namespace {
Guid mustParse(const char *s)
{
    Guid g{};
    guidFromString(s, g);
    return g;
}
} // namespace

const Guid kSonixXuGuid = mustParse("{28f03370-6311-4a2e-ba2c-6890eb334016}");
const Guid kCzurXu2Guid = mustParse("{bd5321b4-d635-ca45-b203-4e0149b301bc}");
const Guid kCzurInstructGuid = mustParse("{63610682-5070-49ab-b8cc-b3855e8d221d}");

std::string guidToString(const Guid &g)
{
    char buf[40];
    std::snprintf(buf, sizeof buf,
                  "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                  g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
                  g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return buf;
}

bool guidFromString(const std::string &in, Guid &out)
{
    std::string hex;
    for (char c : in) {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            hex.push_back(c);
        else if (c != '{' && c != '}' && c != '-')
            return false;
    }
    if (hex.size() != 32)
        return false;
    uint8_t b[16];
    for (int i = 0; i < 16; ++i)
        b[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
    // Text order -> wire order: first three groups are little-endian.
    const int order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    for (int i = 0; i < 16; ++i)
        out[i] = b[order[i]];
    return true;
}

std::vector<ExtensionUnit> parseExtensionUnits(const std::vector<uint8_t> &raw)
{
    constexpr uint8_t kInterface = 0x04;
    constexpr uint8_t kCsInterface = 0x24;
    constexpr uint8_t kVcExtensionUnit = 0x06;
    constexpr uint8_t kVideoClass = 0x0e;
    constexpr uint8_t kVideoControl = 0x01;

    std::vector<ExtensionUnit> units;
    bool inVideoControl = false;
    size_t pos = 0;
    while (pos + 2 <= raw.size()) {
        const uint8_t len = raw[pos];
        if (len < 2 || pos + len > raw.size())
            break;
        const uint8_t type = raw[pos + 1];
        if (type == kInterface && len >= 9) {
            inVideoControl = raw[pos + 5] == kVideoClass && raw[pos + 6] == kVideoControl;
        } else if (inVideoControl && type == kCsInterface && len >= 24 &&
                   raw[pos + 2] == kVcExtensionUnit) {
            ExtensionUnit xu;
            xu.unitId = raw[pos + 3];
            for (int i = 0; i < 16; ++i)
                xu.guid[i] = raw[pos + 4 + i];
            xu.numControls = raw[pos + 20];
            const uint8_t nrInPins = raw[pos + 21];
            const size_t ctrlSizeAt = pos + 22 + nrInPins;
            if (ctrlSizeAt < pos + len) {
                const uint8_t ctrlSize = raw[ctrlSizeAt];
                for (size_t i = 0; i < ctrlSize && ctrlSizeAt + 1 + i < pos + len; ++i)
                    xu.bmControls.push_back(raw[ctrlSizeAt + 1 + i]);
            }
            units.push_back(std::move(xu));
        }
        pos += len;
    }
    return units;
}

} // namespace lensscan
