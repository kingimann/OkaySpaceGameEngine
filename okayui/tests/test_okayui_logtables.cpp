// OkayUI step: logarithmic sliders and drags (with printf-style formats), and table column reordering and hiding (header drag, the
// right-click menu, saved and restored table state). Headless: the Sim driver, synthetic mouse, software pixels.
// OKAYUI_FUZZ_ITERS scales the fuzz (default 800).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

bool Near(float a, float b, float e) { return std::fabs(a - b) <= e; }

void TestMapping() {
    RUN_SUITE("log sliders: the value <-> ratio mapping");
    // linear
    CHECK(Near(UI::SliderRatioFromValue(5, 0, 10, false), 0.5f, 1e-6f));
    CHECK(Near(UI::SliderValueFromRatio(0.25f, 0, 10, false), 2.5f, 1e-5f));
    // a positive range: the geometric middle is in the middle
    CHECK(Near(UI::SliderValueFromRatio(0.5f, 1, 100, true), 10.0f, 0.001f));
    CHECK(Near(UI::SliderRatioFromValue(10, 1, 100, true), 0.5f, 1e-5f));
    CHECK(Near(UI::SliderValueFromRatio(0.5f, 0.01f, 100, true), 1.0f, 0.001f));
    CHECK(Near(UI::SliderValueFromRatio(0.0f, 1, 100, true), 1.0f, 0) && Near(UI::SliderValueFromRatio(1.0f, 1, 100, true), 100.0f, 0));
    // round trip over many values, positive, negative, touching zero, crossing zero, flipped
    struct R { float lo, hi; } ranges[] = {{1, 100}, {0.005f, 4}, {200, 20000}, {0, 1000}, {-100, -1}, {-1000, 0}, {-50, 50}, {-1, 1000}, {100, 1}, {0, 1}};
    for (const R& r : ranges) {
        const float lo = std::min(r.lo, r.hi), hi = std::max(r.lo, r.hi);
        float prev = -1; bool mono = true; int bad = 0;
        for (int i = 0; i <= 100; ++i) {
            const float t = i / 100.0f;
            const float v = UI::SliderValueFromRatio(t, r.lo, r.hi, true);
            if (!(v >= lo - 1e-4f && v <= hi + 1e-4f)) ++bad;
            const float t2 = UI::SliderRatioFromValue(v, r.lo, r.hi, true);
            if (std::fabs(t2 - t) > 0.01f) ++bad;
            if (r.lo <= r.hi && i > 0 && v < prev - 1e-4f) mono = false;
            if (r.lo > r.hi && i > 0 && v > prev + 1e-4f) mono = false;
            prev = v;
        }
        CHECK(bad == 0);
        CHECK(mono);
    }
    // zero in the middle of a symmetric range sits at 0.5
    CHECK(Near(UI::SliderRatioFromValue(0, -50, 50, true), 0.5f, 1e-6f));
    CHECK(Near(UI::SliderValueFromRatio(0.5f, -50, 50, true), 0.0f, 1e-6f));
    CHECK(Near(UI::SliderValueFromRatio(0.75f, -50, 50, true), -UI::SliderValueFromRatio(0.25f, -50, 50, true), 1e-4f));
    // the smallest step near zero follows the decimals
    CHECK(UI::SliderValueFromRatio(0.001f, 0, 1000, true, 3) < 0.01f);
    CHECK(UI::SliderValueFromRatio(0.5f, 0, 1000, true, 3) < 2.0f && UI::SliderValueFromRatio(0.5f, 0, 1000, true, 0) > 20.0f);
    // hostile numbers never give a non-finite result
    const float bad[] = {NAN, INFINITY, -INFINITY, 1e30f, -1e30f, 0.0f, -0.0f, 1e-30f};
    for (float a : bad) for (float b : bad) for (float c : bad) {
        const float r1 = UI::SliderRatioFromValue(a, b, c, true), r2 = UI::SliderValueFromRatio(a, b, c, true), r3 = UI::SliderRatioFromValue(a, b, c, false);
        CHECK(std::isfinite(r1) && r1 >= 0 && r1 <= 1 && std::isfinite(r2) && std::isfinite(r3));
    }
    CHECK(UI::SliderRatioFromValue(5, 5, 5, true) == 0.0f);
}

void TestFormat() {
    RUN_SUITE("number formats");
    char b[64];
    CHECK(UI::FormatNumber(b, 64, "%.2f", 3.14159, false) && std::string(b) == "3.14");
    CHECK(UI::FormatNumber(b, 64, "%.0f Hz", 440.4, false) && std::string(b) == "440 Hz");
    CHECK(UI::FormatNumber(b, 64, "%d%%", 55.0, true) && std::string(b) == "55%");
    CHECK(UI::FormatNumber(b, 64, "%.2fx", 2.0, false) && std::string(b) == "2.00x");
    CHECK(UI::FormatNumber(b, 64, "-", 2.0, true) && std::string(b) == "-");                 // no conversion: literal text
    CHECK(UI::FormatNumber(b, 64, "%5.1f|", 2.25, false) && (std::string(b) == "  2.2|" || std::string(b) == "  2.3|"));
    CHECK(UI::FormatNumber(b, 64, "%08.3f", -1.5, false) && std::string(b) == "-001.500");
    CHECK(UI::FormatNumber(b, 64, "%+d", 7.0, true) && std::string(b) == "+7");
    CHECK(UI::FormatNumber(b, 64, "%ld items", 3.0, true) && std::string(b) == "3 items");
    CHECK(UI::FormatNumber(b, 64, "%g", 0.5, false) && std::string(b) == "0.5");
    CHECK(UI::FormatNumber(b, 64, "%d", 2.6, true) && std::string(b) == "3");                // an int format rounds the value
    // refused: two conversions, strings, %n, wrong kind, unfinished, nul buffer
    CHECK(!UI::FormatNumber(b, 64, "%d %d", 1, true) && b[0] == 0);
    CHECK(!UI::FormatNumber(b, 64, "%s", 1, false));
    CHECK(!UI::FormatNumber(b, 64, "%n", 1, true));
    CHECK(!UI::FormatNumber(b, 64, "%f", 1, true));
    CHECK(!UI::FormatNumber(b, 64, "%d", 1, false));
    CHECK(!UI::FormatNumber(b, 64, "%", 1, false) && !UI::FormatNumber(b, 64, "%.", 1, false) && !UI::FormatNumber(b, 64, "%5", 1, false));
    CHECK(!UI::FormatNumber(b, 64, "%x", 1, true));
    CHECK(!UI::FormatNumber(nullptr, 64, "%d", 1, true) && !UI::FormatNumber(b, 0, "%d", 1, true) && !UI::FormatNumber(b, 64, nullptr, 1, true));
    // truncation is safe, huge widths and precisions are bounded
    CHECK(UI::FormatNumber(b, 5, "%.2f", 123456.789, false) && std::strlen(b) == 4);
    CHECK(!UI::FormatNumber(b, 64, "%99999999.99999999f", 1.0, false) || std::strlen(b) < 64);
    CHECK(UI::FormatNumber(b, 64, "%.2f", NAN, false) && UI::FormatNumber(b, 64, "%d", INFINITY, true));
    CHECK(UI::FormatDecimals("%.2f", 3) == 2 && UI::FormatDecimals("%f", 3) == 6 && UI::FormatDecimals("%d", 3) == 0 && UI::FormatDecimals("none", 3) == 3 && UI::FormatDecimals(nullptr, 3) == 3);
    CHECK(UI::FormatDecimals("%.0f Hz", 3) == 0 && UI::FormatDecimals("%.12f", 3) == 9);
}

struct SliderRig {
    Sim sim{500, 200};
    float v = 1.0f; int iv = 5;
    UI::Rect box;
    SliderRig() { Reset(); }
    template <class F> void Run(F f) { sim.Frame([&] { UI::Begin("W", 10, 10, 480, 180); f(); box = UI::GetItemRect(); UI::End(); }); }
};

void TestSliderWidget() {
    RUN_SUITE("log sliders: the widget");
    SliderRig r;
    auto drawLin = [&] { UI::SliderFloat("", &r.v, 1, 100, "%.1f", 0); };
    auto drawLog = [&] { UI::SliderFloat("", &r.v, 1, 100, "%.1f", UI::Slider_Logarithmic); };
    r.v = 1; r.Run(drawLog); r.Run(drawLog);
    const UI::Rect box = r.box;
    auto pressAt = [&](float t, auto&& draw) { r.sim.Move(box.x + box.w * t, box.y + box.h * 0.5f); r.Run(draw); r.sim.Down(); r.Run(draw); r.sim.Up(); r.Run(draw); r.Run(draw); };
    pressAt(0.5f, drawLog);
    CHECK(Near(r.v, 10.0f, 0.6f));                       // the middle of a log slider over 1..100
    r.v = 1; r.Run(drawLin); pressAt(0.5f, drawLin);
    CHECK(Near(r.v, 50.5f, 1.5f));                       // the same click on a linear one
    r.v = 1; r.Run(drawLog); pressAt(0.9995f, drawLog);
    CHECK(Near(r.v, 100.0f, 0.5f));
    r.v = 50; r.Run(drawLog); pressAt(0.0f, drawLog);
    CHECK(Near(r.v, 1.0f, 0.01f));
    // the value is rounded to the format's decimals
    r.v = 1; r.Run(drawLog); pressAt(0.3f, drawLog);
    CHECK(Near(std::floor(r.v * 10.0f + 0.5f) / 10.0f, r.v, 1e-4f));
    // NoRoundToFormat keeps the full value; a format-less slider never rounds
    float raw = 1; auto drawRaw = [&] { UI::SliderFloat("", &raw, 1, 100, "%.0f", UI::Slider_Logarithmic | UI::Slider_NoRoundToFormat); };
    r.Run(drawRaw); pressAt(0.37f, drawRaw);
    CHECK(std::fabs(raw - std::floor(raw + 0.5f)) > 1e-3f);
    // the value text is drawn over the groove only when a format is given
    float a = 5, b = 5;
    r.sim.Move(0, 0);
    r.Run([&] { UI::SliderFloat("", &a, 0, 10); });
    const Bitmap plain = r.sim.Draw(0, 0, 0);
    r.Run([&] { UI::SliderFloat("", &b, 0, 10, "%.2f"); });
    const Bitmap withText = r.sim.Draw(0, 0, 0);
    int diff = 0; for (size_t i = 0; i < plain.rgba.size(); ++i) if (plain.rgba[i] != withText.rgba[i]) ++diff;
    CHECK(diff > 100);
    // Ctrl+click types a number (rounded and clamped), NoInput turns it off
    r.v = 5; auto drawT = [&] { UI::SliderFloat("", &r.v, 0, 10, "%.1f", 0); };
    r.Run(drawT); r.Run(drawT);
    r.sim.ctrl = true; r.sim.Move(r.box.x + 20, r.box.y + 8); r.Run(drawT); r.sim.Down(); r.Run(drawT); r.sim.Up(); r.Run(drawT);
    r.sim.ctrl = false;
    r.sim.Press(Key_SelectAll); r.Run(drawT); r.sim.Type("99.77"); r.Run(drawT); r.sim.Press(Key_Enter); r.Run(drawT); r.Run(drawT);
    CHECK(Near(r.v, 10.0f, 0.001f));
    // integer slider with a format
    r.iv = 3; auto drawI = [&] { UI::SliderInt("", &r.iv, 0, 100, "%d%%", UI::Slider_Logarithmic); };
    r.Run(drawI); r.Run(drawI);
    const UI::Rect ib = r.box;
    r.sim.Move(ib.x + ib.w * 0.5f, ib.y + 8); r.Run(drawI); r.sim.Down(); r.Run(drawI); r.sim.Up(); r.Run(drawI);
    CHECK(r.iv >= 8 && r.iv <= 12);                          // the geometric middle of 0..100 with whole-number steps (smallest step 1) is 10, not 50
    // an unusable format is ignored, the slider still works
    float f2 = 1; auto drawBad = [&] { UI::SliderFloat("", &f2, 0, 10, "%s %n", 0); };
    r.Run(drawBad); r.sim.Move(r.box.x + r.box.w * 0.5f, r.box.y + 8); r.Run(drawBad); r.sim.Down(); r.Run(drawBad); r.sim.Up(); r.Run(drawBad);
    CHECK(Near(f2, 5.0f, 0.5f));
    // inverted and degenerate ranges do nothing wrong
    float f3 = 3; r.Run([&] { UI::SliderFloat("", &f3, 10, 1, "%.1f", UI::Slider_Logarithmic); UI::SliderFloat("t", &f3, 5, 5, "%.1f", UI::Slider_Logarithmic); UI::SliderFloat("u", &f3, NAN, 5, "%.1f", UI::Slider_Logarithmic); });
    CHECK(std::isfinite(f3));
}

void TestDragWidget() {
    RUN_SUITE("log drags");
    Sim sim(500, 200);
    Reset();
    float v = 1.0f; int iv = 10;
    UI::Rect box;
    auto frame = [&](int kind, int flags) {
        sim.Frame([&] {
            UI::Begin("W", 10, 10, 480, 180);
            if (kind == 0) UI::DragFloat("##d", &v, 1.0f, 1.0f, 1000.0f, "%.2f", flags);
            else if (kind == 1) UI::DragInt("##d", &iv, 1.0f, 1, 1000, "%d px", flags);
            else UI::DragFloat("##d", &v, 1.0f, 1.0f, 1000.0f);
            box = UI::GetItemRect();
            UI::End();
        });
    };
    auto drag = [&](int kind, int flags, float dx, int steps) {
        frame(kind, flags); frame(kind, flags);
        sim.Move(box.x + box.w * 0.5f, box.y + 8); frame(kind, flags);
        sim.Down(); frame(kind, flags);
        for (int i = 1; i <= steps; ++i) { sim.Move(box.x + box.w * 0.5f + dx * i / steps, box.y + 8); frame(kind, flags); }
        sim.Up(); frame(kind, flags);
    };
    // linear: speed 1 per pixel
    v = 100; drag(0, 0, 20, 4);
    CHECK(Near(v, 120.0f, 0.5f));
    // log: the same drag at a small value moves it a little, at a big value a lot (ratio space: 20 px * 1 / 999 of the log scale)
    v = 2; drag(0, UI::Slider_Logarithmic, 20, 4);
    const float smallStep = v - 2.0f;
    v = 500; drag(0, UI::Slider_Logarithmic, 20, 4);
    const float bigStep = v - 500.0f;
    CHECK(smallStep > 0 && bigStep > 0 && bigStep > smallStep * 20.0f);
    // dragging back and forth returns (log and rounding aside)
    v = 30; drag(0, UI::Slider_Logarithmic, 100, 5); const float up = v; drag(0, UI::Slider_Logarithmic, -100, 5);
    CHECK(up > 30.0f && Near(v, 30.0f, 3.0f));
    // clamped to the range
    v = 999; drag(0, UI::Slider_Logarithmic, 400, 8);
    CHECK(Near(v, 1000.0f, 0.001f));
    v = 1.5f; drag(0, UI::Slider_Logarithmic, -400, 8);
    CHECK(Near(v, 1.0f, 0.001f));
    // rounded to the format
    v = 10; drag(0, UI::Slider_Logarithmic, 7, 3);
    CHECK(Near(std::floor(v * 100.0f + 0.5f) / 100.0f, v, 1e-4f));
    // integer log drag: small steps near 1 still move after enough drag
    iv = 1; drag(1, UI::Slider_Logarithmic, 300, 20);
    CHECK(iv > 1 && iv <= 1000);
    iv = 10; drag(1, 0, 15, 5);
    CHECK(iv == 25);
    // Logarithmic with no range is ignored (linear)
    sim.Frame([&] { UI::Begin("W", 10, 10, 480, 180); UI::DragFloat("##x", &v, 1.0f, 0.0f, 0.0f, "%.1f", UI::Slider_Logarithmic); UI::DragInt("##y", &iv, 1.0f, 0, 0, "%d", UI::Slider_Logarithmic); UI::End(); });
    CHECK(std::isfinite(v));
}

// ---- tables: reorder and hide -----------------------------------------------------------------------------------------------

struct TableRig {
    Sim sim{640, 300};
    int flags = UI::Table_Reorderable | UI::Table_Hideable | UI::Table_Sortable;
    int colFlags[4] = {0, 0, 0, 0};
    int freeze = 0;
    float cellX[4] = {0, 0, 0, 0}, cellW[4] = {0, 0, 0, 0};
    bool cellDrawn[4] = {false, false, false, false};
    int cellsSeen[4] = {0, 0, 0, 0};
    bool sorted = false; int sortCol = -1;
    float headerY = 0, headerH = 0, tableX = 0, tableW = 0;
    std::string saved;
    TableRig() { Reset(); }
    void Frame() {
        sim.Frame([&] {
            UI::Begin("W", 10, 10, 620, 280);
            const UI::Rect before = UI::GetItemRect(); (void)before;
            const UI::Vec2 cur = UI::GetCursorScreenPos();
            headerY = cur.y; headerH = UI::GetFrameHeight(); tableX = cur.x;
            if (UI::BeginTable("cols", 4, (UI::TableFlags)flags)) {
                UI::TableSetupColumn("Name", colFlags[0], 1); UI::TableSetupColumn("Size", colFlags[1], 1); UI::TableSetupColumn("Type", colFlags[2], 1); UI::TableSetupColumn("Date", colFlags[3], 1);
                if (freeze) UI::TableSetupScrollFreeze(freeze, 0);
                UI::TableHeadersRow();
                if (UI::TableSortSpecs* sp = UI::TableGetSortSpecs()) if (sp->specsDirty) { sorted = true; sortCol = sp->specsCount ? sp->specs[0].columnIndex : -1; sp->specsDirty = false; }
                for (int row = 0; row < 2; ++row) {
                    UI::TableNextRow();
                    for (int c = 0; c < 4; ++c) {
                        UI::TableSetColumnIndex(c);
                        char t[16]; std::snprintf(t, sizeof t, "c%dr%d", c, row);
                        UI::Button(t);
                        if (row == 0) { cellX[c] = UI::GetItemRectMin().x; cellW[c] = UI::GetItemRectSize().x; ++cellsSeen[c]; }
                    }
                }
                if (!saved.empty() && saved == "save") saved = UI::TableSaveState();
                UI::EndTable();
            }
            UI::End();
        });
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Move(float x, float y) { sim.Move(x, y); Frame(); }
    // x of the middle of column `c`'s header, from where its cell is drawn
    float HeaderX(int c) const { return cellX[c] + cellW[c] * 0.5f; }
    float HeaderYMid() const { return headerY + headerH * 0.5f; }
    void DragHeader(int c, float toX) {
        const float x0 = HeaderX(c), y = HeaderYMid();
        Move(x0, y); sim.Down(); Frame();
        const int steps = 12;
        for (int i = 1; i <= steps; ++i) Move(x0 + (toX - x0) * i / steps, y);
        sim.Up(); Frame(); Frames(2);
    }
    void RightClickHeader(float x) { Move(x, HeaderYMid()); sim.rdown = true; Frame(); sim.rdown = false; Frame(); Frames(2); }
};

void TestHiding() {
    RUN_SUITE("tables: hide, show and reorder columns");
    {   // through the API
        TableRig r; r.flags = UI::Table_Reorderable | UI::Table_Hideable;
        r.Frames(3);
        CHECK(r.cellX[0] < r.cellX[1] && r.cellX[1] < r.cellX[2] && r.cellX[2] < r.cellX[3]);
        const float sp4 = r.cellX[1] - r.cellX[0], first = r.cellX[0];     // column spacing with four columns
        auto frame = [&](const std::function<void()>& inTable) {
            r.sim.Frame([&] {
                UI::Begin("W", 10, 10, 620, 280);
                if (UI::BeginTable("cols", 4, (UI::TableFlags)r.flags)) {
                    UI::TableSetupColumn("Name", r.colFlags[0], 1); UI::TableSetupColumn("Size", r.colFlags[1], 1); UI::TableSetupColumn("Type", r.colFlags[2], 1); UI::TableSetupColumn("Date", r.colFlags[3], 1);
                    UI::TableHeadersRow();
                    if (inTable) inTable();
                    UI::TableNextRow();
                    for (int c = 0; c < 4; ++c) { UI::TableSetColumnIndex(c); UI::Button("b"); r.cellX[c] = UI::GetItemRectMin().x; r.cellW[c] = UI::GetItemRectSize().x; }
                    UI::EndTable();
                }
                UI::End();
            });
        };
        frame([&] { CHECK(UI::TableSetColumnEnabled(1, false)); CHECK(!UI::TableGetColumnEnabled(1)); });
        frame(nullptr); frame(nullptr);
        // the other three share the width: the spacing of the shown ones is a third of the table
        CHECK(r.cellX[2] - r.cellX[0] > sp4 * 1.25f && Near(r.cellX[2] - r.cellX[0], r.cellX[3] - r.cellX[2], 1.0f) && Near(r.cellX[0], first, 0.5f));
        frame([&] { CHECK(!UI::TableGetColumnEnabled(1)); CHECK(UI::TableSetColumnEnabled(1, true)); });
        frame(nullptr);
        CHECK(Near(r.cellX[1] - r.cellX[0], sp4, 1.5f));                       // back to four equal shares
        frame([&] {
            CHECK(UI::TableSetColumnDisplayIndex(3, 0));
            CHECK(UI::TableGetColumnDisplayIndex(3) == 0 && UI::TableGetColumnDisplayIndex(0) == 1 && UI::TableGetColumnDisplayIndex(1) == 2 && UI::TableGetColumnDisplayIndex(2) == 3);
            CHECK(!UI::TableSetColumnDisplayIndex(9, 0) && UI::TableGetColumnDisplayIndex(-1) == -1);
        });
        frame(nullptr);
        CHECK(r.cellX[3] < r.cellX[0] && r.cellX[0] < r.cellX[1] && r.cellX[1] < r.cellX[2]);
        // the last visible column cannot be hidden; every other can
        frame([&] { CHECK(UI::TableSetColumnEnabled(0, false) && UI::TableSetColumnEnabled(1, false) && UI::TableSetColumnEnabled(2, false)); CHECK(!UI::TableSetColumnEnabled(3, false)); CHECK(UI::TableGetColumnEnabled(3)); });
        frame(nullptr);
        CHECK(Near(r.cellX[3], first, 0.5f));                                   // the one left starts where the table starts
        // outside a table and without the flags the calls do nothing
        CHECK(!UI::TableGetColumnEnabled(0) && !UI::TableSetColumnEnabled(0, false) && UI::TableGetColumnDisplayIndex(0) == -1);
        TableRig plain; plain.flags = UI::Table_Sortable;
        plain.sim.Frame([&] { UI::Begin("W", 10, 10, 620, 280); if (UI::BeginTable("p", 3, (UI::TableFlags)plain.flags)) { UI::TableSetupColumn("a"); UI::TableSetupColumn("b"); UI::TableSetupColumn("c"); CHECK(!UI::TableSetColumnEnabled(1, false)); CHECK(!UI::TableSetColumnDisplayIndex(0, 2)); CHECK(UI::TableGetColumnEnabled(1)); UI::EndTable(); } UI::End(); });
    }
    {   // DefaultHide
        TableRig r; r.flags = UI::Table_Hideable; r.colFlags[2] = UI::Col_DefaultHide;
        r.Frames(3);
        CHECK(r.cellX[1] - r.cellX[0] > 150 && Near(r.cellX[1] - r.cellX[0], r.cellX[3] - r.cellX[1], 1.0f));     // three columns share ~600 px: about 200 each, not 150
    }
}

void TestHeaderDrag() {
    RUN_SUITE("tables: dragging a header reorders, a click still sorts");
    TableRig r;
    r.Frames(4);
    const float colW = r.cellX[1] - r.cellX[0];
    r.DragHeader(0, r.cellX[1] + colW * 0.8f);                // drag "Name" past the middle of "Size"
    CHECK(r.cellX[0] > r.cellX[1] && r.cellX[1] < r.cellX[2]);
    CHECK(!r.sorted);                                         // a drag is not a sort click
    // the order is remembered across frames and a drag back restores it
    r.Frames(3);
    CHECK(r.cellX[0] > r.cellX[1]);
    r.DragHeader(0, r.cellX[1] + colW * 0.2f);
    CHECK(r.cellX[0] < r.cellX[1]);
    // a plain click on a header still sorts
    r.sorted = false; r.sortCol = -1;
    r.Move(r.HeaderX(2), r.HeaderYMid()); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frames(2);
    CHECK(r.sorted && r.sortCol == 2);
    // Col_NoReorder: the column does not move and others do not jump over it
    TableRig p; p.colFlags[0] = UI::Col_NoReorder;
    p.Frames(4);
    p.DragHeader(0, p.cellX[2]);
    CHECK(p.cellX[0] < p.cellX[1] && p.cellX[1] < p.cellX[2]);
    p.DragHeader(1, p.cellX[0] + colW * 0.2f);                // "Size" cannot pass the pinned "Name"
    CHECK(p.cellX[0] < p.cellX[1]);
    // without Table_Reorderable nothing moves
    TableRig q; q.flags = UI::Table_Sortable | UI::Table_Hideable;
    q.Frames(4);
    q.DragHeader(0, q.cellX[2]);
    CHECK(q.cellX[0] < q.cellX[1]);
    // frozen columns: a column does not cross the freeze boundary
    TableRig f; f.flags = UI::Table_Reorderable | UI::Table_ScrollX; f.freeze = 1;
    f.Frames(4);
    f.DragHeader(0, f.cellX[2]);
    CHECK(f.cellX[0] < f.cellX[1]);
}

void TestHeaderMenu() {
    RUN_SUITE("tables: the header's right-click menu");
    TableRig r; r.flags = UI::Table_Reorderable | UI::Table_Hideable;
    r.Frames(4);
    const float sp4 = r.cellX[1] - r.cellX[0];
    const Bitmap before = r.sim.Draw(0, 0, 0);
    r.RightClickHeader(r.HeaderX(1));
    const Bitmap menu = r.sim.Draw(0, 0, 0);
    int diff = 0; for (size_t i = 0; i < before.rgba.size(); ++i) if (before.rgba[i] != menu.rgba[i]) ++diff;
    CHECK(diff > 500);                                         // a menu came up
    // the entries, in display order: Name, Size, Type, Date, then "Reset order"; the first row sits under the pointer, each next one a row lower
    const float mx = r.sim.mx, my = r.sim.my;
    const float pitch = UI::GetFrameHeightWithSpacing();
    auto clickRow = [&](int row) { r.Move(mx + 30, my + 20 + pitch * (float)row); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frames(2); };
    clickRow(1);                                               // "Size"
    CHECK(r.cellX[2] - r.cellX[0] > sp4 * 1.25f);              // one column fewer: the rest are wider
    // show it again through the menu (a click elsewhere closes the open one first)
    r.Move(600, 270); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    r.RightClickHeader(r.HeaderX(0));
    const float mx2 = r.sim.mx, my2 = r.sim.my;
    r.Move(mx2 + 30, my2 + 20 + pitch); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frames(2);
    CHECK(Near(r.cellX[1] - r.cellX[0], sp4, 1.5f));
    // move a column, then "Reset order" (the fifth row) puts it back
    r.Move(600, 270); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    r.DragHeader(0, r.cellX[1] + sp4 * 0.8f);
    CHECK(r.cellX[0] > r.cellX[1]);
    r.RightClickHeader(r.HeaderX(1));
    const float mx3 = r.sim.mx, my3 = r.sim.my;
    r.Move(mx3 + 30, my3 + 20 + pitch * 4.0f + 8.0f); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frames(2);
    CHECK(r.cellX[0] < r.cellX[1]);
    // the menu never lets the last column go: hide Name, Size and Type through it, then try Date
    auto closeMenu = [&] { r.Move(600, 270); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); };
    auto pick = [&](int headerCol, int row) {
        closeMenu();
        r.RightClickHeader(r.HeaderX(headerCol));
        const float ax = r.sim.mx, ay = r.sim.my;
        r.Move(ax + 30, ay + 20 + pitch * (float)row); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frames(2);
    };
    const float first = r.cellX[0];
    pick(0, 0);                                                // Name goes
    pick(1, 1);                                                // Size goes
    pick(2, 2);                                                // Type goes
    CHECK(Near(r.cellX[3], first, 0.5f));                      // Date is alone, from the left edge of the table
    pick(3, 3);                                                // the menu entry of the last column is locked
    CHECK(Near(r.cellX[3], first, 0.5f));
    closeMenu();
    r.RightClickHeader(r.HeaderX(3));
    CHECK(r.sim.Draw(0, 0, 0).w > 0);
}

void TestSaveLoad() {
    RUN_SUITE("tables: state saved and restored as text");
    std::string text;
    {
        TableRig r; r.flags = UI::Table_Reorderable | UI::Table_Hideable;
        r.Frames(3);
        r.sim.Frame([&] {
            UI::Begin("W", 10, 10, 620, 280);
            if (UI::BeginTable("cols", 4, (UI::TableFlags)r.flags)) {
                UI::TableSetupColumn("Name"); UI::TableSetupColumn("Size"); UI::TableSetupColumn("Type"); UI::TableSetupColumn("Date");
                CHECK(UI::TableSetColumnDisplayIndex(2, 0)); CHECK(UI::TableSetColumnEnabled(1, false));
                text = UI::TableSaveState();
                UI::TableHeadersRow(); UI::EndTable();
            }
            UI::End();
        });
        r.Frames(2);
        const std::string all = UI::SaveTableStates();
        CHECK(all.compare(0, 12, "okaytables 1") == 0 && all.find("order 2 0 1 3") != std::string::npos && all.find("hidden 2") != std::string::npos);
        CHECK(text.find("order 2 0 1 3") != std::string::npos && text.find("hidden 2") != std::string::npos);
    }
    // a fresh UI: nothing is remembered, then the text brings it back
    Reset();
    {
        TableRig r; r.flags = UI::Table_Reorderable | UI::Table_Hideable;
        CHECK(UI::LoadTableStates(text));
        r.Frames(4);
        CHECK(r.cellX[2] < r.cellX[0] && r.cellX[0] < r.cellX[3]);                 // order 2 0 (1 hidden) 3
        r.sim.Frame([&] {
            UI::Begin("W", 10, 10, 620, 280);
            if (UI::BeginTable("cols", 4, (UI::TableFlags)r.flags)) {
                UI::TableSetupColumn("Name"); UI::TableSetupColumn("Size"); UI::TableSetupColumn("Type"); UI::TableSetupColumn("Date");
                CHECK(!UI::TableGetColumnEnabled(1) && UI::TableGetColumnEnabled(0) && UI::TableGetColumnDisplayIndex(2) == 0);
                UI::TableHeadersRow(); UI::EndTable();
            }
            UI::End();
        });
        // the single-table form
        const std::string one = "okaytables 1\ntable 5 4\norder 3 2 1 0\nhidden 0\nwidth 100 100 100 100\n";
        TableRig q; q.flags = UI::Table_Reorderable | UI::Table_Hideable;
        q.sim.Frame([&] {
            UI::Begin("W", 10, 10, 620, 280);
            if (UI::BeginTable("cols", 4, (UI::TableFlags)q.flags)) {
                UI::TableSetupColumn("Name"); UI::TableSetupColumn("Size"); UI::TableSetupColumn("Type"); UI::TableSetupColumn("Date");
                CHECK(UI::TableLoadState(one));
                CHECK(UI::TableGetColumnDisplayIndex(3) == 0 && UI::TableGetColumnEnabled(1));
                CHECK(!UI::TableLoadState("junk") && !UI::TableLoadState("okaytables 1\ntable 5 3\norder 2 1 0\n"));   // not a table state / another column count
                UI::TableHeadersRow(); UI::EndTable();
            }
            UI::End();
        });
    }
    // damaged text: a bad order becomes the natural one, a mask past the columns is cut, bad widths are ignored; the loader never fails on them
    Reset();
    {
        const char* bad[] = {
            "okaytables 1\ntable 7 4\norder 0 0 1 2\nhidden 99999\nwidth nan inf -5 1e30\n",
            "okaytables 1\ntable 7 4\norder 9 8 7 6\nhidden -1\nwidth\n",
            "okaytables 1\ntable 7 99\norder\nhidden\nwidth\ntable x y\n",
            "okaytables 1\n",
            "okaytables 1\ntable 7 4\nhidden 15\n",
        };
        for (const char* b : bad) {
            CHECK(UI::LoadTableStates(b));
            TableRig r; r.flags = UI::Table_Reorderable | UI::Table_Hideable;
            r.Frames(3);
            CHECK(std::isfinite(r.cellX[0] + r.cellX[1] + r.cellX[2] + r.cellX[3]) && std::isfinite(r.cellW[0]));
        }
        CHECK(!UI::LoadTableStates("nope") && !UI::LoadTableStates("") && !UI::LoadTableStates(std::string(2000000, 'x')));
        CHECK(UI::LoadTableStates("okaytables 1\n" + std::string(100, '\n')));
    }
}

void TestFuzz() {
    RUN_SUITE("log sliders and tables: fuzz (hostile numbers, formats, flags, state text, mouse)");
    const char* e = std::getenv("OKAYUI_FUZZ_ITERS");
    const int iters = e ? std::max(40, std::atoi(e) / 4) : 800;
    std::uint64_t st = 0x7F4A7C15ull * 0x9E3779B97F4A7C15ull;
    auto next = [&] { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (unsigned)(st >> 16); };
    auto I = [&](int n) { return n <= 0 ? 0 : (int)(next() % (unsigned)n); };
    auto F = [&]() -> float {
        switch (I(12)) {
            case 0: return 0.0f; case 1: return std::numeric_limits<float>::quiet_NaN(); case 2: return std::numeric_limits<float>::infinity();
            case 3: return -std::numeric_limits<float>::infinity(); case 4: return 1e30f; case 5: return -1e30f; case 6: return -1.0f;
            default: return (float)I(2000) - 500.0f;
        }
    };
    const char* fmts[] = {nullptr, "%.2f", "%d", "%s", "%n", "%.0f Hz", "%%", "%5.3g|", "-", "%d %d", "%99999f", "%", "%ld", "%.*f", "%08.2f%%", "%x"};
    const char* texts[] = {"", "okaytables 1\n", "okaytables 1\ntable 1 4\norder 3 2 1 0\nhidden 5\nwidth 1 2 3 4\n", "junk", "okaytables 1\ntable -1 99\norder -1 -1\nhidden 99999999999\nwidth nan\n"};
    Reset();
    Sim sim(640, 360);
    float fv = 5; int iv = 5;
    bool valid = true;
    for (int it = 0; it < iters && valid; ++it) {
        sim.Move((float)I(640), (float)I(360));
        sim.down = I(3) == 0; sim.rdown = I(7) == 0; sim.ctrl = I(8) == 0;
        if (I(6) == 0) sim.Type(std::string(1, (char)(32 + I(95))));
        if (I(9) == 0) sim.Press((Key)I(Key_COUNT));
        if (I(25) == 0) UI::LoadTableStates(texts[I(5)]);
        sim.Frame([&] {
            UI::Begin("fz", 10, 10, 600, 330);
            const int n = 2 + I(6);
            for (int k = 0; k < n; ++k) {
                switch (I(6)) {
                    case 0: UI::SliderFloat("s", &fv, F(), F(), fmts[I(16)], I(256)); break;
                    case 1: UI::SliderInt("si", &iv, (int)std::max(-1000000.0f, std::min(1000000.0f, F())), I(100000), fmts[I(16)], I(256)); break;
                    case 2: UI::DragFloat("d", &fv, F(), F(), F(), fmts[I(16)], I(256)); break;
                    case 3: UI::DragInt("di", &iv, F(), I(2000) - 500, I(2000) - 500, fmts[I(16)], I(256)); break;
                    case 4: {
                        const int cols = 1 + I(6);
                        if (UI::BeginTable("t", cols, (UI::TableFlags)(I(1024)))) {
                            for (int c = 0; c < cols; ++c) UI::TableSetupColumn("c", I(256), F());
                            if (I(3) == 0) UI::TableSetupScrollFreeze(I(4), I(3));
                            if (I(2)) UI::TableHeadersRow();
                            if (I(3) == 0) { UI::TableSetColumnEnabled(I(8) - 1, I(2) == 0); UI::TableSetColumnDisplayIndex(I(8) - 1, I(8) - 1); }
                            if (I(6) == 0) { (void)UI::TableSaveState(); (void)UI::TableLoadState(texts[I(5)]); }
                            for (int r = 0; r < 3; ++r) { UI::TableNextRow(); for (int c = 0; c < cols; ++c) { UI::TableSetColumnIndex(c); UI::Text("x"); if (I(4) == 0) UI::Button("b"); } }
                            UI::EndTable();
                        }
                        break;
                    }
                    default: (void)UI::SaveTableStates(); (void)UI::SliderRatioFromValue(F(), F(), F(), I(2) == 0, I(20) - 5); (void)UI::SliderValueFromRatio(F(), F(), F(), I(2) == 0, I(20) - 5); break;
                }
            }
            UI::End();
        });
        const UI::DrawData dd = UI::GetDrawData();
        const UI::DrawVertex* vs = static_cast<const UI::DrawVertex*>(dd.vertices);
        for (int i = 0; i < dd.indexCount && valid; ++i) if (dd.indices[i] < 0 || dd.indices[i] >= dd.vertexCount) { valid = false; std::printf("  fuzz frame %d: index out of range\n", it); }
        for (int i = 0; i < dd.vertexCount && valid; ++i) if (!std::isfinite(vs[i].x) || !std::isfinite(vs[i].y) || !std::isfinite(vs[i].u) || !std::isfinite(vs[i].v)) { valid = false; std::printf("  fuzz frame %d: vertex %d not finite\n", it, i); }
        if (!std::isfinite(fv) && it > 0) { /* a slider with a hostile range may leave NaN in the caller's value; not an invariant */ fv = 5; }
        if (I(10) == 0) (void)sim.Draw(0, 0, 0);
    }
    CHECK(valid);
    Reset();
}

} // namespace

int main() {
    TestMapping();
    TestFormat();
    TestSliderWidget();
    TestDragWidget();
    TestHiding();
    TestHeaderDrag();
    TestHeaderMenu();
    TestSaveLoad();
    TestFuzz();
    TEST_MAIN_RESULT();
}
