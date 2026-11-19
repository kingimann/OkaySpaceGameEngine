#pragma once
// A parser for USB HID report descriptors and the reading of input reports, for game controllers that
// are not XInput devices (own Windows backend: Raw Input; also usable for Linux hidraw). No operating
// system calls; tested on Linux by tests/test_platform_win32_pure.cpp with descriptors assembled byte by byte
// from the HID specification. Written from "Device Class Definition for HID 1.11" (short item format, global,
// local and main items) and the HID Usage Tables (Generic Desktop 0x01, Button 0x09).
//
// STATUS: the parser and the mapping are tested. The Windows side (OwnWin32Hid.cpp, Win32Hid.hpp) reuses NormalizeAxis and
// HatBits but not Parse: Windows gives no descriptor, only HidP_* answers. Compiles with MinGW, never run on a real pad.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace okay::platform::hid {

constexpr uint16_t kPageGenericDesktop = 0x01, kPageButton = 0x09;
constexpr uint16_t kUsageX = 0x30, kUsageY = 0x31, kUsageZ = 0x32, kUsageRx = 0x33, kUsageRy = 0x34, kUsageRz = 0x35,
                   kUsageSlider = 0x36, kUsageDial = 0x37, kUsageWheel = 0x38, kUsageHat = 0x39;

/// One field of an input report: `count` items of `bitSize` bits each starting at `bitOffset` (counted from the first
/// bit after the report id byte), all with the same usage page and logical range. `usages` has one entry per item.
struct Field {
    uint8_t reportId = 0;
    uint32_t bitOffset = 0, bitSize = 0, count = 0;
    uint16_t usagePage = 0;
    std::vector<uint16_t> usages;     // size == count (padded with 0 when the descriptor gave fewer)
    int32_t logicalMin = 0, logicalMax = 0;
    bool variable = false;            // false = array
    bool constant = false;            // padding
};

struct Descriptor {
    std::vector<Field> fields;
    bool usesReportIds = false;
    bool ok = true;                   // false when the bytes were malformed (fields found before the error are kept)
    uint32_t ReportBits(uint8_t id) const {
        uint32_t end = 0;
        for (const Field& f : fields) if (f.reportId == id) end = std::max<uint32_t>(end, f.bitOffset + f.bitSize * f.count);
        return end;
    }
};

inline int32_t SignExtend(uint32_t v, int bytes) {
    if (bytes == 1) return (int8_t)v;
    if (bytes == 2) return (int16_t)v;
    return (int32_t)v;
}

/// Parse a report descriptor. Limits: at most 4096 fields, nesting of push/pop up to 8, items with data of 0, 1, 2 or 4 bytes.
inline Descriptor Parse(const uint8_t* d, size_t n) {
    Descriptor out;
    struct Global { uint16_t page = 0; int32_t lmin = 0, lmax = 0; uint32_t size = 0, count = 0; uint8_t id = 0; };
    Global g;
    std::vector<Global> stack;
    std::vector<uint16_t> usages;             // local: explicit usages, in order
    bool haveRange = false;
    uint16_t umin = 0, umax = 0;
    uint32_t bitPos[256] = {0};               // next free bit per report id
    size_t i = 0;
    while (i < n) {
        const uint8_t pre = d[i++];
        if (pre == 0xFE) {                    // long item: skip it
            if (i + 2 > n) { out.ok = false; break; }
            const size_t len = d[i];
            i += 2 + len;
            if (i > n) { out.ok = false; break; }
            continue;
        }
        const int sz = pre & 3 ? (pre & 3) == 3 ? 4 : (pre & 3) : 0;
        const int type = (pre >> 2) & 3, tag = pre >> 4;
        if (i + (size_t)sz > n) { out.ok = false; break; }
        uint32_t u = 0;
        for (int k = 0; k < sz; ++k) u |= (uint32_t)d[i + k] << (8 * k);
        i += (size_t)sz;
        if (type == 1) {                      // global
            switch (tag) {
            case 0x0: g.page = (uint16_t)u; break;
            case 0x1: g.lmin = SignExtend(u, sz ? sz : 1); break;
            case 0x2: g.lmax = g.lmin >= 0 ? (int32_t)(u > 0x7FFFFFFFu ? 0x7FFFFFFFu : u) : SignExtend(u, sz ? sz : 1); break;   // unsigned range when min >= 0
            case 0x7: g.size = u; break;
            case 0x8: g.id = (uint8_t)u; out.usesReportIds = true; break;
            case 0x9: g.count = u; break;
            case 0xA: if (stack.size() < 8) stack.push_back(g); else out.ok = false; break;
            case 0xB: if (!stack.empty()) { g = stack.back(); stack.pop_back(); } else out.ok = false; break;
            default: break;                   // unit, exponent: not needed
            }
        } else if (type == 2) {               // local
            switch (tag) {
            case 0x0: if (usages.size() < 4096) usages.push_back((uint16_t)u); break;
            case 0x1: umin = (uint16_t)u; haveRange = true; break;
            case 0x2: umax = (uint16_t)u; haveRange = true; break;
            default: break;
            }
        } else if (type == 0) {               // main
            if (tag == 0x8) {                 // Input
                Field f;
                f.reportId = g.id; f.bitSize = g.size; f.count = g.count; f.usagePage = g.page;
                f.logicalMin = g.lmin; f.logicalMax = g.lmax;
                f.constant = (u & 1) != 0; f.variable = (u & 2) != 0;
                if (g.size > 32 || g.count > 4096 || (uint64_t)g.size * g.count > 65536 || out.fields.size() >= 4096) { out.ok = false; break; }
                std::vector<uint16_t> us = usages;
                if (us.empty() && haveRange && umax >= umin && (uint32_t)(umax - umin) < 4096) for (uint32_t k = umin; k <= umax; ++k) us.push_back((uint16_t)k);
                const uint16_t last = us.empty() ? 0 : us.back();   // the last usage applies to every item left over
                us.resize(g.count, last);
                f.usages = us;
                f.bitOffset = bitPos[g.id];
                bitPos[g.id] += g.size * g.count;
                out.fields.push_back(f);
            } else if (tag == 0x9 || tag == 0xB) {   // Output / Feature: no bits in the input report
            }
            if (tag == 0x8 || tag == 0x9 || tag == 0xB || tag == 0xA || tag == 0xC) { usages.clear(); haveRange = false; umin = umax = 0; }   // locals reset after every main item
        }
    }
    return out;
}

/// Read `bitSize` bits at `bitOffset` of `report` (little-endian bit order, as HID reports are); signed fields sign-extend.
inline int64_t ReadBits(const uint8_t* report, size_t len, uint32_t bitOffset, uint32_t bitSize, bool isSigned) {
    if (bitSize == 0 || bitSize > 32) return 0;
    uint64_t v = 0;
    for (uint32_t b = 0; b < bitSize; ++b) {
        const uint32_t pos = bitOffset + b;
        if ((size_t)(pos / 8) >= len) return 0;           // a report that is too short reads as 0
        if (report[pos / 8] & (1u << (pos % 8))) v |= (uint64_t)1 << b;
    }
    if (isSigned && (v >> (bitSize - 1)) & 1) return (int64_t)v - ((int64_t)1 << bitSize);
    return (int64_t)v;
}

/// What a game pad looks like once its descriptor is read: up to 8 axes, 32 buttons and 2 hats.
struct PadLayout {
    struct Axis { uint8_t reportId; uint32_t bitOffset, bitSize; int32_t lo, hi; bool isSigned; uint16_t usage; };
    struct Hat { uint8_t reportId; uint32_t bitOffset, bitSize; int32_t lo, hi; };
    struct Button { uint8_t reportId; uint32_t bitOffset; };
    std::vector<Axis> axes;       // X, Y, Z, Rx, Ry, Rz, sliders in usage order of appearance
    std::vector<Hat> hats;
    std::vector<Button> buttons;  // in order of appearance (button 1 first)
    bool Usable() const { return axes.size() >= 2 || !buttons.empty(); }
};

inline PadLayout LayoutFromFields(const Descriptor& d) {
    PadLayout l;
    for (const Field& f : d.fields) {
        if (f.constant) continue;
        for (uint32_t k = 0; k < f.count; ++k) {
            const uint16_t u = k < f.usages.size() ? f.usages[k] : 0;
            const uint32_t off = f.bitOffset + k * f.bitSize;
            if (f.usagePage == kPageButton && f.variable && f.bitSize == 1) { if (l.buttons.size() < 32) l.buttons.push_back({f.reportId, off}); }
            else if (f.usagePage == kPageGenericDesktop && f.variable && u == kUsageHat) { if (l.hats.size() < 2) l.hats.push_back({f.reportId, off, f.bitSize, f.logicalMin, f.logicalMax}); }
            else if (f.usagePage == kPageGenericDesktop && f.variable && ((u >= kUsageX && u <= kUsageSlider) || u == kUsageDial || u == kUsageWheel)) {
                if (l.axes.size() < 8) l.axes.push_back({f.reportId, off, f.bitSize, f.logicalMin, f.logicalMax, f.logicalMin < 0, u});
            }
        }
    }
    return l;
}

/// A reading of one axis scaled to -32768..32767 (centre at the middle of the logical range). A degenerate range reads 0.
inline int16_t NormalizeAxis(int64_t v, int32_t lo, int32_t hi) {
    if (hi <= lo) return 0;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    const double t = (double)(v - lo) / (double)((int64_t)hi - lo);   // 0..1
    long r = (long)(t * 65535.0 + 0.5) - 32768;
    if (r < -32768) r = -32768;
    if (r > 32767) r = 32767;
    return (int16_t)r;
}

/// Hat switch value -> direction bits (1 up, 2 right, 4 down, 8 left), the numbering of `Gamepad::RawHat`.
/// A hat has 8 positions starting at "up" and going clockwise (value lo = up); a value outside lo..lo+7 is centred (0).
inline uint8_t HatBits(int64_t v, int32_t lo, int32_t hi) {
    static const uint8_t k[8] = {1, 1 | 2, 2, 2 | 4, 4, 4 | 8, 8, 8 | 1};
    const int64_t n = (int64_t)hi - lo + 1;          // 8 positions, or 4 when the pad only reports up, right, down, left
    if (v < lo || v > hi || (n != 8 && n != 4)) return 0;
    const int64_t idx = v - lo;
    return n == 8 ? k[idx] : k[idx * 2];
}

} // namespace okay::platform::hid
