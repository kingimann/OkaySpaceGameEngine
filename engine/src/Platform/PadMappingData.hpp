#pragma once
// Our own small game-controller mapping data for Linux evdev pads, and the step that turns what evdev
// reports into the raw numbering PadMapping.hpp works on. Pure logic: no device, no OS.
//
// Raw numbering we define (the Linux joystick API documents the same idea: numbers follow the codes):
//  * buttons: the key codes the device has from BTN_MISC (0x100) upward, ascending, numbered from 0.
//  * axes: the absolute axis codes the device has, ascending, numbered from 0, EXCLUDING the hat codes
//    ABS_HAT0X..ABS_HAT3Y (0x10..0x17).
//  * hats: hat n is the pair ABS_HAT<n>X / ABS_HAT<n>Y. Bits: 1 up (Y < 0), 2 right (X > 0), 4 down, 8 left.
//
// The table below was written by us from the public USB vendor / product numbers and the HID report order
// of each pad (sticks X Y, then the next axes, hat for the d-pad). It is NOT taken from any other
// controller database. UNVERIFIED on real hardware: no pad of these models was available when it was written.
// A pad that is not in the table (or has no entry that matches) keeps using the fixed layout in
// OwnLinuxEvdev.cpp, which is right for every driver that reports the standard BTN_SOUTH.. codes.
//
// An entry GUID with bus 0 and version 0 (see MakeLinuxGuid) matches that vendor/product on any bus/version.
#include "PadMapping.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay::platform::pad {

constexpr int kEvdevBtnMisc = 0x100;
constexpr int kEvdevAbsHat0X = 0x10;
constexpr int kEvdevAbsHat3Y = 0x17;
constexpr int kEvdevAbsSlots = 64;
constexpr int kEvdevBtnSlots = 512;

/// What a device reports now, in evdev terms. Pointers are borrowed for the call.
struct EvdevView {
    const std::vector<uint16_t>* keys = nullptr;   // present key codes >= BTN_MISC, ascending
    const bool* btn = nullptr;                     // kEvdevBtnSlots values, index = code - BTN_MISC
    const int* abs = nullptr;                      // kEvdevAbsSlots values, index = ABS_* code
    const bool* absPresent = nullptr;
    const int* absMin = nullptr;
    const int* absMax = nullptr;
};

/// evdev state -> the raw numbering above. Axes are scaled to -32768..32767 from their own range.
inline RawState BuildEvdevRaw(const EvdevView& v) {
    RawState r;
    if (v.keys && v.btn)
        for (uint16_t code : *v.keys)
            if (code >= kEvdevBtnMisc && code < kEvdevBtnMisc + kEvdevBtnSlots) r.buttons.push_back(v.btn[code - kEvdevBtnMisc] ? 1 : 0);
    if (v.abs && v.absPresent && v.absMin && v.absMax) {
        for (int c = 0; c < kEvdevAbsSlots; c++) {
            if (!v.absPresent[c] || (c >= kEvdevAbsHat0X && c <= kEvdevAbsHat3Y)) continue;
            const int lo = v.absMin[c], hi = v.absMax[c];
            if (hi == lo) { r.axes.push_back(0); continue; }
            long long x = ((long long)(v.abs[c] - lo) * 65535ll) / (hi - lo) - 32768ll;
            r.axes.push_back((int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : x));
        }
        for (int n = 0; n < 4; n++) {
            const int hx = v.absPresent[kEvdevAbsHat0X + 2 * n] ? v.abs[kEvdevAbsHat0X + 2 * n] : 0;
            const int hy = v.absPresent[kEvdevAbsHat0X + 2 * n + 1] ? v.abs[kEvdevAbsHat0X + 2 * n + 1] : 0;
            r.hats.push_back((uint8_t)((hy < 0 ? 1 : 0) | (hx > 0 ? 2 : 0) | (hy > 0 ? 4 : 0) | (hx < 0 ? 8 : 0)));
        }
    }
    return r;
}

namespace data {
// Layout of a pad driven by the xpad driver (Xbox 360 / Xbox One / Series): keys SOUTH EAST NORTH WEST TL TR
// SELECT START MODE THUMBL THUMBR are buttons 0..10; sticks X Y and RX RY, triggers Z and RZ.
inline constexpr const char* kXpadBody =
    "a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,rightshoulder:b5,back:b6,start:b7,guide:b8,leftstick:b9,rightstick:b10,"
    "leftx:a0,lefty:a1,lefttrigger:a2,rightx:a3,righty:a4,righttrigger:a5,"
    "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,platform:Linux,";
// DualSense (hid-playstation): keys SOUTH EAST NORTH WEST TL TR TL2 TR2 SELECT START MODE THUMBL THUMBR are
// buttons 0..12 (the L2 / R2 buttons are ignored, the analog triggers are used); sticks X Y and RX RY, triggers Z RZ.
inline constexpr const char* kDualSenseBody =
    "a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,rightshoulder:b5,back:b8,start:b9,guide:b10,leftstick:b11,rightstick:b12,"
    "leftx:a0,lefty:a1,lefttrigger:a2,rightx:a3,righty:a4,righttrigger:a5,"
    "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,platform:Linux,";
// DualShock 4 (hid-sony): same buttons as the DualSense; the HID report order puts the right stick on Z and RZ
// and the analog triggers on RX and RY, so axes are numbered X0 Y1 Z2 RX3 RY4 RZ5.
inline constexpr const char* kDualShock4Body =
    "a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,rightshoulder:b5,back:b8,start:b9,guide:b10,leftstick:b11,rightstick:b12,"
    "leftx:a0,lefty:a1,rightx:a2,righty:a5,lefttrigger:a3,righttrigger:a4,"
    "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,platform:Linux,";

struct Entry { uint16_t vendor, product; const char* name; const char* body; };
inline constexpr Entry kEntries[] = {
    {0x045e, 0x028e, "Xbox 360 Controller", kXpadBody},
    {0x045e, 0x028f, "Xbox 360 Controller (play and charge)", kXpadBody},
    {0x045e, 0x0719, "Xbox 360 Wireless Receiver", kXpadBody},
    {0x045e, 0x02d1, "Xbox One Controller", kXpadBody},
    {0x045e, 0x02dd, "Xbox One Controller", kXpadBody},
    {0x045e, 0x02e3, "Xbox One Elite Controller", kXpadBody},
    {0x045e, 0x02ea, "Xbox One S Controller", kXpadBody},
    {0x045e, 0x0b12, "Xbox Series Controller", kXpadBody},
    {0x054c, 0x05c4, "DualShock 4", kDualShock4Body},
    {0x054c, 0x09cc, "DualShock 4 (v2)", kDualShock4Body},
    {0x054c, 0x0ce6, "DualSense", kDualSenseBody},
    {0x054c, 0x0df2, "DualSense Edge", kDualSenseBody},
};
} // namespace data

/// The built-in table as database text (one line per pad), GUIDs with bus 0 and version 0 (match any).
inline std::string BuiltinMappingText() {
    std::string t;
    for (const data::Entry& e : data::kEntries) {
        t += GuidToString(MakeLinuxGuid(0, e.vendor, e.product, 0));
        t += ','; t += e.name; t += ','; t += e.body; t += '\n';
    }
    return t;
}

/// Find for a pad that reported (bus, vendor, product, version): the exact GUID first, then the same
/// device ignoring the name checksum, then the vendor/product entry with bus 0 and version 0.
inline const Mapping* FindForEvdev(const MappingDb& db, uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version,
                                   const std::string& platform = "Linux") {
    if (const Mapping* m = db.Find(MakeLinuxGuid(bus, vendor, product, version), platform)) return m;
    return db.Find(MakeLinuxGuid(0, vendor, product, 0), platform);
}

} // namespace okay::platform::pad
