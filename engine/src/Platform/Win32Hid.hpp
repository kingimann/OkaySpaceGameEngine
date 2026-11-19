#pragma once
// Pure part of the own Windows backend's non-XInput game controller support (Raw Input + HidP_*). No Windows
// headers: tested on Linux by tests/test_platform_win32_pure.cpp.
//
// How it fits together. Windows does not hand an application the raw HID report descriptor, it hands over an
// opaque "preparsed data" blob and the HidP_* functions that read it. So OwnWin32Hid.cpp asks HidP_GetCaps,
// HidP_GetButtonCaps and HidP_GetValueCaps, copies the answers into the plain structs below (ButtonCap, ValueCap)
// and this file turns them into a UsageLayout: which usages are axes, hats and buttons, in order. Each WM_INPUT
// report is then read with HidP_GetUsages / HidP_GetUsageValue, and Evaluate() turns the readings into a PadState,
// using the scaling and hat maths of HidParser.hpp (NormalizeAxis, HatBits) so XInput-less pads read the same way
// as the Linux hidraw / evdev paths. HidParser's descriptor parser itself is not on this path (no descriptor is
// available on Windows); it stays for hidraw.
//
// Written from the Microsoft documentation of HIDP_BUTTON_CAPS / HIDP_VALUE_CAPS / HidP_GetUsageValue, the
// "Device Class Definition for HID 1.11" and the HID Usage Tables. The "IG_" rule is Microsoft's documented way to
// tell XInput devices from the others in a Raw Input device name.
#include "HidParser.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay::platform::win32hid {

using hid::kPageButton;
using hid::kPageGenericDesktop;

/// HIDP_BUTTON_CAPS, reduced to what the layout needs.
struct ButtonCap { uint8_t reportId; uint16_t page; bool isRange; uint16_t usageMin, usageMax; };
/// HIDP_VALUE_CAPS, reduced. `reportCount` > 1 with a single usage is an array of values (not supported, skipped).
struct ValueCap { uint8_t reportId; uint16_t page; bool isRange; uint16_t usageMin, usageMax; uint16_t bitSize, reportCount; int32_t lmin, lmax; };

/// True when a Raw Input device name (RIDI_DEVICENAME) is an XInput device: the name contains "IG_" starting a token
/// (after '#' or '&'), compared without regard to case. Those are read through XInput, not here.
inline bool IsXInputDevicePath(const std::string& path) {
    for (size_t i = 1; i + 3 <= path.size(); ++i) {
        const char a = path[i], b = path[i + 1], c = path[i + 2];
        if ((a == 'I' || a == 'i') && (b == 'G' || b == 'g') && c == '_' && (path[i - 1] == '#' || path[i - 1] == '&')) return true;
    }
    return false;
}

inline int HexDigit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/// Vendor and product id from "VID_xxxx&PID_xxxx" (exactly four hex digits each, any case) in a device name.
inline bool ParseVidPid(const std::string& path, uint16_t& vid, uint16_t& pid) {
    auto find4 = [&](const char* tag, uint16_t& out) {
        for (size_t i = 0; i + 8 <= path.size(); ++i) {
            const char t0 = path[i] | 0x20, t1 = path[i + 1] | 0x20, t2 = path[i + 2] | 0x20;
            if (t0 != tag[0] || t1 != tag[1] || t2 != tag[2] || path[i + 3] != '_') continue;
            uint32_t v = 0;
            for (int k = 0; k < 4; ++k) { const int h = HexDigit(path[i + 4 + (size_t)k]); if (h < 0) return false; v = v * 16 + (uint32_t)h; }
            if (i + 8 < path.size() && HexDigit(path[i + 8]) >= 0) return false;   // five digits: not an id
            out = (uint16_t)v;
            return true;
        }
        return false;
    };
    uint16_t v = 0, p = 0;
    if (!find4("vid", v) || !find4("pid", p)) return false;
    vid = v; pid = p;
    return true;
}

/// HidP_GetUsageValue returns an unsigned number; a field with a negative logical minimum is two's complement in `bitSize` bits.
inline int32_t SignedFromRaw(uint32_t raw, int bitSize, bool isSigned) {
    if (!isSigned || bitSize <= 0 || bitSize >= 32) return (int32_t)raw;
    if ((raw >> (bitSize - 1)) & 1u) return (int32_t)((int64_t)raw - ((int64_t)1 << bitSize));
    return (int32_t)raw;
}

struct UsageLayout {
    struct Axis { uint8_t reportId; uint16_t page, usage; uint16_t bitSize; int32_t lo, hi; bool isSigned; };
    struct Hat { uint8_t reportId; uint16_t page, usage; uint16_t bitSize; int32_t lo, hi; };
    std::vector<Axis> axes;          // at most 8, in the order of the caps
    std::vector<Hat> hats;           // at most 2
    std::vector<uint16_t> buttons;   // button page usages (1 = first button), at most 32, in order; index = RawButton index
    std::vector<uint8_t> buttonReportIds;   // parallel to `buttons`
    bool Usable() const { return axes.size() >= 2 || !buttons.empty(); }
};

/// Some drivers describe an unsigned 0..255 field with a logical maximum of -1 (the byte 0xFF read as signed): a
/// maximum below a non-negative minimum is taken as the largest unsigned value of `bitSize` bits.
inline int32_t FixLogicalMax(int32_t lmin, int32_t lmax, int bitSize) {
    if (lmin >= 0 && lmax < lmin && bitSize > 0 && bitSize < 32) return (int32_t)(((int64_t)1 << bitSize) - 1);
    return lmax;
}

inline UsageLayout BuildLayout(const std::vector<ButtonCap>& buttons, const std::vector<ValueCap>& values) {
    UsageLayout l;
    for (const ButtonCap& b : buttons) {
        if (b.page != kPageButton) continue;
        const uint32_t lo = b.usageMin, hi = b.isRange ? b.usageMax : b.usageMin;
        for (uint32_t u = lo; u <= hi && l.buttons.size() < 32; ++u) { l.buttons.push_back((uint16_t)u); l.buttonReportIds.push_back(b.reportId); }
    }
    for (const ValueCap& v : values) {
        if (v.page != kPageGenericDesktop) continue;
        if (!v.isRange && v.reportCount > 1) continue;                     // array of values for one usage
        const uint32_t lo = v.usageMin, hi = v.isRange ? v.usageMax : v.usageMin;
        const int32_t mx = FixLogicalMax(v.lmin, v.lmax, v.bitSize);
        for (uint32_t u = lo; u <= hi && u - lo < 256; ++u) {
            if (u == hid::kUsageHat) { if (l.hats.size() < 2) l.hats.push_back({v.reportId, v.page, (uint16_t)u, v.bitSize, v.lmin, mx}); }
            else if ((u >= hid::kUsageX && u <= hid::kUsageSlider) || u == hid::kUsageDial || u == hid::kUsageWheel) {
                if (l.axes.size() < 8) l.axes.push_back({v.reportId, v.page, (uint16_t)u, v.bitSize, v.lmin, mx, v.lmin < 0});
            }
        }
    }
    return l;
}

struct PadState {
    int16_t axes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t hats[2] = {0, 0};
    uint32_t buttons = 0;            // bit i = layout.buttons[i]
};

/// `pressed`: the button usages HidP_GetUsages listed for the report. `rawAxes` / `rawHats`: HidP_GetUsageValue results
/// in layout order (missing entries read as 0). Signed fields are sign-extended here.
inline PadState Evaluate(const UsageLayout& l, const std::vector<uint16_t>& pressed, const std::vector<uint32_t>& rawAxes, const std::vector<uint32_t>& rawHats) {
    PadState s;
    for (uint16_t u : pressed)
        for (size_t i = 0; i < l.buttons.size(); ++i) if (l.buttons[i] == u) s.buttons |= 1u << i;
    for (size_t i = 0; i < l.axes.size(); ++i) {
        const UsageLayout::Axis& a = l.axes[i];
        const int32_t v = SignedFromRaw(i < rawAxes.size() ? rawAxes[i] : 0, a.bitSize, a.isSigned);
        s.axes[i] = hid::NormalizeAxis(v, a.lo, a.hi);
    }
    for (size_t i = 0; i < l.hats.size(); ++i) {
        const UsageLayout::Hat& h = l.hats[i];
        s.hats[i] = hid::HatBits(i < rawHats.size() ? rawHats[i] : 0, h.lo, h.hi);
    }
    return s;
}

} // namespace okay::platform::win32hid
