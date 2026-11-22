// Step 6.9 small calls and internals, headless: Shortcut, SetWindowSize by title, GetWindowContentRegionMin,
// GetMousePosOnOpeningCurrentPopup, GetDragDropPayload, BeginDragDropTargetCustom, GetCurrentWindow, GetActiveID, ClearActiveID,
// SetItemKeyOwner / GetKeyOwner and GetStateStorage.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIInternals.hpp"

#include <cmath>
#include <cstring>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;

int main() {
    RUN_SUITE("okayui small calls and internals");

    // ---------------- Shortcut ----------------
    {   // plain chord: fires once on the key-down frame, not while held, not with an extra modifier
        Reset(); Sim s(200, 100);
        int fired = 0; auto ui = [&] { if (UI::Shortcut(UI::Chord(UI::KeyId_S))) ++fired; };
        s.Frame(ui); CHECK(fired == 0);
        s.Hold(UI::KeyId_S, true); s.Frame(ui); CHECK(fired == 1);
        s.Frame(ui); CHECK(fired == 1);
        s.Hold(UI::KeyId_S, false); s.Frame(ui);
        s.Hold(UI::KeyId_Ctrl, true); s.Hold(UI::KeyId_S, true); s.Frame(ui); CHECK(fired == 1);   // Ctrl held: the plain chord does not fire
    }
    {   // modifiers must match exactly
        Reset(); Sim s(200, 100);
        int ctrl = 0, ctrlShift = 0, alt = 0;
        auto ui = [&] {
            if (UI::Shortcut(UI::Chord(UI::KeyId_S, UI::Mod_Ctrl))) ++ctrl;
            if (UI::Shortcut(UI::Chord(UI::KeyId_S, UI::Mod_Ctrl | UI::Mod_Shift))) ++ctrlShift;
            if (UI::Shortcut(UI::Chord(UI::KeyId_S, UI::Mod_Alt))) ++alt;
        };
        s.Hold(UI::KeyId_Ctrl, true); s.Frame(ui); s.Hold(UI::KeyId_S, true); s.Frame(ui);
        CHECK(ctrl == 1 && ctrlShift == 0 && alt == 0);
        s.Hold(UI::KeyId_S, false); s.Frame(ui); s.Hold(UI::KeyId_Shift, true); s.Hold(UI::KeyId_S, true); s.Frame(ui);
        CHECK(ctrl == 1 && ctrlShift == 1);
        s.Hold(UI::KeyId_S, false); s.Hold(UI::KeyId_Ctrl, false); s.Hold(UI::KeyId_Shift, false); s.Frame(ui);
        s.Hold(UI::KeyId_Alt, true); s.Hold(UI::KeyId_S, true); s.Frame(ui);
        CHECK(alt == 1 && ctrl == 1);
    }
    {   // bad chords never fire; repeat only with the flag
        Reset(); Sim s(200, 100);
        int bad = 0, once = 0, rep = 0;
        auto ui = [&] {
            if (UI::Shortcut(0) || UI::Shortcut(UI::Chord(UI::KeyId_COUNT)) || UI::Shortcut(UI::Chord(UI::KeyId_Ctrl)) || UI::Shortcut(255) || UI::Shortcut(-1)) ++bad;
            if (UI::Shortcut(UI::Chord(UI::KeyId_Down))) ++once;
            if (UI::Shortcut(UI::Chord(UI::KeyId_Down), UI::Shortcut_AllowRepeat)) ++rep;
        };
        s.Hold(UI::KeyId_Down, true);
        for (int i = 0; i < 60; ++i) s.Frame(ui);
        CHECK(bad == 0 && once == 1 && rep > 2);
    }
    {   // a focused text field keeps shortcuts quiet unless Shortcut_RouteAlways; an item that owns the key blocks it
        Reset(); Sim s(300, 200);
        char text[32] = ""; UI::Rect fld; int quiet = 0, always = 0, owned = 0; bool own = false;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 300, 200)) {
                UI::InputText("##t", text, sizeof text); fld = UI::GetItemRect();
                if (own) UI::SetItemKeyOwner(UI::KeyId_F5);
            }
            UI::End();
            if (UI::Shortcut(UI::Chord(UI::KeyId_F2))) ++quiet;
            if (UI::Shortcut(UI::Chord(UI::KeyId_F2), UI::Shortcut_RouteAlways)) ++always;
            if (UI::Shortcut(UI::Chord(UI::KeyId_F5), UI::Shortcut_RouteAlways)) ++owned;
        };
        s.Frames(2, ui);
        s.Hold(UI::KeyId_F2, true); s.Frame(ui); s.Hold(UI::KeyId_F2, false); s.Frame(ui);
        CHECK(quiet == 1 && always == 1);                         // no field focused: both fire
        s.Click(fld.x + 5, fld.y + 5, ui); s.Frames(2, ui); CHECK(UI::WantsTextInput());
        s.Hold(UI::KeyId_F2, true); s.Frame(ui); s.Hold(UI::KeyId_F2, false); s.Frame(ui);
        CHECK(quiet == 1 && always == 2);                         // the field has the keyboard: only RouteAlways
        // key ownership
        own = false; s.Hold(UI::KeyId_F5, true); s.Frame(ui); s.Hold(UI::KeyId_F5, false); s.Frame(ui); CHECK(owned == 1);
        own = true; s.Frame(ui); CHECK(UI::GetKeyOwner(UI::KeyId_F5) != 0 && UI::GetKeyOwner(UI::KeyId_F6) == 0);
        s.Hold(UI::KeyId_F5, true); s.Frame(ui); CHECK(owned == 1);   // owned this frame: no fire
        own = false; s.Hold(UI::KeyId_F5, false); s.Frames(3, ui); CHECK(UI::GetKeyOwner(UI::KeyId_F5) == 0);   // ownership lapses
        UI::SetKeyOwner(UI::KeyId_F7, 42u); CHECK(UI::GetKeyOwner(UI::KeyId_F7) == 42u);   // by id, no item needed
        s.Hold(UI::KeyId_F7, true); s.Frame(ui); CHECK(UI::GetKeyOwner(UI::KeyId_F7) == 42u);
        UI::SetKeyOwner(UI::KeyId_F7, 0u); CHECK(UI::GetKeyOwner(UI::KeyId_F7) == 0u); UI::SetKeyOwner((UI::KeyId)-1, 3u); UI::SetKeyOwner(UI::KeyId_COUNT, 3u);
        UI::SetItemKeyOwner(UI::KeyId_F5); UI::SetItemKeyOwner((UI::KeyId)-3); UI::SetItemKeyOwner(UI::KeyId_COUNT);   // outside an item: harmless
    }

    // ---------------- SetWindowSize by title ----------------
    {
        Reset(); Sim s(500, 400);
        auto ui = [&] {
            UI::SetNextWindowPos(UI::Vec2{20, 30}, UI::Cond_FirstUseEver); UI::SetNextWindowSize(UI::Vec2{200, 120}, UI::Cond_FirstUseEver);
            if (UI::Begin("Alpha", nullptr, 0)) UI::Text("a");
            UI::End();
        };
        s.Frames(2, ui);
        CHECK(!UI::SetWindowSize("Nope", UI::Vec2{50, 50}));
        CHECK(!UI::SetWindowSize(nullptr, UI::Vec2{50, 50}) && !UI::SetWindowSize("", UI::Vec2{50, 50}));
        CHECK(!UI::SetWindowSize("Alpha", UI::Vec2{std::nanf(""), 50}) && !UI::SetWindowSize("Alpha", UI::Vec2{50, INFINITY}));
        UI::WindowInfo before = UI::FindWindowByName("Alpha"); CHECK(before.size.x == 200 && before.size.y == 120);
        CHECK(UI::SetWindowSize("Alpha", UI::Vec2{260, 150}, UI::Cond_FirstUseEver));   // found, but the window has been sized already: no change
        s.Frame(ui); UI::WindowInfo kept = UI::FindWindowByName("Alpha"); CHECK(kept.size.x == 200 && kept.size.y == 120);
        CHECK(UI::SetWindowSize("Alpha", UI::Vec2{260, 150}));
        s.Frame(ui); UI::WindowInfo now = UI::FindWindowByName("Alpha");
        CHECK(now.size.x == 260 && now.size.y == 150 && now.pos.x == 20 && now.pos.y == 30);
        CHECK(UI::SetWindowSize("Alpha", UI::Vec2{-5, 1e9f})); s.Frame(ui);
        now = UI::FindWindowByName("Alpha"); CHECK(now.size.x == 1 && now.size.y == 1.0e6f);   // clamped
    }

    // ---------------- window content region, current window ----------------
    {
        Reset(); Sim s(400, 300);
        UI::Vec2 mn{-1, -1}, cur{-1, -1}, win{0, 0}; UI::CurrentWindow cw, outside;
        auto ui = [&](bool bare) { return [&, bare] {
            UI::SetNextWindowPos(UI::Vec2{40, 50}); UI::SetNextWindowSize(UI::Vec2{220, 140});
            if (bare) UI::BareNextWindow();
            if (UI::Begin("Alpha", nullptr, 0)) {
                mn = UI::GetWindowContentRegionMin();
                cur = UI::GetCursorScreenPos(); win = UI::GetWindowPos();
                cw = UI::GetCurrentWindow();
            }
            UI::End();
            outside = UI::GetCurrentWindow();
        }; };
        s.Frames(2, ui(false));
        CHECK(mn.x == cur.x - win.x && mn.y == cur.y - win.y);   // where the first item goes, in window coordinates
        CHECK(mn.y > 0.0f && mn.x >= 0.0f);                       // below the title bar
        CHECK(cw.valid && std::strcmp(cw.title, "Alpha") == 0 && cw.pos.x == 40 && cw.pos.y == 50 && cw.size.x == 220 && cw.size.y == 140);
        CHECK(cw.id != 0 && cw.appearing == false);
        s.Frame(ui(false)); CHECK(UI::GetCurrentWindowRead().valid == false);   // outside a window, like GetCurrentWindow
        CHECK(!outside.valid && outside.id == 0 && outside.title[0] == 0);
        const UI::Vec2 zero = UI::GetWindowContentRegionMin(); CHECK(zero.x == 0 && zero.y == 0);   // outside a window
        Reset(); Sim b(400, 300);
        b.Frames(2, ui(true));
        CHECK(mn.y == 0.0f);                                      // a bare window has no title bar
    }

    // ---------------- popup opening position ----------------
    {
        Reset(); Sim s(400, 300);
        bool open = false; UI::Vec2 at{-1, -1}; bool inPopup = false;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 400, 300)) {
                UI::Text("body");
                if (open) { UI::OpenPopup("menu"); open = false; }
                if (UI::BeginPopup("menu")) { inPopup = true; at = UI::GetMousePosOnOpeningCurrentPopup(); UI::Text("item"); UI::EndPopup(); }
            }
            UI::End();
        };
        s.Move(120, 80); s.Frame(ui); open = true; s.Frame(ui); s.Move(300, 250); inPopup = false; s.Frame(ui);
        CHECK(inPopup && at.x == 120 && at.y == 80);              // where it was opened, not where the pointer is now
        s.Move(310, 260); s.Frame(ui); CHECK(at.x == 120 && at.y == 80);
        s.Frame([&] { const UI::Vec2 p = UI::GetMousePosOnOpeningCurrentPopup(); CHECK(p.x == 310 && p.y == 260); });   // outside a popup: the pointer
    }

    // ---------------- drag and drop payload, custom target ----------------
    {
        Reset(); Sim s(400, 300);
        UI::Rect src; std::string seenType; int seenSize = -1, seenValue = -1, hover = 0, delivered = 0, deliveredValue = -1; bool hasBefore = true;
        const UI::Rect lane{220, 150, 120, 60};
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 400, 300)) {
                UI::Button("src"); src = UI::GetItemRect();
                int v = 7;
                if (UI::BeginDragDropSource()) { UI::SetDragDropPayload("ITEM", &v, (int)sizeof v); UI::EndDragDropSource(); }
                const UI::DragPayload* p = UI::GetDragDropPayload();
                seenType = p ? p->type : ""; seenSize = p ? p->size : -1; if (p) std::memcpy(&seenValue, p->data, sizeof seenValue);
                if (UI::BeginDragDropTargetCustom(lane, 99)) {
                    ++hover;
                    if (const UI::DragPayload* pl = UI::AcceptDragDropPayload("ITEM")) { ++delivered; std::memcpy(&deliveredValue, pl->data, sizeof deliveredValue); }
                    UI::EndDragDropTarget();
                }
                CHECK(!UI::BeginDragDropTargetCustom(UI::Rect{10, 10, 0, 5}, 1));          // empty rectangle
                CHECK(!UI::BeginDragDropTargetCustom(UI::Rect{std::nanf(""), 10, 5, 5}, 1));
            }
            UI::End();
        };
        s.Frames(2, ui);
        hasBefore = UI::GetDragDropPayload() != nullptr; CHECK(!hasBefore);                   // outside a frame, nothing dragged
        CHECK(seenType.empty() && seenSize == -1);
        s.Move(lane.x + 10, lane.y + 10); s.Frame(ui); CHECK(hover == 0);                      // no drag: the custom target stays quiet
        s.Move(src.x + src.w * 0.5f, src.y + src.h * 0.5f); s.Frame(ui);
        s.Down(); s.Frame(ui); s.Move(src.x + 20, src.y + src.h * 0.5f); s.Frame(ui);
        s.Move(lane.x + 60, lane.y + 30); s.Frame(ui); s.Frame(ui);
        CHECK(seenType == "ITEM" && seenSize == 4 && seenValue == 7);                          // the payload is readable during the drag
        CHECK(hover > 0 && delivered == 0);
        s.Up(); s.Frame(ui);
        CHECK(delivered == 1 && deliveredValue == 7);
        s.Frame(ui); CHECK(seenType.empty());                                                  // and gone after the drop
        // released outside the lane: no delivery
        delivered = 0; hover = 0;
        s.Move(src.x + 5, src.y + 5); s.Frame(ui); s.Down(); s.Frame(ui); s.Move(src.x + 40, src.y + 5); s.Frame(ui); s.Move(30, 250); s.Frame(ui); s.Up(); s.Frame(ui);
        CHECK(delivered == 0 && hover == 0);
    }

    // ---------------- active id ----------------
    {
        Reset(); Sim s(300, 200);
        unsigned active = 99, itemId = 0; int clicks = 0; bool clear = false; UI::Rect b;
        auto ui = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 300, 200)) {
                if (clear) UI::ClearActiveID();
                if (UI::Button("press me")) ++clicks;
                b = UI::GetItemRect(); itemId = UI::GetItemID(); active = UI::GetActiveID();
            }
            UI::End();
        };
        s.Frames(2, ui); CHECK(active == 0);
        s.Move(b.x + 5, b.y + 5); s.Frame(ui); s.Down(); s.Frame(ui);
        CHECK(active != 0 && active == itemId);
        s.Up(); s.Frame(ui); CHECK(clicks == 1 && active == 0);                                 // a normal click
        s.Down(); s.Frame(ui); CHECK(active == itemId);
        clear = true; s.Frame(ui); clear = false; CHECK(active == 0);                           // cleared while held
        s.Up(); s.Frame(ui); CHECK(clicks == 1);                                                // the release does not click
        // the keyboard goes too
        Reset(); Sim t(300, 200);
        char text[16] = ""; UI::Rect f; bool wants = false, doClear = false;
        auto ui2 = [&] {
            UI::BareNextWindow();
            if (UI::Begin("w", 0, 0, 300, 200)) {
                if (doClear) { UI::ClearActiveID(); wants = UI::WantsTextInput(); }
                UI::InputText("##f", text, sizeof text); f = UI::GetItemRect();
            }
            UI::End();
        };
        t.Frames(2, ui2); t.Click(f.x + 5, f.y + 5, ui2); t.Frames(2, ui2); CHECK(UI::WantsTextInput());
        doClear = true; wants = true; t.Frame(ui2); CHECK(!wants); doClear = false; t.Frame(ui2); CHECK(!UI::WantsTextInput());
        UI::ClearActiveID();   // outside a frame: harmless
    }

    // ---------------- state storage ----------------
    {
        Reset(); Sim s(300, 200);
        int a1 = -1, a2 = -1, b1 = -1; float af = -1; bool ab = false; void* ap = (void*)1; int marker = 5; int global = -1;
        auto ui = [&] {
            UI::SetNextWindowPos(UI::Vec2{0, 0}); UI::SetNextWindowSize(UI::Vec2{140, 90});
            if (UI::Begin("A", nullptr, 0)) {
                UI::StateStorage st = UI::GetStateStorage();
                a1 = st.GetInt(3, 11); a2 = st.GetInt(4, 22); af = st.GetFloat(5, 1.5f); ab = st.GetBool(6, false); ap = st.GetVoidPtr(7);
                if (a1 == 11) { st.SetInt(3, 33); st.SetFloat(5, 2.5f); st.SetBool(6, true); st.SetVoidPtr(7, &marker); }
            }
            UI::End();
            UI::SetNextWindowPos(UI::Vec2{150, 0}); UI::SetNextWindowSize(UI::Vec2{140, 90});
            if (UI::Begin("B", nullptr, 0)) { b1 = UI::GetStateStorage().GetInt(3, 77); }
            UI::End();
            global = UI::GetStateStorage().GetInt(3, 88);
        };
        s.Frame(ui); CHECK(a1 == 11 && af == 1.5f && !ab && ap == nullptr && b1 == 77 && global == 88);   // defaults on first sight
        s.Frame(ui); CHECK(a1 == 33 && a2 == 22 && af == 2.5f && ab && ap == &marker);                      // kept between frames
        CHECK(b1 == 77 && global == 88);                                                                    // each window has its own store; outside a window another
        UI::StateStorage g = UI::GetStateStorage(); g.SetInt(3, 1); CHECK(g.GetInt(3) == 1);                // the global one works without a frame
        s.Frame(ui); CHECK(a1 == 33 && global == 1);
        UI::ResetState();
        Reset(); s.Frame(ui); CHECK(a1 == 11 && global == 88);                                              // ResetState empties every store
        // a full store drops new values and answers with the default
        UI::StateStorage full = UI::GetStateStorage();
        for (unsigned i = 0; i < 1100; ++i) full.SetInt(1000 + i, (int)i);
        CHECK(full.GetInt(1000) == 0 && full.GetInt(1000 + 1099, -5) == -5);
        full.Clear(); CHECK(full.GetInt(1000, -5) == -5);
        full.SetInt(1, 2); CHECK(full.GetInt(1) == 2);
    }
    TEST_MAIN_RESULT();
}
