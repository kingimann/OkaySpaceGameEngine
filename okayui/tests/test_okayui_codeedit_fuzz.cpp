// Fuzzing OkayUI::CodeEdit (meant to run under ASan/UBSan: cmake -DOKAY_SANITIZE=address,undefined):
//  1. editing against a flat-string reference (insert, delete, backspace, cut, paste, selections, undo and redo), with the
//     invariants checked after every step;
//  2. the widget driven by random mouse, wheel, keys, text, resizes and API calls, frame after frame, rasterised now and then.
// OKAYUI_FUZZ_ITERS scales the work (default 1500 steps per scenario), OKAYUI_FUZZ_SEED changes the seed.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>

using namespace okayuitest;
namespace UI = OkayUI;
using UI::CodePos;
using UI::CodeEdit;

namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ull + 88172645463325252ull) {}
    std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (std::uint32_t)(s >> 16); }
    int Int(int n) { return n <= 0 ? 0 : (int)(Next() % (std::uint32_t)n); }
    bool Chance(int percent) { return Int(100) < percent; }
    float Uni(float lo, float hi) { return lo + (hi - lo) * (float)(Next() & 0xFFFFFF) / 16777215.0f; }
    float Nasty() {
        switch (Int(12)) {
            case 0: return 0.0f; case 1: return NAN; case 2: return INFINITY; case 3: return -INFINITY; case 4: return 1e30f;
            case 5: return -1e30f; case 6: return 1e-30f; case 7: return -1.0f; default: return Uni(-300, 900);
        }
    }
    // Any bytes at all, including invalid UTF-8, control characters, CR and LF.
    std::string Junk(int maxLen) {
        std::string s; const int n = Int(maxLen + 1);
        for (int i = 0; i < n; ++i) {
            switch (Int(7)) {
                case 0: s += (char)Int(256); break;
                case 1: s += (char)(32 + Int(95)); break;
                case 2: s += "\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80"[Int(3) == 0 ? 0 : (Int(2) ? 2 : 5)]; break;   // possibly half characters
                case 3: s += (char)Int(32); break;
                case 4: s += '\n'; break;
                default: s += (char)('a' + Int(26)); break;
            }
        }
        return s;
    }
    // Valid text only (ASCII, tab, newline, two-, three- and four-byte characters).
    std::string Valid(int maxLen) {
        static const char* parts[] = {"a", "b", "foo", " ", "  ", "\t", "\n", "\n", "(", ")", "{", "}", "\"", "//", "/*", "*/", "\xc3\xa9", "\xe6\x97\xa5", "\xf0\x9f\x98\x80", "_", "1", ";"};
        std::string s; const int n = Int(maxLen + 1);
        for (int i = 0; i < n; ++i) s += parts[Int((int)(sizeof parts / sizeof *parts))];
        return s;
    }
};

bool ValidUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        if (!n || i + (size_t)n > s.size()) return false;
        for (int k = 1; k < n; ++k) if (((unsigned char)s[i + (size_t)k] & 0xC0) != 0x80) return false;
        i += (size_t)n;
    }
    return true;
}

bool PosOk(const CodeEdit& e, CodePos p) {
    if (p.line < 0 || p.line >= e.LineCount()) return false;
    const std::string& l = e.Line(p.line);
    if (p.col < 0 || p.col > (int)l.size()) return false;
    return p.col == (int)l.size() || (((unsigned char)l[(size_t)p.col] & 0xC0) != 0x80);
}

// Everything that must hold after any operation.
bool Invariants(CodeEdit& e) {
    if (e.LineCount() < 1) return false;
    for (int i = 0; i < e.LineCount(); ++i) {
        const std::string& l = e.Line(i);
        if (l.find('\n') != std::string::npos || l.find('\r') != std::string::npos || !ValidUtf8(l)) return false;
    }
    if (!PosOk(e, e.Caret()) || !PosOk(e, e.Anchor())) return false;
    if (e.SelectionEnd() < e.SelectionStart()) return false;
    if (!std::isfinite(e.ScrollX()) || !std::isfinite(e.ScrollY()) || e.ScrollX() < 0 || e.ScrollY() < 0) return false;
    for (int b : e.Breakpoints()) if (b < 0) return false;
    for (size_t i = 1; i < e.Breakpoints().size(); ++i) if (e.Breakpoints()[i - 1] >= e.Breakpoints()[i]) return false;
    // GetText round trip
    const std::string t = e.GetText();
    CodeEdit c; c.SetText(t);
    return c.GetText() == t && c.LineCount() == e.LineCount();
}

size_t Offset(const CodeEdit& e, CodePos p) {
    size_t o = 0;
    for (int l = 0; l < p.line; ++l) o += e.Line(l).size() + 1;
    return o + (size_t)p.col;
}
size_t PrevChar(const std::string& s, size_t o) { if (!o) return 0; --o; while (o > 0 && ((unsigned char)s[o] & 0xC0) == 0x80) --o; return o; }
size_t NextChar(const std::string& s, size_t o) { if (o >= s.size()) return s.size(); ++o; while (o < s.size() && ((unsigned char)s[o] & 0xC0) == 0x80) ++o; return o; }

int Iters() { const char* v = std::getenv("OKAYUI_FUZZ_ITERS"); return v && *v ? std::max(10, std::atoi(v)) : 1500; }
std::uint64_t Seed() { const char* v = std::getenv("OKAYUI_FUZZ_SEED"); return v && *v ? (std::uint64_t)std::atoll(v) : 20240611ull; }

void ModelFuzz(std::uint64_t seed) {
    Rng rng(seed);
    CodeEdit e;
    e.SetAutoIndent(false);
    std::string init = rng.Valid(60);
    e.SetText(init);
    std::string ref = e.GetText();
    std::set<std::string> seen{ref};
    UI::SetClipboardText("");
    bool ok = true;
    const int steps = Iters();
    for (int step = 0; step < steps && ok; ++step) {
        const int op = rng.Int(14);
        // a random selection or caret
        if (rng.Chance(50)) {
            const CodePos a{rng.Int(e.LineCount() + 2) - 1, rng.Int(40) - 2}, b{rng.Int(e.LineCount() + 2) - 1, rng.Int(40) - 2};
            if (rng.Chance(50)) e.SetSelection(a, b); else e.SetCaret(a);
        }
        const size_t lo = Offset(e, e.SelectionStart()), hi = Offset(e, e.SelectionEnd());
        const bool sel = lo != hi;
        std::string want = ref;
        bool expectChange = false;
        switch (op) {
            case 0: case 1: case 2: {
                const std::string ins = rng.Valid(6);
                e.InsertText(ins.c_str());
                want = ref.substr(0, lo) + ins + ref.substr(hi);
                break;
            }
            case 3: if (e.Backspace()) {} want = sel ? ref.substr(0, lo) + ref.substr(hi) : (lo ? ref.substr(0, PrevChar(ref, lo)) + ref.substr(lo) : ref); break;
            case 4: if (e.DeleteForward()) {} want = sel ? ref.substr(0, lo) + ref.substr(hi) : (lo < ref.size() ? ref.substr(0, lo) + ref.substr(NextChar(ref, lo)) : ref); break;
            case 5: { const std::string c = ref.substr(lo, hi - lo); e.Cut(); if (sel && c.size() < (size_t)UI::kClipboardMax) want = ref.substr(0, lo) + ref.substr(hi); break; }
            case 6: e.Copy(); break;
            case 7: { const std::string clip = UI::GetClipboardText(); e.Paste(); if (!clip.empty()) want = ref.substr(0, lo) + clip + ref.substr(hi); break; }
            case 8: e.Undo(); want = e.GetText(); expectChange = true; break;
            case 9: e.Redo(); want = e.GetText(); expectChange = true; break;
            case 10: e.Move((UI::CodeMove)rng.Int(12), rng.Chance(40)); break;
            case 11: e.SelectAll(); break;
            case 12: e.SetFind(rng.Valid(2), rng.Chance(50)); if (rng.Chance(50)) e.FindNext(); else e.FindPrev(); break;
            default: e.Tab(rng.Chance(40)); want = e.GetText(); expectChange = true; break;   // line-based: only the invariants and undo are checked
        }
        (void)expectChange;
        const std::string got = e.GetText();
        if (got != want) { std::cerr << "  model mismatch at step " << step << " op " << op << "\n"; ok = false; break; }
        if (!Invariants(e)) { std::cerr << "  invariant broken at step " << step << " op " << op << "\n"; ok = false; break; }
        if (op == 8 || op == 9) { if (!seen.count(got)) { std::cerr << "  undo/redo produced a text never seen, step " << step << "\n"; ok = false; break; } }
        seen.insert(got);
        ref = got;
        if (op != 8 && op != 9 && rng.Chance(30) && e.CanUndo()) {   // an undo followed by a redo changes nothing
            e.Undo();
            if (!Invariants(e)) { std::cerr << "  invariant broken after undo at step " << step << " op " << op << "\n"; ok = false; break; }
            e.Redo();
            if (e.GetText() != got) { std::cerr << "  undo+redo differs at step " << step << " op " << op << "\n"; ok = false; break; }
        }
    }
    CHECK(ok);
    // undo everything: the first text; redo everything: the last. (The history keeps 2000 groups; a run has fewer steps than that.)
    while (e.Redo()) { }                // the run may have left undone steps
    const std::string last = e.GetText();
    int undone = 0;
    while (e.Undo()) { ++undone; if (!Invariants(e)) { ok = false; break; } }
    CHECK(ok);
    if (undone < 2000) CHECK(e.GetText() == init);   // the history keeps 2000 groups
    int redone = 0;
    while (e.Redo()) ++redone;
    CHECK(redone == undone);
    CHECK(e.GetText() == last);
}

void WidgetFuzz(std::uint64_t seed) {
    Rng rng(seed);
    Reset();
    Sim sim(480, 320);
    CodeEdit e;
    bool open = true;
    float w = 400, h = 240;
    e.SetLanguage(UI::OkayScriptLanguage());
    e.SetText(rng.Valid(300));
    const int frames = Iters();
    static const UI::KeyId named[] = {UI::KeyId_Tab, UI::KeyId_PageUp, UI::KeyId_PageDown, UI::KeyId_Z, UI::KeyId_Y, UI::KeyId_A, UI::KeyId_Left, UI::KeyId_Home};
    bool ok = true;
    for (int f = 0; f < frames && ok; ++f) {
        if (rng.Chance(30)) { sim.Move(rng.Chance(80) ? rng.Uni(0, 480) : rng.Nasty(), rng.Chance(80) ? rng.Uni(0, 320) : rng.Nasty()); }
        if (rng.Chance(15)) { if (sim.down) sim.Up(); else sim.Down(); }
        if (rng.Chance(8)) sim.wheel = rng.Nasty();
        if (rng.Chance(20)) sim.Type(rng.Junk(6));
        sim.shift = rng.Chance(20); sim.ctrl = rng.Chance(20);
        for (int k = 0; k < Key_COUNT; ++k) if (rng.Chance(6)) sim.edge[k] = true;
        for (UI::KeyId k : named) sim.Hold(k, rng.Chance(8));
        if (rng.Chance(3)) sim.blocked = !sim.blocked;
        if (rng.Chance(2)) { w = rng.Chance(30) ? rng.Nasty() : rng.Uni(0, 460); h = rng.Chance(30) ? rng.Nasty() : rng.Uni(0, 300); }
        if (rng.Chance(2)) e.SetText(rng.Chance(50) ? rng.Junk(200) : rng.Valid(400));
        if (rng.Chance(2)) e.SetReadOnly(rng.Chance(30));
        if (rng.Chance(2)) e.SetTabSize(rng.Int(20) - 2);
        if (rng.Chance(2)) e.SetMarkers({UI::CodeMarker{rng.Int(30) - 3, rng.Int(20) - 2, rng.Int(20) - 4, "m", 0}});
        if (rng.Chance(2)) e.SetBreakpoints({rng.Int(30) - 3, rng.Int(30), rng.Int(30)});
        if (rng.Chance(2)) e.SetFind(rng.Chance(30) ? std::string() : rng.Valid(2), rng.Chance(50));
        if (rng.Chance(1)) e.SetHighlighter(rng.Chance(50) ? UI::MakeCLikeHighlighter(UI::CLikeLanguage()) : UI::CodeHighlighter());
        if (rng.Chance(2)) e.SetLineSpacing(rng.Nasty());
        if (rng.Chance(1)) e.SetShowLineNumbers(rng.Chance(50));
        if (rng.Chance(1)) e.SetShowBreakpointGutter(rng.Chance(50));
        if (rng.Chance(2)) e.SetScroll(rng.Nasty(), rng.Nasty());
        if (rng.Chance(2)) e.GoToLine(rng.Int(80) - 10);
        if (rng.Chance(1)) UI::SetClipboardText(rng.Junk(40).c_str());
        sim.Frame([&] {
            if (UI::Begin("W", 10, 10, 460, 300, &open)) e.Draw("ed", w, h);
            UI::End();
        });
        if (f % 50 == 0) sim.Draw();
        if (!Invariants(e)) { std::cerr << "  widget invariant broken at frame " << f << "\n"; ok = false; }
    }
    CHECK(ok);
}
} // namespace

int main() {
    RUN_SUITE("okayui codeedit fuzz");
    for (int i = 0; i < 6; ++i) ModelFuzz(Seed() + (std::uint64_t)i);
    for (int i = 0; i < 6; ++i) WidgetFuzz(Seed() + 100 + (std::uint64_t)i);
    TEST_MAIN_RESULT();
}
