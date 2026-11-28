// The OkayUI host layer (shared/OkayUIHost.hpp): okay::platform events in, OkayUI::Input out, and the hosted region that decides who
// owns the mouse. No window: the events are plain okay::platform::Event values.
#include "test_framework.hpp"
#include "OkayUIHost.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"
#include <cstdio>
#include "OkayUITestKit.hpp"

#include <cstring>

using namespace okayuihost;
namespace UI = OkayUI;

using PK = okay::platform::Key;
using okay::platform::Event;
using okay::platform::EventType;
static Event Text(const char* t) { Event e; e.type = EventType::TextInput; e.text = t; return e; }
static Event Key(PK k, std::uint32_t mods = 0, bool down = true, bool repeat = false) {
    Event e; e.type = down ? EventType::KeyDown : EventType::KeyUp; e.key = k; e.mods = mods; e.repeat = repeat; return e;
}
/// A wheel notch count as the device reports it; `flipped` = the OS's natural scrolling (the event then carries it turned back).
static Event Wheel(int y, bool flipped = false) {
    Event e; e.type = EventType::MouseWheel; e.wheelY = (float)(flipped ? -y : y); e.wheelFlipped = flipped; return e;
}
static Event Button(int button, bool down = true) { Event e; e.type = down ? EventType::MouseButtonDown : EventType::MouseButtonUp; e.button = button; return e; }

// One frame of a 400x200 window holding only the CodeEdit `ed`, fed from the collector `c`. `keys` is whether OkayUI may
// have the keyboard this frame (Build's last argument).
static void CodeEditFrame(UI::CodeEdit& ed, InputCollector& c, bool keys = true) {
    UI::BeginFrame(c.Build(0, 0, false, false, false, keys));
    UI::BareNextWindow();
    if (UI::Begin("ce", 0, 0, 400, 200)) ed.Draw("##ce", 400, 200);
    UI::End();
    UI::EndFrameData();
}

int main() {
    RUN_SUITE("okayui host");
    {   // text and every key edge reach the input when keys are delivered; the state is one-shot
        InputCollector c;
        c.Feed(Text("ab")); c.Feed(Text("\xC3\xA9"));
        for (PK k : {PK::Backspace, PK::Delete, PK::Left, PK::Right, PK::Home, PK::End, PK::Return, PK::Escape}) c.Feed(Key(k));
        c.Feed(Key(PK::A, okay::platform::ModCtrl));
        c.Feed(Key(PK::Left, okay::platform::ModShift));
        UI::Input in = c.Build(10, 20, true, false, false, true);
        CHECK(in.mouseX == 10 && in.mouseY == 20 && in.mouseDown && !in.rightDown && !in.blocked);
        CHECK(in.text && !std::strcmp(in.text, "ab\xC3\xA9"));
        CHECK(in.backspace && in.del && in.left && in.right && in.home && in.end && in.enter && in.escape && in.selectAll && in.shift);
        const UI::Input again = c.Build(10, 20, false, false, false, true);
        CHECK(again.text == nullptr && !again.backspace && !again.left && !again.selectAll && !again.mouseDown);
    }
    {   // a plain 'a' is not select-all; the Command key counts like Ctrl; shift follows the latest key event
        InputCollector c;
        c.Feed(Key(PK::A));
        CHECK(!c.Build(0, 0, false, false, false, true).selectAll);
        c.Feed(Key(PK::A, okay::platform::ModGui));
        CHECK(c.Build(0, 0, false, false, false, true).selectAll);
        c.Feed(Key(PK::LShift, okay::platform::ModShift));
        CHECK(c.Build(0, 0, false, false, false, true).shift);
        c.Feed(Key(PK::LShift, 0, false));
        CHECK(!c.Build(0, 0, false, false, false, true).shift);
    }
    {   // key repeat: each repeated KEYDOWN is another edge (OkayUI wants one per press or repeat)
        InputCollector c;
        c.Feed(Key(PK::Backspace, 0, true, true));
        CHECK(c.Build(0, 0, false, false, false, true).backspace);
    }
    {   // a click that goes down and up between two frames is not lost: down for one frame, up for the next
        InputCollector c;
        Event d = Button(okay::platform::MouseLeft);
        Event u = Button(okay::platform::MouseLeft, false);
        c.Feed(d); c.Feed(u);
        CHECK(c.Build(5, 5, false, false, false, false).mouseDown);          // the state sampled at the frame says "up", the event says it was pressed
        CHECK(!c.Build(5, 5, false, false, false, false).mouseDown);
        d.button = okay::platform::MouseRight; c.Feed(d);
        const UI::Input r = c.Build(5, 5, false, false, false, false);
        CHECK(r.rightDown && !r.mouseDown);
        Event m = d; m.button = okay::platform::MouseMiddle; c.Feed(m);      // other buttons are not OkayUI's
        CHECK(!c.Build(5, 5, false, false, false, false).mouseDown);
    }
    {   // two clicks between two frames (a fast double click at a low frame rate): two presses, each an up-then-down edge
        InputCollector c;
        Event d = Button(okay::platform::MouseLeft);
        Event u = Button(okay::platform::MouseLeft, false);
        c.Feed(d); c.Feed(u); c.Feed(d); c.Feed(u);
        CHECK(c.Build(5, 5, false, false, false, false).mouseDown);
        CHECK(!c.Build(5, 5, false, false, false, false).mouseDown);
        CHECK(c.Build(5, 5, false, false, false, false).mouseDown);
        CHECK(!c.Build(5, 5, false, false, false, false).mouseDown);
        // a second click arriving in the frame after the first was shown: the button is shown up once in between
        c.Feed(d); c.Feed(u);
        CHECK(c.Build(5, 5, false, false, false, false).mouseDown);
        c.Feed(d); c.Feed(u);
        CHECK(!c.Build(5, 5, false, false, false, false).mouseDown);
        CHECK(c.Build(5, 5, false, false, false, false).mouseDown);
        // a press held across frames (a drag) stays down
        c.Feed(d);
        CHECK(!c.Build(5, 5, true, false, false, false).mouseDown);   // the last frame showed it down: up once first
        CHECK(c.Build(5, 5, true, false, false, false).mouseDown);
        CHECK(c.Build(5, 5, true, false, false, false).mouseDown);
        CHECK(!c.Build(5, 5, false, false, false, false).mouseDown);
    }
    {   // keys are dropped, not kept, when OkayUI may not have them: nothing arrives a frame late
        InputCollector c;
        c.Feed(Text("x")); c.Feed(Key(PK::Backspace));
        const UI::Input dropped = c.Build(0, 0, false, false, false, false);
        CHECK(dropped.text == nullptr && !dropped.backspace);
        const UI::Input next = c.Build(0, 0, false, false, false, true);
        CHECK(next.text == nullptr && !next.backspace);
    }
    {   // the wheel accumulates in a frame and is delivered even when keys are not; flipped direction inverts it
        InputCollector c;
        c.Feed(Wheel(2)); c.Feed(Wheel(1));
        CHECK(c.Build(0, 0, false, false, false, false).wheel == 3.0f);
        c.Feed(Wheel(2, true));
        CHECK(c.Build(0, 0, false, false, false, false).wheel == -2.0f);
        CHECK(c.Build(0, 0, false, false, false, false).wheel == 0.0f);
    }
    {   // a flood of text cannot overflow the buffer: what does not fit is dropped
        InputCollector c;
        for (int i = 0; i < 100; ++i) c.Feed(Text("abcdefgh"));
        const UI::Input in = c.Build(0, 0, false, false, false, true);
        CHECK(in.text && std::strlen(in.text) < 64 && std::strlen(in.text) > 0);
    }
    {   // the keys a code editor needs: Up/Down, the clipboard shortcuts, the ctrl state and the held named keys
        InputCollector c;
        c.Feed(Key(PK::Up)); c.Feed(Key(PK::Down));
        c.Feed(Key(PK::C, okay::platform::ModCtrl)); c.Feed(Key(PK::X, okay::platform::ModCtrl)); c.Feed(Key(PK::V, okay::platform::ModGui));
        UI::Input in = c.Build(0, 0, false, false, false, true);
        CHECK(in.up && in.down && in.copy && in.cut && in.paste && in.ctrl);
        CHECK(in.keyDown[UI::KeyId_C] && in.keyDown[UI::KeyId_X] && in.keyDown[UI::KeyId_V]);   // pressed and released in one frame still shows once
        in = c.Build(0, 0, false, false, false, true);
        CHECK(!in.up && !in.down && !in.copy && !in.cut && !in.paste);   // edges are one-shot
        CHECK(in.ctrl);                                                  // ...ctrl is a state: it stays until a key event says otherwise
        c.Feed(Key(PK::C)); c.Feed(Key(PK::X)); c.Feed(Key(PK::V));   // without ctrl they are plain letters
        in = c.Build(0, 0, false, false, false, true);
        CHECK(!in.copy && !in.cut && !in.paste && !in.ctrl);
    }
    {   // named keys are held states: down until the key-up, so IsKeyPressed can see the edge and its own repeat
        InputCollector c;
        c.Feed(Key(PK::PageUp)); c.Feed(Key(PK::Tab)); c.Feed(Key(PK::Z, okay::platform::ModCtrl)); c.Feed(Key(PK::Y, okay::platform::ModCtrl));
        UI::Input in = c.Build(0, 0, false, false, false, true);
        CHECK(in.keyDown[UI::KeyId_PageUp] && in.keyDown[UI::KeyId_Tab] && in.keyDown[UI::KeyId_Z] && in.keyDown[UI::KeyId_Y] && !in.keyDown[UI::KeyId_PageDown]);
        in = c.Build(0, 0, false, false, false, true);
        CHECK(in.keyDown[UI::KeyId_PageUp] && in.keyDown[UI::KeyId_Tab]);   // still held
        c.Feed(Key(PK::PageUp, 0, false)); c.Feed(Key(PK::Tab, 0, false));
        in = c.Build(0, 0, false, false, false, true);
        CHECK(!in.keyDown[UI::KeyId_PageUp] && !in.keyDown[UI::KeyId_Tab] && in.keyDown[UI::KeyId_Z]);
        c.Feed(Key(PK::PageDown)); c.Feed(Key(PK::F5));
        in = c.Build(0, 0, false, false, false, false);                       // not delivered while OkayUI may not have the keyboard...
        CHECK(!in.keyDown[UI::KeyId_PageDown] && !in.up);
        in = c.Build(0, 0, false, false, false, true);                        // ...and the held state is still known afterwards
        CHECK(in.keyDown[UI::KeyId_PageDown] && in.keyDown[UI::KeyId_F5]);
        CHECK(InputCollector::KeyIdOf((PK)104, 0) == UI::KeyId_None && InputCollector::KeyIdOf(PK::A, 'a') == UI::KeyId_A &&   // 104: F13
              InputCollector::KeyIdOf(PK::N9, '9') == UI::KeyId_9 && InputCollector::KeyIdOf(PK::RCtrl, 0) == UI::KeyId_Ctrl);
        // '/' and Alt are named keys too: the code editor's Ctrl+/ (comment) and Alt+Up / Alt+Down (move lines)
        CHECK(InputCollector::KeyIdOf(PK::Slash, '/') == UI::KeyId_Slash && InputCollector::KeyIdOf(PK::LAlt, 0) == UI::KeyId_Alt);
        c.Feed(Key(PK::Slash, okay::platform::ModCtrl));
        in = c.Build(0, 0, false, false, false, true);
        CHECK(in.keyDown[UI::KeyId_Slash] && in.ctrl);
        using PK2 = PK;
        CHECK(InputCollector::KeyIdOf(PK2::Slash, 0) == UI::KeyId_Slash && InputCollector::KeyIdOf(PK2::Slash, '/') == UI::KeyId_Slash);
        // the code editor's fold keys
        CHECK(InputCollector::KeyIdOf(PK::LeftBracket, '[') == UI::KeyId_LeftBracket && InputCollector::KeyIdOf(PK::RightBracket, ']') == UI::KeyId_RightBracket);
        CHECK(InputCollector::KeyIdOf(PK2::LeftBracket, 0) == UI::KeyId_LeftBracket && InputCollector::KeyIdOf(PK2::RightBracket, ']') == UI::KeyId_RightBracket);
        CHECK(InputCollector::KeyIdOf(PK2::LAlt, 0) == UI::KeyId_Alt);
    }
    {   // end to end: a CodeEdit driven only by platform events (arrows, Tab, Ctrl+Z/Y, Ctrl+C then Ctrl+V, Ctrl+Left word move)
        okayuitest::Reset();
        okayuitest::Sim sim(400, 200);
        UI::CodeEdit ed;
        ed.SetText("one two\nthree");
        InputCollector c;
        auto frame = [&]() { CodeEditFrame(ed, c); };
        frame(); ed.Focus(); frame();
        // One key per frame, then its release (what a person's keyboard does): the keys of a frame are applied in a fixed order.
        auto tap = [&](PK k, std::uint32_t mod = 0) { c.Feed(Key(k, mod)); frame(); c.Feed(Key(k, 0, false)); frame(); };
        tap(PK::Down);
        CHECK(ed.Caret().line == 1);
        tap(PK::Up);
        CHECK(ed.Caret().line == 0);
        tap(PK::End);
        tap(PK::Left, okay::platform::ModCtrl);                                  // word-left from the end of "one two"
        CHECK(ed.Caret().line == 0 && ed.Caret().col == 4);
        tap(PK::Right, okay::platform::ModShift | okay::platform::ModCtrl);
        CHECK(ed.SelectedText() == "two");
        tap(PK::C, okay::platform::ModCtrl);
        CHECK(std::string(UI::GetClipboardText()) == "two");
        tap(PK::End);
        tap(PK::V, okay::platform::ModCtrl);
        CHECK(ed.Line(0) == "one twotwo");
        tap(PK::Z, okay::platform::ModCtrl);
        CHECK(ed.Line(0) == "one two");
        tap(PK::Y, okay::platform::ModCtrl);
        CHECK(ed.Line(0) == "one twotwo");
        tap(PK::Home, okay::platform::ModCtrl);
        CHECK(ed.Caret().line == 0 && ed.Caret().col == 0);
        tap(PK::Tab);
        CHECK(ed.Line(0) == "    one twotwo");
    }
    {   // input method (IME) composition through the collector: the TextEditing event gives the preedit to a
        // focused CodeEdit every frame until it is cleared; the finished text arrives as typed text (TextInput)
        okayuitest::Reset();
        okayuitest::Sim sim(400, 200);
        UI::CodeEdit ed;
        ed.SetText("ab");
        InputCollector c;
        auto frame = [&]() { CodeEditFrame(ed, c); };
        frame(); ed.Focus(); frame();
        ed.SetCaret(UI::CodePos{0, 1});
        Event e; e.type = EventType::TextEditing;
        e.text = "\xe3\x81\x8b"; e.x = 1;   // a kana being composed, caret after it
        c.Feed(e);
        frame();
        CHECK(ed.Composing());
        CHECK(ed.Line(0) == "ab");                         // the preedit is shown, not inserted
        frame();
        CHECK(ed.Composing());                             // a state: still composing on the next frame
        CHECK(ed.ImeCompositionRect().w > 0.0f);           // where the host puts the candidate window
        e.text.clear(); c.Feed(e);                         // the input method clears the preedit...
        c.Feed(Text("\xe3\x81\x8b"));                       // ...and sends the finished text
        frame();
        CHECK(!ed.Composing());
        CHECK(ed.Line(0) == "a\xe3\x81\x8b" "b");
        // again, then cancelled
        okay::platform::Event pe; pe.type = okay::platform::EventType::TextEditing; pe.text = "xy"; pe.x = 1;
        c.Feed(pe);
        frame();
        CHECK(ed.Composing());
        pe.text.clear(); c.Feed(pe);
        frame();
        CHECK(!ed.Composing() && ed.Line(0) == "a\xe3\x81\x8b" "b");   // cleared without a commit: cancelled, the text is untouched
        // without the keyboard (deliverKeys false) no preedit reaches OkayUI
        pe.text = "zz"; c.Feed(pe);
        CodeEditFrame(ed, c, false);
        CHECK(!ed.Composing());
    }
    {   // the hosted region: owns the mouse only inside the rectangle, only while ImGui said it is hovered, and only for one more frame
        Region r;
        CHECK(!r.Owns(5, 5) && !r.Valid());
        r.Offer(100, 100, 200, 50, true);
        CHECK(r.Owns(100, 100) && r.Owns(299, 149) && !r.Owns(300, 100) && !r.Owns(99, 100) && !r.Owns(150, 150));
        r.Offer(100, 100, 200, 50, false);                     // another window covers it
        CHECK(!r.Owns(150, 120));
        r.Offer(100, 100, 200, 50, true);
        r.NextFrame();                                         // still valid the next frame...
        CHECK(r.Owns(150, 120));
        r.NextFrame();                                         // ...but a panel that was not drawn again is dropped
        CHECK(!r.Owns(150, 120) && !r.Valid());
    }
    {   // several hosted rectangles in one frame (a page and the notifications panel over it): each owns its own area
        Region r;
        r.Offer(0, 0, 400, 300, false);                        // the page, covered here by the panel's ImGui window
        r.Offer(250, 50, 200, 200, true);                      // the panel
        CHECK(r.Count() == 2);
        CHECK(r.Owns(300, 100) && !r.Owns(100, 100));          // inside the panel: owned; the covered page: not
        r.NextFrame();
        r.Offer(0, 0, 400, 300, true);                         // panel closed: only the page is offered now
        CHECK(r.Owns(100, 100));
        r.NextFrame();
        CHECK(r.Count() == 1 && r.Owns(100, 100) && !r.Owns(440, 240));   // the panel's rectangle went with its frame
        r.NextFrame();
        CHECK(r.Count() == 0 && !r.Owns(100, 100));
        for (int i = 0; i < Region::kMax + 3; ++i) r.Offer((float)i * 10, 0, 5, 5, true);   // more than it holds: the rest are ignored
        CHECK(r.Count() == Region::kMax && r.Owns(1, 1) && !r.Owns((Region::kMax + 1) * 10.0f + 1, 1));
        r.NextFrame();
        CHECK(r.Overflowed() == 3);                            // the three ignored offers are counted for the frame
        r.NextFrame();
        CHECK(r.Overflowed() == 0);
    }
    {   // the OkayUI Inspector: one rectangle per section (card, Transform, a header and a body per component). With 8 slots an
        // object with five components lost the mouse on every section after the 8th (its fields ignored clicks: "frozen").
        Region r;
        const int sections = 2 + 2 * 12;                       // twelve components
        CHECK(sections <= Region::kMax);
        for (int i = 0; i < sections; ++i) r.Offer(0, (float)i * 40, 300, 38, true);
        r.NextFrame();
        CHECK(r.Overflowed() == 0);
        bool all = true;
        for (int i = 0; i < sections; ++i) all = all && r.Owns(10, (float)i * 40 + 5);
        CHECK(all);                                            // the last component's body takes the mouse like the first
    }
    {   // a panel drawn by OkayUI only noted as active (hovered or focused): true for that frame and the next, then not
        Region r;
        CHECK(!r.Active());
        r.NoteActive();
        CHECK(r.Active());
        r.NextFrame();
        CHECK(r.Active());                                     // the host asks at the start of the next frame
        r.NextFrame();
        CHECK(!r.Active());
        r.NoteActive(); r.NextFrame(); r.NoteActive(); r.NextFrame();
        CHECK(r.Active());
        for (int i = 0; i < 5; ++i) r.NextFrame();
        CHECK(!r.Active());
        // what the host asks: input for OkayUI's own hosted pieces only
        CHECK(!HostedInputWanted(r, false));                   // nothing hovered, nothing focused: a game's ui_* widgets stay as they were
        CHECK(HostedInputWanted(r, true));                     // a code field has the keyboard
        r.Offer(0, 0, 100, 100, false); CHECK(!r.AnyHovered() && !HostedInputWanted(r, false));   // offered but covered
        r.Offer(0, 0, 100, 100, true);  CHECK(r.AnyHovered() && HostedInputWanted(r, false));     // hovered (a code editor's area)
        r.NextFrame(); r.NextFrame();
        CHECK(!r.AnyHovered() && !HostedInputWanted(r, false));
        r.NoteActive(); CHECK(HostedInputWanted(r, false));
    }
    {   // an open menu offered as an overlay: the host keeps a press there from ImGui (OwnsOverlay); the panel itself is not an overlay
        Region r;
        r.Offer(0, 0, 300, 600, true);                         // the Hierarchy panel
        r.Offer(250, 100, 260, 700, true, true);               // its menu, hanging out over the Scene view
        CHECK(r.Owns(400, 300) && r.OwnsOverlay(400, 300) && !r.OwnsOverlay(100, 50) && r.Owns(100, 50));
        r.NextFrame(); r.NextFrame();                          // the menu closed: no longer kept from ImGui
        CHECK(!r.OwnsOverlay(400, 300));
    }
    {   // end to end through OkayUI: typed text from platform events edits a focused field; a blocked frame does not click
        okayuitest::Reset();
        okayuitest::Sim sim(300, 100);
        char buf[32] = "";
        UI::Rect field;
        InputCollector c;
        auto frame = [&](const UI::Input& in) {
            UI::BeginFrame(in);
            UI::BareNextWindow();
            if (UI::Begin("host", 0, 0, 300, 100)) { UI::InputText("##f", buf, (int)sizeof(buf)); field = UI::GetItemRect(); }
            UI::End();
            UI::EndFrameData();
        };
        frame(c.Build(0, 0, false, false, false, false));
        const float cx = field.x + 20, cy = field.y + field.h * 0.5f;
        frame(c.Build(cx, cy, false, false, true, false));      // blocked press: no focus
        frame(c.Build(cx, cy, true, false, true, false));
        frame(c.Build(cx, cy, false, false, true, false));
        CHECK(!UI::WantsTextInput());
        frame(c.Build(cx, cy, false, false, false, false));     // a real click focuses it
        frame(c.Build(cx, cy, true, false, false, false));
        frame(c.Build(cx, cy, false, false, false, false));
        CHECK(UI::WantsTextInput());
        c.Feed(Text("hi")); c.Feed(Text("!"));
        frame(c.Build(cx, cy, false, false, false, true));
        CHECK(!std::strcmp(buf, "hi!"));
        c.Feed(Key(PK::Backspace));
        frame(c.Build(cx, cy, false, false, false, true));
        CHECK(!std::strcmp(buf, "hi"));
        c.Feed(Key(PK::Escape));
        frame(c.Build(cx, cy, false, false, false, true));
        CHECK(!UI::WantsTextInput());
    }
    {   // a mixed sequence of platform events gives the Input the frame expects
        auto pText = [](const char* t) { return Text(t); };
        auto pKey = [](PK k, uint32_t mods = 0, bool down = true) { return Key(k, mods, down); };
        const PK keys[] = {PK::Backspace, PK::Delete, PK::Left, PK::Right, PK::Home, PK::End, PK::Return, PK::Escape, PK::Up,
                           PK::Down, PK::Tab, PK::F5, PK::Space, PK::PageUp};
        InputCollector p;
        p.Feed(pText("ab"));
        p.Feed(pText("\xC3\xA9"));
        for (PK k : keys) p.Feed(pKey(k));
        p.Feed(pKey(PK::W));
        p.Feed(pKey(PK::N3));
        p.Feed(pKey(PK::A, okay::platform::ModCtrl));
        p.Feed(pKey(PK::C, okay::platform::ModGui));
        p.Feed(pKey(PK::X, okay::platform::ModCtrl));
        p.Feed(pKey(PK::V, okay::platform::ModCtrl));
        p.Feed(pKey(PK::LShift, okay::platform::ModShift));
        p.Feed(Wheel(2));
        p.Feed(Wheel(3, true));
        p.Feed(Button(okay::platform::MouseRight));
        const UI::Input b = p.Build(1, 2, false, false, false, true);
        CHECK(b.text && !std::strcmp(b.text, "ab\xC3\xA9"));
        CHECK(b.del && b.left && b.right && b.home && b.end && b.enter && b.escape && b.up && b.down);
        CHECK(b.shift && b.ctrl && b.rightDown);
        CHECK(b.backspace && b.selectAll && b.copy && b.cut && b.paste && b.wheel == -1.0f);
        CHECK(b.keyDown[UI::KeyId_W] && b.keyDown[UI::KeyId_3] && b.keyDown[UI::KeyId_F1 + 4] && b.keyDown[UI::KeyId_Tab]);
        // a layout where the "a" key types "q" (AZERTY): Ctrl on that key is the shortcut of the letter it types
        InputCollector az;
        az.Feed(pKey(PK::A, okay::platform::ModCtrl), 'q');
        CHECK(!az.Build(0, 0, false, false, false, true).selectAll);
        az.Feed(pKey(PK::Q, okay::platform::ModCtrl), 'a');
        CHECK(az.Build(0, 0, false, false, false, true).selectAll);
        // text past the buffer is dropped, not cut through a character or overrun
        InputCollector big;
        for (int i = 0; i < 40; ++i) big.Feed(pText("xyz"));
        const UI::Input bi = big.Build(0, 0, false, false, false, true);
        CHECK(bi.text && std::strlen(bi.text) < 64);
    }
    TEST_MAIN_RESULT();
}
