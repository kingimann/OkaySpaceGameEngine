#pragma once
// A window of widgets used by the demo executable and by the golden-image test.
#include "imui/Imui.hpp"
#include "../tests/ImuiTestKit.hpp"

namespace imuidemo {

struct State {
    bool check = true, other = false, open = true;
    float slider = 0.35f, drag = 12.5f;
    int islider = 7, combo = 1;
    std::string name = "Okay Space";
    std::string empty;
    int clicks = 0;
    okay::imui::Rect rButton, rSlider, rName, rCombo;   // filled while drawing, so a script can find the widgets
};

inline void Draw(State& s) {
    using namespace okay::imui;
    SetNextWindowPos(Vec2(20, 20), Cond_Once);
    SetNextWindowSize(Vec2(520, 480), Cond_Once);
    if (Begin("okay::imui demo", &s.open)) {
        Text("Our own immediate-mode UI");
        TextDisabled("No ImGui code, no SDL");
        Separator();
        if (Button("Click me")) ++s.clicks;
        s.rButton = GetItemRect();
        SameLine(); AlignTextToFramePadding();
        Text("clicked %d times", s.clicks);
        Checkbox("Enable feature", &s.check);
        Checkbox("Another option", &s.other);
        SliderFloat("Amount", &s.slider, 0.0f, 1.0f);
        s.rSlider = GetItemRect();
        SliderInt("Count", &s.islider, 0, 20);
        DragFloat("Drag", &s.drag, 0.1f);
        InputText("Name", &s.name);
        s.rName = GetItemRect();
        InputText("Empty", &s.empty);
        static const char* const items[] = {"Alpha", "Beta", "Gamma"};
        Combo("Mode", &s.combo, items, 3);
        s.rCombo = GetItemRect();
        if (TreeNode("Tree node")) {
            Selectable("First entry", true);
            Selectable("Second entry");
            if (TreeNode("Nested")) { Text("Deeper"); TreePop(); }
            TreePop();
        }
        Columns(2);
        Text("Left"); NextColumn(); Text("Right"); NextColumn();
        Text("A"); NextColumn(); Text("B");
        Columns(1);
        if (BeginScrollRegion("list", Vec2(0, 70))) {
            for (int i = 0; i < 12; ++i) Text("Scroll row %d", i);
            EndScrollRegion();
        }
    }
    End();
    SetNextWindowPos(Vec2(560, 40), Cond_Once);
    SetNextWindowSize(Vec2(150, 90), Cond_Once);
    if (Begin("Second")) { Text("Second"); Text("window"); }
    End();
}

/// The scripted interaction behind the demo picture and the golden image: three button clicks, a slider
/// drag, typing in a text box and an open combo with a hovered row.
inline void RunScript(imuitest::Sim& sim, State& st) {
    using namespace imuitest;
    auto ui = [&] { Draw(st); };
    sim.Frames(3, ui);
    for (int i = 0; i < 3; ++i) sim.Click(Center(st.rButton), ui);                 // three button clicks
    {                                                                                // drag the slider to 70 percent
        const Rect r = st.rSlider; const float grab = 12;
        sim.Move(r.min.x + grab / 2 + (r.Width() - 6 - 16 * 6 - grab) * 0.2f, Center(r).y); sim.Frame(ui);
        sim.Down(); sim.Frame(ui);
        sim.Move(r.min.x + grab / 2 + (r.Width() - 6 - 16 * 6 - grab) * 0.7f, Center(r).y); sim.Frame(ui);
        sim.Up(); sim.Frame(ui);
    }
    sim.Click(Vec2(st.rName.min.x + 150, Center(st.rName).y), ui);                    // focus the text box and type
    sim.Tap(Key_End, ui); sim.Type(" 2026"); sim.Frame(ui);
    sim.Click(Vec2(st.rCombo.min.x + 30, Center(st.rCombo).y), ui);                  // open the combo
    sim.Frames(2, ui);
    for (auto& kv : sim.ctx->windows)
        if (kv.second->flags & Win_Popup) sim.Move(kv.second->pos.x + 20, kv.second->pos.y + kv.second->size.y - 12);
    sim.Frames(2, ui);
}

} // namespace imuidemo
