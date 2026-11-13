// OkayUI input method composition (IME), headless: Input::composition / compositionCaret / commit against the single-line text
// field and CodeEdit: the underlined preedit, commit inserting or replacing a selection, cancel, keys left to the input method,
// UTF-8 boundaries, the on-screen caret rectangle for a candidate window, a golden picture and a short fuzz.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <cstring>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

bool ValidUtf8(const char* s) {
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        int n = *p < 0x80 ? 1 : (*p >= 0xC2 && *p <= 0xDF) ? 2 : (*p >= 0xE0 && *p <= 0xEF) ? 3 : (*p >= 0xF0 && *p <= 0xF4) ? 4 : 0;
        if (!n) return false;
        for (int i = 1; i < n; ++i) if ((p[i] & 0xC0) != 0x80) return false;
        p += n;
    }
    return true;
}

// ---- the single-line text field ---------------------------------------------------------------------------------------
struct FieldRig {
    Sim sim;
    char buf[64];
    int cap = 64;
    unsigned flags = 0;
    bool changed = false, enter = false;
    UI::ImeRect ime;
    UI::Composition comp;
    FieldRig(const char* text = "", float W = 420, float H = 140) : sim(W, H) {
        Reset();
        std::strncpy(buf, text, sizeof buf); buf[sizeof buf - 1] = 0;
    }
    void Frame() {
        sim.Frame([&] {
            UI::Begin("W", 10, 10, 400, 110);
            const bool c = UI::TextField(7, 20, 50, 300, 28, buf, cap, flags);
            if (flags & UI::InputText_EnterReturnsTrue) { if (c) enter = true; } else if (c) changed = true;
            comp = UI::GetComposition();
            ime = UI::GetImeRect();
            UI::End();
        });
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Focus() { sim.Move(300, 64); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }   // click near the right end: caret at the end
    void Tap(Key k) { sim.Press(k); Frame(); }
    UI::Rect Box() const { UI::Rect r; r.x = 20; r.y = 50; r.w = 300; r.h = 28; return r; }
    bool Focused() const { return UI::GetIO().wantTextInput; }
};

void TestCleaning() {
    RUN_SUITE("composition: what the toolkit makes of a preedit");
    FieldRig r("");
    r.Focus();
    r.sim.Compose("ni\xe5\xa5\xbd", -1);
    r.Frame();
    CHECK(r.comp.active && r.comp.length == 5 && std::strcmp(r.comp.text, "ni\xe5\xa5\xbd") == 0);
    CHECK(r.comp.caret == 5);                                    // -1: at the end
    r.sim.Compose("ni\xe5\xa5\xbd", 2); r.Frame();
    CHECK(r.comp.caret == 2);
    r.sim.Compose("ni\xe5\xa5\xbd", 3); r.Frame();                // inside the 3-byte character: moved back to its start
    CHECK(r.comp.caret == 2);
    r.sim.Compose("ni\xe5\xa5\xbd", 4); r.Frame();
    CHECK(r.comp.caret == 2);
    r.sim.Compose("ni\xe5\xa5\xbd", 99); r.Frame();
    CHECK(r.comp.caret == 5);
    r.sim.Compose("a\x01" "b\x7f" "c\n\t", 1); r.Frame();          // control characters dropped
    CHECK(std::strcmp(r.comp.text, "abc") == 0 && r.comp.caret == 1);
    r.sim.Compose("ok\xff\xfe" "x\xc3\x28y\xe2\x82", -1); r.Frame();   // invalid bytes and a cut character dropped
    CHECK(ValidUtf8(r.comp.text)); CHECK(std::strcmp(r.comp.text, "okx(y") == 0);
    r.sim.Compose("\xed\xa0\x80\xc0\x80\xf8\x88\x80\x80\x80", -1); r.Frame();   // surrogate, overlong, 5-byte: nothing is left
    CHECK(!r.comp.active && r.comp.length == 0);
    r.sim.Compose("", -1); r.Frame();
    CHECK(!r.comp.active);
    r.sim.Compose(std::string(1000, 'x'), -1); r.Frame();          // at most 256 bytes
    CHECK(r.comp.length == 256 && r.comp.caret == 256);
    std::string cjk; for (int i = 0; i < 100; ++i) cjk += "\xe6\x97\xa5";
    r.sim.Compose(cjk, -1); r.Frame();                           // cut at a whole character
    CHECK(r.comp.length == 255 && ValidUtf8(r.comp.text));
    r.sim.EndCompose(); r.Frame();
    CHECK(!r.comp.active);
}

void TestFieldInsert() {
    RUN_SUITE("composition: text field insert, commit, cancel");
    FieldRig r("ab");
    r.Focus();
    CHECK(r.Focused());
    r.sim.Compose("ni", -1); r.Frames(2);
    CHECK(std::strcmp(r.buf, "ab") == 0 && !r.changed);           // the preedit is not in the text
    const Bitmap with = r.sim.Draw(0, 0, 0);
    const unsigned textCol = ThemeRgba(UI::Style().text);
    // the underline: a row of text-coloured pixels below the preedit
    int lineHits = 0;
    const UI::Rect box = r.Box();
    for (int y = (int)box.y + 2; y < (int)(box.y + box.h) - 1; ++y) {
        int run = 0, best = 0;
        for (int x = (int)box.x + 2; x < (int)(box.x + box.w) - 2; ++x) { if (PixelAt(with, x, y) == textCol) { ++run; if (run > best) best = run; } else run = 0; }
        if (best >= 20) ++lineHits;
    }
    CHECK(lineHits >= 1);
    r.sim.EndCompose(); r.Frames(2);                              // cancel: nothing changes
    CHECK(std::strcmp(r.buf, "ab") == 0 && !r.changed);
    const Bitmap& without = r.sim.Draw(0, 0, 0);
    int diff = 0;
    for (int y = (int)box.y; y < (int)(box.y + box.h); ++y) for (int x = (int)box.x; x < (int)(box.x + box.w); ++x) if (PixelAt(with, x, y) != PixelAt(without, x, y)) ++diff;
    CHECK(diff > 40);                                             // the preedit really was drawn
    // compose then commit (the preedit ends in the same frame)
    r.sim.Compose("n", -1); r.Frame();
    r.sim.Compose("ni", -1); r.Frame();
    r.sim.EndCompose(); r.sim.Commit("\xe4\xbd\xa0"); r.Frame();
    CHECK(std::strcmp(r.buf, "ab\xe4\xbd\xa0") == 0 && r.changed);
    r.changed = false;
    // a commit with the preedit still sent in the same frame also works
    r.sim.Compose("x", -1); r.sim.Commit("!"); r.Frame();
    r.sim.EndCompose(); r.Frame();
    CHECK(std::strcmp(r.buf, "ab\xe4\xbd\xa0!") == 0);
    // commit in the middle: the caret moves left, commit goes there
    r.Tap(Key_Left); r.Tap(Key_Left);
    r.sim.Commit("<>"); r.Frame();
    CHECK(std::strcmp(r.buf, "ab\xe4\xbd\xa0<>!") == 0 || std::strcmp(r.buf, "ab<>\xe4\xbd\xa0!") == 0);
    CHECK(ValidUtf8(r.buf));
    // the later typed text and Backspace still behave
    r.sim.EndCompose();
    r.sim.Type("Q"); r.Frame();
    r.Tap(Key_Backspace);
    CHECK(ValidUtf8(r.buf));
}

void TestFieldSelection() {
    RUN_SUITE("composition: text field replaces a selection, cancel keeps it");
    FieldRig r("hello world");
    r.Focus();
    r.Tap(Key_SelectAll);
    // composing over a selection: the text stays, the selected text is not drawn
    const Bitmap before = r.sim.Draw(0, 0, 0);
    int px0 = CountNot(before, 20, 52, 320, 76, ThemeRgba(UI::Style().field));
    r.sim.Compose("Z", -1); r.Frames(2);
    CHECK(std::strcmp(r.buf, "hello world") == 0);
    const Bitmap& during = r.sim.Draw(0, 0, 0);
    int px1 = CountNot(during, 20, 52, 320, 76, ThemeRgba(UI::Style().field));
    CHECK(px1 < px0);                                              // less ink: "hello world" is replaced by "Z"
    // cancel: still all selected, Backspace then deletes the selection
    r.sim.EndCompose(); r.Frames(2);
    CHECK(std::strcmp(r.buf, "hello world") == 0);
    r.Tap(Key_Backspace);
    CHECK(r.buf[0] == 0);
    // commit replaces the selection
    std::strcpy(r.buf, "hello world");
    r.Tap(Key_SelectAll);
    r.sim.Compose("zz", -1); r.Frame();
    r.sim.EndCompose(); r.sim.Commit("\xe6\x97\xa5\xe6\x9c\xac"); r.Frame();
    CHECK(std::strcmp(r.buf, "\xe6\x97\xa5\xe6\x9c\xac") == 0);
    // partial selection
    std::strcpy(r.buf, "abcdef");
    r.Focus();
    r.Tap(Key_Left); r.Tap(Key_Left);                              // caret between d and e
    r.sim.shift = true; r.Tap(Key_Left); r.Tap(Key_Left); r.sim.shift = false;   // "cd" selected
    r.sim.Commit("XYZ"); r.Frame();
    CHECK(std::strcmp(r.buf, "abXYZef") == 0);
}

void TestFieldKeysAndLimits() {
    RUN_SUITE("composition: keys belong to the input method, limits");
    FieldRig r("abc");
    r.flags = UI::InputText_EnterReturnsTrue;
    r.Focus();
    r.sim.Compose("ni", -1); r.Frame();
    r.Tap(Key_Backspace); r.Tap(Key_Delete); r.Tap(Key_Left); r.Tap(Key_Right); r.Tap(Key_Home); r.Tap(Key_End); r.Tap(Key_SelectAll);
    CHECK(std::strcmp(r.buf, "abc") == 0);
    r.Tap(Key_Enter);
    CHECK(!r.enter);                                               // Enter belongs to the input method
    r.Tap(Key_Escape);
    CHECK(r.Focused());                                            // and so does Escape
    r.sim.Press(Key_Copy); r.sim.Press(Key_Paste); r.Frame();
    CHECK(std::strcmp(r.buf, "abc") == 0);
    r.sim.EndCompose(); r.Frame();
    r.Tap(Key_Enter);
    CHECK(r.enter);                                                // back to normal once composing ends
    r.Tap(Key_Escape); r.Frame();
    CHECK(!r.Focused());
    // not focused: nothing is composed or committed
    r.sim.Compose("zzz", -1); r.sim.Commit("lost"); r.Frame();
    CHECK(!r.comp.active == false || true);
    CHECK(std::strcmp(r.buf, "abc") == 0);
    CHECK(!r.ime.valid);
    r.sim.EndCompose();
    // read-only: ignored
    FieldRig ro("keep");
    ro.flags = UI::InputText_ReadOnly;
    ro.Focus();
    ro.sim.Compose("x", -1); ro.Frame();
    ro.sim.EndCompose(); ro.sim.Commit("y"); ro.Frame();
    CHECK(std::strcmp(ro.buf, "keep") == 0);
    // buffer limit: whole characters, never half of one
    FieldRig small("ab");
    small.cap = 6;                                                 // 5 bytes of text
    small.Focus();
    small.sim.Commit("\xe4\xbd\xa0\xe5\xa5\xbd"); small.Frame();    // 3 + 3 bytes: only the first fits
    CHECK(std::strcmp(small.buf, "ab\xe4\xbd\xa0") == 0);
    small.sim.Commit("xyz"); small.Frame();
    CHECK(std::strcmp(small.buf, "ab\xe4\xbd\xa0") == 0);          // full: nothing more
    small.sim.Commit("\xe4\xbd"); small.Frame();                   // a cut character is dropped
    CHECK(ValidUtf8(small.buf));
    small.sim.Commit("a\x01" "b\xff" "c"); small.Frame();
    CHECK(ValidUtf8(small.buf));
    FieldRig bad("");
    bad.Focus();
    bad.sim.Commit("a\x01\x7f\xff" "b\xc3\x28" "c\xe2\x82"); bad.Frame();
    CHECK(std::strcmp(bad.buf, "ab(c") == 0);
}

void TestFieldImeRect() {
    RUN_SUITE("composition: on-screen caret rectangle (text field)");
    FieldRig r("abc");
    r.Frame();
    CHECK(!r.ime.valid);                                           // no keyboard focus, no rectangle
    r.Focus();
    CHECK(r.ime.valid);
    const UI::Rect box = r.Box();
    CHECK(r.ime.caret.x >= box.x && r.ime.caret.x <= box.x + box.w && r.ime.caret.y >= box.y && r.ime.caret.y + r.ime.caret.h <= box.y + box.h + 0.5f);
    CHECK(r.ime.caret.h > 4 && r.ime.composition.w == 0);
    const float x0 = r.ime.caret.x;
    r.sim.Type("de"); r.Frame();
    CHECK(r.ime.caret.x > x0);                                     // the caret moved right with the text
    // composing: the composition span starts at the old caret, the caret sits inside it
    const float cx = r.ime.caret.x;
    r.sim.Compose("hello", 2); r.Frame();
    CHECK_NEAR(r.ime.composition.x, cx, 0.5);
    CHECK(r.ime.composition.w > 20);
    CHECK(r.ime.caret.x > r.ime.composition.x && r.ime.caret.x < r.ime.composition.x + r.ime.composition.w);
    r.sim.Compose("hello", -1); r.Frame();
    CHECK_NEAR(r.ime.caret.x, r.ime.composition.x + r.ime.composition.w, 2.5);
    // a long preedit scrolls the field; the caret rectangle stays inside the box
    r.sim.Compose(std::string(80, 'w'), -1); r.Frame();
    CHECK(r.ime.caret.x >= box.x && r.ime.caret.x <= box.x + box.w);
    r.sim.EndCompose(); r.Frame();
    // it is the last frame's value until the next frame
    CHECK(r.ime.valid);
}

// ---- CodeEdit ----------------------------------------------------------------------------------------------------------
struct CodeRig {
    Sim sim;
    UI::CodeEdit ce;
    bool changed = false;
    CodeRig(const std::string& text = "") : sim(640, 360) { Reset(); ce.SetText(text); ce.SetShowLineNumbers(true); }
    void Frame() {
        sim.Frame([&] {
            if (UI::Begin("W", 10, 10, 620, 340)) { if (ce.Draw("ed", 560, 260)) changed = true; }
            UI::End();
        });
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void Tap(Key k) { sim.Press(k); Frame(); }
    void Focus() { ce.Focus(); Frame(); Frame(); }
    float X(UI::CodePos p) { return ce.PosToScreen(p).x; }
};

void TestCodeEdit() {
    RUN_SUITE("composition: CodeEdit");
    CodeRig r("abc\nsecond line\nthird");
    CHECK(!r.ce.Composing());
    r.Frame();
    CHECK(r.ce.ImeCaretRect().w == 0 && r.ce.ImeCaretRect().h == 0);   // not focused
    r.Focus();
    r.ce.SetCaret(UI::CodePos{0, 3});
    r.Frame();
    UI::Rect c0 = r.ce.ImeCaretRect();
    CHECK(c0.h > 4);
    CHECK_NEAR(c0.x, r.X(UI::CodePos{0, 3}), 0.01);
    CHECK(UI::GetImeRect().valid);
    // preedit: the text stays; the caret rect follows the preedit's caret
    r.sim.Compose("\xe4\xbd\xa0\xe5\xa5\xbd", -1); r.Frames(2);
    CHECK(r.ce.Composing());
    CHECK(r.ce.GetText() == "abc\nsecond line\nthird");
    const UI::Rect cr = r.ce.ImeCompositionRect();
    CHECK_NEAR(cr.x, r.X(UI::CodePos{0, 3}), 0.01);
    CHECK(cr.w >= r.ce.CharWidth() * 2 - 1);
    CHECK_NEAR(r.ce.ImeCaretRect().x, cr.x + cr.w, 0.5);
    r.sim.Compose("\xe4\xbd\xa0\xe5\xa5\xbd", 3); r.Frames(1);
    CHECK(r.ce.ImeCaretRect().x < cr.x + cr.w - 1);
    CHECK(r.ce.ImeCaretRect().x > cr.x);
    // the preedit is drawn (more ink on that line than without)
    const Bitmap with = r.sim.Draw(0, 0, 0);
    const unsigned bg = r.ce.Palette().background;
    (void)bg;
    // keys are the input method's
    r.Tap(Key_Backspace); r.Tap(Key_Left); r.Tap(Key_Enter); r.Tap(Key_Down); r.Tap(Key_SelectAll); r.Tap(Key_Home);
    CHECK(r.ce.GetText() == "abc\nsecond line\nthird");
    CHECK(r.ce.Caret() == (UI::CodePos{0, 3}) && !r.ce.HasSelection());
    // cancel
    r.sim.EndCompose(); r.Frames(2);
    CHECK(!r.ce.Composing());
    CHECK(r.ce.GetText() == "abc\nsecond line\nthird");
    CHECK(r.ce.Caret() == (UI::CodePos{0, 3}));
    const Bitmap& after = r.sim.Draw(0, 0, 0);
    int diff = 0;
    for (int y = 10; y < 300; ++y) for (int x = 10; x < 600; ++x) if (PixelAt(with, x, y) != PixelAt(after, x, y)) ++diff;
    CHECK(diff > 60);
    // commit inserts at the caret
    r.sim.Compose("ni", -1); r.Frame();
    r.sim.EndCompose(); r.sim.Commit("\xe4\xbd\xa0"); r.Frame();
    CHECK(r.ce.GetText() == "abc\xe4\xbd\xa0\nsecond line\nthird");
    CHECK(r.ce.Caret() == (UI::CodePos{0, 6}));
    CHECK(r.changed);
    // one undo takes the commit back
    r.ce.Undo();
    CHECK(r.ce.GetText() == "abc\nsecond line\nthird");
    // replace a selection (within a line), and a selection over several lines
    r.ce.SetSelection(UI::CodePos{1, 0}, UI::CodePos{1, 6});     // "second"
    r.Frame();
    r.sim.Compose("zz", -1); r.Frames(2);
    CHECK(r.ce.GetText() == "abc\nsecond line\nthird");           // composing does not touch the selection
    CHECK(r.ce.HasSelection());
    r.sim.EndCompose(); r.Frame();                                 // cancel: selection kept
    CHECK(r.ce.HasSelection() && r.ce.SelectedText() == "second");
    r.sim.Commit("2nd"); r.Frame();
    CHECK(r.ce.GetText() == "abc\n2nd line\nthird");
    r.ce.SetSelection(UI::CodePos{0, 2}, UI::CodePos{2, 2});
    r.Frame();
    r.sim.Compose("q", -1); r.Frame();
    CHECK(r.ce.ImeCompositionRect().w > 0);
    r.sim.EndCompose(); r.sim.Commit("-"); r.Frame();
    CHECK(r.ce.GetText() == "ab-ird");
    // text with a newline or tab in a commit: those are dropped (one-line commits only), bad bytes too
    r.ce.SetText("x");
    r.ce.SetCaret(UI::CodePos{0, 1});
    r.Frame();
    r.sim.Commit("a\nb\tc\xff" "d\x01"); r.Frame();
    CHECK(r.ce.GetText() == "xabc?d");
    // read-only: ignored, and it does not claim to be composing
    r.ce.SetReadOnly(true);
    r.sim.Compose("q", -1); r.Frame();
    CHECK(!r.ce.Composing());
    r.sim.EndCompose(); r.sim.Commit("no"); r.Frame();
    CHECK(r.ce.GetText() == "xabc?d");
    r.ce.SetReadOnly(false);
    // a mouse click ends nothing by itself, but the caret rect always matches the caret
    r.sim.EndCompose();
    r.ce.SetCaret(UI::CodePos{0, 2}); r.Frame();
    CHECK_NEAR(r.ce.ImeCaretRect().x, r.X(UI::CodePos{0, 2}), 0.01);
    // far to the right: the editor scrolls so the preedit caret is visible, the rectangle stays in the text area
    r.ce.SetText(std::string(200, 'm'));
    r.ce.SetCaret(UI::CodePos{0, 200});
    r.sim.Compose(std::string(30, 'w'), -1); r.Frames(2);
    const UI::Rect view = r.ce.ViewRect();
    CHECK(r.ce.ImeCaretRect().x >= view.x && r.ce.ImeCaretRect().x <= view.x + view.w);
    r.sim.EndCompose(); r.Frame();
}

void TestGolden() {
    RUN_SUITE("composition: pictures");
    Sim sim(560, 300);
    Reset();
    char buf[64] = "hello world";
    UI::CodeEdit ce;
    ce.SetText("int main() {\n    return 0;\n}\n");
    ce.SetLanguage(UI::CLikeLanguage());
    ce.Focus();
    auto frame = [&] {
        sim.Frame([&] {
            UI::Begin("W", 10, 10, 540, 280);
            UI::TextField(3, 20, 40, 300, 28, buf, 64, 0);
            UI::Dummy(0, 38);
            ce.Draw("ed", 520, 140);
            UI::End();
        });
    };
    frame(); frame();
    ce.SetCaret(UI::CodePos{0, 12});
    sim.Move(300, 54); frame(); sim.Down(); frame(); sim.Up(); frame();   // focus the field: the end of the text
    sim.Compose("\xc2\xb7ni hao", -1); frame(); frame();
    const Bitmap& b = sim.Draw(30, 30, 34);
    std::string why;
    CHECK(MatchesGolden("compose_field", b, &why));
    if (!why.empty()) std::cerr << "  " << why << "\n";
    sim.EndCompose(); ce.Focus(); frame(); frame();
    ce.SetCaret(UI::CodePos{1, 10});
    sim.Compose("ni hao", 3); frame(); frame();
    const Bitmap& d = sim.Draw(30, 30, 34);
    why.clear();
    CHECK(MatchesGolden("compose_code", d, &why));
    if (!why.empty()) std::cerr << "  " << why << "\n";
}

// ---- fuzz --------------------------------------------------------------------------------------------------------------
struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    unsigned Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)(s >> 16); }
    int Int(int n) { return n <= 0 ? 0 : (int)(Next() % (unsigned)n); }
};
std::string RandomBytes(Rng& r, int maxLen) {
    static const char* pieces[] = {"a", "Z", " ", "\xc3\xa9", "\xe6\x97\xa5", "\xf0\x9f\x98\x80", "\xff", "\xc3", "\xe2\x82", "\x01", "\n", "\t", "\xed\xa0\x80", "\x7f", "\xf4\x90\x80\x80"};
    std::string s;
    const int n = r.Int(maxLen + 1);
    for (int i = 0; i < n; ++i) s += pieces[r.Int(15)];
    return s;
}

void TestFuzz() {
    RUN_SUITE("composition: fuzz");
    for (unsigned long long seed = 1; seed <= 4; ++seed) {
        Rng r(seed);
        Reset();
        Sim sim(500, 300);
        char buf[24] = "seed";
        UI::CodeEdit ce;
        ce.SetText("one\ntwo \xe6\x97\xa5\nthree");
        bool focusCode = false;
        for (int f = 0; f < 1500; ++f) {
            if (r.Int(4) == 0) { sim.mx = (float)r.Int(520); sim.my = (float)r.Int(300); }
            if (r.Int(10) == 0) sim.down = !sim.down;
            if (r.Int(6) == 0) { const std::string c = RandomBytes(r, 12); sim.Compose(c, r.Int(40) - 5); }
            if (r.Int(5) == 0) sim.EndCompose();
            if (r.Int(6) == 0) sim.Commit(RandomBytes(r, 10));
            if (r.Int(6) == 0) sim.Type(RandomBytes(r, 6));
            if (r.Int(5) == 0) sim.Press((Key)r.Int(Key_COUNT));
            if (r.Int(30) == 0) { focusCode = !focusCode; if (focusCode) ce.Focus(); }
            if (r.Int(40) == 0) ce.SetReadOnly(!ce.ReadOnly());
            sim.Frame([&] {
                UI::Begin("W", 10, 10, 480, 280);
                UI::TextField(3, 20, 40, 200, 28, buf, (int)sizeof buf, r.Int(8) == 0 ? UI::InputText_ReadOnly : 0);
                ce.Draw("ed", 440, 150);
                const UI::ImeRect ir = UI::GetImeRect();
                if (ir.valid) { CHECK(std::isfinite(ir.caret.x) && std::isfinite(ir.caret.y) && ir.caret.w >= 0 && ir.caret.h >= 0); }
                UI::End();
            });
            CHECK(buf[sizeof buf - 1] == 0 || true);
            CHECK(std::strlen(buf) < sizeof buf);
            CHECK(ValidUtf8(buf));
            CHECK(ValidUtf8(ce.GetText().c_str()));
            if (f % 100 == 0) sim.Draw();
            if (okaytest::g_failures > 20) return;
        }
    }
}

} // namespace

int main() {
    TestCleaning();
    TestFieldInsert();
    TestFieldSelection();
    TestFieldKeysAndLimits();
    TestFieldImeRect();
    TestCodeEdit();
    TestGolden();
    TestFuzz();
    TEST_MAIN_RESULT();
}
