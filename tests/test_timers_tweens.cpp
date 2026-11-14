// Persistent (Global) timers, per-task options and cancel-by-tag/owner for the
// Scheduler, plus callback-order, re-entrancy, hitch and lifetime checks for the
// tween engine.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <string>

using namespace okay;

// One frame the way the host loop runs it: step Time, then update the scene.
static void Frame(Scene& s, float unscaledDt) {
    Time::Step(unscaledDt);
    s.Update(Time::DeltaTime());
}

int main() {
    RUN_SUITE("timers_tweens");

    // A host that never steps Time: the same scene updating again is a new frame,
    // another scene in between is not. (Must run before anything calls Time::Step.)
    {
        Scheduler::Global().Clear();
        Scene a("A"), b("B");
        int n = 0;
        Scheduler::Global().InvokeRepeating(0.1f, [&] { ++n; });
        a.Update(0.1f); b.Update(0.1f);
        CHECK(n == 1);
        a.Update(0.1f); b.Update(0.1f);
        CHECK(n == 2);
        Scheduler::Global().Clear();
    }

    const char* level = "test_timers_level.okayscene";
    {
        Scene lvl("Level");
        lvl.CreateGameObject("Goal");
        CHECK(SceneSerializer::SaveToFile(lvl, level));
    }

    // ---------------------------------------------------------- Scheduler ----
    {   // A global timer survives Scene::Clear; the scene's own timer does not.
        Scheduler::Global().Clear();
        Scene s("S");
        int global = 0, local = 0;
        Scheduler::Global().Invoke(0.5f, [&] { ++global; });
        s.scheduler().Invoke(0.5f, [&] { ++local; });
        s.Clear();
        CHECK(Scheduler::Global().ActiveCount() == 1);
        Frame(s, 0.6f);
        CHECK(global == 1);
        CHECK(local == 0);
        CHECK(Scheduler::Global().ActiveCount() == 0);
    }
    {   // ... and a scene load (RequestLoad swaps the scene at the end of Update).
        Scheduler::Global().Clear();
        Scene s("S");
        s.CreateGameObject("Old");
        int global = 0, local = 0;
        Scheduler::Global().Invoke(0.25f, [&] { ++global; });
        s.scheduler().Invoke(0.25f, [&] { ++local; });
        s.RequestLoad(level);
        Frame(s, 0.1f);                              // load happens here
        CHECK(s.Find("Goal") != nullptr && s.Find("Old") == nullptr);
        CHECK(global == 0);
        Frame(s, 0.1f); Frame(s, 0.1f);              // 0.3 s in total
        CHECK(global == 1);
        CHECK(local == 0);
    }
    {   // Two scenes updating in one frame tick the global scheduler once.
        Scheduler::Global().Clear();
        Scene a("A"), b("B");
        int n = 0;
        Scheduler::Global().InvokeRepeating(0.1f, [&] { ++n; });
        for (int frame = 1; frame <= 3; ++frame) {
            Time::Step(0.1f);
            a.Update(Time::DeltaTime());
            b.Update(Time::DeltaTime());
            CHECK(n == frame);
        }
        // The same scene updating twice in one frame (a fixed-step host) too.
        Time::Step(0.1f);
        a.Update(0.1f); a.Update(0.1f);
        CHECK(n == 4);
        Scheduler::Global().Clear();
    }
    {   // Unscaled tasks keep running at time scale 0; scaled ones stop.
        Scheduler::Global().Clear();
        Scene s("S");
        int scaled = 0, unscaled = 0, localScaled = 0, localUnscaled = 0;
        Scheduler::Options un; un.unscaled = true;
        Scheduler::Global().InvokeRepeating(0.1f, [&] { ++scaled; });
        Scheduler::Global().InvokeRepeating(0.1f, [&] { ++unscaled; }, -1, un);
        s.scheduler().InvokeRepeating(0.1f, [&] { ++localScaled; });
        s.scheduler().InvokeRepeating(0.1f, [&] { ++localUnscaled; }, -1, un);
        Time::SetTimeScale(0.0f);                     // what Game::SetPaused does
        for (int i = 0; i < 5; ++i) Frame(s, 0.1f);
        CHECK(scaled == 0 && localScaled == 0);
        CHECK(unscaled == 5 && localUnscaled == 5);
        Time::SetTimeScale(0.5f);                     // slow motion: unscaled still real time
        for (int i = 0; i < 4; ++i) Frame(s, 0.1f);
        CHECK(scaled == 2 && localScaled == 2);
        CHECK(unscaled == 9 && localUnscaled == 9);
        Time::SetTimeScale(1.0f);
        Scheduler::Global().Clear();
    }
    {   // Pause / resume by handle, remaining time, changing the interval.
        Scheduler s;
        int n = 0;
        auto h = s.InvokeRepeating(1.0f, [&] { ++n; });
        s.Update(0.25f);
        CHECK_NEAR(s.Remaining(h), 0.75f, 1e-5f);
        CHECK(s.SetPaused(h, true) && s.IsPaused(h));
        s.Update(5.0f);
        CHECK(n == 0);
        CHECK_NEAR(s.Remaining(h), 0.75f, 1e-5f);     // progress kept
        s.SetPaused(h, false);
        CHECK(!s.IsPaused(h));
        s.Update(0.75f);
        CHECK(n == 1);
        CHECK(s.SetInterval(h, 0.25f));
        s.Update(0.5f);
        CHECK(n == 3);
        CHECK(s.IsScheduled(h));
        CHECK(s.Cancel(h));
        CHECK(s.Remaining(h) < 0.0f);
        CHECK(!s.SetPaused(h, true) && !s.SetInterval(h, 1.0f) && !s.IsPaused(h));
        // A task that pauses itself stops firing within the same Update.
        int m = 0; Scheduler::Handle self = 0;
        self = s.InvokeRepeating(0.1f, [&] { ++m; s.SetPaused(self, true); });
        s.Update(1.0f);
        CHECK(m == 1);
        CHECK(s.ActiveCount() == 1);                  // paused tasks still count
    }
    {   // CancelByTag / CancelByOwner, outside and inside Update.
        Scheduler s;
        int a = 0, b = 0, c = 0, ownedFired = 0;
        int ownerA = 0, ownerB = 0;
        Scheduler::Options ta; ta.tag = "wave";
        Scheduler::Options tb; tb.tag = "other";
        Scheduler::Options oa; oa.owner = &ownerA;
        Scheduler::Options ob; ob.owner = &ownerB;
        s.Invoke(1.0f, [&] { ++a; }, ta);
        s.InvokeRepeating(1.0f, [&] { ++a; }, -1, ta);
        s.Tween(1.0f, [&](float) { ++a; }, {}, ta);
        s.Invoke(1.0f, [&] { ++b; }, tb);
        s.Invoke(1.0f, [&] { ++ownedFired; }, oa);
        s.Invoke(1.0f, [&] { ++c; }, ob);
        CHECK(s.ActiveCount() == 6);
        CHECK(s.CancelByTag("wave") == 3);
        CHECK(s.CancelByTag("wave") == 0);
        CHECK(s.CancelByTag("") == 0);
        CHECK(s.CancelByOwner(nullptr) == 0);
        CHECK(s.CancelByOwner(&ownerA) == 1);
        CHECK(s.ActiveCount() == 2);
        s.Update(2.0f);
        CHECK(a == 0 && ownedFired == 0 && b == 1 && c == 1);
        CHECK(s.ActiveCount() == 0);

        // From inside a callback: cancels later siblings and the running task itself,
        // and tasks added by the callback after the cancel survive.
        int fired = 0, survivors = 0;
        Scheduler::Options grp; grp.tag = "grp"; grp.owner = &ownerA;
        s.Invoke(0.5f, [&] {
            ++fired;
            CHECK(s.CancelByTag("grp") == 3);         // the other two and itself
            s.Invoke(0.0f, [&] { ++survivors; });
        }, grp);
        s.Invoke(0.5f, [&] { ++fired; }, grp);
        s.InvokeRepeating(0.5f, [&] { ++fired; }, -1, grp);
        s.Update(1.0f);
        CHECK(fired == 1);
        CHECK(survivors == 1);
        CHECK(s.ActiveCount() == 0);

        // A repeating task cancelling its own tag stops after this run.
        int reps = 0;
        s.InvokeRepeating(0.1f, [&] { ++reps; s.CancelByTag("self"); }, -1, Scheduler::Options{false, "self", nullptr});
        s.Update(1.0f);
        CHECK(reps == 1 && s.ActiveCount() == 0);

        // By owner from a callback, with the list reallocating under it.
        int ownedRan = 0;
        s.Invoke(0.0f, [&] {
            for (int i = 0; i < 64; ++i) s.Invoke(1.0f, [&] { ++ownedRan; }, ob);
            CHECK(s.CancelByOwner(&ownerB) == 64);
        });
        s.Update(0.1f);
        s.Update(2.0f);
        CHECK(ownedRan == 0 && s.ActiveCount() == 0);
    }
    {   // Global callbacks may schedule, cancel and clear while it is ticking.
        Scheduler::Global().Clear();
        Scene s("S");
        int n = 0;
        Scheduler::Options o; o.tag = "g";
        Scheduler::Global().Invoke(0.1f, [&] {
            ++n;
            Scheduler::Global().Invoke(0.1f, [&] { ++n; }, o);
            Scheduler::Global().CancelByTag("g");
        });
        Frame(s, 0.1f); Frame(s, 0.1f); Frame(s, 0.1f);
        CHECK(n == 1);
        CHECK(Scheduler::Global().ActiveCount() == 0);
    }

    // -------------------------------------------------------------- Tween ----
    auto linear = [](TweenManager& m, float& v, float to, float d) {
        return m.To([&v] { return v; }, [&v](float x) { v = x; }, to, d);
    };

    {   // Callback order: start, update, step, complete.
        TweenManager m; float v = 0; std::string log;
        linear(m, v, 1.0f, 1.0f)->SetEase(Ease::Linear)
            .OnStart([&] { log += "S"; }).OnUpdate([&] { log += "U"; })
            .OnStepComplete([&] { log += "P"; }).OnComplete([&] { log += "C"; }).OnKill([&] { log += "K"; });
        m.Update(0.5f, 0.5f);
        CHECK(log == "SU");
        m.Update(0.5f, 0.5f);
        CHECK(log == "SUUPCK");                      // then the auto-kill, same frame
    }
    {   // Kill(true) completes exactly once, from outside and from OnComplete.
        TweenManager m; float v = 0; int done = 0, killed = 0, steps = 0;
        auto t = linear(m, v, 5.0f, 1.0f);
        t->SetEase(Ease::Linear).OnComplete([&] { ++done; }).OnKill([&] { ++killed; }).OnStepComplete([&] { ++steps; });
        t->Kill(true);
        t->Kill(true);
        CHECK(done == 1 && killed == 1 && steps == 1);
        CHECK_NEAR(v, 5.0f, 1e-6f);
        m.Update(0.1f, 0.1f);
        CHECK(done == 1 && killed == 1 && m.Count() == 0);

        int done2 = 0, killed2 = 0; float w = 0;
        auto u = linear(m, w, 1.0f, 1.0f);
        u->SetAutoKill(false).OnKill([&] { ++killed2; });
        Tweener* raw = u.get();
        u->OnComplete([&, raw] { ++done2; raw->Kill(true); });
        m.Update(2.0f, 2.0f);
        CHECK(done2 == 1 && killed2 == 1);

        // Complete() on a finished tween that is kept does not complete it again.
        int done3 = 0; float x = 0;
        auto k = linear(m, x, 1.0f, 1.0f);
        k->SetAutoKill(false).OnComplete([&] { ++done3; });
        m.Update(2.0f, 2.0f); k->Complete(); k->Kill(true);
        CHECK(done3 == 1);
    }
    {   // Kill() from OnUpdate stops everything after it: no step / complete for a dead tween.
        TweenManager m; float v = 0; int steps = 0, done = 0, killed = 0;
        auto t = linear(m, v, 1.0f, 1.0f);
        Tweener* raw = t.get();
        t->OnUpdate([raw] { raw->Kill(); }).OnStepComplete([&] { ++steps; })
            .OnComplete([&] { ++done; }).OnKill([&] { ++killed; });
        m.Update(2.0f, 2.0f);
        CHECK(steps == 0 && done == 0 && killed == 1);
        CHECK(m.Count() == 0);
    }
    {   // Kill() from OnStart: nothing else fires.
        TweenManager m; float v = 0; int ups = 0, done = 0;
        auto t = linear(m, v, 1.0f, 1.0f);
        Tweener* raw = t.get();
        t->OnStart([raw] { raw->Kill(); }).OnUpdate([&] { ++ups; }).OnComplete([&] { ++done; });
        m.Update(2.0f, 2.0f);
        CHECK(ups == 0 && done == 0);
    }
    {   // Restart() from OnStepComplete leaves a consistent timeline.
        TweenManager m; float v = 0; int steps = 0; bool once = false;
        auto t = linear(m, v, 1.0f, 1.0f);
        Tweener* raw = t.get();
        t->SetEase(Ease::Linear).SetLoops(3).OnStepComplete([&, raw] {
            ++steps; if (!once) { once = true; raw->Restart(); }
        });
        m.Update(2.5f, 2.5f);                        // crosses two loops in one go
        CHECK(steps == 1);                           // the second crossing belongs to the old run
        CHECK(t->CompletedLoops() == 0);
        CHECK_NEAR(t->Elapsed(), 0.0f, 1e-5f);
        CHECK(!t->IsComplete());
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(v, 0.5f, 1e-4f);
    }
    {   // Restart() from OnUpdate on the finishing frame cancels that completion.
        TweenManager m; float v = 0; int done = 0, ups = 0;
        auto t = linear(m, v, 1.0f, 1.0f);
        Tweener* raw = t.get();
        t->SetEase(Ease::Linear).OnUpdate([&, raw] { if (++ups == 1) raw->Restart(); }).OnComplete([&] { ++done; });
        m.Update(2.0f, 2.0f);
        CHECK(done == 0);
        CHECK(!t->IsComplete());
        m.Update(1.0f, 1.0f);
        CHECK(done == 1);
    }
    {   // Restart() from OnComplete plays again; the tween is not left half-reset.
        TweenManager m; float v = 0; int done = 0;
        auto t = linear(m, v, 1.0f, 1.0f);
        Tweener* raw = t.get();
        t->SetEase(Ease::Linear).SetAutoKill(false).OnComplete([&, raw] { if (++done == 1) raw->Restart(); });
        m.Update(1.0f, 1.0f);
        CHECK(done == 1);
        CHECK(!t->IsComplete());
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(v, 0.5f, 1e-4f);
        m.Update(0.5f, 0.5f);
        CHECK(done == 2);
    }
    {   // Complete() from OnUpdate: step callbacks are not fired twice.
        TweenManager m; float v = 0; int steps = 0, done = 0; bool once = false;
        auto t = linear(m, v, 1.0f, 1.0f);
        Tweener* raw = t.get();
        t->SetLoops(3).OnUpdate([&, raw] { if (!once) { once = true; raw->Complete(); } })
            .OnStepComplete([&] { ++steps; }).OnComplete([&] { ++done; });
        m.Update(1.5f, 1.5f);
        CHECK(steps == 3 && done == 1);
    }
    {   // Loops land exactly on the end values: yoyo, restart, backwards.
        TweenManager m; float v = 0.5f;
        auto t = linear(m, v, 1.0f, 1.0f);
        t->SetEase(Ease::QuadInOut).SetLoops(2, LoopType::Yoyo);
        m.Update(9.0f, 9.0f);
        CHECK(v == 0.5f);                            // back where it began, exactly
        float w = 0.0f;
        linear(m, w, 1.0f, 1.0f)->SetEase(Ease::CubicOut).SetLoops(3, LoopType::Yoyo);
        m.Update(9.0f, 9.0f);
        CHECK(w == 1.0f);
        float x = 0.0f;
        linear(m, x, 1.0f, 1.0f)->SetEase(Ease::SineInOut).SetLoops(3, LoopType::Restart);
        m.Update(9.0f, 9.0f);
        CHECK(x == 1.0f);

        float y = 0.0f;
        auto b = linear(m, y, 1.0f, 1.0f);
        b->SetEase(Ease::Linear).SetLoops(2, LoopType::Yoyo).SetAutoKill(false);
        m.Update(0.7f, 0.7f);
        b->PlayBackwards();
        m.Update(0.2f, 0.2f);
        CHECK_NEAR(y, 0.5f, 1e-4f);
        m.Update(10.0f, 10.0f);
        CHECK(y == 0.0f);
        CHECK(!b->IsPlaying());                      // it stops at the start

        // Complete() on an endless tween: the end of the loop it is in, exactly.
        float e = 0.0f, f = 0.0f;
        auto endless = linear(m, e, 1.0f, 1.0f);
        endless->SetEase(Ease::Linear).SetLoops(-1, LoopType::Restart);
        auto yo = linear(m, f, 1.0f, 1.0f);
        yo->SetEase(Ease::Linear).SetLoops(-1, LoopType::Yoyo);
        m.Update(3.3f, 3.3f);
        endless->Complete(); yo->Complete();
        CHECK(e == 1.0f);
        CHECK(f == 0.0f);                            // loop 3 of a yoyo runs back down
    }
    {   // Goto with a delay counts from the end of the delay.
        TweenManager m; float v = 0; int starts = 0;
        auto t = linear(m, v, 1.0f, 1.0f);
        t->SetEase(Ease::Linear).SetDelay(1.0f).OnStart([&] { ++starts; });
        t->Goto(0.5f);
        CHECK_NEAR(v, 0.5f, 1e-5f);
        CHECK(starts == 1);
        m.Update(5.0f, 5.0f);                        // paused by Goto
        CHECK_NEAR(v, 0.5f, 1e-5f);
        t->Goto(0.2f);
        CHECK_NEAR(v, 0.2f, 1e-5f);
        t->Goto(0.0f, true);
        m.Update(0.25f, 0.25f);
        CHECK_NEAR(v, 0.25f, 1e-5f);
        CHECK(starts == 1);
    }
    {   // Zero and negative durations finish in one frame without hanging.
        TweenManager m; float a = 0, b = 0; int done = 0, call = 0;
        linear(m, a, 3.0f, 0.0f)->OnComplete([&] { ++done; });
        linear(m, b, 4.0f, -2.0f)->OnComplete([&] { ++done; });
        m.DelayedCall(0.0f, [&] { ++call; });
        m.Update(0.016f, 0.016f);
        CHECK(a == 3.0f && b == 4.0f);
        CHECK(done == 2 && call == 1 && m.Count() == 0);
        // Directly setting loops = 0 behaves like one loop instead of never ending.
        float c = 0; int d = 0;
        auto t = linear(m, c, 1.0f, 1.0f);
        t->loops = 0; t->OnComplete([&] { ++d; });
        m.Update(2.0f, 2.0f);
        CHECK(d == 1 && c == 1.0f);
    }
    {   // A frame hitch over several loops fires one step per loop, once.
        TweenManager m; float v = 0; int steps = 0, done = 0, ups = 0;
        linear(m, v, 1.0f, 0.5f)->SetEase(Ease::Linear).SetLoops(-1)
            .OnStepComplete([&] { ++steps; }).OnComplete([&] { ++done; }).OnUpdate([&] { ++ups; });
        m.Update(25.25f, 25.25f);
        CHECK(steps == 50 && done == 0 && ups == 1);
        CHECK_NEAR(v, 0.5f, 1e-3f);
        m.Update(0.5f, 0.5f);
        CHECK(steps == 51);
        m.KillAll();

        float w = 0; int s5 = 0, d5 = 0;
        linear(m, w, 1.0f, 1.0f)->SetLoops(5, LoopType::Yoyo)
            .OnStepComplete([&] { ++s5; }).OnComplete([&] { ++d5; });
        m.Update(1000.0f, 1000.0f);
        CHECK(s5 == 5 && d5 == 1);

        // An absurd step (and non-finite ones) must neither hang nor corrupt the timeline.
        float z = 0; int s6 = 0;
        auto t = linear(m, z, 1.0f, 1.0f);
        t->SetLoops(-1).OnStepComplete([&] { ++s6; });
        m.Update(1.0e12f, 1.0e12f);
        CHECK(s6 > 0 && s6 <= 4096);
        m.Update(std::numeric_limits<float>::quiet_NaN(), 0.0f);
        m.Update(0.25f, 0.25f);
        CHECK(z >= 0.0f && z <= 1.0f);
    }
    {   // Time scale: SetSpeed, scaled vs unscaled tweens.
        TweenManager m; float a = 0, b = 0, c = 0;
        linear(m, a, 1.0f, 1.0f)->SetEase(Ease::Linear);
        linear(m, b, 1.0f, 1.0f)->SetEase(Ease::Linear).SetUpdate(true);
        linear(m, c, 1.0f, 1.0f)->SetEase(Ease::Linear).SetSpeed(0.5f);
        m.Update(0.0f, 0.4f);                        // paused game: scaled dt is 0
        CHECK_NEAR(a, 0.0f, 1e-6f);
        CHECK_NEAR(b, 0.4f, 1e-5f);
        CHECK_NEAR(c, 0.0f, 1e-6f);
        m.Update(0.2f, 0.2f);
        CHECK_NEAR(a, 0.2f, 1e-5f); CHECK_NEAR(c, 0.1f, 1e-5f);
    }
    {   // Scene path: the tween manager gets the real frame time at time scale 0.
        Scene s("S"); GameObject* go = s.CreateGameObject("G");
        DOTween::LocalMove(go, {1, 0, 0}, 1.0f)->SetEase(Ease::Linear);
        GameObject* go2 = s.CreateGameObject("G2");
        DOTween::LocalMove(go2, {1, 0, 0}, 1.0f)->SetEase(Ease::Linear).SetUpdate(true);
        Time::SetTimeScale(0.0f);
        Frame(s, 0.25f); Frame(s, 0.25f);
        CHECK_NEAR(go->transform->localPosition.x, 0.0f, 1e-6f);
        CHECK_NEAR(go2->transform->localPosition.x, 0.5f, 1e-4f);
        Time::SetTimeScale(1.0f);
    }
    {   // A tween somebody still holds is dead once its scene is cleared, and
        // survives its manager being destroyed (no pointer back into it).
        std::shared_ptr<Tweener> held;
        {
            Scene s("S"); GameObject* go = s.CreateGameObject("G");
            held = DOTween::LocalMove(go, {1, 0, 0}, 1.0f);
            CHECK(held->IsActive());
            s.Clear();
            CHECK(!held->IsActive());
            GameObject* go2 = s.CreateGameObject("G2");
            held = DOTween::LocalMove(go2, {1, 0, 0}, 1.0f);
            CHECK(held->IsActive());
        }                                            // scene (and its manager) gone
        CHECK(!held->IsActive());
        TweenManager other;
        other.Sequence()->Append(held);              // would chase a dangling owner
        held->Kill(true);
        other.Update(1.0f, 1.0f);
    }
    {   // Removing the component a tween writes to, or destroying the object from
        // inside the tween's own callback: nothing touches freed memory.
        Scene s("S");
        GameObject* a = s.CreateGameObject("A");
        a->AddComponent<SpriteRenderer>();
        GameObject* t = s.CreateGameObject("T");
        auto* tr = t->AddComponent<TextRenderer>();
        DOTween::ColorTo(a, Color::Black, 1.0f);
        DOTween::Fade(a, 0.0f, 1.0f);
        DOTween::Text(t, "hello", 1.0f);
        DOTween::Counter(t, 0, 10, 1.0f);
        s.Update(0.2f);
        a->RemoveComponent<SpriteRenderer>();
        t->RemoveComponent(tr);
        s.Update(0.2f); s.Update(1.0f);
        CHECK(s.tweens().Count() == 0);

        GameObject* d = s.CreateGameObject("D");
        int after = 0;
        auto tw = DOTween::LocalMove(d, {5, 0, 0}, 1.0f);
        tw->OnUpdate([&] { s.Destroy(d); });
        DOTween::Scale(d, 2.0f, 1.0f)->OnComplete([&] { ++after; });
        s.Update(0.3f);                              // destroy queued, flushed at the end
        CHECK(s.tweens().Count() == 0);
        s.Update(2.0f);
        CHECK(after == 0);
    }

    std::remove(level);
    TEST_MAIN_RESULT();
}
