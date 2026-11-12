// OkayUI fuzzing: random (seeded, reproducible) input, widget trees and draw data, plus reference-model checks of
// text editing and layout. Meant to run under ASan/UBSan (cmake -DOKAY_SANITIZE=address,undefined,float-cast-overflow).
// OKAYUI_FUZZ_ITERS scales the work (default 1500 frames per scenario), OKAYUI_FUZZ_SEED changes the seed.
//
// Ported from the okay::imui fuzz test. The two rasteriser bugs it found there stay fixed (a non-finite clip
// rectangle or texture coordinate converted to an int) and are covered by the raw draw data scenario.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ull + 88172645463325252ull) {}
    std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (std::uint32_t)(s >> 16); }
    int Int(int n) { return n <= 0 ? 0 : (int)(Next() % (std::uint32_t)n); }
    bool Chance(int percent) { return Int(100) < percent; }
    float Uni(float lo, float hi) { return lo + (hi - lo) * (float)(Next() & 0xFFFFFF) / 16777215.0f; }
    float Nasty() {
        switch (Int(14)) {
            case 0: return 0.0f; case 1: return -0.0f; case 2: return NAN; case 3: return INFINITY; case 4: return -INFINITY;
            case 5: return 1e30f; case 6: return -1e30f; case 7: return 1e-30f; case 8: return 16777217.0f;
            case 9: return -1.0f; case 10: return 0.5f; default: return Uni(-800, 800);
        }
    }
    int NastyInt(int mod) { const float f = Nasty(); return std::isfinite(f) && std::fabs(f) < 1e9f ? (int)f % mod : Int(mod); }
    std::string Junk(int maxLen) {
        std::string s; const int n = Int(maxLen + 1);
        for (int i = 0; i < n; ++i) {
            switch (Int(6)) {
                case 0: s += (char)Int(256); break;                         // any byte, including invalid UTF-8
                case 1: s += (char)(32 + Int(95)); break;
                case 2: { const unsigned cp = 0x80 + (unsigned)Int(0x2000);  // a valid two- or three-byte character
                          if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
                          else { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); } break; }
                case 3: s += (char)Int(32); break;                          // control characters
                default: s += (char)('a' + Int(26)); break;
            }
        }
        return s;
    }
    std::string Label() { return Chance(10) ? Junk(40) : std::string("w") + std::to_string(Int(6)); }
};

int Iters() { const char* e = std::getenv("OKAYUI_FUZZ_ITERS"); return e ? std::max(10, std::atoi(e)) : 1500; }
std::uint64_t Seed() { const char* e = std::getenv("OKAYUI_FUZZ_SEED"); return e ? (std::uint64_t)std::atoll(e) : 12345; }

bool Utf8Valid(const char* s) {
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        const int n = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 0;
        if (!n) return false;
        for (int i = 1; i < n; ++i) if ((p[i] & 0xC0) != 0x80) return false;
        p += n;
    }
    return true;
}

// The draw list must stay self-consistent whatever the input was: indices in range, commands contiguous and
// covering the whole index buffer.
bool DataSane(const UI::DrawData& d) {
    if (d.vertexCount < 0 || d.indexCount < 0 || d.indexCount % 3 != 0) return false;
    for (int i = 0; i < d.indexCount; ++i) if (d.indices[i] < 0 || d.indices[i] >= d.vertexCount) return false;
    int next = 0;
    for (int i = 0; i < d.cmdCount; ++i) {
        const UI::DrawCmd& c = d.cmds[i];
        if (c.idxOffset != next || c.idxCount <= 0 || c.idxCount % 3 != 0) return false;
        next += c.idxCount;
    }
    return d.indexCount == 0 ? d.cmdCount == 0 || next == 0 : next == d.indexCount;
}

// Random calls to the public draw list with hostile numbers.
void RandomDraw(Rng& r, const UI::DrawList& dl) {
    auto P = [&] { return UI::Vec2{r.Chance(30) ? r.Nasty() : r.Uni(-50, 400), r.Chance(30) ? r.Nasty() : r.Uni(-50, 300)}; };
    const UI::Color32 col = r.Next();
    const float th = r.Chance(40) ? r.Nasty() : r.Uni(0, 12);
    switch (r.Int(16)) {
        case 0: dl.AddLine(P(), P(), col, th); break;
        case 1: dl.AddRect(P(), P(), col, r.Nasty(), th); break;
        case 2: dl.AddRectFilled(P(), P(), col, r.Nasty()); break;
        case 3: dl.AddRectFilledMultiColor(P(), P(), col, r.Next(), r.Next(), r.Next()); break;
        case 4: dl.AddCircle(P(), r.Chance(30) ? r.Nasty() : r.Uni(0, 80), col, r.NastyInt(200) - 20, th); break;
        case 5: dl.AddCircleFilled(P(), r.Chance(30) ? r.Nasty() : r.Uni(0, 80), col, r.NastyInt(200) - 20); break;
        case 6: dl.AddTriangle(P(), P(), P(), col, th); dl.AddTriangleFilled(P(), P(), P(), col); break;
        case 7: dl.AddQuad(P(), P(), P(), P(), col, th); dl.AddQuadFilled(P(), P(), P(), P(), col); break;
        case 8: {
            UI::Vec2 pts[300];
            for (auto& p : pts) p = P();
            const int n = r.Int(310) - 3;
            if (r.Chance(5)) dl.AddPolyline(nullptr, n, col, r.Chance(50), th);
            else if (r.Chance(50)) dl.AddPolyline(pts, n, col, r.Chance(50), th);
            else dl.AddConvexPolyFilled(pts, n, col);
            break;
        }
        case 9: dl.AddBezierCubic(P(), P(), P(), P(), col, th, r.NastyInt(400) - 5); break;
        case 10: { const std::string t = r.Junk(20); dl.AddText(P(), col, r.Chance(10) ? nullptr : t.c_str(), r.Nasty()); break; }
        case 11: for (int i = 0, n = r.Int(40); i < n; ++i) dl.PathLineTo(P()); dl.PathStroke(col, r.Chance(50), th); break;
        case 12: dl.PathArcTo(P(), r.Nasty(), r.Nasty(), r.Nasty(), r.NastyInt(300) - 5); dl.PathFillConvex(col); break;
        case 13: dl.PathClear(); dl.PathArcTo(P(), r.Uni(0, 50), r.Uni(-9, 9), r.Uni(-9, 9), r.Int(40)); dl.PathStroke(col, false, th); break;
        case 14: { const UI::Vec2 a = P(), b = P(); dl.PushClipRect(a, b); dl.AddRectFilled(P(), P(), col); dl.PopClipRect(); break; }
        default: dl.PopClipRect(); dl.AddImage(nullptr, P(), P()); break;
    }
}

// A random but well nested widget tree: every Begin / Push has its matching End / Pop.
void RandomWidgets(Rng& r, int depth, char strs[3][64], bool* bools, float* floats, int* ints, int* combo) {
    static const char* const items[] = {"Alpha", "Beta", "", "A very long combo entry that will not fit", "\xC3\xA9"};
    static const char* const hdr[16] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P"};   // a table needs one header per column
    const int n = 1 + r.Int(8);
    for (int i = 0; i < n; ++i) {
        const std::string lbl = r.Label();
        if (r.Chance(10)) UI::SameLine(r.Chance(50) ? r.Nasty() : -1.0f);
        if (r.Chance(8)) UI::SetNextItemWidth(r.Nasty());
        switch (r.Int(48)) {
            case 0: UI::Button(lbl.c_str()); break;
            case 1: UI::SmallButton(lbl.c_str()); break;
            case 2: UI::Checkbox(lbl.c_str(), &bools[r.Int(4)]); break;
            case 3: UI::SliderFloat(lbl.c_str(), &floats[r.Int(4)], r.Nasty(), r.Nasty()); break;
            case 4: UI::SliderInt(lbl.c_str(), &ints[r.Int(4)], r.NastyInt(100000), r.NastyInt(100000)); break;
            case 5: UI::DragFloat(lbl.c_str(), &floats[r.Int(4)], r.Nasty(), r.Nasty(), r.Nasty()); break;
            case 6: UI::InputText(lbl.c_str(), strs[r.Int(3)], r.Chance(10) ? r.Int(4) : 64, (unsigned)r.Int(8)); break;
            case 7: UI::Combo(lbl.c_str(), items, r.Chance(10) ? -3 : 5, r.Chance(10) ? nullptr : combo); break;
            case 8: UI::Selectable(lbl.c_str(), r.Chance(50)); break;
            case 9: UI::Text(lbl.c_str()); break;
            case 10: UI::TextColored((unsigned char)r.Int(256), 0, 255, lbl.c_str()); break;
            case 11: UI::Separator(); UI::Spacing(r.Nasty()); break;
            case 12: UI::Dummy(r.Nasty(), r.Nasty()); break;
            case 13: if (r.Chance(50)) UI::Indent(r.Nasty()); else UI::Unindent(r.Nasty()); break;   // unbalanced indents are legal
            case 14: if (depth < 3 && UI::TreeNode(lbl.c_str())) { RandomWidgets(r, depth + 1, strs, bools, floats, ints, combo); UI::TreePop(); } break;
            case 15: if (depth < 3 && UI::CollapsingHeader(lbl.c_str())) RandomWidgets(r, depth + 1, strs, bools, floats, ints, combo); break;
            case 16: if (depth < 3 && UI::BeginChild(lbl.c_str(), r.Nasty(), r.Nasty(), r.Chance(50))) { RandomWidgets(r, depth + 1, strs, bools, floats, ints, combo); UI::EndChild(); } break;
            case 17: UI::Columns(r.Int(8) - 1); break;
            case 18: UI::NextColumn(); break;
            case 19: UI::EndColumns(); break;
            case 20: UI::PushID(r.Chance(50) ? lbl.c_str() : "id"); UI::PushID(r.Int(5)); if (depth < 3) RandomWidgets(r, depth + 1, strs, bools, floats, ints, combo); UI::PopID(); UI::PopID(); break;
            case 21: UI::PushStyleColor((UI::Col)r.Int(UI::Col_COUNT), (unsigned char)r.Int(256), 5, 9); UI::Text("styled"); UI::PopStyleColor(); break;
            case 22: UI::PlotLines(lbl.c_str(), floats, r.Int(5) - 1, r.Nasty(), r.Nasty(), r.Nasty()); break;
            case 23: UI::ProgressBar(r.Nasty(), r.Chance(50) ? lbl.c_str() : nullptr); break;
            case 24: { int t = r.Int(4); UI::TabBar(hdr, r.Int(5) - 1, &t); break; }
            case 25: UI::Tooltip(lbl.c_str()); break;
            case 26: if (UI::BeginTable(lbl.c_str(), r.Int(20) - 2, r.Chance(50) ? hdr : nullptr)) { UI::TableNextRow(); UI::TableNextColumn(); UI::Text("c"); UI::TableNextColumn(); UI::Text("d"); UI::EndTable(); } break;
            case 27: UI::OpenPopup(lbl.c_str()); break;
            case 28: if (UI::BeginPopup(lbl.c_str())) { UI::Selectable("in popup", false); if (r.Chance(30)) UI::CloseCurrentPopup(); UI::EndPopup(); } break;
            case 29: if (UI::BeginPopupContextItem(lbl.c_str())) { UI::Text("ctx"); UI::EndPopup(); } break;
            case 30: UI::BeginMenuBar(); if (UI::BeginMenu(lbl.c_str())) { UI::MenuItem("item"); UI::EndMenu(); } UI::EndMenuBar(); break;
            case 31: UI::RadioButton(lbl.c_str(), &ints[0], r.Int(3)); break;
            case 32: UI::ToggleSwitch(lbl.c_str(), &bools[r.Int(4)]); break;
            case 33: UI::VSliderFloat(lbl.c_str(), r.Nasty(), r.Nasty(), &floats[0], r.Nasty(), r.Nasty()); break;
            case 34: UI::Knob(lbl.c_str(), &floats[1], r.Nasty(), r.Nasty(), r.Nasty()); break;
            case 35: UI::InputInt(lbl.c_str(), &ints[r.Int(4)], r.NastyInt(50)); break;
            case 36: UI::ListBox(lbl.c_str(), &ints[1], items, r.Int(7) - 1, r.Int(6) - 1); break;
            case 37: { int p = 0; if (UI::DragSource(r.Int(9))) {} if (UI::DropTarget(&p)) {} break; }
            case 38: UI::TextWrapped(lbl.c_str()); UI::BulletText(lbl.c_str()); UI::SeparatorText(lbl.c_str()); break;
            case 39: UI::BeginDisabled(r.Chance(60)); if (depth < 3) RandomWidgets(r, depth + 1, strs, bools, floats, ints, combo); if (r.Chance(90)) UI::EndDisabled(); break;   // sometimes unbalanced
            case 40: RandomDraw(r, UI::GetWindowDrawList()); break;
            case 41: RandomDraw(r, UI::GetForegroundDrawList()); break;
            case 42: UI::SetCursorScreenPos({r.Nasty(), r.Nasty()}); UI::SetCursorPos({r.Nasty(), r.Nasty()}); UI::SetCursorPosX(r.Nasty()); UI::SetCursorPosY(r.Nasty()); break;
            case 43: { const UI::Vec2 a = UI::GetCursorScreenPos(), b = UI::GetContentRegionAvail(), c = UI::GetCursorPos(), d = UI::CalcTextSize(lbl.c_str());
                       if (!(b.x >= 0) || !(b.y >= 0) || !(d.x >= 0) || !(d.y >= 0)) std::fprintf(stderr, "  negative or NaN measurement: avail %g %g text %g %g (cursor %g %g %g %g)\n", b.x, b.y, d.x, d.y, a.x, a.y, c.x, c.y), std::abort(); break; }
            case 44: UI::InvisibleButton(lbl.c_str(), r.Nasty(), r.Nasty()); UI::IsItemActive(); UI::IsItemActivated(); UI::IsItemDeactivated(); UI::IsItemClicked(r.Int(2)); break;
            case 45: UI::IsMouseDown(r.Int(3)); UI::IsMouseClicked(r.Int(3)); UI::IsMouseReleased(r.Int(3)); UI::GetMousePos(); UI::GetMouseDelta(); UI::IsMouseHoveringRect({r.Nasty(), r.Nasty()}, {r.Nasty(), r.Nasty()}); break;
            case 46: UI::EndDisabled(); UI::IsDisabled(); UI::GetColorU32((UI::Col)r.Int(UI::Col_COUNT), r.Nasty()); UI::ColorMulAlpha(r.Next(), r.Nasty()); break;
            default: UI::ColorEdit3(lbl.c_str(), &floats[r.Int(2)]); break;   // reads 3 floats: the array has 4 entries, index <= 1 stays in range
        }
        if (r.Chance(20)) { UI::IsItemHovered(); UI::GetItemRect(); UI::WantsTextInput(); }
    }
    UI::EndColumns();
}
} // namespace

int main() {
    RUN_SUITE("okayui fuzz");
    const int iters = Iters();
    const std::uint64_t seed = Seed();

    // ---- random frames, random input, random widget trees ------------------------------------
    {
        Reset();
        Rng r(seed);
        char strs[3][64] = {"hello", "", "caf\xC3\xA9"};
        bool bools[4] = {}; float floats[4] = {0.5f, 0, 1e9f, -3}; int ints[4] = {1, 2, 3, 4}; int combo = 1;
        Bitmap bm(96, 72);
        float mx = 0, my = 0; bool down = false, rdown = false;
        int bad = 0;
        for (int f = 0; f < iters; ++f) {
            UI::Input in;
            if (r.Chance(60)) { mx = r.Chance(20) ? r.Nasty() : r.Uni(-20, 700); my = r.Chance(20) ? r.Nasty() : r.Uni(-20, 500); }
            if (r.Chance(20)) down = !down;
            if (r.Chance(10)) rdown = !rdown;
            in.mouseX = mx; in.mouseY = my; in.mouseDown = down; in.rightDown = rdown; in.blocked = r.Chance(5);
            in.wheel = r.Chance(10) ? r.Nasty() : 0;
            const std::string typed = r.Chance(30) ? r.Junk(12) : "";
            in.text = r.Chance(80) ? typed.c_str() : nullptr;
            in.backspace = r.Chance(10); in.del = r.Chance(8); in.left = r.Chance(8); in.right = r.Chance(8);
            in.home = r.Chance(4); in.end = r.Chance(4); in.enter = r.Chance(4); in.escape = r.Chance(3); in.selectAll = r.Chance(3); in.shift = r.Chance(20);
            UI::BeginFrame(in);
            const int windows = 1 + r.Int(4);
            for (int w = 0; w < windows; ++w) {
                if (r.Chance(10)) UI::PinNextWindow();
                if (r.Chance(20)) UI::ResizableNextWindow(r.Nasty(), r.Nasty());
                bool open = true;
                const std::string name = r.Chance(10) ? r.Junk(20) : "win" + std::to_string(r.Int(5));
                const bool vis = UI::Begin(name.c_str(), r.Chance(40) ? r.Nasty() : r.Uni(0, 300), r.Chance(40) ? r.Nasty() : r.Uni(0, 300),
                                           r.Chance(30) ? r.Nasty() : r.Uni(50, 400), r.Chance(30) ? r.Nasty() : r.Uni(50, 300), r.Chance(50) ? &open : nullptr);
                if (vis) RandomWidgets(r, 0, strs, bools, floats, ints, &combo);
                UI::End();
            }
            if (r.Chance(10)) UI::PushClipRect(r.Nasty(), r.Nasty(), r.Nasty(), r.Nasty()), UI::PopClipRect();
            if (r.Chance(5)) UI::Button(r.Int(1000), r.Nasty(), r.Nasty(), r.Nasty(), r.Nasty(), "explicit");
            if (r.Chance(5)) UI::Label(r.Nasty(), r.Nasty(), "label");
            if (r.Chance(5)) UI::Toast("toast", r.Int(3), 0.001f);
            UI::EndFrameData();
            const UI::DrawData dd = UI::GetDrawData();
            if (!DataSane(dd)) { ++bad; std::fprintf(stderr, "  frame %d: draw data inconsistent\n", f); }
            if (f % 7 == 0) { bm.Clear(0, 0, 0); UI::soft::Render(dd, bm); }
            for (auto& s : strs) if (!Utf8Valid(s) && std::strcmp(s, "hello") && std::strcmp(s, "caf\xC3\xA9")) { ++bad; std::fprintf(stderr, "  frame %d: a text buffer is not valid UTF-8\n", f); }
            if (r.Chance(2)) Reset();
        }
        CHECK(bad == 0);
    }

    // ---- unbalanced calls and garbage: never fatal -------------------------------------------
    {
        Reset();
        Rng r(seed + 1);
        for (int f = 0; f < iters / 3; ++f) {
            UI::Input in; in.mouseX = r.Uni(0, 300); in.mouseY = r.Uni(0, 200); in.mouseDown = r.Chance(30);
            UI::BeginFrame(in);
            const int n = r.Int(40);
            for (int i = 0; i < n; ++i) {
                switch (r.Int(24)) {
                    case 0: UI::Begin("a", 10, 10, 200, 150); break; case 1: UI::End(); break;
                    case 2: UI::BeginChild("s"); break; case 3: UI::EndChild(); break;
                    case 4: UI::PushID(1); break; case 5: UI::PopID(); break;
                    case 6: UI::TreeNode("t"); break; case 7: UI::TreePop(); break;
                    case 8: UI::PushStyleColor(UI::Col_Text, 1, 2, 3); break; case 9: UI::PopStyleColor(3); break;
                    case 10: UI::Columns(r.Int(5)); break; case 11: UI::NextColumn(); break; case 12: UI::EndColumns(); break;
                    case 13: UI::BeginTable("t", 2); break; case 14: UI::EndTable(); break; case 15: UI::TableNextColumn(); break;
                    case 16: UI::BeginPopup("p"); break; case 17: UI::EndPopup(); break; case 18: UI::CloseCurrentPopup(); break;
                    case 19: UI::BeginMenu("m"); break; case 20: UI::EndMenu(); break;
                    case 21: UI::PopClipRect(); break; case 22: UI::PopItemWidth(); break; default: UI::PopTextScale(); UI::EndCard(); break;
                }
            }
            UI::EndFrameData();
            if (r.Chance(5)) UI::EndFrameData();                         // twice
            if (r.Chance(5)) UI::BeginFrame(in);                         // BeginFrame without EndFrame
            if (!DataSane(UI::GetDrawData())) { CHECK(false); break; }
        }
        // widgets outside any window are inert
        Reset();
        UI::Input none; UI::BeginFrame(none);
        CHECK(!UI::Button("b") && !UI::Checkbox("c", nullptr) && !UI::Combo("c", nullptr, 0, nullptr));
        UI::EndFrameData();
    }

    // ---- a frame too big for the batch: geometry is dropped, the draw list stays valid ---
    {
        Reset();
        Sim sim(700, 500);
        sim.Move(60, 60);
        int worst = 0; bool sawDrop = false;
        for (int f = 0; f < 3; ++f) {
            sim.Frame([&] {
                if (UI::Begin("big", 0, 0, 600, 400)) {
                    for (int i = 0; i < 14000; ++i) { UI::Text("WWWWWWWWWWWWWWWW"); if (i == 0) UI::Tooltip("overlay while the main batch overflows"); }
                    UI::Button("last"); UI::Tooltip("tip");
                }
                UI::End();
            });
            const UI::DrawData d = UI::GetDrawData();
            CHECK(DataSane(d));
            worst = std::max(worst, d.vertexCount);
            if (d.dropped > 0 && UI::DroppedGeometry() == d.dropped) sawDrop = true;
            sim.Draw();
        }
        CHECK(worst >= 262000);      // the batch really was full
        CHECK(sawDrop);              // and it said so
    }

    // ---- text editing against a reference model ----------------------------------------------
    {
        Reset();
        Rng r(seed + 2);
        Sim sim(300, 100);
        char text[41] = "";
        UI::Rect rc;
        auto ui = [&] { if (UI::Begin("m", 0, 0, 300, 100)) { UI::InputText("", text, (int)sizeof(text)); rc = UI::GetItemRect(); } UI::End(); };
        sim.Frames(2, ui);
        sim.Click(rc.x + rc.w - 5, rc.y + 8, ui);
        std::string model; int cur = 0, anc = 0;           // ASCII only, so one byte per character
        int mismatches = 0, caretMismatch = 0;
        auto lo = [&] { return std::min(cur, anc); };
        auto hi = [&] { return std::max(cur, anc); };
        auto delSel = [&] { if (cur == anc) return false; model.erase((size_t)lo(), (size_t)(hi() - lo())); cur = anc = std::min(cur, anc); return true; };
        for (int i = 0; i < iters; ++i) {
            switch (r.Int(11)) {
                case 0: case 1: case 2: {
                    const char ch = (char)('a' + r.Int(26));
                    sim.Type(std::string(1, ch)); sim.Frame(ui);
                    if ((int)model.size() - (hi() - lo()) < 40) { delSel(); model.insert((size_t)cur, 1, ch); ++cur; anc = cur; }
                    break;
                }
                case 3: sim.Tap(Key_Backspace, ui); if (!delSel() && cur > 0) { model.erase((size_t)cur - 1, 1); --cur; anc = cur; } break;
                case 4: sim.Tap(Key_Delete, ui); if (!delSel() && cur < (int)model.size()) { model.erase((size_t)cur, 1); anc = cur; } break;
                case 5: case 6: {
                    const bool left = r.Chance(50), sh = r.Chance(40);
                    sim.shift = sh; sim.Tap(left ? Key_Left : Key_Right, ui); sim.shift = false;
                    const int to = left ? std::max(0, cur - 1) : std::min((int)model.size(), cur + 1);
                    if (sh) cur = to;
                    else { cur = (anc != cur) ? (left ? lo() : hi()) : to; anc = cur; }
                    break;
                }
                case 7: { const bool sh = r.Chance(40); sim.shift = sh; sim.Tap(Key_Home, ui); sim.shift = false; cur = 0; if (!sh) anc = 0; break; }
                case 8: { const bool sh = r.Chance(40); sim.shift = sh; sim.Tap(Key_End, ui); sim.shift = false; cur = (int)model.size(); if (!sh) anc = cur; break; }
                case 9: sim.Tap(Key_SelectAll, ui); anc = 0; cur = (int)model.size(); break;
                default: { const char ch = (char)('A' + r.Int(26)); sim.Type(std::string(1, ch)); sim.Type("\x01\x7f"); sim.Frame(ui);   // control characters never get in
                           if ((int)model.size() - (hi() - lo()) < 40) { delSel(); model.insert((size_t)cur, 1, ch); ++cur; anc = cur; } break; }
            }
            if (model != text) ++mismatches;
            if (i % 50 == 49) {   // probe the caret: type a marker and see where it lands
                sim.Type("#"); sim.Frame(ui);
                if ((int)model.size() - (hi() - lo()) < 40) { delSel(); model.insert((size_t)cur, 1, '#'); ++cur; anc = cur; }
                if (model != text) ++caretMismatch;
            }
        }
        CHECK(mismatches == 0);
        CHECK(caretMismatch == 0);
    }

    // ---- typed junk never breaks the buffer ----------------------------------------------------
    {
        Reset();
        Rng r(seed + 3);
        Sim sim(300, 100);
        char text[24] = "";
        UI::Rect rc;
        auto ui = [&] { if (UI::Begin("m", 0, 0, 300, 100)) { UI::InputText("", text, (int)sizeof(text)); rc = UI::GetItemRect(); } UI::End(); };
        sim.Frames(2, ui);
        sim.Click(rc.x + 20, rc.y + 8, ui);
        int bad = 0;
        for (int i = 0; i < iters * 2; ++i) {
            sim.Type(r.Junk(16));
            if (r.Chance(30)) sim.Press((Key)r.Int(7));   // never Escape: stay focused
            sim.shift = r.Chance(20);
            sim.Frame(ui);
            if (!Utf8Valid(text) || std::strlen(text) >= sizeof(text)) ++bad;
            if (r.Chance(5)) sim.Click(rc.x + r.Uni(0, rc.w), rc.y + 8, ui);
        }
        CHECK(bad == 0);
    }

    // ---- layout properties: stacked items never overlap, same-line items advance ---------------
    {
        Reset();
        Rng r(seed + 4);
        Sim sim(700, 500);
        int overlaps = 0;
        for (int f = 0; f < iters / 3; ++f) {
            const int n = 2 + r.Int(10);
            std::vector<float> ws, hs; std::vector<bool> same;
            for (int i = 0; i < n; ++i) { ws.push_back(r.Uni(0, 150)); hs.push_back(r.Uni(0, 60)); same.push_back(i > 0 && r.Chance(40)); }
            const float indent = r.Chance(30) ? r.Uni(0, 40) : 0;
            sim.Frame([&] {
                if (UI::Begin("l", 0, 0, 600, 400)) {
                    if (indent > 0) UI::Indent(indent);
                    UI::Rect prev, lineTop;
                    for (int i = 0; i < n; ++i) {
                        if (same[(size_t)i]) UI::SameLine();
                        UI::Dummy(ws[(size_t)i], hs[(size_t)i]);
                        const UI::Rect cur = UI::GetItemRect();
                        if (i > 0) {
                            if (same[(size_t)i]) { if (cur.x < prev.x + prev.w - 1e-3f || std::fabs(cur.y - prev.y) > 1e-3f) ++overlaps; }
                            else if (cur.y < lineTop.y + lineTop.h - 1e-3f) ++overlaps;
                        }
                        if (!same[(size_t)i]) lineTop = cur; else lineTop.h = std::max(lineTop.h, cur.y + cur.h - lineTop.y);
                        prev = cur;
                    }
                    if (indent > 0) UI::Unindent(indent);
                }
                UI::End();
            });
        }
        CHECK(overlaps == 0);
    }

    // ---- raw draw data and rasteriser with hostile numbers -------------------------------------
    {
        Rng r(seed + 5);
        unsigned char atlas[4 * 4 * 4]; for (auto& b : atlas) b = (unsigned char)r.Int(256);
        int tag = 0, other = 0;
        for (int f = 0; f < iters / 3; ++f) {
            std::vector<UI::DrawVertex> v; std::vector<int> idx; std::vector<UI::DrawCmd> cmds;
            const int nv = r.Int(30);
            for (int i = 0; i < nv; ++i) v.push_back({r.Nasty(), r.Nasty(), (unsigned char)r.Int(256), (unsigned char)r.Int(256), (unsigned char)r.Int(256), (unsigned char)r.Int(256), r.Nasty(), r.Nasty()});
            const int ni = r.Int(40) * 3;
            for (int i = 0; i < ni; ++i) idx.push_back(r.Chance(10) ? (int)r.Next() : r.Int(nv + 1));
            const int nc = r.Int(6);
            for (int i = 0; i < nc; ++i) {
                UI::DrawCmd c; c.idxOffset = r.Chance(20) ? (int)r.Next() : r.Int(ni + 1); c.idxCount = r.Chance(20) ? (int)r.Next() : r.Int(ni + 1);
                c.clipOn = r.Chance(60); c.clipX = r.Chance(30) ? (int)r.Next() : r.Int(100) - 20; c.clipY = r.Chance(30) ? (int)r.Next() : r.Int(100) - 20;
                c.clipW = r.Chance(30) ? (int)r.Next() : r.Int(100); c.clipH = r.Chance(30) ? (int)r.Next() : r.Int(100);
                c.texture = r.Chance(40) ? (const void*)&tag : r.Chance(20) ? (const void*)&other : nullptr;
                cmds.push_back(c);
            }
            UI::DrawData d;
            d.vertices = v.data(); d.vertexCount = nv; d.vertexStride = r.Chance(95) ? (int)sizeof(UI::DrawVertex) : 3;
            d.indices = idx.data(); d.indexCount = ni; d.cmds = cmds.data(); d.cmdCount = nc;
            d.atlasTexture = &tag; d.atlasRgba = r.Chance(90) ? atlas : nullptr; d.atlasW = r.Chance(90) ? 4 : r.Int(6) - 1; d.atlasH = r.Chance(90) ? 4 : r.Int(6) - 1;
            Bitmap bm(r.Int(70) - 3, r.Int(60) + 1);
            UI::soft::Render(d, bm);
        }
        CHECK(true);
    }
    TEST_MAIN_RESULT();
}
