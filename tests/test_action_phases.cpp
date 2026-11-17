// Interactions and phases (ActionSet): press, release, hold, tap, multi-tap, repeat, chorded, default behaviour, callbacks,
// plus the input buffer and coyote time helpers. Real Input fed by hand; pictures with OKAY_SHOT_DIR (software render).
#include "test_framework.hpp"
#include "shot_support.hpp"
#include "okay/Input/ActionSet.hpp"
#include "okay/Input/InputBuffer.hpp"
#include <cmath>
#include <map>

using namespace okay;

namespace {
constexpr float DT = 0.05f;
void Reset() { Input::FeedKeys({}); Input::FeedMouse({400, 300}, 0); }
struct Log { std::vector<std::string> ev; };
// Hold key 'x' for `frames` frames (0 = released), step the set and collect "action:phase" strings.
void Step(ActionSet& s, bool x, std::vector<std::string>& out, bool mod = false) {
    std::vector<char> k; if (x) k.push_back('x'); if (mod) k.push_back('m');
    Input::FeedKeys(k);
    s.Update(DT);
    for (const PhaseEvent& e : s.Events()) out.push_back(e.action + ":" + PhaseName(e.phase) + (e.down ? "" : "(up)"));
}
void Frames(ActionSet& s, int n, bool x, std::vector<std::string>& out, bool mod = false) { for (int i = 0; i < n; ++i) Step(s, x, out, mod); }
bool Eq(const std::vector<std::string>& a, std::initializer_list<const char*> b) {
    if (a.size() != b.size()) return false;
    std::size_t i = 0; for (const char* s : b) if (a[i++] != s) return false;
    return true;
}
std::string Join(const std::vector<std::string>& v) { std::string s; for (auto& e : v) s += e + " "; return s; }
ActionSet One(const char* interaction) {
    ActionSet s;
    ActionDef& a = s.Add("A", ValueType::Button);
    a.AddBinding("key:x");
    if (interaction[0]) a.AddInteraction(interaction);
    return s;
}
} // namespace

int main() {
    RUN_SUITE("action_phases");

    // ---- parse
    {
        Interaction i;
        for (const char* t : {"press", "release", "press_release", "hold(0.5)", "tap(0.2)", "multitap(2,0.3,0.2)", "repeat(0.1,0.4)", "chorded(Shift)"}) {
            CHECK(Interaction::Parse(t, i) && i.ToString() == t);
        }
        CHECK(Interaction::Parse("hold", i) && i.a == 0.5f);     // defaults
        CHECK(!Interaction::Parse("hold(0)", i) && !Interaction::Parse("multitap(1)", i) && !Interaction::Parse("bogus", i) && !Interaction::Parse("chorded()", i));
    }

    // ---- default behaviour
    {
        Reset();
        ActionSet s = One("");
        std::vector<std::string> o;
        Step(s, false, o); Step(s, true, o); Step(s, true, o); Step(s, false, o);
        CHECK(Eq(o, {"A:started", "A:performed", "A:canceled"}));
        // a value action: started when it leaves zero, performed on change, canceled at zero
        ActionSet v;
        v.Add("Aim", ValueType::Axis1D).AddAxis("key:z", "key:x");
        std::vector<std::string> p;
        Step(v, false, p); Step(v, true, p); Step(v, true, p); Step(v, false, p);
        CHECK(Eq(p, {"Aim:started", "Aim:performed", "Aim:canceled"}));
    }

    // ---- press / release / press_release
    {
        Reset();
        std::vector<std::string> o;
        ActionSet s = One("press");
        Step(s, true, o); Step(s, true, o); Step(s, false, o);
        CHECK(Eq(o, {"A:started", "A:performed"}));
        o.clear(); ActionSet r = One("release");
        Step(r, true, o); Step(r, true, o); Step(r, false, o);
        CHECK(Eq(o, {"A:started", "A:performed(up)"}));
        o.clear(); ActionSet pr = One("press_release");
        Step(pr, true, o); Step(pr, false, o);
        CHECK(Eq(o, {"A:started", "A:performed", "A:performed(up)"}));
    }

    // ---- hold
    {
        Reset();
        std::vector<std::string> o;
        ActionSet s = One("hold(0.5)");
        Step(s, true, o);                           // started
        Frames(s, 5, true, o);                      // 0.25 s
        CHECK(Eq(o, {"A:started"}) && s.Progress("A") > 0.4f && s.Progress("A") < 0.6f);
        Frames(s, 6, true, o);                      // past 0.5 s
        CHECK(Eq(o, {"A:started", "A:performed"}) && s.Progress("A") == 1.0f);
        Frames(s, 4, true, o); Step(s, false, o);   // holding on adds nothing; releasing after performed adds nothing
        CHECK(Eq(o, {"A:started", "A:performed"}) && s.Progress("A") == 0.0f);
        o.clear(); Step(s, true, o); Frames(s, 3, true, o); Step(s, false, o);   // released early
        CHECK(Eq(o, {"A:started", "A:canceled(up)"}));
    }

    // ---- tap
    {
        Reset();
        std::vector<std::string> o;
        ActionSet s = One("tap(0.2)");
        Step(s, true, o); Step(s, true, o); Step(s, false, o);       // 0.1 s press
        CHECK(Eq(o, {"A:started", "A:performed(up)"}));
        o.clear(); Step(s, true, o); Frames(s, 6, true, o);          // held too long: canceled while still down
        CHECK(Eq(o, {"A:started", "A:canceled"}));
        Step(s, false, o);
        o.clear(); Step(s, true, o); Step(s, false, o);              // and a fresh tap works again
        CHECK(Eq(o, {"A:started", "A:performed(up)"}));
    }

    // ---- multi-tap: double tap within a gap, too slow, too long
    {
        Reset();
        std::vector<std::string> o;
        ActionSet s = One("multitap(2,0.3,0.2)");
        Step(s, true, o); Step(s, false, o);                         // first tap
        Step(s, false, o);                                           // 0.05 gap
        Step(s, true, o); Step(s, false, o);                         // second tap
        CHECK(Eq(o, {"A:started", "A:performed(up)"}));
        o.clear();
        Step(s, true, o); Step(s, false, o); Frames(s, 8, false, o); // second tap never comes: 0.4 s gap
        CHECK(Eq(o, {"A:started", "A:canceled"}));
        o.clear();
        Step(s, true, o); Frames(s, 6, true, o);                     // a press that is too long
        CHECK(Eq(o, {"A:started", "A:canceled"}));
        Step(s, false, o);
        ActionSet t3 = One("multitap(3,0.3,0.2)");
        o.clear();
        for (int k = 0; k < 3; ++k) { Step(t3, true, o); Step(t3, false, o); }
        CHECK(Eq(o, {"A:started", "A:performed(up)"}));
    }

    // ---- repeat
    {
        Reset();
        std::vector<std::string> o;
        ActionSet s = One("repeat(0.1,0.2)");
        Step(s, true, o);                                            // started + performed at once
        CHECK(Eq(o, {"A:started", "A:performed"}));
        o.clear(); Frames(s, 3, true, o);                            // 0.15 s: not yet
        CHECK(o.empty());
        Frames(s, 2, true, o);                                       // 0.25 s: first repeat at 0.2
        CHECK(Eq(o, {"A:performed"}));
        o.clear(); Frames(s, 2, true, o);                            // 0.35 s: next at 0.3
        CHECK(Eq(o, {"A:performed"}));
        o.clear(); Step(s, false, o);
        CHECK(Eq(o, {"A:canceled(up)"}));
    }

    // ---- chorded: counts only while the named action is held
    {
        Reset();
        ActionSet s;
        s.Add("Mod", ValueType::Button).AddBinding("key:m");
        ActionDef& a = s.Add("A", ValueType::Button);
        a.AddBinding("key:x"); a.AddInteraction("chorded(Mod)"); a.AddInteraction("press");
        std::vector<std::string> o;
        Step(s, true, o);                                            // x without the modifier
        CHECK(Eq(o, {"Mod:started"}) == false);
        bool aEvent = false; for (auto& e : o) if (e.rfind("A:", 0) == 0) aEvent = true;
        CHECK(!aEvent);
        Step(s, false, o); o.clear();
        Step(s, true, o, true);                                      // modifier and x together: x was pressed with the gate open
        aEvent = false; for (auto& e : o) if (e == "A:performed") aEvent = true;
        CHECK(aEvent);
    }

    // ---- callbacks and flags
    {
        Reset();
        ActionSet s = One("hold(0.2)");
        std::vector<std::string> seen;
        const int id = s.Subscribe([&](const PhaseEvent& e) { seen.push_back(std::string(PhaseName(e.phase))); });
        std::vector<std::string> o;
        Step(s, true, o);
        CHECK(s.Started("A") && !s.Performed("A") && s.PhaseOf("A") == Phase::Started);
        Frames(s, 5, true, o);
        CHECK(Eq(seen, {"started", "performed"}));
        s.Unsubscribe(id);
        Frames(s, 2, true, o); Step(s, false, o);
        CHECK(seen.size() == 2);
        CHECK(s.PhaseOf("A") == Phase::Waiting && !s.Started("A"));
    }

    // ---- input buffer and coyote time
    {
        InputBuffer b;
        CHECK(!b.Pending());
        b.Press(); b.Update(0.1f);
        CHECK(b.Pending() && b.Consume() && !b.Consume());        // once per press
        b.Press(); b.Update(0.2f);
        CHECK(!b.Pending() && !b.Consume());                       // too old
        CoyoteTime c;
        c.Update(true, 0.016f);
        CHECK(c.CanUse());
        c.Update(false, 0.05f); CHECK(c.CanUse());                 // just walked off
        c.Update(false, 0.1f);  CHECK(!c.CanUse());                // too late
        c.Update(true, 0.016f); c.Update(false, 0.02f);
        CHECK(c.CanUse()); c.Use(); CHECK(!c.CanUse());            // used: no double jump
        c.Update(true, 0.016f); CHECK(c.CanUse());
    }

    // ---- a picture: phase timeline of every interaction (software render; only with OKAY_SHOT_DIR)
    {
        Reset();
        struct Row { const char* text; };
        const Row rows[] = {{"press"}, {"release"}, {"hold(0.5)"}, {"tap(0.2)"}, {"multitap(2,0.3,0.2)"}, {"repeat(0.1,0.2)"}};
        // input pattern: one char per frame (0.05 s). X = key down, . = up
        const std::string pattern = "..XXXXXXXXXXXX..XX.XX.....XX......XXXXXXXXXXXXX...";
        std::string t = "INTERACTIONS AND PHASES   one key, 0.05 s per frame   S started  P performed  C canceled  (up) on release\n\n";
        t += shot::Pad("input", 24) + pattern + "\n";
        for (const Row& r : rows) {
            ActionSet s = One(r.text);
            std::string line = shot::Pad(r.text, 24);
            for (char ch : pattern) {
                std::vector<std::string> o; Step(s, ch == 'X', o);
                char c = '-';
                for (auto& e : o) { if (e.find("performed") != std::string::npos) c = 'P'; else if (e.find("canceled") != std::string::npos && c != 'P') c = 'C'; else if (e.find("started") != std::string::npos && c == '-') c = 'S'; }
                line += c;
            }
            t += line + "\n";
        }
        t += "\nA frame shows the strongest phase that happened in it (P over C over S).";
        Scene sc("shot"); sc.physicsEnabled = false;
        shot::AddText(sc, "T", 16, 16, t, 1.0f);
        CHECK(shot::ShotTo(sc, "action-phases.png"));
        Reset();
    }

    TEST_MAIN_RESULT();
}
