// Controller classifier. Written from public USB id lists (the vendor ids of
// Microsoft, Sony, Nintendo, Valve ...) and from how each maker names its pads;
// no other project's mapping text was used.
#include "okay/Input/GamepadInfo.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace okay {
namespace {

constexpr std::uint16_t kMicrosoft = 0x045E, kSony = 0x054C, kNintendo = 0x057E, kValve = 0x28DE,
                        kGoogle = 0x18D1, kAmazon = 0x1949, kNvidia = 0x0955, kLogitech = 0x046D;

std::string Lower(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) o += (char)std::tolower((unsigned char)c);
    return o;
}

struct IdRow { std::uint16_t vendor, product; ControllerFamily family; };
// Product ids that are public USB facts. A row is exact.
const IdRow kIds[] = {
    {kMicrosoft, 0x028E, ControllerFamily::Xbox360}, {kMicrosoft, 0x028F, ControllerFamily::Xbox360},
    {kMicrosoft, 0x0291, ControllerFamily::Xbox360}, {kMicrosoft, 0x02A1, ControllerFamily::Xbox360},
    {kMicrosoft, 0x0719, ControllerFamily::Xbox360},
    {kMicrosoft, 0x02D1, ControllerFamily::XboxOne}, {kMicrosoft, 0x02DD, ControllerFamily::XboxOne},
    {kMicrosoft, 0x02E3, ControllerFamily::XboxOne}, {kMicrosoft, 0x02EA, ControllerFamily::XboxOne},
    {kMicrosoft, 0x02FD, ControllerFamily::XboxOne}, {kMicrosoft, 0x02FF, ControllerFamily::XboxOne},
    {kMicrosoft, 0x0B00, ControllerFamily::XboxOne}, {kMicrosoft, 0x0B05, ControllerFamily::XboxOne},
    {kMicrosoft, 0x0B0A, ControllerFamily::XboxOne},
    {kMicrosoft, 0x0B12, ControllerFamily::XboxSeries}, {kMicrosoft, 0x0B13, ControllerFamily::XboxSeries},
    {kMicrosoft, 0x0B20, ControllerFamily::XboxSeries}, {kMicrosoft, 0x0B21, ControllerFamily::XboxSeries},
    {kSony, 0x0268, ControllerFamily::DualShock3},
    {kSony, 0x05C4, ControllerFamily::DualShock4}, {kSony, 0x09CC, ControllerFamily::DualShock4},
    {kSony, 0x0BA0, ControllerFamily::DualShock4},
    {kSony, 0x0CE6, ControllerFamily::DualSense}, {kSony, 0x0DF2, ControllerFamily::DualSenseEdge},
    {kNintendo, 0x2006, ControllerFamily::JoyConLeft}, {kNintendo, 0x2007, ControllerFamily::JoyConRight},
    {kNintendo, 0x2008, ControllerFamily::JoyConPair}, {kNintendo, 0x2009, ControllerFamily::SwitchPro},
    {kNintendo, 0x2069, ControllerFamily::SwitchPro},
    {kValve, 0x1102, ControllerFamily::SteamController}, {kValve, 0x1142, ControllerFamily::SteamController},
    {kValve, 0x1205, ControllerFamily::SteamDeck},
    {kGoogle, 0x9400, ControllerFamily::Stadia},
    {kAmazon, 0x0419, ControllerFamily::Luna},
    {kNvidia, 0x7210, ControllerFamily::Shield}, {kNvidia, 0x7214, ControllerFamily::Shield},
    {kLogitech, 0xC24F, ControllerFamily::Wheel}, {kLogitech, 0xC262, ControllerFamily::Wheel},
    {kLogitech, 0xC29B, ControllerFamily::Wheel}, {kLogitech, 0xC29A, ControllerFamily::Wheel},
    {kLogitech, 0xC215, ControllerFamily::FlightStick},
};

bool Has(const std::string& n, const char* w) { return n.find(w) != std::string::npos; }

ControllerFamily FromSdlType(int t, std::uint16_t vendor) {
    switch (t) {
        case 1: return vendor == kMicrosoft || vendor == 0 ? ControllerFamily::Xbox360 : ControllerFamily::GenericXInput;
        case 2: return vendor == kMicrosoft || vendor == 0 ? ControllerFamily::XboxOne : ControllerFamily::GenericXInput;
        case 3: return ControllerFamily::DualShock3;
        case 4: return ControllerFamily::DualShock4;
        case 5: return ControllerFamily::SwitchPro;
        case 7: return ControllerFamily::DualSense;
        case 8: return ControllerFamily::Luna;
        case 9: return ControllerFamily::Stadia;
        case 10: return ControllerFamily::Shield;
        case 11: return ControllerFamily::JoyConLeft;
        case 12: return ControllerFamily::JoyConRight;
        case 13: return ControllerFamily::JoyConPair;
        default: return ControllerFamily::Unknown;
    }
}

ControllerFamily FromName(const std::string& n, std::uint16_t vendor) {
    if (Has(n, "dualsense") && Has(n, "edge")) return ControllerFamily::DualSenseEdge;
    if (Has(n, "dualsense") || Has(n, "ps5") || Has(n, "playstation 5")) return ControllerFamily::DualSense;
    if (Has(n, "dualshock 4") || Has(n, "dualshock4") || Has(n, "ps4") || Has(n, "playstation 4")) return ControllerFamily::DualShock4;
    if (Has(n, "dualshock 3") || Has(n, "ps3") || Has(n, "playstation(r)3") || Has(n, "playstation 3")) return ControllerFamily::DualShock3;
    if (vendor == kSony && Has(n, "wireless controller")) return ControllerFamily::DualShock4;   // the DS4 reports exactly this
    if (Has(n, "steam deck")) return ControllerFamily::SteamDeck;
    if (Has(n, "steam controller")) return ControllerFamily::SteamController;
    if (Has(n, "joy-con") || Has(n, "joycon")) {
        if (Has(n, "(l)") || Has(n, " left")) return ControllerFamily::JoyConLeft;
        if (Has(n, "(r)") || Has(n, " right")) return ControllerFamily::JoyConRight;
        return ControllerFamily::JoyConPair;
    }
    if (Has(n, "pro controller") || Has(n, "switch pro")) return ControllerFamily::SwitchPro;
    if (Has(n, "stadia")) return ControllerFamily::Stadia;
    if (Has(n, "luna")) return ControllerFamily::Luna;
    if (Has(n, "shield")) return ControllerFamily::Shield;
    if (Has(n, "xbox series") || Has(n, "series x") || Has(n, "series s")) return ControllerFamily::XboxSeries;
    if (Has(n, "xbox one") || Has(n, "xbox elite") || Has(n, "xbox wireless") || Has(n, "xbox adaptive")) return ControllerFamily::XboxOne;
    if (Has(n, "xbox 360") || Has(n, "x360") || Has(n, "xbox360")) return ControllerFamily::Xbox360;
    if (Has(n, "xbox") || Has(n, "x-box")) return vendor == kMicrosoft ? ControllerFamily::XboxOne : ControllerFamily::GenericXInput;
    if (Has(n, "xinput")) return ControllerFamily::GenericXInput;
    if (Has(n, "wheel") || Has(n, "racing") || Has(n, "driving force") || Has(n, "g29") || Has(n, "g920") ||
        Has(n, "g923") || Has(n, "g27") || Has(n, "t300") || Has(n, "t150") || Has(n, "pedals")) return ControllerFamily::Wheel;
    if (Has(n, "hotas") || Has(n, "flight") || Has(n, "throttle") || Has(n, "x52") || Has(n, "x56") ||
        Has(n, "joystick") || Has(n, "extreme 3d") || Has(n, "t.16000")) return ControllerFamily::FlightStick;
    if (Has(n, "arcade") || Has(n, "fight stick") || Has(n, "fightstick")) return ControllerFamily::ArcadeStick;
    if (Has(n, "gamepad") || Has(n, "controller") || Has(n, "game pad")) return ControllerFamily::GenericDInput;
    return ControllerFamily::Unknown;
}

void FillCaps(GamepadInfo& g) {
    using F = ControllerFamily;
    switch (g.family) {
        case F::DualShock4: g.touchpad = g.gyro = g.lightbar = g.shareButton = true; break;
        case F::DualSense: g.touchpad = g.gyro = g.lightbar = g.playerLeds = g.adaptiveTriggers = g.hdHaptics = g.shareButton = g.muteButton = true; break;
        case F::DualSenseEdge: g.touchpad = g.gyro = g.lightbar = g.playerLeds = g.adaptiveTriggers = g.hdHaptics = g.shareButton = g.muteButton = g.paddles = true; break;
        case F::DualShock3: g.gyro = true; g.playerLeds = true; break;
        case F::XboxOne: g.triggerRumble = true; break;
        case F::XboxSeries: g.triggerRumble = true; g.shareButton = true; break;
        case F::SwitchPro: g.gyro = true; g.playerLeds = true; g.hdHaptics = true; g.shareButton = true; break;
        case F::JoyConLeft: case F::JoyConRight: case F::JoyConPair: g.gyro = true; g.playerLeds = true; g.hdHaptics = true; g.shareButton = true; break;
        case F::SteamDeck: g.gyro = true; g.touchpad = true; g.paddles = true; break;
        case F::SteamController: g.touchpad = true; g.paddles = true; break;
        case F::Stadia: g.gyro = true; g.shareButton = true; break;
        default: break;
    }
}

} // namespace

const char* FamilyName(ControllerFamily f) {
    switch (f) {
        case ControllerFamily::Xbox360: return "Xbox 360";
        case ControllerFamily::XboxOne: return "Xbox One";
        case ControllerFamily::XboxSeries: return "Xbox Series";
        case ControllerFamily::DualShock3: return "DualShock 3";
        case ControllerFamily::DualShock4: return "DualShock 4";
        case ControllerFamily::DualSense: return "DualSense";
        case ControllerFamily::DualSenseEdge: return "DualSense Edge";
        case ControllerFamily::SwitchPro: return "Switch Pro";
        case ControllerFamily::JoyConLeft: return "Joy-Con (L)";
        case ControllerFamily::JoyConRight: return "Joy-Con (R)";
        case ControllerFamily::JoyConPair: return "Joy-Con pair";
        case ControllerFamily::SteamController: return "Steam Controller";
        case ControllerFamily::SteamDeck: return "Steam Deck";
        case ControllerFamily::Stadia: return "Stadia";
        case ControllerFamily::Luna: return "Luna";
        case ControllerFamily::Shield: return "Shield";
        case ControllerFamily::GenericXInput: return "Generic XInput controller";
        case ControllerFamily::GenericDInput: return "Generic controller";
        case ControllerFamily::FlightStick: return "Flight stick";
        case ControllerFamily::Wheel: return "Racing wheel";
        case ControllerFamily::ArcadeStick: return "Arcade stick";
        default: return "Unknown controller";
    }
}

GlyphStyle GlyphStyleOf(ControllerFamily f) {
    switch (f) {
        case ControllerFamily::Xbox360: case ControllerFamily::XboxOne: case ControllerFamily::XboxSeries:
        case ControllerFamily::GenericXInput: case ControllerFamily::Stadia: case ControllerFamily::Luna:
        case ControllerFamily::Shield: case ControllerFamily::SteamController: case ControllerFamily::SteamDeck:
            return GlyphStyle::Xbox;
        case ControllerFamily::DualShock3: case ControllerFamily::DualShock4:
        case ControllerFamily::DualSense: case ControllerFamily::DualSenseEdge:
            return GlyphStyle::PlayStation;
        case ControllerFamily::SwitchPro: case ControllerFamily::JoyConLeft:
        case ControllerFamily::JoyConRight: case ControllerFamily::JoyConPair:
            return GlyphStyle::Nintendo;
        default: return GlyphStyle::Generic;
    }
}

Input::PadFamily CoarseFamily(ControllerFamily f) {
    switch (GlyphStyleOf(f)) {
        case GlyphStyle::PlayStation: return Input::PadFamily::PlayStation;
        case GlyphStyle::Nintendo: return Input::PadFamily::Switch;
        case GlyphStyle::Xbox: return Input::PadFamily::Xbox;
        default: return Input::PadFamily::Generic;
    }
}

GamepadInfo ClassifyGamepad(const GamepadIdentity& id) {
    GamepadInfo g;
    const std::string n = Lower(id.name);
    ControllerFamily f = ControllerFamily::Unknown;
    const char* by = "default";
    // 1. exact USB ids (the strongest hint: a Sony pad is a Sony pad whatever the name says)
    if (id.vendor || id.product)
        for (const IdRow& r : kIds)
            if (r.vendor == id.vendor && r.product == id.product) { f = r.family; by = "usb id"; break; }
    // 2. the host's own controller type
    if (f == ControllerFamily::Unknown && id.sdlType > 0) {
        f = FromSdlType(id.sdlType, id.vendor);
        if (f != ControllerFamily::Unknown) by = "sdl type";
    }
    // 3. the name
    if (f == ControllerFamily::Unknown) {
        f = FromName(n, id.vendor);
        if (f != ControllerFamily::Unknown) by = "name";
    }
    // 4. a maker we know, a model we do not: still that maker's layout
    if (f == ControllerFamily::Unknown) {
        if (id.vendor == kSony) { f = ControllerFamily::DualShock4; by = "usb id"; }
        else if (id.vendor == kMicrosoft) { f = ControllerFamily::XboxOne; by = "usb id"; }
        else if (id.vendor == kNintendo) { f = ControllerFamily::SwitchPro; by = "usb id"; }
        else if (id.vendor == kValve) { f = ControllerFamily::SteamController; by = "usb id"; }
    }
    // 5. the shape of the device
    if (f == ControllerFamily::Unknown && id.axes >= 4 && id.buttons >= 8) { f = ControllerFamily::GenericDInput; by = "shape"; }
    if (f == ControllerFamily::Unknown && id.axes >= 2 && id.buttons >= 1) { f = ControllerFamily::GenericDInput; by = "shape"; }
    // A device the SDL layer calls an Xbox pad but the maker is not Microsoft is a third-party XInput pad.
    g.family = f;
    g.style = GlyphStyleOf(f);
    g.familyName = FamilyName(f);
    g.matchedBy = by;
    g.recognised = f != ControllerFamily::Unknown && std::string(by) != "shape";
    FillCaps(g);
    return g;
}

std::string FormatUsbId(std::uint16_t v, std::uint16_t p) {
    if (!v && !p) return "none";
    char b[24];
    std::snprintf(b, sizeof b, "0x%04x:0x%04x", (unsigned)v, (unsigned)p);
    return b;
}

std::string PadButtonName(int b, ControllerFamily f) {
    if (b < 0 || b >= Input::PadButtonCount) return "?";
    if (b == Input::PadGuide) return PadExtraName(PadExtra::Guide, f);   // "PS", "Home", "Steam", "Guide" (the tables below end at RT)
    const GlyphStyle s = GlyphStyleOf(f);
    // PadButton order: A B X Y Back Start LB RB Up Down Left Right LS RS LT RT
    static const char* const kPs[Input::PadButtonCount] = {"Cross", "Circle", "Square", "Triangle", "Share", "Options", "L1", "R1",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "L3", "R3", "L2", "R2"};
    static const char* const kNin[Input::PadButtonCount] = {"B", "A", "Y", "X", "-", "+", "L", "R",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Left Stick", "Right Stick", "ZL", "ZR"};
    static const char* const kXb[Input::PadButtonCount] = {"A", "B", "X", "Y", "Back", "Start", "LB", "RB",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "LS", "RS", "LT", "RT"};
    if (s == GlyphStyle::PlayStation) {
        if (b == Input::PadBack) return f == ControllerFamily::DualSense || f == ControllerFamily::DualSenseEdge ? "Create"
                                      : f == ControllerFamily::DualShock3 ? "Select" : "Share";
        if (b == Input::PadStart && f == ControllerFamily::DualShock3) return "Start";
        return kPs[b];
    }
    if (s == GlyphStyle::Nintendo) return kNin[b];
    if (b == Input::PadBack || b == Input::PadStart) {
        const bool modern = f == ControllerFamily::XboxOne || f == ControllerFamily::XboxSeries ||
                            f == ControllerFamily::SteamDeck;
        if (modern) return b == Input::PadBack ? "View" : "Menu";
    }
    if (s == GlyphStyle::Xbox && (f == ControllerFamily::SteamDeck)) {
        static const char* const kDeck[Input::PadButtonCount] = {"A", "B", "X", "Y", "View", "Menu", "L1", "R1",
            "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "L3", "R3", "L2", "R2"};
        return kDeck[b];
    }
    return kXb[b];
}

std::string PadExtraName(PadExtra e, ControllerFamily f) {
    const GlyphStyle s = GlyphStyleOf(f);
    switch (e) {
        case PadExtra::Guide: return s == GlyphStyle::PlayStation ? "PS" : s == GlyphStyle::Nintendo ? "Home"
                                   : (f == ControllerFamily::SteamDeck || f == ControllerFamily::SteamController) ? "Steam" : "Guide";
        case PadExtra::Share:
            if (f == ControllerFamily::DualShock4) return "";   // Share is its Back button
            if (f == ControllerFamily::DualSense || f == ControllerFamily::DualSenseEdge) return "";   // Create is its Back button
            if (f == ControllerFamily::XboxSeries) return "Share";
            if (s == GlyphStyle::Nintendo) return "Capture";
            return f == ControllerFamily::Stadia ? "Capture" : "";
        case PadExtra::Touchpad: return (f == ControllerFamily::DualShock4 || f == ControllerFamily::DualSense || f == ControllerFamily::DualSenseEdge) ? "Touchpad" : "";
        case PadExtra::Mute: return (f == ControllerFamily::DualSense || f == ControllerFamily::DualSenseEdge) ? "Mute" : "";
    }
    return "";
}

} // namespace okay
