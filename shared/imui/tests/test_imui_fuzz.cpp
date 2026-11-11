// okay::imui fuzzing: random (seeded, reproducible) input, geometry and widget trees, plus a reference-model
// check of text editing. Meant to run under ASan/UBSan (cmake -DOKAY_SANITIZE=address,undefined). Set
// IMUI_FUZZ_ITERS to scale the work (default 1500 frames per scenario) and IMUI_FUZZ_SEED to change the seed.
#include "test_framework.hpp"
#include "ImuiTestKit.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>

using namespace imuitest;

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
                case 2: Utf8Append(s, 0x80 + (std::uint32_t)Int(0x2000)); break;
                case 3: s += (char)Int(32); break;                          // control characters
                default: s += (char)('a' + Int(26)); break;
            }
        }
        return s;
    }
};

int Iters() { const char* e = std::getenv("IMUI_FUZZ_ITERS"); return e ? std::max(10, std::atoi(e)) : 1500; }
std::uint64_t Seed() { const char* e = std::getenv("IMUI_FUZZ_SEED"); return e ? (std::uint64_t)std::atoll(e) : 12345; }

bool DataSane(const DrawData& dd) {
    for (const DrawList* l : dd.lists) {
        if (!l) return false;
        for (const Vertex& v : l->vtx) if (!std::isfinite(v.pos.x) || !std::isfinite(v.pos.y) || !std::isfinite(v.uv.x) || !std::isfinite(v.uv.y)) return false;
        for (const DrawCmd& c : l->cmds) {
            if ((std::size_t)c.idxOffset + c.idxCount > l->idx.size() || c.idxCount % 3) return false;
            if (!std::isfinite(c.clip.min.x) || !std::isfinite(c.clip.max.x) || c.clip.max.x < c.clip.min.x || c.clip.max.y < c.clip.min.y) return false;
        }
        for (unsigned i : l->idx) if (i >= l->vtx.size()) return false;
    }
    return true;
}

// A random but well nested widget tree. Every Begin / Push has its matching End / Pop.
void RandomWidgets(Rng& r, int depth, std::string* strs, bool* bools, float* floats, int* ints, int* combo) {
    static const char* const items[] = {"Alpha", "Beta", "", "A very long combo entry that will not fit", "\xC3\xA9"};
    const int n = 1 + r.Int(8);
    for (int i = 0; i < n; ++i) {
        const std::string lbl = r.Chance(10) ? r.Junk(40) : std::string("w") + std::to_string(r.Int(6));
        if (r.Chance(10)) SameLine(r.Chance(50) ? r.Nasty() : 0, r.Chance(50) ? r.Nasty() : -1);
        if (r.Chance(8)) SetNextItemWidth(r.Nasty());
        switch (r.Int(24)) {
            case 0: Button(lbl.c_str(), Vec2(r.Nasty(), r.Nasty())); break;
            case 1: SmallButton(lbl.c_str()); break;
            case 2: Checkbox(lbl.c_str(), &bools[r.Int(4)]); break;
            case 3: SliderFloat(lbl.c_str(), &floats[r.Int(4)], r.Nasty(), r.Nasty(), r.Chance(20) ? "%d%n%s" : "%.2f"); break;
            case 4: SliderInt(lbl.c_str(), &ints[r.Int(4)], r.NastyInt(100000), r.NastyInt(100000), r.Chance(20) ? "%f" : "%d"); break;
            case 5: DragFloat(lbl.c_str(), &floats[r.Int(4)], r.Nasty(), r.Nasty(), r.Nasty()); break;
            case 6: InputText(lbl.c_str(), &strs[r.Int(3)], r.Int(8), r.Chance(50) ? 16 : r.NastyInt(5000)); break;
            case 7: Combo(lbl.c_str(), r.Chance(10) ? nullptr : combo, items, r.Chance(10) ? -3 : 5); break;
            case 8: Selectable(lbl.c_str(), r.Chance(50), Vec2(r.Nasty(), r.Nasty())); break;
            case 9: Text("%s", lbl.c_str()); break;
            case 10: TextUnformatted(lbl.c_str()); break;
            case 11: TextColored(r.Next(), "c %d", r.Int(100)); break;
            case 12: Separator(); Spacing(); break;
            case 13: Dummy(Vec2(r.Nasty(), r.Nasty())); break;
            case 14: if (r.Chance(50)) Indent(r.Nasty()); else Unindent(r.Nasty()); break;   // unbalanced indents are legal
            case 15: if (depth < 3 && TreeNode(lbl.c_str())) { RandomWidgets(r, depth + 1, strs, bools, floats, ints, combo); TreePop(); } break;
            case 16: if (depth < 3) { BeginGroup(); RandomWidgets(r, depth + 1, strs, bools, floats, ints, combo); EndGroup(); } break;
            case 17: if (depth < 3 && BeginScrollRegion(lbl.c_str(), Vec2(r.Nasty(), r.Nasty()))) { RandomWidgets(r, depth + 1, strs, bools, floats, ints, combo); EndScrollRegion(); } break;
            case 18: Columns(r.Int(40) - 3); break;
            case 19: NextColumn(); break;
            case 20: PushID(r.Chance(50) ? lbl.c_str() : "id"); PushID(r.Int(5)); RandomWidgets(r, depth + 1, strs, bools, floats, ints, combo); PopID(); PopID(); break;
            case 21: PushStyleColor((ColIdx)r.Int(Col_Count), r.Next()); Text("styled"); PopStyleColor(); break;
            case 22: AlignTextToFramePadding(); Text("aligned"); break;
            default: SetCursorScreenPos(Vec2(r.Nasty(), r.Nasty())); if (r.Chance(50)) { Vec2 a = GetContentRegionAvail(); (void)a; GetCursorScreenPos(); } break;
        }
        if (r.Chance(20)) { IsItemHovered(); IsItemActive(); IsItemClicked(); GetItemRect(); }
    }
    Columns(1);
}
}

int main() {
    RUN_SUITE("imui fuzz");
    const int iters = Iters();
    const std::uint64_t seed = Seed();

    // ---- random frames, random input, random widget trees ------------------------------------
    {
        Context* ctx = MakeContext();
        Rng r(seed);
        std::string strs[3] = {"hello", "", "caf\xC3\xA9"};
        bool bools[4] = {}; float floats[4] = {0.5f, 0, 1e9f, -3}; int ints[4] = {1, 2, 3, 4}; int combo = 1;
        Input in; Bitmap bm(96, 72);
        int bad = 0;
        for (int f = 0; f < iters; ++f) {
            // input
            if (r.Chance(60)) in.mousePos = Vec2(r.Chance(20) ? r.Nasty() : r.Uni(-20, 700), r.Chance(20) ? r.Nasty() : r.Uni(-20, 500));
            for (int b = 0; b < 3; ++b) if (r.Chance(20)) in.mouseDown[b] = !in.mouseDown[b];
            in.wheel = r.Chance(10) ? r.Nasty() : 0;
            for (int k = 0; k < Key_Count; ++k) if (r.Chance(15)) in.keyDown[k] = !in.keyDown[k];
            in.ctrl = r.Chance(20); in.shift = r.Chance(20);
            in.text = r.Chance(30) ? r.Junk(12) : "";
            in.dt = r.Chance(10) ? r.Nasty() : 1.0f / 60;
            const Vec2 disp = r.Chance(10) ? Vec2(r.Nasty(), r.Nasty()) : Vec2(640, 480);
            NewFrame(in, disp);
            const int windows = 1 + r.Int(4);
            for (int w = 0; w < windows; ++w) {
                if (r.Chance(40)) SetNextWindowPos(Vec2(r.Nasty(), r.Nasty()), r.Chance(50) ? Cond_Always : Cond_Once);
                if (r.Chance(40)) SetNextWindowSize(Vec2(r.Nasty(), r.Nasty()), r.Chance(50) ? Cond_Always : Cond_Once);
                bool open = true;
                const std::string name = r.Chance(10) ? r.Junk(20) : "win" + std::to_string(r.Int(5));
                Begin(name.c_str(), r.Chance(50) ? &open : nullptr, r.Int(512));
                RandomWidgets(r, 0, strs, bools, floats, ints, &combo);
                End();
            }
            if (r.Chance(5)) { CloseCurrentPopup(); }
            const DrawData& dd = Render();
            if (!DataSane(dd)) ++bad;
            if (f % 7 == 0) { bm.Clear(MakeCol(0, 0, 0)); SoftRender(dd, ctx->font, bm); }
            for (auto& s : strs) if (!s.empty() && !Utf8Valid(s) && s != "hello" && s != "caf\xC3\xA9") ++bad;
        }
        CHECK(bad == 0);
        CHECK(ctx->errors == 0);       // the generator is balanced, so the library must not report misuse
        for (int i = 0; i < 4; ++i) CHECK(!std::isnan(floats[i]) || i == 99);
        DestroyContext(ctx);
    }

    // ---- unbalanced calls and garbage: counted, never fatal ---------------------------------
    {
        Context* ctx = MakeContext();
        Rng r(seed + 1);
        Input in;
        for (int f = 0; f < iters / 3; ++f) {
            NewFrame(in, Vec2(300, 200));
            const int n = r.Int(30);
            for (int i = 0; i < n; ++i) {
                switch (r.Int(16)) {
                    case 0: Begin("a"); break; case 1: End(); break; case 2: BeginScrollRegion("s"); break; case 3: EndScrollRegion(); break;
                    case 4: BeginGroup(); break; case 5: EndGroup(); break; case 6: PushID(1); break; case 7: PopID(); break;
                    case 8: TreeNode("t"); break; case 9: TreePop(); break; case 10: PushStyleColor(Col_Text, 1); break; case 11: PopStyleColor(3); break;
                    case 12: Columns(r.Int(5)); break; case 13: NextColumn(); break; case 14: BeginCombo("c", "p"); break; default: EndCombo(); break;
                }
            }
            Render();
            if (r.Chance(5)) Render();          // double Render
            if (r.Chance(5)) NewFrame(in, Vec2(300, 200));   // NewFrame without Render
        }
        CHECK(ctx->errors > 0);
        DestroyContext(ctx);
        SetCurrentContext(nullptr);
        Begin("no context"); End(); Text("x"); Button("b"); NewFrame(Input(), Vec2(1, 1)); Render();   // no current context: inert
        CHECK(!Button("b") && !Checkbox("c", nullptr) && GetActiveId() == 0 && GetID("x") != 0 + 0 * 1);
    }

    // ---- text editing against a reference model ----------------------------------------------
    {
        Context* ctx = MakeContext();
        Rng r(seed + 2);
        Sim sim(ctx);
        std::string text; Rect rc;
        auto ui = [&] { SetNextWindowPos(Vec2(0, 0), Cond_Once); SetNextWindowSize(Vec2(300, 100), Cond_Once); Begin("m", nullptr, Win_NoTitleBar); InputText("t", &text, 0, 40); rc = GetItemRect(); End(); };
        sim.Frames(2, ui);
        sim.Click(Vec2(rc.min.x + 5, rc.min.y + 8), ui);
        std::string model; int cur = 0;        // ASCII only, so one byte per character
        int mismatches = 0;
        for (int i = 0; i < iters; ++i) {
            switch (r.Int(8)) {
                case 0: case 1: case 2: {
                    const char ch = (char)('a' + r.Int(26));
                    sim.Type(std::string(1, ch)); sim.Frame(ui);
                    if ((int)model.size() < 40) { model.insert((size_t)cur, 1, ch); ++cur; }
                    break;
                }
                case 3: sim.Tap(Key_Backspace, ui); if (cur > 0) { model.erase((size_t)cur - 1, 1); --cur; } break;
                case 4: sim.Tap(Key_Delete, ui); if (cur < (int)model.size()) model.erase((size_t)cur, 1); break;
                case 5: sim.Tap(Key_Left, ui); if (cur > 0) --cur; break;
                case 6: sim.Tap(Key_Right, ui); if (cur < (int)model.size()) ++cur; break;
                default: if (r.Chance(50)) { sim.Tap(Key_Home, ui); cur = 0; } else { sim.Tap(Key_End, ui); cur = (int)model.size(); } break;
            }
            const InputTextState& st = ctx->inputState[GetID("t") * 0 + ctx->focusId];
            if (text != model || st.cursor != cur || st.anchor != cur) ++mismatches;
        }
        CHECK(mismatches == 0);
        DestroyContext(ctx);
    }

    // ---- layout properties: stacked items never overlap, same-line items advance ---------------
    {
        Context* ctx = MakeContext();
        Rng r(seed + 3);
        Sim sim(ctx);
        int overlaps = 0;
        for (int f = 0; f < iters / 3; ++f) {
            const int n = 2 + r.Int(10);
            std::vector<Vec2> sizes; std::vector<bool> same;
            for (int i = 0; i < n; ++i) { sizes.push_back(Vec2(r.Uni(0, 150), r.Uni(0, 60))); same.push_back(i > 0 && r.Chance(40)); }
            const float indent = r.Chance(30) ? r.Uni(0, 40) : 0;
            sim.Frame([&] {
                SetNextWindowPos(Vec2(0, 0), Cond_Once); SetNextWindowSize(Vec2(600, 400), Cond_Once);
                Begin("l", nullptr, Win_NoTitleBar);
                if (indent > 0) Indent(indent);
                Rect prev, lineTop;
                for (int i = 0; i < n; ++i) {
                    if (same[(size_t)i]) SameLine();
                    Dummy(sizes[(size_t)i]);
                    const Rect cur = GetItemRect();
                    if (i > 0) {
                        if (same[(size_t)i]) { if (cur.min.x < prev.max.x - 1e-3f || std::fabs(cur.min.y - prev.min.y) > 1e-3f) ++overlaps; }
                        else if (cur.min.y < lineTop.max.y - 1e-3f) ++overlaps;
                    }
                    if (!same[(size_t)i]) lineTop = cur; else lineTop.max.y = std::max(lineTop.max.y, cur.max.y);
                    prev = cur;
                }
                if (indent > 0) Unindent(indent);
                End();
            });
        }
        CHECK(overlaps == 0);
        DestroyContext(ctx);
    }

    // ---- raw draw list and rasteriser with hostile numbers -------------------------------------
    {
        Context* ctx = MakeContext();
        Rng r(seed + 4);
        for (int f = 0; f < iters / 3; ++f) {
            DrawList dl; dl.Init(&ctx->font, Rect(r.Nasty(), r.Nasty(), r.Nasty(), r.Nasty()));
            const int n = r.Int(20);
            for (int i = 0; i < n; ++i) {
                const Vec2 a(r.Nasty(), r.Nasty()), b(r.Nasty(), r.Nasty());
                const Col c = r.Next();
                switch (r.Int(10)) {
                    case 0: dl.AddRectFilled(a, b, c, r.Nasty()); break; case 1: dl.AddRect(a, b, c, r.Nasty(), r.Nasty()); break;
                    case 2: dl.AddLine(a, b, c, r.Nasty()); break; case 3: dl.AddTriangleFilled(a, b, Vec2(r.Nasty(), r.Nasty()), c); break;
                    case 4: dl.AddCircleFilled(a, r.Nasty(), c, r.NastyInt(80)); break;
                    case 5: dl.AddText(a, c, r.Junk(10).c_str()); break; case 6: dl.PushClipRect(Rect(a, b), r.Chance(50)); break;
                    case 7: dl.PopClipRect(); break; case 8: dl.AddImage((std::uint32_t)r.Int(3), a, b, Vec2(r.Nasty(), r.Nasty()), Vec2(r.Nasty(), r.Nasty()), c); break;
                    default: { std::string s = r.Junk(10); dl.AddText(a, c, s.c_str(), s.c_str() + s.size() / 2); break; }
                }
            }
            DrawData dd; dd.lists.push_back(&dl);
            Bitmap bm((int)r.Int(70) - 3, r.Int(60) + 1);
            SoftRender(dd, ctx->font, bm);
            if (r.Chance(10)) {   // corrupt the buffers themselves
                for (auto& i : dl.idx) if (r.Chance(10)) i = r.Next();
                for (auto& c : dl.cmds) if (r.Chance(30)) { c.idxOffset = r.Next(); c.idxCount = r.Next(); }
                Bitmap b2(40, 30); SoftRender(dd, ctx->font, b2);
            }
        }
        CHECK(true);
        DestroyContext(ctx);
    }

    // ---- UTF-8 and hashing on random bytes ---------------------------------------------------
    {
        Rng r(seed + 5);
        int bad = 0;
        for (int i = 0; i < iters * 4; ++i) {
            const std::string s = r.Junk(24);
            const char* p = s.data(); const char* e = p + s.size();
            std::string again;
            while (p < e) {
                std::uint32_t cp; const int n = Utf8Decode(p, e, &cp);
                if (n < 1 || n > 4 || p + n > e) { ++bad; break; }
                Utf8Append(again, cp);
                p += n;
            }
            if (!Utf8Valid(again)) ++bad;               // re-encoding always gives valid UTF-8
            if (HashBytes(s.data(), s.size(), r.Next()) == 0) ++bad;
        }
        CHECK(bad == 0);
    }
    TEST_MAIN_RESULT();
}
