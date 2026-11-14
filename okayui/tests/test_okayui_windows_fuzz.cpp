// Fuzz for window stacking: random windows (positions, sizes, flags, some docked, some appearing and vanishing) and random
// mouse frames (moves, presses, releases, wheel). Checked after every frame:
//  * a button only fires in the window that was topmost under the pointer when the button was pressed (the test keeps its own
//    model of the stacking order: new windows on top, a press raises the topmost window under the pointer);
//  * at most one floating window reports IsWindowHovered, and it is the model's topmost one;
//  * the draw list is valid: indices in range, a multiple of three, commands tile the index buffer exactly, nothing dropped;
//  * everything is finite and nothing crashes (run it under ASan / UBSan).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9e3779b97f4a7c15ull + 1) {}
    std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (std::uint32_t)(s >> 16); }
    int Int(int n) { return n <= 0 ? 0 : (int)(Next() % (std::uint32_t)n); }
    float F(float a, float b) { return a + (b - a) * (float)(Next() % 10001) / 10000.0f; }
    bool Coin(int outOf = 2) { return Int(outOf) == 0; }
};

struct W {
    std::string name;
    float x, y, w, h;
    int flags;
    bool shown = true;
    bool docked = false;
    int buttons = 3;
    // reported by the frame
    UI::Vec2 pos{0, 0}, size{0, 0};
    bool seen = false, hovered = false;
};

bool DrawListValid(const UI::DrawData& d) {
    if (d.indexCount % 3 != 0 || d.dropped != 0) return false;
    for (int i = 0; i < d.indexCount; ++i) if (d.indices[i] < 0 || d.indices[i] >= d.vertexCount) return false;
    int covered = 0;
    for (int i = 0; i < d.cmdCount; ++i) {
        if (d.cmds[i].idxOffset != covered || d.cmds[i].idxCount <= 0 || d.cmds[i].idxCount % 3 != 0) return false;
        covered += d.cmds[i].idxCount;
    }
    if (covered != d.indexCount) return false;
    const UI::DrawVertex* v = (const UI::DrawVertex*)d.vertices;
    for (int i = 0; i < d.vertexCount; ++i) if (!std::isfinite(v[i].x) || !std::isfinite(v[i].y)) return false;
    return true;
}

// the window flags the fuzz may use (the ones that do not change stacking)
int RandomFlags(Rng& r) {
    int f = 0;
    if (r.Coin(4)) f |= UI::Window_NoMove;
    if (r.Coin(4)) f |= UI::Window_NoResize;
    if (r.Coin(5)) f |= UI::Window_NoTitleBar;
    if (r.Coin(5)) f |= UI::Window_NoScrollbar;
    if (r.Coin(5)) f |= UI::Window_NoBackground;
    if (r.Coin(5)) f |= UI::Window_NoCollapse;
    return f;
}

void RunOne(std::uint64_t seed, bool look, bool useDock) {
    Reset();
    if (look) { UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
    Rng rng(seed);
    Sim sim(640, 480);
    std::vector<W> wins;
    const int n = 2 + rng.Int(6);
    for (int i = 0; i < n; ++i) {
        W w;
        char nm[16]; std::snprintf(nm, sizeof nm, "win%d", i);
        w.name = nm;
        w.x = rng.F(0, 400); w.y = rng.F(0, 300); w.w = rng.F(60, 300); w.h = rng.F(50, 260);
        w.flags = RandomFlags(rng);
        w.buttons = 1 + rng.Int(6);
        wins.push_back(w);
    }
    if (useDock) {
        // two of them live in the dock space
        for (int i = 0; i < 2 && i < n; ++i) wins[(size_t)i].docked = true;
    }
    std::vector<std::string> model;        // back to front: the fuzz's own idea of the stacking order
    std::vector<UI::Rect> prevRect(wins.size());
    std::vector<bool> prevShown(wins.size(), false), wasDocked(wins.size(), false);
    int topAtPress = -1;                   // model window under the pointer when the left button went down
    int fired = -1;
    float pressX = 0, pressY = 0;
    unsigned verAtPress = 0;                // the dock layout's version at the press: a re-layout moves docked windows under the pointer
    bool pressedLast = false;

    // a window the fuzz asked to dock is docked only while the layout says so (a torn-off tab floats)
    auto dk = [&](size_t i) { return useDock && wins[i].docked && UI::GetDockLayout().NodeOf(wins[i].name) != 0; };
    auto topUnder = [&](float px, float py) {
        for (int k = (int)model.size() - 1; k >= 0; --k) {
            for (size_t i = 0; i < wins.size(); ++i) {
                if (wins[i].name != model[(size_t)k] || !prevShown[i] || dk(i)) continue;
                const UI::Rect& r = prevRect[i];
                if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) return (int)i;
            }
        }
        for (size_t i = 0; i < wins.size(); ++i) {   // docked content is below every floating window
            if (!dk(i) || !prevShown[i]) continue;
            const UI::Rect& r = prevRect[i];
            if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) return (int)i;
        }
        return -1;
    };

    auto ui = [&] {
        if (useDock) UI::DockSpace(UI::Rect{0, 0, 640, 480});
        for (size_t i = 0; i < wins.size(); ++i) {
            W& w = wins[i];
            w.seen = false; w.hovered = false;
            if (!w.shown) continue;
            bool vis;
            bool o = true;   // DockEnd reads it again, so it lives as long as the window
            if (w.docked) {
                vis = UI::DockBegin(w.name.c_str(), &o, w.flags & ~UI::Window_NoTitleBar);
            } else {
                UI::SetNextWindowPos(UI::Vec2{w.x, w.y}, UI::Cond_FirstUseEver);
                UI::SetNextWindowSize(UI::Vec2{w.w, w.h}, UI::Cond_FirstUseEver);
                vis = UI::Begin(w.name.c_str(), nullptr, w.flags);
            }
            w.seen = true;
            w.pos = UI::GetWindowPos(); w.size = UI::GetWindowSize();
            w.hovered = UI::IsWindowHovered();
            if (vis) {
                for (int b = 0; b < w.buttons; ++b) {
                    char l[24]; std::snprintf(l, sizeof l, "button %d of %s", b, w.name.c_str());
                    if (UI::Button(l)) fired = (int)i;
                }
            }
            if (w.docked) UI::DockEnd(); else UI::End();
        }
    };

    if (useDock) {
        sim.Frames(2, ui);
        for (int i = 0; i < 2 && i < n; ++i) UI::GetDockLayout().Dock(wins[(size_t)i].name, UI::GetDockLayout().Root(), i == 0 ? UI::DockSide_Center : UI::DockSide_Right);
    }
    for (int frame = 0; frame < 300; ++frame) {
        // random input
        const int what = rng.Int(10);
        if (what < 4) sim.Move(rng.F(-10, 650), rng.F(-10, 490));
        else if (what < 6) { if (!sim.down) { sim.Down(); } }
        else if (what < 8) { if (sim.down) sim.Up(); }
        else if (what == 8) sim.wheel = rng.F(-3, 3);
        else if (rng.Coin(3)) {            // show / hide a window
            W& w = wins[(size_t)rng.Int(n)];
            w.shown = !w.shown;
        }
        // window moves are not part of the model: only floating windows with default flags follow their reported rects
        const bool wasDown = sim.down;
        (void)wasDown;
        fired = -1;
        // does the model see a press in this frame?
        const bool pressNow = sim.down && !pressedLast;
        if (pressNow) { topAtPress = topUnder(sim.mx, sim.my); verAtPress = UI::GetDockLayout().Version(); pressX = sim.mx; pressY = sim.my; }
        sim.Frame(ui);
        // model update: windows seen this frame, in order of first appearance
        for (size_t i = 0; i < wins.size(); ++i) {
            const bool known = [&] { for (const std::string& s : model) if (s == wins[i].name) return true; return false; }();
            if (wins[i].seen && !known && !dk(i)) model.push_back(wins[i].name);
            if (wins[i].seen && !dk(i) && (!prevShown[i] || wasDocked[i])) {   // appeared again or torn off a dock: on top
                for (size_t k = 0; k < model.size(); ++k) if (model[k] == wins[i].name) { model.erase(model.begin() + (long)k); break; }
                model.push_back(wins[i].name);
            }
        }
        if (pressNow && topAtPress >= 0 && !dk((size_t)topAtPress) && !(wins[(size_t)topAtPress].flags & UI::Window_NoBringToFront)) {
            const std::string nm = wins[(size_t)topAtPress].name;
            for (size_t k = 0; k < model.size(); ++k) if (model[k] == nm) { model.erase(model.begin() + (long)k); break; }
            model.push_back(nm);
        }
        pressedLast = sim.down;
        bool anyAppeared = false;   // a window drawn for the first time (or again) this frame is on top, but windows begun before it cannot know yet
        for (size_t i = 0; i < wins.size(); ++i) if (wins[i].seen && !prevShown[i]) anyAppeared = true;
        for (size_t i = 0; i < wins.size(); ++i) {
            prevShown[i] = wins[i].seen; wasDocked[i] = dk(i);
            if (wins[i].seen) prevRect[i] = UI::Rect{wins[i].pos.x, wins[i].pos.y, wins[i].size.x, wins[i].size.y};
        }
        // ---- checks ----
        // a click only fires in the window that was topmost under the pointer at the press (when the press was in some window)
        if (fired >= 0 && topAtPress >= 0 && !sim.down && UI::GetDockLayout().Version() == verAtPress) {
            const UI::Rect& r = prevRect[(size_t)fired];
            const bool inside = sim.mx >= r.x && sim.mx < r.x + r.w && sim.my >= r.y && sim.my < r.y + r.h
                                && pressX >= r.x && pressX < r.x + r.w && pressY >= r.y && pressY < r.y + r.h;
            // a widget hanging out of a small window's rectangle is drawn over what is below the window and takes clicks there; only check presses inside the window
            if (inside) CHECK(fired == topAtPress);
        }
        int hoverCount = 0, hoverIdx = -1;
        for (size_t i = 0; i < wins.size(); ++i) if (wins[i].seen && !dk(i) && wins[i].hovered) { ++hoverCount; hoverIdx = (int)i; }
        // while the button is down a window may be moving under the pointer, and windows begun before it cannot know yet
        if (!anyAppeared && !sim.down) CHECK(hoverCount <= 1);
        if (hoverCount == 1 && !sim.down && pressNow == false && !anyAppeared) {
            // the hovered window is not covered by a window the model puts in front of it
            const std::string nm = wins[(size_t)hoverIdx].name;
            size_t mine = 0;
            for (size_t k = 0; k < model.size(); ++k) if (model[k] == nm) mine = k;
            for (size_t k = mine + 1; k < model.size(); ++k) {
                for (size_t i = 0; i < wins.size(); ++i) {
                    if (wins[i].name != model[k] || !wins[i].seen || dk(i)) continue;
                    const bool cover = sim.mx >= wins[i].pos.x && sim.mx < wins[i].pos.x + wins[i].size.x && sim.my >= wins[i].pos.y && sim.my < wins[i].pos.y + wins[i].size.y;
                    CHECK(!cover);
                }
            }
        }
        CHECK(DrawListValid(UI::GetDrawData()));
        for (size_t i = 0; i < wins.size(); ++i) if (wins[i].seen) CHECK(std::isfinite(wins[i].pos.x) && std::isfinite(wins[i].size.x) && std::isfinite(wins[i].size.y));
        if (frame % 60 == 0) sim.Draw(12, 14, 20);   // rasterise now and then: the software backend must cope with the reordered list
    }
    if (look) UI::UseLauncherLook(false);
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("okayui windows fuzz");
    const int rounds = argc > 1 ? std::atoi(argv[1]) : 120;
    for (int i = 0; i < rounds; ++i) RunOne((std::uint64_t)i + 1, (i & 1) != 0, (i % 3) == 0);
    TEST_MAIN_RESULT();
}
