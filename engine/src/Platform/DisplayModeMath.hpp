#pragma once
// Pure helpers for the own backends' display lists (no X11 or Win32 header needed, so tests run anywhere).
// Written from the X Resize and Rotate extension protocol (randrproto.txt: a mode line's dot clock, total width and
// total height, and the DoubleScan / Interlace flags) and the engine's DisplayMode type.
#include "okay/Platform/Platform.hpp"
#include <cstdint>
#include <vector>

namespace okay::platform::modemath {

/// Refresh rate in whole Hz of a mode line: pixels per second over pixels per frame. A double-scan mode draws each
/// line twice (half the frames); an interlaced mode draws half the lines per field (twice the fields). 0 when the
/// totals are 0.
inline int RefreshFromModeLine(uint64_t dotClockHz, uint32_t hTotal, uint32_t vTotal, bool doubleScan, bool interlace) {
    double v = (double)vTotal;
    if (doubleScan) v *= 2.0;
    if (interlace) v /= 2.0;
    const double frame = (double)hTotal * v;
    if (frame <= 0.0 || dotClockHz == 0) return 0;
    return (int)((double)dotClockHz / frame + 0.5);
}

/// `modes` with the current mode first and each (w, h, Hz) only once; modes with no size are dropped.
inline std::vector<DisplayMode> CurrentFirst(const DisplayMode& current, const std::vector<DisplayMode>& modes) {
    std::vector<DisplayMode> out;
    auto add = [&](const DisplayMode& m) {
        if (m.width <= 0 || m.height <= 0) return;
        for (const DisplayMode& o : out)
            if (o.width == m.width && o.height == m.height && o.refreshHz == m.refreshHz) return;
        out.push_back(m);
    };
    add(current);
    for (const DisplayMode& m : modes) add(m);
    return out;
}

} // namespace okay::platform::modemath
