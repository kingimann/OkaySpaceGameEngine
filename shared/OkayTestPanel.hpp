#pragma once
// A self-contained "Test UI" panel that exercises every OkayUI widget. Shared by the
// editor (View > Test UI) and the player runtime (F1 overlay) so both show the same
// functional UI — and as a copy-paste starting point for building your own game UI.
//
// Call between OkayUI::BeginFrame() and OkayUI::EndFrame(). Returns true if "Play"
// was clicked this frame (so the host can react).
#include "okay/UI/OkayUI.hpp"

namespace okay_testui {

inline bool Panel(float x, float y) {
    static bool  sound = true, bold = false;
    static float vol = 60.0f, speed = 1.5f, col[3] = {0.30f, 0.65f, 0.95f};
    static int   mode = 0, quality = 1, lives = 3;
    static char  name[24] = "Player";
    static const char* opts[] = {"Low", "Medium", "High"};

    OkayUI::SetFont(bold ? OkayUI::FontBold() : OkayUI::FontDefault());
    OkayUI::Begin("Test UI", x, y, 320.0f, 400.0f);
    OkayUI::BeginMenuBar();
    if (OkayUI::BeginMenu("File")) { OkayUI::MenuItem("New"); OkayUI::MenuItem("Open"); OkayUI::EndMenu(); }
    if (OkayUI::BeginMenu("Help")) { OkayUI::MenuItem("About"); OkayUI::EndMenu(); }
    OkayUI::EndMenuBar();
    OkayUI::InputText("Name", name, (int)sizeof(name));
    OkayUI::Combo("Quality", opts, 3, &quality);
    OkayUI::RadioButton("Easy", &mode, 0); OkayUI::SameLine(); OkayUI::RadioButton("Hard", &mode, 1);
    OkayUI::SliderFloat("Volume", &vol, 0.0f, 100.0f);
    OkayUI::DragFloat("Speed", &speed, 0.1f, 0.0f, 10.0f);
    OkayUI::DragInt("Lives", &lives, 0.25f, 0, 9);
    OkayUI::ColorEdit3("Tint", col);
    OkayUI::Checkbox("Sound", &sound);
    OkayUI::Checkbox("Bold font", &bold);
    if (OkayUI::TreeNode("Stats")) {
        OkayUI::Text("Health"); OkayUI::ProgressBar(0.8f);
        OkayUI::Text("Hunger"); OkayUI::ProgressBar(0.35f);
        OkayUI::TreePop();
    }
    OkayUI::Spacing();
    bool play = OkayUI::Button("Play");
    OkayUI::SameLine();
    OkayUI::Button("Quit");
    OkayUI::End();
    OkayUI::SetFont(OkayUI::FontDefault());
    return play;
}

} // namespace okay_testui
