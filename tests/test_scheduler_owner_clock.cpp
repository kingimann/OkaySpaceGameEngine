// Scheduler: tasks that die with their owner (a GameObject or any liveness
// predicate), tasks that follow a named Clock channel, pause / cancel by tag,
// and the introspection helpers (Tasks, Dump). Clocks are driven by hand, so
// every number here is exact and nothing depends on the wall clock.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Clock.hpp"
#include <memory>
#include <string>

using namespace okay;

// One frame of a fake clock set: tick it, then let the scheduler look.
static void Frame(ClockSet& cs, Scheduler& s, float scaled, float unscaled, bool gamePaused = false) {
    cs.Tick(scaled, unscaled, gamePaused);
    s.Update(scaled, unscaled);
}

int main() {
    RUN_SUITE("scheduler_owner_clock");

    // -------------------------------------------------- liveness predicate ----
    {   // A weak token: the task is dropped silently once the owner is gone.
        Scheduler s;
        auto token = std::make_shared<int>(0);
        int fired = 0, ticks = 0, tw = 0, twDone = 0;
        Scheduler::Options o;
        o.alive = [w = std::weak_ptr<int>(token)] { return !w.expired(); };
        s.Invoke(1.0f, [&] { ++fired; }, o);
        s.InvokeRepeating(0.25f, [&] { ++ticks; }, -1, o);
        s.Tween(1.0f, [&](float) { ++tw; }, [&] { ++twDone; }, o);
        s.Update(0.5f);
        CHECK(ticks == 2 && tw == 1);
        token.reset();                               // the owner dies
        const int twBefore = tw;
        s.Update(0.5f);
        CHECK(fired == 0 && ticks == 2 && tw == twBefore && twDone == 0);
        CHECK(s.ActiveCount() == 0);
        s.Update(5.0f);
        CHECK(fired == 0);
    }
    {   // alive is looked at before each run, so a catch-up burst stops when the owner dies mid-way.
        Scheduler s;
        int runs = 0;
        bool alive = true;
        Scheduler::Options o;
        o.alive = [&] { return alive; };
        s.InvokeRepeating(0.1f, [&] { if (++runs == 3) alive = false; }, -1, o);
        s.Update(2.0f);
        CHECK(runs == 3 && s.ActiveCount() == 0);
    }
    {   // A predicate that throws counts as gone; PurgeDeadOwners drops those tasks now.
        Scheduler s;
        Scheduler::Options bad; bad.alive = []() -> bool { throw 1; };
        bool alive = true;
        Scheduler::Options flag; flag.alive = [&] { return alive; };
        int n = 0;
        s.Invoke(10.0f, [&] { ++n; }, bad);
        s.Invoke(10.0f, [&] { ++n; }, flag);
        s.Invoke(10.0f, [&] { ++n; });
        CHECK(s.ActiveCount() == 3);
        CHECK(s.PurgeDeadOwners() == 1);
        CHECK(s.ActiveCount() == 2);
        alive = false;
        CHECK(s.PurgeDeadOwners() == 1);
        CHECK(s.ActiveCount() == 1);
        s.Update(11.0f);
        CHECK(n == 1);
    }

    // -------------------------------------------------------- GameObject ----
    {   // A global task bound to an object dies with it, and never runs on freed memory.
        Scheduler::Global().Clear();
        Scene s("S");
        GameObject* go = s.CreateGameObject("Pickup");
        GameObject* keep = s.CreateGameObject("Keep");
        int ran = 0, keptRan = 0;
        Scheduler::Options o; o.object = go;
        Scheduler::Options k; k.object = keep;
        Scheduler::Global().Invoke(1.0f, [go, &ran] { go->transform->localPosition.x = 1.0f; ++ran; }, o);
        Scheduler::Global().InvokeRepeating(0.25f, [go, &ran] { go->transform->localPosition.x = 2.0f; ++ran; }, -1, o);
        Scheduler::Global().Invoke(1.0f, [&] { ++keptRan; }, k);
        CHECK(Scheduler::Global().ActiveCount() == 3);
        s.Update(0.1f);
        s.Destroy(go);
        s.Update(0.1f);                              // flushed at the end of this frame
        s.Update(0.1f);                              // the next global tick drops its tasks
        CHECK(ran == 0);
        CHECK(Scheduler::Global().ActiveCount() == 1);
        s.Update(1.0f);
        CHECK(ran == 0 && keptRan == 1);
        Scheduler::Global().Clear();
    }
    {   // Scene loads free every object: global tasks bound to them are gone with them.
        const char* level = "test_sched_owner_level.okayscene";
        { Scene lvl("Level"); lvl.CreateGameObject("Goal"); CHECK(SceneSerializer::SaveToFile(lvl, level)); }
        Scheduler::Global().Clear();
        Scene s("S");
        GameObject* old = s.CreateGameObject("Old");
        int ran = 0;
        Scheduler::Options o; o.object = old;
        Scheduler::Global().InvokeRepeating(0.1f, [old, &ran] { old->transform->localPosition.x = 3.0f; ++ran; }, -1, o);
        s.RequestLoad(level);
        Time::Step(0.05f); s.Update(Time::DeltaTime());   // the load happens at the end of this update
        CHECK(s.Find("Old") == nullptr);
        for (int i = 0; i < 5; ++i) { Time::Step(0.1f); s.Update(Time::DeltaTime()); }
        CHECK(Scheduler::Global().ActiveCount() == 0);
        CHECK(ran <= 1);                             // at most the run before the load
        Scheduler::Global().Clear();
        std::remove(level);
    }
    {   // The scene's own scheduler too; the object also serves as the owner for CancelByOwner.
        Scene s("S");
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        int aRan = 0, bRan = 0;
        Scheduler::Options oa; oa.object = a;
        Scheduler::Options ob; ob.object = b;
        s.scheduler().Tween(1.0f, [a, &aRan](float) { a->transform->localPosition.x += 1.0f; ++aRan; }, {}, oa);
        s.scheduler().Tween(1.0f, [b, &bRan](float) { b->transform->localPosition.x += 1.0f; ++bRan; }, {}, ob);
        s.Update(0.1f);
        CHECK(aRan == 1 && bRan == 1);
        CHECK(s.scheduler().CancelByOwner(b) == 1);
        CHECK(s.scheduler().ActiveCount() == 1);
        CHECK(s.scheduler().CancelByObject(nullptr) == 0);
        s.Destroy(a);
        s.Update(0.1f);
        s.Update(0.1f);
        CHECK(aRan == 2);                            // the frame it was destroyed in, not after
        CHECK(s.scheduler().ActiveCount() == 0);
    }
    {   // A task for an object that is already destroyed is refused (no live owner to run it for).
        Scene s("S");
        GameObject* go = s.CreateGameObject("Shell");
        go->StripToTransform();
        CHECK(go->IsDestroyed());
        Scheduler::Options o; o.object = go;
        Scheduler sch;
        CHECK(sch.Invoke(1.0f, [] {}, o) == Scheduler::InvalidHandle);
        CHECK(sch.ActiveCount() == 0);
        // An unbound option (null object) is just a normal task.
        Scheduler::Options none;
        CHECK(sch.Invoke(1.0f, [] {}, none) != Scheduler::InvalidHandle);
    }

    // ----------------------------------------------------- clock channels ----
    {   // A pause menu: gameplay timers stop, UI timers keep running.
        ClockSet cs;
        Scheduler s; s.SetClockSet(&cs);
        Scheduler::Options game; game.clock = "game";
        Scheduler::Options ui; ui.clock = "ui";
        int g = 0, u = 0;
        s.InvokeRepeating(0.1f, [&] { ++g; }, -1, game);
        s.InvokeRepeating(0.1f, [&] { ++u; }, -1, ui);
        Frame(cs, s, 0.1f, 0.1f);
        CHECK(g == 1 && u == 1);
        // The game pauses: scaled time is 0, real time goes on.
        for (int i = 0; i < 5; ++i) Frame(cs, s, 0.0f, 0.1f, true);
        CHECK(g == 1 && u == 6);
        Frame(cs, s, 0.1f, 0.1f);
        CHECK(g == 2 && u == 7);
        // The channel's own scale: slow motion for the game channel only.
        cs.Get("game").SetScale(0.5f);
        for (int i = 0; i < 4; ++i) Frame(cs, s, 0.1f, 0.1f);
        CHECK(g == 4 && u == 11);
        cs.Get("game").Pause();
        for (int i = 0; i < 4; ++i) Frame(cs, s, 0.1f, 0.1f);
        CHECK(g == 4 && u == 15);
        cs.Get("game").Resume();
        Frame(cs, s, 0.1f, 0.1f);
        CHECK(g == 4 && u == 16);                    // half speed: 0.05 s so far of the next 0.1
        Frame(cs, s, 0.1f, 0.1f);
        CHECK(g == 5);
    }
    {   // A cinematic channel holds while the game is paused; a custom channel is its own.
        ClockSet cs;
        Scheduler s; s.SetClockSet(&cs);
        Scheduler::Options cine; cine.clock = "cinematic";
        Scheduler::Options stun; stun.clock = "stun";
        int c = 0, st = 0;
        s.InvokeRepeating(0.1f, [&] { ++c; }, -1, cine);
        s.InvokeRepeating(0.1f, [&] { ++st; }, -1, stun);
        cs.Get("stun").SetScale(0.0f);               // the stun freezes only its own timers
        for (int i = 0; i < 3; ++i) Frame(cs, s, 0.1f, 0.1f);
        CHECK(c == 3 && st == 0);
        for (int i = 0; i < 3; ++i) Frame(cs, s, 0.0f, 0.1f, true);   // game paused
        CHECK(c == 3);
        cs.Get("stun").SetScale(1.0f);
        Frame(cs, s, 0.1f, 0.1f);
        CHECK(c == 4 && st == 1);                    // nothing banked while frozen
    }
    {   // A tween on a channel follows its progress; two updates in one tick do not double count.
        ClockSet cs;
        Scheduler s; s.SetClockSet(&cs);
        Scheduler::Options ui; ui.clock = "ui";
        float t = -1.0f; int done = 0;
        s.Tween(1.0f, [&](float p) { t = p; }, [&] { ++done; }, ui);
        cs.Tick(0.25f, 0.25f, false);
        s.Update(0.25f); s.Update(0.25f); s.Update(0.25f);
        CHECK_NEAR(t, 0.25f, 1e-6f);
        cs.Tick(0.5f, 0.5f, false);
        s.Update(0.5f);
        CHECK_NEAR(t, 0.75f, 1e-6f);
        cs.Get("ui").Pause();
        cs.Tick(0.5f, 0.5f, false);
        s.Update(0.5f);
        CHECK_NEAR(t, 0.75f, 1e-6f);
        cs.Get("ui").Resume();
        cs.Tick(0.5f, 0.5f, false);
        s.Update(0.5f);
        CHECK(done == 1);
    }
    {   // SetPaused on a channel task keeps its progress and banks no time; Remaining counts channel seconds.
        ClockSet cs;
        Scheduler s; s.SetClockSet(&cs);
        Scheduler::Options ui; ui.clock = "ui";
        int n = 0;
        auto h = s.Invoke(1.0f, [&] { ++n; }, ui);
        Frame(cs, s, 0.25f, 0.25f);
        CHECK_NEAR(s.Remaining(h), 0.75f, 1e-6f);
        s.SetPaused(h, true);
        for (int i = 0; i < 10; ++i) Frame(cs, s, 0.25f, 0.25f);
        CHECK_NEAR(s.Remaining(h), 0.75f, 1e-6f);
        s.SetPaused(h, false);
        Frame(cs, s, 0.25f, 0.25f);
        CHECK_NEAR(s.Remaining(h), 0.5f, 1e-6f);
        for (int i = 0; i < 2; ++i) Frame(cs, s, 0.25f, 0.25f);
        CHECK(n == 1);
    }
    {   // A clock that is reset (a new Play session) resyncs instead of running backwards or jumping.
        ClockSet cs;
        Scheduler s; s.SetClockSet(&cs);
        Scheduler::Options ui; ui.clock = "ui";
        int n = 0;
        s.InvokeRepeating(1.0f, [&] { ++n; }, -1, ui);
        for (int i = 0; i < 5; ++i) Frame(cs, s, 0.1f, 0.1f);     // 0.5 s in
        cs.Reset();
        for (int i = 0; i < 5; ++i) Frame(cs, s, 0.1f, 0.1f);     // the first frame after the reset only resyncs
        CHECK(n == 0);
        for (int i = 0; i < 5; ++i) Frame(cs, s, 0.1f, 0.1f);     // 0.4 + 0.5 more: past the 1 s
        CHECK(n == 1);
    }
    {   // A task scheduled from inside an update gets that frame's share, like a scene-time task.
        ClockSet cs;
        Scheduler s; s.SetClockSet(&cs);
        Scheduler::Options ui; ui.clock = "ui";
        int inner = 0;
        s.Invoke(0.0f, [&] { s.Invoke(0.1f, [&] { ++inner; }, ui); });
        Frame(cs, s, 0.1f, 0.1f);
        CHECK(inner == 1);
        // ... while one scheduled between frames counts from the next frame on.
        int later = 0;
        s.Invoke(0.15f, [&] { ++later; }, ui);
        Frame(cs, s, 0.1f, 0.1f);
        CHECK(later == 0);
        Frame(cs, s, 0.1f, 0.1f);
        CHECK(later == 1);
    }
    {   // Without a set of its own a scheduler reads the process-wide clocks.
        ClockSet::Global().Reset();
        Scheduler s;
        Scheduler::Options ui; ui.clock = "ui";
        int n = 0;
        s.InvokeRepeating(0.5f, [&] { ++n; }, -1, ui);
        Time::Step(0.25f); ClockSet::Global().TickFromTime(); s.Update(Time::DeltaTime());
        Time::Step(0.25f); ClockSet::Global().TickFromTime(); s.Update(Time::DeltaTime());
        CHECK(n == 1);
        Clock::Get("ui").SetScale(2.0f);
        Time::Step(0.25f); ClockSet::Global().TickFromTime(); s.Update(Time::DeltaTime());
        CHECK(n == 2);
        Clock::Get("ui").SetScale(1.0f);
        ClockSet::Global().Reset();
    }

    // ---------------------------------------------- tags, owners, listings ----
    {   // Pause and resume a group by tag or owner; counts by tag.
        Scheduler s;
        int owner = 0, a = 0, b = 0, c = 0;
        Scheduler::Options tw; tw.tag = "wave";
        Scheduler::Options to; to.tag = "wave"; to.owner = &owner;
        Scheduler::Options other; other.tag = "menu";
        s.InvokeRepeating(1.0f, [&] { ++a; }, -1, tw);
        s.InvokeRepeating(1.0f, [&] { ++b; }, -1, to);
        s.InvokeRepeating(1.0f, [&] { ++c; }, -1, other);
        CHECK(s.CountByTag("wave") == 2 && s.CountByTag("menu") == 1 && s.CountByTag("") == 0);
        CHECK(s.SetPausedByTag("wave", true) == 2);
        s.Update(1.0f);
        CHECK(a == 0 && b == 0 && c == 1);
        CHECK(s.SetPausedByTag("wave", false) == 2);
        CHECK(s.SetPausedByOwner(&owner, true) == 1);
        CHECK(s.SetPausedByTag("", true) == 0 && s.SetPausedByOwner(nullptr, true) == 0);
        s.Update(1.0f);
        CHECK(a == 1 && b == 0 && c == 2);
        CHECK(s.SetPausedByOwner(&owner, false) == 1);
        // From inside a callback.
        int inner = 0;
        s.Invoke(0.0f, [&] { s.SetPausedByTag("wave", true); ++inner; });
        s.Update(1.0f);
        CHECK(inner == 1);
        s.Update(1.0f);
        CHECK(a == 2 && b == 1);                     // frozen where they were
    }
    {   // Tasks() and Dump().
        ClockSet cs;
        Scheduler s; s.SetClockSet(&cs);
        Scheduler::Options w; w.tag = "wave";
        Scheduler::Options un; un.unscaled = true;
        Scheduler::Options ui; ui.clock = "ui";
        s.Invoke(0.5f, [] {}, w);
        s.InvokeRepeating(1.0f, [] {}, -1, un);
        auto h = s.Tween(2.0f, [](float) {}, {}, ui);
        s.InvokeRepeating(0.25f, [] {}, 3);
        s.SetPaused(h, true);
        s.Update(0.1f, 0.1f);
        auto list = s.Tasks();
        CHECK(list.size() == 4);
        if (list.size() == 4) {
            CHECK(list[0].kind == Scheduler::TaskInfo::Kind::Delay && list[0].tag == "wave");
            CHECK_NEAR(list[0].period, 0.5f, 1e-6f);
            CHECK_NEAR(list[0].remaining, 0.4f, 1e-6f);
            CHECK(list[1].kind == Scheduler::TaskInfo::Kind::Repeating && list[1].unscaled && list[1].repeatsLeft == -1);
            CHECK(list[2].kind == Scheduler::TaskInfo::Kind::Tween && list[2].clock == "ui" && list[2].paused);
            CHECK(list[3].repeatsLeft == 3 && list[3].ownerAlive && !list[3].hasObject);
            CHECK(list[0].handle < list[1].handle && list[1].handle < list[2].handle);
        }
        const std::string d = s.Dump();
        CHECK(d.rfind("Scheduler: 4 tasks", 0) == 0);
        CHECK(d.find("delay 0.5s, left 0.4s tag=wave") != std::string::npos);
        CHECK(d.find("repeat every 1s (forever), next in 0.9s [unscaled]") != std::string::npos);
        CHECK(d.find("tween 2s, left 2s [ui] [paused]") != std::string::npos);
        CHECK(d.find("repeat every 0.25s (3 left)") != std::string::npos);
        Scheduler empty;
        CHECK(empty.Dump() == "Scheduler: 0 tasks");
    }
    {   // Many timers: schedule, cancel by tag, run a few seconds, all counts exact.
        Scheduler s;
        const int N = 20000;
        int fired = 0;
        Scheduler::Options even; even.tag = "even";
        for (int i = 0; i < N; ++i) {
            if (i % 2 == 0) s.Invoke(0.5f + (float)(i % 100) * 0.01f, [&] { ++fired; }, even);
            else s.Invoke(0.5f + (float)(i % 100) * 0.01f, [&] { ++fired; });
        }
        CHECK(s.ActiveCount() == (std::size_t)N);
        CHECK(s.CancelByTag("even") == N / 2);
        for (int f = 0; f < 200; ++f) s.Update(0.01f, 0.01f);
        CHECK(fired == N / 2);
        CHECK(s.ActiveCount() == 0);
        // A task cancelling thousands of others from its own callback.
        for (int i = 0; i < 5000; ++i) s.Invoke(10.0f, [&] { ++fired; }, even);
        s.Invoke(0.0f, [&] { CHECK(s.CancelByTag("even") == 5000); });
        s.Update(0.1f, 0.1f);
        CHECK(s.ActiveCount() == 0);
    }

    TEST_MAIN_RESULT();
}
