// OkayUI golden images: committed PNGs (written with okay::ImageWrite, read back with the test kit's own inflate)
// compared against what the software backend draws from OkayUI's draw list. OKAYUI_UPDATE_GOLDEN=1 regenerates them.
#include "test_framework.hpp"
#include "../demo/DemoScene.hpp"

#include <cstring>

using namespace okayuitest;
namespace UI = OkayUI;

static void Check(const char* name, const Bitmap& b) {
    std::string why;
    const bool ok = MatchesGolden(name, b, &why);
    if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
    CHECK(ok);
}

int main() {
    RUN_SUITE("okayui golden");
    {   // explicit-coordinate widgets: panel, buttons, progress, switch, radios, tabs, checkboxes, a slider, a text field
        Reset();
        Sim sim(420, 220);
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
        Check("primitives", sim.Draw(12, 14, 20));
    }
    {   // a button in its three states
        Reset();
        Sim sim(330, 70);
        UI::Rect r[3];
        auto ui = [&] {
            if (UI::Begin("states", 0, 0, 330, 70)) {
                UI::Button("Idle"); r[0] = UI::GetItemRect(); UI::SameLine();
                UI::Button("Hover"); r[1] = UI::GetItemRect(); UI::SameLine();
                UI::Button("Held"); r[2] = UI::GetItemRect();
            }
            UI::End();
        };
        sim.Frames(2, ui);
        sim.Move(CenterX(r[2]), CenterY(r[2])); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        const Bitmap held = sim.Draw(12, 14, 20);
        auto at = [&](const Bitmap& b, const UI::Rect& rr) { return PixelAt(b, (int)rr.x + 6, (int)rr.y + 6); };
        CHECK(at(held, r[0]) == ThemeRgba(UI::Style().bg));
        CHECK(at(held, r[2]) == ThemeRgba(UI::Style().bgDown));
        Check("button_held", held);
        sim.Up(); sim.Move(CenterX(r[1]), CenterY(r[1])); sim.Frames(2, ui);
        const Bitmap hov = sim.Draw(12, 14, 20);
        CHECK(at(hov, r[1]) == ThemeRgba(UI::Style().bgHover) && at(hov, r[2]) == ThemeRgba(UI::Style().bg));
        Check("button_hover", hov);
    }
    {   // the demo window after its scripted interaction, classic look
        Reset();
        Sim sim(800, 780);
        okayuidemo::State st;
        okayuidemo::RunScript(sim, st);
        CHECK(st.clicks == 3 && std::fabs(st.slider - 0.7f) < 0.02f && !std::strcmp(st.name, "Okay Space 2026") && st.combo == 1);
        Check("demo_window", sim.Draw(12, 14, 20));
    }
    {   // the same window in the launcher look (TrueType atlas through the software backend)
        Reset();
        UI::UseLauncherLook(true); UI::Style().caretBlink = false;
        Sim sim(800, 780);
        okayuidemo::State st;
        okayuidemo::RunScript(sim, st);
        CHECK(st.clicks == 3 && !std::strcmp(st.name, "Okay Space 2026"));
        const UI::DrawData d = UI::GetDrawData();
        CHECK(d.atlasRgba != nullptr);
        Check("demo_window_launcher", sim.Draw(12, 14, 20));
        UI::UseLauncherLook(false);
    }
    {   // determinism: the same frames render the same pixels, also after a reset in between
        Bitmap a, b;
        for (int pass = 0; pass < 2; ++pass) {
            Reset();
            Sim sim(800, 780);
            okayuidemo::State st;
            okayuidemo::RunScript(sim, st);
            (pass == 0 ? a : b) = sim.Draw(12, 14, 20);
        }
        CHECK(a.rgba == b.rgba);
    }
    {   // our PNG reader round-trips what ImageWrite wrote
        Bitmap bm(33, 17); bm.Clear(1, 2, 3);
        for (int i = 0; i < 33 * 17; ++i) bm.rgba[(size_t)i * 4] = (unsigned char)(i * 7);
        const std::string tmp = "okayui_roundtrip.png";
        CHECK(WritePng(tmp, bm));
        Png p = ReadPng(tmp);
        CHECK(p.ok && p.w == 33 && p.h == 17 && p.rgba == bm.rgba);
        std::remove(tmp.c_str());
        CHECK(!ReadPng("does-not-exist.png").ok);
    }
    TEST_MAIN_RESULT();
}
