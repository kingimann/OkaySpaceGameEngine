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
    OkayUI::Begin("Test UI", x, y, 340.0f, 540.0f);
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

// A lobby screen built from the launcher-style blocks (Heading, Card, ListItem,
// Badge, KeyValue, PrimaryButton, Spinner, Toast) — a starting point for menus,
// lobbies and scoreboards.
inline void Showcase(float x, float y) {
    static int tab = 0;
    static bool ready = false;
    static const char* tabs[] = {"Players", "Match", "Settings"};
    OkayUI::Begin("Lobby", x, y, 420.0f, 540.0f);
    OkayUI::Caption("NOW PLAYING");
    OkayUI::Heading("Star Arena");
    OkayUI::TextDisabled("Docks - Ranked - EU 1");
    OkayUI::TabBar(tabs, 3, &tab);
    if (tab == 0) {
        OkayUI::BeginCard("players");
        OkayUI::ListItem("Maya Chen", "Ready", "1,240");
        OkayUI::ListItem("Theo", "Choosing a loadout", "980");
        OkayUI::ListItem("Rin", ready ? "Ready" : "Not ready", "1,105", true);
        OkayUI::EndCard();
    } else if (tab == 1) {
        OkayUI::BeginCard("match");
        OkayUI::KeyValue("Mode", "Ranked");
        OkayUI::KeyValue("Map", "Docks");
        OkayUI::KeyValue("Players", "3 / 4");
        OkayUI::KeyValue("Ping", "23 ms");
        OkayUI::EndCard();
    } else {
        static bool voice = true; static float vol = 70.0f;
        OkayUI::ToggleSwitch("Voice chat", &voice);
        OkayUI::SliderFloat("Volume", &vol, 0.0f, 100.0f);
    }
    OkayUI::Badge("3 / 4"); OkayUI::SameLine(); OkayUI::Badge("RANKED", 171, 92, 230); OkayUI::SameLine(); OkayUI::Badge("EU", 67, 181, 129);
    OkayUI::Spinner(); OkayUI::SameLine(); OkayUI::TextDisabled("Waiting for one more player...");
    if (OkayUI::PrimaryButton(ready ? "Unready" : "Ready")) {
        ready = !ready;
        OkayUI::Toast(ready ? "You're ready" : "Marked not ready", ready ? 1 : 0);
    }
    OkayUI::SameLine();
    if (OkayUI::Button("Leave")) OkayUI::Toast("Left the lobby", 2);
    OkayUI::End();
}

} // namespace okay_testui
