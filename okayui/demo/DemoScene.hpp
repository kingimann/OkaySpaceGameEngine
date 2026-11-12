#pragma once
// A window of OkayUI widgets used by the headless demo executable and by the golden-image test. It is drawn
// with the real auto-layout API; RunScript then drives it with synthetic input (clicks, a slider drag, typing,
// an open combo with a hovered row) so the picture shows live state.
#include "../tests/OkayUITestKit.hpp"

#include <cmath>
#include <cstdio>

namespace okayuidemo {

namespace UI = OkayUI;

struct State {
    bool check = true, other = false, open = true, sw = true;
    float slider = 0.35f, drag = 12.5f, color[3] = {0.92f, 0.42f, 0.18f};
    int islider = 7, combo = 1, radio = 0, tab = 0, clicks = 0;
    char name[64] = "Okay Space";
    char empty[32] = "";
    // Filled while drawing, so a script can find the widgets.
    UI::Rect rButton, rSlider, rName, rCombo, rTree;
};

inline void Draw(State& s) {
    if (UI::Begin("OkayUI headless demo", 20, 20, 540, 740, &s.open)) {
        UI::Text("Our own immediate-mode UI");
        UI::Separator();
        if (UI::Button("Click me")) ++s.clicks;
        s.rButton = UI::GetItemRect();
        UI::SameLine();
        char buf[48]; std::snprintf(buf, sizeof buf, "clicked %d times", s.clicks);
        UI::Text(buf);
        UI::Checkbox("Enable", &s.check); UI::SameLine(); UI::Checkbox("Other", &s.other); UI::SameLine(); UI::ToggleSwitch("Switch", &s.sw);
        UI::RadioButton("One", &s.radio, 0); UI::SameLine(); UI::RadioButton("Two", &s.radio, 1);
        UI::SliderFloat("Amount", &s.slider, 0.0f, 1.0f);
        s.rSlider = UI::GetItemRect();
        UI::SliderInt("Count", &s.islider, 0, 20);
        UI::DragFloat("Drag", &s.drag, 0.1f);
        UI::InputText("Name", s.name, (int)sizeof(s.name));
        s.rName = UI::GetItemRect();
        UI::InputText("Empty", s.empty, (int)sizeof(s.empty));
        static const char* const items[] = {"Alpha", "Beta", "Gamma"};
        UI::Combo("Mode", items, 3, &s.combo);
        s.rCombo = UI::GetItemRect();
        UI::ProgressBar(0.6f, "60 percent");
        if (UI::TreeNode("Tree node")) {
            UI::Selectable("First entry", true);
            UI::Selectable("Second entry", false);
            UI::TreePop();
        }
        s.rTree = UI::GetItemRect();
        static const char* const tabs[] = {"General", "Audio", "Video"};
        UI::TabBar(tabs, 3, &s.tab);
        UI::ColorEdit3("Colour", s.color);
        if (UI::BeginChild("list", 0, 80)) {
            for (int i = 0; i < 12; ++i) { char row[24]; std::snprintf(row, sizeof row, "Scroll row %d", i); UI::Text(row); }
            UI::EndChild();
        }
    }
    UI::End();
    if (UI::Begin("Second", 580, 40, 190, 110)) { UI::Text("Second"); UI::Text("window"); }
    UI::End();
}

/// The scripted interaction behind the demo picture and the golden image: three button clicks, a slider drag to
/// 70 percent, typing in a text box and an open combo with a hovered row.
inline void RunScript(okayuitest::Sim& sim, State& st) {
    using namespace okayuitest;
    auto ui = [&] { Draw(st); };
    sim.Frames(3, ui);
    for (int i = 0; i < 3; ++i) sim.Click(CenterX(st.rButton), CenterY(st.rButton), ui);          // three button clicks
    {                                                                                                // drag the slider to 70 percent
        const UI::Rect r = st.rSlider;
        const float x0 = r.x, w = r.w - (UI::CalcTextWidth("Amount") + 8.0f);
        sim.Move(x0 + w * 0.2f, CenterY(r)); sim.Frame(ui);
        sim.Down(); sim.Frame(ui);
        sim.Move(x0 + w * 0.7f, CenterY(r)); sim.Frame(ui);
        sim.Up(); sim.Frame(ui);
    }
    sim.Click(CenterX(st.rTree), CenterY(st.rTree), ui);                                           // open the tree node
    sim.Click(st.rName.x + 120, CenterY(st.rName), ui);                                            // focus the text box and type
    sim.Tap(Key_End, ui); sim.Type(" 2026"); sim.Frame(ui);
    sim.Click(st.rCombo.x + 30, CenterY(st.rCombo), ui);                                           // open the combo
    sim.Frames(2, ui);
    sim.Move(st.rCombo.x + 30, st.rCombo.y + st.rCombo.h * 2.5f);                                  // hover the third row
    sim.Frames(2, ui);
}


// ---- The draw list lab ---------------------------------------------------------------------------------------------
// A canvas drawn with the public draw list (lines, shapes, a curve, a path gauge, text) inside the auto layout, made
// interactive with InvisibleButton and the item-state queries, a measured right-aligned label placed with the
// cursor calls, and a block of widgets inside BeginDisabled. `okayui_headless_demo out.png lab` renders it.
struct LabState {
    float gain = 0.4f, level = 0.65f;
    bool locked = false, other = true;
    char note[32] = "keep me";
    int openClicks = 0, lockedClicks = 0, canvasClicks = 0, dragEnded = 0;
    // Markers set by clicks and strokes set by drags on the canvas, in canvas-relative pixels.
    UI::Vec2 markers[8]; int nMarkers = 0;
    UI::Vec2 strokeA[4], strokeB[4]; int nStrokes = 0;
    UI::Vec2 pressAt{}; bool dragging = false;
    bool hoverOnCanvas = false;
    UI::Rect rCanvas, rLock, rOpen;
};

inline void DrawLab(LabState& s) {
    using UI::Vec2; using UI::RGBA;
    if (UI::Begin("Draw list lab", 10, 10, 740, 580)) {
        UI::Text("A canvas drawn with the public draw list");
        if (UI::InvisibleButton("canvas", 0.0f, 250.0f)) {}
        s.rCanvas = UI::GetItemRect();
        const UI::Rect c = s.rCanvas;
        const Vec2 mouse = UI::GetMousePos();
        s.hoverOnCanvas = UI::IsItemHovered();
        if (UI::IsItemClicked()) { s.pressAt = Vec2{mouse.x - c.x, mouse.y - c.y}; s.dragging = false; ++s.canvasClicks; }
        if (UI::IsItemActive()) {
            const float dx = mouse.x - c.x - s.pressAt.x, dy = mouse.y - c.y - s.pressAt.y;
            if (dx * dx + dy * dy > 16.0f) s.dragging = true;
        }
        if (UI::IsItemDeactivated()) {
            if (s.dragging) { if (s.nStrokes < 4) { s.strokeA[s.nStrokes] = s.pressAt; s.strokeB[s.nStrokes] = Vec2{mouse.x - c.x, mouse.y - c.y}; ++s.nStrokes; } ++s.dragEnded; }
            else if (s.nMarkers < 8) s.markers[s.nMarkers++] = s.pressAt;
        }
        const UI::DrawList& dl = UI::GetWindowDrawList();
        const Vec2 a{c.x, c.y}, b{c.x + c.w, c.y + c.h};
        dl.PushClipRect(a, b);
        dl.AddRectFilled(a, b, RGBA(18, 22, 32), 6.0f);
        for (float x = 20.0f; x < c.w; x += 20.0f) dl.AddLine({c.x + x, c.y}, {c.x + x, b.y}, RGBA(255, 255, 255, 18));
        for (float y = 20.0f; y < c.h; y += 20.0f) dl.AddLine({c.x, c.y + y}, {b.x, c.y + y}, RGBA(255, 255, 255, 18));
        Vec2 wave[64];
        for (int i = 0; i < 64; ++i) wave[i] = Vec2{c.x + 16.0f + i * 5.2f, c.y + 60.0f + 32.0f * std::sin(i * 0.2f)};
        dl.AddPolyline(wave, 64, RGBA(90, 220, 140), false, 2.0f);
        dl.AddBezierCubic({c.x + 20, c.y + 220}, {c.x + 140, c.y + 120}, {c.x + 260, c.y + 300}, {c.x + 380, c.y + 150}, RGBA(255, 150, 70), 2.5f);
        dl.AddRectFilledMultiColor({c.x + 20, c.y + 130}, {c.x + 180, c.y + 160}, RGBA(40, 80, 255), RGBA(255, 60, 140), RGBA(255, 200, 60), RGBA(30, 200, 200));
        dl.AddRect({c.x + 20, c.y + 130}, {c.x + 180, c.y + 160}, RGBA(255, 255, 255, 200), 4.0f, 1.0f);
        dl.AddCircleFilled({c.x + 450, c.y + 70}, 26.0f, RGBA(240, 80, 90));
        dl.AddCircle({c.x + 450, c.y + 70}, 36.0f, RGBA(240, 200, 90), 0, 3.0f);
        dl.AddTriangleFilled({c.x + 520, c.y + 100}, {c.x + 580, c.y + 100}, {c.x + 550, c.y + 45}, RGBA(120, 140, 255));
        dl.AddTriangle({c.x + 590, c.y + 100}, {c.x + 650, c.y + 100}, {c.x + 620, c.y + 45}, RGBA(120, 140, 255), 2.0f);
        dl.AddQuadFilled({c.x + 480, c.y + 150}, {c.x + 560, c.y + 140}, {c.x + 575, c.y + 185}, {c.x + 470, c.y + 195}, RGBA(90, 200, 120, 200));
        dl.AddRect({c.x + 590, c.y + 140}, {c.x + 700, c.y + 190}, RGBA(255, 255, 255), 12.0f, 3.0f);
        // a gauge: a track and a value arc built with the path calls, then a centred label measured with CalcTextSize
        const Vec2 g{c.x + 300, c.y + 200};
        dl.PathArcTo(g, 34.0f, 2.4f, 6.8f, 24); dl.PathStroke(RGBA(60, 66, 84), false, 8.0f);
        dl.PathArcTo(g, 34.0f, 2.4f, 2.4f + 4.4f * s.level, 24); dl.PathStroke(RGBA(90, 200, 255), false, 8.0f);
        char pct[16]; std::snprintf(pct, sizeof pct, "%d%%", (int)(s.level * 100.0f + 0.5f));
        const Vec2 ts = UI::CalcTextSize(pct);
        dl.AddText({g.x - ts.x * 0.5f, g.y - ts.y * 0.5f}, UI::GetColorU32(UI::Col_Text), pct);
        dl.AddText({c.x + 12, c.y + 8}, RGBA(255, 255, 255, 160), "click to mark, drag to draw");
        for (int i = 0; i < s.nMarkers; ++i) { const Vec2 m{c.x + s.markers[i].x, c.y + s.markers[i].y}; dl.AddCircle(m, 7.0f, RGBA(255, 255, 255), 0, 2.0f); dl.AddCircleFilled(m, 2.5f, RGBA(255, 255, 255)); }
        for (int i = 0; i < s.nStrokes; ++i) dl.AddLine({c.x + s.strokeA[i].x, c.y + s.strokeA[i].y}, {c.x + s.strokeB[i].x, c.y + s.strokeB[i].y}, RGBA(255, 230, 120), 3.0f);
        dl.PopClipRect();
        if (UI::IsItemActive() && s.dragging)   // the rubber band drawn on top, unclipped
            UI::GetForegroundDrawList().AddLine({c.x + s.pressAt.x, c.y + s.pressAt.y}, mouse, RGBA(255, 255, 255), 2.0f);

        // measured and placed with the cursor calls
        char info[64]; const Vec2 av = UI::GetContentRegionAvail();
        std::snprintf(info, sizeof info, "content region %d x %d", (int)av.x, (int)av.y);
        UI::Text(info);
        const Vec2 right = UI::CalcTextSize("right aligned");
        UI::SetCursorScreenPos({c.x + c.w - right.x, UI::GetItemRect().y});   // same row as the line above, flush right
        UI::Text("right aligned");

        UI::Spacing();
        if (UI::Checkbox("Lock the panel below", &s.locked)) {}
        s.rLock = UI::GetItemRect();
        UI::BeginDisabled(s.locked);
        if (UI::Button("Open")) { if (s.locked) ++s.lockedClicks; ++s.openClicks; }
        s.rOpen = UI::GetItemRect();
        UI::SameLine(); UI::Checkbox("Other", &s.other);
        UI::SliderFloat("Gain", &s.gain, 0.0f, 1.0f);
        UI::InputText("Note", s.note, (int)sizeof(s.note));
        UI::EndDisabled();
        UI::SliderFloat("Gauge level", &s.level, 0.0f, 1.0f);
    }
    UI::End();
}

/// Two clicks that set markers, a drag that draws a stroke and ends inside the canvas, a click on "Open", then the lock
/// checkbox and a click on "Open" while locked (ignored). Ends with the mouse over the canvas.
inline void RunLabScript(okayuitest::Sim& sim, LabState& st) {
    using namespace okayuitest;
    auto ui = [&] { DrawLab(st); };
    sim.Frames(3, ui);
    const UI::Rect c = st.rCanvas;
    sim.Click(c.x + 90, c.y + 100, ui);
    sim.Click(c.x + 420, c.y + 200, ui);
    sim.Move(c.x + 200, c.y + 40); sim.Frame(ui); sim.Down(); sim.Frame(ui);
    sim.Move(c.x + 260, c.y + 90); sim.Frame(ui); sim.Move(c.x + 330, c.y + 125); sim.Frame(ui);
    sim.Up(); sim.Frame(ui);
    sim.Click(CenterX(st.rOpen), CenterY(st.rOpen), ui);
    sim.Click(CenterX(st.rLock), CenterY(st.rLock), ui);
    sim.Frames(2, ui);
    sim.Click(CenterX(st.rOpen), CenterY(st.rOpen), ui);
    sim.Move(c.x + 560, c.y + 30); sim.Frames(2, ui);
}

} // namespace okayuidemo
