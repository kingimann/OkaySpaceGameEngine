// The printf-style number format of the drag, slider and float input widgets (`fmt` in ImGui::DragFloat("x", &v, 0.1f, 0, 1, "%.2f px")),
// and the slider flags NoInput and NoRoundToFormat. Headless. Every expected text below was worked out by hand from the format
// (what C's printf prints for it), not by running the code.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICompat.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
std::string Fmt(const char* f, double v, bool isInt = false, bool* ok = nullptr, int* dec = nullptr) {
    char b[64];
    const bool r = UI::FormatNumber(b, sizeof b, f, v, isInt, dec);
    if (ok) *ok = r;
    return b;
}
}

int main() {
    RUN_SUITE("okayui number format");
    {   // the formatter
        CHECK(Fmt("%.2f", 3.14159) == "3.14");
        CHECK(Fmt("%.0f", 2.6) == "3");
        CHECK(Fmt("%.1f px", 12.34) == "12.3 px");
        CHECK(Fmt("%d", 42, true) == "42");
        CHECK(Fmt("%d%%", 50, true) == "50%");
        CHECK(Fmt("%05.1f", 3.14159) == "003.1");
        CHECK(Fmt("%+d", 5, true) == "+5");
        CHECK(Fmt("%.2f", 7.0, true) == "7.00");      // a whole number shown with a decimal conversion
        CHECK(Fmt("%d", 2.6) == "3");                 // a decimal number shown with a whole-number conversion is rounded
        CHECK(Fmt("%d", -2.6) == "-3");
        CHECK(Fmt("%g", 0.5) == "0.5");
        CHECK(Fmt("%x", 255, true) == "ff");
        CHECK(Fmt("%X", 255, true) == "FF");
        CHECK(Fmt("%ld", 7, true) == "7");
        CHECK(Fmt("%lld", 123456789012.0, true) == "123456789012");
        CHECK(Fmt("%8.2f|", 1.5) == "    1.50|");
        CHECK(Fmt("%-6d|", 12, true) == "12    |");
        CHECK(Fmt("x %.1f y", 2.25).substr(0, 2) == "x ");
        CHECK(Fmt("", 5.0).empty());
        CHECK(Fmt("no number", 5.0) == "no number");
        CHECK(Fmt("%.3f s", 0.0) == "0.000 s");
        CHECK(Fmt("%.2f", 1.0e30).size() > 20);       // large values do not break the text
        char small[4]; CHECK(UI::FormatNumber(small, sizeof small, "%.3f", 1.5, false) && std::strcmp(small, "1.5") == 0);   // cut to fit
        CHECK(!UI::FormatNumber(nullptr, 0, "%d", 1, true));
    }
    {   // refused formats become the default and report false
        bool ok = true;
        CHECK(Fmt("%s", 1.5, false, &ok) == "1.500" && !ok);
        CHECK(Fmt("%d %d", 3.0, true, &ok) == "3" && !ok);
        CHECK(Fmt("%n", 1.5, false, &ok) == "1.500" && !ok);
        CHECK(Fmt("abc %", 1.5, false, &ok) == "1.500" && !ok);
        CHECK(Fmt("%.2f and %f", 1.5, false, &ok) == "1.500" && !ok);
        CHECK(Fmt("%*d", 5.0, true, &ok) == "5" && !ok);   // a star width would read an argument that is not there
        CHECK(Fmt(nullptr, 1.5, false, &ok) == "1.500" && !ok);
        CHECK(Fmt("%p", 1.5, false, &ok) == "1.500" && !ok);
        CHECK(Fmt("%.2f%%", 1.5, false, &ok) == "1.50%" && ok);
    }
    {   // decimals shown
        int d = 99;
        Fmt("%.2f", 1.0, false, nullptr, &d); CHECK(d == 2);
        Fmt("%f", 1.0, false, nullptr, &d); CHECK(d == 6);
        Fmt("%.0f", 1.0, false, nullptr, &d); CHECK(d == 0);
        Fmt("%d", 1.0, true, nullptr, &d); CHECK(d == 0);
        Fmt("%g", 1.0, false, nullptr, &d); CHECK(d == -1);
        Fmt("text", 1.0, false, nullptr, &d); CHECK(d == -1);
    }
    {   // a drag field shows its value in the given format
        Reset(); Sim sim(400, 300);
        float f = 1.5f; int n = 40; float v3[3] = {1.4f, 2.6f, 7.6f}; float lo = 1.234f, hi = 5.678f; float fi = 0.5f;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 380, 280)) {
                UI::DragFloat("a", &f, 0.1f, 0.0f, 10.0f);                    // the core widget: its own "%.3f"
                CHECK(std::strcmp(UI::GetLastNumberText(), "1.500") == 0);
                UI::DragFloat("b", &f, 0.1f, 0.0f, 10.0f, "%.2f px", 0);       // compat
                CHECK(std::strcmp(UI::GetLastNumberText(), "1.50 px") == 0);
                UI::DragInt("c", &n, 1.0f, 0, 100, "%d%%", 0);
                CHECK(std::strcmp(UI::GetLastNumberText(), "40%") == 0);
                UI::DragInt("c2", &n, 1.0f, 0, 100);
                CHECK(std::strcmp(UI::GetLastNumberText(), "40") == 0);
                UI::DragFloat3("d", v3, 0.1f, 0.0f, 0.0f, "%.0f", 0);
                CHECK(std::strcmp(UI::GetLastNumberText(0), "1") == 0 && std::strcmp(UI::GetLastNumberText(1), "3") == 0 &&
                      std::strcmp(UI::GetLastNumberText(2), "8") == 0 && std::strcmp(UI::GetLastNumberText() , "8") == 0);
                UI::DragFloatRange2("e", &lo, &hi, 0.1f, 0.0f, 10.0f, "%.1f", "%.2f", 0);
                CHECK(std::strcmp(UI::GetLastNumberText(0), "1.2") == 0 && std::strcmp(UI::GetLastNumberText(1), "5.68") == 0);
                UI::DragFloatRange2("e2", &lo, &hi, 0.1f, 0.0f, 10.0f, "%.1f", nullptr, 0);   // no max format: the first one is used
                CHECK(std::strcmp(UI::GetLastNumberText(1), "5.7") == 0);
                UI::InputFloat("g", &fi, 0.1f, 1.0f, "%.1f", 0);
                CHECK(std::strcmp(UI::GetLastNumberText(), "0.5") == 0);
                UI::DragFloat("h", &f, 0.1f, 0.0f, 10.0f, "%s", 0);            // a bad format falls back to the default
                CHECK(std::strcmp(UI::GetLastNumberText(), "1.500") == 0);
                UI::DragFloat("i", &f, 0.1f, 0.0f, 10.0f);                    // and the format does not stick to the next widget
                CHECK(std::strcmp(UI::GetLastNumberText(), "1.500") == 0);
            }
            UI::End();
        };
        sim.Frames(3, ui);
        CHECK(sim.frames == 3);
    }
    {   // the format and the picture agree: a different format changes the pixels, the same format does not
        auto draw = [](const char* fmt) {
            Reset(); Sim sim(300, 80); float f = 1.5f;
            auto ui = [&] { UI::BareNextWindow(); if (UI::Begin("w", 4, 4, 290, 70)) { UI::DragFloat("", &f, 0.1f, 0.0f, 10.0f, fmt, 0); } UI::End(); };
            sim.Frames(2, ui);
            return sim.Draw();
        };
        const Bitmap a = draw("%.1f"), b = draw("%.1f"), c = draw("%.3f");
        auto same = [](const Bitmap& x, const Bitmap& y) { for (int j = 0; j < 80; ++j) for (int i = 0; i < 300; ++i) if (PixelAt(x, i, j) != PixelAt(y, i, j)) return false; return true; };
        CHECK(same(a, b));
        CHECK(!same(a, c));
    }
    {   // slider shows its value centred only when a format is given
        Reset(); Sim sim(400, 120);
        float f = 0.5f; int n = 3; UI::Rect r;
        bool withFmt = true;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 10, 10, 380, 100)) {
                UI::SetNextItemWidth(300);
                if (withFmt) UI::SliderFloat("", &f, 0.0f, 1.0f, "%.2f", 0); else UI::SliderFloat("", &f, 0.0f, 1.0f);
                r = UI::GetItemRect();
                if (withFmt) { CHECK(std::strcmp(UI::GetLastNumberText(), "0.50") == 0); }
                else { CHECK(UI::GetLastNumberText()[0] == 0); }
                UI::SetNextItemWidth(300);
                UI::SliderInt("##i", &n, 0, 10, "%d / 10", 0);
                CHECK(std::strcmp(UI::GetLastNumberText(), "3 / 10") == 0);
            }
            UI::End();
        };
        sim.Frames(2, ui);
        withFmt = false; sim.Frames(2, ui);
    }
    {   // NoRoundToFormat: a slider value follows the shown decimals, unless the flag asks for the raw value
        for (int round = 1; round >= 0; --round) {
            Reset(); Sim sim(400, 120);
            float v = 0.0f; UI::Rect r;
            auto ui = [&] {
                UI::BareNextWindow();
                if (UI::Begin("w", 10, 10, 380, 100)) {
                    UI::SetNextItemWidth(300);
                    UI::SliderFloat("", &v, 0.0f, 1.0f, "%.1f", round ? 0 : UI::Slider_NoRoundToFormat);
                    r = UI::GetItemRect();
                }
                UI::End();
            };
            sim.Frames(2, ui);
            sim.Move(r.x + r.w * 0.3333f, CenterY(r)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
            if (round) CHECK(std::fabs(v - 0.3f) < 1e-6f);
            else CHECK(v > 0.2f && v < 0.4f && std::fabs(v - 0.3f) > 0.001f);
        }
    }
    {   // NoInput: Ctrl+click types a number (default), or is just a click on the slider
        for (int noInput = 0; noInput <= 1; ++noInput) {
            Reset(); Sim sim(400, 120);
            float v = 0.0f; UI::Rect r;
            auto ui = [&] {
                UI::BareNextWindow();
                if (UI::Begin("w", 10, 10, 380, 100)) {
                    UI::SetNextItemWidth(300);
                    UI::SliderFloat("", &v, 0.0f, 1.0f, "%.2f", noInput ? UI::Slider_NoInput : 0);
                    r = UI::GetItemRect();
                }
                UI::End();
            };
            sim.Frames(2, ui);
            sim.ctrl = true; sim.Move(r.x + r.w * 0.5f, CenterY(r)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui); sim.ctrl = false;
            sim.Type("0.9"); sim.Frame(ui); sim.Tap(Key_Enter, ui); sim.Frames(2, ui);
            if (noInput) CHECK(std::fabs(v - 0.5f) < 0.02f);     // the click set the value; the typing went nowhere
            else CHECK(std::fabs(v - 0.9f) < 1e-6f);             // the typed number
        }
    }
    {   // typed numbers are rounded to the format too (drag), unless NoRoundToFormat
        for (int round = 1; round >= 0; --round) {
            Reset(); Sim sim(400, 120);
            float v = 0.0f; UI::Rect r;
            auto ui = [&] {
                UI::BareNextWindow();
                if (UI::Begin("w", 10, 10, 380, 100)) {
                    UI::SetNextItemWidth(300);
                    UI::DragFloat("", &v, 0.1f, 0.0f, 10.0f, "%.1f", round ? 0 : UI::Slider_NoRoundToFormat);
                    r = UI::GetItemRect();
                }
                UI::End();
            };
            sim.Frames(2, ui);
            sim.ctrl = true; sim.Move(CenterX(r), CenterY(r)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui); sim.ctrl = false;
            sim.Type("2.345"); sim.Frame(ui); sim.Tap(Key_Enter, ui); sim.Frames(2, ui);
            if (round) CHECK(std::fabs(v - 2.3f) < 1e-6f);      // 2.345 shown with one decimal: 2.3
            else CHECK(std::fabs(v - 2.345f) < 1e-6f);
        }
    }
    {   // Logarithmic: halfway along the handle is the geometric middle (1..100 -> 10), and the range keeps its ends
        struct Case { float mn, mx, start, at, expect; };
        const Case cases[] = {
            {1.0f, 100.0f, 1.0f, 0.5f, 10.0f},          // 1 * 100^0.5
            {1.0f, 100.0f, 1.0f, 0.25f, 3.1623f},       // 100^0.25 = 10^0.5
            {200.0f, 20000.0f, 200.0f, 0.5f, 2000.0f},  // 200 * 100^0.5
            {-100.0f, -1.0f, -50.0f, 0.5f, -10.0f},     // both ends negative: -100 * (0.01)^0.5
            {0.0f, 1.0f, 0.2f, 0.5f, 0.5f},             // the range touches 0: stays linear
            {-1.0f, 1.0f, 0.2f, 0.5f, 0.0f},            // the range crosses 0: stays linear
        };
        for (const Case& c : cases) {
            for (int lg = 0; lg <= 1; ++lg) {
                Reset(); Sim sim(400, 120);
                float v = c.start; UI::Rect r;
                auto ui = [&] {
                    UI::BareNextWindow();
                    if (UI::Begin("w", 10, 10, 380, 100)) {
                        UI::SetNextItemWidth(300);
                        UI::SliderFloat("", &v, c.mn, c.mx, "%.4f", lg ? UI::Slider_Logarithmic : 0);
                        r = UI::GetItemRect();
                    }
                    UI::End();
                };
                sim.Frames(2, ui);
                sim.Move(r.x + r.w * c.at, CenterY(r)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
                const float linear = c.mn + c.at * (c.mx - c.mn);
                const bool logOk = c.mn * c.mx > 0.0f;
                const float want = (lg && logOk) ? c.expect : linear;
                if (!(std::fabs(v - want) <= 0.01f * std::fabs(want) + 0.01f)) std::printf("case %g..%g at %g lg=%d got %g want %g\n", c.mn, c.mx, c.at, lg, v, want);
                CHECK(std::fabs(v - want) <= 0.01f * std::fabs(want) + 0.01f);
            }
        }
        // dragging the whole way stays inside the range
        Reset(); Sim sim(400, 120); float v = 5.0f; UI::Rect r;
        auto ui = [&] { UI::BareNextWindow(); if (UI::Begin("w", 10, 10, 380, 100)) { UI::SetNextItemWidth(300); UI::SliderFloat("", &v, 1.0f, 100.0f, "%.2f", UI::Slider_Logarithmic); r = UI::GetItemRect(); } UI::End(); };
        sim.Frames(2, ui);
        sim.Move(r.x + 2, CenterY(r)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Move(r.x + r.w + 200, CenterY(r)); sim.Frame(ui); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(v >= 1.0f && v <= 100.0f && std::fabs(v - 100.0f) < 0.5f);
        sim.Move(r.x - 300, CenterY(r)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(v >= 1.0f && v <= 100.0f);
    }
    {   // hostile input: huge and non-finite values, long labels and formats never crash and always give a terminated text
        Reset(); Sim sim(400, 200);
        float vals[] = {0.0f, -0.0f, 1e30f, -1e30f, INFINITY, -INFINITY, NAN, 1e-30f};
        const char* fmts[] = {"%f", "%e", "%g", "%d", "%x", "%.20f", "%99.99f", "%-50d|", "%", "%%%", "%.", "%.f", "%5", "%l", "%lld", "%hhd", "", "%.2f%.2f"};
        for (float v : vals) for (const char* f : fmts) {
            float x = v; int ix = (int)(v > 1e9f ? 1000000000 : (v < -1e9f ? -1000000000 : 0));
            auto ui = [&] {
                UI::BareNextWindow();
                if (UI::Begin("w", 10, 10, 380, 180)) {
                    UI::DragFloat("a", &x, 0.1f, 0.0f, 0.0f, f, 0);
                    UI::DragInt("b", &ix, 0.1f, 0, 0, f, 0);
                    UI::SliderFloat("c", &x, -1.0f, 1.0f, f, 0);
                    UI::InputFloat("d", &x, 0.1f, 1.0f, f, 0);
                    CHECK(std::strlen(UI::GetLastNumberText()) < 64);
                }
                UI::End();
            };
            Reset(); sim.Frames(1, ui);
        }
    }
    TEST_MAIN_RESULT();
}
