// OkayUI input method host side, headless: the events an operating system's input method sends (preedit, commit, cancel), the SDL
// translation (SDL_TEXTEDITING / SDL_TEXTINPUT as plain SDL_Event structs), the calls back (start and stop text input, candidate
// window rectangle) into an in-memory sink, and all of it driving the real single-line field and CodeEdit. Nothing here opens a window:
// a real input method (Windows, macOS, X11, Wayland) is NOT exercised. OKAYUI_FUZZ_ITERS scales the fuzz (default 3000).
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "OkayUIHost.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace okayuitest;
using namespace okayuihost;
namespace UI = OkayUI;

namespace {

SDL_Event Editing(const char* s, int start = 0, int len = 0) {
    SDL_Event e; std::memset(&e, 0, sizeof e); e.type = SDL_TEXTEDITING;
    std::strncpy(e.edit.text, s, sizeof(e.edit.text) - 1); e.edit.start = start; e.edit.length = len;
    return e;
}
SDL_Event Typed(const char* s) { SDL_Event e; std::memset(&e, 0, sizeof e); e.type = SDL_TEXTINPUT; std::strncpy(e.text.text, s, sizeof(e.text.text) - 1); return e; }

void TestState() {
    RUN_SUITE("ime state: preedit is a state, a commit is one-shot");
    ImeState ime;
    UI::Input in;
    ime.Apply(in);
    CHECK(in.composition == nullptr && in.commit == nullptr && in.compositionCaret == -1 && !ime.Composing());
    ime.Preedit("ni", -1);
    ime.Apply(in);
    CHECK(in.composition && !std::strcmp(in.composition, "ni") && in.compositionCaret == -1 && ime.Composing());
    UI::Input again; ime.Apply(again);                         // the preedit stays until composing ends
    CHECK(again.composition && !std::strcmp(again.composition, "ni"));
    ime.Preedit("ni\xe5\xa5\xbd", 3);                          // caret counted in characters: after "ni" and one CJK character = 5 bytes
    ime.Apply(in);
    CHECK(in.compositionCaret == 5);
    ime.Preedit("ni\xe5\xa5\xbd", 2); ime.Apply(in); CHECK(in.compositionCaret == 2);
    ime.Preedit("ni\xe5\xa5\xbd", 99); ime.Apply(in); CHECK(in.compositionCaret == 5);   // past the end: the end
    ime.Preedit("ni", -5); ime.Apply(in); CHECK(in.compositionCaret == -1);
    ime.Commit("\xe4\xbd\xa0");                                // commit ends composing and arrives once
    ime.Apply(in);
    CHECK(in.composition == nullptr && in.commit && !std::strcmp(in.commit, "\xe4\xbd\xa0"));
    ime.Apply(in); CHECK(in.commit == nullptr);
    ime.Commit("a"); ime.Commit("b"); ime.Apply(in);          // two commits in one frame add up
    CHECK(in.commit && !std::strcmp(in.commit, "ab"));
    ime.Preedit("x"); ime.Cancel(); ime.Apply(in);
    CHECK(in.composition == nullptr && in.commit == nullptr);
    ime.Preedit(""); CHECK(!ime.Composing());
    ime.Preedit(nullptr); ime.Commit(nullptr); ime.Commit(""); ime.Apply(in);
    CHECK(in.composition == nullptr && in.commit == nullptr);
    // bounded
    ime.Preedit(std::string(5000, 'x').c_str()); CHECK(ime.PreeditText().size() <= ImeState::kMaxPreedit);
    for (int i = 0; i < 100; ++i) ime.Commit(std::string(500, 'y').c_str());
    ime.Apply(in); CHECK(in.commit && std::strlen(in.commit) <= ImeState::kMaxPreedit);
    CHECK(ImeState::CharsToBytes("abc", 2) == 2 && ImeState::CharsToBytes("\xe5\xa5\xbd" "a", 1) == 3 && ImeState::CharsToBytes("a\xf0\x9f\x98\x80z", 2) == 5);
    CHECK(ImeState::CharsToBytes("\xe5\xa5", 1) == 2 && ImeState::CharsToBytes("", 3) == 0);   // a cut character does not run past the end
}

void TestCollector() {
    RUN_SUITE("ime host: SDL_TEXTEDITING and SDL_TEXTINPUT through the collector");
    InputCollector c;
    c.Feed(Editing("ni\xe5\xa5\xbd", 2));
    UI::Input in = c.Build(0, 0, false, false, false, true);
    CHECK(in.composition && !std::strcmp(in.composition, "ni\xe5\xa5\xbd") && in.compositionCaret == 2);
    in = c.Build(0, 0, false, false, false, true);                      // a state: still there on the next frame
    CHECK(in.composition && !std::strcmp(in.composition, "ni\xe5\xa5\xbd"));
    in = c.Build(0, 0, false, false, false, false);                     // keys not delivered (an ImGui field has them): no preedit either
    CHECK(in.composition == nullptr);
    c.Feed(Editing("", 0));                                              // an empty editing event: composing ended
    in = c.Build(0, 0, false, false, false, true);
    CHECK(in.composition == nullptr);
    c.Feed(Editing("ab", 1)); c.Feed(Typed("\xe4\xbd\xa0"));            // the commit arrives as text and ends the preedit
    in = c.Build(0, 0, false, false, false, true);
    CHECK(in.composition == nullptr && in.text && !std::strcmp(in.text, "\xe4\xbd\xa0"));
    // typing without composing is untouched
    c.Feed(Typed("q"));
    in = c.Build(0, 0, false, false, false, true);
    CHECK(in.text && !std::strcmp(in.text, "q") && in.composition == nullptr);
    // a 32-byte (full) editing text, bad UTF-8 in it, never crashes
    SDL_Event e = Editing("", 0); std::memset(e.edit.text, 0xff, sizeof(e.edit.text) - 1); e.edit.start = 5000; c.Feed(e);
    in = c.Build(0, 0, false, false, false, true);
    (void)in;
}

// A field and a CodeEdit in one window driven through the collector and a recording sink, like a host would.
struct HostRig {
    Sim sim;                       // used only for its Draw (the frames are driven here)
    InputCollector col;
    RecordingImeSink sink;
    char buf[64] = "";
    UI::CodeEdit ce;
    bool useEdit = false;
    float mx = 0, my = 0; bool down = false;
    HostRig() : sim(480, 260) { Reset(); ce.SetText("abc"); }
    void Frame() {
        UI::Input in = col.Build(mx, my, down, false, false, true);
        in.displayW = 480; in.displayH = 260;
        UI::BeginFrame(in);
        UI::Begin("W", 10, 10, 460, 240);
        UI::TextField(7, 20, 40, 300, 28, buf, (int)sizeof buf, 0);
        UI::SetCursorScreenPos(UI::Vec2{20, 90});
        ce.Draw("ed", 440, 120);
        UI::End();
        UI::EndFrameData();
        col.Ime().AfterFrame(sink);
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void ClickField() { mx = 300; my = 54; Frame(); down = true; Frame(); down = false; Frame(); }
    void ClickEdit() { mx = 100; my = 130; Frame(); down = true; Frame(); down = false; Frame(); }
    void ClickNowhere() { mx = 470; my = 250; Frame(); down = true; Frame(); down = false; Frame(); }
};

void TestHostLoop() {
    RUN_SUITE("ime host: text input on and off, candidate window, composing in a field");
    HostRig r;
    r.Frames(3);
    CHECK(!r.sink.active && r.sink.starts == 0);                         // nothing focused: no text input
    r.ClickField();
    CHECK(r.sink.active && r.sink.starts == 1);
    CHECK(r.sink.haveRect);
    const float rx0 = r.sink.rx, ry0 = r.sink.ry;
    CHECK(ry0 >= 40 && ry0 <= 70 && rx0 >= 20 && rx0 <= 330 && r.sink.rh > 4);   // the caret of the field
    const int calls = r.sink.rectCalls;
    r.Frames(30);
    CHECK(r.sink.rectCalls == calls && r.sink.starts == 1);              // no call per frame while nothing moves
    // composing: the preedit is drawn, the buffer stays, the rectangle is the preedit span
    r.col.Feed(Editing("ni\xe5\xa5\xbd", 3)); r.Frames(2);
    CHECK(std::string(r.buf).empty() && UI::GetComposition().active);
    CHECK(r.sink.rectCalls > calls && r.sink.rw > 4);                    // a span, not a 2 px caret
    const float spanW = r.sink.rw, spanX = r.sink.rx;
    CHECK(spanX >= rx0 - 1);
    r.col.Feed(Editing("ni\xe5\xa5\xbd\xe5\xa5\xbd", 4)); r.Frames(2);
    CHECK(r.sink.rw > spanW);                                            // a longer preedit: a wider span
    // commit: the text lands in the buffer once, composing is over
    r.col.Feed(Editing("", 0)); r.col.Feed(Typed("\xe4\xbd\xa0\xe5\xa5\xbd")); r.Frames(2);
    CHECK(std::string(r.buf) == "\xe4\xbd\xa0\xe5\xa5\xbd" && !UI::GetComposition().active);
    r.Frames(3);
    CHECK(std::string(r.buf) == "\xe4\xbd\xa0\xe5\xa5\xbd");             // not inserted twice
    // a half-made composition is dropped when the field loses the keyboard
    r.col.Feed(Editing("abc", 3)); r.Frames(2);
    CHECK(r.col.Ime().Composing());
    r.ClickNowhere();
    CHECK(!r.sink.active && r.sink.stops == 1);
    CHECK(!r.col.Ime().Composing());
    CHECK(std::string(r.buf) == "\xe4\xbd\xa0\xe5\xa5\xbd");
    // into the CodeEdit
    r.ClickEdit();
    CHECK(r.sink.active && r.sink.starts == 2);
    CHECK(r.sink.ry >= 60);                                              // the editor is below the field
    const std::string before = r.ce.GetText();
    r.col.Feed(Editing("\xe6\x97\xa5\xe6\x9c\xac", 2)); r.Frames(2);
    CHECK(r.ce.Composing() && r.ce.GetText() == before && r.sink.rw > 4);
    r.col.Feed(Editing("", 0)); r.col.Feed(Typed("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e")); r.Frames(2);
    CHECK(r.ce.GetText().find("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") != std::string::npos && !r.ce.Composing());
    // focus moves from the editor to the field: text input stays on (one start, no stop), the rectangle moves
    const float editY = r.sink.ry;
    r.ClickField();
    CHECK(r.sink.active && r.sink.ry < editY);
}

void TestRecordingSink() {
    RUN_SUITE("ime sink: only changes are sent");
    ImeState ime; RecordingImeSink sink;
    Reset();
    for (int i = 0; i < 5; ++i) { UI::Input in; UI::BeginFrame(in); UI::EndFrameData(); ime.AfterFrame(sink); }
    CHECK(sink.starts == 0 && sink.stops == 0 && sink.rectCalls == 0);
    // a custom text widget reports its caret with SetImeRect and takes the keyboard with SetKeyboardFocusId
    for (int i = 0; i < 3; ++i) {
        UI::Input in; UI::BeginFrame(in);
        UI::SetKeyboardFocusId(99);
        UI::ImeRect r; r.caret = UI::Rect{10, 20, 2, 16}; r.composition = UI::Rect{10, 20, 0, 16}; UI::SetImeRect(r);
        UI::EndFrameData(); ime.AfterFrame(sink);
    }
    CHECK(sink.starts == 1 && sink.rectCalls == 1 && sink.rx == 10 && sink.ry == 20 && sink.rw == 2 && sink.rh == 16);
    ime.Preedit("ab");
    { UI::Input in; ime.Apply(in); UI::BeginFrame(in); UI::SetKeyboardFocusId(99);
      UI::ImeRect r; r.caret = UI::Rect{10, 20, 2, 16}; r.composition = UI::Rect{10, 20, 30, 16}; UI::SetImeRect(r); UI::EndFrameData(); ime.AfterFrame(sink); }
    CHECK(sink.rectCalls == 2 && sink.rw == 30);                         // composing: the preedit span
    { UI::Input in; ime.Apply(in); UI::BeginFrame(in); UI::SetKeyboardFocusId(0); UI::EndFrameData(); ime.AfterFrame(sink); }   // the focus is gone
    CHECK(sink.stops == 1 && !sink.active && !ime.Composing());
    Reset();
}

void TestFuzz() {
    RUN_SUITE("ime host: fuzz (random events, focus, mouse)");
    const char* e = std::getenv("OKAYUI_FUZZ_ITERS");
    const int iters = e ? std::max(50, std::atoi(e)) : 3000;
    std::uint64_t st = 0x2545F4914F6CDD1Dull;
    auto next = [&] { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (unsigned)(st >> 16); };
    auto I = [&](int n) { return n <= 0 ? 0 : (int)(next() % (unsigned)n); };
    HostRig r;
    bool ok = true;
    int expectActive = 0;
    for (int it = 0; it < iters && ok; ++it) {
        switch (I(9)) {
            case 0: { char t[40]; const int n = I(30); for (int k = 0; k < n; ++k) t[k] = (char)(I(4) == 0 ? I(256) : 0x20 + I(0x60)); t[n] = 0; r.col.Feed(Editing(t, I(100) - 20, I(10))); break; }
            case 1: r.col.Feed(Editing("", 0)); break;
            case 2: { const char* cands[] = {"\xe4\xbd\xa0", "a", "\xf0\x9f\x98\x80", "\xff\xfe", "", "\xe5\xa5"}; r.col.Feed(Typed(cands[I(6)])); break; }
            case 3: r.mx = (float)I(480); r.my = (float)I(260); break;
            case 4: r.down = I(2) == 0; break;
            case 5: { SDL_Event k; std::memset(&k, 0, sizeof k); k.type = SDL_KEYDOWN; k.key.keysym.sym = I(2) ? SDLK_BACKSPACE : SDLK_RETURN; r.col.Feed(k); break; }
            default: break;
        }
        r.Frame();
        // invariants: the sink's idea of "active" follows the keyboard focus; the rectangle is finite; the buffer stays terminated and valid
        const bool want = UI::WantsTextInput();
        if (r.sink.active != want) { ok = false; std::printf("  frame %d: sink active %d but focus %d\n", it, (int)r.sink.active, (int)want); }
        if (!std::isfinite(r.sink.rx + r.sink.ry + r.sink.rw + r.sink.rh)) { ok = false; std::printf("  frame %d: rect not finite\n", it); }
        if (std::strlen(r.buf) >= sizeof r.buf) { ok = false; std::printf("  frame %d: buffer overrun\n", it); }
        if (r.sink.starts - r.sink.stops != (r.sink.active ? 1 : 0)) { ok = false; std::printf("  frame %d: start/stop do not balance\n", it); }
        (void)expectActive;
    }
    CHECK(ok);
}

} // namespace

int main() {
    TestState();
    TestCollector();
    TestHostLoop();
    TestRecordingSink();
    TestFuzz();
    TEST_MAIN_RESULT();
}
