// Controller / keyboard navigation of UI (the focus graph: geometry, wrap, tab order, traps,
// links, and the scene adapter next to NavigateUI) and the controls-screen model (rebindable
// actions, conflicts and their resolution).
#include "test_framework.hpp"
#include "okay/Components/UINavigation.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/RemapModel.hpp"
#include "okay/UI/FocusGraph.hpp"
#include <Okay.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace okay;

namespace {

// A 3 x 3 grid of 80 x 40 buttons, 100 x 60 apart; ids 1..9 row by row.
void BuildGrid(FocusGraph& g) {
    g.Clear();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) g.Add((float)c * 100.0f, (float)r * 60.0f, 80.0f, 40.0f);
}

const RemapRow* Row(const std::vector<RemapRow>& rows, const std::string& name) {
    for (const auto& r : rows) if (r.action == name) return &r;
    return nullptr;
}

std::string Current(const char* action, bool pad) {
    const InputBinding* b = InputActions::Current(action, pad);
    return b ? b->ToString() : std::string("-");
}

bool Has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

} // namespace

int main() {
    RUN_SUITE("focus_remap");

    // ---- Directional moves on a grid ---------------------------------------------------------------
    {
        FocusGraph g;
        BuildGrid(g);
        CHECK(g.Size() == 9 && g.Find(5)->x == 100.0f && g.Find(5)->y == 60.0f);
        CHECK(g.Move(5, FocusDir::Right) == 6 && g.Move(5, FocusDir::Left) == 4);
        CHECK(g.Move(5, FocusDir::Up) == 2 && g.Move(5, FocusDir::Down) == 8);
        CHECK(g.Move(1, FocusDir::Down) == 4 && g.Move(9, FocusDir::Left) == 8);

        // Past the edge: the opposite side of the same row or column.
        CHECK(g.Move(1, FocusDir::Left) == 3 && g.Move(3, FocusDir::Right) == 1);
        CHECK(g.Move(1, FocusDir::Up) == 7 && g.Move(9, FocusDir::Down) == 3);
        CHECK(g.Move(4, FocusDir::Left) == 6 && g.Move(8, FocusDir::Down) == 2);

        FocusOptions none;
        none.vertical = none.horizontal = FocusWrap::None;
        CHECK(g.Move(3, FocusDir::Right, none) == 3 && g.Move(9, FocusDir::Down, none) == 9 && g.Move(1, FocusDir::Up, none) == 1);
        CHECK(g.Move(5, FocusDir::Right, none) == 6);                              // inside the layout nothing changes

        FocusOptions reading;
        reading.vertical = reading.horizontal = FocusWrap::ReadingOrder;
        CHECK(g.Move(9, FocusDir::Down, reading) == 1 && g.Move(1, FocusDir::Up, reading) == 9);
        CHECK(g.Move(9, FocusDir::Right, reading) == 1 && g.Move(1, FocusDir::Left, reading) == 9);

        // Mixed: vertical wraps, horizontal stays (what NavigateUI does for Left / Right).
        FocusOptions mixed;
        mixed.horizontal = FocusWrap::None;
        CHECK(g.Move(3, FocusDir::Right, mixed) == 3 && g.Move(9, FocusDir::Down, mixed) == 3);

        // A disabled button is skipped: the nearest one that way, even off the row, else the far side.
        CHECK(g.SetEnabled(6, false) && !g.SetEnabled(99, false));
        CHECK(g.Move(5, FocusDir::Right) == 3);                                      // 6 is gone: the nearest to the right is the one above it
        CHECK(g.Move(3, FocusDir::Down) == 5);                                       // and the diagonal beats the far one straight below
        CHECK(g.Candidates().size() == 8);
        g.SetEnabled(3, false);
        g.SetEnabled(9, false);
        CHECK(g.Move(5, FocusDir::Right) == 4);                                      // nothing to the right: the row's far side
        CHECK(g.Move(5, FocusDir::Right, none) == 5);
        g.SetEnabled(3, true);
        g.SetEnabled(6, true);
        g.SetEnabled(9, true);

        // A single button: nowhere to go.
        FocusGraph one;
        const int only = one.Add(10, 10, 50, 20);
        CHECK(one.Move(only, FocusDir::Down) == only && one.Move(only, FocusDir::Next) == only && one.Move(only, FocusDir::Left) == only);
        FocusGraph empty;
        CHECK(empty.Move(0, FocusDir::Down) == 0 && empty.Navigate(FocusDir::Next) == 0 && empty.First() == 0 && empty.Last() == 0);
    }

    // ---- Geometry that isn't a grid -------------------------------------------------------------------------
    {
        // The widget straight below beats a closer one off to the side.
        FocusGraph g;
        const int f = g.Add(0, 0, 80, 40);
        const int side = g.Add(200, 50, 80, 40);      // nearer diagonally, but far to the right
        const int below = g.Add(0, 100, 80, 40);
        CHECK(g.Move(f, FocusDir::Down) == below && g.Move(f, FocusDir::Right) == side);
        CHECK(FocusScore(*g.Find(f), *g.Find(below), 0, 1) < FocusScore(*g.Find(f), *g.Find(side), 0, 1));
        CHECK(FocusScore(*g.Find(f), *g.Find(side), 0, -1) < 0.0f);                  // not ahead: no score
        CHECK(FocusScore(*g.Find(f), *g.Find(f), 0, 1) < 0.0f);

        // A wide button under two small ones is reached from either; going up lands on one of them.
        FocusGraph w;
        const int a = w.Add(0, 0, 80, 40), b = w.Add(100, 0, 80, 40), wide = w.Add(0, 100, 180, 40);
        CHECK(w.Move(a, FocusDir::Down) == wide && w.Move(b, FocusDir::Down) == wide);
        const int up = w.Move(wide, FocusDir::Up);
        CHECK(up == a || up == b);
        CHECK(w.Move(a, FocusDir::Right) == b && w.Move(b, FocusDir::Left) == a);

        // Sliding rows: each row's own neighbour, not a button further along.
        FocusGraph s;
        const int r1a = s.Add(0, 0, 60, 30), r1b = s.Add(70, 0, 60, 30);
        const int r2a = s.Add(20, 40, 60, 30), r2b = s.Add(90, 40, 60, 30);
        CHECK(s.Move(r1b, FocusDir::Down) == r2b || s.Move(r1b, FocusDir::Down) == r2a);
        CHECK(s.Move(r1a, FocusDir::Down) == r2a && s.Move(r2b, FocusDir::Up) == r1b);
    }

    // ---- Tab order -----------------------------------------------------------------------------------------------------
    {
        FocusGraph g;
        FocusNode n;
        n.w = 80; n.h = 40;
        n.x = 0;   n.y = 0;   n.tabIndex = 2;  const int a = g.Add(n);
        n.x = 100; n.y = 0;   n.tabIndex = 1;  const int b = g.Add(n);
        n.x = 0;   n.y = 60;  n.tabIndex = 0;  const int c = g.Add(n);
        n.x = 100; n.y = 62;  n.tabIndex = 0;  const int d = g.Add(n);        // a few pixels lower: still the same row as c
        n.x = 200; n.y = 60;  n.tabIndex = -1; const int skipped = g.Add(n);   // arrows reach it, Tab doesn't
        CHECK(g.TabOrder() == std::vector<int>({b, a, c, d}));
        CHECK(g.ReadingOrder() == std::vector<int>({a, b, c, d, skipped}));
        CHECK(g.First() == b && g.Last() == d);
        CHECK(g.Move(b, FocusDir::Next) == a && g.Move(a, FocusDir::Next) == c && g.Move(c, FocusDir::Next) == d);
        CHECK(g.Move(d, FocusDir::Next) == b);                                   // wraps
        CHECK(g.Move(b, FocusDir::Previous) == d && g.Move(c, FocusDir::Previous) == a);
        FocusOptions stay;
        stay.tabWraps = false;
        CHECK(g.Move(d, FocusDir::Next, stay) == d && g.Move(b, FocusDir::Previous, stay) == b);
        FocusOptions noWrap;
        noWrap.vertical = noWrap.horizontal = FocusWrap::None;
        CHECK(g.Move(d, FocusDir::Next, noWrap) == d);                           // no wrapping anywhere: Tab doesn't either
        CHECK(g.Move(d, FocusDir::Right) == skipped);                            // reached with arrows
        CHECK(g.Move(skipped, FocusDir::Next) == b);                             // Tab from outside the order starts at its first
        CHECK(g.Move(skipped, FocusDir::Previous) == d);
        g.SetEnabled(a, false);
        CHECK(g.TabOrder() == std::vector<int>({b, c, d}));

        // Focus state: nothing focused yet, Down starts at the first, Up at the last.
        g.SetEnabled(a, true);
        CHECK(g.Focus() == 0 && g.Navigate(FocusDir::Down) == b && g.Focus() == b);
        g.ClearFocus();
        CHECK(g.Navigate(FocusDir::Up) == d);
        CHECK(g.Navigate(FocusDir::Previous) == c && g.Navigate(FocusDir::Next) == d);
        CHECK(g.SetFocus(a) && g.Focus() == a && !g.SetFocus(999));
        CHECK(g.Focus() == a);
        g.SetEnabled(a, false);
        CHECK(g.Focus() == 0);                                                   // a disabled widget keeps no focus
        CHECK(!g.SetFocus(a));
        g.SetEnabled(a, true);
        g.SetFocus(b);
        CHECK(g.Remove(b) && g.Focus() == 0 && !g.Remove(b) && g.Size() == 4);
        // Re-adding with an id replaces that node.
        FocusNode again;
        again.id = a; again.x = 500; again.w = 10; again.h = 10;
        CHECK(g.Add(again) == a && g.Size() == 4 && g.Find(a)->x == 500.0f);
        FocusNode fresh;
        CHECK(g.Add(fresh) > a);
    }

    // ---- Explicit links ----------------------------------------------------------------------------------------------------------
    {
        FocusGraph g;
        BuildGrid(g);
        CHECK(g.SetLink(3, FocusDir::Right, 7) && g.Move(3, FocusDir::Right) == 7);
        CHECK(g.SetLink(5, FocusDir::Up, -1) && g.Move(5, FocusDir::Up) == 5);       // blocked
        CHECK(g.Move(5, FocusDir::Down) == 8);                                       // other directions still use the geometry
        g.SetEnabled(7, false);
        CHECK(g.Move(3, FocusDir::Right) == 1);                                      // a link to a disabled node is ignored
        CHECK(!g.SetLink(3, FocusDir::Next, 1) && !g.SetLink(99, FocusDir::Up, 1));
        CHECK(g.SetLink(5, FocusDir::Up, 0) && g.Move(5, FocusDir::Up) == 2);        // 0 gives the geometry back
    }

    // ---- Focus traps (modals) ----------------------------------------------------------------------------------------------------------------
    {
        FocusGraph g;
        BuildGrid(g);                                                // page: ids 1..9, group 0
        const int ok = g.Add(300, 300, 80, 40, 1);                   // a modal's buttons: group 1
        const int cancel = g.Add(400, 300, 80, 40, 1);
        const int more = g.Add(300, 360, 80, 40, 1);
        g.SetFocus(5);
        CHECK(!g.Trapped() && g.ActiveTrap() == -1);
        g.PushTrap(1);
        CHECK(g.Trapped() && g.ActiveTrap() == 1 && g.Focus() == ok);                // focus moved into the modal
        CHECK(!g.SetFocus(2));                                                       // the page behind can't take focus
        CHECK(g.Candidates().size() == 3);
        CHECK(g.Navigate(FocusDir::Right) == cancel && g.Navigate(FocusDir::Right) == ok);   // wraps inside the modal
        CHECK(g.Navigate(FocusDir::Down) == more && g.Navigate(FocusDir::Down) == ok);
        CHECK(g.Navigate(FocusDir::Next) == cancel);
        for (int i = 0; i < 20; ++i) {
            const int f = g.Navigate(i % 2 ? FocusDir::Left : FocusDir::Up);
            CHECK(f == ok || f == cancel || f == more);                              // never leaves
        }
        CHECK(g.TabOrder().size() == 3);

        // A second modal on top of the first.
        const int inner = g.Add(10, 400, 80, 40, 2);
        g.SetFocus(cancel);
        g.PushTrap(2);
        CHECK(g.Focus() == inner && g.Navigate(FocusDir::Down) == inner && g.Navigate(FocusDir::Next) == inner);
        CHECK(g.PopTrap() == cancel && g.ActiveTrap() == 1);                         // back to where it was in the first modal
        CHECK(g.PopTrap() == 5 && !g.Trapped() && g.Focus() == 5);                   // and out to the page
        CHECK(g.PopTrap() == 5);                                                     // nothing to pop

        // A trap whose controls are gone has nothing to focus.
        g.PushTrap(7);
        CHECK(g.Focus() == 0 && g.Move(0, FocusDir::Down) == 0 && g.Navigate(FocusDir::Next) == 0);
        CHECK(g.PopTrap() == 5);                                                     // focus comes back to where it was

        // The remembered node is gone by the time the modal closes: the first node takes focus.
        g.PushTrap(1);
        g.Remove(5);
        const int back = g.PopTrap();
        CHECK(back == g.First() && back != 0 && g.Focus() == back);
    }

    // ---- The scene adapter agrees with NavigateUI ------------------------------------------------------------------------------------------------
    {
        UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 800, 600, o, sz); };
        UIWorld().screenW = 800; UIWorld().screenH = 600;
        Scene s("nav");
        auto button = [&](const char* name, Vec2 pos) {
            GameObject* g = s.CreateGameObject(name);
            auto* b = g->AddComponent<UIButton>();
            b->position = pos;
            b->size = {100, 40};
            return b;
        };
        UIButton* b1 = button("b1", {20, 20});
        UIButton* b2 = button("b2", {160, 20});
        UIButton* b3 = button("b3", {20, 100});
        UIButton* b4 = button("b4", {160, 100});
        UIButton* hidden = button("hidden", {300, 20});
        UIButton* off = button("disabled", {300, 100});
        hidden->gameObject->active = false;
        off->interactable = false;
        s.CreateGameObject("plain");                                  // no widget: not focusable
        UpdateUILayouts(s);

        FocusGraph g;
        const std::vector<GameObject*> objs = BuildFocusGraph(s, g);
        CHECK(objs.size() == 4 && g.Size() == 4);
        CHECK(objs[0] == b1->gameObject && objs[3] == b4->gameObject);
        CHECK(g.Find(1)->x == 20.0f && g.Find(1)->w == 100.0f && g.Find(2)->x == 160.0f && g.Find(3)->y == 100.0f);
        CHECK(IsNavFocusable(b1->gameObject) && !IsNavFocusable(off->gameObject));
        CHECK(g.Move(1, FocusDir::Right) == 2 && g.Move(1, FocusDir::Down) == 3 && g.Move(4, FocusDir::Up) == 2);

        // The same moves through NavigateUI.
        Input::FeedKeys({});
        Input::FeedKeys({'d'});
        NavigateUI(s);                                                // first press focuses the first widget
        CHECK(b1->IsFocused());
        Input::FeedKeys({});
        Input::FeedKeys({'d'});
        NavigateUI(s);
        CHECK(b2->IsFocused());
        Input::FeedKeys({});
        Input::FeedKeys({'s'});
        NavigateUI(s);
        CHECK(b4->IsFocused() && g.Move(2, FocusDir::Down) == 4);
        Input::FeedKeys({});
        Input::FeedKeys({'a'});
        NavigateUI(s);
        CHECK(b3->IsFocused() && g.Move(4, FocusDir::Left) == 3);
        Input::FeedKeys({});

        // Tab order and links on the scene's graph.
        g.SetLink(1, FocusDir::Right, 4);
        CHECK(g.Move(1, FocusDir::Right) == 4);
        CHECK(g.TabOrder() == std::vector<int>({1, 2, 3, 4}));
    }

    // ---- The controls list -------------------------------------------------------------------------------------------------------------------------------
    {
        Prefs::Clear();
        InputActions::Restore();
        const std::vector<RemapRow> rows = RemapRows();
        CHECK(rows.size() >= 11);
        const RemapRow* jump = Row(rows, "Jump");
        CHECK(jump && jump->keyboard.bound && jump->keyboard.label == "Space" && jump->gamepad.label == "A");
        CHECK(jump->keyboard.isDefault && jump->gamepad.isDefault && !jump->Changed() && !jump->direction && !jump->reserved);
        const RemapRow* move = Row(rows, "Move");
        CHECK(move && move->direction && move->keyboard.label == "W A S D" && move->gamepad.label == "Left Stick");
        CHECK(move->keyboard.partLabels == std::vector<std::string>({"W", "S", "A", "D"}));
        const RemapRow* look = Row(rows, "Look");
        CHECK(look && !look->keyboard.bound && look->keyboard.label.empty() && look->gamepad.label == "Right Stick");
        CHECK(RemapDirectionChoices() == std::vector<std::string>({"stick:left", "stick:right", "dpad"}));
        RemapOptions opt;
        opt.hidden = {"look", "INVENTORY"};
        opt.reserved = {"Pause"};
        const std::vector<RemapRow> some = RemapRows(opt);
        CHECK(some.size() == rows.size() - 2 && !Row(some, "Look") && !Row(some, "Inventory"));
        CHECK(Row(some, "Pause")->reserved && !Row(some, "Jump")->reserved);

        // Who already uses an input
        auto conflicts = RemapConflicts("Jump", "key:e");
        CHECK(conflicts.size() == 1 && conflicts[0].action == "Interact" && conflicts[0].binding == "key:e" && conflicts[0].part == -1 && !conflicts[0].gamepad);
        conflicts = RemapConflicts("Jump", "key:w");
        CHECK(conflicts.size() == 1 && conflicts[0].action == "Move" && conflicts[0].part == 0 && conflicts[0].binding == "keys:w,s,a,d");
        conflicts = RemapConflicts("Jump", "key:right");
        CHECK(conflicts.size() == 1 && conflicts[0].action == "Move" && conflicts[0].part == 3);
        conflicts = RemapConflicts("Jump", "pad:up");
        CHECK(conflicts.size() == 1 && conflicts[0].action == "Move" && conflicts[0].binding == "dpad" && conflicts[0].gamepad);
        CHECK(RemapConflicts("Jump", "pad:b").size() == 1 && RemapConflicts("Fire", "mouse:right")[0].action == "Aim");
        CHECK(RemapConflicts("Jump", "key:space").empty() && RemapConflicts("Jump", "key:j").empty());   // its own key, a free key
        CHECK(RemapConflicts("Jump", "key:nonsense").empty() && RemapConflicts("Jump", "").empty());
        CHECK(RemapConflicts("Look", "stick:left").size() == 1);                                          // Move uses the left stick
        CHECK(RemapConflicts("Jump", "key:escape", -1, opt)[0].reserved);

        // Overlap rules
        InputBinding w = InputBinding::Parse("key:w"), keys4 = InputBinding::Parse("keys:w,s,a,d"), dpad = InputBinding::Parse("dpad");
        CHECK(RemapOverlap(w, keys4) && RemapOverlap(keys4, w) && RemapOverlap(keys4, InputBinding::Parse("keys:s,x,y,z")));
        CHECK(!RemapOverlap(w, InputBinding::Parse("key:e")) && !RemapOverlap(w, InputBinding::Parse("pad:a")));
        CHECK(RemapOverlap(dpad, InputBinding::Parse("pad:left")) && !RemapOverlap(dpad, InputBinding::Parse("pad:a")));
        CHECK(RemapOverlap(InputBinding::Parse("stick:left"), InputBinding::Parse("stick:left")) && !RemapOverlap(InputBinding::Parse("stick:left"), InputBinding::Parse("stick:right")));
        CHECK(!RemapOverlap(InputBinding{}, InputBinding{}));
        CHECK(RemapAllConflicts().empty());                                                               // the defaults clash with nothing

        // Cancel leaves everything alone
        RemapResult r = RemapBind("Jump", "key:e");
        CHECK(!r.ok && r.error.find("Interact") != std::string::npos && r.conflicts.size() == 1 && r.changed.empty());
        CHECK(Current("Jump", false) == "key:space" && Current("Interact", false) == "key:e");

        // A free key just rebinds, and is saved
        r = RemapBind("Jump", "key:j");
        CHECK(r.ok && r.conflicts.empty() && r.changed == std::vector<std::string>({"Jump"}));
        CHECK(Current("Jump", false) == "key:j" && Prefs::GetString("input.jump") == "key:j;pad:a");
        CHECK(Row(RemapRows(), "Jump")->Changed() && !Row(RemapRows(), "Jump")->keyboard.isDefault && Row(RemapRows(), "Jump")->gamepad.isDefault);
        Prefs::Clear();
        InputActions::Restore();

        // Swap: the other action gets what this one gave up
        r = RemapBind("Jump", "key:e", RemapResolve::Swap);
        CHECK(r.ok && r.changed == std::vector<std::string>({"Jump", "Interact"}));
        CHECK(Current("Jump", false) == "key:e" && Current("Interact", false) == "key:space");
        CHECK(Current("Jump", true) == "pad:a" && Current("Interact", true) == "pad:x");                  // the controller side is untouched
        CHECK(Prefs::GetString("input.interact") == "key:space;pad:x" && Prefs::GetString("input.jump") == "key:e;pad:a");
        CHECK(RemapAllConflicts().empty());
        Prefs::Clear();
        InputActions::Restore();

        // Swap with a key that belongs to a four-key direction: that direction's key changes
        r = RemapBind("Jump", "key:w", RemapResolve::Swap);
        CHECK(r.ok && Current("Jump", false) == "key:w" && Current("Move", false) == "keys:space,s,a,d" && r.changed.size() == 2);
        Prefs::Clear();
        InputActions::Restore();

        // Unbind: the other action loses it, as long as it keeps another
        r = RemapBind("Fire", "key:e", RemapResolve::Unbind);
        CHECK(r.ok && Current("Fire", false) == "key:e" && Current("Interact", false) == "-" && Current("Interact", true) == "pad:x");
        CHECK(Has(r.changed, "Fire") && Has(r.changed, "Interact"));
        CHECK(Prefs::GetString("input.interact") == "pad:x");
        Prefs::Clear();
        InputActions::Restore();
        InputActions::Define("Solo", {"key:q"});
        r = RemapBind("Jump", "key:q", RemapResolve::Unbind);
        CHECK(!r.ok && r.error.find("no binding") != std::string::npos && Current("Jump", false) == "key:space" && Current("Solo", false) == "key:q");

        // Allow: both use it, and the screen can mark the pair
        r = RemapBind("Jump", "key:e", RemapResolve::Allow);
        CHECK(r.ok && Current("Jump", false) == "key:e" && Current("Interact", false) == "key:e");
        const auto dups = RemapAllConflicts();
        CHECK(dups.size() == 1 && dups[0].first == "Jump" && dups[0].second == "Interact" && dups[0].binding == "E");
        Prefs::Clear();
        InputActions::Restore();

        // One key of a four-key direction: the direction's own keys are in the way too
        r = RemapBind("Move", "key:w", RemapResolve::Cancel, 1);                   // down := w, but up is w
        CHECK(!r.ok && r.conflicts.size() == 1 && r.conflicts[0].action == "Move" && r.conflicts[0].part == 0);
        CHECK(Current("Move", false) == "keys:w,s,a,d");
        r = RemapBind("Move", "key:w", RemapResolve::Swap, 1);
        CHECK(r.ok && Current("Move", false) == "keys:s,w,a,d");
        r = RemapBind("Move", "key:e", RemapResolve::Swap, 2);                     // left := e, which Interact has
        CHECK(r.ok && Current("Move", false) == "keys:s,w,e,d" && Current("Interact", false) == "key:a");
        r = RemapBind("Move", "key:p", RemapResolve::Cancel, 0);
        CHECK(r.ok && Current("Move", false) == "keys:p,w,e,d");
        CHECK(!RemapBind("Jump", "key:p", RemapResolve::Allow, 0).ok);             // Jump has no four-key direction
        CHECK(!RemapBind("Move", "pad:a", RemapResolve::Allow, 0).ok);             // a part is a key
        CHECK(!RemapBind("Move", "key:p", RemapResolve::Allow, 7).ok);
        Prefs::Clear();
        InputActions::Restore();

        // Reserved actions
        r = RemapBind("Pause", "key:p", RemapResolve::Allow, -1, opt);
        CHECK(!r.ok && r.error.find("can't be rebound") != std::string::npos && Current("Pause", false) == "key:escape");
        r = RemapBind("Jump", "key:escape", RemapResolve::Swap, -1, opt);
        CHECK(!r.ok && r.error.find("Pause") != std::string::npos && Current("Jump", false) == "key:space");
        r = RemapBind("Jump", "key:escape", RemapResolve::Unbind, -1, opt);
        CHECK(!r.ok && Current("Pause", false) == "key:escape");
        r = RemapBind("Jump", "key:escape", RemapResolve::Allow, -1, opt);        // allowed on request
        CHECK(r.ok);
        Prefs::Clear();
        InputActions::Restore();

        // Mistakes
        CHECK(!RemapBind("NoSuchAction", "key:j").ok && RemapBind("NoSuchAction", "key:j").error.find("no action") != std::string::npos);
        CHECK(!RemapBind("Jump", "banana").ok && Current("Jump", false) == "key:space");
        CHECK(!RemapBind("Jump", "").ok);

        // Controller side: a button, then a stick for a direction action
        r = RemapBind("Jump", "pad:y", RemapResolve::Swap);
        CHECK(r.ok && Current("Jump", true) == "pad:y" && Current("Reload", true) == "pad:a" && Current("Jump", false) == "key:space");
        r = RemapBind("Move", "stick:right", RemapResolve::Cancel);
        CHECK(!r.ok && r.conflicts[0].action == "Look");
        Prefs::Clear();
        InputActions::Restore();
    }

    Prefs::Clear();
    InputActions::Restore();
    TEST_MAIN_RESULT();
}
