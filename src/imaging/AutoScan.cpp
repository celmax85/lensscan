// SPDX-License-Identifier: GPL-3.0-or-later
#include "AutoScan.h"

#include <cmath>
#include <cstdlib>

namespace lensscan {

namespace {
constexpr int kSigW = 64, kSigH = 48;
}

AutoScanTrigger::AutoScanTrigger() = default;

AutoScanTrigger::AutoScanTrigger(const Params &p) : m_p(p) {}

std::vector<uint8_t> AutoScanTrigger::signature(const QImage &frame)
{
    // A tiny blurred grey thumbnail: robust to sensor noise and MJPEG artefacts.
    const QImage g = frame.scaled(kSigW, kSigH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_Grayscale8);
    std::vector<uint8_t> s(size_t(kSigW * kSigH));
    for (int y = 0; y < kSigH; ++y)
        for (int x = 0; x < kSigW; ++x)
            s[size_t(y * kSigW + x)] = g.constScanLine(y)[x];
    return s;
}

double AutoScanTrigger::difference(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) const
{
    if (a.size() != b.size() || a.empty())
        return 100;
    long changed = 0;
    for (size_t i = 0; i < a.size(); ++i)
        changed += std::abs(int(a[i]) - int(b[i])) > m_p.cellThreshold;
    return 100.0 * double(changed) / double(a.size());
}

bool AutoScanTrigger::feed(const QImage &frame, int64_t timeMs)
{
    if (frame.isNull())
        return false;
    std::vector<uint8_t> sig = signature(frame);
    if (m_reference.empty())
        m_reference = sig; // what is under the camera when auto scan is switched on
    m_motion = m_prev.empty() ? 0 : difference(sig, m_prev);
    m_prev = sig;

    if (m_motion > m_p.moveThreshold) {
        m_moved = true;
        m_stillSince = -1;
        return false;
    }
    if (m_motion > m_p.stillThreshold) {
        m_stillSince = -1;
        return false;
    }
    if (m_stillSince < 0)
        m_stillSince = timeMs;
    if (!m_moved || timeMs - m_stillSince < m_p.stillMs)
        return false;
    // Still long enough after a movement: scan if the page changed.
    m_moved = false;
    return difference(sig, m_reference) > m_p.changeThreshold;
}

void AutoScanTrigger::scanned()
{
    if (!m_prev.empty())
        m_reference = m_prev;
    m_moved = false;
    m_stillSince = -1;
}

void AutoScanTrigger::reset()
{
    m_prev.clear();
    m_reference.clear();
    m_moved = false;
    m_stillSince = -1;
}

} // namespace lensscan
