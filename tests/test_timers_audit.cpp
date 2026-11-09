// Fourth audit of timers and tweens (1.2): numbers that are not numbers, zero and huge counts, owners
// and scenes that die inside a callback, a TweenAnimation saved and loaded mid-play, easing outside
// 0..1, springs at their edges, and the clock cursor. Each block was written to fail first.
// Block N runs alone with `test_timers_audit N`.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace okay;

static int g_only = 0;
#define BLOCK(n) if (g_only == 0 || g_only == (n))

namespace {
const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

std::shared_ptr<ValueTween> Lin(TweenManager& m, float& v, float to, float d) {
    auto t = m.To([&v] { return v; }, [&v](float x) { v = x; }, to, d);
    t->SetEase(Ease::Linear);
    return t;
}
bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}

int main(int argc, char** argv) {
    if (argc > 1) g_only = std::atoi(argv[1]);
    RUN_SUITE("timers_audit");

    // ------------------------------------------------- numbers that are not numbers ----
    BLOCK(1) {   // a NaN or infinite end value, punch, shake, jump, rotation or path never writes NaN into a transform
        Scene s;
        GameObject* go = s.CreateGameObject("Victim");
        go->transform->localPosition = {1, 2, 3};
        go->transform->localScale = {1, 1, 1};
        const Vec3 bad{kNan, kInf, -kInf};
        DOTween::LocalMove(go, bad, 0.5f);
        DOTween::Scale(go, bad, 0.5f);
        DOTween::Rotate(go, bad, 0.5f);
        DOTween::PunchPosition(go, bad, 0.5f, 5, kNan);
        DOTween::PunchScale(go, bad, 0.5f);
        DOTween::PunchRotation(go, bad, 0.5f);
        DOTween::ShakePosition(go, 0.5f, bad);
        DOTween::ShakeRotation(go, 0.5f, bad);
        DOTween::ShakeScale(go, 0.5f, bad);
        DOTween::Jump(go, bad, kNan, 2, 0.5f);
        DOTween::Path(go, {bad, Vec3{1, 1, 1}}, 0.5f, DOTween::PathType::CatmullRom);
        for (int i = 0; i < 12; ++i) {
            s.Update(0.1f);
            CHECK(Finite3(go->transform->localPosition) && Finite3(go->transform->localScale));
            const Quat r = go->transform->localRotation;
            CHECK(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z) && std::isfinite(r.w));
        }
        CHECK(s.tweens().Count() == 0);
    }

    BLOCK(2) {   // direct writes of garbage into a tween's fields: no crash, no hang, numbers stay numbers
        TweenManager m;
        float v = 0;
        auto t = Lin(m, v, 1.0f, 1.0f);
        t->delay = kNan; t->duration = -4.0f; t->timeScale = kNan; t->loops = -3;
        t->SetAutoKill(false);
        for (int i = 0; i < 5; ++i) m.Update(0.25f, 0.25f);
        CHECK(std::isfinite(t->Elapsed()) && std::isfinite(v) && std::isfinite(t->ElapsedPercentage()));
        t->timeScale = 1.0f;
        t->delay = -7.0f;                                      // negative counts as none
        t->loops = 1;
        t->Restart();
        m.Update(1.0f, 1.0f);
        CHECK(t->IsComplete() && v == 1.0f);
        t->delay = kInf;                                       // infinite counts as the longest delay, not as a hang
        t->Restart();
        m.Update(100.0f, 100.0f);
        CHECK(std::isfinite(v) && std::isfinite(t->Elapsed()));
        t->Kill();
    }

    BLOCK(3) {   // zero and tiny durations with endless loops: each frame is bounded and the loop count stays sane
        TweenManager m;
        float v = 0;
        int steps = 0;
        auto t = Lin(m, v, 1.0f, 0.0f);
        t->SetLoops(-1, LoopType::Yoyo).OnStepComplete([&] { ++steps; });
        m.Update(0.016f, 0.016f);
        CHECK(steps > 0 && steps <= 4096 && std::isfinite(v) && v >= 0.0f && v <= 1.0f);
        steps = 0;
        m.Update(1000.0f, 1000.0f);
        CHECK(steps <= 4096 && steps > 0);
        CHECK(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
        CHECK(t->CompletedLoops() >= 0);
        t->Kill();

        float w = 0;
        auto u = Lin(m, w, 5.0f, -1.0f);                       // negative duration: an instant tween
        m.Update(0.01f, 0.01f);
        CHECK(!u->IsActive() && w == 5.0f);
    }

    BLOCK(4) {   // enormous loop counts: finite, no hang, the end is reached exactly where it should be
        TweenManager m;
        float v = 0;
        auto t = Lin(m, v, 1.0f, 1.0f);
        t->SetLoops(INT_MAX, LoopType::Incremental).SetAutoKill(false);
        m.Update(1.0e30f, 1.0e30f);
        CHECK(std::isfinite(v));
        t->Restart();
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(v, 0.5f, 1e-4);
        float w = 0;
        auto u = Lin(m, w, 1.0f, 1.0f);
        u->SetLoops(INT_MAX, LoopType::Yoyo).SetAutoKill(false);
        u->RenderAt(3.0e8);
        CHECK(std::isfinite(w) && w >= 0.0f && w <= 1.0f);
        u->Complete();                                          // beyond the 31 year cap of the timeline: still a number
        CHECK(std::isfinite(w) && w >= 0.0f && w <= 1.0f);
        t->Kill(); u->Kill();
    }

    BLOCK(19) {  // a pause menu: PauseAll / PlayAll must not turn a backwards tween round; unscaled ones keep playing
        TweenManager m;
        float a = 0, b = 0, c = 0;
        auto fwd = Lin(m, a, 10.0f, 10.0f);
        auto back = Lin(m, b, 10.0f, 10.0f);
        back->SetAutoKill(false);
        auto ui = Lin(m, c, 10.0f, 10.0f);
        ui->SetUpdate(true);
        m.Update(5.0f, 5.0f);
        back->PlayBackwards();
        m.Update(1.0f, 1.0f);                                   // b: 5 -> 4
        CHECK_NEAR(b, 4.0f, 1e-4);
        m.PauseAll(true);
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(a, 6.0f, 1e-4); CHECK_NEAR(b, 4.0f, 1e-4);   // paused
        CHECK_NEAR(c, 7.0f, 1e-4);                              // the unscaled one kept playing
        m.PlayAll();
        CHECK(back->IsBackwards());
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(a, 7.0f, 1e-4);
        CHECK_NEAR(b, 3.0f, 1e-4);                              // still running back
        CHECK(fwd->IsActive() && ui->IsActive());
        back->Kill();
    }

    // ------------------------------------------------------- owners dying in callbacks ----
    BLOCK(5) {   // the object a task or tween acts on is destroyed from inside one of its own callbacks
        Scene s;
        GameObject* a = s.CreateGameObject("A");
        Scheduler::Options o; o.object = a;
        int ranAfter = 0;
        Scheduler::Handle lateHandle = 12345;
        s.scheduler().Invoke(0.1f, [&] {
            s.Destroy(a);                                                 // queued: `a` lives until the end of this frame
            lateHandle = s.scheduler().Invoke(0.1f, [] {}, o);
        });
        s.scheduler().Invoke(0.1f, [&] { ++ranAfter; }, o);               // still runs this frame: the object is alive
        s.scheduler().InvokeRepeating(0.1f, [&] { ++ranAfter; }, -1, o);
        s.Update(0.1f);
        CHECK(ranAfter == 2);
        CHECK(lateHandle != Scheduler::InvalidHandle);
        s.Update(0.1f);                                                   // `a` was freed at the end of the last frame: the repeating timer goes without running
        CHECK(ranAfter == 2);
        CHECK(s.scheduler().ActiveCount() == 0);
        s.Update(0.1f);

        GameObject* b = s.CreateGameObject("B");
        auto t = DOTween::Move(b, {5, 0, 0}, 1.0f);
        t->OnUpdate([&] { s.Destroy(b); });                               // the tween's own object dies in its callback
        s.Update(0.25f);
        s.Update(0.25f);
        CHECK(!t->IsActive());
        CHECK(s.tweens().Count() == 0);
    }

    BLOCK(6) {   // the scene is cleared from inside a scheduler or tween callback: nothing runs on freed objects
        // A Clear() requested while the scene is updating waits for the end of the frame (Scene::IsUpdating,
        // RequestClear), so everything that was already due in that frame still runs on live objects; after the
        // frame the scene is empty and nothing runs again.
        Scene s;
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        int later = 0, cleared = 0;
        Scheduler::Options oa; oa.object = a;
        s.scheduler().Invoke(0.1f, [&] { ++cleared; s.Clear(); });
        s.scheduler().Invoke(0.1f, [&] { ++later; }, oa);
        s.scheduler().InvokeRepeating(0.1f, [&] { ++later; });
        auto t = DOTween::Move(b, {1, 0, 0}, 1.0f);
        int tweenCalls = 0;
        t->OnUpdate([&] { ++tweenCalls; });
        s.Update(0.1f);
        CHECK(cleared == 1 && later == 2);                                // both timers due this frame ran before the Clear took effect
        CHECK(s.scheduler().ActiveCount() == 0 && s.tweens().Count() == 0 && !t->IsActive());
        CHECK(s.Objects().empty());
        s.Update(0.1f);
        CHECK(later == 2 && cleared == 1);                                // nothing survives the Clear

        // From a tween callback.
        GameObject* c = s.CreateGameObject("C");
        auto u = DOTween::Move(c, {1, 0, 0}, 1.0f);
        int calls = 0;
        u->OnUpdate([&] { ++calls; s.Clear(); });
        auto w = DOTween::Scale(c, 2.0f, 1.0f);
        s.Update(0.1f);
        s.Update(0.1f);
        CHECK(calls == 1 && !u->IsActive() && !w->IsActive());
        CHECK(s.Objects().empty());
    }

    BLOCK(7) {   // a callback that calls Update on its own scheduler / manager: no crash, nothing runs twice for one tick
        Scheduler sch;
        int fired = 0, nested = 0;
        sch.Invoke(1.0f, [&] { ++fired; sch.Update(1.0f); ++nested; });
        sch.Invoke(1.0f, [&] { ++fired; });
        sch.Update(1.0f);
        CHECK(fired == 2 && nested == 1);
        CHECK(sch.ActiveCount() == 0);

        TweenManager m;
        float v = 0;
        int completes = 0;
        auto t = Lin(m, v, 1.0f, 1.0f);
        t->OnUpdate([&] { m.Update(0.5f, 0.5f); }).OnComplete([&] { ++completes; });
        m.Update(0.25f, 0.25f);
        m.Update(0.25f, 0.25f);
        CHECK(completes <= 1);
        CHECK(std::isfinite(v));
        t->Kill();
    }

    BLOCK(8) {   // Global scheduler cleared from a callback, and a global task that cancels itself by tag
        Scheduler& g = Scheduler::Global();
        g.Clear();
        int a = 0, b = 0;
        Scheduler::Options o; o.tag = "gone";
        g.Invoke(0.1f, [&] { ++a; g.Clear(); });
        g.Invoke(0.1f, [&] { ++b; }, o);
        g.Update(0.1f);
        CHECK(a == 1 && b == 0 && g.ActiveCount() == 0);
        g.InvokeRepeating(0.1f, [&] { ++a; g.CancelByTag("self"); }, -1, [] { Scheduler::Options x; x.tag = "self"; return x; }());
        g.Update(0.1f);
        g.Update(0.1f);
        CHECK(a == 2 && g.ActiveCount() == 0);
    }

    // -------------------------------------------------------- TweenAnimation save and load ----
    BLOCK(9) {   // saved mid-play: settings round trip exactly; the loaded copy plays again from its own start
        Scene s("S");
        GameObject* go = s.CreateGameObject("Logo");
        go->transform->localPosition = {0, 0, 0};
        auto* ta = go->AddComponent<TweenAnimation>();
        ta->type = (int)TweenAnimation::Type::LocalMove;
        ta->endValue = {0, 4, 0};
        ta->duration = 2.0f; ta->ease = (int)Ease::Linear;
        ta->loops = -1; ta->loopType = (int)LoopType::Yoyo;
        ta->id = "bob"; ta->autoKill = false;
        s.Start();
        s.Update(1.0f);                                         // half way up
        CHECK_NEAR(go->transform->localPosition.y, 2.0f, 1e-3);
        const std::string text = SceneSerializer::SerializeObject(*go);
        Scene s2("S2");
        GameObject* back = SceneSerializer::InstantiateFromText(s2, text);
        CHECK(back != nullptr);
        auto* tb = back ? back->GetComponent<TweenAnimation>() : nullptr;
        CHECK(tb != nullptr);
        if (tb) {
            CHECK(tb->loops == -1 && tb->loopType == (int)LoopType::Yoyo && tb->id == "bob" && !tb->autoKill);
            CHECK(tb->duration == 2.0f && tb->endValue.y == 4.0f);
            CHECK_NEAR(back->transform->localPosition.y, 2.0f, 1e-3);   // saved where it was
            s2.Start();
            CHECK(tb->IsPlaying());
            s2.Update(0.5f);
            CHECK(back->transform->localPosition.y > 2.0f);              // goes on from the saved pose, no crash, finite
            CHECK(std::isfinite(back->transform->localPosition.y));
        }
        // The original keeps playing untouched by the copy.
        s.Update(0.5f);
        CHECK_NEAR(go->transform->localPosition.y, 3.0f, 1e-3);
    }

    BLOCK(10) {  // a scene file whose TweenAnimation has NaN, infinity or garbage: the file loads, the other objects survive, nothing is poisoned
        Scene s("S");
        GameObject* first = s.CreateGameObject("First");
        GameObject* go = s.CreateGameObject("Bad");
        GameObject* last = s.CreateGameObject("Last");
        first->transform->localPosition = {1, 0, 0};
        last->transform->localPosition = {9, 0, 0};
        auto* ta = go->AddComponent<TweenAnimation>();
        ta->type = (int)TweenAnimation::Type::PunchPosition;
        ta->endValue = {kNan, kInf, 1};
        ta->endFloat = kNan; ta->duration = kInf; ta->delay = kNan; ta->elasticity = kNan;
        ta->vibrato = INT_MAX; ta->jumps = INT_MIN; ta->loops = INT_MIN;
        ta->ease = 9999; ta->loopType = -5; ta->type = 77;      // out of range: falls back to a local move
        ta->clock = std::string(1000, 'x');
        ta->easeCurve = "steps(1e9)";
        const char* path = "test_timers_audit_bad.okayscene";
        CHECK(SceneSerializer::SaveToFile(s, path));
        Scene s2("S2");
        std::string error;
        CHECK(SceneSerializer::LoadFromFile(s2, path, &error));
        GameObject* l1 = s2.Find("First");
        GameObject* l3 = s2.Find("Last");
        GameObject* bad = s2.Find("Bad");
        CHECK(l1 && l3 && bad);
        if (l1 && l3 && bad) {
            CHECK_NEAR(l1->transform->localPosition.x, 1.0f, 1e-6);
            CHECK_NEAR(l3->transform->localPosition.x, 9.0f, 1e-6);
            s2.Start();
            for (int i = 0; i < 20; ++i) s2.Update(0.1f);
            CHECK(Finite3(bad->transform->localPosition) && Finite3(bad->transform->localScale));
            CHECK(Finite3(l1->transform->localPosition));
        }
        std::remove(path);
    }

    BLOCK(11) {  // removing a TweenAnimation (or its object) mid-play ends its tween; a second Play after Kill makes a new one
        Scene s;
        GameObject* go = s.CreateGameObject("Logo");
        auto* ta = go->AddComponent<TweenAnimation>();
        ta->type = (int)TweenAnimation::Type::LocalMove; ta->endValue = {0, 9, 0}; ta->duration = 1.0f;
        ta->ease = (int)Ease::Linear; ta->loops = -1;
        s.Start();
        s.Update(0.25f);
        const auto cur = ta->Current();
        CHECK(cur && cur->IsActive());
        ta->Kill();
        CHECK(!cur->IsActive());
        ta->Play();
        CHECK(ta->Current() && ta->Current()->IsActive() && ta->Current() != cur);
        s.Destroy(go);
        s.Update(0.1f);
        CHECK(s.tweens().Count() == 0);
    }

    // ------------------------------------------------------------------- easing ----
    BLOCK(12) {  // every ease, for input far outside 0..1 and for NaN and infinity: always a number, never NaN
        const float inputs[] = {-1.0e30f, -1000.0f, -100.0f, -13.0f, -2.0f, -1.0f, -0.5f, -0.0f, 0.0f, 0.25f, 0.5f, 0.75f,
                                1.0f, 1.5f, 2.0f, 13.0f, 100.0f, 1000.0f, 1.0e30f, kInf, -kInf, kNan};
        int bad = 0;
        for (int e = 0; e < (int)Ease::Count; ++e)
            for (float t : inputs) if (!std::isfinite(Easing::Evaluate((Ease)e, t))) ++bad;
        CHECK(bad == 0);
        CHECK(std::isfinite(Easing::Evaluate((Ease)999, 0.5f)) && std::isfinite(Easing::Evaluate((Ease)-1, 0.5f)));
        CHECK(Easing::Evaluate(Ease::Count, 0.3f) == 0.3f);
        // Inside the range nothing changed: the ends are exact and NaN input starts at the start.
        for (int e = 0; e < (int)Ease::Count; ++e) {
            CHECK_NEAR(Easing::Evaluate((Ease)e, 1.0f), 1.0f, 1e-5);
            CHECK(Easing::Evaluate((Ease)e, kNan) == Easing::Evaluate((Ease)e, 0.0f));
        }
        CHECK(Easing::Evaluate(Ease::Linear, kInf) == 1.0f && Easing::Evaluate(Ease::Linear, -kInf) == 0.0f);
        // Curves described as data: no crash for hostile numbers.
        for (float t : inputs) {
            CHECK(std::isfinite(Easing::CubicBezier(kNan, kInf, -kInf, kNan, t)));
            CHECK(std::isfinite(Easing::CubicBezier(0.4f, 0.0f, 0.2f, 1.0f, t)));
            CHECK(std::isfinite(Easing::Steps(INT_MAX, t)) && std::isfinite(Easing::Steps(-5, t, true)));
        }
        EaseCurve c;
        CHECK(!EaseCurve::Parse("steps(1e9)", c));
        CHECK(!EaseCurve::Parse("cubic-bezier(nan,0,1,1)", c));
        CHECK(!EaseCurve::Parse("cubic-bezier(1,2,3)", c));
        CHECK(!EaseCurve::Parse(std::string(100000, '('), c));
        CHECK(!EaseCurve::Parse("steps(", c) && !EaseCurve::Parse("steps()", c) && !EaseCurve::Parse(")", c));
        CHECK(Easing::FromName(std::string(5000, 'q'), Ease::Linear) == Ease::Linear);
    }

    BLOCK(13) {  // a tween whose ease curve overshoots (Back, Elastic) and whose end is far away stays finite and ends exactly
        TweenManager m;
        float v = 0;
        auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 1.0e30f, 1.0f);
        t->SetEase(Ease::ElasticOut);
        for (int i = 0; i < 12; ++i) { m.Update(0.1f, 0.1f); CHECK(std::isfinite(v)); }
        CHECK(!t->IsActive());
        CHECK(v == 1.0e30f);
    }

    // ------------------------------------------------------------------- springs ----
    BLOCK(14) {  // zero, negative and non-finite stiffness and damping: no blow-up, no NaN
        for (float k : {0.0f, -50.0f, -1.0e30f, kNan, kInf, 1.0e-12f}) {
            for (float c : {0.0f, 5.0f, -5.0f, kNan, kInf}) {
                float x = 1.0f, v = 2.0f;
                for (int i = 0; i < 200; ++i) Spring::Step(x, v, 0.0f, k, c, 0.016f);
                CHECK(std::isfinite(x) && std::isfinite(v));
                CHECK(std::fabs(x) < 1000.0f && std::fabs(v) < 1000.0f);
            }
        }
        // No spring and no friction: a straight coast.
        float x = 1.0f, v = 2.0f;
        Spring::Step(x, v, 0.0f, 0.0f, 0.0f, 0.5f);
        CHECK_NEAR(x, 2.0f, 1e-6); CHECK_NEAR(v, 2.0f, 1e-6);
        // No spring, friction only: velocity decays and the value settles at a finite place.
        x = 0.0f; v = 10.0f;
        for (int i = 0; i < 1000; ++i) Spring::Step(x, v, 100.0f, -1.0f, 4.0f, 0.1f);
        CHECK_NEAR(x, 2.5f, 1e-3); CHECK(std::fabs(v) < 1e-6);
    }

    BLOCK(15) {  // critical damping and its neighbourhood: no overshoot at critical, continuous across the switch
        float x = 1.0f, v = 0.0f;
        float minX = 1.0f;
        for (int i = 0; i < 600; ++i) { Spring::Step(x, v, 0.0f, 100.0f, 20.0f, 0.01f); minX = std::min(minX, x); }
        CHECK(minX >= -1e-5f && std::fabs(x) < 1e-3f);          // 2 sqrt(100) = 20: exactly critical, never crosses
        // Damping a hair either side of critical must give nearly the same motion as critical itself.
        float ref = 0.0f;
        { float p = 1.0f, q = 0.0f; Spring::Step(p, q, 0.0f, 100.0f, 20.0f, 0.3f); ref = p; }
        for (float c : {19.9999f, 19.9990f, 19.99f, 20.0001f, 20.001f, 20.01f, 20.2f}) {
            float p = 1.0f, q = 0.0f;
            Spring::Step(p, q, 0.0f, 100.0f, c, 0.3f);
            CHECK(std::fabs(p - ref) < 0.01f);
        }
        // One long step equals many short ones, on both sides and at critical.
        for (float c : {5.0f, 19.99f, 20.0f, 20.01f, 80.0f}) {
            float a = 1.0f, av = 3.0f, b = 1.0f, bv = 3.0f;
            Spring::Step(a, av, 0.0f, 100.0f, c, 1.0f);
            for (int i = 0; i < 100; ++i) Spring::Step(b, bv, 0.0f, 100.0f, c, 0.01f);
            CHECK(std::fabs(a - b) < 1e-3f && std::fabs(av - bv) < 1e-2f);
        }
    }

    BLOCK(16) {  // a very overdamped spring crawls at the right rate (the slow pole must not round to noise)
        float x = 1.0f, v = 0.0f;
        Spring::Step(x, v, 0.0f, 100.0f, 1.0e9f, 1.0e6f);       // slow rate k / c = 1e-7: exp(-0.1) after 1e6 s
        CHECK_NEAR(x, std::exp(-0.1), 2e-4);
        float y = 1.0f, w = 0.0f;
        for (int i = 0; i < 10; ++i) Spring::Step(y, w, 0.0f, 100.0f, 1.0e9f, 1.0e5f);
        CHECK_NEAR(y, x, 2e-4);                                  // ten steps of a tenth of it agree
        float z = 1.0f, u = 0.0f;
        Spring::Step(z, u, 0.0f, 100.0f, 1.0e4f, 100.0f);        // k / c = 0.01: exp(-1)
        CHECK_NEAR(z, std::exp(-1.0), 2e-3);
    }

    BLOCK(17) {  // an undamped spring keeps its energy; the Spring object and its helpers survive junk
        float x = 1.0f, v = 0.0f;
        const double e0 = 0.5 * 100.0 * x * x + 0.5 * v * v;
        for (int i = 0; i < 5000; ++i) Spring::Step(x, v, 0.0f, 100.0f, 0.0f, 0.016f);
        const double e1 = 0.5 * 100.0 * x * x + 0.5 * v * v;
        CHECK(std::fabs(e1 - e0) / e0 < 1e-3);

        Spring s;
        s.SetCritical(kNan);
        CHECK(std::isfinite(s.stiffness) && std::isfinite(s.damping));
        s.SetCritical(0.0f); CHECK(std::isfinite(s.stiffness) && s.stiffness > 0.0f);
        s.SetCritical(-1.0f); CHECK(std::isfinite(s.stiffness));
        s.SetCritical(kInf);                                     // never arrives: stays where it is, no NaN
        s.target = 5.0f; s.Update(0.1f);
        CHECK(std::isfinite(s.position));
        s.SetCritical(0.2f); s.target = 3.0f;
        for (int i = 0; i < 200; ++i) s.Update(0.016f);
        CHECK(s.AtRest(1e-2f));
        // A step of 0, negative, NaN or infinity changes nothing; a broken position snaps to the target.
        float p = 1.0f, q = 1.0f;
        Spring::Step(p, q, 0.0f, 100.0f, 10.0f, 0.0f);   CHECK(p == 1.0f && q == 1.0f);
        Spring::Step(p, q, 0.0f, 100.0f, 10.0f, -1.0f);  CHECK(p == 1.0f && q == 1.0f);
        Spring::Step(p, q, 0.0f, 100.0f, 10.0f, kNan);   CHECK(p == 1.0f && q == 1.0f);
        Spring::Step(p, q, 0.0f, 100.0f, 10.0f, kInf);   CHECK(p == 1.0f && q == 1.0f);
        Spring::Step(p, q, kNan, 100.0f, 10.0f, 0.1f);   CHECK(p == 1.0f && q == 1.0f);
        p = kNan;
        Spring::Step(p, q, 2.0f, 100.0f, 10.0f, 0.1f);   CHECK(p == 2.0f && q == 0.0f);
        // Quaternion springs: a zero-length target or a NaN does not poison the rotation.
        Quat r = Quat::Identity; Vec3 w(0, 0, 0);
        Spring::StepQuatCritical(r, w, Quat{0, 0, 0, 0}, 0.2f, 0.1f);
        CHECK(std::isfinite(r.x) && std::isfinite(r.w));
        Spring::StepQuatCritical(r, w, Quat::Euler(0, 90, 0), 0.2f, 100.0f);
        CHECK(std::fabs(Quat::Dot(r, Quat::Euler(0, 90, 0).Normalized())) > 0.9999f);
    }

    // -------------------------------------------------------------- clock cursor ----
    BLOCK(18) {  // the cursor: one tick gives its delta, a gap gives the sum, a reset resyncs, junk gives 0
        ClockCursor c;
        CHECK_NEAR(c.Take(5, 1.0, 0.016f), 0.016f, 1e-7);        // first look: this frame's share
        CHECK(c.Take(5, 1.0, 0.016f) == 0.0f);                   // same tick again: nothing
        CHECK_NEAR(c.Take(6, 1.016, 0.016f), 0.016f, 1e-7);
        CHECK_NEAR(c.Take(9, 1.064, 0.016f), 0.048f, 1e-6);      // three ticks since the last look
        CHECK(c.Take(1, 0.016, 0.016f) == 0.0f);                 // the clock was reset: resync, no jump backwards
        CHECK_NEAR(c.Take(2, 0.032, 0.016f), 0.016f, 1e-7);
        CHECK(c.Take(3, 0.048, kNan) == 0.0f);                   // a NaN delta counts as nothing
        CHECK(c.Take(4, 0.064, -1.0f) == 0.0f);
        CHECK(c.Take(5, 0.080, kInf) == 0.0f);
        CHECK_NEAR(c.Take(6, 0.096, 0.016f), 0.016f, 1e-7);      // and it recovers
        CHECK(c.Take(8, std::numeric_limits<double>::quiet_NaN(), 0.016f) == 0.0f);
        CHECK_NEAR(c.Take(9, 0.2, 0.016f), 0.016f, 1e-6);        // a NaN elapsed does not stick
    }

    TEST_MAIN_RESULT();
}
