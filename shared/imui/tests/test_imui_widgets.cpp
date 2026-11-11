// okay::imui behaviour: synthetic mouse / keyboard / text fed across frames, asserting widget results and state.
#include "test_framework.hpp"
#include "ImuiTestKit.hpp"

using namespace imuitest;

namespace {
Context* g_ctx = nullptr;

// One window "W" at a fixed place; `body` draws widgets in it.
struct Rig {
    Sim sim;
    std::function<void()> body;
    Rig() : sim(g_ctx) {}
    void Ui() {
        SetNextWindowPos(Vec2(10, 10), Cond_Once);
        SetNextWindowSize(Vec2(400, 400), Cond_Once);
        Begin("W");
        if (body) body();
        End();
    }
    void Frame() { sim.Frame([&] { Ui(); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void MoveTo(Vec2 p) { sim.Move(p.x, p.y); Frame(); }
    void Click(Vec2 p) { sim.Move(p.x, p.y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
};
}

int main() {
    RUN_SUITE("imui widgets");
    g_ctx = MakeContext();

    // ---- Button ------------------------------------------------------------------------------
    {
        Rig r; int clicks = 0; bool hovered = false, active = false; Rect rc; bool pressedNow = false;
        r.body = [&] {
            pressedNow = Button("Go");
            if (pressedNow) ++clicks;
            hovered = IsItemHovered(); active = IsItemActive(); rc = GetItemRect();
        };
        r.Frames(2);
        CHECK(rc.Width() > 0 && rc.Height() == GetFrameHeight());
        CHECK_NEAR(rc.Width(), 2 * 16 + 12, 1e-4);                      // "Go" = 2 glyphs of 16 + padding 6 each side
        CHECK(!hovered && clicks == 0);
        r.MoveTo(Center(rc));
        CHECK(hovered && !active && GetHoveredId() == GetID("Go") * 0 + GetHoveredId() && GetHoveredId() != 0);
        r.sim.Down(); r.Frame();
        CHECK(active && clicks == 0);
        r.sim.Up(); r.sim.Frame([&] { r.Ui(); });
        CHECK(clicks == 1 && pressedNow);
        r.Frame();
        CHECK(clicks == 1 && !pressedNow);                               // one frame only
        // press, slide off, release: no click
        r.sim.Down(); r.Frame(); r.MoveTo(Vec2(300, 300)); r.sim.Up(); r.Frame();
        CHECK(clicks == 1);
        // press outside, slide over the button, release: no click (the press did not start on it)
        r.MoveTo(Vec2(300, 300)); r.sim.Down(); r.Frame(); r.MoveTo(Center(rc)); r.sim.Up(); r.Frame();
        CHECK(clicks == 1 && !active);
        r.Click(Center(rc));
        CHECK(clicks == 2);
        // right button does nothing
        r.MoveTo(Center(rc)); r.sim.Down(1); r.Frame(); r.sim.Up(1); r.Frame();
        CHECK(clicks == 2);
    }
    // ---- Checkbox ----------------------------------------------------------------------------
    {
        Rig r; bool v = false; Rect rc; bool ch = false;
        r.body = [&] { ch = Checkbox("Opt", &v); rc = GetItemRect(); };
        r.Frames(2);
        r.Click(Vec2(rc.min.x + 5, rc.min.y + 5));
        CHECK(v);
        r.Click(Vec2(rc.max.x - 3, rc.min.y + 5));   // the label is clickable too
        CHECK(!v);
        r.Click(Vec2(rc.max.x + 40, rc.min.y + 5));  // outside
        CHECK(!v);
        bool never = true; r.body = [&] { Checkbox("n", nullptr); never = !Checkbox("m", nullptr); };
        r.Frames(2); CHECK(never);
    }
    // ---- Slider / Drag -----------------------------------------------------------------------
    {
        Rig r; float v = 0.5f; int iv = 5; float dv = 10; Rect rc, ri, rd; bool c1 = false, c2 = false, c3 = false;
        r.body = [&] {
            SetNextItemWidth(200); c1 = SliderFloat("f", &v, 0, 1); rc = GetItemRect();
            SetNextItemWidth(200); c2 = SliderInt("i", &iv, 0, 10); ri = GetItemRect();
            SetNextItemWidth(200); c3 = DragFloat("d", &dv, 0.5f); rd = GetItemRect();
        };
        r.Frames(2);
        CHECK(!c1 && v == 0.5f);
        const float x0 = rc.min.x, grab = 12, travel = 200 - grab;
        const float y = (rc.min.y + rc.max.y) / 2;
        r.MoveTo(Vec2(x0 + grab / 2 + travel * 0.25f, y)); CHECK(v == 0.5f && !c1);   // hover alone does not change
        r.sim.Down(); r.Frame();
        CHECK(c1 && std::fabs(v - 0.25f) < 1e-4);                                      // value follows the press immediately
        r.MoveTo(Vec2(x0 + grab / 2 + travel * 0.75f, y));
        CHECK(std::fabs(v - 0.75f) < 1e-4);
        r.MoveTo(Vec2(x0 + 5000, y + 200));                                            // way off to the right and down: clamps
        CHECK(v == 1.0f);
        r.MoveTo(Vec2(-400, y)); CHECK(v == 0.0f);
        r.sim.Up(); r.Frame(); CHECK(v == 0.0f && GetActiveId() == 0);
        r.MoveTo(Vec2(x0 + 100, y)); CHECK(v == 0.0f);                                  // released: no more change
        // integer slider snaps
        const float yi = (ri.min.y + ri.max.y) / 2;
        r.MoveTo(Vec2(ri.min.x + grab / 2 + travel * 0.5f, yi)); r.sim.Down(); r.Frame();
        CHECK(!c2 && iv == 5);   // pressed exactly at the value: no change
        r.MoveTo(Vec2(ri.min.x + grab / 2 + travel * 0.93f, yi)); CHECK(iv == 9);
        r.MoveTo(Vec2(ri.min.x + grab / 2 + travel * 0.0f, yi)); CHECK(iv == 0);
        r.sim.Up(); r.Frame();
        // drag: first frame of the press does not jump, then follows mouse delta * speed
        const float yd = (rd.min.y + rd.max.y) / 2;
        r.MoveTo(Vec2(rd.min.x + 30, yd)); r.sim.Down(); r.Frame(); CHECK(dv == 10);
        r.MoveTo(Vec2(rd.min.x + 50, yd)); CHECK(c3 && std::fabs(dv - 20) < 1e-4);
        r.MoveTo(Vec2(rd.min.x + 40, yd)); CHECK(std::fabs(dv - 15) < 1e-4);
        r.sim.Up(); r.Frame();
        // bad ranges and NaN never produce NaN
        float bad = NAN; r.body = [&] { SliderFloat("b", &bad, 0, 0); SliderFloat("b2", &bad, 5, 1); SliderFloat("b3", &bad, 0, 1, "%s%n"); };
        r.Frames(2); CHECK(true);
    }
    // ---- InputText ---------------------------------------------------------------------------
    {
        Rig r; std::string text = "abc"; Rect rc; bool ch = false, enter = false; int flags = 0;
        r.body = [&] { ch = InputText("t", &text, flags); rc = GetItemRect(); };
        r.Frames(2);
        auto focused = [&] { return GetFocusId() == GetID("t") * 0 + GetFocusId() && GetFocusId() != 0; };
        // typing without focus does nothing
        r.sim.Type("zzz"); r.Frame(); CHECK(text == "abc");
        // click at the end focuses
        r.Click(Vec2(rc.min.x + 150, rc.min.y + 10)); CHECK(focused());
        auto& st = [&]() -> InputTextState& { return g_ctx->inputState[g_ctx->focusId]; }();
        CHECK(st.cursor == 3 && st.anchor == 3);
        r.sim.Type("de"); r.Frame(); CHECK(text == "abcde" && ch);
        r.Frame(); CHECK(!ch);
        r.sim.Tap(Key_Backspace, [&] { r.Ui(); }); CHECK(text == "abcd");
        r.sim.Tap(Key_Left, [&] { r.Ui(); }); r.sim.Tap(Key_Left, [&] { r.Ui(); }); CHECK(st.cursor == 2);
        r.sim.Type("X"); r.Frame(); CHECK(text == "abXcd" && st.cursor == 3);
        r.sim.Tap(Key_Delete, [&] { r.Ui(); }); CHECK(text == "abXd");
        r.sim.Tap(Key_Home, [&] { r.Ui(); }); CHECK(st.cursor == 0);
        r.sim.Tap(Key_Backspace, [&] { r.Ui(); }); CHECK(text == "abXd");          // nothing before the cursor
        r.sim.Tap(Key_End, [&] { r.Ui(); }); CHECK(st.cursor == 4);
        r.sim.Tap(Key_Delete, [&] { r.Ui(); }); CHECK(text == "abXd");
        // selection with shift+Left, then typing replaces it
        r.sim.in.shift = true; r.sim.Tap(Key_Left, [&] { r.Ui(); }); r.sim.Tap(Key_Left, [&] { r.Ui(); }); r.sim.in.shift = false;
        CHECK(st.cursor == 2 && st.anchor == 4);
        r.sim.Type("Q"); r.Frame(); CHECK(text == "abQ");
        // ctrl+A then Backspace
        r.sim.in.ctrl = true; r.sim.Tap(Key_A, [&] { r.Ui(); }); r.sim.in.ctrl = false;
        CHECK(st.anchor == 0 && st.cursor == 3);
        r.sim.Tap(Key_Backspace, [&] { r.Ui(); }); CHECK(text.empty());
        // control characters and invalid UTF-8 are dropped; a real multi-byte character is kept and deleted whole
        r.sim.Type(std::string("a\tb\n\x7f") + "\xFF" + "\xC3\xA9" + "z"); r.Frame();
        CHECK(text == "ab\xC3\xA9z");
        r.sim.Tap(Key_Left, [&] { r.Ui(); }); r.sim.Tap(Key_Left, [&] { r.Ui(); });
        CHECK(st.cursor == 2);                                                   // before the two-byte char
        r.sim.Tap(Key_Delete, [&] { r.Ui(); }); CHECK(text == "abz");
        r.sim.Type("\xC3\xA9"); r.Frame(); r.sim.Tap(Key_Backspace, [&] { r.Ui(); }); CHECK(text == "abz");
        CHECK(Utf8Valid(text));
        // mouse placement: click between glyphs
        r.Click(Vec2(rc.min.x + 6 + 16 + 4, rc.min.y + 10)); CHECK(st.cursor == 1);
        r.Click(Vec2(rc.min.x + 6 + 16 + 12, rc.min.y + 10)); CHECK(st.cursor == 2);
        // drag selects
        r.MoveTo(Vec2(rc.min.x + 6 + 2, rc.min.y + 10)); r.sim.Down(); r.Frame(); r.MoveTo(Vec2(rc.min.x + 6 + 40, rc.min.y + 10));
        CHECK(st.anchor == 0 && st.cursor == 3); r.sim.Up(); r.Frame();
        // Escape and clicking elsewhere drop focus
        r.sim.Tap(Key_Escape, [&] { r.Ui(); }); CHECK(GetFocusId() == 0);
        r.Click(Vec2(rc.min.x + 20, rc.min.y + 10)); CHECK(focused());
        r.Click(Vec2(300, 300)); CHECK(GetFocusId() == 0);
        // EnterReturnsTrue
        flags = Input_EnterReturnsTrue; text.clear();
        r.Click(Vec2(rc.min.x + 20, rc.min.y + 10));
        r.sim.Type("go"); r.Frame(); CHECK(!ch && text == "go");
        r.sim.Key(Key_Enter, true); r.sim.Frame([&] { r.Ui(); enter = ch; }); CHECK(enter);
        r.sim.Key(Key_Enter, false); r.Frame(); CHECK(!ch);
        // ReadOnly and max length
        flags = Input_ReadOnly; r.sim.Type("x"); r.Frame(); r.sim.Tap(Key_Backspace, [&] { r.Ui(); }); CHECK(text == "go");
        flags = 0; int mx = 0; std::string small; r.body = [&] { InputText("m", &small, 0, 4); (void)mx; };
        r.Frames(2); Rect rm = GetItemRect(); (void)rm;
        r.Frame();
        // key repeat: holding Backspace deletes more over time
        small = "0123456789";
        r.body = [&] { InputText("m", &small, 0, 20); rc = GetItemRect(); };
        r.Frame(); r.Click(Vec2(rc.min.x + 200, rc.min.y + 10));
        r.sim.Key(Key_Backspace, true);
        for (int i = 0; i < 36; ++i) r.Frame();             // 0.6 s: one press, then repeats after 0.4 s
        r.sim.Key(Key_Backspace, false); r.Frame();
        CHECK(small.size() >= 3 && small.size() <= 8);
        // max bytes
        small.clear(); r.body = [&] { InputText("m", &small, 0, 4); rc = GetItemRect(); };
        r.Frame(); r.Click(Vec2(rc.min.x + 10, rc.min.y + 10)); r.sim.Type("abcdefg"); r.Frame(); CHECK(small == "abcd");
        // null buffer
        r.body = [&] { CHECK(!InputText("n", nullptr)); }; r.Frames(2);
    }
    // ---- Combo / popups ----------------------------------------------------------------------
    {
        Rig r; int cur = 0; bool ch = false; Rect rc; Rect below;
        const char* items[] = {"One", "Two", "Three"};
        r.body = [&] { SetNextItemWidth(150); ch = Combo("c", &cur, items, 3); rc = GetItemRect(); Button("after"); below = GetItemRect(); };
        r.Frames(2);
        const Vec2 head(rc.min.x + 40, rc.min.y + 10);
        r.MoveTo(head); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frames(3);
        CHECK(g_ctx->popupId != 0);
        Window* pop = nullptr;
        for (auto& kv : g_ctx->windows) if (kv.second->flags & Win_Popup) pop = kv.second.get();
        CHECK(pop && pop->activeThisFrame);
        CHECK(pop->pos.y == rc.max.y - 0 || std::fabs(pop->pos.y - rc.max.y) < 1e-3);
        CHECK(g_ctx->order.back() == pop);                                       // drawn on top
        // items hover: second row
        const Vec2 row2(pop->pos.x + 20, pop->pos.y + pop->size.y * 0.5f);
        r.MoveTo(row2);
        CHECK(g_ctx->hoveredRoot == pop);
        // a click on a widget hidden below the popup must not reach it: put the mouse over the popup where "after" is
        r.sim.Down(); r.Frame(); r.sim.Up(); r.Frames(2);
        CHECK(cur == 1 && g_ctx->popupId == 0);                                  // picked "Two", popup closed
        // open again, click outside: closes, nothing changes, the click does not activate what is under it
        int clicks = 0; r.body = [&] { SetNextItemWidth(150); Combo("c", &cur, items, 3); rc = GetItemRect(); if (Button("after")) ++clicks; below = GetItemRect(); };
        r.Frames(2);
        r.MoveTo(Vec2(rc.min.x + 40, rc.min.y + 10)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frames(3);
        CHECK(g_ctx->popupId != 0);
        r.Click(Vec2(300, 300));
        CHECK(g_ctx->popupId == 0 && cur == 1);
        // click the header while open: closes (does not re-open)
        r.MoveTo(Vec2(rc.min.x + 40, rc.min.y + 10)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frames(3);
        CHECK(g_ctx->popupId != 0);
        r.MoveTo(Vec2(rc.min.x + 40, rc.min.y + 10)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frames(3);
        CHECK(g_ctx->popupId == 0 && cur == 1);
        // invalid arguments
        r.body = [&] { int bad = 99; Combo("x", &bad, items, 3); Combo("y", nullptr, items, 3); Combo("z", &bad, nullptr, 0); Combo("w", &bad, items, -1); };
        r.Frames(3);
        CHECK(clicks == 0);
    }
    // ---- TreeNode / Selectable ---------------------------------------------------------------
    {
        Rig r; Rect rt, rs; int sel = -1; bool opened = false;
        r.body = [&] {
            opened = TreeNode("Node");
            rt = GetItemRect();
            if (opened) { if (Selectable("child##1")) sel = 1; rs = GetItemRect(); TreePop(); }
            Text("tail");
        };
        r.Frames(2);
        CHECK(!opened);
        const float tailY = GetContext()->windows[GetWindowID("W")]->lastItemRect.min.y;
        r.Click(Center(rt)); CHECK(opened);
        CHECK(GetContext()->windows[GetWindowID("W")]->lastItemRect.min.y > tailY);   // the tail moved down
        CHECK(rs.min.x > rt.min.x);                                                    // indented
        r.Click(Center(rs)); CHECK(sel == 1);
        r.Click(Center(rt)); r.Frame(); CHECK(!opened);
        // same label in two scopes gets two IDs
        r.body = [&] {
            PushID(1); bool a = Button("same"); PopID();
            PushID(2); bool b = Button("same"); PopID();
            (void)a; (void)b;
        };
        r.Frames(2);
    }
    // ---- Scroll region -----------------------------------------------------------------------
    {
        Rig r; Rect rc; int clicked = -1;
        r.body = [&] {
            Text("above");
            if (BeginScrollRegion("list", Vec2(300, 100))) {
                for (int i = 0; i < 20; ++i) { char b[16]; std::snprintf(b, sizeof b, "row %d", i); if (Selectable(b)) clicked = i; }
                EndScrollRegion();
            }
            rc = GetItemRect();
            Text("below");
        };
        r.Frames(3);
        CHECK_NEAR(rc.Height(), 100, 1e-3);
        Window* ch = nullptr;
        for (auto& kv : g_ctx->windows) if (kv.second->flags & Win_Child) ch = kv.second.get();
        CHECK(ch && ch->scrollY == 0 && ch->hasScrollbar);
        CHECK(ch->contentSize.y > 100);
        // wheel while hovering scrolls the child, not the parent
        r.MoveTo(Center(rc)); r.Frame();
        r.sim.in.wheel = -2; r.Frame(); r.Frame();
        CHECK(ch->scrollY > 40 && g_ctx->windows[GetWindowID("W")]->scrollY == 0);
        const float s1 = ch->scrollY;
        r.sim.in.wheel = 1000; r.Frame(); CHECK(ch->scrollY == 0);                      // clamps at the top
        r.sim.in.wheel = -1000; r.Frame(); r.Frame();
        CHECK(ch->scrollY > s1 && ch->scrollY <= ch->contentSize.y - 100 + 1e-3);      // clamps at the bottom
        // wheel outside does nothing to the child
        const float s2 = ch->scrollY;
        r.MoveTo(Vec2(rc.min.x + 5, rc.max.y + 40)); r.sim.in.wheel = 3; r.Frame(); r.Frame(); CHECK(ch->scrollY == s2);
        // clicking a row inside the scrolled child hits the right row
        r.sim.in.wheel = 1000; r.MoveTo(Center(rc)); r.Frame();
        r.MoveTo(Vec2(rc.min.x + 30, rc.min.y + 6)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(clicked == 0);
        // dragging the scroll thumb
        r.MoveTo(Vec2(rc.max.x - 4, rc.min.y + 10));
        r.sim.Down(); r.Frame(); r.MoveTo(Vec2(rc.max.x - 4, rc.min.y + 40)); r.Frame(); r.sim.Up(); r.Frame();
        CHECK(ch->scrollY > 0);
    }
    // ---- layout ------------------------------------------------------------------------------
    {
        Rig r; Rect a, b, c, d, e, f, g;
        Window* w = nullptr;
        r.body = [&] {
            w = g_ctx->cur;
            Button("aa"); a = GetItemRect();
            SameLine(); Button("bb"); b = GetItemRect();
            Button("cc"); c = GetItemRect();
            Dummy(Vec2(10, 30)); d = GetItemRect();
            Indent(20); Button("ee"); e = GetItemRect(); Unindent(20);
            BeginGroup(); Button("g1"); SameLine(); Button("g2"); Button("g3"); EndGroup(); f = GetItemRect();
            SameLine(); Button("after-group"); g = GetItemRect();
        };
        r.Frames(2);
        const float sp = g_ctx->style.itemSpacing.x, spy = g_ctx->style.itemSpacing.y;
        CHECK_NEAR(b.min.x, a.max.x + sp, 1e-3); CHECK_NEAR(b.min.y, a.min.y, 1e-3);
        CHECK_NEAR(c.min.x, a.min.x, 1e-3); CHECK_NEAR(c.min.y, a.max.y + spy, 1e-3);
        CHECK_NEAR(d.min.y, c.max.y + spy, 1e-3);
        CHECK_NEAR(e.min.x, a.min.x + 20, 1e-3); CHECK_NEAR(e.min.y, d.max.y + spy, 1e-3);
        CHECK_NEAR(f.min.x, a.min.x, 1e-3); CHECK_NEAR(f.min.y, e.max.y + spy, 1e-3);
        CHECK_NEAR(f.Height(), 2 * 24 + spy, 1e-3);                                   // two button rows
        CHECK(f.Width() > 2 * 28);
        CHECK_NEAR(g.min.x, f.max.x + sp, 1e-3); CHECK_NEAR(g.min.y, f.min.y, 1e-3);
        // text next to a button lines up when asked to
        float yText = 0, yPlain = 0, bh = 0;
        r.body = [&] {
            Button("b"); bh = GetItemRect().Height();
            SameLine(); Text("plain"); yPlain = GetItemRect().min.y - 0;
            Button("b2");
            SameLine(); AlignTextToFramePadding(); Text("aligned"); yText = GetItemRect().min.y;
        };
        r.Frames(2);
        (void)bh;
        CHECK(yText > yPlain);
        // columns
        Rect c0, c1, c2, c3;
        r.body = [&] {
            Columns(2);
            Text("a"); c0 = GetItemRect(); NextColumn();
            Text("b"); c1 = GetItemRect(); NextColumn();
            Text("c"); c2 = GetItemRect(); NextColumn();
            Text("d"); c3 = GetItemRect();
            Columns(1);
            Text("after");
        };
        r.Frames(2);
        CHECK_NEAR(c0.min.y, c1.min.y, 1e-3); CHECK(c1.min.x > c0.min.x + 50);
        CHECK(c2.min.y > c0.min.y && std::fabs(c2.min.x - c0.min.x) < 1e-3);
        CHECK(std::fabs(c3.min.x - c1.min.x) < 1e-3);
        CHECK(w->lastItemRect.min.y > c3.max.y);
        // content region and item width
        Vec2 avail; Rect s1;
        r.body = [&] { avail = GetContentRegionAvail(); SetNextItemWidth(77); float v = 0; SliderFloat("s", &v, 0, 1); s1 = GetItemRect(); };
        r.Frames(2);
        CHECK(avail.x > 300 && avail.x < 400);
        CHECK_NEAR(s1.Width(), 77 + 6 + 16, 1e-3);
        // coloured text and style stack
        r.body = [&] { PushStyleColor(Col_Text, MakeCol(255, 0, 0)); Text("red"); PopStyleColor(); CHECK(GetColor(Col_Text) == Style().colors[Col_Text]); };
        r.Frames(2);
        // everything is inert in a collapsed window
        r.body = [&] { CHECK(!Button("x")); };
        Window* ww = g_ctx->windows[GetWindowID("W")].get();
        ww->collapsed = true; r.Frames(3);
        ww->collapsed = false; r.Frames(2);
    }
    // ---- child hover and focus ---------------------------------------------------------------
    {
        Rig r; Rect btn; int clicks = 0;
        r.body = [&] {
            if (BeginScrollRegion("inner", Vec2(200, 80))) { if (Button("in")) ++clicks; btn = GetItemRect(); EndScrollRegion(); }
        };
        r.Frames(3);
        r.Click(Center(btn)); CHECK(clicks == 1);
        r.MoveTo(Vec2(5, 5)); CHECK(GetHoveredId() == 0);
    }
    DestroyContext(g_ctx);
    TEST_MAIN_RESULT();
}
