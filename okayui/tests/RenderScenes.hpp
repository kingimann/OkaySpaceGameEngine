#pragma once
// The OkayUI scenes the render-backend tests draw: the golden scenes of the other okayui tests (explicit widgets, the
// scripted demo window, the draw-list lab, the colour picker, a scrolling table) in both looks, plus scenes made for the
// backends: plots, stacked translucent windows (the draw list reordered by window stacking), nested clipped children,
// host images (textured quads, tinted, clipped), a code editor view, shapes with rounded corners and alpha, and text
// in a script the launcher font lacks (a fallback font atlas).
//
// A Scene leaves its LAST frame in OkayUI (OkayUI::GetDrawData()); Setup() then registers the images it uses.
#include "OkayUITestKit.hpp"
#include "../demo/DemoScene.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"
#include "okay/UI/OkayUIRender.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace rscenes {

namespace UI = OkayUI;
using okayuitest::Sim;

/// A 32 x 32 test image: a diagonal gradient, an opaque border, a transparent hole and a half-transparent band.
inline const std::vector<std::uint8_t>& TestImage() {
    static std::vector<std::uint8_t> px;
    if (px.empty()) {
        px.resize(32 * 32 * 4);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            std::uint8_t* p = &px[((size_t)y * 32 + x) * 4];
            p[0] = (std::uint8_t)(x * 8); p[1] = (std::uint8_t)(y * 8); p[2] = (std::uint8_t)(255 - (x + y) * 4); p[3] = 255;
            if (x == 0 || y == 0 || x == 31 || y == 31) { p[0] = 255; p[1] = 255; p[2] = 255; }
            if (x >= 12 && x < 20 && y >= 12 && y < 20) p[3] = 0;
            if (y >= 24 && y < 28) p[3] = 128;
        }
    }
    return px;
}
/// The key the scenes pass to OkayUI::DrawTexture (never dereferenced: an opaque pointer).
inline SDL_Texture* ImageKey() { static char k; return reinterpret_cast<SDL_Texture*>(&k); }

struct Scene {
    std::string name;
    int w = 0, h = 0;
    bool launcher = false;
    bool usesImage = false;
    std::function<void(Sim&)> build;
};

inline void SetupImages(UI::render::Renderer& r) {
    r.SetImage(ImageKey(), 32, 32, TestImage().data(), true);
}

inline void Primitives(Sim& sim) {
    bool cb = true, sw = true; int radio = 1, tab = 1; float sl = 0.4f; char txt[16] = "hello";
    sim.Frame([&] {
        UI::Panel(8, 8, 404, 204);
        UI::Button(1, 20, 20, 90, 30, "Button");
        UI::Checkbox(2, 20, 62, 22, "Check", &cb);
        UI::ToggleSwitch(3, 20, 94, 44, 22, "Switch", &sw);
        UI::RadioButton(4, 20, 126, 22, "A", &radio, 0); UI::RadioButton(5, 80, 126, 22, "B", &radio, 1);
        UI::Tab(6, 20, 160, 80, 28, "One", &tab, 0); UI::Tab(7, 104, 160, 80, 28, "Two", &tab, 1);
        UI::ProgressBar(220, 20, 170, 20, 0.65f);
        UI::Slider(8, 220, 60, 170, 24, &sl, 0.0f, 1.0f);
        UI::TextField(9, 220, 100, 170, 30, txt, (int)sizeof(txt));
        UI::Label(220, 150, "Label text");
    });
}

inline void Picker(Sim& sim) {
    float c[4] = {0.9f, 0.5f, 0.2f, 0.7f};
    auto ui = [&] {
        if (UI::Begin("Picker", 10, 10, 480, 500)) { UI::ColorPicker4("p", c, 0); float c3[3] = {0.2f, 0.6f, 0.9f}; UI::ColorEdit3("edit", c3); }
        UI::End();
    };
    sim.Frames(4, ui);
    sim.Move(200, 150); sim.Down(); sim.Frame(ui); sim.Frame(ui); sim.Up(); sim.Frames(2, ui);
}

inline void Table(Sim& sim) {
    auto ui = [&] {
        if (UI::BeginTable("pic", 4, UI::Table_ScrollX | UI::Table_ScrollY | UI::Table_RowBg | UI::Table_Sortable | UI::Table_BordersInnerH, UI::Vec2{360, 200})) {
            UI::TableSetupColumn("Name", UI::Col_WidthFixed | UI::Col_DefaultSort, 90); UI::TableSetupColumn("Kind", UI::Col_WidthFixed, 120);
            UI::TableSetupColumn("Size", UI::Col_WidthFixed, 120); UI::TableSetupColumn("Date", UI::Col_WidthFixed, 140);
            UI::TableSetupScrollFreeze(1, 1);
            UI::TableHeadersRow();
            if (UI::TableSortSpecs* sp = UI::TableGetSortSpecs()) sp->specsDirty = false;
            for (int i = 0; i < 30; ++i) {
                UI::TableNextRow();
                char t[24];
                UI::TableNextColumn(); std::snprintf(t, sizeof t, "item %02d", i); UI::Text(t);
                UI::TableNextColumn(); UI::Text(i % 2 ? "folder" : "document");
                UI::TableNextColumn(); std::snprintf(t, sizeof t, "%d KB", 100 + i * 37); UI::Text(t);
                UI::TableNextColumn(); UI::Text("2026-01-01");
            }
            UI::EndTable();
        }
    };
    // (the window of the table test is the whole screen; here the table is drawn in a window of its own)
    auto win = [&] { if (UI::Begin("T", 10, 10, 400, 280)) ui(); UI::End(); };
    sim.Frames(4, win);
    sim.Move(100, 150); sim.wheel = -2.0f; sim.Frame(win); sim.Frames(2, win);
    sim.shift = true; sim.Move(100, 150); sim.wheel = -2.0f; sim.Frame(win); sim.shift = false;
    sim.Move(410, 290); sim.Frames(2, win);
}

inline void Plots(Sim& sim) {
    static float v[64], w[24], dens[200];
    for (int i = 0; i < 64; ++i) v[i] = std::sin(i * 0.31f) * 0.8f + 0.1f * std::sin(i * 2.7f);
    for (int i = 0; i < 24; ++i) w[i] = (float)((i * 37) % 11) - 3.0f;
    for (int i = 0; i < 200; ++i) dens[i] = std::exp(-((i - 100) * (i - 100)) / 1800.0f) * 3.0f;
    auto ui = [&] {
        if (UI::Begin("Plots", 10, 10, 420, 400)) {
            UI::PlotOptions o; o.overlay = "signal"; o.size = UI::Vec2{380, 90};
            UI::PlotLines("lines", v, 64, o);
            UI::PlotOptions h; h.overlay = "bars"; h.scaleMin = -4.0f; h.scaleMax = 8.0f; h.size = UI::Vec2{380, 90};
            UI::PlotHistogram("hist", w, 24, h);
            UI::PlotOptions d; d.size = UI::Vec2{380, 70};
            UI::PlotLines("density", dens, 200, d);
        }
        UI::End();
    };
    sim.Frames(3, ui);
    sim.Move(120, 70); sim.Frames(2, ui);        // hover: a tooltip and the hovered colour
}

inline void Stacking(Sim& sim) {
    // three overlapping windows; a click on the back one raises it, so the draw list is reordered
    auto ui = [&] {
        UI::SetNextWindowPos(UI::Vec2{20, 20}); UI::SetNextWindowSize(UI::Vec2{260, 180});
        if (UI::Begin("Back", 20, 20, 260, 180)) { UI::Text("Back window"); UI::Button("A button"); static float sv = 0.3f; UI::SliderFloat("s", &sv, 0, 1); }
        UI::End();
        UI::SetNextWindowPos(UI::Vec2{120, 90}); UI::SetNextWindowSize(UI::Vec2{260, 180});
        if (UI::Begin("Middle", 120, 90, 260, 180)) { UI::Text("Middle window"); static bool cv = true; UI::Checkbox("check", &cv); }
        UI::End();
        UI::SetNextWindowPos(UI::Vec2{60, 160}); UI::SetNextWindowSize(UI::Vec2{260, 140});
        if (UI::Begin("Front", 60, 160, 260, 140)) { UI::Text("Front window"); UI::ProgressBar(0.4f, "40"); }
        UI::End();
        auto dl = UI::GetForegroundDrawList();
        dl.AddRectFilled(UI::Vec2{250, 40}, UI::Vec2{420, 150}, UI::RGBA(255, 60, 60, 110), 14.0f);
        dl.AddRectFilled(UI::Vec2{300, 90}, UI::Vec2{440, 200}, UI::RGBA(60, 60, 255, 110), 14.0f);
    };
    sim.Frames(3, ui);
    sim.Click(60, 26, ui);                        // title bar of "Back": it moves to the front
    sim.Frames(2, ui);
}

inline void NestedClip(Sim& sim) {
    auto ui = [&] {
        if (UI::Begin("Clip", 10, 10, 360, 330)) {
            UI::Text("outer child");
            if (UI::BeginChild("outer", 320, 150)) {
                for (int i = 0; i < 20; ++i) { char b[80]; std::snprintf(b, sizeof b, "outer row %d ................ long text that is cut", i); UI::Text(b); }
                if (UI::BeginChild("inner", 200, 70)) {
                    for (int i = 0; i < 12; ++i) { char b[40]; std::snprintf(b, sizeof b, "inner %d", i); UI::Button(b); }
                    UI::EndChild();
                }
                UI::EndChild();
            }
        }
        UI::End();
    };
    sim.Frames(3, ui);
    sim.Move(100, 100); sim.wheel = -1.5f; sim.Frame(ui); sim.Frames(2, ui);
}

inline void Images(Sim& sim) {
    auto ui = [&] {
        if (UI::Begin("Images", 10, 10, 400, 300)) {
            UI::Text("host images");
            UI::Image(ImageKey(), 64, 64);
            UI::SameLine(); UI::Image(ImageKey(), 100, 50, 255, 200, 200, 255);
            UI::SameLine(); UI::Image(ImageKey(), 40, 40, 255, 255, 255, 120);
            if (UI::BeginChild("imgs", 300, 100)) {
                for (int i = 0; i < 6; ++i) { UI::Image(ImageKey(), 48 + i * 6, 48 + i * 6); UI::SameLine(); }
                UI::EndChild();
            }
            UI::ImageButton("imgbtn", ImageKey(), 40, 40);
        }
        UI::End();
        UI::DrawTexture(ImageKey(), 330, 200, 32, 32);
    };
    sim.Frames(3, ui);
}

inline void Shapes(Sim& sim) {
    sim.Frame([&] {
        UI::Begin("Shapes", 5, 5, 390, 330);
        const UI::DrawList& dl = UI::GetWindowDrawList();
        dl.AddRectFilled(UI::Vec2{20, 40}, UI::Vec2{140, 120}, UI::RGBA(200, 60, 60, 255), 24.0f);
        dl.AddRectFilled(UI::Vec2{80, 70}, UI::Vec2{200, 150}, UI::RGBA(60, 200, 80, 160), 18.0f);
        dl.AddRect(UI::Vec2{30, 160}, UI::Vec2{170, 230}, UI::RGBA(255, 255, 0, 255), 10.0f, 3.0f);
        dl.AddCircleFilled(UI::Vec2{260, 90}, 50.0f, UI::RGBA(80, 120, 255, 200), 48);
        dl.AddCircle(UI::Vec2{260, 90}, 60.0f, UI::RGBA(255, 255, 255, 255), 64, 2.0f);
        dl.AddRectFilledMultiColor(UI::Vec2{200, 170}, UI::Vec2{370, 240}, UI::RGBA(255, 0, 0, 255), UI::RGBA(0, 255, 0, 255), UI::RGBA(0, 0, 255, 255), UI::RGBA(255, 255, 0, 80));
        UI::Vec2 poly[5] = {{40, 260}, {110, 250}, {150, 290}, {90, 320}, {35, 300}};
        dl.AddConvexPolyFilled(poly, 5, UI::RGBA(180, 80, 220, 220));
        dl.AddBezierCubic(UI::Vec2{200, 260}, UI::Vec2{250, 200}, UI::Vec2{300, 330}, UI::Vec2{370, 260}, UI::RGBA(255, 160, 40, 255), 3.0f);
        dl.PushClipRect(UI::Vec2{190, 250}, UI::Vec2{300, 300});
        dl.AddCircleFilled(UI::Vec2{250, 280}, 40.0f, UI::RGBA(255, 255, 255, 140), 32);
        dl.PopClipRect();
        dl.AddText(UI::Vec2{20, 20}, UI::RGBA(255, 255, 255, 255), "Rounded rectangles and alpha");
        UI::End();
    });
}

inline void CodeView(Sim& sim) {
    static UI::CodeEdit ce;
    static bool init = false;
    if (!init) {
        ce.SetLanguage(UI::OkayScriptLanguage());
        ce.SetText("// a script\nfunction Update(dt) {\n    local x = 0\n    for i = 1, 10 do\n        x = x + i * dt  // sum\n    end\n    if x > 3.5 then print(\"big\") end\n    return x\nend\n\nfunction Start() print(\"start\") end\n");
        ce.SetCaret(UI::CodePos{4, 12}); ce.SetSelection(UI::CodePos{1, 0}, UI::CodePos{3, 6});
        init = true;
    }
    auto ui = [&] { if (UI::Begin("W", 5, 5, sim.W - 10, sim.H - 10)) ce.Draw("ed", sim.W - 30, sim.H - 50); UI::End(); };
    sim.Frames(3, ui);
}

inline void Fallback(Sim& sim) {
    auto ui = [&] {
        if (UI::Begin("Text", 10, 10, 380, 200)) {
            UI::Text("Latin text and numbers 0123456789");
            UI::Text("Привет, мир! Кириллица");
            UI::Text("Ελληνικά και ελληνικά");
            UI::Button("Кнопка");
        }
        UI::End();
    };
    sim.Frames(3, ui);
}

inline std::vector<Scene> All() {
    std::vector<Scene> v;
    auto add = [&](const char* n, int w, int h, bool launcher, bool img, std::function<void(Sim&)> f) { v.push_back(Scene{n, w, h, launcher, img, std::move(f)}); };
    for (int look = 0; look < 2; ++look) {
        const bool l = look == 1; const std::string sfx = l ? "_launcher" : "";
        v.push_back(Scene{"primitives" + sfx, 420, 220, l, false, Primitives});
        v.push_back(Scene{"demo_window" + sfx, 800, 780, l, false, [](Sim& s) { okayuidemo::State st; okayuidemo::RunScript(s, st); }});
        v.push_back(Scene{"lab" + sfx, 760, 600, l, false, [](Sim& s) { okayuidemo::LabState st; okayuidemo::RunLabScript(s, st); }});
        v.push_back(Scene{"colorpicker" + sfx, 500, 520, l, false, Picker});
        v.push_back(Scene{"table_scroll" + sfx, 420, 300, l, false, Table});
        v.push_back(Scene{"plots" + sfx, 440, 420, l, false, Plots});
        v.push_back(Scene{"stacking" + sfx, 460, 330, l, false, Stacking});
        v.push_back(Scene{"nested_clip" + sfx, 380, 350, l, false, NestedClip});
        v.push_back(Scene{"images" + sfx, 420, 320, l, true, Images});
        v.push_back(Scene{"shapes" + sfx, 400, 340, l, false, Shapes});
        v.push_back(Scene{"code_view" + sfx, 520, 300, l, false, CodeView});
    }
    add("fallback_text_launcher", 400, 220, true, false, Fallback);
    return v;
}

inline Scene Find(const std::string& name) {
    for (const Scene& s : All()) if (s.name == name) return s;
    return Scene();
}

/// Run one scene: reset OkayUI, set the look, build the frames. The last frame stays in OkayUI.
inline void Run(const Scene& s, Sim& sim) {
    okayuitest::Reset();
    UI::UseLauncherLook(s.launcher);
    UI::Style().caretBlink = false;
    sim = Sim((float)s.w, (float)s.h);
    s.build(sim);
}

// ---- comparing two RGBA8 images -----------------------------------------------------------------------------------
struct Diff {
    int w = 0, h = 0;
    int maxDiff = 0;                // largest per-channel difference
    double meanDiff = 0;            // mean per-channel difference over all channels
    long long above = 0;            // pixels with any channel differing by more than `tol`
    long long total = 0;            // pixels
    long long differing = 0;        // pixels with any difference at all
    bool sameSize = true;
};
inline Diff Compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int w, int h, int tol) {
    Diff d; d.w = w; d.h = h; d.total = (long long)w * h;
    if (a.size() != b.size() || a.size() != (size_t)w * (size_t)h * 4) { d.sameSize = false; return d; }
    long long sum = 0;
    for (size_t i = 0; i < a.size(); i += 4) {
        int m = 0;
        for (int c = 0; c < 4; ++c) { const int e = std::abs((int)a[i + c] - (int)b[i + c]); sum += e; if (e > m) m = e; }
        if (m > d.maxDiff) d.maxDiff = m;
        if (m > tol) ++d.above;
        if (m > 0) ++d.differing;
    }
    d.meanDiff = (double)sum / (double)a.size();
    return d;
}

} // namespace rscenes
