// The Game view's device emulator, the Dear ImGui part (DeviceEmulator.hpp). The device numbers, geometry, touch,
// keyboard, sensor and settings logic are in DeviceEmulatorModel.cpp; this file draws the frame, the status bar, the
// keyboard and the touch points with our own vector code, and hands the game what a phone would.
// Written for this engine from the model's numbers; no simulator or SDK frames, pictures or code.
#include "DeviceEmulator.hpp"
#include "ConsolePanel.hpp"     // ConsoleLog
#include "EditorWidgets.hpp"    // BeginDropdown, ToggleChip, IconButton
#include "MainShared.hpp"       // SaveSettings
#include "ProjectCreate.hpp"    // okayproj::layout (ProjectSettings/, Logs/), WriteTextAtomic, ReadText
#include "okay/Core/Paths.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UISafeArea.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

namespace oked {

namespace dm = okay::devemu;

namespace {

dm::Settings s_set;
std::string s_projectDir;
dm::Placement s_place;                 // the main device's screen in the view (last drawn)
bool s_placed = false;
dm::RotateAnimation s_rot;
dm::TouchEmulator s_touch;
dm::Pt s_lastTouch{-1.0f, -1.0f};
dm::Pt s_pan;
// The keyboard: its layout in game pixels (last drawn), its state, and what it typed.
dm::KeyboardLayout s_kb;
bool s_kbShown = false, s_kbShift = false, s_kbSymbols = false, s_kbHidden = false, s_kbPinned = false;
std::string s_typed;
int s_backspaces = 0;
bool s_enter = false, s_back = false;
// The network condition: the simulator's own values are kept while ours are applied, and put back after.
bool s_netOn = false;
okay::net::SimSettings s_netSaved;
// A screenshot asked for (taken two frames later, once the menu that asked has closed).
std::string s_shotPath;
int s_shotArm = 0;
bool s_wasActive = false;
// The device being drawn now: the main one, or an extra one (DevEmuBeginExtra).
struct Drawing { const dm::DeviceSpec* dev = nullptr; dm::Orientation o = dm::Orientation::Portrait; dm::Placement place; int slot = 1; };
Drawing s_draw;
Drawing s_mainDraw;
okay::UISafeInsetFractions s_mainInsets;
float s_mainCanvasW = 0.0f, s_mainCanvasH = 0.0f, s_mainResScale = 1.0f;
int s_rotVtx = -1;

const dm::DeviceSpec* Device() { return dm::FindDevice(s_set.device, &s_set.custom); }
ImVec2 V(dm::Pt p) { return ImVec2(p.x, p.y); }
dm::Box ToBox(ImVec2 o, ImVec2 s) { return dm::Box{o.x, o.y, s.x, s.y}; }
ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

std::string ShortLabel(const dm::DeviceSpec& d) {
    char b[128];
    std::snprintf(b, sizeof b, "%s   %dx%d", d.name.c_str(), d.width, d.height);
    return b;
}

// Orientations the device itself allows (phones without a home button do not turn upside down).
bool Allowed(const dm::DeviceSpec& d, dm::Orientation o) { return o != dm::Orientation::PortraitUpsideDown || d.upsideDown; }

void Turn(dm::Orientation to) {
    if (to == s_set.orientation) return;
    s_rot.Start(s_set.orientation, to, ImGui::GetTime());
    s_set.orientation = to;
    s_pan = {};
    DevEmuSave();
}
void TurnNext(bool clockwise) {
    const dm::DeviceSpec* d = Device();
    if (!d) return;
    dm::Orientation o = s_set.orientation;
    for (int i = 0; i < 4; ++i) {
        o = clockwise ? dm::TurnRight(o) : dm::TurnLeft(o);
        if (Allowed(*d, o)) break;
    }
    Turn(o);
}

// The device's own frame as a map from the portrait device (u right, v down, 0..1 across the body) to the view: the
// body turns with the device, so a button on its physical right side ends up on top in landscape left.
struct BodyMap {
    ImVec2 b0, b1;
    dm::Orientation o;
    ImVec2 At(float u, float v) const {
        float x = u, y = v;
        switch (o) {
            case dm::Orientation::LandscapeLeft: x = v; y = 1.0f - u; break;
            case dm::Orientation::LandscapeRight: x = 1.0f - v; y = u; break;
            case dm::Orientation::PortraitUpsideDown: x = 1.0f - u; y = 1.0f - v; break;
            default: break;
        }
        return ImVec2(b0.x + (b1.x - b0.x) * x, b0.y + (b1.y - b0.y) * y);
    }
    void Rect(ImDrawList* dl, float u0, float v0, float u1, float v1, ImU32 c, float r) const {
        const ImVec2 a = At(u0, v0), b = At(u1, v1);
        dl->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), c, r);
    }
};

// The bezel around the screen in view units: thin all round, a forehead and a chin on a home-button device.
void Bezels(const dm::DeviceSpec& d, dm::Orientation o, float shortSide, float& l, float& t, float& r, float& b) {
    const float bezel = std::max(6.0f, shortSide * (d.form == dm::Form::Tablet ? 0.045f : 0.035f));
    l = t = r = b = bezel;
    if (d.cutout == dm::Cutout::HomeButton) {
        const float big = shortSide * (d.form == dm::Form::Tablet ? 0.09f : 0.20f);
        if (dm::IsLandscape(o)) l = r = big; else t = b = big;
    }
}

void DrawText(ImDrawList* dl, float size, ImVec2 centre, ImU32 c, const char* text) {
    ImFont* f = ImGui::GetFont();
    const ImVec2 ts = f->CalcTextSizeA(size, 1e9f, 0.0f, text);
    dl->AddText(f, size, ImVec2(centre.x - ts.x * 0.5f, centre.y - ts.y * 0.5f), c, text);
}

// The status bar: the clock, the signal bars and the battery, in the device's two ears beside the camera.
void DrawStatusBar(ImDrawList* dl, const dm::DeviceSpec& d, dm::Orientation o, const dm::Placement& p, bool dark) {
    const float hPx = dm::StatusBarPx(d, o);
    if (hPx <= 0.0f) return;
    const float hs = hPx * p.scale;
    if (hs < 6.0f) return;
    const dm::Box& s = p.screen;
    const ImU32 fg = dark ? Col(245, 246, 250) : Col(18, 18, 22);
    const float barH = std::min(hs, 44.0f * d.scale * p.scale);   // the content row (iOS puts it in the ears' middle)
    const float cy = s.y + (d.platform == dm::Platform::Ios ? hs * 0.55f : hs * 0.5f);
    const float txt = std::max(8.0f, barH * (d.platform == dm::Platform::Ios ? 0.36f : 0.42f));
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    const std::string clock = dm::StatusClock(lt.tm_hour, lt.tm_min, d.platform);
    // The ears: left of the camera and right of it (the whole width when there is none in this orientation).
    float earL0 = s.x, earL1 = s.x + s.w * 0.5f, earR0 = s.x + s.w * 0.5f, earR1 = s.Right();
    const dm::CutoutShape cut = dm::CutoutPx(d, o);
    if (o == dm::Orientation::Portrait && cut.box.w > 0) {
        earL1 = s.x + cut.box.x * p.scale;
        earR0 = s.x + cut.box.Right() * p.scale;
    }
    const float corner = dm::CornerPx(d) * p.scale * 0.6f;
    earL0 += std::max(corner, s.w * 0.04f); earR1 -= std::max(corner, s.w * 0.04f);
    if (d.platform == dm::Platform::Ios) {
        DrawText(dl, txt, ImVec2((earL0 + earL1) * 0.5f, cy), fg, clock.c_str());
    } else {
        ImFont* f = ImGui::GetFont();
        const ImVec2 ts = f->CalcTextSizeA(txt, 1e9f, 0.0f, clock.c_str());
        dl->AddText(f, txt, ImVec2(earL0, cy - ts.y * 0.5f), fg, clock.c_str());
    }
    // Right ear, right to left: battery, signal.
    const float bh = txt * 0.62f, bw = bh * 2.1f;
    float x = d.platform == dm::Platform::Ios ? (earR0 + earR1) * 0.5f + bw * 1.2f : earR1;
    const int level = std::clamp(s_set.battery, 0, 100);
    char pct[8];
    std::snprintf(pct, sizeof pct, "%d", level);
    if (d.platform == dm::Platform::Android) {   // Android writes the percentage beside the battery
        ImFont* f = ImGui::GetFont();
        const ImVec2 ts = f->CalcTextSizeA(txt * 0.85f, 1e9f, 0.0f, pct);
        x -= ts.x;
        dl->AddText(f, txt * 0.85f, ImVec2(x, cy - ts.y * 0.5f), fg, pct);
        x -= 4.0f;
    }
    const ImVec2 b0(x - bw, cy - bh * 0.5f), b1(x, cy + bh * 0.5f);
    dl->AddRect(b0, b1, fg, bh * 0.25f, 0, std::max(1.0f, bh * 0.09f));
    dl->AddRectFilled(ImVec2(b1.x + 1.0f, cy - bh * 0.2f), ImVec2(b1.x + 1.0f + bh * 0.12f, cy + bh * 0.2f), fg, 1.0f);
    const float in = std::max(1.5f, bh * 0.16f);
    const ImU32 fill = level <= 20 ? Col(235, 70, 60) : fg;
    dl->AddRectFilled(ImVec2(b0.x + in, b0.y + in), ImVec2(b0.x + in + (bw - 2 * in) * (float)level / 100.0f, b1.y - in), fill, bh * 0.12f);
    // Signal: four bars, rising.
    float sx = b0.x - bh * 0.9f;
    const float barW = bh * 0.22f;
    for (int i = 3; i >= 0; --i) {
        const float bhI = bh * (0.35f + 0.22f * (float)i);
        const bool on = s_set.network != dm::Network::Offline && (s_set.network != dm::Network::Slow || i < 2);
        dl->AddRectFilled(ImVec2(sx - barW, cy + bh * 0.5f - bhI), ImVec2(sx, cy + bh * 0.5f), on ? fg : (fg & 0x00FFFFFF) | 0x50000000, 1.0f);
        sx -= barW * 1.6f;
    }
    if (s_set.network == dm::Network::Offline) DrawText(dl, txt * 0.7f, ImVec2(sx - txt * 0.9f, cy), fg, "x");
}

// The home indicator, or Android's three buttons (back works: it is the game's Escape, as on Android).
void DrawNavigation(ImDrawList* dl, const dm::DeviceSpec& d, dm::Orientation o, const dm::Placement& p, bool dark, bool clicks) {
    const float hPx = dm::HomeBarPx(d, o);
    if (hPx <= 0.0f || d.nav == dm::Nav::HomeButton) return;
    const dm::Box& s = p.screen;
    const float h = hPx * p.scale, y0 = s.Bottom() - h, cy = y0 + h * 0.5f;
    if (d.nav == dm::Nav::Gesture) {
        const float L = std::min(s.w, s.h) * 0.36f * (dm::IsLandscape(o) ? 0.8f : 1.0f), W = std::max(3.0f, h * 0.16f);
        dl->AddRectFilled(ImVec2(s.x + s.w * 0.5f - L * 0.5f, cy - W * 0.5f), ImVec2(s.x + s.w * 0.5f + L * 0.5f, cy + W * 0.5f),
                          dark ? Col(240, 240, 245, 200) : Col(20, 20, 24, 200), W * 0.5f);
        return;
    }
    dl->AddRectFilled(ImVec2(s.x, y0), ImVec2(s.Right(), s.Bottom()), dark ? Col(0, 0, 0, 255) : Col(244, 244, 248, 255));
    const ImU32 ic = dark ? Col(230, 230, 236) : Col(40, 40, 46);
    const float r = h * 0.2f;
    const ImVec2 back(s.x + s.w * 0.25f, cy), home(s.x + s.w * 0.5f, cy), rec(s.x + s.w * 0.75f, cy);
    dl->AddTriangle(ImVec2(back.x - r, back.y), ImVec2(back.x + r * 0.8f, back.y - r), ImVec2(back.x + r * 0.8f, back.y + r), ic, 1.6f);
    dl->AddCircle(home, r, ic, 24, 1.6f);
    dl->AddRect(ImVec2(rec.x - r * 0.85f, rec.y - r * 0.85f), ImVec2(rec.x + r * 0.85f, rec.y + r * 0.85f), ic, 2.0f, 0, 1.6f);
    if (clicks && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 m = ImGui::GetMousePos();
        if (m.y >= y0 && m.y <= s.Bottom() && std::fabs(m.x - back.x) < s.w * 0.12f) s_back = true;
    }
}

void DrawKeyboard(ImDrawList* dl, const dm::Placement& p, bool dark, bool clicks) {
    const dm::Box& s = p.screen;
    const ImVec2 a0(s.x + s_kb.area.x, s.y + s_kb.area.y), a1(s.x + s_kb.area.Right(), s.y + s_kb.area.Bottom());
    dl->AddRectFilled(a0, a1, dark ? Col(38, 38, 42, 248) : Col(208, 211, 218, 248));
    const ImVec2 m = ImGui::GetMousePos();
    const dm::Pt mg = dm::ViewToGame(p, dm::Pt{m.x, m.y});
    const dm::KeyCap* hot = dm::KeyAt(s_kb, mg);
    for (const dm::KeyCap& k : s_kb.keys) {
        const bool special = k.kind != dm::KeyKind::Char && k.kind != dm::KeyKind::Space;
        ImU32 bg = dark ? (special ? Col(70, 70, 76) : Col(100, 100, 106)) : (special ? Col(172, 177, 188) : Col(255, 255, 255));
        if (k.kind == dm::KeyKind::Shift && s_kbShift) bg = dark ? Col(230, 230, 236) : Col(255, 255, 255);
        if (&k == hot && ImGui::IsMouseDown(ImGuiMouseButton_Left)) bg = dark ? Col(140, 140, 150) : Col(225, 228, 236);
        const ImVec2 k0(s.x + k.box.x, s.y + k.box.y), k1(s.x + k.box.Right(), s.y + k.box.Bottom());
        dl->AddRectFilled(ImVec2(k0.x, k0.y + 1.0f), ImVec2(k1.x, k1.y + 1.0f), Col(0, 0, 0, 60), k.box.h * 0.14f);
        dl->AddRectFilled(k0, k1, bg, k.box.h * 0.14f);
        const ImU32 fg = (k.kind == dm::KeyKind::Shift && s_kbShift && dark) ? Col(20, 20, 24) : (dark ? Col(240, 240, 244) : Col(16, 16, 20));
        const float ts = std::max(7.0f, k.box.h * (k.kind == dm::KeyKind::Char ? 0.46f : 0.30f));
        DrawText(dl, ts, ImVec2((k0.x + k1.x) * 0.5f, (k0.y + k1.y) * 0.5f), fg, k.label.c_str());
    }
    if (!clicks || !hot || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    switch (hot->kind) {
        case dm::KeyKind::Char:
        case dm::KeyKind::Space:
            s_typed += hot->ch;
            if (s_kbShift && hot->kind == dm::KeyKind::Char) s_kbShift = false;   // one capital, like a phone
            break;
        case dm::KeyKind::Shift: s_kbShift = !s_kbShift; break;
        case dm::KeyKind::Backspace: ++s_backspaces; break;
        case dm::KeyKind::Enter: s_enter = true; break;
        case dm::KeyKind::Symbols: s_kbSymbols = !s_kbSymbols; break;
        case dm::KeyKind::Hide: s_kbHidden = true; s_kbPinned = false; break;
    }
}

void DrawTouchPoints(ImDrawList* dl, const dm::Placement& p, bool hovered) {
    const float r = std::max(10.0f, std::min(p.screen.w, p.screen.h) * 0.035f);
    auto dot = [&](dm::Pt g, bool ghost) {
        const ImVec2 c = V(dm::GameToView(p, g));
        dl->AddCircleFilled(c, r, ghost ? Col(255, 255, 255, 30) : Col(255, 255, 255, 70), 28);
        dl->AddCircle(c, r, ghost ? Col(255, 255, 255, 110) : Col(255, 255, 255, 220), 28, ghost ? 1.2f : 2.0f);
    };
    for (int i = 0; i < s_touch.Fingers(); ++i) dot(s_touch.Finger(i), false);
    // While the second-finger key is held over the screen: where the two fingers would land.
    if (s_touch.Fingers() == 0 && hovered && ImGui::GetIO().KeyAlt) {
        const ImVec2 m = ImGui::GetMousePos();
        const dm::Pt g = dm::ViewToGame(p, dm::Pt{m.x, m.y});
        dot(g, true);
        dot(dm::TouchEmulator::Mirror(g, dm::Pt{p.screen.w * 0.5f, p.screen.h * 0.5f}), true);
    }
}

void ApplyNetwork() {
    okay::net::SimSettings& sim = okay::net::Simulator();
    const dm::NetValues v = dm::NetworkValues(s_set.network);
    const bool want = DevEmuActive() && v.enabled;
    if (want && !s_netOn) { s_netSaved = sim; s_netOn = true; }
    if (want) { sim.enabled = true; sim.latencyMs = v.latencyMs; sim.jitterMs = v.jitterMs; sim.loss = v.loss; }
    else if (s_netOn) { sim = s_netSaved; s_netOn = false; }
}

std::string SettingsPath() {
    if (s_projectDir.empty()) return {};
    return okayproj::layout::SettingsFile(std::filesystem::path(s_projectDir), dm::kSettingsFile, dm::kLegacySettingsFile).string();
}

void RequestShot() {
    const dm::DeviceSpec* d = Device();
    if (!d) return;
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    const std::string name = dm::ScreenshotName(d->name, s_set.orientation, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    // Never in Assets (pictures are not project content): the project's Logs/Screenshots, or the user's log folder.
    const std::filesystem::path p = okayproj::layout::LogFile(s_projectDir, okay::Paths::LogDir(), std::string("Screenshots/") + name);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    s_shotPath = p.string();
    s_shotArm = 2;
}

const char* kNoDevice = "Off (no device)";

void DeviceMenu() {
    if (ImGui::Selectable(kNoDevice, s_set.device.empty())) { s_set.device.clear(); DevEmuSave(); }
    struct Group { const char* title; dm::Platform p; dm::Form f; bool anyPlatform; };
    const Group groups[] = {
        {"iPhone", dm::Platform::Ios, dm::Form::Phone, false},
        {"Android phones", dm::Platform::Android, dm::Form::Phone, false},
        {"Foldables", dm::Platform::Android, dm::Form::Foldable, true},
        {"iPad", dm::Platform::Ios, dm::Form::Tablet, false},
        {"Android tablets", dm::Platform::Android, dm::Form::Tablet, false},
    };
    for (const Group& g : groups) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", g.title);
        for (const dm::DeviceSpec& d : dm::Devices()) {
            if (d.form != g.f || (!g.anyPlatform && d.platform != g.p)) continue;
            const std::string lbl = ShortLabel(d) + "##" + d.id;
            if (ImGui::Selectable(lbl.c_str(), s_set.device == d.id)) {
                s_set.device = d.id;
                if (!Allowed(d, s_set.orientation)) s_set.orientation = dm::Orientation::Portrait;
                s_pan = {};
                DevEmuSave();
            }
        }
    }
    ImGui::Separator();
    const std::string lbl = ShortLabel(s_set.custom) + "##custom";
    if (ImGui::Selectable(lbl.c_str(), s_set.device == "custom")) { s_set.device = "custom"; DevEmuSave(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Your own device: edit it in the \"...\" menu, Custom device.");
}

bool DeviceCombo(const char* id, std::string& pick) {
    bool changed = false;
    const dm::DeviceSpec* cur = dm::FindDevice(pick, &s_set.custom);
    ImGui::SetNextItemWidth(220);
    if (BeginDropdown(id, cur ? cur->name.c_str() : "(none)")) {
        if (ImGui::Selectable("(none)", pick.empty())) { pick.clear(); changed = true; }
        for (const dm::DeviceSpec& d : dm::Devices())
            if (ImGui::Selectable((ShortLabel(d) + "##x" + d.id).c_str(), pick == d.id)) { pick = d.id; changed = true; }
        if (ImGui::Selectable("Custom device##xcustom", pick == "custom")) { pick = "custom"; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

// A small square: drag in it to tilt the device (left / right = roll, up / down = pitch). Double-click levels it.
bool TiltPad() {
    const float sz = 110.0f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tiltpad", ImVec2(sz, sz));
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const ImVec2 m = ImGui::GetMousePos();
        const float u = std::clamp((m.x - p0.x) / sz * 2.0f - 1.0f, -1.0f, 1.0f), v = std::clamp((m.y - p0.y) / sz * 2.0f - 1.0f, -1.0f, 1.0f);
        s_set.tiltRoll = u * 90.0f; s_set.tiltPitch = v * 90.0f;
        changed = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { s_set.tiltRoll = s_set.tiltPitch = 0.0f; changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to tilt the device (left / right rolls it, up / down tips it back).\nDouble-click to hold it upright again.");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + sz, p0.y + sz), c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    dl->AddRectFilled(p0, p1, Col(30, 32, 38), 6.0f);
    dl->AddRect(p0, p1, Col(90, 94, 106), 6.0f);
    dl->AddLine(ImVec2(c.x, p0.y + 4), ImVec2(c.x, p1.y - 4), Col(70, 74, 84));
    dl->AddLine(ImVec2(p0.x + 4, c.y), ImVec2(p1.x - 4, c.y), Col(70, 74, 84));
    const float rr = std::clamp(s_set.tiltRoll, -90.0f, 90.0f) / 90.0f, pp = std::clamp(s_set.tiltPitch, -90.0f, 90.0f) / 90.0f;
    dl->AddCircleFilled(ImVec2(c.x + rr * sz * 0.5f, c.y + pp * sz * 0.5f), 6.0f, Col(90, 170, 255));
    return changed;
}

void CustomEditor() {
    dm::DeviceSpec& c = s_set.custom;
    bool ch = false;
    char name[96];
    std::snprintf(name, sizeof name, "%s", c.name.c_str());
    if (ImGui::InputText("Name##cd", name, sizeof name)) { c.name = name; ch = true; }
    int plat = (int)c.platform, form = (int)c.form, cut = (int)c.cutout, nav = (int)c.nav;
    ch |= ImGui::Combo("Platform##cd", &plat, "iOS\0Android\0");
    ch |= ImGui::Combo("Kind##cd", &form, "Phone\0Foldable\0Tablet\0");
    ch |= ImGui::DragInt("Width px##cd", &c.width, 4.0f, 64, 8192);
    ch |= ImGui::DragInt("Height px##cd", &c.height, 4.0f, 64, 8192);
    ch |= ImGui::DragFloat("Pixels per inch##cd", &c.ppi, 1.0f, 50.0f, 1000.0f, "%.0f");
    ch |= ImGui::DragFloat("Pixels per point / dp##cd", &c.scale, 0.01f, 0.5f, 5.0f, "%.3f");
    ch |= ImGui::DragFloat("Corner radius##cd", &c.cornerPt, 0.5f, 0.0f, 200.0f, "%.0f pt");
    ch |= ImGui::Combo("Camera cut-out##cd", &cut, "None\0Notch\0Pill (island)\0Punch hole\0Home button\0");
    ch |= ImGui::DragFloat("Cut-out width##cd", &c.cutoutWPt, 0.5f, 0.0f, 400.0f, "%.0f pt");
    ch |= ImGui::DragFloat("Cut-out height##cd", &c.cutoutHPt, 0.5f, 0.0f, 100.0f, "%.0f pt");
    ch |= ImGui::DragFloat("Top inset (portrait)##cd", &c.topPt, 0.5f, 0.0f, 200.0f, "%.0f pt");
    ch |= ImGui::DragFloat("Bottom inset (portrait)##cd", &c.bottomPt, 0.5f, 0.0f, 200.0f, "%.0f pt");
    ch |= ImGui::DragFloat("Camera side (landscape)##cd", &c.landSidePt, 0.5f, 0.0f, 200.0f, "%.0f pt");
    ch |= ImGui::DragFloat("Bottom inset (landscape)##cd", &c.landBottomPt, 0.5f, 0.0f, 200.0f, "%.0f pt");
    ch |= ImGui::DragFloat("Status bar (landscape)##cd", &c.landTopPt, 0.5f, 0.0f, 200.0f, "%.0f pt");
    ch |= ImGui::Checkbox("Inset both sides in landscape##cd", &c.bothSides);
    ch |= ImGui::Combo("Navigation##cd", &nav, "Gesture bar\0Three buttons\0Home button\0");
    ch |= ImGui::Checkbox("Turns upside down##cd", &c.upsideDown);
    if (ch) {
        c.platform = (dm::Platform)plat; c.form = (dm::Form)form; c.cutout = (dm::Cutout)cut; c.nav = (dm::Nav)nav;
        dm::ClampCustomDevice(c);
        DevEmuSave();
    }
    if (ImGui::Button("Use the custom device")) { s_set.device = "custom"; DevEmuSave(); }
    ImGui::SameLine();
    if (const dm::DeviceSpec* d = Device(); d && d != &c && ImGui::Button("Copy the current device")) {
        c = *d; c.name = d->name + " (custom)";
        dm::ClampCustomDevice(c);
        DevEmuSave();
    }
}

void OptionsMenu(bool playing) {
    const dm::DeviceSpec* d = Device();
    if (!d) return;
    bool ch = false;
    ImGui::TextDisabled("%s  -  %s %s, %d x %d, %.0f ppi", d->name.c_str(), dm::PlatformName(d->platform), dm::FormName(d->form), d->width, d->height, d->ppi);
    ImGui::SeparatorText("Turn");
    for (int i = 0; i < 4; ++i) {
        const dm::Orientation o = (dm::Orientation)i;
        ImGui::BeginDisabled(!Allowed(*d, o));
        if (ImGui::RadioButton(dm::OrientationName(o), s_set.orientation == o)) Turn(o);
        ImGui::EndDisabled();
        if (i % 2 == 0) ImGui::SameLine(170);
    }
    bool autoRot = !s_set.rotationLock;
    if (ImGui::Checkbox("Auto-rotate (follows the tilt below)", &autoRot)) { s_set.rotationLock = !autoRot; ch = true; }
    ImGui::SeparatorText("Screen");
    ch |= ImGui::Checkbox("Safe area tint", &s_set.safeArea);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tint the parts the camera, the rounded corners, the status bar and the home bar\ncover: keep buttons and text out of them. UI Safe Area containers always keep clear.");
    ch |= ImGui::Checkbox("Status bar (time, signal, battery)", &s_set.statusBar);
    ch |= ImGui::Checkbox("Dark system theme", &s_set.darkTheme);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The phone's dark mode: the status bar, keyboard and navigation bar turn dark,\nand scripts read Screen.darkMode (screen_dark_mode()).");
    ch |= ImGui::Checkbox("Render at the device's resolution", &s_set.deviceResolution);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draw the 3D picture at the phone's own pixels (sharp as on the device),\nthen scale it to the view. Off: at the size shown (faster).");
    ch |= ImGui::SliderInt("Battery", &s_set.battery, 0, 100, "%d%%");
    ImGui::SeparatorText("Touch and keyboard");
    ch |= ImGui::Checkbox("The mouse is a finger", &s_set.touchInput);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("While playing, clicks and drags on the screen reach the game as touches (as on the phone):\nno hover, the left button only. Hold Alt for a second finger (pinch and rotate around the centre),\nAlt+Shift to move both fingers together (two-finger pan).");
    ch |= ImGui::Checkbox("Show touch points", &s_set.touchPoints);
    ch |= ImGui::Checkbox("On-screen keyboard for text fields", &s_set.keyboard);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("When a text field in the game has the keyboard, the phone's keyboard opens and covers\nthe bottom of the screen; the safe area shrinks above it (Screen.keyboardHeight).");
    if (ImGui::Button(s_kbPinned ? "Close the keyboard" : "Open the keyboard now")) { s_kbPinned = !s_kbPinned; s_kbHidden = false; }
    if (d->platform == dm::Platform::Android) {
        ImGui::SameLine();
        if (ImGui::Button("Back")) s_back = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Android's Back: the game gets Escape for one frame.");
    }
    ImGui::SeparatorText("Motion sensor");
    if (TiltPad()) ch = true;
    ImGui::SameLine();
    ImGui::BeginGroup();
    ch |= ImGui::SliderFloat("Roll##tilt", &s_set.tiltRoll, -180.0f, 180.0f, "%.0f deg");
    ch |= ImGui::SliderFloat("Tip back##tilt", &s_set.tiltPitch, -90.0f, 90.0f, "%.0f deg");
    if (ImGui::Button("Level##tilt")) { s_set.tiltRoll = s_set.tiltPitch = 0.0f; ch = true; }
    const dm::Vec3f g = dm::TiltToGravity(s_set.tiltPitch, s_set.tiltRoll);
    ImGui::TextDisabled("Input.acceleration %.2f, %.2f, %.2f", g.x, g.y, g.z);
    ImGui::EndGroup();
    ImGui::SeparatorText("Conditions");
    {
        static const int kCaps[] = {0, 60, 30, 20, 15};
        static const char* kCapNames[] = {"Full speed", "60 fps", "30 fps (older phone)", "20 fps (slow phone)", "15 fps"};
        int sel = 0;
        for (int i = 0; i < 5; ++i) if (kCaps[i] == s_set.fpsCap) sel = i;
        ImGui::SetNextItemWidth(200);
        if (ImGui::Combo("Frame rate while playing", &sel, kCapNames, 5)) { s_set.fpsCap = kCaps[sel]; ch = true; }
        int net = (int)s_set.network;
        ImGui::SetNextItemWidth(200);
        if (ImGui::Combo("Network", &net, "Online\0Slow (weak signal)\0Offline\0")) { s_set.network = (dm::Network)net; ch = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Through the network simulator (View > Panels > Net): Slow adds 300 ms delay and drops\n3%% of packets, Offline drops every packet. Web requests are not affected.");
    }
    ImGui::SeparatorText("More devices beside it");
    for (int i = 0; i < dm::kMaxExtraDevices; ++i) {
        std::string pick = i < (int)s_set.extra.size() ? s_set.extra[(std::size_t)i] : std::string();
        char id[24];
        std::snprintf(id, sizeof id, "##extra%d", i);
        if (DeviceCombo(id, pick)) {
            s_set.extra.resize(std::max<std::size_t>(s_set.extra.size(), (std::size_t)i + 1));
            s_set.extra[(std::size_t)i] = pick;
            std::vector<std::string> kept;
            for (const std::string& e : s_set.extra) if (!e.empty()) kept.push_back(e);
            s_set.extra = kept;
            ch = true;
        }
        if (i >= (int)s_set.extra.size()) break;   // one empty row at a time
    }
    ImGui::SeparatorText("Screenshot");
    if (ImGui::Button("Save a screenshot of the screen")) { RequestShot(); ImGui::CloseCurrentPopup(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The device's screen as a PNG in the project's Logs/Screenshots folder (never in Assets).");
    if (ImGui::CollapsingHeader("Custom device")) CustomEditor();
    (void)playing;
    if (ch) DevEmuSave();
}

}  // namespace

// ---- the device and its settings ----
bool DevEmuActive() { return Device() != nullptr; }
const dm::DeviceSpec* DevEmuDevice() { return Device(); }
dm::Orientation DevEmuOrientation() { return s_set.orientation; }
void DevEmuScreenPixels(int& w, int& h) {
    w = h = 0;
    if (const dm::DeviceSpec* d = Device()) dm::ScreenPixels(*d, s_set.orientation, w, h);
}

bool DevEmuLoadLegacySetting(const std::string& key, int value) {
    if (!s_projectDir.empty()) {   // the project's own file decides; the old keys only say what they are
        dm::Settings ignore;
        return dm::ApplyLegacySetting(ignore, key, value);
    }
    return dm::ApplyLegacySetting(s_set, key, value);
}
std::string DevEmuLegacySettingLines() {
    std::string o;
    o += "gamedevice2 " + std::to_string(dm::LegacyDeviceIndex(s_set.device)) + "\n";
    o += "gamedeviceland " + std::to_string(dm::IsLandscape(s_set.orientation) ? 1 : 0) + "\n";
    o += "gamesafearea " + std::to_string(s_set.safeArea ? 1 : 0) + "\n";
    return o;
}
void DevEmuLoadProject(const std::string& projectDir) {
    s_projectDir = projectDir;
    const std::string path = SettingsPath();
    std::string text;
    if (!path.empty() && okayproj::ReadText(path, text)) {
        dm::Settings fresh;
        dm::ParseSettings(text, fresh);
        s_set = fresh;
    }
    if (const dm::DeviceSpec* d = Device(); d && !Allowed(*d, s_set.orientation)) s_set.orientation = dm::Orientation::Portrait;
}
void DevEmuSave() {
    SaveSettings();   // the old keys in okay_settings.txt, for an older editor
    const std::string path = SettingsPath();
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::string err;
    if (!okayproj::WriteTextAtomic(path, dm::WriteSettings(s_set), &err)) ConsoleLog("Couldn't save the device emulator settings: " + err, 1);
}

// ---- the Game view ----
bool DevEmuToolbar(bool playing) {
    const dm::DeviceSpec* d = Device();
    ImGui::SetNextItemWidth(170);
    if (BeginDropdown("##device", d ? d->name.c_str() : "Device: off")) { DeviceMenu(); ImGui::EndCombo(); }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Device emulator: play the game on a phone or tablet screen: its real resolution and pixel density,\n"
                          "rounded corners, camera cut-out, status bar, safe area and keyboard. The mouse is your finger.");
    d = Device();
    if (!d) return false;
    ImGui::SameLine();
    if (IconButton("##devrot", TbIcon::Rotate, "Turn the device a quarter (right-click: the other way)")) TurnNext(false);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) TurnNext(true);
    ImGui::SameLine();
    char zl[32];
    if (s_set.zoom == dm::Zoom::Fit) std::snprintf(zl, sizeof zl, "Fit");
    else if (s_set.zoom == dm::Zoom::Actual) std::snprintf(zl, sizeof zl, "100%%");
    else std::snprintf(zl, sizeof zl, "%.0f%%", s_set.zoomPercent);
    ImGui::SetNextItemWidth(78);
    if (BeginDropdown("##devzoom", zl)) {
        bool ch = false;
        if (ImGui::Selectable("Fit", s_set.zoom == dm::Zoom::Fit)) { s_set.zoom = dm::Zoom::Fit; ch = true; }
        if (ImGui::Selectable("100% (one device pixel per screen pixel)", s_set.zoom == dm::Zoom::Actual)) { s_set.zoom = dm::Zoom::Actual; ch = true; }
        static const float kZ[] = {25, 33, 50, 75, 150, 200};
        for (float z : kZ) {
            char l[16];
            std::snprintf(l, sizeof l, "%.0f%%", z);
            if (ImGui::Selectable(l, s_set.zoom == dm::Zoom::Percent && s_set.zoomPercent == z)) { s_set.zoom = dm::Zoom::Percent; s_set.zoomPercent = z; ch = true; }
        }
        if (ch) { s_pan = {}; DevEmuSave(); }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom: Fit the view, or the real size. A zoomed screen larger than the view: drag the dark area around it to move it.");
    if (d->platform == dm::Platform::Android) {
        ImGui::SameLine();
        if (ImGui::Button("Back##devback")) s_back = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Android's Back button: the game gets Escape for one frame.");
    }
    ImGui::SameLine();
    if (IconButton("##devmore", TbIcon::More, "Device options: turn, safe area, status bar, theme, touch, keyboard, tilt,\nslow device, network, more devices, screenshot, custom device"))
        ImGui::OpenPopup("##devoptions");
    if (ImGui::BeginPopup("##devoptions")) { OptionsMenu(playing); ImGui::EndPopup(); }
    return true;
}

bool DevEmuPlace(ImVec2 origin, ImVec2 avail, bool fitOnly, ImVec2& pos, ImVec2& size, float& resScale) {
    const dm::DeviceSpec* d = Device();
    if (!d) { s_placed = false; return false; }
    int w = 0, h = 0;
    dm::ScreenPixels(*d, s_set.orientation, w, h);
    const dm::Box view = ToBox(origin, avail);
    const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
    const dm::Zoom z = fitOnly ? dm::Zoom::Fit : s_set.zoom;
    // Drag the dark area around a zoomed screen to move it.
    if (!fitOnly && z != dm::Zoom::Fit && ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && s_placed) {
        const ImVec2 start = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
        if (view.Contains(dm::Pt{start.x, start.y}) && !s_place.screen.Contains(dm::Pt{start.x, start.y})) {
            s_pan.x += ImGui::GetIO().MouseDelta.x; s_pan.y += ImGui::GetIO().MouseDelta.y;
        }
    }
    const dm::Placement p = dm::PlaceScreen(view, w, h, z, s_set.zoomPercent, fb, s_pan, dm::FrameRoom(view));
    pos = ImVec2(p.screen.x, p.screen.y);
    size = ImVec2(p.screen.w, p.screen.h);
    resScale = p.scale;
    if (!fitOnly) {
        s_place = p; s_placed = true;
        s_draw = Drawing{d, s_set.orientation, p, 1};
        s_mainDraw = s_draw;
    }
    return true;
}

int DevEmuExtraCount() {
    if (!Device()) return 0;
    int n = 0;
    for (const std::string& e : s_set.extra) if (dm::FindDevice(e, &s_set.custom)) ++n;
    return std::min(n, dm::kMaxExtraDevices);
}

ImVec2 DevEmuCell(ImVec2 origin, ImVec2 avail, int cell, ImVec2& cellSize) {
    const std::vector<dm::Box> cells = dm::SplitView(ToBox(origin, avail), 1 + DevEmuExtraCount(), 8.0f);
    const dm::Box& b = cells[(std::size_t)std::clamp(cell, 0, (int)cells.size() - 1)];
    cellSize = ImVec2(b.w, b.h);
    return ImVec2(b.x, b.y);
}

bool DevEmuBeginExtra(int i, ImVec2 origin, ImVec2 avail, ImVec2& pos, ImVec2& size, float& resScale) {
    int seen = 0;
    const dm::DeviceSpec* d = nullptr;
    for (const std::string& e : s_set.extra)
        if (const dm::DeviceSpec* x = dm::FindDevice(e, &s_set.custom)) { if (seen == i) { d = x; break; } ++seen; }
    if (!d) return false;
    s_mainInsets = okay::UISafeInsets();
    s_mainCanvasW = okay::UICanvas::Width(); s_mainCanvasH = okay::UICanvas::Height();
    s_mainResScale = okay::UIResolutionScale();
    // The extra device takes the main one's turn when it can (a phone that does not turn upside down stays upright).
    dm::Orientation o = Allowed(*d, s_set.orientation) ? s_set.orientation : dm::Orientation::Portrait;
    int w = 0, h = 0;
    dm::ScreenPixels(*d, o, w, h);
    const dm::Box view = ToBox(origin, avail);
    const dm::Placement p = dm::PlaceScreen(view, w, h, dm::Zoom::Fit, 100, 1.0f, {}, dm::FrameRoom(view));
    s_draw = Drawing{d, o, p, 4 + std::clamp(i, 0, 2)};
    pos = ImVec2(p.screen.x, p.screen.y); size = ImVec2(p.screen.w, p.screen.h); resScale = p.scale;
    const dm::ScreenReport r = dm::MakeScreenReport(*d, o, p, 0.0f, s_set.darkTheme);
    okay::UICanvas::Set(r.width, r.height);
    okay::UISafeInsets() = okay::UISafeInsetFractions{r.safe.left, r.safe.top, r.safe.right, r.safe.bottom};
    okay::UIResolutionScale() = p.scale;
    return true;
}

void DevEmuEndExtra(ImDrawList* dl) {
    if (s_draw.dev && dl) {   // its name under the screen
        const dm::Box& s = s_draw.place.screen;
        DrawText(dl, ImGui::GetFontSize() * 0.9f, ImVec2(s.x + s.w * 0.5f, s.Bottom() + ImGui::GetFontSize() * 1.6f), Col(170, 176, 190), s_draw.dev->name.c_str());
    }
    okay::UISafeInsets() = s_mainInsets;
    okay::UICanvas::Set(s_mainCanvasW, s_mainCanvasH);
    okay::UIResolutionScale() = s_mainResScale;
    s_draw = s_mainDraw;
}

void DevEmuDrawBody(ImDrawList* dl, ImVec2 sp, ImVec2 se) {
    const dm::DeviceSpec* d = s_draw.dev;
    if (!d) return;
    const dm::Orientation o = s_draw.o;
    const float shortSide = std::min(se.x - sp.x, se.y - sp.y);
    float bl, bt, br, bb;
    Bezels(*d, o, shortSide, bl, bt, br, bb);
    const ImVec2 b0(sp.x - bl, sp.y - bt), b1(se.x + br, se.y + bb);
    const float bodyR = dm::CornerPx(*d) * s_draw.place.scale + std::min(bl, bt);
    const bool ios = d->platform == dm::Platform::Ios;
    const ImU32 body = ios ? Col(52, 52, 56) : Col(26, 27, 31), rim = ios ? Col(150, 150, 158) : Col(84, 88, 100);
    dl->AddRectFilled(ImVec2(b0.x + 3, b0.y + 6), ImVec2(b1.x + 3, b1.y + 6), Col(0, 0, 0, 90), bodyR + 4);   // shadow
    dl->AddRectFilled(b0, b1, body, bodyR + 2);
    dl->AddRect(b0, b1, rim, bodyR + 2, 0, 2.0f);
    // The side buttons, on the device's own sides (they turn with it): iPhone volume on the left and power on the
    // right; Android volume and power both on the right. Tablets: on the top edge.
    const BodyMap m{ImVec2(b0.x - 3, b0.y - 3), ImVec2(b1.x + 3, b1.y + 3), o};
    const ImU32 btn = Col(70, 73, 82);
    // 3 view units across the edge, as a fraction of the body (u across the device's width, v along its height).
    const float eu = dm::IsLandscape(o) ? 3.0f / (b1.y - b0.y + 6.0f) : 3.0f / (b1.x - b0.x + 6.0f);
    const float ev = dm::IsLandscape(o) ? 3.0f / (b1.x - b0.x + 6.0f) : 3.0f / (b1.y - b0.y + 6.0f);
    if (d->form == dm::Form::Tablet) {
        m.Rect(dl, 0.70f, 0.0f, 0.82f, ev, btn, 1.5f);
        m.Rect(dl, 1.0f - eu, 0.10f, 1.0f, 0.16f, btn, 1.5f);
        m.Rect(dl, 1.0f - eu, 0.17f, 1.0f, 0.23f, btn, 1.5f);
    } else if (ios) {
        m.Rect(dl, 0.0f, 0.18f, eu, 0.22f, btn, 1.5f);   // action button
        m.Rect(dl, 0.0f, 0.26f, eu, 0.33f, btn, 1.5f);
        m.Rect(dl, 0.0f, 0.35f, eu, 0.42f, btn, 1.5f);
        m.Rect(dl, 1.0f - eu, 0.28f, 1.0f, 0.38f, btn, 1.5f);
    } else {
        m.Rect(dl, 1.0f - eu, 0.22f, 1.0f, 0.34f, btn, 1.5f);
        m.Rect(dl, 1.0f - eu, 0.38f, 1.0f, 0.44f, btn, 1.5f);
    }
    if (d->cutout == dm::Cutout::HomeButton) {   // the round home button in the chin, and the earpiece in the forehead
        const ImVec2 hb = dm::IsLandscape(o) ? (o == dm::Orientation::LandscapeLeft ? ImVec2(se.x + br * 0.5f, (sp.y + se.y) * 0.5f)
                                                                                       : ImVec2(sp.x - bl * 0.5f, (sp.y + se.y) * 0.5f))
                                             : (o == dm::Orientation::PortraitUpsideDown ? ImVec2((sp.x + se.x) * 0.5f, sp.y - bt * 0.5f)
                                                                                           : ImVec2((sp.x + se.x) * 0.5f, se.y + bb * 0.5f));
        const float r = shortSide * (d->form == dm::Form::Tablet ? 0.03f : 0.075f);
        dl->AddCircle(hb, r, Col(90, 94, 104), 40, 2.0f);
        const ImVec2 ear(2.0f * ((sp.x + se.x) * 0.5f) - hb.x, 2.0f * ((sp.y + se.y) * 0.5f) - hb.y);
        const bool land = dm::IsLandscape(o);
        const float L = shortSide * 0.16f, W = std::max(2.0f, shortSide * 0.012f);
        dl->AddRectFilled(land ? ImVec2(ear.x - W, ear.y - L * 0.5f) : ImVec2(ear.x - L * 0.5f, ear.y - W),
                          land ? ImVec2(ear.x + W, ear.y + L * 0.5f) : ImVec2(ear.x + L * 0.5f, ear.y + W), Col(60, 62, 70), W);
    }
}

void DevEmuDrawOverlay(ImDrawList* dl, ImVec2 sp, ImVec2 se, bool playing, bool hovered) {
    const dm::DeviceSpec* d = s_draw.dev;
    if (!d) return;
    const dm::Orientation o = s_draw.o;
    const dm::Placement& p = s_draw.place;
    const bool mainDevice = s_draw.slot == 1;
    const bool dark = s_set.darkTheme;
    const float sc = p.scale;
    const dm::Insets ins = dm::SafeInsetsPx(*d, o);
    // The keyboard: open while a game text field has the keyboard (or opened from the menu), until Hide.
    if (mainDevice) {
        const bool want = s_kbPinned || (s_set.keyboard && playing && okay::Input::TextFocused());
        if (!okay::Input::TextFocused() && !s_kbPinned) s_kbHidden = false;
        s_kbShown = want && !s_kbHidden;
        if (s_kbShown)
            s_kb = dm::LayoutKeyboard(p.screen.w, p.screen.h, d->form == dm::Form::Tablet, dm::HomeBarPx(*d, o) * sc, s_kbShift, s_kbSymbols);
        else s_kb = dm::KeyboardLayout{};
    }
    if (s_set.safeArea) {   // the unsafe parts, tinted and outlined
        const ImU32 tint = Col(255, 80, 80, 40), line = Col(255, 110, 110, 150);
        const float l = ins.left * sc, t = ins.top * sc, r = ins.right * sc, b = ins.bottom * sc;
        if (t > 0) { dl->AddRectFilled(sp, ImVec2(se.x, sp.y + t), tint); dl->AddLine(ImVec2(sp.x, sp.y + t), ImVec2(se.x, sp.y + t), line); }
        if (b > 0) { dl->AddRectFilled(ImVec2(sp.x, se.y - b), se, tint); dl->AddLine(ImVec2(sp.x, se.y - b), ImVec2(se.x, se.y - b), line); }
        if (l > 0) { dl->AddRectFilled(ImVec2(sp.x, sp.y + t), ImVec2(sp.x + l, se.y - b), tint); dl->AddLine(ImVec2(sp.x + l, sp.y), ImVec2(sp.x + l, se.y), line); }
        if (r > 0) { dl->AddRectFilled(ImVec2(se.x - r, sp.y + t), ImVec2(se.x, se.y - b), tint); dl->AddLine(ImVec2(se.x - r, sp.y), ImVec2(se.x - r, se.y), line); }
    }
    if (mainDevice && s_kbShown) DrawKeyboard(dl, p, dark, playing || s_kbPinned);
    if (s_set.statusBar) DrawStatusBar(dl, *d, o, p, dark);
    DrawNavigation(dl, *d, o, p, dark, mainDevice);
    // The rounded screen corners: a thick rounded outline in the body's colour over the square corners.
    const ImU32 body = d->platform == dm::Platform::Ios ? Col(52, 52, 56) : Col(26, 27, 31);
    const float rad = dm::CornerPx(*d) * sc;
    if (rad > 1.0f) {
        const float t = rad * 0.75f + 2.0f;
        dl->AddRect(ImVec2(sp.x - t * 0.5f, sp.y - t * 0.5f), ImVec2(se.x + t * 0.5f, se.y + t * 0.5f), body, rad + t * 0.5f, 0, t);
    }
    // The camera cut-out.
    const dm::CutoutShape c = dm::CutoutPx(*d, o);
    if (c.box.w > 0.0f) {
        const ImVec2 c0 = V(dm::DeviceToView(p, dm::Pt{c.box.x, c.box.y})), c1 = V(dm::DeviceToView(p, dm::Pt{c.box.Right(), c.box.Bottom()}));
        const float cr = c.radius * sc;
        if (c.kind == dm::Cutout::Notch) {   // hangs from the edge, its outer corners square
            const ImDrawFlags fl = o == dm::Orientation::Portrait ? ImDrawFlags_RoundCornersBottom
                                 : o == dm::Orientation::PortraitUpsideDown ? ImDrawFlags_RoundCornersTop
                                 : o == dm::Orientation::LandscapeLeft ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersLeft;
            ImVec2 a = c0, b = c1;
            if (o == dm::Orientation::Portrait) a.y -= 1; else if (o == dm::Orientation::PortraitUpsideDown) b.y += 1;
            else if (o == dm::Orientation::LandscapeLeft) a.x -= 1; else b.x += 1;
            dl->AddRectFilled(a, b, body, cr, fl);
        } else if (c.kind == dm::Cutout::PunchHole) {
            dl->AddCircleFilled(ImVec2((c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f), cr, Col(0, 0, 0), 24);
            dl->AddCircle(ImVec2((c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f), cr * 0.45f, Col(30, 34, 52), 16, 1.0f);
        } else {
            dl->AddRectFilled(c0, c1, Col(0, 0, 0), cr);
        }
    }
    if (mainDevice && s_set.touchPoints && s_set.touchInput && playing) DrawTouchPoints(dl, p, hovered);
}

void DevEmuRotateBegin(ImDrawList* dl) { s_rotVtx = (dl && s_rot.Running(ImGui::GetTime())) ? dl->VtxBuffer.size() : -1; }
void DevEmuRotateEnd(ImDrawList* dl, ImVec2 regionPos, ImVec2 regionSize) {
    if (!dl || s_rotVtx < 0) return;
    const float ang = s_rot.Angle(ImGui::GetTime()) * 3.14159265358979f / 180.0f;
    const int from = s_rotVtx;
    s_rotVtx = -1;
    if (std::fabs(ang) < 1e-4f || !s_placed) return;
    // Turn about the screen's centre, scaled down so the turning device stays inside the region (the screen y axis is
    // down, so a counter-clockwise turn is a negative angle in these coordinates).
    const dm::Box& s = s_place.screen;
    const ImVec2 c(s.x + s.w * 0.5f, s.y + s.h * 0.5f);
    const float cs = std::cos(-ang), sn = std::sin(-ang);
    const float bw = std::fabs(s.w * cs) + std::fabs(s.h * sn), bh = std::fabs(s.w * sn) + std::fabs(s.h * cs);
    const float k = std::min(1.0f, std::min(regionSize.x / std::max(1.0f, bw * 1.12f), regionSize.y / std::max(1.0f, bh * 1.12f)));
    (void)regionPos;
    for (int i = from; i < dl->VtxBuffer.size(); ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        const float x = v.pos.x - c.x, y = v.pos.y - c.y;
        v.pos.x = c.x + (x * cs - y * sn) * k;
        v.pos.y = c.y + (x * sn + y * cs) * k;
    }
}

void DevEmuLabelOffsets(float& drop, float& in) {
    drop = in = 0.0f;
    const dm::DeviceSpec* d = Device();
    if (!d || !s_placed) return;
    const dm::Insets ins = dm::SafeInsetsPx(*d, s_set.orientation);
    drop = ins.top * s_place.scale + 4.0f;
    in = dm::CornerPx(*d) * s_place.scale * 0.45f + ins.left * s_place.scale;
}

bool DevEmuRenderSize(float canvasW, float canvasH, float framebufferScale, int& w, int& h) {
    if (!s_draw.dev || !Device()) return false;
    int sw = 0, sh = 0;
    dm::ScreenPixels(*s_draw.dev, s_draw.o, sw, sh);
    dm::Placement p = s_draw.place;
    p.screen.w = canvasW; p.screen.h = canvasH;
    // Extra devices are drawn at their on-screen size (four full-resolution phones a frame would be slow).
    dm::RenderSize(sw, sh, p, framebufferScale, s_set.deviceResolution && s_draw.slot == 1, 4096, w, h);
    return true;
}
int DevEmuRenderSlot() { return Device() && s_draw.dev ? s_draw.slot : 1; }

// ---- the frame loop ----
bool DevEmuFeedPointer(float& x, float& y, unsigned& mask, bool twoFinger, bool pan) {
    const dm::DeviceSpec* d = Device();
    if (!d || !s_set.touchInput || !s_placed) return false;
    dm::PointerFrame in;
    in.pos = dm::Pt{x, y};
    in.down = (mask & 1u) != 0;
    in.twoFinger = twoFinger;
    in.pan = pan && twoFinger;
    const dm::Box screen{0.0f, 0.0f, s_place.screen.w, s_place.screen.h};
    std::vector<dm::TouchEvent> ev;
    s_touch.Update(in, screen, s_kbShown ? s_kb.area : dm::Box{}, ev);
    for (const dm::TouchEvent& e : ev) okay::Input::FeedTouch(e.id, e.state, okay::Vec2{e.pos.x, e.pos.y});
    // A phone's mouse: the first finger, no hover, no right or middle button.
    if (s_touch.Fingers() > 0) { s_lastTouch = s_touch.Finger(0); mask = 1u; }
    else mask = 0u;
    for (const dm::TouchEvent& e : ev) if (e.state == 2 && e.id == ev.front().id) s_lastTouch = e.pos;
    x = s_lastTouch.x; y = s_lastTouch.y;
    return true;
}

std::string DevEmuTakeTyped() { std::string t; t.swap(s_typed); return t; }
int DevEmuTakeBackspaces() { const int n = s_backspaces; s_backspaces = 0; return n; }
bool DevEmuTakeEnter() { const bool e = s_enter; s_enter = false; return e; }
bool DevEmuTakeBack() { const bool b = s_back; s_back = false; return b; }

void DevEmuPublish(bool playing) {
    const dm::DeviceSpec* d = Device();
    const bool active = d && s_placed;
    if (active) {
        // Auto-rotate: the tilt control turns the screen when the lock is off.
        const dm::Vec3f g = dm::TiltToGravity(s_set.tiltPitch, s_set.tiltRoll);
        if (!s_set.rotationLock) {
            const dm::Orientation want = dm::OrientationForGravity(g, s_set.orientation, d->upsideDown);
            if (want != s_set.orientation) Turn(want);
        }
        okay::Input::FeedAcceleration(g.x, g.y, g.z);
        const float kbGame = s_kbShown ? s_kb.area.h : 0.0f;
        dm::ApplyScreenReport(dm::MakeScreenReport(*d, s_set.orientation, s_place, kbGame, s_set.darkTheme));
        if (s_set.touchInput) okay::Input::SetTouchScreen(true);
        if (!playing) {   // Stop: no finger stays down
            std::vector<dm::TouchEvent> ev;
            s_touch.Release(ev);
            for (const dm::TouchEvent& e : ev) okay::Input::FeedTouch(e.id, e.state, okay::Vec2{e.pos.x, e.pos.y});
            s_typed.clear(); s_backspaces = 0; s_enter = s_back = false;
        }
    } else if (s_wasActive) {   // turned off: the desktop's values again
        std::vector<dm::TouchEvent> ev;
        s_touch.Release(ev);
        for (const dm::TouchEvent& e : ev) okay::Input::FeedTouch(e.id, e.state, okay::Vec2{e.pos.x, e.pos.y});
        dm::ResetScreenReport();
        okay::Input::FeedAcceleration(0.0f, 0.0f, 0.0f);
        okay::Input::SetTouchScreen(false);   // the frame loop sets it again for a real touch screen
        s_kbShown = s_kbPinned = false;
    }
    if (!d) s_placed = false;
    s_wasActive = active;
    ApplyNetwork();
}

int DevEmuFpsCap(int editorCap, bool playing) { return dm::EffectiveFpsCap(editorCap, s_set.fpsCap, playing, DevEmuActive()); }

bool DevEmuTakeShot(std::string& path, int& x, int& y, int& w, int& h) {
    if (s_shotArm <= 0 || s_shotPath.empty()) return false;
    if (--s_shotArm > 0) return false;
    if (!s_placed) { s_shotPath.clear(); return false; }
    path = s_shotPath;
    s_shotPath.clear();
    const ImVec2 vp = ImGui::GetMainViewport()->Pos;
    const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
    x = (int)std::lround((s_place.screen.x - vp.x) * fb.x);
    y = (int)std::lround((s_place.screen.y - vp.y) * fb.y);
    w = (int)std::lround(s_place.screen.w * fb.x);
    h = (int)std::lround(s_place.screen.h * fb.y);
    return true;
}

} // namespace oked
