// Verifies the ui_* script builtins forward to the host's ScriptUIBridge and return
// values back into the script — no real renderer needed (a mock bridge stands in).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/ScriptUI.hpp"
#include <string>

using namespace okay;

struct MockUI : ScriptUIBridge {
    int begins = 0, ends = 0, buttons = 0, texts = 0;
    std::string lastText;
    float lastSliderIn = -1.0f;
    bool  retButton = false, retCheckbox = false;
    float retSlider = 0.0f;
    void  Begin(const char*, float, float, float, float) override { ++begins; }
    void  End() override { ++ends; }
    void  Text(const char* s) override { ++texts; lastText = s ? s : ""; }
    bool  Button(const char*) override { ++buttons; return retButton; }
    bool  Checkbox(const char*, bool) override { return retCheckbox; }
    float Slider(const char*, float v, float, float) override { lastSliderIn = v; return retSlider; }
};

int main() {
    RUN_SUITE("script_ui");

    MockUI ui;
    ui.retButton = true; ui.retSlider = 75.0f; ui.retCheckbox = true;
    SetScriptUI(&ui);

    Scene s("ui");
    GameObject* o = s.CreateGameObject("HUD");
    auto* sc = o->AddComponent<ScriptComponent>("okayscript");
    std::string err;
    bool ok = sc->LoadSource(
        "var clicked = false;\n"
        "var hp = 50;\n"
        "var snd = false;\n"
        "function update(dt) {\n"
        "    ui_begin(\"HUD\", 10, 10, 200, 100);\n"
        "    ui_text(\"Hello\");\n"
        "    if (ui_button(\"Hit\")) { clicked = true; }\n"
        "    hp = ui_slider(\"HP\", hp, 0, 100);\n"
        "    snd = ui_checkbox(\"Sound\", snd);\n"
        "    ui_end();\n"
        "}\n", &err);
    CHECK(ok);
    if (!ok) std::cerr << "  script error: " << err << "\n";

    s.Start();
    s.Update(1.0f / 60.0f);

    // The bridge saw the calls...
    CHECK(ui.begins == 1);
    CHECK(ui.ends == 1);
    CHECK(ui.texts == 1);
    CHECK(ui.lastText == "Hello");
    CHECK(ui.buttons == 1);
    CHECK(ui.lastSliderIn == 50.0f);   // the script passed in hp=50 this frame
    // ...and the returns landed back in the script.
    CHECK(sc->VM()->GetGlobal("clicked").AsBool());
    CHECK(sc->VM()->GetGlobal("hp").AsFloat() == 75.0f);
    CHECK(sc->VM()->GetGlobal("snd").AsBool());

    // With no bridge installed, the builtins are safe no-ops and echo values.
    SetScriptUI(nullptr);
    s.Update(1.0f / 60.0f);
    CHECK(sc->VM()->GetGlobal("hp").AsFloat() == 75.0f);   // ui_slider echoed hp unchanged

    // ---- The launcher-style blocks: ui_heading, cards, list items, tabs, inputs, toasts ----
    {
        struct KitUI : ScriptUIBridge {
            std::string calls, lastToast, lastInputIn;
            int tabsIn = -1, toastKind = -1;
            void Heading(const char* s) override { calls += std::string("H:") + s + ";"; }
            void Caption(const char* s, int r, int g, int b) override { calls += std::string("C:") + s + ":" + std::to_string(r + g + b) + ";"; }
            void BeginCard(const char* id) override { calls += std::string("[") + id + ";"; }
            void EndCard() override { calls += "];"; }
            bool ListItem(const char* t, const char* sub, const char* r, bool sel) override {
                calls += std::string("L:") + t + "/" + (sub ? sub : "-") + "/" + (r ? r : "-") + (sel ? "*" : "") + ";";
                return std::string(t) == "Rin";
            }
            void KeyValue(const char* k, const char* v) override { calls += std::string("K:") + k + "=" + v + ";"; }
            bool PrimaryButton(const char*) override { return true; }
            void Badge(const char* s, int, int, int) override { calls += std::string("B:") + s + ";"; }
            void Toast(const char* s, int kind, float) override { lastToast = s; toastKind = kind; }
            int  Tabs(const char* labels, int cur) override { calls += std::string("T:") + labels + ";"; tabsIn = cur; return 2; }
            const char* Input(const char*, const char* text) override { lastInputIn = text; return "edited"; }
            void Style(const char* n) override { calls += std::string("S:") + n + ";"; }
        } kit;
        SetScriptUI(&kit);
        Scene k("kit");
        auto* kc = k.CreateGameObject("Lobby")->AddComponent<ScriptComponent>("okayscript");
        std::string kerr;
        const bool kok = kc->LoadSource(
            "var tab = 0; var picked = \"\"; var name = \"Maya\"; var ready = false;\n"
            "function update(dt) {\n"
            "    ui_style(\"launcher\");\n"
            "    ui_caption(\"NOW PLAYING\");\n"
            "    ui_heading(\"Star Arena\");\n"
            "    tab = ui_tabs(\"Players|Match|Settings\", tab);\n"
            "    ui_card_begin(\"players\");\n"
            "    var names = [\"Maya\", \"Rin\"];\n"
            "    for (var i = 0; i < count(names); i = i + 1) {\n"
            "        if (ui_list_item(names[i], \"Ready\", \"\" + (i + 1), i == 1)) { picked = names[i]; }\n"
            "    }\n"
            "    ui_card_end();\n"
            "    ui_key_value(\"Ping\", \"23 ms\");\n"
            "    ui_badge(\"3 / 4\");\n"
            "    name = ui_input(\"Name\", name);\n"
            "    if (ui_primary_button(\"Ready\")) { ready = true; ui_toast(\"You're ready\", 1); }\n"
            "}\n", &kerr);
        CHECK(kok);
        if (!kok) std::cerr << "  script error: " << kerr << "\n";
        k.Start();
        k.Update(1.0f / 60.0f);
        CHECK(kit.calls.find("S:launcher;C:NOW PLAYING:467;H:Star Arena;T:Players|Match|Settings;[players;") == 0);
        CHECK(kit.calls.find("L:Maya/Ready/1;L:Rin/Ready/2*;];K:Ping=23 ms;B:3 / 4;") != std::string::npos);
        CHECK(kit.tabsIn == 0 && kc->VM()->GetGlobal("tab").AsFloat() == 2.0f);
        CHECK(kc->VM()->GetGlobal("picked").AsString() == "Rin");
        CHECK(kit.lastInputIn == "Maya" && kc->VM()->GetGlobal("name").AsString() == "edited");
        CHECK(kc->VM()->GetGlobal("ready").AsBool() && kit.lastToast == "You're ready" && kit.toastKind == 1);
        SetScriptUI(nullptr);
    }

    TEST_MAIN_RESULT();
}
