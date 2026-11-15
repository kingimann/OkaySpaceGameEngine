// OkayUI::CodeEdit against what the script editor needs from its code widget (docs/own_code_editor.md, "Parity table"):
// the pieces that were missing or partial before this test existed, plus hostile input.
//   1. identifier docs: built-in names coloured as known words and a hover tooltip with their text
//   2. a proportional font (the launcher look's Roboto): glyphs sit on the character grid, so the caret and the selection line up
//   3. the clipboard is no longer 4 KB: copy, cut and paste of big selections, and what happens past the (much bigger) limit
//   4. hostile input: NaN and infinite scroll / mouse, extreme positions and line numbers, one 4 MB line, one million lines,
//      invalid UTF-8 through every way text comes in
// Written from the widget's own public API and the behaviour list in docs/own_ui_code_editor.md; nothing here is taken from another editor.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <algorithm>
#include <unordered_map>

using namespace okayuitest;
namespace UI = OkayUI;
using UI::CodeEdit;
using UI::CodePos;

namespace {

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
constexpr double kSlack = 20.0;
#else
constexpr double kSlack = 1.0;
#endif

CodePos P(int l, int c) { return CodePos{l, c}; }
bool At(const CodeEdit& e, int l, int c) { return e.Caret() == P(l, c) && !e.HasSelection(); }
double Ms(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}

struct Rig {
    Sim sim;
    CodeEdit ce;
    bool open = true;
    float w = 500, h = 300;
    explicit Rig(float W = 640, float H = 400) : sim(W, H) { Reset(); UI::SetClipboardText(""); }
    std::function<void()> Body() {
        return [&] {
            if (UI::Begin("W", 10, 10, sim.W - 20, sim.H - 20, &open)) ce.Draw("ed", w, h);
            UI::End();
        };
    }
    void Frame() { sim.Frame(Body()); }
    void Click(float x, float y) { sim.Click(x, y, Body()); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    // centre of the cell that holds the character at a position
    float CellX(CodePos p) { return ce.PosToScreen(p).x + ce.CharWidth() * 0.5f; }
    float CellY(CodePos p) { return ce.PosToScreen(p).y + ce.LineHeight() * 0.5f; }
    void Hover(CodePos p) { sim.Move(CellX(p), CellY(p)); Frame(); }
};

bool ValidUtf8(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        if (!n || i + (std::size_t)n > s.size()) return false;
        for (int k = 1; k < n; ++k) if (((unsigned char)s[i + (std::size_t)k] & 0xC0) != 0x80) return false;
        if (c == 0xE0 && (unsigned char)s[i + 1] < 0xA0) return false;       // overlong
        if (c == 0xED && (unsigned char)s[i + 1] >= 0xA0) return false;      // surrogate
        if (c == 0xF0 && (unsigned char)s[i + 1] < 0x90) return false;       // overlong
        if (c == 0xF4 && (unsigned char)s[i + 1] >= 0x90) return false;      // above U+10FFFF
        if (c < 0x20 && c != '\n' && c != '\t') return false;                  // the editor drops the other control characters
        i += (std::size_t)n;
    }
    return true;
}

// The script editor's own palette (OkayScriptPalette in editor/main.cpp) has the same colours as the widget's default, so switching
// the backend does not change how a script looks. The numbers are copied from that table, entry by entry.
void PaletteTests() {
    const UI::CodePalette p = UI::DefaultCodePalette();
    CHECK(p.text[UI::CodeColor_Default] == 0xffc6b7a9u);      // Default and Identifier
    CHECK(p.text[UI::CodeColor_Keyword] == 0xff3278ccu);      // Keyword
    CHECK(p.text[UI::CodeColor_Number] == 0xffbb9768u);       // Number
    CHECK(p.text[UI::CodeColor_String] == 0xff59876au && p.text[UI::CodeColor_Char] == 0xff59876au);   // String, CharLiteral
    CHECK(p.text[UI::CodeColor_Punct] == 0xffc8c8c8u);        // Punctuation
    CHECK(p.text[UI::CodeColor_Preproc] == 0xff808080u);      // Preprocessor
    CHECK(p.text[UI::CodeColor_Known] == 0xffaa7698u);        // KnownIdentifier
    CHECK(p.text[UI::CodeColor_Comment] == 0xff808a80u);      // Comment and MultiLineComment
    CHECK(p.background == 0xff241f1cu);                       // Background
    CHECK(p.caret == 0xffe8e6e2u);                            // Cursor
    CHECK(p.selection == 0x8c915c3au);                        // Selection
    CHECK(p.lineNumber == 0xff8a8070u);                       // (LineNumber is 0xffc4bbb0 for the current line)
    CHECK(p.lineNumberCurrent == 0xffc4bbb0u);
    CHECK(p.text[UI::CodeColor_Type] == 0xffb0c94eu);         // TypeName
    CHECK(p.text[UI::CodeColor_Function] == 0xff6dc6ffu);     // Function
    CHECK(p.text[UI::CodeColor_Member] == 0xffc49ab0u);       // Member
}

// ---------------------------------------------------------------------------------------------
// 1. Identifier docs: known-word colour and hover tooltip
// ---------------------------------------------------------------------------------------------
unsigned char ColorAt(CodeEdit& e, int line, int col) {
    std::vector<UI::CodeToken> t;
    e.LineTokens(line, t);
    for (const UI::CodeToken& k : t) if (col >= k.start && col < k.end) return k.color;
    return UI::CodeColor_Default;
}

void DocsTests() {
    CodeEdit e;
    e.SetLanguage(UI::OkayScriptLanguage());
    e.SetText("Debug.Log(\"x\")\nvar speed = Abs(5)\nif (Vector3 == Vector3)\n\tLog");
    // before: Log and Abs are function calls, Vector3 and Debug are types
    CHECK(ColorAt(e, 0, 6) == UI::CodeColor_Function);
    CHECK(ColorAt(e, 1, 12) == UI::CodeColor_Function);
    const int rev0 = e.LanguageRevision();
    e.SetIdentifierDocs({{"Log", "Log(message)\nWrites a line to the Console."}, {"Abs", "Abs(x)"}, {"if", "not a word to document"}, {"Vector3", "A 3D vector"}, {"", "empty name"}});
    CHECK(e.LanguageRevision() != rev0);
    CHECK(ColorAt(e, 0, 6) == UI::CodeColor_Known);           // Log
    CHECK(ColorAt(e, 1, 12) == UI::CodeColor_Known);          // Abs
    CHECK(ColorAt(e, 2, 0) == UI::CodeColor_Keyword);         // a keyword stays a keyword
    CHECK(ColorAt(e, 2, 4) == UI::CodeColor_Type);            // a type stays a type
    CHECK(ColorAt(e, 0, 0) == UI::CodeColor_Type);            // Debug has no doc: unchanged
    // the same docs again change nothing (no re-colouring of the whole text)
    const int rev1 = e.LanguageRevision();
    e.SetIdentifierDocs({{"Log", "Log(message)\nWrites a line to the Console."}, {"Abs", "Abs(x)"}, {"if", "not a word to document"}, {"Vector3", "A 3D vector"}, {"", "empty name"}});
    CHECK(e.LanguageRevision() == rev1);
    e.SetUserTypes({"Foo"});
    const int rev2 = e.LanguageRevision();
    CHECK(rev2 != rev1);
    e.SetUserTypes({"Foo"});
    CHECK(e.LanguageRevision() == rev2);                       // unchanged user types: nothing rebuilt
    e.SetUserTypes({"Foo", "Bar"});
    CHECK(e.LanguageRevision() != rev2);
    // docs survive user types and the other way round
    e.SetText("Bar Log");
    CHECK(ColorAt(e, 0, 0) == UI::CodeColor_Type && ColorAt(e, 0, 4) == UI::CodeColor_Known);
    // removing the docs puts the old colours back
    e.SetIdentifierDocs({});
    CHECK(ColorAt(e, 0, 4) != UI::CodeColor_Known);
    // no language: docs are kept but nothing colours
    CodeEdit raw;
    raw.SetText("Log");
    raw.SetIdentifierDocs({{"Log", "doc"}});
    CHECK(raw.IdentifierAt(-1.0f, -1.0f).empty());
}

void HoverTests() {
    Rig r;
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.ce.SetText("Debug.Log(\"hello\")\nvar speed = 5\n\tLog\nh\xC3\xA9llo = 1\nlast");
    r.ce.SetIdentifierDocs({{"Log", "Log(message)\nWrites a line to the Console."}, {"h\xC3\xA9llo", "accented"}, {"var", "docs for a keyword are still shown"}, {"speed", "the speed"}});
    r.Frames(2);
    const float cw = r.ce.CharWidth();
    // IdentifierAt: the word under a point, only on a cell that holds one of its characters
    CHECK(r.ce.IdentifierAt(r.CellX(P(0, 6)), r.CellY(P(0, 6))) == "Log");
    CHECK(r.ce.IdentifierAt(r.CellX(P(0, 7)), r.CellY(P(0, 7))) == "Log");
    CHECK(r.ce.IdentifierAt(r.CellX(P(0, 8)), r.CellY(P(0, 8))) == "Log");
    CHECK(r.ce.IdentifierAt(r.CellX(P(0, 0)), r.CellY(P(0, 0))) == "Debug");      // a word without docs is still found
    CHECK(r.ce.IdentifierAt(r.CellX(P(0, 5)), r.CellY(P(0, 5))).empty());         // the dot
    CHECK(r.ce.IdentifierAt(r.CellX(P(0, 9)), r.CellY(P(0, 9))).empty());         // the bracket
    CHECK(r.ce.IdentifierAt(r.CellX(P(0, 0)) + 100 * cw, r.CellY(P(0, 0))).empty());   // far past the end of the line
    CHECK(r.ce.IdentifierAt(r.CellX(P(0, 0)), r.CellY(P(0, 0)) - 4000.0f).empty());    // above the first line
    CHECK(r.ce.IdentifierAt(r.CellX(P(0, 0)), r.CellY(P(4, 0)) + 4000.0f).empty());    // below the last line
    CHECK(r.ce.IdentifierAt(r.ce.ViewRect().x - 3.0f, r.CellY(P(0, 0))).empty());      // in the gutter
    CHECK(r.ce.IdentifierAt(std::nanf(""), 5.0f).empty() && r.ce.IdentifierAt(5.0f, INFINITY).empty());
    // a tab fills several cells; only the character after it is a word
    CHECK(r.ce.IdentifierAt(r.ce.PosToScreen(P(2, 0)).x + cw * 0.5f, r.CellY(P(2, 0))).empty());
    CHECK(r.ce.IdentifierAt(r.ce.PosToScreen(P(2, 1)).x + cw * 0.5f, r.CellY(P(2, 1))) == "Log");
    // a multi-byte character belongs to its word
    CHECK(r.ce.IdentifierAt(r.CellX(P(3, 1)), r.CellY(P(3, 1))) == "h\xC3\xA9llo");
    // hovering shows the doc text as a tooltip, and only then
    r.Hover(P(0, 7));
    CHECK(r.ce.HoverTip() == "Log(message)\nWrites a line to the Console.");
    const Bitmap withTip = r.sim.Draw();
    r.sim.Move(r.CellX(P(0, 7)), r.CellY(P(0, 7)) + 5 * r.ce.LineHeight()); r.Frame();     // moves off the text: below the last line
    CHECK(r.ce.HoverTip().empty());
    const Bitmap noTip = r.sim.Draw();
    int differ = 0;
    for (int y = 0; y < withTip.h; ++y) for (int x = 0; x < withTip.w; ++x) if (PixelAt(withTip, x, y) != PixelAt(noTip, x, y)) ++differ;
    CHECK(differ > 200);                                                                    // a tooltip box was drawn
    r.Hover(P(0, 0)); CHECK(r.ce.HoverTip().empty());                                       // Debug: no doc
    r.Hover(P(0, 5)); CHECK(r.ce.HoverTip().empty());                                       // the dot
    r.Hover(P(3, 2)); CHECK(r.ce.HoverTip() == "accented");
    r.Hover(P(1, 1)); CHECK(r.ce.HoverTip() == "docs for a keyword are still shown");       // docs are the caller's; a keyword with docs shows them
    r.Hover(P(1, 6)); CHECK(r.ce.HoverTip() == "the speed");
    // not while blocked (another popup has the pointer), not while dragging a selection, not while composing
    r.sim.blocked = true; r.Hover(P(0, 7)); CHECK(r.ce.HoverTip().empty()); r.sim.blocked = false;
    r.Hover(P(0, 7)); CHECK(!r.ce.HoverTip().empty());
    r.sim.Down(); r.Frame(); r.Hover(P(0, 8)); CHECK(r.ce.HoverTip().empty()); r.sim.Up(); r.Frames(2);
    // an error message and a doc on the same line: both are shown, the error first
    r.ce.SetMarkers({UI::CodeMarker{0, 0, -1, "unknown name"}});
    r.Hover(P(0, 7));
    CHECK(r.ce.HoverTip() == "unknown name\n\nLog(message)\nWrites a line to the Console.");
    r.Hover(P(0, 0));
    CHECK(r.ce.HoverTip() == "unknown name");                                               // the old behaviour: the marker's message
    // a tooltip longer than the tooltip box is cut at a character boundary, never inside one
    std::string longDoc; for (int i = 0; i < 700; ++i) longDoc += "\xC3\xA9";
    r.ce.SetMarkers({});
    r.ce.SetIdentifierDocs({{"Log", longDoc}});
    r.Hover(P(0, 7));
    CHECK(!r.ce.HoverTip().empty() && r.ce.HoverTip().size() < 1024 && ValidUtf8(r.ce.HoverTip()));
    // reading only: hovering never changes the text, the caret or the undo history
    CHECK(r.ce.GetText().find("Debug.Log") == 0 && !r.ce.CanUndo());
}

// ---------------------------------------------------------------------------------------------
// 2. Proportional font: every glyph sits in its own cell
// ---------------------------------------------------------------------------------------------
struct Quad { float x0, y0, x1, y1; };

// The glyph quads (textured, in the editor's default text colour) of the frame inside the horizontal band [y0, y1). A quad is
// four consecutive vertices drawn as two triangles that share their first index.
std::vector<Quad> GlyphQuads(const UI::DrawData& d, float y0, float y1, const unsigned char rgb[3]) {
    std::vector<Quad> out;
    std::set<int> seen;
    const UI::DrawVertex* v = static_cast<const UI::DrawVertex*>(d.vertices);
    for (int c = 0; c < d.cmdCount; ++c) {
        const UI::DrawCmd& cm = d.cmds[c];
        if (!cm.texture) continue;
        for (int i = cm.idxOffset; i + 3 <= cm.idxOffset + cm.idxCount; i += 3) {
            const int lo = std::min({d.indices[i], d.indices[i + 1], d.indices[i + 2]}), hi = std::max({d.indices[i], d.indices[i + 1], d.indices[i + 2]});
            if (hi - lo != 3 || !seen.insert(lo).second) continue;
            Quad q{1e30f, 1e30f, -1e30f, -1e30f};
            bool colourOk = true;
            for (int k = 0; k < 4; ++k) {
                const UI::DrawVertex& p = v[lo + k];
                q.x0 = std::min(q.x0, p.x); q.x1 = std::max(q.x1, p.x); q.y0 = std::min(q.y0, p.y); q.y1 = std::max(q.y1, p.y);
                if (p.r != rgb[0] || p.g != rgb[1] || p.b != rgb[2]) colourOk = false;
            }
            if (colourOk && q.y0 >= y0 && q.y1 <= y1 && q.x1 > q.x0) out.push_back(q);
        }
    }
    std::sort(out.begin(), out.end(), [](const Quad& a, const Quad& b) { return a.x0 < b.x0; });
    return out;
}

void ProportionalFontTests() {
    Rig r(640, 300);
    UI::UseLauncherLook(true); UI::Style().caretBlink = false;
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.ce.SetText("iiiiiiiimmmmmmmm\nllllWWWWiiii");
    r.Frames(2);
    const float cw = r.ce.CharWidth();
    CHECK(cw >= 1.0f);
    CHECK(r.ce.CellsAreGrid());                                    // the launcher look's font is proportional: glyphs are placed cell by cell
    const UI::CodePalette pal = r.ce.Palette();
    const unsigned char rgb[3] = {(unsigned char)(pal.text[UI::CodeColor_Default] & 0xFF), (unsigned char)((pal.text[UI::CodeColor_Default] >> 8) & 0xFF),
                                  (unsigned char)((pal.text[UI::CodeColor_Default] >> 16) & 0xFF)};
    const UI::DrawData d = UI::GetDrawData();
    const float y0 = r.ce.PosToScreen(P(0, 0)).y, lh = r.ce.LineHeight();
    for (int line = 0; line < 2; ++line) {
        const std::vector<Quad> q = GlyphQuads(d, y0 + lh * (float)line - 1.0f, y0 + lh * (float)(line + 1) + 1.0f, rgb);
        const int want = line == 0 ? 16 : 12;
        CHECK((int)q.size() == want);
        if ((int)q.size() != want) continue;
        // the middle of each glyph is within a third of a cell of the middle of its cell (a glyph's ink is not exactly centred)
        for (int k = 0; k < want; ++k) {
            const float cellMid = r.ce.PosToScreen(P(line, k)).x + cw * 0.5f;
            const float mid = (q[(size_t)k].x0 + q[(size_t)k].x1) * 0.5f;
            CHECK(std::fabs(mid - cellMid) < cw * 0.34f);
        }
    }
    // the caret and a selection use the same cells: select the 16 characters of line 0 and the highlight is 16 cells wide
    r.ce.SetSelection(P(0, 0), P(0, 16));
    r.Frame();
    CHECK(std::fabs((r.ce.PosToScreen(P(0, 16)).x - r.ce.PosToScreen(P(0, 0)).x) - 16.0f * cw) < 0.01f);
    // ... and a click at the right end of the text lands on the last character
    r.Click(r.ce.PosToScreen(P(0, 15)).x + cw * 0.3f, r.ce.PosToScreen(P(0, 15)).y + lh * 0.5f);
    CHECK(r.ce.Caret() == P(0, 15));
    // a space, a tab and wide-open text keep the grid too
    r.ce.SetText("a\tb  c");
    r.Frames(2);
    const std::vector<Quad> q2 = GlyphQuads(UI::GetDrawData(), y0 - 1.0f, y0 + lh + 1.0f, rgb);
    CHECK(q2.size() == 3);
    if (q2.size() == 3) {
        CHECK(std::fabs((q2[1].x0 + q2[1].x1) * 0.5f - (r.ce.PosToScreen(P(0, 2)).x + cw * 0.5f)) < cw * 0.34f);   // b: after the tab, column 4
        CHECK(std::fabs((q2[2].x0 + q2[2].x1) * 0.5f - (r.ce.PosToScreen(P(0, 5)).x + cw * 0.5f)) < cw * 0.34f);   // c: column 7
    }
    UI::UseLauncherLook(false);
    // the classic 8x8 font is monospaced: the old run drawing stays
    Rig c(640, 300);
    c.ce.SetText("iiiiiiiimmmmmmmm");
    c.Frames(2);
    CHECK(!c.ce.CellsAreGrid());
}

// The editor's zoom is a text scale around the widget: cells, rows and the grid follow it, and so do the geometry calls.
void ZoomTests() {
    for (int look = 0; look < 2; ++look) {
        Rig r(640, 300);
        if (look) { UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
        r.ce.SetText("hello world\nsecond line");
        r.Frames(2);
        const float cw1 = r.ce.CharWidth(), lh1 = r.ce.LineHeight(), gw1 = r.ce.GutterWidth();
        // as ScriptEditorWidget::Render does it: push inside the frame, before the window the editor is drawn in
        r.sim.Frame([&] {
            UI::PushTextScale(1.5f);
            if (UI::Begin("W", 10, 10, r.sim.W - 20, r.sim.H - 20, &r.open)) r.ce.Draw("ed", r.w, r.h);
            UI::End();
            UI::PopTextScale();
        });
        CHECK(r.ce.CharWidth() > cw1 * 1.2f && r.ce.CharWidth() < cw1 * 1.8f);
        CHECK(r.ce.LineHeight() > lh1 * 1.2f && r.ce.LineHeight() < lh1 * 1.8f);
        CHECK(r.ce.GutterWidth() > gw1 * 1.2f);
        // the cell grid is still the cell grid: 11 characters span 11 cells
        CHECK(std::fabs((r.ce.PosToScreen(P(0, 11)).x - r.ce.PosToScreen(P(0, 0)).x) - 11.0f * r.ce.CharWidth()) < 0.01f);
        // the scale does not creep: the push-before-Begin, pop-after-End pattern every frame leaves the same size every frame (it used to
        // multiply the text scale by the zoom once per frame, because End() did not forget the window's base scale), and a frame
        // without the push is back to the normal size
        const float cwZoomed = r.ce.CharWidth();
        for (int i = 0; i < 6; ++i) {
            r.sim.Frame([&] {
                UI::PushTextScale(1.5f);
                if (UI::Begin("W", 10, 10, r.sim.W - 20, r.sim.H - 20, &r.open)) r.ce.Draw("ed", r.w, r.h);
                UI::End();
                UI::PopTextScale();
            });
            CHECK(std::fabs(r.ce.CharWidth() - cwZoomed) < 0.01f);
        }
        r.Frame(); r.Frame();
        CHECK(std::fabs(r.ce.CharWidth() - cw1) < 0.01f);
        UI::UseLauncherLook(false);
    }
}

// An error's message shown dimmed after the end of its line (the script editor draws these itself with ImGui, where the OkayUI editor
// would cover them): off by default, a switch of the widget.
void InlineMessageTests() {
    auto render = [](bool inlineOn, const std::string& msg, int& lineEndX, int& rowY0, int& rowY1, float& charW) {
        Rig r(640, 300);
        r.ce.SetText("var x = 1;\nfoo(bar);\nlast line");
        r.ce.SetMarkers({UI::CodeMarker{1, 0, -1, msg}});
        r.ce.SetInlineMessages(inlineOn);
        r.Frames(2);
        const UI::Vec2 end = r.ce.PosToScreen(P(1, (int)r.ce.Line(1).size()));
        lineEndX = (int)end.x; rowY0 = (int)end.y; rowY1 = (int)(end.y + r.ce.LineHeight()); charW = r.ce.CharWidth();
        r.sim.Move(1, 1); r.Frame();                                         // the pointer is away: no tooltip
        return r.sim.Draw();
    };
    int ex = 0, y0 = 0, y1 = 0; float cw = 0;
    const Bitmap off = render(false, "unknown name 'foo'", ex, y0, y1, cw);
    const Bitmap on = render(true, "unknown name 'foo'", ex, y0, y1, cw);
    int rightDiff = 0, leftDiff = 0;
    for (int y = y0; y < y1; ++y) for (int x = 0; x < on.w; ++x) if (PixelAt(on, x, y) != PixelAt(off, x, y)) (x > ex + (int)cw ? rightDiff : leftDiff)++;
    CHECK(rightDiff > 80);                                                   // text appeared after the end of the line
    CHECK(leftDiff == 0);                                                    // and nothing moved before it
    int otherRows = 0;
    for (int y = 0; y < on.h; ++y) if (y < y0 || y >= y1) for (int x = 0; x < on.w; ++x) if (PixelAt(on, x, y) != PixelAt(off, x, y)) ++otherRows;
    CHECK(otherRows == 0);                                                   // only that row changed
    // a long, multi-line message is shortened to one line (about 90 characters) and never leaves the view
    int ex2 = 0, a2 = 0, b2 = 0; float cw2 = 0;
    const std::string longMsg = "first line\nsecond line " + std::string(400, 'w');
    const Bitmap longOn = render(true, longMsg, ex2, a2, b2, cw2);
    int lastX = 0;
    for (int y = a2; y < b2; ++y) for (int x = 0; x < longOn.w; ++x) if (PixelAt(longOn, x, y) != PixelAt(off, x, y)) lastX = std::max(lastX, x);
    CHECK(lastX > ex2 + (int)(10 * cw2) && lastX < ex2 + (int)(100 * cw2));   // 90 characters plus the mark, not 400
    // an empty message draws nothing extra; markers without the switch never draw text (the default)
    const Bitmap empty = render(true, "", ex2, a2, b2, cw2);
    int emptyDiff = 0;
    for (int y = a2; y < b2; ++y) for (int x = ex2 + (int)cw2; x < empty.w; ++x) if (PixelAt(empty, x, y) != PixelAt(off, x, y)) ++emptyDiff;
    CHECK(emptyDiff == 0);
}

// ---------------------------------------------------------------------------------------------
// 3. The clipboard is not 4 KB any more
// ---------------------------------------------------------------------------------------------
void ClipboardTests() {
    // the plain clipboard holds much more than a text field does
    std::string big;
    for (int i = 0; i < 20000; ++i) big += "line " + std::to_string(i) + " \xC3\xA9\n";
    CHECK(big.size() > 4096 * 4);
    UI::SetClipboardText(big.c_str());
    CHECK(std::string(UI::GetClipboardText()) == big);
    CHECK(UI::kClipboardStoreMax >= (std::size_t)16 << 20 && UI::kClipboardMax == 4096);
    // CodeEdit copy, cut and paste of a selection of 300 KB
    CodeEdit e;
    e.SetText(big);
    e.SelectAll();
    CHECK(e.Copy() && std::string(UI::GetClipboardText()) == e.GetText());
    UI::SetClipboardText("");
    const std::string before = e.GetText();
    CHECK(e.Cut() && e.IsEmpty() && std::string(UI::GetClipboardText()) == before);
    CHECK(e.Paste() && e.GetText() == before);
    CHECK(e.Undo() && e.IsEmpty());                          // paste and cut are one undo step each
    CHECK(e.Undo() && e.GetText() == before);
    CHECK(e.Redo() && e.IsEmpty() && e.Redo() && e.GetText() == before);
    // text pasted into a text field is still limited by the field, not by the clipboard (see test_okayui_values)
    // a clipboard bigger than the store is cut at a character boundary; Copy of a bigger selection copies what fits, Cut does nothing
    std::string huge;
    const std::size_t kLimit = UI::kClipboardStoreMax;
    huge.reserve(kLimit + 1024);
    while (huge.size() < kLimit + 100) huge += "\xC3\xA9";               // e-acute, 2 bytes: the limit (16 MiB - 1 with the end mark) is odd
    UI::SetClipboardText(huge.c_str());
    const std::string got = UI::GetClipboardText();
    CHECK(got.size() == kLimit - 2);                                       // one byte too many would split a character: cut before it
    CHECK(huge.compare(0, got.size(), got) == 0);
    CodeEdit h;
    h.SetLimits((std::size_t)-1, (std::size_t)-1);                        // the editor's own size limit is lifted: it is the clipboard's that is tested
    h.SetText(huge);
    h.SelectAll();
    CHECK(h.Copy());
    CHECK(std::string(UI::GetClipboardText()).size() == got.size());
    UI::SetClipboardText("keep");
    const std::string hugeBefore = h.GetText();
    CHECK(!h.Cut());                                                      // never delete what the clipboard could not take
    CHECK(h.GetText() == hugeBefore && std::string(UI::GetClipboardText()) == "keep");
    // a paste of the biggest clipboard works and is valid text
    UI::SetClipboardText(huge.c_str());
    CodeEdit p;
    p.SetLimits((std::size_t)-1, (std::size_t)-1);
    CHECK(p.Paste() && p.GetText().size() == got.size() && ValidUtf8(p.GetText()));
    // invalid UTF-8 and control characters in the clipboard are repaired like typed text
    UI::SetClipboardText("ok\xff\xfe\xc3(\x01\x7f z\xed\xa0\x80 end");
    CodeEdit q;
    CHECK(q.Paste() && ValidUtf8(q.GetText()) && q.GetText().find("ok") == 0 && q.GetText().find("end") != std::string::npos);
    UI::SetClipboardText("");
}

// ---------------------------------------------------------------------------------------------
// 4. Hostile input
// ---------------------------------------------------------------------------------------------
// A paste or a load that would make the text absurdly big is refused (a paste) or cut (a load), whatever the clipboard holds. Before
// this, a 16 MiB paste of newlines made about 16.7 million lines and a gigabyte of temporary memory.
void SizeLimitTests() {
    CHECK(CodeEdit::kDefaultMaxBytes == (4u << 20) && CodeEdit::kDefaultMaxLines == 200000);
    {   // a paste of newlines only: 300,000 lines is over the default limit of 200,000 lines
        CodeEdit e;
        CHECK(e.MaxBytes() == CodeEdit::kDefaultMaxBytes && e.MaxLines() == CodeEdit::kDefaultMaxLines && e.Status().empty());
        e.SetText("keep\nme");
        e.SetSelection(P(0, 1), P(1, 1));
        UI::SetClipboardText(std::string(300000, '\n').c_str());
        CHECK(!e.Paste());
        CHECK(e.GetText() == "keep\nme" && e.LineCount() == 2);          // nothing changed
        CHECK(e.Anchor() == P(0, 1) && e.Caret() == P(1, 1));            // and the selection was not collapsed
        CHECK(!e.CanUndo());
        CHECK(e.Status().find("limit") != std::string::npos && e.Status().find("200,000 lines") != std::string::npos && e.Status().find("4 MiB") != std::string::npos);
        CHECK(e.InsertText("!") && e.Status().empty());                  // the message goes away with the next change
        UI::SetClipboardText("");
    }
    {   // the biggest clipboard there is (16 MiB) of newlines: refused at once, no 16.7 million lines and no gigabyte of memory
        UI::SetClipboardText(std::string(UI::kClipboardStoreMax, '\n').c_str());
        CodeEdit e;
        e.SetText("abc");
        e.SetCaret(P(0, 3));
        CHECK(!e.Paste() && e.GetText() == "abc" && e.LineCount() == 1 && At(e, 0, 3) && !e.CanUndo());
        CHECK(e.Status().find("limit") != std::string::npos);
        // 3 MiB of newlines is under the byte limit and still over the line limit
        UI::SetClipboardText(std::string(3u << 20, '\n').c_str());
        CHECK(!e.Paste() && e.LineCount() == 1);
        // and the same text typed or inserted through the other doors
        CHECK(!e.InsertText(std::string(300000, '\n').c_str()) && e.LineCount() == 1);
        UI::SetClipboardText("");
    }
    {   // text over the byte limit that has few lines: refused too
        CodeEdit e;
        UI::SetClipboardText(std::string((4u << 20) + 1, 'x').c_str());
        CHECK(!e.Paste() && e.IsEmpty() && !e.Status().empty());
        UI::SetClipboardText(std::string(4u << 20, 'x').c_str());       // exactly the limit fits
        CHECK(e.Paste() && e.GetText().size() == (4u << 20) && e.Status().empty());
        CHECK(!e.InsertText("y") && e.GetText().size() == (4u << 20));  // and now one more byte is over it
        CHECK(e.Backspace() && e.InsertText("y") && e.GetText().size() == (4u << 20));
        UI::SetClipboardText("");
    }
    {   // limits you set: bytes, lines, every way in, and every refusal leaves the text, the caret, the selection and the undo log alone
        CodeEdit e;
        e.SetLimits(40, 4);
        CHECK(e.MaxBytes() == 40 && e.MaxLines() == 4);
        e.SetText("one\ntwo\nthree");
        e.SetSelection(P(0, 1), P(0, 2));
        UI::SetClipboardText("a\nb\nc\nd\ne");
        CHECK(!e.Paste() && e.GetText() == "one\ntwo\nthree" && e.Anchor() == P(0, 1) && e.Caret() == P(0, 2) && !e.CanUndo());
        CHECK(e.InsertText("x\ny") && e.GetText() == "ox\nye\ntwo\nthree" && e.LineCount() == 4);   // replaces the selection: 4 lines is the limit
        CHECK(!e.NewLine() && e.LineCount() == 4 && !e.Status().empty());      // Enter would make a fifth
        CHECK(!e.InsertText("\n") && e.GetText() == "ox\nye\ntwo\nthree");
        CHECK(e.InsertText("z") && e.GetText() == "ox\nyze\ntwo\nthree");     // no new line: fine
        CHECK(e.Undo() && e.Undo() && e.GetText() == "one\ntwo\nthree");
        e.SetSelection(P(0, 0), P(2, 5));                                       // a selection that spans every line: its lines are given back
        CHECK(e.InsertText("a\nb\nc\nd") && e.LineCount() == 4);
        CHECK(e.TypeText(std::string(40, 'q').c_str()) == false && e.GetText() == "a\nb\nc\nd");   // over the byte limit
        e.SetText("");
        CHECK(e.InsertText(std::string(40, 'x').c_str()) && !e.InsertText("y") && e.GetText().size() == 40);
        e.SetFind("x");
        CHECK(e.ReplaceAll("xx") == 0 && e.GetText().size() == 40 && !e.Status().empty());   // replace all that would go over: refused, and says so
        CHECK(e.ReplaceAll("") > 0 && e.IsEmpty() && e.Status().empty());      // shrinking is always fine
        e.SetLimits(10, 0);                                                     // 0 lines means 1
        CHECK(e.MaxLines() == 1 && !e.InsertText("a\nb") && e.InsertText("ab"));
        e.SetLimits((std::size_t)-1, (std::size_t)-1);
        CHECK(e.InsertText("c\nd\ne") && e.LineCount() == 3);                  // no limit at all
        e.SetLimits(2, 2);                                                      // a text that is already over a new limit stays; growing is refused
        CHECK(e.LineCount() == 3 && !e.InsertText("z") && e.Backspace() && e.Backspace());
        UI::SetClipboardText("");
    }
    {   // SetText over the limits: cut to the longest start that fits, at a line break, and read-only so the cut copy can never be saved
        std::string many;
        for (int i = 0; i < 300000; ++i) many += "x\n";
        CodeEdit e;
        e.SetText(many);
        CHECK(e.LineCount() == 200000 && e.Truncated() && e.ReadOnly() && e.GetText().size() == 2 * 199999 + 1);
        CHECK(e.Status().find("only the start is shown") != std::string::npos && e.Status().find("200,000 lines") != std::string::npos);
        CHECK(!e.InsertText("a") && !e.Backspace() && !e.DeleteForward() && !e.NewLine() && !e.Tab() && !e.TypeText("a") && !e.Cut() && !e.Paste());
        e.SelectAll(); CHECK(e.Copy());                                         // looking and copying still work
        e.SetReadOnly(false); CHECK(!e.InsertText("a") && e.ReadOnly());       // and it cannot be lifted
        e.SetText("short");                                                     // the next SetText is a fresh start
        CHECK(!e.Truncated() && !e.ReadOnly() && e.Status().empty() && e.InsertText("!") && e.GetText() == "!short");
        // CRLF and a lone CR are line breaks too, and the cut is before the break that would make one line too many
        std::string crlf, cr;
        for (int i = 0; i < 300000; ++i) { crlf += "ab\r\n"; cr += "ab\r"; }
        e.SetText(crlf);
        CHECK(e.LineCount() == 200000 && e.Truncated() && e.Line(199999) == "ab" && e.GetText().find('\r') == std::string::npos);
        e.SetText(cr);
        CHECK(e.LineCount() == 200000 && e.Truncated() && e.Line(199998) == "ab");
        // a CRLF split by the byte limit does not leave a stray break
        CodeEdit f;
        f.SetLimits(5, 100);
        f.SetText("abcd\r\nxyz");
        CHECK(f.Truncated() && f.GetText() == "abcd" + std::string(f.LineCount() == 2 ? "\n" : ""));
        CHECK(f.LineCount() <= 2);
        // one huge line: cut at the byte limit on a character boundary (here the limit falls in the middle of an e-acute)
        std::string wide = "a";
        while (wide.size() < (4u << 20) + 100) wide += "\xC3\xA9";
        CodeEdit g;
        g.SetText(wide);
        CHECK(g.Truncated() && g.LineCount() == 1 && g.GetText().size() == (4u << 20) - 1 && ValidUtf8(g.GetText()));
        // under both limits: not cut, not read-only
        CodeEdit h;
        h.SetText(std::string(4u << 20, 'x'));
        CHECK(!h.Truncated() && !h.ReadOnly() && h.GetText().size() == (4u << 20));
        // limits of zero cut everything and say so
        CodeEdit z;
        z.SetLimits(0, 1);
        z.SetText("abc");
        CHECK(z.Truncated() && z.IsEmpty() && z.ReadOnly());
    }
    {   // the multi-line text field (InputTextMultiline) is built on CodeEdit: a normal buffer is unaffected by the limits
        Rig r(640, 400);
        r.ce.SetText("small text");
        CHECK(!r.ce.Truncated() && r.ce.InsertText("!") && r.ce.GetText() == "!small text");
    }
}

void NanAndExtremeTests() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    Rig r(640, 400);
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    std::string t;
    for (int i = 0; i < 400; ++i) t += "    var line" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    r.ce.SetText(t);
    r.ce.SetBreakpoints({0, 5, INT_MAX});
    r.ce.SetMarkers({UI::CodeMarker{3, 0, -1, "x"}, UI::CodeMarker{INT_MAX, INT_MAX, INT_MIN, "far"}, UI::CodeMarker{-5, -5, 99999999, "neg"}});
    r.Frames(2);
    const float vals[] = {nan, inf, -inf, 1e30f, -1e30f, 3.0e9f, -3.0e9f, 0.0f, -0.0f, 1e-30f, std::numeric_limits<float>::max()};
    for (float a : vals) for (float b : vals) {
        r.ce.SetScroll(a, b);
        r.Frame();
        CHECK(std::isfinite(r.ce.ScrollX()) && std::isfinite(r.ce.ScrollY()) && r.ce.ScrollX() >= 0 && r.ce.ScrollY() >= 0);
        CHECK(r.ce.FirstVisibleLine() >= 0 && r.ce.FirstVisibleLine() < r.ce.LineCount());
        const CodePos s = r.ce.ScreenToPos(a, b);
        CHECK(s.line >= 0 && s.line < r.ce.LineCount() && s.col >= 0 && s.col <= r.ce.LineLength(s.line));
        const std::string w = r.ce.IdentifierAt(a, b);
        (void)w;
        r.sim.Move(a, b); r.Frame();                                  // the pointer is anywhere, including nowhere
        r.sim.Down(); r.Frame(); r.sim.Move(b, a); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(r.ce.Caret().line >= 0 && r.ce.Caret().line < r.ce.LineCount());
    }
    // the wheel
    for (float a : vals) { r.sim.wheel = a; r.Frame(); CHECK(std::isfinite(r.ce.ScrollY())); }
    // lines and columns
    const int ints[] = {INT_MIN, INT_MIN + 1, -1, 0, 1, 399, 400, 401, INT_MAX - 1, INT_MAX};
    for (int a : ints) {
        r.ce.GoToLine(a);
        r.Frame();
        CHECK(r.ce.Caret().line >= 0 && r.ce.Caret().line < r.ce.LineCount());
        for (int b : ints) {
            r.ce.SetCaret(P(a, b));
            const CodePos c = r.ce.Caret();
            CHECK(c.line >= 0 && c.line < r.ce.LineCount() && c.col >= 0 && c.col <= r.ce.LineLength(c.line));
            r.ce.SetSelection(P(a, b), P(b, a));
            const UI::Vec2 sp = r.ce.PosToScreen(P(a, b));
            CHECK(std::isfinite(sp.x) && std::isfinite(sp.y));
            CHECK(r.ce.ByteToColumn(a, b) >= 0);
            const int cb = r.ce.ColumnToByte(a, (float)b);
            CHECK(cb >= 0 && cb <= r.ce.LineLength(std::min(std::max(a, 0), r.ce.LineCount() - 1)));
            r.ce.SelectLine(a); r.ce.SelectWordAt(P(a, b));
            r.Frame();
        }
    }
    for (float a : vals) { CHECK(r.ce.ColumnToByte(0, a) >= 0); }
    r.ce.SetFind("var", true);
    CHECK(r.ce.FindNext() && r.ce.FindPrev() && r.ce.FindCount() == 400);
    CHECK(r.ce.ReplaceAll("v") == 400 && r.ce.Undo());
    // sizes of the widget itself
    for (float a : vals) {
        r.w = a; r.h = a; r.Frame();
        r.w = 500; r.h = 300; r.Frame();
    }
}

void JunkTextTests() {
    // junk through every way text comes in: SetText, InsertText, paste, typed text, IME commit and preedit
    std::string junk;
    for (int i = 0; i < 4000; ++i) junk.push_back((char)((i * 131 + (i >> 3) * 17) & 0xFF));
    junk += std::string("\x00\x00", 2);
    Rig r(640, 300);
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.ce.SetText(junk);
    CHECK(ValidUtf8(r.ce.GetText()));
    r.ce.InsertText(junk.c_str());
    CHECK(ValidUtf8(r.ce.GetText()));
    UI::SetClipboardText(junk.c_str());
    r.ce.Paste();
    CHECK(ValidUtf8(r.ce.GetText()));
    r.Frames(2);
    r.ce.Focus(); r.Frames(2);
    r.sim.Type(junk); r.Frame();
    CHECK(ValidUtf8(r.ce.GetText()));
    r.sim.Commit(junk); r.Frame();
    CHECK(ValidUtf8(r.ce.GetText()));
    r.sim.Compose(junk, 7); r.Frames(2);
    r.sim.Compose(std::string(100000, '\xE3'), 99999); r.Frames(2);
    r.sim.EndCompose(); r.Frames(2);
    // every byte value as a lone input
    for (int b = 0; b < 256; ++b) {
        const char s[2] = {(char)b, 0};
        r.ce.InsertText(s);
        r.sim.Type(std::string(s)); r.Frame();
    }
    CHECK(ValidUtf8(r.ce.GetText()));
    // undo everything and redo everything: the text is valid in between
    int guard = 0;
    while (r.ce.Undo() && ++guard < 100000) {}
    CHECK(r.ce.GetText().empty() || ValidUtf8(r.ce.GetText()));
    guard = 0;
    while (r.ce.Redo() && ++guard < 100000) {}
    CHECK(ValidUtf8(r.ce.GetText()));
    // the first line numbers of the Console's text can hold NUL: SetText with embedded NULs keeps the text after them
    r.ce.SetText(std::string("a\0b\nc", 5));
    CHECK(r.ce.GetText() == "ab\nc");
}

void LongLineTests() {
    // one line of 4 MB: every operation a user can do on it must stay usable
    std::string line;
    line.reserve(4u << 20);
    for (std::size_t i = 0; line.size() < (4u << 20); ++i) line += "x" + std::to_string(i % 97) + " + ";
    Rig r(800, 400);
    r.ce.SetLimits((std::size_t)-1, (std::size_t)-1);                     // 4 MB is a hair over the default limit of 4 MiB: lifted, it is the speed that is tested
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.w = 760; r.h = 340;
    auto t0 = std::chrono::steady_clock::now();
    r.ce.SetText(line);
    const double tSet = Ms(t0);
    CHECK(r.ce.LineCount() == 1);
    t0 = std::chrono::steady_clock::now();
    r.Frame();
    const double tFirst = Ms(t0);
    t0 = std::chrono::steady_clock::now();
    const int kFrames = 10;
    for (int i = 0; i < kFrames; ++i) r.Frame();
    const double tSteady = Ms(t0) / kFrames;
    // scroll to the far right end and edit there
    r.ce.SetCaret(P(0, (int)line.size()));
    t0 = std::chrono::steady_clock::now();
    r.Frame();
    const double tEnd = Ms(t0);
    CHECK(r.ce.ScrollX() > 1000.0f);
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) { r.ce.InsertText("y"); r.Frame(); }
    const double tType = Ms(t0) / 5;
    t0 = std::chrono::steady_clock::now();
    r.ce.SetFind("x5 + ", true);
    const int cnt = r.ce.FindCount();
    const double tFind = Ms(t0);
    CHECK(cnt > 1000);
    t0 = std::chrono::steady_clock::now();
    r.ce.SetCaret(P(0, 2000000)); r.ce.Move(UI::CodeMove_WordLeft); r.ce.Move(UI::CodeMove_Left, true); r.Frame();
    const double tMove = Ms(t0);
    t0 = std::chrono::steady_clock::now();
    r.ce.SelectAll(); r.Frame();
    const double tSelAll = Ms(t0);
    CHECK(r.ce.Undo());
    std::cout << "  one 4 MB line: SetText " << tSet << " ms, first frame " << tFirst << " ms, steady frame " << tSteady << " ms, jump to the end " << tEnd
              << " ms, type+frame " << tType << " ms, find-count " << tFind << " ms, moves " << tMove << " ms, select all + frame " << tSelAll << " ms\n";
    CHECK(tSet < 3000 * kSlack);
    CHECK(tFirst < 3000 * kSlack);
    CHECK(tSteady < 150 * kSlack);          // far from smooth, but a frame is not seconds
    CHECK(tEnd < 3000 * kSlack);
    CHECK(tType < 1500 * kSlack);
    CHECK(tFind < 3000 * kSlack);
    CHECK(tMove < 1500 * kSlack);
    CHECK(tSelAll < 1500 * kSlack);
    // a line that is nothing but one huge token (an unterminated string) and one that is all tabs
    r.ce.SetText("\"" + std::string(2u << 20, 'a'));
    r.Frames(2);
    r.ce.SetText(std::string(1u << 20, '\t'));
    r.Frames(2);
    CHECK(r.ce.LineCount() == 1);
}

void ManyLinesTests() {
    // one million lines (about 8 MB)
    std::string t;
    t.reserve(9u << 20);
    for (int i = 0; i < 1000000; ++i) { t += "v"; t += std::to_string(i); t += "\n"; }
    Rig r(800, 400);
    r.ce.SetLimits((std::size_t)-1, (std::size_t)-1);                     // a million lines is over the default limits: lifted, it is the speed that is tested
    r.ce.SetLanguage(UI::OkayScriptLanguage());
    r.w = 760; r.h = 340;
    auto t0 = std::chrono::steady_clock::now();
    r.ce.SetText(t);
    const double tSet = Ms(t0);
    CHECK(r.ce.LineCount() == 1000001);
    t0 = std::chrono::steady_clock::now();
    r.Frame();
    const double tFirst = Ms(t0);
    t0 = std::chrono::steady_clock::now();
    r.ce.SetCaret(P(999999, 0)); r.Frame();
    const double tEnd = Ms(t0);
    CHECK(r.ce.FirstVisibleLine() > 900000);
    t0 = std::chrono::steady_clock::now();
    const int kFrames = 20;
    for (int i = 0; i < kFrames; ++i) r.Frame();
    const double tSteady = Ms(t0) / kFrames;
    r.ce.GoToLine(500000); r.Frame();
    CHECK(r.ce.FirstVisibleLine() > 499900 && r.ce.FirstVisibleLine() <= 500000);
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) { r.ce.InsertText("x"); r.ce.InsertText("\n"); r.Frame(); }
    const double tEdit = Ms(t0) / 10;
    r.ce.SetBreakpoints({0, 500000, 999999});
    r.ce.SetMarkers({UI::CodeMarker{12345, 0, -1, "m"}});
    t0 = std::chrono::steady_clock::now();
    r.ce.SetCaret(P(0, 0)); r.ce.InsertText("\n\n\n"); r.Frame();
    const double tTop = Ms(t0);
    // breakpoints below the insert move down by three lines; one on the line the text was inserted at (line 0, column 0) may stay or move
    CHECK(r.ce.HasBreakpoint(500003) && r.ce.HasBreakpoint(999999 + 3) && (r.ce.HasBreakpoint(0) || r.ce.HasBreakpoint(3)));
    CHECK(r.ce.Markers().size() == 1 && r.ce.Markers()[0].line == 12348);
    t0 = std::chrono::steady_clock::now();
    r.ce.SelectAll(); r.ce.InsertText(""); r.Frame();
    CHECK(r.ce.LineCount() == 1);
    CHECK(r.ce.Undo() && r.ce.LineCount() > 1000000);
    const double tAll = Ms(t0);
    t0 = std::chrono::steady_clock::now();
    r.ce.SetFind("v99", true);
    const int cnt = r.ce.FindCount();
    const double tFind = Ms(t0);
    CHECK(cnt > 10000);
    std::cout << "  1M lines (" << t.size() / 1024 << " KiB): SetText " << tSet << " ms, first frame " << tFirst << " ms, jump to end " << tEnd
              << " ms, steady frame " << tSteady << " ms, edit+frame in the middle " << tEdit << " ms, insert lines at the top (marks move) " << tTop
              << " ms, delete all + undo " << tAll << " ms, find-count " << tFind << " ms\n";
    CHECK(tSet < 6000 * kSlack);
    CHECK(tFirst < 1500 * kSlack);
    CHECK(tEnd < 6000 * kSlack);
    CHECK(tSteady < 30 * kSlack);
    CHECK(tEdit < 400 * kSlack);
    CHECK(tTop < 1500 * kSlack);
    CHECK(tAll < 8000 * kSlack);
    CHECK(tFind < 3000 * kSlack);
}

} // namespace

int main() {
    RUN_SUITE("okayui codeedit parity");
    PaletteTests();
    DocsTests();
    HoverTests();
    ProportionalFontTests();
    ZoomTests();
    InlineMessageTests();
    ClipboardTests();
    SizeLimitTests();
    NanAndExtremeTests();
    JunkTextTests();
    LongLineTests();
    ManyLinesTests();
    TEST_MAIN_RESULT();
}
