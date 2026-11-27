#pragma once
// The pieces the OkayUI apps in a rectangle share (the Script Editor app, shared/ScriptIdeApp; the Visual Script app,
// shared/VisualScriptApp): their dark colour scheme, a dialog box over the app, a bare window filled with a colour, a strip of
// document tabs with close buttons, and a window program's command line ([PATH ...] [--frames N]). One copy for every app
// (rule 19c). Written for this project from the OkayUI interface.
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUIDesign.hpp"   // Packed / PackedMix: the design's colours

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace okayuiapp {

namespace UI = OkayUI;

inline UI::Color32 Rgb(int r, int g, int b, int a = 255) { return UI::RGBA((unsigned)r, (unsigned)g, (unsigned)b, (unsigned)a); }
// The apps' chrome colours: the design in force (okay/UI/OkayUIDesign.hpp), taken from OkayUI's theme at the start of every frame
// (FollowDesignColours), so the apps match the launcher and the editor in every design and scheme. The values below are the
// dark scheme they start with.
inline UI::Color32 kBack = Rgb(30, 32, 37), kPanel = Rgb(37, 40, 46), kStrip = Rgb(26, 28, 32), kLine = Rgb(56, 60, 69),
                   kText = Rgb(214, 218, 226), kDim = Rgb(140, 146, 158), kAccent = Rgb(86, 156, 214), kTabOn = Rgb(44, 48, 56),
                   kHover = Rgb(52, 57, 66), kError = Rgb(232, 102, 92), kWarn = Rgb(226, 182, 76), kAdd = Rgb(98, 180, 110);
inline void FollowDesignColours() {
    const UI::Theme& t = UI::Style();
    auto c = [](const unsigned char* v) { return OkayUI::design::Packed(v); };
    auto mix = [](const unsigned char* a, const unsigned char* b, float k) { return OkayUI::design::PackedMix(a, b, k); };
    kBack = c(t.panel); kPanel = c(t.raised); kStrip = c(t.track); kLine = mix(t.panel, t.text, 0.14f);
    kText = c(t.text); kDim = c(t.textDim); kAccent = c(t.accent); kTabOn = c(t.bg); kHover = c(t.bgHover);
    kError = c(t.danger); kWarn = c(t.warning); kAdd = c(t.success);
}

/// A bare OkayUI window over (X, Y, W, H) filled with `fill`, with a line along its top edge when `topLine`. Call UI::End() after,
/// whatever it returns.
inline bool BeginFilled(const std::string& id, float X, float Y, float W, float H, UI::Color32 fill, bool topLine = false) {
    UI::BareNextWindow();
    if (!UI::Begin(id.c_str(), X, Y, W, H)) return false;
    const auto& dl = UI::GetWindowDrawList();
    dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + H), fill);
    if (topLine) dl->AddLine(UI::Vec2(X, Y), UI::Vec2(X + W, Y), kLine);
    return true;
}

/// A dialog box of about bw x bh over the app's rectangle (X, Y, W, H), which is dimmed; (bx, by) is where its content starts.
/// Call UI::End() after, whatever it returns.
inline bool BeginDialog(const std::string& id, float X, float Y, float W, float H, float bw, float bh, float& bx, float& by) {
    if (!BeginFilled(id, X, Y, W, H, Rgb(0, 0, 0, 90))) return false;
    const auto& dl = UI::GetWindowDrawList();
    bw = std::min(bw, W - 20.0f);
    const float x0 = X + (W - bw) * 0.5f, y0 = Y + std::max(20.0f, std::min(60.0f, (H - bh) * 0.3f));
    dl->AddRectFilled(UI::Vec2(x0, y0), UI::Vec2(x0 + bw, y0 + bh), kPanel, 6.0f);
    dl->AddRect(UI::Vec2(x0, y0), UI::Vec2(x0 + bw, y0 + bh), kLine, 6.0f, 1.0f);
    bx = x0 + 14.0f;
    by = y0 + 12.0f;
    return true;
}

/// What a click on a tab strip did: the tab clicked (-1 none), the tab to close (its x or a middle click; -1 none), the tab
/// right-clicked (its menu; -1 none), and where the strip's tabs end (a host draws a "+" there).
struct TabStripResult { int clicked = -1, close = -1, menu = -1; float endX = 0; };
/// A strip of document tabs in (X, Y, W, h), drawn in the current window: each label (already with its "* " when changed) with
/// a close "x", the `active` one marked. `fit(text, maxW)` cuts a label to a width. Clicks count unless `modal`.
template <class Fit>
TabStripResult DrawTabStrip(float X, float Y, float W, float h, const std::vector<std::string>& labels, int active, bool modal, Fit fit,
                            float roomRight = 40.0f) {
    TabStripResult r;
    const auto& dl = UI::GetWindowDrawList();
    dl->AddRectFilled(UI::Vec2(X, Y), UI::Vec2(X + W, Y + h), kStrip);
    const UI::Vec2 m = UI::GetMousePos();
    const float lh = UI::GetTextLineHeight();
    float x = X + 4;
    for (std::size_t i = 0; i < labels.size() && x < X + W - roomRight; ++i) {
        const std::string& label = labels[i];
        const float tw = std::min(220.0f, UI::CalcTextWidth(label.c_str()) + 40.0f);
        const bool on = (int)i == active;
        const bool hover = m.x >= x && m.x < x + tw && m.y >= Y && m.y < Y + h;
        dl->AddRectFilled(UI::Vec2(x, Y + 3), UI::Vec2(x + tw, Y + h), on ? kTabOn : (hover ? kHover : kStrip), 4.0f);
        if (on) dl->AddRectFilled(UI::Vec2(x, Y + h - 2), UI::Vec2(x + tw, Y + h), kAccent);
        dl->AddText(UI::Vec2(x + 10, Y + (h - lh) * 0.5f + 1), on ? kText : kDim, fit(label, tw - 34).c_str());
        const float cx = x + tw - 18;
        const bool overClose = m.x >= cx && m.x < cx + 14 && m.y >= Y + 6 && m.y < Y + h - 4;
        dl->AddText(UI::Vec2(cx + 3, Y + (h - lh) * 0.5f + 1), overClose ? kText : kDim, "x");
        if (hover && !modal && UI::IsMouseClicked(0)) { if (overClose) r.close = (int)i; else r.clicked = (int)i; }
        if (hover && !modal && UI::IsMouseClicked(2)) r.close = (int)i;
        if (hover && !modal && UI::IsMouseClicked(1)) r.menu = (int)i;
        x += tw + 2;
    }
    r.endX = x;
    return r;
}

/// A window program's command line: words that do not start with '-' are paths, "--frames N" ends the program after N frames
/// (1 .. 1000000; a start-and-quit check). "--help" prints `usage` (with the program's name for %s) and gives false: the
/// program then ends with 0.
inline bool ParseProgramArgs(int argc, char** argv, const char* usage, std::vector<std::string>& paths, int& frames) {
    frames = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { std::printf(usage, argv[0]); return false; }
        if (a == "--frames" && i + 1 < argc) {
            const long n = std::strtol(argv[++i], nullptr, 10);
            frames = n < 1 ? 1 : (n > 1000000 ? 1000000 : (int)n);
        } else if (!a.empty() && a[0] != '-') paths.push_back(a);
    }
    return true;
}

}  // namespace okayuiapp
