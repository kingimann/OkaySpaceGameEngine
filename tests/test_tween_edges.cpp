// Tween edge cases found in the 1.2 audit: a callback that moves a tween into a
// sequence, NaN / infinite settings, a removed TweenAnimation, and a visual-script
// tween on an object that is destroyed. Block N runs alone with `test_tween_edges N`.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace okay;

static int g_only = 0;
#define BLOCK(n) if (g_only == 0 || g_only == (n))

int main(int argc, char** argv) {
    if (argc > 1) g_only = std::atoi(argv[1]);
    RUN_SUITE("tween_edges");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    auto linear = [](TweenManager& m, float& v, float to, float d) {
        auto t = m.To([&v] { return v; }, [&v](float x) { v = x; }, to, d);
        t->SetEase(Ease::Linear);
        return t;
    };

    BLOCK(1) {   // Moving a tween into a sequence from a callback must not make the next tween skip its frame.
        TweenManager m;
        float a = 0, b = 0, c = 0;
        auto ta = linear(m, a, 10.0f, 1.0f);
        auto tb = linear(m, b, 10.0f, 1.0f);
        auto tc = linear(m, c, 10.0f, 1.0f);
        auto seq = m.Sequence();
        bool moved = false;
        tb->OnUpdate([&] { if (!moved) { moved = true; seq->Append(ta); } });
        m.Update(0.5f, 0.5f);
        CHECK(moved);
        CHECK_NEAR(b, 5.0f, 1e-4f);
        CHECK_NEAR(c, 5.0f, 1e-4f);                   // the one after it still got its time
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(c, 10.0f, 1e-4f);
        CHECK_NEAR(a, 10.0f, 1e-4f);                  // now driven by the sequence
    }
    BLOCK(2) {   // The same when the running tween itself moves into a sequence.
        TweenManager m;
        float a = 0, b = 0;
        auto ta = linear(m, a, 10.0f, 1.0f);
        auto tb = linear(m, b, 10.0f, 1.0f);
        auto seq = m.Sequence();
        bool moved = false;
        ta->OnUpdate([&] { if (!moved) { moved = true; seq->Append(ta); } });
        m.Update(0.5f, 0.5f);
        CHECK_NEAR(b, 5.0f, 1e-4f);
    }

    BLOCK(3) {   // NaN and infinite durations behave like very short / very long ones.
        TweenManager m;
        float v = 0, w = 0;
        int done = 0;
        auto t = linear(m, v, 5.0f, nan);
        t->OnComplete([&] { ++done; });
        auto u = linear(m, w, 5.0f, inf);
        m.Update(0.1f, 0.1f);
        CHECK(done == 1);
        CHECK(v == 5.0f);
        CHECK(std::isfinite(w));
        CHECK(w >= 0.0f && w < 0.001f);               // an endless wait: hardly moved
        m.Update(1.0f, 1.0f);
        CHECK(std::isfinite(w));
        CHECK(u->IsActive());
        // A duration set directly is read the same way.
        float x = 0;
        auto d = linear(m, x, 1.0f, 1.0f);
        d->duration = nan;
        m.Update(0.1f, 0.1f);
        CHECK(x == 1.0f && !d->IsActive());
        CHECK(std::isfinite(t->Duration()) && std::isfinite(u->Duration()));
    }

    BLOCK(4) {   // A NaN speed or Goto must not poison the timeline for good.
        TweenManager m;
        float v = 0;
        auto t = linear(m, v, 10.0f, 1.0f);
        t->SetSpeed(nan);
        m.Update(0.1f, 0.1f);
        t->SetSpeed(1.0f);
        m.Update(0.5f, 0.5f);
        CHECK(std::isfinite(v));
        CHECK_NEAR(v, 5.0f, 1e-3f);

        float w = 0;
        auto g = linear(m, w, 10.0f, 1.0f);
        g->Goto(nan);
        CHECK(std::isfinite(w));
        CHECK(std::isfinite(g->Elapsed()));
        g->Goto(0.5f);
        CHECK_NEAR(w, 5.0f, 1e-3f);

        // Non-finite delay, loops with a NaN-length child in a sequence.
        float z = 0;
        auto h = linear(m, z, 1.0f, 1.0f);
        h->SetDelay(nan);
        m.Update(0.25f, 0.25f);
        CHECK(std::isfinite(z));
        auto seq = m.Sequence();
        seq->Insert(nan, linear(m, z, 3.0f, 0.5f));
        seq->AppendInterval(nan);
        m.Update(0.25f, 0.25f);
        CHECK(std::isfinite(z));
    }

    BLOCK(5) {   // Removing a TweenAnimation stops its tween.
        Scene s("S");
        GameObject* go = s.CreateGameObject("Logo");
        auto* ta = go->AddComponent<TweenAnimation>();
        ta->type = (int)TweenAnimation::Type::LocalMove;
        ta->endValue = {0, 10, 0};
        ta->duration = 1.0f;
        ta->ease = (int)Ease::Linear;
        ta->loops = -1;
        ta->loopType = (int)LoopType::Yoyo;
        s.Start();
        s.Update(0.25f);
        CHECK_NEAR(go->transform->localPosition.y, 2.5f, 1e-3f);
        CHECK(s.tweens().Count() == 1);
        go->RemoveComponent(ta);
        s.Update(0.25f);
        s.Update(0.25f);
        CHECK(s.tweens().Count() == 0);
        const float y = go->transform->localPosition.y;
        s.Update(0.25f);
        CHECK_NEAR(go->transform->localPosition.y, y, 1e-6f);   // nothing keeps moving it
    }

    BLOCK(6) {   // A visual-script tween on an object that is destroyed does not outlive it.
        ActionList::ResetVars();
        Scene s("S");
        s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("Hero");
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = { ActionList::Item{"tween_move", {"5", "0", "1.0"}},
                             ActionList::Item{"shake", {"1", "1.0"}},
                             ActionList::Item{"punch_scale", {"0.5", "1.0"}} };
        s.Start();
        s.Update(0.1f);
        CHECK(s.scheduler().ActiveCount() == 3);
        s.Destroy(o);
        s.Update(0.1f);                               // the object is gone at the end of this frame
        s.Update(0.1f);                               // the tweens must not touch it again
        CHECK(s.scheduler().ActiveCount() == 0);
    }

    BLOCK(7) {   // A tween callback that throws ends that tween only: the others still get their time.
        TweenManager m;
        float a = 0, b = 0, c = 0;
        int killed = 0;
        auto ta = linear(m, a, 10.0f, 1.0f);
        auto tb = linear(m, b, 10.0f, 1.0f);
        auto tc = linear(m, c, 10.0f, 1.0f);
        tb->OnUpdate([] { throw std::runtime_error("boom"); });
        ta->OnKill([&] { ++killed; });
        bool threw = false;
        try { m.Update(0.5f, 0.5f); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
        CHECK_NEAR(a, 5.0f, 1e-4f);
        CHECK_NEAR(c, 5.0f, 1e-4f);                   // after the thrower in the list, still updated
        CHECK(!tb->IsActive());
        CHECK(m.Count() == 2);
        m.Update(0.5f, 0.5f);                         // nothing throws again
        CHECK_NEAR(c, 10.0f, 1e-4f);
        CHECK(m.Count() == 0 && killed == 1);
        // The manager works normally afterwards, including moving a tween into a sequence.
        float x = 0, y = 0;
        auto tx = linear(m, x, 1.0f, 1.0f);
        auto ty = linear(m, y, 1.0f, 1.0f);
        tx->OnComplete([] { throw std::logic_error("done"); });
        try { m.Update(1.0f, 1.0f); } catch (const std::logic_error&) {}
        CHECK_NEAR(y, 1.0f, 1e-5f);
        auto seq = m.Sequence();
        seq->Append(linear(m, x, 2.0f, 1.0f));
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(x, 2.0f, 1e-4f);
    }

    BLOCK(8) {   // Random control calls from inside callbacks (kill, complete, restart, goto, sequences): values stay sane.
        unsigned rng = 77123u;
        auto next = [&] { rng = rng * 1664525u + 1013904223u; return (int)(rng >> 8); };
        auto unit = [&] { return (float)(next() % 1000) / 1000.0f; };
        long callbacks = 0;
        for (int round = 0; round < 200; ++round) {
            TweenManager m;
            std::vector<float> vals(24, 0.0f);
            std::vector<std::shared_ptr<Tweener>> all;
            std::vector<std::shared_ptr<TweenSequence>> seqs;
            bool bad = false;
            auto op = [&](int self) {
                ++callbacks;
                const int pick = all.empty() ? 0 : next() % (int)all.size();
                Tweener& t = *all[(std::size_t)pick];
                switch (next() % 14) {
                    case 0: t.Kill(next() % 2 == 0); break;
                    case 1: t.Complete(); break;
                    case 2: t.Restart(next() % 2 == 0); break;
                    case 3: t.Rewind(); break;
                    case 4: t.Goto(unit() * 3.0f, next() % 2 == 0); break;
                    case 5: t.Pause(); break;
                    case 6: t.Play(); break;
                    case 7: t.PlayBackwards(); break;
                    case 8: if (!seqs.empty()) seqs[(std::size_t)(next() % (int)seqs.size())]->Append(all[(std::size_t)pick]); break;
                    case 9: if (!seqs.empty()) seqs[(std::size_t)(next() % (int)seqs.size())]->Join(all[(std::size_t)pick]); break;
                    case 10: if (!seqs.empty()) seqs[(std::size_t)(next() % (int)seqs.size())]->Insert(unit(), all[(std::size_t)pick]); break;
                    case 11: m.KillAll(next() % 3 == 0); break;
                    case 12: all[(std::size_t)self]->Kill(); break;
                    default: break;
                }
            };
            for (int i = 0; i < 12; ++i) {
                const int slot = i;
                auto t = m.To([&vals, slot] { return vals[(std::size_t)slot]; }, [&vals, slot](float x) { vals[(std::size_t)slot] = x; }, 1.0f, 0.1f + unit());
                t->SetEase(next() % 2 ? Ease::Linear : Ease::QuadInOut).SetDelay(next() % 3 == 0 ? unit() * 0.5f : 0.0f);
                const int l = next() % 4;
                t->SetLoops(l == 3 ? -1 : l + 1, next() % 2 ? LoopType::Restart : LoopType::Yoyo);
                t->SetAutoKill(next() % 3 != 0);
                t->OnUpdate([&, slot] { if (next() % 7 == 0) op(slot); });
                t->OnStepComplete([&, slot] { if (next() % 3 == 0) op(slot); });
                t->OnComplete([&, slot] { if (next() % 2 == 0) op(slot); });
                t->OnStart([&, slot] { if (next() % 4 == 0) op(slot); });
                all.push_back(t);
            }
            for (int i = 0; i < 3; ++i) {
                auto q = m.Sequence();
                q->SetLoops(next() % 2 ? 1 : 2, LoopType::Restart);
                q->AppendInterval(unit() * 0.3f);
                q->AppendCallback([&] { if (next() % 3 == 0) op(0); });
                seqs.push_back(q);
                all.push_back(q);
            }
            for (int step = 0; step < 80; ++step) {
                const float dt = next() % 10 == 0 ? 0.0f : next() % 20 == 0 ? 7.0f : unit() * 0.2f;
                m.Update(dt, dt);
                for (float v : vals) if (!(v >= -1e-3f && v <= 1.001f)) bad = true;   // also catches NaN
                if (bad) break;
            }
            CHECK(!bad);
            m.KillAll();
            m.Update(0.1f, 0.1f);
            CHECK(m.Count() == 0);
        }
        CHECK(callbacks > 500);
    }

    BLOCK(9) {   // A child callback that adds to its own sequence while the loop restarts: the list moves under the restart.
        TweenManager m;
        float a = 0, extra = 0;
        auto seq = m.Sequence();
        auto child = linear(m, a, 1.0f, 1.0f);
        seq->SetLoops(2, LoopType::Restart);
        seq->Append(child);
        bool grew = false;
        child->OnUpdate([&] {
            if (grew || child->Elapsed() > 0.0f) return;     // only on the restart render at time 0
            grew = true;
            for (int i = 0; i < 16; ++i) seq->Append(linear(m, extra, 1.0f, 0.1f));
        });
        m.Update(1.2f, 1.2f);                           // into the second loop: DoLoopRestart runs
        m.Update(5.0f, 5.0f);
        CHECK(grew);
        CHECK(seq->Count() == 17);
        CHECK(!seq->IsActive() || seq->IsComplete());
    }
    BLOCK(10) {  // A sequence callback that appends more callbacks (the list moves while it runs).
        TweenManager m;
        auto seq = m.Sequence();
        int first = 0, later = 0;
        auto marker = std::make_shared<int>(41);
        seq->AppendInterval(0.5f);
        seq->AppendCallback([&, marker] {
            for (int i = 0; i < 32; ++i) seq->AppendCallback([&] { ++later; });
            ++*marker;                                   // the running closure is still intact
            ++first;
        });
        seq->AppendInterval(0.5f);
        m.Update(0.6f, 0.6f);
        CHECK(first == 1 && *marker == 42);
        m.Update(5.0f, 5.0f);
        CHECK(first == 1 && m.Count() == 0);
    }
    BLOCK(11) {  // A callback that completes its own sequence: each callback fires once, in order.
        TweenManager m;
        auto seq = m.Sequence();
        std::string log;
        int completes = 0;
        seq->AppendInterval(0.5f);
        seq->AppendCallback([&] { log += "A"; if (log == "A") seq->Complete(); });
        seq->AppendInterval(0.5f);
        seq->AppendCallback([&] { log += "B"; });
        seq->OnComplete([&] { ++completes; });
        m.Update(0.6f, 0.6f);
        CHECK(log == "AB");
        CHECK(seq->IsComplete());
        m.Update(2.0f, 2.0f);
        CHECK(completes == 1);                        // not fired a second time by the next frame
        CHECK(m.Count() == 0);
    }

    BLOCK(12) {  // Clearing the whole manager from inside a tween callback (a scene unload) ends the update cleanly.
        TweenManager m;
        float a = 0, b = 0;
        int after = 0;
        auto ta = linear(m, a, 1.0f, 1.0f);
        auto tb = linear(m, b, 1.0f, 1.0f);
        ta->OnUpdate([&] { m.Clear(); });
        ta->OnComplete([&] { ++after; });
        tb->OnUpdate([&] { ++after; });
        m.Update(0.5f, 0.5f);
        CHECK(after == 0);                            // nothing fires for a tween that was just cleared
        CHECK(!ta->IsActive() && !tb->IsActive() && m.Count() == 0);
        float c = 0;
        linear(m, c, 1.0f, 1.0f);
        m.Update(1.0f, 1.0f);
        CHECK_NEAR(c, 1.0f, 1e-5f);                   // and the manager is reusable
    }
    BLOCK(13) {  // Endless tweens sent to absurd times stay finite and keep working.
        TweenManager m;
        float v = 0;
        auto t = linear(m, v, 1.0f, 1.0f);
        t->SetLoops(-1, LoopType::Yoyo).SetAutoKill(false);
        for (float to : {1.0e30f, inf, 3.0e38f, 5.0e8f}) {
            t->Goto(to);
            CHECK(std::isfinite(v) && v >= 0.0f && v <= 1.0f && std::isfinite(t->Elapsed()));
        }
        t->Restart();
        m.Update(0.25f, 0.25f);
        CHECK_NEAR(v, 0.25f, 1e-5f);
        float w = 0;
        auto u = linear(m, w, 1.0f, 0.5f);
        u->SetLoops(std::numeric_limits<int>::max()).SetAutoKill(false);
        m.Update(1.0e30f, 1.0e30f);
        CHECK(std::isfinite(w) && std::isfinite(u->FullDuration()));
        u->Kill();
        t->Kill();
    }

    BLOCK(14) {  // A negative, NaN or infinite frame time moves nothing (it must not rewind the tweens).
        TweenManager m;
        float v = 0, u = 0;
        linear(m, v, 1.0f, 1.0f);
        linear(m, u, 1.0f, 1.0f)->SetUpdate(true);
        m.Update(0.5f, 0.5f);
        m.Update(-0.25f, -0.25f);
        m.Update(nan, nan);
        m.Update(inf, inf);
        CHECK_NEAR(v, 0.5f, 1e-5f);
        CHECK_NEAR(u, 0.5f, 1e-5f);
        m.Update(0.25f, 0.25f);
        CHECK_NEAR(v, 0.75f, 1e-5f);
    }

    TEST_MAIN_RESULT();
}
