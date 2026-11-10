// Tween sequencing and the juice helpers added with it: Prepend / JoinCallback / Length, sequence
// loops and yoyo, speed based tweens, ChangeEnd (retargeting a running tween), typed tweens for
// Vec2 / Vec3 / Color / Quat values you own, tween tags, falloff for punch and shake, and a tween
// that rides a Spline. Block N runs alone with `test_tween_sequencing N`.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace okay;

static int g_only = 0;
#define BLOCK(n) if (g_only == 0 || g_only == (n))

namespace {
std::shared_ptr<ValueTween> Lin(TweenManager& m, float& v, float to, float d) {
    auto t = m.To([&v] { return v; }, [&v](float x) { v = x; }, to, d);
    t->SetEase(Ease::Linear);
    return t;
}
}

int main(int argc, char** argv) {
    if (argc > 1) g_only = std::atoi(argv[1]);
    RUN_SUITE("tween_sequencing");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // -------------------------------------------------------- sequences ----
    BLOCK(1) {   // Prepend plays first, everything else moves later, the length grows by what was prepended
        TweenManager m;
        float a = 0, b = 0;
        auto seq = m.Sequence();
        seq->Append(Lin(m, a, 1.0f, 1.0f));
        int calls = 0;
        seq->AppendCallback([&] { ++calls; });            // at 1.0
        seq->Prepend(Lin(m, b, 1.0f, 1.0f));              // now b: 0..1, a: 1..2, callback at 2
        CHECK_NEAR(seq->Length(), 2.0f, 1e-5);
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(b, 0.5f, 1e-4); CHECK_NEAR(a, 0.0f, 1e-4);
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(b, 1.0f, 1e-4); CHECK_NEAR(a, 0.0f, 1e-4); CHECK(calls == 0);
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(a, 0.5f, 1e-4); CHECK(calls == 0);
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(a, 1.0f, 1e-4); CHECK(calls == 1);
        CHECK(m.Count() == 0);
    }

    BLOCK(2) {   // Prepend twice: the second one goes in front of the first; Join still means "with the last appended"
        TweenManager m;
        float a = 0, b = 0, c = 0, d = 0;
        auto seq = m.Sequence();
        seq->Append(Lin(m, a, 1.0f, 1.0f));
        seq->Join(Lin(m, d, 1.0f, 1.0f));                 // with a
        seq->Prepend(Lin(m, b, 1.0f, 0.5f));
        seq->Prepend(Lin(m, c, 1.0f, 0.25f));
        CHECK_NEAR(seq->Length(), 1.75f, 1e-5);
        m.Update(0.25f, 0.25f);
        CHECK_NEAR(c, 1.0f, 1e-4); CHECK_NEAR(b, 0.0f, 1e-4);
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(b, 1.0f, 1e-4); CHECK_NEAR(a, 0.0f, 1e-4);
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(a, 0.5f, 1e-4); CHECK_NEAR(d, 0.5f, 1e-4);
        seq->Join(Lin(m, d, 1.0f, 0.1f));                 // still joins at a's start (now 0.75)
        CHECK_NEAR(seq->Length(), 1.75f, 1e-5);
    }

    BLOCK(3) {   // Prepend of nothing, of itself, or of a sequence that holds it changes nothing
        TweenManager m;
        float a = 0;
        auto outer = m.Sequence();
        outer->Append(Lin(m, a, 1.0f, 1.0f));
        auto inner = m.Sequence();
        inner->Append(Lin(m, a, 2.0f, 1.0f));
        outer->Append(inner);
        const float before = outer->Length();
        outer->Prepend(nullptr);
        outer->Prepend(outer);
        inner->Prepend(outer);                            // would make a loop
        CHECK_NEAR(outer->Length(), before, 1e-6);
        CHECK(outer->Count() == 2 && inner->Count() == 1);
    }

    BLOCK(4) {   // JoinCallback fires when the last appended tween starts
        TweenManager m;
        float a = 0, b = 0;
        std::vector<int> order;
        auto seq = m.Sequence();
        seq->Append(Lin(m, a, 1.0f, 1.0f));
        seq->Append(Lin(m, b, 1.0f, 1.0f));
        seq->JoinCallback([&] { order.push_back(1); });   // at 1.0
        seq->AppendCallback([&] { order.push_back(2); }); // at 2.0
        seq->InsertCallback(0.5f, [&] { order.push_back(0); });
        m.Update(0.9f, 0.9f);
        CHECK(order == std::vector<int>{0});
        m.Update(0.2f, 0.2f);
        CHECK(order == (std::vector<int>{0, 1}));
        m.Update(1.0f, 1.0f);
        CHECK(order == (std::vector<int>{0, 1, 2}));
    }

    BLOCK(5) {   // a sequence loops like any tween: Restart replays it, Yoyo plays it back, OnStepComplete once per loop
        TweenManager m;
        float x = 0;
        int steps = 0, done = 0, calls = 0;
        auto seq = m.Sequence();
        seq->Append(Lin(m, x, 2.0f, 1.0f));
        seq->AppendCallback([&] { ++calls; });
        seq->SetLoops(3, LoopType::Restart).OnStepComplete([&] { ++steps; }).OnComplete([&] { ++done; });
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(x, 1.0f, 1e-4);
        m.Update(1.0f, 1.0f);                             // into the second loop
        CHECK(steps == 1 && calls == 1);
        CHECK_NEAR(x, 1.0f, 1e-4);                        // restarted: 0.5 s into loop two
        m.Update(5.0f, 5.0f);
        CHECK(steps == 3 && done == 1 && calls == 3);
        CHECK_NEAR(x, 2.0f, 1e-5);
        CHECK(m.Count() == 0);
    }

    BLOCK(6) {   // sequence yoyo: there and back, ends exactly on the start, completes once
        TweenManager m;
        float x = 0, y = 0;
        int steps = 0, done = 0, calls = 0;
        auto seq = m.Sequence();
        seq->Append(Lin(m, x, 2.0f, 1.0f));
        seq->Join(Lin(m, y, 10.0f, 1.0f));
        seq->AppendCallback([&] { ++calls; });                // at the end of the forward pass
        seq->SetLoops(2, LoopType::Yoyo).OnStepComplete([&] { ++steps; }).OnComplete([&] { ++done; });
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(x, 2.0f, 1e-4); CHECK_NEAR(y, 10.0f, 1e-4);
        m.Update(0.5f, 0.5f);                             // running back
        CHECK_NEAR(x, 1.0f, 1e-4); CHECK_NEAR(y, 5.0f, 1e-3);
        m.Update(0.5f, 0.5f);
        CHECK(x == 0.0f && y == 0.0f);                    // exactly back at the start
        CHECK(steps == 2 && done == 1);
        CHECK(calls == 1);                                // callbacks fire on the way forward only
    }

    BLOCK(7) {   // Goto, Rewind and Complete on a sequence
        TweenManager m;
        float x = 0, y = 0;
        int calls = 0, done = 0;
        auto seq = m.Sequence();
        seq->Append(Lin(m, x, 4.0f, 2.0f));
        seq->AppendCallback([&] { ++calls; });
        seq->Append(Lin(m, y, 6.0f, 3.0f));
        seq->OnComplete([&] { ++done; }).SetAutoKill(false);
        seq->Goto(1.0f);                                  // pauses there
        CHECK_NEAR(x, 2.0f, 1e-4); CHECK_NEAR(y, 0.0f, 1e-4); CHECK(calls == 0);
        seq->Goto(3.5f);
        CHECK_NEAR(x, 4.0f, 1e-4); CHECK_NEAR(y, 3.0f, 1e-4); CHECK(calls == 1);
        seq->Rewind();
        CHECK(x == 0.0f && y == 0.0f);
        seq->Complete();
        CHECK(x == 4.0f && y == 6.0f);
        CHECK(done == 1);
        CHECK(calls == 2);                                // the callback sits at 2 s and was passed once more
        seq->Complete();
        CHECK(done == 1);                                 // already at the end
    }

    BLOCK(8) {   // sequences inside sequences, with loops inside, keep their own phase
        TweenManager m;
        float x = 0, y = 0;
        auto inner = m.Sequence();
        inner->Append(Lin(m, x, 1.0f, 0.5f));
        inner->AppendInterval(0.5f);
        inner->SetLoops(2, LoopType::Restart);
        auto outer = m.Sequence();
        outer->Append(inner);
        outer->Append(Lin(m, y, 1.0f, 1.0f));
        CHECK_NEAR(outer->Length(), 3.0f, 1e-5);          // the inner loop counts as its loops (2 x 1 s), then the 1 s move
        m.Update(1.25f, 1.25f);
        CHECK_NEAR(x, 0.5f, 1e-3);                        // 0.25 s into the second pass
        CHECK_NEAR(y, 0.0f, 1e-4);
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(y, 0.25f, 1e-3);
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(y, 1.0f, 1e-5);
        CHECK(m.Count() == 0);
    }

    // ------------------------------------------------------ speed based ----
    BLOCK(9) {   // a speed based move: length = distance / speed, worked out when it starts
        Scene s;
        GameObject* go = s.CreateGameObject("Mover");
        go->transform->localPosition = {1, 0, 0};
        auto t = DOTween::LocalMove(go, {7, 0, 0}, 3.0f);   // 6 units at 3 units a second
        t->SetEase(Ease::Linear).SetSpeedBased();
        CHECK_NEAR(t->Duration(), 3.0f, 1e-6);              // still the speed until it starts
        s.Update(1.0f);
        CHECK_NEAR(t->Duration(), 2.0f, 1e-5);
        CHECK_NEAR(go->transform->localPosition.x, 4.0f, 1e-3);
        s.Update(0.99f);
        CHECK(t->IsActive());
        s.Update(0.1f);
        CHECK(!t->IsActive());
        CHECK(go->transform->localPosition.x == 7.0f);
    }

    BLOCK(10) {  // speed based: 3D distance, delay, relative, From, loops, yoyo, and a second tween with a different distance
        Scene s;
        GameObject* go = s.CreateGameObject("Mover");
        auto t = DOTween::LocalMove(go, {3, 4, 0}, 5.0f);   // distance 5 at speed 5: one second
        t->SetEase(Ease::Linear).SetSpeedBased().SetDelay(0.5f).SetLoops(2, LoopType::Yoyo);
        s.Update(0.5f);                                     // the delay
        CHECK(t->IsActive() && go->transform->localPosition.SqrMagnitude() == 0.0f);
        s.Update(1.0f);
        CHECK_NEAR(t->Duration(), 1.0f, 1e-5);
        CHECK_NEAR(go->transform->localPosition.x, 3.0f, 1e-3); CHECK_NEAR(go->transform->localPosition.y, 4.0f, 1e-3);
        s.Update(1.1f);
        CHECK(!t->IsActive());
        CHECK(go->transform->localPosition.SqrMagnitude() == 0.0f);   // yoyo twice: back at the start

        go->transform->localPosition = {0, 0, 0};
        auto r = DOTween::LocalMove(go, {0, 6, 0}, 2.0f);   // relative: 6 up from where it starts, at speed 2
        r->SetEase(Ease::Linear).SetSpeedBased().SetRelative();
        s.Update(1.5f);
        CHECK_NEAR(go->transform->localPosition.y, 3.0f, 1e-3);
        CHECK_NEAR(r->Duration(), 3.0f, 1e-5);
    }

    BLOCK(11) {  // speed based edge cases: zero, negative, NaN, infinite speed and a zero distance are an instant tween, never a hang
        for (float speed : {0.0f, -3.0f, nan, 1.0e-30f}) {
            Scene s;
            GameObject* go = s.CreateGameObject("Mover");
            auto t = DOTween::LocalMove(go, {5, 0, 0}, speed);
            t->SetEase(Ease::Linear).SetSpeedBased();
            s.Update(0.1f);
            s.Update(0.1f);
            CHECK(!t->IsActive());
            CHECK(go->transform->localPosition.x == 5.0f);
        }
        Scene s;
        GameObject* go = s.CreateGameObject("Mover");
        auto none = DOTween::LocalMove(go, {0, 0, 0}, 4.0f);   // already there: nothing to travel
        none->SetSpeedBased();
        s.Update(0.1f); s.Update(0.1f);
        CHECK(!none->IsActive());
        auto fast = DOTween::LocalMove(go, {1, 0, 0}, inf);    // infinite speed: instant
        fast->SetSpeedBased();
        s.Update(0.1f); s.Update(0.1f);
        CHECK(!fast->IsActive() && go->transform->localPosition.x == 1.0f);
    }

    BLOCK(12) {  // a tween that cannot measure a distance ignores the speed flag; one inside a sequence reads `duration` as seconds
        TweenManager m;
        Quat q = Quat::Identity;
        auto rot = m.ToQuat([&] { return q; }, [&](const Quat& v) { q = v; }, Quat::Euler(0, 90, 0), 2.0f);
        rot->SetSpeedBased().SetEase(Ease::Linear);
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(rot->Duration(), 2.0f, 1e-6);
        CHECK(rot->IsActive());
        float x = 0;
        auto seq = m.Sequence();
        auto child = Lin(m, x, 10.0f, 5.0f);
        child->SetSpeedBased();
        seq->Append(child);
        m.Update(2.5f, 2.5f);
        CHECK_NEAR(x, 5.0f, 1e-3);                            // 5 "seconds", half done at 2.5
    }

    // ---------------------------------------------------------- ChangeEnd ----
    BLOCK(13) {  // aim a running tween at a new end: it continues from the same start towards the new value
        TweenManager m;
        float v = 0;
        auto t = Lin(m, v, 10.0f, 2.0f);
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(v, 5.0f, 1e-4);
        t->ChangeEnd(20.0f);                                  // start stays 0, progress stays 0.5
        m.Update(0.0f, 0.0f);
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(v, 15.0f, 1e-3);
        m.Update(1.0f, 1.0f);
        CHECK(v == 20.0f);
        CHECK(!t->IsActive());
    }

    BLOCK(14) {  // ChangeEnd before the tween starts replaces the end; with relative it is an offset; with From it moves the start of the motion
        TweenManager m;
        float a = 1, b = 1, c = 1;
        auto ta = Lin(m, a, 10.0f, 1.0f); ta->SetDelay(1.0f);
        ta->ChangeEnd(3.0f);
        auto tb = Lin(m, b, 4.0f, 1.0f); tb->SetRelative();      // 1 -> 5
        m.Update(0.5f, 0.5f);
        tb->ChangeEnd(9.0f);                                      // 1 -> 10
        auto tc = Lin(m, c, 5.0f, 1.0f); tc->From();             // plays from 5 to the current 1
        m.Update(0.0f, 0.0f);
        tc->ChangeEnd(7.0f);                                      // now from 7 to 1
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(b, 1.0f + 9.0f * 1.0f, 1e-3);                  // finished: 1 + 9
        CHECK(!tb->IsActive());
        m.Update(1.0f, 1.0f);
        CHECK(a == 3.0f);
        CHECK(c == 1.0f);                                         // From ends on the value it started from
    }

    BLOCK(15) {  // ChangeEnd with a NaN or infinite value leaves that part alone; typed overloads for Vec2, Vec3, Color
        TweenManager m;
        Vec3 p{0, 0, 0};
        auto t = m.ToVec3([&] { return p; }, [&](const Vec3& v) { p = v; }, {10, 10, 10}, 1.0f);
        t->SetEase(Ease::Linear);
        m.Update(0.5f, 0.5f);
        t->ChangeEnd(Vec3{20.0f, nan, inf});
        m.Update(0.5f, 0.5f);
        CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        CHECK(p.x == 20.0f);
        CHECK(p.y == 10.0f && p.z == 10.0f);
        Vec2 q{0, 0};
        auto t2 = m.ToVec2([&] { return q; }, [&](const Vec2& v) { q = v; }, {1, 1}, 1.0f);
        t2->SetEase(Ease::Linear);
        m.Update(0.25f, 0.25f);
        t2->ChangeEnd(Vec2{5, 9});
        m.Update(1.0f, 1.0f);
        CHECK(q.x == 5.0f && q.y == 9.0f);
        Color c{0, 0, 0, 1};
        auto t3 = m.ToColor([&] { return c; }, [&](const Color& v) { c = v; }, Color{1, 0, 0, 1}, 1.0f);
        t3->SetEase(Ease::Linear);
        t3->ChangeEnd(Color{0, 1, 0, 0.5f});
        m.Update(2.0f, 2.0f);
        CHECK(c.r == 0.0f && c.g == 1.0f && c.a == 0.5f);
    }

    // ------------------------------------------------------- typed values ----
    BLOCK(16) {  // Vec2, Vec3 and Color tweens of values you own; loops and relative work
        TweenManager m;
        Vec2 a{1, 2};
        Vec3 b{0, 0, 0};
        Color c{1, 1, 1, 1};
        auto ta = m.ToVec2([&] { return a; }, [&](const Vec2& v) { a = v; }, {3, 6}, 2.0f);
        auto tb = m.ToVec3([&] { return b; }, [&](const Vec3& v) { b = v; }, {2, 4, 8}, 1.0f);
        auto tc = m.ToColor([&] { return c; }, [&](const Color& v) { c = v; }, Color{0, 0, 0, 0}, 1.0f);
        ta->SetEase(Ease::Linear); tb->SetEase(Ease::Linear).SetLoops(2, LoopType::Yoyo); tc->SetEase(Ease::Linear);
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(a.x, 1.5f, 1e-4); CHECK_NEAR(a.y, 3.0f, 1e-4);
        CHECK_NEAR(b.x, 1.0f, 1e-4); CHECK_NEAR(b.y, 2.0f, 1e-4); CHECK_NEAR(b.z, 4.0f, 1e-4);
        CHECK_NEAR(c.r, 0.5f, 1e-4); CHECK_NEAR(c.a, 0.5f, 1e-4);
        m.Update(1.5f, 1.5f);
        CHECK(a.x == 3.0f && a.y == 6.0f);
        CHECK(b.x == 0.0f && b.y == 0.0f && b.z == 0.0f);        // yoyo: back where it began
        CHECK(c.r == 0.0f && c.a == 0.0f);
        CHECK(m.Count() == 0);

        Vec3 r{1, 1, 1};
        auto rel = m.ToVec3([&] { return r; }, [&](const Vec3& v) { r = v; }, {1, 2, 3}, 1.0f);
        rel->SetEase(Ease::Linear).SetRelative();
        m.Update(1.0f, 1.0f);
        CHECK(r.x == 2.0f && r.y == 3.0f && r.z == 4.0f);
    }

    BLOCK(17) {  // a null getter or setter is harmless
        TweenManager m;
        auto a = m.ToVec3(nullptr, nullptr, {1, 1, 1}, 1.0f);
        auto b = m.ToColor(nullptr, nullptr, Color::Red, 1.0f);
        auto c = m.ToVec2(nullptr, nullptr, {1, 1}, 1.0f);
        auto d = m.ToQuat(nullptr, nullptr, Quat::Euler(0, 45, 0), 1.0f);
        m.Update(2.0f, 2.0f);
        CHECK(!a->IsActive() && !b->IsActive() && !c->IsActive() && !d->IsActive());
    }

    BLOCK(18) {  // a Quat tween takes the short way round, ends exactly on the target, and can be relative or From
        TweenManager m;
        Quat q = Quat::Euler(0, 350, 0);
        auto t = m.ToQuat([&] { return q; }, [&](const Quat& v) { q = v; }, Quat::Euler(0, 10, 0), 1.0f);
        t->SetEase(Ease::Linear);
        m.Update(0.5f, 0.5f);
        // Half way of the 20 degree turn that passes 0: the y rotation is 0 (360), not 180.
        const Vec3 fwd = q * Vec3::Forward;
        CHECK_NEAR(fwd.x, 0.0f, 1e-3); CHECK_NEAR(fwd.z, 1.0f, 1e-3);
        m.Update(0.5f, 0.5f);
        CHECK(Quat::Dot(q, Quat::Euler(0, 10, 0).Normalized()) > 0.99999f || Quat::Dot(q, Quat::Euler(0, 10, 0).Normalized()) < -0.99999f);

        Quat r = Quat::Euler(0, 30, 0);
        auto rel = m.ToQuat([&] { return r; }, [&](const Quat& v) { r = v; }, Quat::Euler(0, 60, 0), 1.0f);
        rel->SetRelative().SetEase(Ease::Linear);
        m.Update(1.0f, 1.0f);
        const Quat want = (Quat::Euler(0, 30, 0) * Quat::Euler(0, 60, 0)).Normalized();
        CHECK(std::fabs(Quat::Dot(r, want)) > 0.99999f);

        Quat f = Quat::Euler(0, 30, 0);
        auto from = m.ToQuat([&] { return f; }, [&](const Quat& v) { f = v; }, Quat::Euler(0, 90, 0), 1.0f);
        from->From().SetEase(Ease::Linear);
        m.Update(0.0f, 0.0f);
        CHECK(std::fabs(Quat::Dot(f, Quat::Euler(0, 90, 0).Normalized())) > 0.9999f);   // starts at the end value
        m.Update(1.0f, 1.0f);
        CHECK(std::fabs(Quat::Dot(f, Quat::Euler(0, 30, 0).Normalized())) > 0.99999f);  // and arrives at the current one
    }

    // ---------------------------------------------------------------- tags ----
    BLOCK(19) {  // tags group tweens: kill, pause, play, complete and count them together; the id and tag are independent
        TweenManager m;
        float a = 0, b = 0, c = 0, d = 0;
        auto ta = Lin(m, a, 1.0f, 1.0f); ta->SetTag("menu").SetId("one");
        auto tb = Lin(m, b, 1.0f, 1.0f); tb->SetTag("menu");
        auto tc = Lin(m, c, 1.0f, 1.0f); tc->SetTag("hud");
        auto td = Lin(m, d, 1.0f, 1.0f);
        CHECK(m.CountByTag("menu") == 2 && m.CountByTag("hud") == 1 && m.CountByTag("") == 0 && m.CountByTag("none") == 0);
        CHECK(m.WithTag("menu").size() == 2 && m.WithTag("").empty());
        CHECK(m.PauseByTag("menu") == 2);
        m.Update(0.5f, 0.5f);
        CHECK(a == 0.0f && b == 0.0f && c == 0.5f && d == 0.5f);
        CHECK(m.PlayByTag("menu") == 2);
        m.Update(0.25f, 0.25f);
        CHECK_NEAR(a, 0.25f, 1e-4);
        CHECK(m.CompleteByTag("hud") == 1);
        CHECK(c == 1.0f);
        CHECK(m.KillByTag("menu", true) == 2);                // complete, then remove
        CHECK(a == 1.0f && b == 1.0f);
        CHECK(!ta->IsActive() && !tb->IsActive() && td->IsActive());
        CHECK(m.CountByTag("menu") == 0);
        CHECK(m.Rewind("one") == 0);                          // already gone
    }

    BLOCK(20) {  // Rewind by id restores the start and pauses
        TweenManager m;
        float a = 0;
        auto t = Lin(m, a, 10.0f, 2.0f);
        t->SetId("slide").SetAutoKill(false);
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(a, 5.0f, 1e-4);
        CHECK(m.Rewind("slide") == 1);
        CHECK(a == 0.0f && t->paused);
        CHECK(m.Play("slide") == 1);
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(a, 5.0f, 1e-4);
    }

    BLOCK(21) {  // a tween handed to a second manager leaves the first (it must not be advanced twice)
        TweenManager first, second;
        float v = 0;
        auto t = Lin(first, v, 10.0f, 10.0f);
        CHECK(first.Count() == 1);
        second.Add(t);
        CHECK(first.Count() == 0 && second.Count() == 1);
        first.Update(1.0f, 1.0f);
        CHECK(v == 0.0f);
        second.Update(1.0f, 1.0f);
        CHECK_NEAR(v, 1.0f, 1e-4);
        second.Update(1.0f, 1.0f);
        CHECK_NEAR(v, 2.0f, 1e-4);                            // once per frame, not twice
    }

    // ------------------------------------------------------- decay (juice) ----
    BLOCK(22) {  // Easing::Decay: 1 at the start, 0 at the end, falling faster for a larger rate, a straight line for 0
        CHECK(Easing::Decay(0.0f, 5.0f) == 1.0f);
        CHECK(Easing::Decay(1.0f, 5.0f) == 0.0f);
        CHECK_NEAR(Easing::Decay(0.5f, 0.0f), 0.5f, 1e-6);
        CHECK_NEAR(Easing::Decay(0.5f, nan), 0.5f, 1e-6);
        CHECK_NEAR(Easing::Decay(0.5f, -3.0f), 0.5f, 1e-6);
        float last = 1.0f;
        bool monotonic = true;
        for (int i = 1; i <= 20; ++i) {
            const float k = Easing::Decay((float)i / 20.0f, 6.0f);
            monotonic = monotonic && k <= last && k >= 0.0f;
            last = k;
        }
        CHECK(monotonic);
        CHECK(Easing::Decay(0.25f, 6.0f) < Easing::Decay(0.25f, 2.0f));
        CHECK(Easing::Decay(0.25f, 2.0f) < Easing::Decay(0.25f, 0.0f));
        CHECK(std::isfinite(Easing::Decay(0.5f, inf)) && std::isfinite(Easing::Decay(0.5f, 1.0e30f)));
        CHECK(Easing::Decay(nan, 5.0f) == 1.0f);
    }

    BLOCK(23) {  // punch and shake with decay settle exactly back where they started; the first swing is bigger than with the straight fade
        Scene s;
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        a->transform->localPosition = {1, 2, 3};
        b->transform->localPosition = {1, 2, 3};
        auto pa = DOTween::PunchPosition(a, {4, 0, 0}, 1.0f, 8, 1.0f);
        auto pb = DOTween::PunchPosition(b, {4, 0, 0}, 1.0f, 8, 1.0f);
        pb->SetDecay(6.0f);
        float maxA = 0, maxB = 0, lateA = 0, lateB = 0;
        for (int i = 1; i <= 101; ++i) {             // 101 frames: a hundred 0.01f steps are a hair short of a second
            s.Update(0.01f);
            const float ea = std::fabs(a->transform->localPosition.x - 1.0f);
            const float eb = std::fabs(b->transform->localPosition.x - 1.0f);
            if (i <= 40) { maxA = std::max(maxA, ea); maxB = std::max(maxB, eb); }
            if (i > 60 && i < 100) { lateA = std::max(lateA, ea); lateB = std::max(lateB, eb); }
        }
        CHECK(!pa->IsActive() && !pb->IsActive());
        CHECK(a->transform->localPosition.x == 1.0f && b->transform->localPosition.x == 1.0f);
        CHECK(a->transform->localPosition.y == 2.0f && b->transform->localPosition.z == 3.0f);
        CHECK(lateB < lateA);                                  // the decayed punch is quiet at the end
        CHECK(maxB > 0.0f && maxA > 0.0f);
        CHECK(maxB <= maxA + 1e-4f);                           // the decayed punch is never louder than the straight fade
        CHECK(lateB < 0.35f * maxB);

        GameObject* c = s.CreateGameObject("C");
        auto sh = DOTween::ShakePosition(c, 1.0f, {1, 1, 0}, 12, true);
        sh->SetDecay(8.0f);
        float early = 0, late = 0;
        for (int i = 1; i <= 101; ++i) {
            s.Update(0.01f);
            const float mag = std::fabs(c->transform->localPosition.x) + std::fabs(c->transform->localPosition.y);
            if (i <= 30) early = std::max(early, mag);
            if (i > 80 && i < 100) late = std::max(late, mag);
        }
        CHECK(!sh->IsActive());
        CHECK(c->transform->localPosition.x == 0.0f && c->transform->localPosition.y == 0.0f);
        CHECK(early > 0.0f && late < early);
        // SetDecay with junk keeps the straight fade.
        auto junk = DOTween::PunchScale(c, {1, 1, 1}, 1.0f);
        junk->SetDecay(nan).SetDecay(-5.0f);
        CHECK(junk->decay == 0.0f);
    }

    // ------------------------------------------------------------- spline ----
    BLOCK(24) {  // a tween rides a Spline at constant speed, ends on the last point, can face along it and loop
        Scene s;
        GameObject* rail = s.CreateGameObject("Rail");
        auto* sp = rail->AddComponent<Spline>();
        sp->AddPoint({0, 0, 0});
        sp->AddPoint({10, 0, 0});
        GameObject* rider = s.CreateGameObject("Rider");
        auto t = DOTween::AlongSpline(rider, rail, 2.0f, true);
        t->SetEase(Ease::Linear);
        CHECK(t->IsActive());
        s.Update(0.5f);
        CHECK_NEAR(rider->transform->Position().x, 2.5f, 0.1f);
        const Vec3 f = rider->transform->Forward();
        CHECK(f.x > 0.99f);                                    // facing along the rail
        s.Update(0.5f);
        CHECK_NEAR(rider->transform->Position().x, 5.0f, 0.1f);
        s.Update(1.0f);
        CHECK_NEAR(rider->transform->Position().x, 10.0f, 0.01f);
        CHECK(!t->IsActive());

        // Yoyo on a ping-pong ride: back at the start.
        GameObject* r2 = s.CreateGameObject("Rider2");
        auto y = DOTween::AlongSpline(r2, rail, 1.0f);
        y->SetEase(Ease::Linear).SetLoops(2, LoopType::Yoyo);
        s.Update(1.0f);
        CHECK_NEAR(r2->transform->Position().x, 10.0f, 0.01f);
        s.Update(1.0f);
        CHECK_NEAR(r2->transform->Position().x, 0.0f, 0.01f);
    }

    BLOCK(25) {  // the spline going away mid-ride just stops the effect; bad arguments give an inactive tween
        Scene s;
        GameObject* rail = s.CreateGameObject("Rail");
        auto* sp = rail->AddComponent<Spline>();
        sp->AddPoint({0, 0, 0});
        sp->AddPoint({0, 10, 0});
        GameObject* rider = s.CreateGameObject("Rider");
        auto t = DOTween::AlongSpline(rider, rail, 2.0f);
        t->SetEase(Ease::Linear);
        s.Update(0.5f);
        const float y = rider->transform->Position().y;
        CHECK(y > 1.0f);
        s.Destroy(rail);
        s.Update(0.25f);                                       // the rail is freed at the end of this update
        const float after = rider->transform->Position().y;
        s.Update(0.25f);
        CHECK(rider->transform->Position().y == after);        // the rail is gone: the tween no longer moves the rider
        s.Update(2.0f);
        CHECK(!t->IsActive());

        GameObject* plain = s.CreateGameObject("NoSpline");
        CHECK(!DOTween::AlongSpline(rider, plain, 1.0f)->IsActive());
        CHECK(!DOTween::AlongSpline(rider, nullptr, 1.0f)->IsActive());
        CHECK(!DOTween::AlongSpline(nullptr, plain, 1.0f)->IsActive());

        GameObject* rail2 = s.CreateGameObject("Rail2");
        rail2->AddComponent<Spline>();                         // a spline with no points: nothing to ride
        auto e = DOTween::AlongSpline(rider, rail2, 1.0f);
        s.Update(1.5f);
        CHECK(!e->IsActive() && std::isfinite(rider->transform->Position().y));
    }

    // ------------------------------------------------------ non-finite input ----
    BLOCK(26) {  // an end value that is not a number, or an ease that returns one, never writes NaN into the object
        TweenManager m;
        float v = 5.0f;
        Lin(m, v, nan, 1.0f);
        m.Update(0.5f, 0.5f); m.Update(0.5f, 0.5f);
        CHECK(std::isfinite(v) && v == 5.0f);                  // stays where it was

        float w = 1.0f;
        auto u = m.To([&] { return w; }, [&](float x) { w = x; }, 3.0f, 1.0f);
        u->SetEaseCurve([](float) { return std::numeric_limits<float>::quiet_NaN(); });
        m.Update(0.5f, 0.5f);
        CHECK(std::isfinite(w));
        m.Update(0.5f, 0.5f);
        CHECK(w == 3.0f);

        // The object itself was already broken: the tween snaps to the end instead of carrying NaN along.
        float z = nan;
        Lin(m, z, 2.0f, 1.0f);
        m.Update(0.5f, 0.5f);
        CHECK(std::isfinite(z));
        m.Update(0.5f, 0.5f);
        CHECK(z == 2.0f);
        float inf1 = 0;
        Lin(m, inf1, inf, 1.0f);
        m.Update(1.0f, 1.0f);
        CHECK(std::isfinite(inf1));
    }

    // ---------------------------------------------------------------- scripts ----
    BLOCK(27) {  // the new script builtins: tags, speed based, decay, prepend, join callback, rewind by id
        Scene s("sc"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Box");
        go->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var calls = 0; var a = 0; var b = 0; var killed = 0; var paused = 0;\n"
            "function start() {\n"
            "  a = tween_loops(tween_scale(2, 1.0, \"linear\"), -1, \"yoyo\");\n"
            "  tween_tag(a, \"menu\");\n"
            "  b = tween_move(6, 0, 3.0, \"linear\");\n"
            "  tween_speed_based(b);\n"
            "  var seq = seq_new();\n"
            "  seq_append(seq, tween_call(1.0, \"hit\"));\n"
            "  seq_join_callback(seq, \"hit\");\n"
            "  seq_prepend(seq, tween_call(0.5, \"hit\"));\n"
            "  var p = tween_punch_rotate(30, 1.0, 6);\n"
            "  tween_decay(p, 6);\n"
            "}\n"
            "function hit() { calls = calls + 1; }\n"
            "function pause_menu() { paused = tween_pause_tag(\"menu\"); }\n"
            "function play_menu() { tween_play_tag(\"menu\"); }\n"
            "function kill_menu() { killed = tween_kill_tag(\"menu\"); }\n"
            "function rewind_it() { tween_set_id(b, \"slide\"); tween_rewind_id(\"slide\"); }\n");
        auto* sc = go->GetComponent<ScriptComponent>();
        s.Start();
        s.Update(1.0f);
        CHECK_NEAR(go->transform->localPosition.x, 3.0f, 0.05f);        // 6 units at speed 3: 2 s, so half way after 1 s
        CHECK(sc->VM()->GetGlobal("calls").AsFloat() == 2.0f);          // the prepended call (0.5 s) and the joined callback (0.5 s)
        sc->VM()->CallEvent("pause_menu");
        CHECK(sc->VM()->GetGlobal("paused").AsFloat() == 1.0f);
        const float scale = go->transform->localScale.x;
        s.Update(0.25f);
        CHECK_NEAR(go->transform->localScale.x, scale, 1e-4);           // paused by its tag
        sc->VM()->CallEvent("play_menu");
        s.Update(0.25f);
        CHECK(std::fabs(go->transform->localScale.x - scale) > 1e-4);
        CHECK(sc->VM()->GetGlobal("calls").AsFloat() == 3.0f);          // the appended call ends the sequence at 1.5 s
        sc->VM()->CallEvent("kill_menu");
        CHECK(sc->VM()->GetGlobal("killed").AsFloat() == 1.0f);
        s.Update(1.0f);
        CHECK(sc->VM()->GetGlobal("calls").AsFloat() == 3.0f);
        CHECK_NEAR(go->transform->localPosition.x, 6.0f, 1e-3);
        sc->VM()->CallEvent("rewind_it");                                // by id: back at the start, paused
        CHECK_NEAR(go->transform->localPosition.x, 6.0f, 1e-3);          // the tween had finished and is gone: nothing to rewind
    }

    BLOCK(28) {  // script timers with a NaN time: after() fires on the next tick, every() runs, neither is a timer that never ends
        Scene s("sc"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Box");
        go->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var hits = 0; var ticks = 0;\n"
            "function start() { after(sqrt(0 - 1), \"hit\"); after(0 - 5, \"hit\"); every(sqrt(0 - 1), \"tick\"); }\n"
            "function hit() { hits = hits + 1; }\n"
            "function tick() { ticks = ticks + 1; }\n");
        auto* sc = go->GetComponent<ScriptComponent>();
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(0.1f);
        CHECK(sc->VM()->GetGlobal("hits").AsFloat() == 2.0f);            // both, once
        CHECK(sc->VM()->GetGlobal("ticks").AsFloat() > 5.0f);            // the shortest interval, bounded per frame
        CHECK(sc->VM()->GetGlobal("ticks").AsFloat() <= 5.0f * 64.0f);
        s.Update(nan);                                                   // a NaN frame time does not stop the timers for good
        s.Update(0.1f);
        CHECK(std::isfinite(sc->VM()->GetGlobal("ticks").AsFloat()));
    }

    TEST_MAIN_RESULT();
}
