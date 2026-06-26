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

    TEST_MAIN_RESULT();
}
