// SPDX-License-Identifier: GPL-3.0-or-later
// Automatic scan on page turn: watches the live preview and fires once the scene has
// moved (a hand turning the page), then stayed still for a moment, and differs from
// what was scanned last.
#pragma once

#include <QImage>

#include <cstdint>
#include <vector>

namespace lensscan {

class AutoScanTrigger {
public:
    struct Params {
        // Differences are the percentage of thumbnail cells whose grey level changed by
        // more than cellThreshold, which ignores sensor noise and slow light changes.
        int cellThreshold = 12;
        double moveThreshold = 1.5;   // % of cells changed between frames that means "moving"
        double stillThreshold = 0.5;  // below this the scene is still
        int stillMs = 900;            // how long it must stay still before scanning
        double changeThreshold = 4.0; // % changed since the last scan that means "new page"
    };

    AutoScanTrigger();
    explicit AutoScanTrigger(const Params &p);

    // Feed every preview frame with its time in milliseconds. Returns true when a scan
    // should be taken now.
    bool feed(const QImage &frame, int64_t timeMs);
    // Call after a scan (manual or automatic): the current view becomes the reference.
    void scanned();
    void reset();

    double lastMotion() const { return m_motion; }

private:
    static std::vector<uint8_t> signature(const QImage &frame);
    double difference(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) const;

    Params m_p;
    std::vector<uint8_t> m_prev, m_reference;
    bool m_moved = false;
    int64_t m_stillSince = -1;
    double m_motion = 0;
};

} // namespace lensscan
