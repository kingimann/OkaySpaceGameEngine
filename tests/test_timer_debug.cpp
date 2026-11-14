// The timers and tweens debug view (Core/TimerDebug.hpp): the text lines an overlay or a console
// prints, the numbers behind them, and the one-line summary. Everything is driven by fake frame
// times. Block N runs alone with `test_timer_debug N`.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Core/Clock.hpp>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace okay;

static int g_only = 0;
#define BLOCK(n) if (g_only == 0 || g_only == (n))

static bool StartsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
static bool EndsWith(const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }
static bool HasLine(const std::vector<std::string>& lines, const std::string& line) {
    for (const std::string& l : lines) if (l == line) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc > 1) g_only = std::atoi(argv[1]);
    RUN_SUITE("timer_debug");
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    BLOCK(1) {   // empty sources, missing sources
        Scheduler s;
        TweenManager m;
        CHECK(TimerDebug::BuildLines(&s, nullptr) == std::vector<std::string>{"Scheduler: 0 tasks"});
        CHECK(TimerDebug::BuildLines(nullptr, &m) == std::vector<std::string>{"Tweens: 0"});
        CHECK((TimerDebug::BuildLines(&s, &m) == std::vector<std::string>{"Scheduler: 0 tasks", "Tweens: 0"}));
        CHECK(TimerDebug::BuildLines(nullptr, nullptr).empty());
        CHECK(TimerDebug::Summary(&s, &m) == "Timers: 0 | Tweens: 0");
        CHECK(TimerDebug::Summary(nullptr, nullptr).empty());
        CHECK(TimerDebug::Summary(&s, nullptr) == "Timers: 0");
        s.Invoke(1.0f, [] {});
        CHECK(TimerDebug::BuildLines(&s, nullptr)[0] == "Scheduler: 1 task (1 delay, 0 repeating, 0 tweens)");
    }

    BLOCK(2) {   // a scheduler with tags, owners, an object, a paused task: the lines and the numbers
        Scene scene;
        GameObject* boss = scene.CreateGameObject("Boss");
        Scheduler s;
        int ownerB = 0;
        Scheduler::Options wave; wave.tag = "wave";
        s.Invoke(5.0f, [] {}, wave);                                   // #1
        s.Invoke(6.0f, [] {}, wave);                                   // #2
        Scheduler::Options rep; rep.tag = "wave"; rep.object = boss;
        s.InvokeRepeating(0.5f, [] {}, -1, rep);                       // #3
        Scheduler::Options ui; ui.tag = "ui"; ui.owner = &ownerB;
        s.Tween(2.0f, [](float) {}, {}, ui);                           // #4
        const auto h5 = s.Invoke(9.0f, [] {});                         // #5
        s.SetPaused(h5, true);
        s.Update(0.25f);

        const TimerDebug::SchedulerStats st = TimerDebug::Capture(s);
        CHECK(st.total == 5 && st.delays == 3 && st.repeating == 1 && st.tweens == 1);
        CHECK(st.paused == 1 && st.overdue == 0 && st.ownerGone == 0);
        CHECK_NEAR(st.nextDue, 0.25f, 1e-5);
        CHECK(st.nextHandle == 3);
        CHECK(st.byTag.size() == 3 && st.byTag[0].name == "wave" && st.byTag[0].count == 3);
        CHECK(st.byTag[1].name == "(none)" && st.byTag[2].name == "ui");
        CHECK(st.byOwner.size() == 3 && st.byOwner[0].name == "(none)" && st.byOwner[0].count == 3);
        CHECK(st.oldest.size() == 3 && st.oldest[0].handle == 1 && st.oldest[2].handle == 3);   // equal ages: scheduling order
        CHECK_NEAR(st.oldest[0].age, 0.25, 1e-6);

        const std::vector<std::string> lines = TimerDebug::BuildLines(&s, nullptr);
        CHECK(lines.size() == 5);
        CHECK(lines[0] == "Scheduler: 5 tasks (3 delays, 1 repeating, 1 tween), 1 paused");
        CHECK(lines[1] == "  next due in 0.25s (#3)");
        CHECK(lines[2] == "  tags: wave x3, (none) x1, ui x1");
        CHECK(StartsWith(lines[3], "  owners: (none) x3, 0x") && EndsWith(lines[3], ", Boss x1"));
        CHECK(lines[4] == "  oldest: #1 delay 5s [wave] running 0.25s; #2 delay 6s [wave] running 0.25s; "
                          "#3 repeat every 0.5s [wave] running 0.25s");
        CHECK(TimerDebug::BuildLines(&s, nullptr) == lines);            // the same text every time
        CHECK(TimerDebug::Summary(&s, nullptr) == "Timers: 5 (1 paused, next 0.25s)");
    }

    BLOCK(3) {   // a task past its time that did not run (paused after it was due) is overdue; nothing else is waiting
        Scheduler s;
        const auto h = s.Invoke(1.0f, [] {});
        s.Update(0.9f);
        s.SetInterval(h, 0.5f);                                        // now due, 0.4 s ago
        s.SetPaused(h, true);
        const TimerDebug::SchedulerStats st = TimerDebug::Capture(s);
        CHECK(st.overdue == 1 && st.paused == 1);
        CHECK(st.nextDue < 0.0f && st.nextHandle == Scheduler::InvalidHandle);
        CHECK(st.oldest.size() == 1 && std::fabs(st.oldest[0].overdue - 0.4) < 1e-6);
        const auto lines = TimerDebug::BuildLines(&s, nullptr);
        CHECK(lines.size() >= 2 && lines[1] == "  nothing waiting, 1 overdue");
        CHECK(TimerDebug::Summary(&s, nullptr) == "Timers: 1 (1 paused, 1 overdue)");
        s.SetPaused(h, false);
        s.Update(0.0f);
        CHECK(TimerDebug::Capture(s).overdue == 0 && TimerDebug::Capture(s).total == 0);   // it ran
    }

    BLOCK(4) {   // a task whose object is destroyed is reported until the next update drops it
        Scene scene;
        GameObject* door = scene.CreateGameObject("Door");
        Scheduler s;
        Scheduler::Options o; o.object = door; o.tag = "door";
        s.Invoke(9.0f, [] {}, o);
        auto before = TimerDebug::Capture(s);
        CHECK(before.ownerGone == 0 && before.byOwner[0].name == "Door");
        scene.Destroy(door);
        scene.Update(0.016f);
        const TimerDebug::SchedulerStats st = TimerDebug::Capture(s);
        CHECK(st.ownerGone == 1);
        CHECK(st.byOwner.size() == 1 && st.byOwner[0].name == "(object gone)");
        const auto lines = TimerDebug::BuildLines(&s, nullptr);
        CHECK(lines.size() >= 2 && lines[1] == "  nothing waiting, 1 owner gone");
        CHECK(s.PurgeDeadOwners() == 1);
        CHECK(TimerDebug::Capture(s).total == 0);
    }

    BLOCK(5) {   // channels and unscaled tasks are counted; a task that never fires does not count as due
        ClockSet clocks;
        Scheduler s;
        s.SetClockSet(&clocks);
        Scheduler::Options c; c.clock = "ui";
        s.Invoke(1.0f, [] {}, c);
        Scheduler::Options u; u.unscaled = true;
        s.Invoke(2.0f, [] {}, u);
        s.Invoke(inf, [] {});
        const TimerDebug::SchedulerStats st = TimerDebug::Capture(s);
        CHECK(st.total == 3 && st.onClock == 1 && st.unscaled == 1);
        CHECK_NEAR(st.nextDue, 1.0f, 1e-6);                             // not the infinite one
        s.Invoke(nan, [] {});                                           // NaN delay counts as 0: due now
        CHECK_NEAR(TimerDebug::Capture(s).nextDue, 0.0f, 1e-6);
    }

    BLOCK(6) {   // only the biggest groups are listed, the rest is counted; options switch the detail off
        Scheduler s;
        for (const char* tag : {"a", "b", "c", "d", "e", "f"}) {
            Scheduler::Options o; o.tag = tag;
            s.Invoke(5.0f, [] {}, o);
        }
        Scheduler::Options o; o.tag = "a";
        s.Invoke(5.0f, [] {}, o);
        TimerDebug::Options opt;
        opt.topGroups = 2;
        auto lines = TimerDebug::BuildLines(&s, nullptr, opt);
        CHECK(HasLine(lines, "  tags: a x2, b x1, +4 more"));
        opt.showGroups = false;
        opt.showOldest = false;
        lines = TimerDebug::BuildLines(&s, nullptr, opt);
        CHECK(lines.size() == 2);
        CHECK(lines[0].find("Scheduler: 7 tasks") == 0);
        opt.showOldest = true;
        opt.topOldest = 0;
        CHECK(TimerDebug::Capture(s, opt).oldest.empty());
        // Negative numbers are treated as zero (a bad option cannot crash the overlay).
        opt.topOldest = -5; opt.topGroups = -1;
        opt.showGroups = true;
        CHECK(!TimerDebug::BuildLines(&s, nullptr, opt).empty());
    }

    BLOCK(7) {   // tweens: kinds, playing / paused, done and kept, endless, nested parts, ids and tags, the oldest
        TweenManager m;
        float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
        auto lin = [&](float& v, float dur) {
            auto t = m.To([&v] { return v; }, [&v](float x) { v = x; }, 1.0f, dur);
            t->SetEase(Ease::Linear);
            return t;
        };
        auto t1 = lin(a, 1.0f); t1->SetId("fade").SetTag("ui");                // value, playing
        auto t2 = lin(b, 1.0f); t2->SetId("fade"); t2->Pause();                // paused
        auto t3 = lin(c, 1.0f); t3->SetLoops(-1);                              // endless
        auto c1 = lin(d, 1.0f), c2 = lin(e, 1.0f);
        auto seq = m.Sequence();
        seq->Append(c1); seq->Append(c2);                                      // a sequence with two parts
        auto t4 = lin(f, 0.4f); t4->SetAutoKill(false);                        // done and kept after 0.5 s
        m.Update(0.5f, 0.5f);

        const TweenManager::TweenInfo* none = nullptr;
        const std::vector<TweenManager::TweenInfo> infos = m.Infos();
        CHECK(infos.size() == 5);
        for (const auto& i : infos) if (i.handle == t1->handle()) none = &i;
        CHECK(none && none->kind == TweenManager::TweenInfo::Kind::Value && none->id == "fade" && none->tag == "ui");
        CHECK(none && std::fabs(none->elapsed - 0.5) < 1e-6 && std::fabs(none->age - 0.5) < 1e-6);

        const TimerDebug::TweenStats st = TimerDebug::Capture(m);
        CHECK(st.total == 5 && st.playing == 3 && st.paused == 1);
        CHECK(st.sequences == 1 && st.nested == 2 && st.endless == 1 && st.doneKept == 1);
        CHECK(st.byId.size() == 2 && st.byId[0].name == "(none)" && st.byId[0].count == 3 && st.byId[1].name == "fade");
        CHECK(st.byTag.size() == 1 && st.byTag[0].name == "ui");
        CHECK(st.oldest.size() == 3);

        const std::vector<std::string> lines = TimerDebug::BuildLines(nullptr, &m);
        CHECK(lines.size() == 5);
        CHECK(lines[0] == "Tweens: 5 (3 playing, 1 paused), 1 done and kept, 1 endless");
        CHECK(lines[1] == "  1 sequence holding 2 parts");
        CHECK(lines[2] == "  ids: (none) x3, fade x2");
        CHECK(lines[3] == "  tags: ui x1");
        // The oldest three are the ones that ran longest; equal ages keep the order they were added in.
        CHECK(lines[4] == "  oldest: #" + std::to_string(t1->handle()) + " value \"fade\" running 0.5s, loop 1/1; #" +
                          std::to_string(t3->handle()) + " value running 0.5s, loop 1/forever; #" +
                          std::to_string(seq->handle()) + " sequence running 0.5s, loop 1/1");
        CHECK(TimerDebug::Summary(nullptr, &m) == "Tweens: 5 (3 playing)");
    }

    BLOCK(8) {   // seconds read well, and non-numbers do not print nonsense
        CHECK(TimerDebug::FormatSeconds(0.25) == "0.25s");
        CHECK(TimerDebug::FormatSeconds(35.0) == "35s");
        CHECK(TimerDebug::FormatSeconds(59.9) == "59.9s");
        CHECK(TimerDebug::FormatSeconds(60.0) == "1m00s");
        CHECK(TimerDebug::FormatSeconds(125.0) == "2m05s");
        CHECK(TimerDebug::FormatSeconds(3599.6) == "1h00m");
        CHECK(TimerDebug::FormatSeconds(3600.0) == "1h00m");
        CHECK(TimerDebug::FormatSeconds(36000.0) == "10h00m");
        CHECK(TimerDebug::FormatSeconds(inf) == "never");
        CHECK(TimerDebug::FormatSeconds(-1.0) == "0s");
        CHECK(TimerDebug::FormatSeconds(nan) == "0s");
        CHECK(TimerDebug::FormatSeconds(0.0) == "0s");
        CHECK(TimerDebug::FormatSeconds(1.0e300) != "");
    }

    BLOCK(9) {   // the overlay hook: one line through DebugOverlay::AddLine, and a scene's own scheduler and tweens
        Scene scene;
        GameObject* go = scene.CreateGameObject("Box");
        scene.scheduler().InvokeRepeating(1.0f, [] {});
        DOTween::Move(go, {1, 0, 0}, 2.0f);
        const int id = DebugOverlay::AddLine([&] { return TimerDebug::Summary(&scene.scheduler(), &scene.tweens()); });
        scene.Update(0.5f);
        const auto lines = DebugOverlay::BuildLines(DebugOverlay::Capture(&scene), false, true);
        bool found = false;
        for (const std::string& l : lines) if (l == "Timers: 1 (next 0.5s) | Tweens: 1 (1 playing)") found = true;
        CHECK(found);
        DebugOverlay::RemoveLine(id);
        const auto info = TimerDebug::BuildLines(&scene.scheduler(), &scene.tweens());
        CHECK(info.size() >= 2 && info[0].find("Scheduler: 1 task") == 0);
    }

    BLOCK(10) {  // a big scheduler is summarised without trouble (and quickly)
        Scheduler s;
        for (int i = 0; i < 20000; ++i) {
            Scheduler::Options o; o.tag = "t" + std::to_string(i % 50);
            s.Invoke(5.0f + (float)(i % 3), [] {}, o);
        }
        s.Update(1.0f);
        const auto st = TimerDebug::Capture(s);
        CHECK(st.total == 20000 && st.byTag.size() == 50 && st.byTag[0].count == 400);
        CHECK_NEAR(st.nextDue, 4.0f, 1e-5);
        const auto lines = TimerDebug::BuildLines(&s, nullptr);
        CHECK(lines.size() == 4);                                        // head, next due, tags, oldest (no owners)
    }

    TEST_MAIN_RESULT();
}
