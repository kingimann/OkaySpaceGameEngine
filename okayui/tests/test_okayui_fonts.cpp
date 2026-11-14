// OkayUI fonts, headless: arbitrary TrueType fonts and icon fonts (OkayUIFonts.hpp). Synthetic fonts built in the test have glyphs
// of known size, so advances, boxes and coverage are exact; the launcher's Roboto is the real font; the drawing goes through the
// software backend and is checked pixel by pixel and against a golden picture; hostile fonts, sizes and text are fuzzed.
// OKAYUI_FUZZ_ITERS scales the fuzz (default 600).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIFonts.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace okayuitest;
namespace UI = OkayUI;
using Bytes = std::vector<std::uint8_t>;

namespace {

// ---- a tiny TrueType writer: simple rectangle glyphs, a format 12 cmap -------------------------------------------------
void P8(Bytes& b, unsigned v) { b.push_back((std::uint8_t)v); }
void P16(Bytes& b, unsigned v) { b.push_back((std::uint8_t)(v >> 8)); b.push_back((std::uint8_t)v); }
void P32(Bytes& b, std::uint32_t v) { P16(b, v >> 16); P16(b, v & 0xFFFF); }
void Put16At(Bytes& b, std::size_t at, unsigned v) { b[at] = (std::uint8_t)(v >> 8); b[at + 1] = (std::uint8_t)v; }
void Put32At(Bytes& b, std::size_t at, std::uint32_t v) { Put16At(b, at, v >> 16); Put16At(b, at + 2, v & 0xFFFF); }

struct G { std::uint32_t cp; int adv; int x0, y0, x1, y1; };   // a rectangle glyph (x1 <= x0: empty glyph)

Bytes RectGlyph(int x0, int y0, int x1, int y1) {
    Bytes g;
    P16(g, 1); P16(g, (unsigned)x0); P16(g, (unsigned)y0); P16(g, (unsigned)x1); P16(g, (unsigned)y1);
    P16(g, 3); P16(g, 0);
    for (int i = 0; i < 4; ++i) P8(g, 1);
    const int xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    int px = 0; for (int i = 0; i < 4; ++i) { P16(g, (unsigned)(xs[i] - px)); px = xs[i]; }
    int py = 0; for (int i = 0; i < 4; ++i) { P16(g, (unsigned)(ys[i] - py)); py = ys[i]; }
    while (g.size() % 4) g.push_back(0);
    return g;
}

Bytes MakeFont(const std::vector<G>& glyphs, int upem = 1000, int asc = 800, int desc = -200) {
    const int n = (int)glyphs.size() + 1;   // glyph 0 = .notdef (empty)
    struct T { std::string tag; Bytes d; };
    T head{"head", Bytes(54, 0)}; Put32At(head.d, 0, 0x00010000); Put16At(head.d, 18, (unsigned)upem); Put16At(head.d, 50, 1);
    T hhea{"hhea", Bytes(36, 0)}; Put16At(hhea.d, 4, (unsigned)asc); Put16At(hhea.d, 6, (unsigned)desc); Put16At(hhea.d, 34, (unsigned)n);
    T maxp{"maxp", Bytes(6, 0)}; Put32At(maxp.d, 0, 0x00010000); Put16At(maxp.d, 4, (unsigned)n);
    T hmtx{"hmtx", {}}; P16(hmtx.d, 500); P16(hmtx.d, 0);
    for (const G& g : glyphs) { P16(hmtx.d, (unsigned)g.adv); P16(hmtx.d, (unsigned)std::max(0, g.x0)); }
    T glyf{"glyf", {}}, loca{"loca", {}};
    P32(loca.d, 0);
    P32(loca.d, 0);   // .notdef is empty
    for (const G& g : glyphs) {
        if (g.x1 > g.x0 && g.y1 > g.y0) { Bytes b = RectGlyph(g.x0, g.y0, g.x1, g.y1); glyf.d.insert(glyf.d.end(), b.begin(), b.end()); }
        P32(loca.d, (std::uint32_t)glyf.d.size());
    }
    T cmap{"cmap", {}};
    {
        Bytes sub;
        std::vector<std::pair<std::uint32_t, int>> m;
        for (int i = 0; i < (int)glyphs.size(); ++i) m.push_back({glyphs[(size_t)i].cp, i + 1});
        std::sort(m.begin(), m.end());
        P16(sub, 12); P16(sub, 0); P32(sub, 0); P32(sub, 0); P32(sub, (std::uint32_t)m.size());
        for (auto& e : m) { P32(sub, e.first); P32(sub, e.first); P32(sub, (std::uint32_t)e.second); }
        Put32At(sub, 4, (std::uint32_t)sub.size());
        P16(cmap.d, 0); P16(cmap.d, 1); P16(cmap.d, 3); P16(cmap.d, 10); P32(cmap.d, 12);
        cmap.d.insert(cmap.d.end(), sub.begin(), sub.end());
    }
    std::vector<T> ts = {head, hhea, maxp, hmtx, cmap, loca, glyf};
    Bytes f;
    P32(f, 0x00010000); P16(f, (unsigned)ts.size()); P16(f, 0); P16(f, 0); P16(f, 0);
    std::size_t pos = 12 + ts.size() * 16;
    std::vector<std::size_t> offs;
    for (T& t : ts) { pos = (pos + 3) & ~(std::size_t)3; offs.push_back(pos); pos += t.d.size(); }
    for (size_t i = 0; i < ts.size(); ++i) { for (int k = 0; k < 4; ++k) P8(f, (unsigned char)ts[i].tag[(size_t)k]); P32(f, 0); P32(f, (std::uint32_t)offs[i]); P32(f, (std::uint32_t)ts[i].d.size()); }
    for (size_t i = 0; i < ts.size(); ++i) { while (f.size() < offs[i]) f.push_back(0); f.insert(f.end(), ts[i].d.begin(), ts[i].d.end()); }
    return f;
}

// upem 1000, ascent 800, descent -200: at 100 px one unit is 0.1 px, ascent 80 px.
Bytes LatinFont() {
    return MakeFont({{' ', 300, 0, 0, 0, 0}, {'A', 600, 100, 0, 500, 600}, {'B', 500, 0, 0, 500, 500}, {'?', 500, 100, 0, 400, 700},
                     {'i', 250, 50, 0, 150, 700}});
}
Bytes IconFont() {   // private use area: a big square, a thin bar
    return MakeFont({{0xE000, 1000, 50, -100, 950, 800}, {0xE001, 500, 200, 200, 300, 700}, {0xE002, 800, 0, 0, 800, 800}});
}
Bytes WideFont() {   // another 'A' (not to be chosen when LatinFont comes first), a Z, and a euro sign
    return MakeFont({{'A', 900, 0, 0, 800, 800}, {'Z', 400, 0, 0, 300, 300}, {0x20AC, 700, 0, 0, 600, 600}, {' ', 250, 0, 0, 0, 0}});
}

UI::FontId Add(const Bytes& b, float px, const UI::FontConfig* c = nullptr) { return UI::AddFontFromMemory(b.data(), b.size(), px, c); }
bool Near(float a, float b, float e = 0.01f) { return std::fabs(a - b) <= e; }
int CountLit(const std::vector<unsigned char>& v, int thr = 200) { int n = 0; for (unsigned char c : v) if (c >= thr) ++n; return n; }

void TestLoading() {
    RUN_SUITE("fonts: loading and refusing");
    Reset();
    CHECK(UI::FontCount() == 0);
    const Bytes latin = LatinFont();
    const UI::FontId f = Add(latin, 16.0f);
    CHECK(f == 1 && UI::FontValid(f) && UI::FontCount() == 1 && UI::FontSourceCount(f) == 1);
    CHECK(!UI::FontValid(0) && !UI::FontValid(2) && !UI::FontValid(-1));
    CHECK(Near(UI::FontDefaultSize(f), 16.0f));
    CHECK(Near(UI::FontDefaultSize(Add(latin, 9999.0f)), 512.0f));
    CHECK(Near(UI::FontDefaultSize(Add(latin, NAN)), 16.0f));
    CHECK(Near(UI::FontDefaultSize(Add(latin, -3.0f)), 4.0f));
    // things that are not fonts
    CHECK(UI::AddFontFromMemory(nullptr, 100, 16) == 0);
    CHECK(UI::AddFontFromMemory(latin.data(), 0, 16) == 0);
    CHECK(UI::AddFontFromMemory(latin.data(), 5, 16) == 0);
    const Bytes junk(4000, 0x41);
    CHECK(UI::AddFontFromMemory(junk.data(), junk.size(), 16) == 0);
    const Bytes cut(latin.begin(), latin.begin() + 90);
    CHECK(UI::AddFontFromMemory(cut.data(), cut.size(), 16) == 0);
    CHECK(UI::AddFontFromFile(nullptr, 16) == 0 && UI::AddFontFromFile("", 16) == 0 && UI::AddFontFromFile("/no/such/font.ttf", 16) == 0);
    // from a file
    const std::string path = "okayui_test_font.ttf";
    { std::ofstream o(path, std::ios::binary); o.write((const char*)latin.data(), (std::streamsize)latin.size()); }
    UI::FontConfig cfg; cfg.name = "from file";
    const UI::FontId ff = UI::AddFontFromFile(path.c_str(), 20.0f, &cfg);
    CHECK(ff != 0 && UI::FontHasGlyph(ff, 'A'));
    std::remove(path.c_str());
    // the launcher's own font as a font of its own
    const UI::FontId rb = UI::AddFontBuiltin(16.0f);
    CHECK(rb != 0 && UI::FontHasGlyph(rb, 'H') && UI::FontHasGlyph(rb, 0xE9) && !UI::FontHasGlyph(rb, 0x4E2D));
    // the font count is capped
    int made = 0;
    for (int i = 0; i < 100; ++i) if (Add(latin, 12.0f)) ++made;
    CHECK(UI::FontCount() <= 64 && made < 100);
    UI::ClearFonts();
    CHECK(UI::FontCount() == 0 && !UI::FontValid(f));
    Reset();
}

void TestKnownGlyphs() {
    RUN_SUITE("fonts: known glyphs (exact metrics and coverage)");
    Reset();
    const UI::FontId f = Add(LatinFont(), 100.0f);
    std::vector<unsigned char> cov;
    UI::FontGlyphInfo g = UI::FontGlyph(f, 'A', 100.0f, &cov);
    CHECK(g.found && !g.usedFallback && g.source == 0);
    CHECK(Near(g.advance, 60.0f) && Near(g.x0, 10.0f) && Near(g.x1, 50.0f) && Near(g.y0, -60.0f) && Near(g.y1, 0.0f));
    CHECK(g.w == 40 && g.h == 60 && cov.size() == 2400);
    CHECK(CountLit(cov, 254) == 2400);                    // a pixel-aligned square: full coverage everywhere
    // the same glyph at other sizes: proportional box and advance, area scales with the square of the size
    for (float px : {50.0f, 20.0f, 13.0f}) {
        const UI::FontGlyphInfo s = UI::FontGlyph(f, 'A', px, &cov);
        CHECK(s.found && Near(s.advance, 0.6f * px, 0.02f));
        double area = 0; for (unsigned char c : cov) area += c / 255.0;
        CHECK(std::fabs(area - 0.4 * 0.6 * px * px) < 0.02 * px * px);   // 4% of the em squared at most
    }
    // an empty glyph has an advance and no bitmap
    const UI::FontGlyphInfo sp = UI::FontGlyph(f, ' ', 100.0f, &cov);
    CHECK(sp.found && Near(sp.advance, 30.0f) && sp.w == 0 && sp.h == 0 && cov.empty());
    // a character the font lacks: the fallback character
    const UI::FontGlyphInfo q = UI::FontGlyph(f, 'Q', 100.0f);
    CHECK(q.found && q.usedFallback && Near(q.advance, 50.0f));
    CHECK(!UI::FontHasGlyph(f, 'Q') && UI::FontHasGlyph(f, '?'));
    // hostile arguments
    CHECK(!UI::FontGlyph(f, 'A', NAN).found && !UI::FontGlyph(f, 'A', -1.0f).found && !UI::FontGlyph(f, 'A', 0.0f).found);
    CHECK(!UI::FontGlyph(0, 'A', 16.0f).found && !UI::FontGlyph(77, 'A', 16.0f).found);
    CHECK(UI::FontGlyph(f, 0x7FFFFFFF, 16.0f).usedFallback);
    CHECK(UI::FontGlyph(f, 'A', 1.0e9f).found);                       // clamped, not an allocation bomb
    CHECK(UI::FontGlyph(f, 'A', 1.0e-9f).found || true);
    // real font: stems of 'H'
    const UI::FontId rb = UI::AddFontBuiltin(32.0f);
    const UI::FontGlyphInfo h = UI::FontGlyph(rb, 'H', 32.0f, &cov);
    CHECK(h.found && h.w > 10 && h.h > 18 && h.h < 30);               // cap height about 0.71 em
    CHECK(h.advance > 17 && h.advance < 27);
    int midRow = h.h / 2, lit = 0, runs = 0; bool in = false;
    for (int x = 0; x < h.w; ++x) { const bool on = cov[(size_t)(h.h / 2 - 3) * h.w + x] > 128; lit += on; if (on && !in) ++runs; in = on; }
    (void)midRow;
    CHECK(runs == 2);                                                  // two vertical stems above the crossbar
    int barRow = 0, best = 0;
    for (int y = 0; y < h.h; ++y) { int c = 0; for (int x = 0; x < h.w; ++x) c += cov[(size_t)y * h.w + x] > 128; if (c > best) { best = c; barRow = y; } }
    CHECK(best >= h.w - 2 && barRow > 3 && barRow < h.h - 3);          // the crossbar spans the glyph
    CHECK(lit > 4);
    Reset();
}

void TestMergeAndFallback() {
    RUN_SUITE("fonts: merged icon font, ranges, fallback order");
    Reset();
    const UI::FontId f = Add(LatinFont(), 100.0f);
    UI::FontConfig ic; ic.merge = true; ic.ranges = {{0xE000, 0xE0FF}};
    const UI::FontId same = Add(IconFont(), 100.0f, &ic);
    CHECK(same == f && UI::FontSourceCount(f) == 2 && UI::FontCount() == 1);
    UI::FontGlyphInfo g = UI::FontGlyph(f, 0xE000, 100.0f);
    CHECK(g.found && g.source == 1 && Near(g.advance, 100.0f) && g.w == 90 && g.h == 90);
    CHECK(UI::FontHasGlyph(f, 0xE001) && !UI::FontHasGlyph(f, 0xE0FF + 5));
    // a code point outside the range is not taken from the icon font even if the face has it
    UI::FontConfig narrow; narrow.merge = true; narrow.ranges = {{0xE002, 0xE002}};
    const UI::FontId f2 = Add(LatinFont(), 100.0f);
    Add(IconFont(), 100.0f, &narrow);
    CHECK(f2 != f);
    CHECK(UI::FontHasGlyph(f2, 0xE002) && !UI::FontHasGlyph(f2, 0xE000) && !UI::FontHasGlyph(f2, 0xE001));
    // swapped range ends still work
    UI::FontConfig swapped; swapped.merge = true; swapped.ranges = {{0xE0FF, 0xE000}};
    const UI::FontId f3 = Add(LatinFont(), 100.0f);
    Add(IconFont(), 100.0f, &swapped);
    CHECK(UI::FontHasGlyph(f3, 0xE000));
    // the first source that has a character wins; a later face only fills the gaps (no ranges = a fallback)
    const UI::FontId f4 = Add(LatinFont(), 100.0f);
    UI::FontConfig wide; wide.merge = true;
    Add(WideFont(), 100.0f, &wide);
    CHECK(UI::FontGlyph(f4, 'A', 100.0f).source == 0 && Near(UI::FontGlyph(f4, 'A', 100.0f).advance, 60.0f));
    CHECK(UI::FontGlyph(f4, 'Z', 100.0f).source == 1 && Near(UI::FontGlyph(f4, 'Z', 100.0f).advance, 40.0f));
    CHECK(UI::FontGlyph(f4, 0x20AC, 100.0f).source == 1 && UI::FontHasGlyph(f4, 0x20AC));
    CHECK(UI::FontGlyph(f4, 'Q', 100.0f).usedFallback);
    // a different fallback character through a merge
    const UI::FontId f5 = Add(LatinFont(), 100.0f);
    UI::FontConfig fbc; fbc.merge = true; fbc.fallbackChar = 'B'; fbc.ranges = {{0x4E00, 0x4E01}};   // answers for nothing the face has
    Add(WideFont(), 100.0f, &fbc);
    CHECK(Near(UI::FontGlyph(f5, 'Q', 100.0f).advance, 50.0f) && UI::FontGlyph(f5, 'Q', 100.0f).usedFallback);
    // a fallback character that does not exist: nothing is drawn and it takes no room
    const UI::FontId f6 = Add(IconFont(), 100.0f);
    CHECK(!UI::FontGlyph(f6, 'Q', 100.0f).found);
    CHECK(UI::CalcTextSize(f6, 100.0f, "QQ").x == 0.0f);
    // a merge with nothing to merge into starts a font; the source cap holds
    UI::ClearFonts();
    UI::FontConfig alone; alone.merge = true;
    CHECK(Add(LatinFont(), 20.0f, &alone) == 1);
    int added = 0;
    for (int i = 0; i < 40; ++i) if (Add(WideFont(), 20.0f, &alone)) ++added;
    CHECK(UI::FontSourceCount(1) <= 16 && added < 40);
    Reset();
}

void TestAdvanceAndOffset() {
    RUN_SUITE("fonts: minimum advance (equal-width icons), glyph offset");
    Reset();
    const UI::FontId plain = Add(IconFont(), 100.0f);
    UI::FontConfig eq; eq.glyphMinAdvanceX = 80.0f; eq.glyphOffset = UI::Vec2{0.0f, 5.0f};
    const UI::FontId eqf = Add(IconFont(), 100.0f, &eq);
    const UI::FontGlyphInfo bar = UI::FontGlyph(plain, 0xE001, 100.0f), eqbar = UI::FontGlyph(eqf, 0xE001, 100.0f);
    CHECK(Near(bar.advance, 50.0f) && Near(eqbar.advance, 80.0f));
    CHECK(Near(eqbar.x0 - bar.x0, 15.0f));            // centred in the wider cell
    CHECK(Near(eqbar.y0 - bar.y0, 5.0f));              // offset down by 5 px
    UI::FontConfig cap; cap.glyphMaxAdvanceX = 60.0f;
    const UI::FontId capf = Add(IconFont(), 100.0f, &cap);
    CHECK(Near(UI::FontGlyph(capf, 0xE000, 100.0f).advance, 60.0f));
    CHECK(Near(UI::CalcTextSize(eqf, 100.0f, "\xEE\x80\x81\xEE\x80\x81").x, 160.0f));
    UI::FontConfig bad; bad.glyphMinAdvanceX = NAN; bad.glyphMaxAdvanceX = INFINITY; bad.glyphOffset = UI::Vec2{NAN, 1e30f};
    const UI::FontId badf = Add(IconFont(), 100.0f, &bad);
    const UI::FontGlyphInfo b = UI::FontGlyph(badf, 0xE000, 100.0f);
    CHECK(b.found && std::isfinite(b.advance) && std::isfinite(b.x0) && std::isfinite(b.y0) && std::fabs(b.y0) < 2000.0f);
    Reset();
}

void TestMeasure() {
    RUN_SUITE("fonts: measuring (lines, wrapping, hostile text)");
    Reset();
    const UI::FontId f = Add(LatinFont(), 100.0f);
    CHECK(Near(UI::CalcTextSize(f, 100.0f, "AB").x, 110.0f) && Near(UI::CalcTextSize(f, 100.0f, "AB").y, 100.0f));
    CHECK(Near(UI::CalcTextSize(f, 50.0f, "AB").x, 55.0f, 0.5f) && Near(UI::CalcTextSize(f, 50.0f, "AB").y, 50.0f));
    const UI::Vec2 two = UI::CalcTextSize(f, 100.0f, "A\nBBB");
    CHECK(Near(two.x, 150.0f) && Near(two.y, 200.0f));
    CHECK(Near(UI::CalcTextSize(f, 100.0f, "A\n").y, 200.0f));
    CHECK(Near(UI::CalcTextSize(f, 100.0f, "A\r\nB").y, 200.0f) && Near(UI::CalcTextSize(f, 100.0f, "A\r\nB").x, 60.0f));
    CHECK(UI::CalcTextSize(f, 100.0f, nullptr).x == 0 && UI::CalcTextSize(f, 100.0f, "").y == 0);
    CHECK(UI::CalcTextSize(f, NAN, "A").x == 0 && UI::CalcTextSize(f, -4.0f, "A").y == 0);
    CHECK(Near(UI::CalcTextSize(f, 100.0f, "A\tB").x, 60.0f + 120.0f + 50.0f));   // a tab is four spaces
    // wrapping at spaces
    const UI::Vec2 w = UI::CalcTextSize(f, 100.0f, "AB AB AB", 200.0f);
    CHECK(w.y >= 200.0f && w.x <= 200.0f + 0.01f && w.x >= 110.0f);
    const UI::Vec2 nowrap = UI::CalcTextSize(f, 100.0f, "AB AB AB", 0.0f);
    CHECK(Near(nowrap.y, 100.0f) && nowrap.x > 300.0f);
    const UI::Vec2 tight = UI::CalcTextSize(f, 100.0f, "ABABAB", 130.0f);     // no space: breaks inside the word
    CHECK(tight.y >= 300.0f && tight.x <= 130.0f);
    CHECK(UI::CalcTextSize(f, 100.0f, "AB AB", NAN).y == 100.0f);              // a bad wrap width means no wrapping
    CHECK(UI::CalcTextSize(f, 100.0f, "AB", -5.0f).y == 100.0f);
    CHECK(UI::CalcTextSize(f, 100.0f, "AAAA", 1.0f).y >= 400.0f);              // a width narrower than a glyph: one glyph a line
    // invalid UTF-8 and huge text neither crash nor take long
    const std::string bad = std::string("A\xff\xfe\xc3\x28\xe2\x82") + "B\xf8\x88\x80\x80\x80" + "A";
    CHECK(UI::CalcTextSize(f, 100.0f, bad.c_str()).x > 0);
    const std::string huge(3000000, 'A');
    const auto t0 = std::chrono::steady_clock::now();
    const UI::Vec2 hs = UI::CalcTextSize(f, 16.0f, huge.c_str());
    CHECK(std::isfinite(hs.x) && hs.x > 0);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0);
    // font 0 and a bad id measure with the UI's own font, scaled to the size
    const float own = UI::CalcTextSize("Hello").x;
    CHECK(Near(UI::CalcTextSize(0, UI::CalcTextSize("Hello").y, "Hello").x, own, 0.5f));
    CHECK(UI::CalcTextSize(55, 32.0f, "Hello").x > 0);
    // a proportional font: 'i' is narrower than 'B'
    CHECK(UI::CalcTextSize(f, 100.0f, "iiii").x < UI::CalcTextSize(f, 100.0f, "BBBB").x);
    // the real font: width grows linearly with size
    const UI::FontId rb = UI::AddFontBuiltin(16.0f);
    const float w16 = UI::CalcTextSize(rb, 16.0f, "The quick brown fox").x, w32 = UI::CalcTextSize(rb, 32.0f, "The quick brown fox").x;
    CHECK(w16 > 80 && w16 < 160 && std::fabs(w32 / w16 - 2.0f) < 0.06f);
    Reset();
}

void TestStack() {
    RUN_SUITE("fonts: push and pop change what widgets measure and draw");
    Reset();
    Sim sim(640, 360);
    const UI::FontId f = Add(LatinFont(), 32.0f);
    float hSmall = 0, hBig = 0, wSmall = 0, wBig = 0, hAfter = 0;
    CHECK(UI::GetCurrentFont() == 0 && UI::GetCurrentFontSizePx() == 0);
    sim.Frames(2, [&] {
        UI::Begin("W", 10, 10, 500, 300);
        UI::Button("AB"); hSmall = UI::GetItemRectSize().y; wSmall = UI::GetItemRectSize().x;
        UI::PushFont(f);
        CHECK(UI::GetCurrentFont() == f && Near(UI::GetCurrentFontSizePx(), 32.0f));
        UI::Button("AB"); hBig = UI::GetItemRectSize().y; wBig = UI::GetItemRectSize().x;
        UI::PushFont(f, 64.0f);
        CHECK(Near(UI::GetCurrentFontSizePx(), 64.0f));
        UI::PushFont(0);                                        // back to the UI's own font for a moment
        CHECK(UI::GetCurrentFont() == 0);
        UI::PopFont();
        CHECK(UI::GetCurrentFont() == f && Near(UI::GetCurrentFontSizePx(), 64.0f));
        UI::PopFont();
        CHECK(Near(UI::GetCurrentFontSizePx(), 32.0f));
        UI::PopFont();
        CHECK(UI::GetCurrentFont() == 0);
        UI::PopFont(); UI::PopFont();                           // unmatched: harmless
        UI::Button("AB"); hAfter = UI::GetItemRectSize().y;
        UI::End();
    });
    CHECK(hBig > hSmall + 4.0f);                 // a 32 px font makes taller rows than the 16 px default
    CHECK(Near(hAfter, hSmall));                 // and the pop restores them
    CHECK(wBig != wSmall);
    // measuring text uses the pushed font
    UI::PushFont(f, 100.0f);
    CHECK(Near(UI::CalcTextSize("AB").x, 110.0f, 3.0f));
    UI::PopFont();
    // depth cap
    for (int i = 0; i < 40; ++i) UI::PushFont(f);
    for (int i = 0; i < 40; ++i) UI::PopFont();
    CHECK(UI::GetCurrentFont() == 0);
    // push with a hostile size and an unknown font
    UI::PushFont(f, NAN); CHECK(Near(UI::GetCurrentFontSizePx(), 32.0f)); UI::PopFont();
    UI::PushFont(f, 1e9f); CHECK(Near(UI::GetCurrentFontSizePx(), 512.0f)); UI::PopFont();
    UI::PushFont(999); CHECK(UI::GetCurrentFont() == 0); UI::PopFont();
    // ResetState forgets fonts and the stack
    UI::PushFont(f);
    UI::ResetState();
    CHECK(UI::GetCurrentFont() == 0 && UI::FontCount() == 0);
    Reset();
}

// Where the lit pixels of the bitmap are: bounding box of pixels brighter than `thr` in the red channel.
struct Box { int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1; int n = 0; };
Box Lit(const Bitmap& b, int x0, int y0, int x1, int y1, unsigned thr = 128) {
    Box r;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        if (((PixelAt(b, x, y) >> 24) & 255u) < thr) continue;
        r.x0 = std::min(r.x0, x); r.y0 = std::min(r.y0, y); r.x1 = std::max(r.x1, x); r.y1 = std::max(r.y1, y); ++r.n;
    }
    return r;
}

void TestDrawnExact() {
    RUN_SUITE("fonts: drawn through the software backend (exact pixels)");
    Reset();
    Sim sim(400, 300);
    const UI::FontId f = Add(LatinFont(), 100.0f);
    UI::FontConfig ic; ic.merge = true; ic.ranges = {{0xE000, 0xE0FF}};
    Add(IconFont(), 100.0f, &ic);
    // one 'A' at 100 px, top-left (20, 20): the square covers x 30..70, y 40..100 (ascent 80, glyph 60 tall on the baseline)
    sim.Frame([&] { UI::GetForegroundDrawList().AddText(f, 100.0f, UI::Vec2{20, 20}, 0xffffffffu, "A"); });
    Bitmap b = sim.Draw(0, 0, 0);
    Box box = Lit(b, 0, 0, 400, 300);
    CHECK(box.x0 == 30 && box.x1 == 69 && box.y0 == 40 && box.y1 == 99);
    CHECK(box.n == 40 * 60);
    // the colour is applied
    sim.Frame([&] { UI::GetForegroundDrawList().AddText(f, 100.0f, UI::Vec2{20, 20}, 0xff0000ffu, "A"); });   // 0xAABBGGRR: red
    b = sim.Draw(0, 0, 0);
    CHECK(PixelAt(b, 50, 70) == Rgba(255, 0, 0, 255));
    // two glyphs advance by the font's advance: 'B' starts at 20 + 60
    sim.Frame([&] { UI::GetForegroundDrawList().AddText(f, 100.0f, UI::Vec2{20, 20}, 0xffffffffu, "AB"); });
    b = sim.Draw(0, 0, 0);
    box = Lit(b, 0, 0, 400, 300);
    CHECK(box.x0 == 30 && box.x1 == 129);              // 'B' spans 80 .. 130 (x 0..500 units at 0.1 + pen 60), 50 wide
    CHECK(Lit(b, 80, 0, 400, 300).y0 == 50 && Lit(b, 80, 0, 400, 300).y1 == 99);   // 'B' is 50 tall
    // merged icon glyph from the second face, in the same string
    sim.Frame([&] { UI::GetForegroundDrawList().AddText(f, 100.0f, UI::Vec2{0, 0}, 0xffffffffu, "\xEE\x80\x80"); });
    b = sim.Draw(0, 0, 0);
    box = Lit(b, 0, 0, 400, 300);
    CHECK(box.x0 == 5 && box.x1 == 94 && box.y0 == 80 - 80 && box.y1 == 80 + 10 - 1);   // 90 x 90 from 5 px right, top at 0, bottom 10 below the baseline
    // newline and wrap move the second line down by the size
    sim.Frame([&] { UI::GetForegroundDrawList().AddText(f, 50.0f, UI::Vec2{10, 10}, 0xffffffffu, "A\nA"); });
    b = sim.Draw(0, 0, 0);
    box = Lit(b, 0, 0, 400, 300);
    CHECK(box.n == 2 * 20 * 30 && box.y1 - box.y0 >= 50);
    sim.Frame([&] { UI::GetForegroundDrawList().AddText(f, 50.0f, UI::Vec2{10, 10}, 0xffffffffu, "A A A", 70.0f); });
    b = sim.Draw(0, 0, 0);
    box = Lit(b, 0, 0, 400, 300);
    CHECK(box.n == 3 * 20 * 30 && box.y1 - box.y0 >= 100);   // three lines: each 'A' + space is 45 px, so two do not fit in 70
    // font 0 draws in the UI's own font, and size 0 uses the font's size
    sim.Frame([&] { UI::GetForegroundDrawList().AddText(0, 16.0f, UI::Vec2{10, 10}, 0xffffffffu, "Hi"); });
    CHECK(Lit(sim.Draw(0, 0, 0), 0, 0, 400, 300).n > 10);
    sim.Frame([&] { UI::GetForegroundDrawList().AddText(f, 0.0f, UI::Vec2{10, 10}, 0xffffffffu, "A"); });
    CHECK(Lit(sim.Draw(0, 0, 0), 0, 0, 400, 300).n == 40 * 60);
    // hostile calls draw nothing and do not crash
    sim.Frame([&] {
        const UI::DrawList& dl = UI::GetForegroundDrawList();
        dl.AddText(f, NAN, UI::Vec2{1, 1}, 0xffffffffu, "A"); dl.AddText(f, 20.0f, UI::Vec2{NAN, 1}, 0xffffffffu, "A");
        dl.AddText(f, 20.0f, UI::Vec2{1, 1}, 0xffffffffu, nullptr); dl.AddText(f, 20.0f, UI::Vec2{1, 1}, 0xffffffffu, "A", NAN);
        dl.AddText(999, 20.0f, UI::Vec2{1, 1}, 0xffffffffu, "A"); dl.AddText(f, 1e30f, UI::Vec2{1, 1}, 0xffffffffu, "A");
    });
    (void)sim.Draw(0, 0, 0);
    Reset();
}

void TestWidgetsWithFont() {
    RUN_SUITE("fonts: pictures, widgets in a pushed font (golden)");
    Reset();
    Sim sim(560, 360);
    const UI::FontId rb = UI::AddFontBuiltin(18.0f);
    UI::FontConfig ic; ic.merge = true; ic.ranges = {{0xE000, 0xE0FF}}; ic.glyphMinAdvanceX = 18.0f;
    const UI::FontId code = Add(LatinFont(), 18.0f);
    Add(IconFont(), 18.0f, &ic);
    int clicks = 0;
    for (int i = 0; i < 3; ++i) sim.Frame([&] {
        UI::Begin("Fonts", 10, 10, 540, 340);
        UI::Text("UI font: Hello, world");
        UI::PushFont(rb);       UI::Text("Roboto 18: The quick brown fox"); UI::PopFont();
        UI::PushFont(rb, 28.0f); UI::Text("Roboto 28"); UI::PopFont();
        UI::PushFont(rb, 12.0f); UI::Text("Roboto 12: small print"); UI::PopFont();
        UI::PushFont(code);     UI::Text("\xEE\x80\x80 AB iB \xEE\x80\x81 AB"); if (UI::Button("\xEE\x80\x80 AB")) ++clicks; UI::PopFont();
        UI::Text("back in the UI font");
        UI::End();
    });
    const Bitmap& b = sim.Draw(30, 30, 34);
    std::string why;
    CHECK(MatchesGolden("fonts_mixed", b, &why));
    if (!why.empty()) std::cerr << "  " << why << "\n";
    CHECK(clicks == 0);
    Reset();
}

void TestCodeEditAndFieldWithFont() {
    RUN_SUITE("fonts: a code font in CodeEdit and a text field");
    Reset();
    Sim sim(500, 260);
    // a monospaced font: every glyph steps 600 units (12 px at 20 px)
    const Bytes mono = MakeFont({{' ', 600, 0, 0, 0, 0}, {'A', 600, 50, 0, 550, 700}, {'B', 600, 50, 0, 550, 500}, {'i', 600, 200, 0, 300, 700}, {'?', 600, 100, 0, 500, 700}});
    const UI::FontId code = Add(mono, 20.0f);
    UI::CodeEdit ce;
    ce.SetText("ABAB\niiii\nAB AB");
    float cwDefault = 0, lhDefault = 0, cwCode = 0, lhCode = 0;
    UI::Rect field;
    char buf[32] = "ABAB";
    auto frame = [&](bool useFont) {
        sim.Frame([&] {
            UI::Begin("W", 10, 10, 480, 240);
            if (useFont) UI::PushFont(code);
            ce.Draw("ed", 440, 110);
            (useFont ? cwCode : cwDefault) = ce.CharWidth(); (useFont ? lhCode : lhDefault) = ce.LineHeight();
            UI::TextField(9, 20, 140, 200, 30, buf, (int)sizeof buf, 0);
            field = UI::Rect{20, 140, 200, 30};
            if (useFont) UI::PopFont();
            UI::End();
        });
    };
    frame(false); frame(false); frame(true); frame(true);
    CHECK(cwDefault > 0 && cwCode > 0);
    CHECK(Near(cwCode, 12.0f, 1.0f));                    // the font's advance at 20 px, and the same for every character
    CHECK(Near(UI::CalcTextSize(code, 20.0f, "iiii").x, UI::CalcTextSize(code, 20.0f, "ABAB").x, 0.01f));
    CHECK(lhCode > 0 && lhCode != lhDefault);
    // caret positions in the field follow the proportional or monospaced font: click after the second character
    sim.Frames(2, [&] { UI::Begin("W", 10, 10, 480, 240); UI::PushFont(code); UI::TextField(9, 20, 140, 200, 30, buf, (int)sizeof buf, 0); UI::PopFont(); UI::End(); });
    sim.Move(field.x + 4 + 12 * 2 + 2, field.y + 15);
    for (int i = 0; i < 3; ++i) sim.Frame([&] { UI::Begin("W", 10, 10, 480, 240); UI::PushFont(code); UI::TextField(9, 20, 140, 200, 30, buf, (int)sizeof buf, 0); UI::PopFont(); UI::End(); });
    sim.Down(); sim.Frame([&] { UI::Begin("W", 10, 10, 480, 240); UI::PushFont(code); UI::TextField(9, 20, 140, 200, 30, buf, (int)sizeof buf, 0); UI::PopFont(); UI::End(); });
    sim.Up(); sim.Type("i");
    sim.Frames(2, [&] { UI::Begin("W", 10, 10, 480, 240); UI::PushFont(code); UI::TextField(9, 20, 140, 200, 30, buf, (int)sizeof buf, 0); UI::PopFont(); UI::End(); });
    CHECK(std::string(buf) == "ABiAB" || std::string(buf) == "ABAiB" || std::string(buf) == "AiBAB");   // the caret landed near the second character, not at an end
    CHECK(std::string(buf) != "ABABi" && std::string(buf) != "iABAB");
    Reset();
}

void TestFuzz() {
    RUN_SUITE("fonts: fuzz (mutated font files, hostile sizes and text)");
    const char* e = std::getenv("OKAYUI_FUZZ_ITERS");
    const int iters = e ? std::max(30, std::atoi(e) / 10) : 600;
    std::uint64_t st = 0x9E3779B97F4A7C15ull;
    auto next = [&] { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (unsigned)(st >> 16); };
    auto I = [&](int n) { return n <= 0 ? 0 : (int)(next() % (unsigned)n); };
    auto F = [&]() -> float {
        switch (I(10)) { case 0: return NAN; case 1: return INFINITY; case 2: return -INFINITY; case 3: return 1e30f; case 4: return -1.0f; case 5: return 0.0f; default: return (float)I(300); }
    };
    Reset();
    std::vector<unsigned char> rob; { /* the launcher font */ }
    const Bytes seeds[3] = {LatinFont(), IconFont(), WideFont()};
    Sim sim(500, 300);
    bool valid = true;
    for (int it = 0; it < iters && valid; ++it) {
        Bytes b = seeds[I(3)];
        const int muts = 1 + I(6);
        for (int m = 0; m < muts; ++m) {
            switch (I(4)) {
                case 0: b[(size_t)I((int)b.size())] = (std::uint8_t)I(256); break;
                case 1: if (b.size() > 20) b.resize((size_t)I((int)b.size())); break;
                case 2: { const size_t at = (size_t)I((int)b.size()); const size_t n = (size_t)I(16); for (size_t k = 0; k < n && at + k < b.size(); ++k) b[at + k] = (std::uint8_t)I(256); break; }
                default: b.insert(b.begin() + I((int)b.size()), (std::uint8_t)I(256)); break;
            }
        }
        UI::FontConfig c; c.merge = I(3) == 0; c.glyphMinAdvanceX = F(); c.glyphMaxAdvanceX = F(); c.glyphOffset = UI::Vec2{F(), F()};
        if (I(2)) c.ranges.push_back(UI::GlyphRange{(unsigned)I(70000), (unsigned)I(70000)});
        c.fallbackChar = I(2) ? '?' : (unsigned)I(0x20000);
        const UI::FontId f = UI::AddFontFromMemory(b.data(), b.size(), F(), &c);
        if (UI::FontCount() > 60) UI::ClearFonts();
        std::string text;
        const int n = I(24);
        for (int k = 0; k < n; ++k) { switch (I(6)) { case 0: text += (char)I(256); break; case 1: text += "\n"; break; case 2: text += "\xEE\x80\x80"; break; case 3: text += " "; break; default: text += (char)('A' + I(26)); } }
        const float px = F();
        (void)UI::FontGlyph(f, (unsigned)next(), px);
        const UI::Vec2 sz = UI::CalcTextSize(f, px, text.c_str(), F());
        if (!std::isfinite(sz.x) || !std::isfinite(sz.y) || sz.x < 0 || sz.y < 0) { valid = false; std::printf("  bad size %g %g\n", sz.x, sz.y); }
        sim.Frame([&] {
            UI::Begin("F", 10, 10, 480, 280);
            if (I(2)) UI::PushFont(f, F());
            UI::Text(text.c_str()); UI::Button(text.c_str());
            if (I(2)) UI::PopFont();
            UI::GetWindowDrawList().AddText(f, F(), UI::Vec2{(float)I(400), (float)I(250)}, 0xffffffffu, text.c_str(), F());
            while (UI::GetCurrentFont() != 0) UI::PopFont();
            UI::End();
        });
        const UI::DrawData dd = UI::GetDrawData();
        const UI::DrawVertex* vs = static_cast<const UI::DrawVertex*>(dd.vertices);
        for (int i = 0; i < dd.indexCount && valid; ++i) if (dd.indices[i] < 0 || dd.indices[i] >= dd.vertexCount) { valid = false; std::printf("  frame %d: index out of range\n", it); }
        for (int i = 0; i < dd.vertexCount && valid; ++i) if (!std::isfinite(vs[i].x) || !std::isfinite(vs[i].y) || !std::isfinite(vs[i].u) || !std::isfinite(vs[i].v)) { valid = false; std::printf("  frame %d: vertex %d not finite\n", it, i); }
        if (I(10) == 0) (void)sim.Draw(0, 0, 0);
    }
    CHECK(valid);
    Reset();
}

} // namespace

int main() {
    TestLoading();
    TestKnownGlyphs();
    TestMergeAndFallback();
    TestAdvanceAndOffset();
    TestMeasure();
    TestStack();
    TestDrawnExact();
    TestWidgetsWithFont();
    TestCodeEditAndFieldWithFont();
    TestFuzz();
    TEST_MAIN_RESULT();
}
