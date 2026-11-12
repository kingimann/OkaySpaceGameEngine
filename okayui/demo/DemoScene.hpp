#pragma once
// A window of OkayUI widgets used by the headless demo executable and by the golden-image test. It is drawn
// with the real auto-layout API; RunScript then drives it with synthetic input (clicks, a slider drag, typing,
// an open combo with a hovered row) so the picture shows live state.
#include "../tests/OkayUITestKit.hpp"

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

} // namespace okayuidemo
