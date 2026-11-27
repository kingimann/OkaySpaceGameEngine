// The device emulator's model (DeviceEmulatorModel.hpp). Written for this engine; see the header for the sources of the
// device numbers. No UI library here: tests/test_device_emulator.cpp checks every part.
#include "DeviceEmulatorModel.hpp"
#include "okay/Components/UIAnchor.hpp"     // UICanvas: the Screen API's state
#include "okay/Components/UISafeArea.hpp"   // UISafeInsets
#include "ProjectVersion.hpp"                // okayver::detail::NextLine / SplitKeyValue: the key=value lines of the settings file
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace okay::devemu {

namespace {

using P = Platform;
using F = Form;
using C = Cutout;
using N = Nav;

// One table row. Lengths in points (iOS) or dp (Android); see DeviceSpec.
struct Row {
    const char* id; const char* name; P platform; F form;
    int w, h; float ppi, scale, corner;
    C cutout; float cutW, cutH;
    float top, bottom, landSide, landBottom, landTop;
    bool bothSides; N nav; bool upsideDown;
};

// Rows 1..24 are the old list's order (okay_settings.txt "gamedevice2"); new devices follow. Screen pixels and pixel
// density are the makers' published figures; the insets are the platforms' published safe-area / bar sizes, rounded.
const Row kRows[] = {
    // ---- the old list, in its order ----
    {"iphone-16-pro",      "iPhone 16 Pro",            P::Ios, F::Phone,      1206, 2622, 460, 3.0f,   55, C::Island,     125, 37, 62, 34, 62, 21, 0,  true,  N::Gesture,     false},
    {"iphone-16-pro-max",  "iPhone 16 Pro Max",        P::Ios, F::Phone,      1320, 2868, 460, 3.0f,   55, C::Island,     125, 37, 62, 34, 62, 21, 0,  true,  N::Gesture,     false},
    {"iphone-15",          "iPhone 15 / 15 Pro / 16",  P::Ios, F::Phone,      1179, 2556, 460, 3.0f,   55, C::Island,     125, 37, 59, 34, 59, 21, 0,  true,  N::Gesture,     false},
    {"iphone-15-pro-max",  "iPhone 15 Pro Max",        P::Ios, F::Phone,      1290, 2796, 460, 3.0f,   55, C::Island,     125, 37, 59, 34, 59, 21, 0,  true,  N::Gesture,     false},
    {"iphone-13",          "iPhone 13 / 14",           P::Ios, F::Phone,      1170, 2532, 460, 3.0f,   47, C::Notch,      162, 33, 47, 34, 47, 21, 0,  true,  N::Gesture,     false},
    {"iphone-13-mini",     "iPhone 13 mini",           P::Ios, F::Phone,      1080, 2340, 476, 2.88f,  44, C::Notch,      162, 33, 50, 34, 50, 21, 0,  true,  N::Gesture,     false},
    {"iphone-se",          "iPhone SE",                P::Ios, F::Phone,       750, 1334, 326, 2.0f,    0, C::HomeButton,   0,  0, 20,  0,  0,  0, 0,  false, N::HomeButton,  false},
    {"pixel-8",            "Pixel 8",                  P::Android, F::Phone,  1080, 2400, 428, 2.625f, 41, C::PunchHole,   20, 20, 32, 24, 32, 24, 24, false, N::Gesture,     false},
    {"pixel-8-pro",        "Pixel 8 Pro",              P::Android, F::Phone,  1344, 2992, 489, 3.0f,   40, C::PunchHole,   20, 20, 32, 24, 32, 24, 24, false, N::Gesture,     false},
    {"galaxy-s24",         "Galaxy S24",               P::Android, F::Phone,  1080, 2340, 416, 2.8125f, 35, C::PunchHole,  18, 18, 28, 24, 28, 24, 24, false, N::Gesture,     false},
    {"galaxy-s24-ultra",   "Galaxy S24 Ultra",         P::Android, F::Phone,  1440, 3120, 505, 3.75f,  19, C::PunchHole,   16, 16, 26, 24, 26, 24, 24, false, N::Gesture,     false},
    {"galaxy-a54",         "Galaxy A54 / A-series",    P::Android, F::Phone,  1080, 2340, 403, 2.8125f, 31, C::PunchHole,  20, 20, 29, 24, 29, 24, 24, false, N::Gesture,     false},
    {"galaxy-z-flip",      "Galaxy Z Flip (open)",     P::Android, F::Foldable, 1080, 2640, 425, 2.8125f, 31, C::PunchHole, 20, 20, 31, 24, 31, 24, 24, false, N::Gesture,     false},
    {"oneplus-12",         "OnePlus 12",               P::Android, F::Phone,  1440, 3168, 510, 3.5f,   37, C::PunchHole,   18, 18, 28, 24, 28, 24, 24, false, N::Gesture,     false},
    {"xiaomi-14",          "Xiaomi 14",                P::Android, F::Phone,  1200, 2670, 460, 3.0f,   36, C::PunchHole,   18, 18, 28, 24, 28, 24, 24, false, N::Gesture,     false},
    {"android-720p",       "Small Android (720p)",     P::Android, F::Phone,   720, 1600, 270, 1.875f, 27, C::PunchHole,   20, 20, 30, 24, 30, 24, 24, false, N::Gesture,     false},
    {"android-16x9",       "Old Android (16:9)",       P::Android, F::Phone,   720, 1280, 294, 2.0f,    0, C::None,         0,  0, 24, 48,  0, 48, 24, false, N::ThreeButton, false},
    {"ipad-pro-12-9",      "iPad Pro 12.9\"",          P::Ios, F::Tablet,     2048, 2732, 264, 2.0f,   18, C::None,         0,  0, 24, 20,  0, 20, 24, false, N::Gesture,     true},
    {"ipad-pro-11",        "iPad Pro 11\"",            P::Ios, F::Tablet,     1668, 2388, 264, 2.0f,   18, C::None,         0,  0, 24, 20,  0, 20, 24, false, N::Gesture,     true},
    {"ipad-air",           "iPad Air / iPad 10",       P::Ios, F::Tablet,     1640, 2360, 264, 2.0f,   18, C::None,         0,  0, 24, 20,  0, 20, 24, false, N::Gesture,     true},
    {"ipad-mini",          "iPad mini",                P::Ios, F::Tablet,     1488, 2266, 326, 2.0f,   21, C::None,         0,  0, 24, 20,  0, 20, 24, false, N::Gesture,     true},
    {"galaxy-tab-s9",      "Galaxy Tab S9",            P::Android, F::Tablet, 1600, 2560, 274, 2.0f,   16, C::None,         0,  0, 24, 24,  0, 24, 24, false, N::Gesture,     true},
    {"pixel-fold",         "Pixel Fold (open)",        P::Android, F::Foldable, 1840, 2208, 380, 2.3f, 20, C::None,         0,  0, 24, 24,  0, 24, 24, false, N::Gesture,     true},
    {"fire-hd-10",         "Amazon Fire HD 10",        P::Android, F::Tablet, 1200, 1920, 224, 1.5f,   10, C::None,         0,  0, 24, 48,  0, 48, 24, false, N::ThreeButton, true},
    // ---- more phones ----
    {"iphone-16e",         "iPhone 16e",               P::Ios, F::Phone,      1170, 2532, 460, 3.0f,   47, C::Notch,      162, 33, 47, 34, 47, 21, 0,  true,  N::Gesture,     false},
    {"iphone-16-plus",     "iPhone 15 Plus / 16 Plus", P::Ios, F::Phone,      1290, 2796, 460, 3.0f,   55, C::Island,     125, 37, 59, 34, 59, 21, 0,  true,  N::Gesture,     false},
    {"iphone-x",           "iPhone X / XS / 11 Pro",   P::Ios, F::Phone,      1125, 2436, 458, 3.0f,   39, C::Notch,      209, 30, 44, 34, 44, 21, 0,  true,  N::Gesture,     false},
    {"iphone-11",          "iPhone 11 / XR",           P::Ios, F::Phone,       828, 1792, 326, 2.0f,   41, C::Notch,      209, 30, 48, 34, 48, 21, 0,  true,  N::Gesture,     false},
    {"iphone-8-plus",      "iPhone 8 Plus",            P::Ios, F::Phone,      1080, 1920, 401, 2.608f,  0, C::HomeButton,   0,  0, 20,  0,  0,  0, 0,  false, N::HomeButton,  false},
    {"pixel-9",            "Pixel 9",                  P::Android, F::Phone,  1080, 2424, 422, 2.625f, 41, C::PunchHole,   20, 20, 32, 24, 32, 24, 24, false, N::Gesture,     false},
    {"pixel-9-pro-xl",     "Pixel 9 Pro XL",           P::Android, F::Phone,  1344, 2992, 486, 3.0f,   40, C::PunchHole,   20, 20, 32, 24, 32, 24, 24, false, N::Gesture,     false},
    {"pixel-7a",           "Pixel 7a",                 P::Android, F::Phone,  1080, 2400, 429, 2.625f, 38, C::PunchHole,   20, 20, 32, 24, 32, 24, 24, false, N::Gesture,     false},
    {"galaxy-s24-plus",    "Galaxy S24+",              P::Android, F::Phone,  1440, 3120, 516, 3.75f,  28, C::PunchHole,   16, 16, 26, 24, 26, 24, 24, false, N::Gesture,     false},
    {"galaxy-a15",         "Galaxy A15 (drop notch)",  P::Android, F::Phone,  1080, 2340, 396, 2.8125f, 28, C::Notch,      28, 24, 32, 48, 32, 48, 24, false, N::ThreeButton, false},
    {"moto-g-power",       "Moto G Power",             P::Android, F::Phone,  1080, 2400, 395, 2.625f, 30, C::PunchHole,   20, 20, 32, 24, 32, 24, 24, false, N::Gesture,     false},
    {"android-budget",     "Budget Android (3 buttons)", P::Android, F::Phone,  720, 1600, 270, 1.875f, 20, C::Notch,      26, 22, 30, 48, 30, 48, 24, false, N::ThreeButton, false},
    // ---- foldables ----
    {"galaxy-z-fold-open", "Galaxy Z Fold (open)",     P::Android, F::Foldable, 1812, 2176, 374, 2.5f, 20, C::None,         0,  0, 24, 24,  0, 24, 24, false, N::Gesture,     true},
    {"galaxy-z-fold-cover", "Galaxy Z Fold (cover)",   P::Android, F::Foldable,  904, 2316, 402, 2.5f, 22, C::PunchHole,   16, 16, 30, 24, 30, 24, 24, false, N::Gesture,     false},
    {"pixel-fold-cover",   "Pixel Fold (cover)",       P::Android, F::Foldable, 1080, 2092, 408, 2.625f, 30, C::PunchHole, 18, 18, 30, 24, 30, 24, 24, false, N::Gesture,     false},
    // ---- more tablets ----
    {"ipad-pro-13",        "iPad Pro 13\" (M4)",       P::Ios, F::Tablet,     2064, 2752, 264, 2.0f,   18, C::None,         0,  0, 24, 20,  0, 20, 24, false, N::Gesture,     true},
    {"ipad-9",             "iPad (9th gen, home button)", P::Ios, F::Tablet,  1620, 2160, 264, 2.0f,    0, C::HomeButton,   0,  0, 20,  0,  0,  0, 20, false, N::HomeButton,  true},
    {"pixel-tablet",       "Pixel Tablet",             P::Android, F::Tablet, 1600, 2560, 276, 2.0f,   16, C::None,         0,  0, 24, 24,  0, 24, 24, false, N::Gesture,     true},
    {"galaxy-tab-a9-plus", "Galaxy Tab A9+",           P::Android, F::Tablet, 1200, 1920, 207, 1.5f,   12, C::None,         0,  0, 24, 48,  0, 48, 24, false, N::ThreeButton, true},
};
constexpr int kLegacyCount = 24;

DeviceSpec FromRow(const Row& r) {
    DeviceSpec d;
    d.id = r.id; d.name = r.name; d.platform = r.platform; d.form = r.form;
    d.width = r.w; d.height = r.h; d.ppi = r.ppi; d.scale = r.scale; d.cornerPt = r.corner;
    d.cutout = r.cutout; d.cutoutWPt = r.cutW; d.cutoutHPt = r.cutH;
    d.topPt = r.top; d.bottomPt = r.bottom; d.landSidePt = r.landSide; d.landBottomPt = r.landBottom; d.landTopPt = r.landTop;
    d.bothSides = r.bothSides; d.nav = r.nav; d.upsideDown = r.upsideDown;
    return d;
}

float Clampf(float v, float lo, float hi) { return !(v >= lo) ? lo : (v > hi ? hi : v); }

}  // namespace

const std::vector<DeviceSpec>& Devices() {
    static const std::vector<DeviceSpec> all = [] {
        std::vector<DeviceSpec> v;
        for (const Row& r : kRows) v.push_back(FromRow(r));
        return v;
    }();
    return all;
}

const DeviceSpec* FindDevice(const std::string& id, const DeviceSpec* custom) {
    if (id.empty()) return nullptr;
    if (id == "custom") return custom;
    for (const DeviceSpec& d : Devices()) if (d.id == id) return &d;
    return nullptr;
}

std::string LegacyDeviceId(int index) {
    if (index < 1 || index > kLegacyCount) return {};
    return kRows[index - 1].id;
}
int LegacyDeviceIndex(const std::string& id) {
    for (int i = 0; i < kLegacyCount; ++i) if (id == kRows[i].id) return i + 1;
    return 0;
}
std::string LegacyShortListId(int value) {
    static const int kOld[] = {0, 3, 4, 5, 7, 8, 10, 16, 20, 21, 22};   // the short list's entries in the old list
    if (value < 0 || value >= (int)(sizeof(kOld) / sizeof(kOld[0]))) return {};
    return LegacyDeviceId(kOld[value]);
}

DeviceSpec DefaultCustomDevice() {
    DeviceSpec d = FromRow(kRows[7]);   // a Pixel-8-shaped phone
    d.id = "custom"; d.name = "Custom device";
    return d;
}
void ClampCustomDevice(DeviceSpec& d) {
    d.id = "custom";
    if (d.name.empty()) d.name = "Custom device";
    d.width = std::clamp(d.width, 64, 8192);
    d.height = std::clamp(d.height, 64, 8192);
    d.ppi = Clampf(d.ppi, 50.0f, 1000.0f);
    d.scale = Clampf(d.scale, 0.5f, 5.0f);
    const float shortPt = (float)std::min(d.width, d.height) / d.scale, longPt = (float)std::max(d.width, d.height) / d.scale;
    d.cornerPt = Clampf(d.cornerPt, 0.0f, shortPt * 0.2f);
    d.cutoutWPt = Clampf(d.cutoutWPt, 0.0f, shortPt * 0.6f);
    d.cutoutHPt = Clampf(d.cutoutHPt, 0.0f, longPt * 0.1f);
    const float halfShort = shortPt * 0.5f - 1.0f, halfLong = longPt * 0.5f - 1.0f;
    d.topPt = Clampf(d.topPt, 0.0f, halfLong * 0.5f);
    d.bottomPt = Clampf(d.bottomPt, 0.0f, halfLong * 0.5f);
    d.landSidePt = Clampf(d.landSidePt, 0.0f, halfLong * 0.5f);
    d.landBottomPt = Clampf(d.landBottomPt, 0.0f, halfShort * 0.5f);
    d.landTopPt = Clampf(d.landTopPt, 0.0f, halfShort * 0.5f);
    if (d.cutout != Cutout::None && d.cutout != Cutout::HomeButton) d.cutoutHPt = std::min(d.cutoutHPt, d.topPt);
}

const char* PlatformName(Platform p) { return p == Platform::Ios ? "iOS" : "Android"; }
const char* FormName(Form f) { return f == Form::Tablet ? "Tablet" : f == Form::Foldable ? "Foldable" : "Phone"; }
const char* CutoutName(Cutout c) {
    switch (c) {
        case Cutout::Notch: return "Notch";
        case Cutout::Island: return "Pill (island)";
        case Cutout::PunchHole: return "Punch hole";
        case Cutout::HomeButton: return "Home button";
        default: return "None";
    }
}
const char* OrientationName(Orientation o) {
    switch (o) {
        case Orientation::LandscapeLeft: return "Landscape left";
        case Orientation::PortraitUpsideDown: return "Portrait upside down";
        case Orientation::LandscapeRight: return "Landscape right";
        default: return "Portrait";
    }
}
int OrientationDegrees(Orientation o) { return (int)o * 90; }
Orientation TurnLeft(Orientation o) { return (Orientation)(((int)o + 1) % 4); }
Orientation TurnRight(Orientation o) { return (Orientation)(((int)o + 3) % 4); }

void ScreenPixels(const DeviceSpec& d, Orientation o, int& w, int& h) {
    w = IsLandscape(o) ? d.height : d.width;
    h = IsLandscape(o) ? d.width : d.height;
}

float StatusBarPx(const DeviceSpec& d, Orientation o) {
    return (IsLandscape(o) ? d.landTopPt : d.topPt) * d.scale;
}
float HomeBarPx(const DeviceSpec& d, Orientation o) {
    return (IsLandscape(o) ? d.landBottomPt : d.bottomPt) * d.scale;
}
float CornerPx(const DeviceSpec& d) { return d.cornerPt * d.scale; }

Insets SafeInsetsPx(const DeviceSpec& d, Orientation o) {
    // The camera is fixed to the device and turns with it; the status bar stays at the top of the screen and the home
    // indicator / navigation bar at its bottom, whichever way the device is turned.
    const float s = d.scale;
    Insets r;
    switch (o) {
        case Orientation::Portrait:
            r.top = d.topPt * s; r.bottom = d.bottomPt * s;
            break;
        case Orientation::PortraitUpsideDown:
            r.top = d.topPt * s;   // the status bar (the camera is now at the bottom)
            r.bottom = std::max(d.bottomPt, d.cutout == Cutout::HomeButton || d.cutout == Cutout::None ? 0.0f : d.topPt) * s;
            break;
        case Orientation::LandscapeLeft:
        case Orientation::LandscapeRight: {
            r.top = d.landTopPt * s; r.bottom = d.landBottomPt * s;
            const float side = d.landSidePt * s;
            if (d.bothSides) { r.left = r.right = side; }
            else if (o == Orientation::LandscapeLeft) r.left = side;   // the top edge (the camera) is on the left
            else r.right = side;
        } break;
    }
    return r;
}

CutoutShape CutoutPx(const DeviceSpec& d, Orientation o) {
    CutoutShape c;
    c.kind = d.cutout;
    if (d.cutout == Cutout::None || d.cutout == Cutout::HomeButton) return c;
    int w = 0, h = 0;
    ScreenPixels(d, o, w, h);
    const float s = d.scale;
    float along = d.cutoutWPt * s, across = d.cutoutHPt * s, off = 0.0f;   // along the edge, into the screen, gap from the edge
    switch (d.cutout) {
        case Cutout::Island: off = 11.0f * s; c.radius = across * 0.5f; break;
        case Cutout::PunchHole: off = std::max(0.0f, d.topPt * s * 0.5f - across * 0.5f); c.radius = across * 0.5f; break;
        default: c.radius = across * 0.45f; break;   // a notch hangs from the edge
    }
    // In portrait the camera is centred on the top edge: x from the middle, y from the top.
    Box b;
    switch (o) {
        case Orientation::Portrait:           b = Box{(w - along) * 0.5f, off, along, across}; break;
        case Orientation::PortraitUpsideDown: b = Box{(w - along) * 0.5f, h - off - across, along, across}; break;
        case Orientation::LandscapeLeft:      b = Box{off, (h - along) * 0.5f, across, along}; break;
        case Orientation::LandscapeRight:     b = Box{w - off - across, (h - along) * 0.5f, across, along}; break;
    }
    c.box = b;
    return c;
}

float FrameRoom(Box view) { return std::min(view.w, view.h) * 0.06f + 10.0f; }

Placement PlaceScreen(Box view, int screenW, int screenH, Zoom zoom, float percent, float framebufferScale, Pt pan, float frameRoom) {
    Placement p;
    if (screenW <= 0 || screenH <= 0) { p.screen = view; return p; }
    const float fb = framebufferScale > 0.25f ? framebufferScale : 1.0f;
    float scale = 1.0f;
    if (zoom == Zoom::Fit) {
        const float aw = std::max(40.0f, view.w - 2.0f * frameRoom), ah = std::max(40.0f, view.h - 2.0f * frameRoom);
        scale = std::min(aw / (float)screenW, ah / (float)screenH);
    } else {
        scale = 1.0f / fb;   // one device pixel per display pixel
        if (zoom == Zoom::Percent) scale *= Clampf(percent, 5.0f, 800.0f) / 100.0f;
    }
    const float sw = (float)screenW * scale, sh = (float)screenH * scale;
    float x = view.x + (view.w - sw) * 0.5f, y = view.y + (view.h - sh) * 0.5f;
    if (zoom != Zoom::Fit) {   // a screen larger than the view moves with the pan, never past its own edges
        if (sw > view.w) x = Clampf(x + pan.x, view.Right() - sw, view.x);
        if (sh > view.h) y = Clampf(y + pan.y, view.Bottom() - sh, view.y);
    }
    p.screen = Box{x, y, sw, sh};
    p.scale = scale;
    return p;
}

Pt ViewToGame(const Placement& p, Pt v) { return Pt{v.x - p.screen.x, v.y - p.screen.y}; }
Pt GameToView(const Placement& p, Pt g) { return Pt{g.x + p.screen.x, g.y + p.screen.y}; }
Pt ViewToDevice(const Placement& p, Pt v) {
    const float s = p.scale > 1e-6f ? p.scale : 1.0f;
    return Pt{(v.x - p.screen.x) / s, (v.y - p.screen.y) / s};
}
Pt DeviceToView(const Placement& p, Pt d) { return Pt{p.screen.x + d.x * p.scale, p.screen.y + d.y * p.scale}; }
float GameDpi(const DeviceSpec& d, const Placement& p) { return d.ppi * p.scale; }

void RenderSize(int screenW, int screenH, const Placement& p, float framebufferScale, bool deviceResolution, int cap, int& w, int& h) {
    if (!deviceResolution || screenW <= 0 || screenH <= 0) {
        const float fb = framebufferScale > 0.25f ? framebufferScale : 1.0f;
        w = std::max(1, (int)std::lround(p.screen.w * fb));
        h = std::max(1, (int)std::lround(p.screen.h * fb));
        return;
    }
    const int c = cap > 0 ? cap : 4096;
    const float k = std::min(1.0f, std::min((float)c / (float)screenW, (float)c / (float)screenH));
    w = std::max(1, (int)std::lround(screenW * k));
    h = std::max(1, (int)std::lround(screenH * k));
}

ScreenReport MakeScreenReport(const DeviceSpec& d, Orientation o, const Placement& p, float keyboardGamePx, bool darkTheme) {
    ScreenReport r;
    r.width = p.screen.w; r.height = p.screen.h;
    r.dpi = GameDpi(d, p);
    r.orientation = (int)o;
    r.darkTheme = darkTheme;
    int w = 0, h = 0;
    ScreenPixels(d, o, w, h);
    const Insets s = SafeInsetsPx(d, o);
    r.safe.left = s.left / (float)w; r.safe.right = s.right / (float)w;
    r.safe.top = s.top / (float)h;
    r.keyboardHeight = std::max(0.0f, keyboardGamePx);
    const float kbFrac = r.height > 0.0f ? r.keyboardHeight / r.height : 0.0f;
    r.safe.bottom = std::max(s.bottom / (float)h, kbFrac);
    return r;
}

void ApplyScreenReport(const ScreenReport& r) {
    if (r.width > 0.0f && r.height > 0.0f) UICanvas::Set(r.width, r.height);
    UICanvas::Dpi() = r.dpi > 0.0f ? r.dpi : 96.0f;
    UICanvas::Orientation() = r.orientation;
    UICanvas::DarkTheme() = r.darkTheme;
    UICanvas::KeyboardHeight() = r.keyboardHeight;
    UISafeInsetFractions& f = UISafeInsets();
    f.left = r.safe.left; f.top = r.safe.top; f.right = r.safe.right; f.bottom = r.safe.bottom;
}
void ResetScreenReport() {
    UICanvas::Dpi() = 96.0f;
    UICanvas::Orientation() = -1;
    UICanvas::DarkTheme() = false;
    UICanvas::KeyboardHeight() = 0.0f;
    UISafeInsets() = UISafeInsetFractions{};
}

// ---- touch ----
namespace {
Pt ClampTo(Pt p, Box b) {
    return Pt{Clampf(p.x, b.x, b.x + std::max(0.0f, b.w - 0.001f)), Clampf(p.y, b.y, b.y + std::max(0.0f, b.h - 0.001f))};
}
}  // namespace

void TouchEmulator::Update(const PointerFrame& in, Box screen, Box exclude, std::vector<TouchEvent>& out) {
    const Pt centre{screen.x + screen.w * 0.5f, screen.y + screen.h * 0.5f};
    if (!m_down) {
        if (!in.down || !screen.Contains(in.pos) || exclude.Contains(in.pos)) return;
        // (A press that began elsewhere and is dragged onto the screen does not count: m_down stays false until a press
        // starts on it. The host only passes `down` for presses that started on the Game view.)
        m_down = true;
        m_id0 = m_next++;
        m_p0 = in.pos;
        out.push_back({m_id0, 0, m_p0});
        if (in.twoFinger) {
            m_two = true;
            m_id1 = m_next++;
            m_p1 = ClampTo(Mirror(m_p0, centre), screen);
            m_panOffset = Pt{m_p1.x - m_p0.x, m_p1.y - m_p0.y};
            out.push_back({m_id1, 0, m_p1});
        }
        return;
    }
    if (!in.down) {   // lifted
        out.push_back({m_id0, 2, m_p0});
        if (m_two) out.push_back({m_id1, 2, m_p1});
        m_down = m_two = false;
        return;
    }
    const Pt p0 = ClampTo(in.pos, screen);
    if (p0.x != m_p0.x || p0.y != m_p0.y) { m_p0 = p0; out.push_back({m_id0, 1, m_p0}); }
    if (in.twoFinger && !m_two) {   // the modifier pressed mid-drag: put the second finger down
        m_two = true;
        m_id1 = m_next++;
        m_p1 = ClampTo(Mirror(m_p0, centre), screen);
        m_panOffset = Pt{m_p1.x - m_p0.x, m_p1.y - m_p0.y};
        out.push_back({m_id1, 0, m_p1});
        return;
    }
    if (!in.twoFinger && m_two) {   // the modifier let go: lift the second finger
        out.push_back({m_id1, 2, m_p1});
        m_two = false;
        return;
    }
    if (m_two) {
        Pt p1;
        if (in.pan) p1 = ClampTo(Pt{m_p0.x + m_panOffset.x, m_p0.y + m_panOffset.y}, screen);
        else {
            p1 = ClampTo(Mirror(m_p0, centre), screen);
            m_panOffset = Pt{p1.x - m_p0.x, p1.y - m_p0.y};   // a pan that starts now keeps this gap
        }
        if (p1.x != m_p1.x || p1.y != m_p1.y) { m_p1 = p1; out.push_back({m_id1, 1, m_p1}); }
    }
}

void TouchEmulator::Release(std::vector<TouchEvent>& out) {
    if (!m_down) return;
    out.push_back({m_id0, 2, m_p0});
    if (m_two) out.push_back({m_id1, 2, m_p1});
    m_down = m_two = false;
}

// ---- keyboard ----
float KeyboardHeight(float w, float h, bool tablet, float bottomInset) {
    const bool land = w > h;
    float keys = tablet ? h * (land ? 0.38f : 0.28f) : (land ? h * 0.50f : std::min(h * 0.36f, w * 0.82f));
    return keys + std::max(0.0f, bottomInset);
}

KeyboardLayout LayoutKeyboard(float w, float h, bool tablet, float bottomInset, bool shift, bool symbols) {
    KeyboardLayout k;
    const float total = KeyboardHeight(w, h, tablet, bottomInset);
    k.area = Box{0.0f, h - total, w, total};
    const float keysH = total - std::max(0.0f, bottomInset);
    const float rowH = keysH / 4.0f, gap = std::max(2.0f, std::min(w, h) * 0.012f);
    const float unit = w / 10.0f;   // the top row's ten keys set the width of a key
    auto cap = [&](float x, int row, float wUnits, KeyKind kind, char ch, std::string label) {
        KeyCap c;
        c.box = Box{x + gap * 0.5f, k.area.y + row * rowH + gap * 0.5f, wUnits * unit - gap, rowH - gap};
        c.kind = kind; c.ch = ch; c.label = std::move(label);
        k.keys.push_back(std::move(c));
    };
    auto letters = [&](const char* s, int row, float x0) {
        for (int i = 0; s[i]; ++i) {
            char c = s[i];
            if (shift && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            cap(x0 + i * unit, row, 1.0f, KeyKind::Char, c, std::string(1, c));
        }
    };
    if (!symbols) {
        letters("qwertyuiop", 0, 0.0f);
        letters("asdfghjkl", 1, unit * 0.5f);
        cap(0.0f, 2, 1.5f, KeyKind::Shift, 0, "shift");
        letters("zxcvbnm", 2, unit * 1.5f);
        cap(unit * 8.5f, 2, 1.5f, KeyKind::Backspace, 0, "del");
    } else {
        letters("1234567890", 0, 0.0f);
        letters("-/:;()$&@", 1, unit * 0.5f);
        cap(0.0f, 2, 1.5f, KeyKind::Shift, 0, "shift");
        letters(".,?!'\"#", 2, unit * 1.5f);
        cap(unit * 8.5f, 2, 1.5f, KeyKind::Backspace, 0, "del");
    }
    cap(0.0f, 3, 1.5f, KeyKind::Symbols, 0, symbols ? "ABC" : "123");
    cap(unit * 1.5f, 3, 1.0f, KeyKind::Hide, 0, "hide");
    cap(unit * 2.5f, 3, 5.0f, KeyKind::Space, ' ', "space");
    cap(unit * 7.5f, 3, 2.5f, KeyKind::Enter, '\n', "return");
    return k;
}

const KeyCap* KeyAt(const KeyboardLayout& k, Pt p) {
    for (const KeyCap& c : k.keys) if (c.box.Contains(p)) return &c;
    return nullptr;
}

// ---- motion sensor ----
Vec3f TiltToGravity(float pitchDeg, float rollDeg) {
    const float p = pitchDeg * 3.14159265358979f / 180.0f, r = rollDeg * 3.14159265358979f / 180.0f;
    return Vec3f{std::sin(r) * std::cos(p), -std::cos(r) * std::cos(p), -std::sin(p)};
}

Orientation OrientationForGravity(Vec3f g, Orientation current, bool allowUpsideDown) {
    const float ax = std::fabs(g.x), ay = std::fabs(g.y);
    // Held clearly upright on one edge: that edge's component is most of gravity and well ahead of the other.
    if (std::max(ax, ay) < 0.6f || std::fabs(ax - ay) < 0.3f) return current;
    if (ax > ay) return g.x > 0 ? Orientation::LandscapeRight : Orientation::LandscapeLeft;
    if (g.y < 0) return Orientation::Portrait;
    return allowUpsideDown ? Orientation::PortraitUpsideDown : current;
}

// ---- rotate animation ----
void RotateAnimation::Start(Orientation from, Orientation to, double now, double seconds) {
    int d = (OrientationDegrees(to) - OrientationDegrees(from)) % 360;   // counter-clockwise turn of the device
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    if (d == 180) d = -180;   // half a turn: either way round, one fixed choice
    m_delta = (float)d;
    m_t0 = now; m_len = seconds > 0.0 ? seconds : 0.35;
    m_running = d != 0;
}
float RotateAnimation::Angle(double now) const {
    if (!m_running) return 0.0f;
    const double t = (now - m_t0) / m_len;
    if (t >= 1.0) return 0.0f;
    const double u = t <= 0.0 ? 0.0 : t;
    const double e = 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u);   // ease out (cubic)
    return (float)(-(double)m_delta * (1.0 - e));
}

// ---- conditions ----
NetValues NetworkValues(Network n) {
    NetValues v;
    if (n == Network::Slow) { v.enabled = true; v.latencyMs = 300.0f; v.jitterMs = 120.0f; v.loss = 0.03f; }   // a weak mobile signal
    else if (n == Network::Offline) { v.enabled = true; v.latencyMs = 0.0f; v.jitterMs = 0.0f; v.loss = 1.0f; }
    return v;
}
const char* NetworkName(Network n) { return n == Network::Slow ? "Slow" : n == Network::Offline ? "Offline" : "Online"; }
int EffectiveFpsCap(int editorCap, int deviceCap, bool playing, bool emulating) {
    if (!playing || !emulating || deviceCap <= 0) return editorCap;
    return editorCap > 0 ? std::min(editorCap, deviceCap) : deviceCap;
}

// ---- several devices ----
std::vector<Box> SplitView(Box view, int n, float gap) {
    n = std::clamp(n, 1, 4);
    std::vector<Box> out;
    if (n == 4 && view.h > view.w) {   // 2 x 2
        const float cw = (view.w - gap) * 0.5f, ch = (view.h - gap) * 0.5f;
        for (int i = 0; i < 4; ++i) out.push_back(Box{view.x + (i % 2) * (cw + gap), view.y + (i / 2) * (ch + gap), cw, ch});
        return out;
    }
    const float cw = (view.w - gap * (n - 1)) / (float)n;
    for (int i = 0; i < n; ++i) out.push_back(Box{view.x + i * (cw + gap), view.y, cw, view.h});
    return out;
}

// ---- screenshots ----
std::string ScreenshotName(const std::string& device, Orientation o, int y, int mo, int d, int h, int mi, int s) {
    std::string n;
    bool gap = false;
    for (char c : device) {
        if (std::isalnum((unsigned char)c)) { if (gap && !n.empty()) n += '_'; n += c; gap = false; }
        else gap = true;
    }
    if (n.empty()) n = "device";
    char tail[64];
    std::snprintf(tail, sizeof tail, "_%s_%04d%02d%02d_%02d%02d%02d.png", IsLandscape(o) ? "landscape" : "portrait", y, mo, d, h, mi, s);
    return n + tail;
}

std::vector<std::uint8_t> CropRgba(const std::vector<std::uint8_t>& src, int sw, int sh, int x, int y, int w, int h, int& outW, int& outH) {
    const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(sw, x + w), y1 = std::min(sh, y + h);
    outW = outH = 0;
    if (x1 <= x0 || y1 <= y0 || src.size() < (std::size_t)sw * (std::size_t)sh * 4u) return {};
    outW = x1 - x0; outH = y1 - y0;
    std::vector<std::uint8_t> out((std::size_t)outW * (std::size_t)outH * 4u);
    for (int r = 0; r < outH; ++r)
        std::copy_n(src.begin() + ((std::size_t)(y0 + r) * (std::size_t)sw + (std::size_t)x0) * 4u, (std::size_t)outW * 4u,
                    out.begin() + (std::size_t)r * (std::size_t)outW * 4u);
    return out;
}

std::string StatusClock(int hour, int minute, Platform p) {
    char b[16];
    hour = ((hour % 24) + 24) % 24; minute = ((minute % 60) + 60) % 60;
    if (p == Platform::Ios) { const int h12 = hour % 12 == 0 ? 12 : hour % 12; std::snprintf(b, sizeof b, "%d:%02d", h12, minute); }
    else std::snprintf(b, sizeof b, "%02d:%02d", hour, minute);
    return b;
}

// ---- settings ----
namespace {
bool ReadFloat(const std::string& v, float& out) {
    const char* p = v.c_str();
    char* e = nullptr;
    const float x = std::strtof(p, &e);
    if (e == p || !std::isfinite(x)) return false;
    out = x;
    return true;
}
bool ReadInt(const std::string& v, int& out) {
    const char* p = v.c_str();
    char* e = nullptr;
    const long x = std::strtol(p, &e, 10);
    if (e == p) return false;
    out = (int)std::clamp(x, -1000000L, 1000000L);
    return true;
}
std::string Num(float v) { char b[32]; std::snprintf(b, sizeof b, "%g", v); return b; }
}  // namespace

std::string WriteSettings(const Settings& s) {
    std::ostringstream o;
    o << "device=" << s.device << "\n";
    o << "orientation=" << (int)s.orientation << "\n";
    o << "rotation_lock=" << (s.rotationLock ? 1 : 0) << "\n";
    o << "zoom=" << (int)s.zoom << "\n";
    o << "zoom_percent=" << Num(s.zoomPercent) << "\n";
    o << "safe_area=" << (s.safeArea ? 1 : 0) << "\n";
    o << "status_bar=" << (s.statusBar ? 1 : 0) << "\n";
    o << "touch_input=" << (s.touchInput ? 1 : 0) << "\n";
    o << "touch_points=" << (s.touchPoints ? 1 : 0) << "\n";
    o << "keyboard=" << (s.keyboard ? 1 : 0) << "\n";
    o << "dark_theme=" << (s.darkTheme ? 1 : 0) << "\n";
    o << "device_resolution=" << (s.deviceResolution ? 1 : 0) << "\n";
    o << "fps_cap=" << s.fpsCap << "\n";
    o << "network=" << (int)s.network << "\n";
    o << "tilt_pitch=" << Num(s.tiltPitch) << "\n";
    o << "tilt_roll=" << Num(s.tiltRoll) << "\n";
    o << "battery=" << s.battery << "\n";
    o << "extra=";
    for (std::size_t i = 0; i < s.extra.size(); ++i) o << (i ? "," : "") << s.extra[i];
    o << "\n";
    const DeviceSpec& c = s.custom;
    o << "custom_name=" << c.name << "\n";
    o << "custom_platform=" << (int)c.platform << "\n";
    o << "custom_form=" << (int)c.form << "\n";
    o << "custom_width=" << c.width << "\n";
    o << "custom_height=" << c.height << "\n";
    o << "custom_ppi=" << Num(c.ppi) << "\n";
    o << "custom_scale=" << Num(c.scale) << "\n";
    o << "custom_corner=" << Num(c.cornerPt) << "\n";
    o << "custom_cutout=" << (int)c.cutout << "\n";
    o << "custom_cutout_w=" << Num(c.cutoutWPt) << "\n";
    o << "custom_cutout_h=" << Num(c.cutoutHPt) << "\n";
    o << "custom_top=" << Num(c.topPt) << "\n";
    o << "custom_bottom=" << Num(c.bottomPt) << "\n";
    o << "custom_land_side=" << Num(c.landSidePt) << "\n";
    o << "custom_land_bottom=" << Num(c.landBottomPt) << "\n";
    o << "custom_land_top=" << Num(c.landTopPt) << "\n";
    o << "custom_both_sides=" << (c.bothSides ? 1 : 0) << "\n";
    o << "custom_nav=" << (int)c.nav << "\n";
    return o.str();
}

void ParseSettings(const std::string& text, Settings& s) {
    DeviceSpec& c = s.custom;
    std::string k, v;
    for (std::size_t pos = 0, n = 0; pos < text.size(); ++n) {   // key=value lines (okayver's shared line splitter)
        const okayver::detail::LineSpan span = okayver::detail::NextLine(text, pos);
        pos = span.next;
        if (!okayver::detail::SplitKeyValue(text, span, n == 0, &k, &v)) continue;
        int i = 0; float f = 0.0f;
        const bool hasI = ReadInt(v, i), hasF = ReadFloat(v, f);
        auto flag = [&](bool& b) { if (hasI) b = i != 0; };
        if (k == "device") s.device = v;
        else if (k == "orientation") { if (hasI && i >= 0 && i < 4) s.orientation = (Orientation)i; }
        else if (k == "rotation_lock") flag(s.rotationLock);
        else if (k == "zoom") { if (hasI && i >= 0 && i < 3) s.zoom = (Zoom)i; }
        else if (k == "zoom_percent") { if (hasF) s.zoomPercent = Clampf(f, 5.0f, 800.0f); }
        else if (k == "safe_area") flag(s.safeArea);
        else if (k == "status_bar") flag(s.statusBar);
        else if (k == "touch_input") flag(s.touchInput);
        else if (k == "touch_points") flag(s.touchPoints);
        else if (k == "keyboard") flag(s.keyboard);
        else if (k == "dark_theme") flag(s.darkTheme);
        else if (k == "device_resolution") flag(s.deviceResolution);
        else if (k == "fps_cap") { if (hasI) s.fpsCap = std::clamp(i, 0, 240); }
        else if (k == "network") { if (hasI && i >= 0 && i < 3) s.network = (Network)i; }
        else if (k == "tilt_pitch") { if (hasF) s.tiltPitch = Clampf(f, -90.0f, 90.0f); }
        else if (k == "tilt_roll") { if (hasF) s.tiltRoll = Clampf(f, -180.0f, 180.0f); }
        else if (k == "battery") { if (hasI) s.battery = std::clamp(i, 0, 100); }
        else if (k == "extra") {
            s.extra.clear();
            std::string part;
            std::istringstream ps(v);
            while (std::getline(ps, part, ',') && (int)s.extra.size() < kMaxExtraDevices) if (!part.empty()) s.extra.push_back(part);
        }
        else if (k == "custom_name") c.name = v;
        else if (k == "custom_platform") { if (hasI && (i == 0 || i == 1)) c.platform = (Platform)i; }
        else if (k == "custom_form") { if (hasI && i >= 0 && i < 3) c.form = (Form)i; }
        else if (k == "custom_width") { if (hasI) c.width = i; }
        else if (k == "custom_height") { if (hasI) c.height = i; }
        else if (k == "custom_ppi") { if (hasF) c.ppi = f; }
        else if (k == "custom_scale") { if (hasF) c.scale = f; }
        else if (k == "custom_corner") { if (hasF) c.cornerPt = f; }
        else if (k == "custom_cutout") { if (hasI && i >= 0 && i < 5) c.cutout = (Cutout)i; }
        else if (k == "custom_cutout_w") { if (hasF) c.cutoutWPt = f; }
        else if (k == "custom_cutout_h") { if (hasF) c.cutoutHPt = f; }
        else if (k == "custom_top") { if (hasF) c.topPt = f; }
        else if (k == "custom_bottom") { if (hasF) c.bottomPt = f; }
        else if (k == "custom_land_side") { if (hasF) c.landSidePt = f; }
        else if (k == "custom_land_bottom") { if (hasF) c.landBottomPt = f; }
        else if (k == "custom_land_top") { if (hasF) c.landTopPt = f; }
        else if (k == "custom_both_sides") flag(c.bothSides);
        else if (k == "custom_nav") { if (hasI && i >= 0 && i < 3) c.nav = (Nav)i; }
    }
    ClampCustomDevice(c);
}

bool ApplyLegacySetting(Settings& s, const std::string& key, int value) {
    if (key == "gamedevice") { s.device = LegacyShortListId(value); return true; }
    if (key == "gamedevice2") { s.device = LegacyDeviceId(value); return true; }
    if (key == "gamedeviceland") { s.orientation = value != 0 ? Orientation::LandscapeLeft : Orientation::Portrait; return true; }
    if (key == "gamesafearea") { s.safeArea = value != 0; return true; }
    return false;
}

} // namespace okay::devemu
