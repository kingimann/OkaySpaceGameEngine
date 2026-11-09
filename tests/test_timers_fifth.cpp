// Fifth pass on timers and tweens (1.2): a callback that updates the scheduler that runs it, a pause menu
// and tweens that follow a clock channel, the TweenAnimation fields for the new tween tools, and a second
// hard look at scale (many owners, many tweens), nested sequences and scene clears. Each block was written
// to fail first. Block N runs alone with `test_timers_fifth N`.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Clock.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <random>
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

// A random nested sequence for the loop test: the same seed always gives the same structure.
struct Rig {
    TweenManager m;
    std::vector<float> vals;         // one per leaf
    std::shared_ptr<TweenSequence> root;
    int completes = 0;
};

std::shared_ptr<TweenSequence> Build(Rig& r, std::mt19937& g, int depth) {
    auto seq = r.m.Sequence();
    std::uniform_int_distribution<int> n(1, 3), kind(0, 4), loopsD(1, 3), lt(0, 2);
    const int count = n(g);
    for (int i = 0; i < count; ++i) {
        const int k = kind(g);
        if (k <= 1 || depth >= 3) {
            const int idx = (int)r.vals.size();
            r.vals.push_back(0);
            auto t = r.m.To([&r, idx] { return r.vals[(std::size_t)idx]; }, [&r, idx](float x) { r.vals[(std::size_t)idx] = x; },
                            1.0f + (float)idx, 0.25f + 0.25f * (float)(g() % 4));
            t->SetEase(Ease::Linear);
            if (g() % 3 == 0) t->SetLoops(2, (LoopType)lt(g));
            if (k == 0) seq->Append(t); else seq->Join(t);
        } else if (k == 2) {
            seq->AppendInterval(0.1f * (float)(g() % 5));
        } else if (k == 3) {
            seq->AppendCallback([] {});
        } else {
            auto inner = Build(r, g, depth + 1);
            inner->SetLoops(loopsD(g), (LoopType)lt(g));
            seq->Append(inner);
        }
    }
    return seq;
}
}

int main(int argc, char** argv) {
    if (argc > 1) g_only = std::atoi(argv[1]);
    RUN_SUITE("timers_fifth");

    // ------------------------------------------------ a callback that updates its own scheduler ----
    BLOCK(1) {   // the running task is not run again by the nested Update: it used to lose its period (and a repeat) without running
        Scheduler s;
        int fired = 0;
        s.InvokeRepeating(1.0f, [&] { ++fired; s.Update(1.0f); }, 3);
        s.Update(1.0f);
        CHECK(fired == 1);
        s.Update(1.0f);
        CHECK(fired == 2);
        s.Update(1.0f);
        CHECK(fired == 3 && s.ActiveCount() == 0);   // all three repeats ran, none was eaten by the nested update
        s.Update(1.0f);
        CHECK(fired == 3);

        // A tween task: the nested update does not step it a second time and does not skip its callbacks.
        Scheduler t;
        std::vector<float> seen;
        int done = 0;
        t.Tween(1.0f, [&](float p) { seen.push_back(p); t.Update(0.5f); }, [&] { ++done; });
        t.Update(0.25f);
        t.Update(0.25f);
        t.Update(0.5f);
        CHECK(seen.size() == 3 && done == 1);
        if (seen.size() == 3) { CHECK_NEAR(seen[0], 0.25f, 1e-6); CHECK_NEAR(seen[1], 0.5f, 1e-6); CHECK_NEAR(seen[2], 1.0f, 1e-6); }
        CHECK(t.ActiveCount() == 0);

        // The other tasks are advanced by the nested update, as by any update, and run once for each.
        Scheduler u;
        int a = 0, b = 0;
        u.Invoke(1.0f, [&] { ++a; u.Update(1.0f); });
        u.InvokeRepeating(1.0f, [&] { ++b; });
        u.Update(1.0f);
        CHECK(a == 1 && b == 2);   // one second from the nested update, one from the outer one
        CHECK(u.ActiveCount() == 1);

        // A one-shot with a backlog: it runs once, however it is nested.
        Scheduler w;
        int once = 0;
        w.Invoke(1.0f, [&] { ++once; w.Update(5.0f); });
        w.Update(5.0f);
        CHECK(once == 1 && w.ActiveCount() == 0);

        // The alive predicate may update too (it runs before the task does).
        Scheduler p;
        int ran = 0, preds = 0;
        Scheduler::Options o;
        o.alive = [&] { ++preds; p.Update(0.0f); return true; };
        p.InvokeRepeating(1.0f, [&] { ++ran; }, 2, o);
        p.Update(1.0f);
        p.Update(1.0f);
        CHECK(ran == 2 && p.ActiveCount() == 0);

        // A callback that schedules enough to move the list and then updates: the running task keeps its place.
        Scheduler m;
        int hits = 0, spawned = 0;
        m.InvokeRepeating(1.0f, [&] {
            ++hits;
            for (int i = 0; i < 200; ++i) m.Invoke(0.5f, [&] { ++spawned; });
            m.Update(0.5f);
        }, 2);
        m.Update(1.0f);
        CHECK(hits == 1 && spawned == 200 && m.ActiveCount() == 1);
        m.Update(1.0f);
        CHECK(hits == 2 && spawned == 400 && m.ActiveCount() == 0);

        // A throwing callback that also updated: the task is dropped and the error comes out once.
        Scheduler x;
        int thrown = 0;
        x.Invoke(1.0f, [&] { x.Update(0.0f); throw 7; });
        try { x.Update(1.0f); } catch (int) { ++thrown; }
        CHECK(thrown == 1 && x.ActiveCount() == 0);
    }

    // ------------------------------------------------ a pause menu and channel tweens ----
    BLOCK(2) {   // PauseAll(true) keeps the tweens that do not follow the game's time, channel tweens included
        ClockSet cs;
        cs.Get("fx");                                          // a channel of the game's own: real time
        cs.Get("held").SetPaused(true);                        // a real-time channel that is paused
        TweenManager m;
        m.SetClockSet(&cs);
        float scene = 0, uni = 0, ui = 0, fx = 0, game = 0, cin = 0, held = 0;
        auto tScene = Lin(m, scene, 100.0f, 100.0f);
        auto tUni = Lin(m, uni, 100.0f, 100.0f); tUni->SetUpdate(true);
        auto tUi = Lin(m, ui, 100.0f, 100.0f); tUi->SetClock("ui");
        auto tFx = Lin(m, fx, 100.0f, 100.0f); tFx->SetClock("fx");
        auto tGame = Lin(m, game, 100.0f, 100.0f); tGame->SetClock("game");
        auto tCin = Lin(m, cin, 100.0f, 100.0f); tCin->SetClock("cinematic");
        auto tHeld = Lin(m, held, 100.0f, 100.0f); tHeld->SetClock("held");
        auto frame = [&](float dt) { cs.Tick(dt, dt, false); m.Update(dt, dt); };
        frame(1.0f);
        CHECK_NEAR(scene, 1.0f, 1e-4); CHECK_NEAR(ui, 1.0f, 1e-4); CHECK_NEAR(game, 1.0f, 1e-4); CHECK_NEAR(held, 0.0f, 1e-4);
        m.PauseAll(true);
        frame(1.0f);
        CHECK_NEAR(scene, 1.0f, 1e-4);                         // scene time: paused
        CHECK_NEAR(game, 1.0f, 1e-4);                          // the game channel is the game's time: paused
        CHECK_NEAR(cin, 1.0f, 1e-4);                           // holds while the game is paused: paused
        CHECK_NEAR(held, 0.0f, 1e-4);
        CHECK_NEAR(uni, 2.0f, 1e-4);                           // ignores the time scale: keeps playing
        CHECK_NEAR(ui, 2.0f, 1e-4);                            // a real-time channel that is running: keeps playing
        CHECK_NEAR(fx, 2.0f, 1e-4);                            // so does one of the game's own
        CHECK(tUi->IsPlaying() && tFx->IsPlaying() && !tGame->IsPlaying() && !tHeld->IsPlaying());
        CHECK(m.PlayingCount() == 3);                          // uni, ui, fx
        cs.Get("held").SetPaused(false);
        frame(1.0f);
        CHECK_NEAR(held, 0.0f, 1e-4);                          // PauseAll paused it: the channel going again does not resume it
        m.PlayAll();
        frame(1.0f);
        CHECK_NEAR(scene, 2.0f, 1e-4); CHECK_NEAR(game, 2.0f, 1e-4); CHECK_NEAR(held, 1.0f, 1e-4);
        CHECK_NEAR(ui, 4.0f, 1e-4);
        // PauseAll() (the default) stops everything, channel tweens too.
        m.PauseAll();
        frame(1.0f);
        CHECK_NEAR(ui, 4.0f, 1e-4); CHECK_NEAR(uni, 4.0f, 1e-4); CHECK_NEAR(scene, 2.0f, 1e-4);
        CHECK(m.PlayingCount() == 0);
        m.KillAll();

        // A channel nobody has made yet is real time (it is created on first use): it keeps playing too.
        ClockSet cs2;
        TweenManager m2;
        m2.SetClockSet(&cs2);
        float fresh = 0;
        auto tf = Lin(m2, fresh, 10.0f, 10.0f); tf->SetClock("later");
        m2.PauseAll(true);
        CHECK(tf->IsPlaying());
        cs2.Tick(1.0f, 1.0f, false); m2.Update(1.0f, 1.0f);    // this update makes the channel
        cs2.Tick(1.0f, 1.0f, false); m2.Update(1.0f, 1.0f);
        CHECK_NEAR(fresh, 1.0f, 1e-4);
        // A channel that wins over SetUpdate(true): a tween on "game" is paused although it ignores the time scale.
        float gu = 0;
        auto tg = Lin(m2, gu, 10.0f, 10.0f); tg->SetUpdate(true).SetClock("game");
        m2.PauseAll(true);
        CHECK(!tg->IsPlaying());
        m2.KillAll();
    }

    // ------------------------------------------------ TweenAnimation: decay, speed based, tag ----
    BLOCK(3) {   // the three new fields: defaults, round trip (with and without the older tail), older files, forward tail, play
        auto lineOf = [](const std::string& text) {
            const std::size_t at = text.find("tweenanim ");
            if (at == std::string::npos) return std::string();
            return text.substr(at, text.find('\n', at) - at);
        };
        auto quotes = [](const std::string& line) { int n = 0; for (char c : line) n += c == '"'; return n; };

        Scene s("S");
        GameObject* go = s.CreateGameObject("Hero");
        auto* d = go->AddComponent<TweenAnimation>();
        CHECK(!d->speedBased && d->decay == 0.0f && d->tag.empty());
        const std::string plainText = SceneSerializer::SerializeObject(*go);
        const std::string plainLine = lineOf(plainText);
        CHECK(!plainLine.empty() && quotes(plainLine) == 2);      // only the id: the line is what older builds wrote, byte for byte

        // Every combination of the older tail (clock, curve) and the new one loads back as it was saved.
        const char* clocks[] = {"", "ui"};
        const char* curves[] = {"", "steps(4)"};
        int combos = 0;
        for (const char* clock : clocks) for (const char* curve : curves) for (int variant = 0; variant < 4; ++variant) {
            Scene a("A");
            GameObject* g = a.CreateGameObject("G");
            auto* ta = g->AddComponent<TweenAnimation>();
            ta->id = "bob"; ta->clock = clock; ta->easeCurve = curve; ta->duration = 2.5f;
            ta->decay = variant == 1 || variant == 3 ? 6.25f : 0.0f;
            ta->speedBased = variant == 2 || variant == 3;
            ta->tag = variant != 0 ? "menu group" : "";
            const std::string text = SceneSerializer::SerializeObject(*g);
            Scene b("B");
            GameObject* back = SceneSerializer::InstantiateFromText(b, text);
            auto* tb = back ? back->GetComponent<TweenAnimation>() : nullptr;
            CHECK(tb != nullptr);
            if (!tb) continue;
            ++combos;
            CHECK(tb->clock == ta->clock && tb->easeCurve == ta->easeCurve && tb->id == "bob" && tb->duration == 2.5f);
            CHECK(tb->decay == ta->decay && tb->speedBased == ta->speedBased && tb->tag == ta->tag);
            const std::string again = SceneSerializer::SerializeObject(*back);
            CHECK(lineOf(again) == lineOf(text));                // it saves the same text again
            if (variant == 0 && !*clock && !*curve) CHECK(quotes(lineOf(text)) == 2);   // nothing set: no tail at all
        }
        CHECK(combos == 16);

        // A file from before the new tail (clock and curve only) and one from before both: the new fields keep their defaults.
        {
            std::string old = plainText;
            const std::size_t at = old.find(plainLine);
            old.insert(at + plainLine.size(), " \"cinematic\" \"cubic-bezier(0.4,0,0.2,1)\"");
            Scene b("B");
            GameObject* back = SceneSerializer::InstantiateFromText(b, old);
            auto* tb = back ? back->GetComponent<TweenAnimation>() : nullptr;
            CHECK(tb && tb->clock == "cinematic" && tb->easeCurve == "cubic-bezier(0.4,0,0.2,1)");
            CHECK(tb && tb->decay == 0.0f && !tb->speedBased && tb->tag.empty());
            Scene c("C");
            GameObject* plain = SceneSerializer::InstantiateFromText(c, plainText);
            auto* tp = plain ? plain->GetComponent<TweenAnimation>() : nullptr;
            CHECK(tp && tp->clock.empty() && tp->easeCurve.empty() && tp->decay == 0.0f && !tp->speedBased && tp->tag.empty());
        }

        // A newer build's fields after the tag are stepped over, and the record after this one still loads.
        {
            Scene a("A");
            GameObject* g = a.CreateGameObject("G");
            auto* t1 = g->AddComponent<TweenAnimation>(); t1->id = "one"; t1->tag = "t"; t1->decay = 3.0f;
            auto* t2 = g->AddComponent<TweenAnimation>(); t2->id = "two"; t2->speedBased = true;
            std::string text = SceneSerializer::SerializeObject(*g);
            const std::size_t first = text.find("tweenanim ");
            const std::size_t eol = text.find('\n', first);
            text.insert(eol, " 99 \"from the future\" 7");
            Scene b("B");
            GameObject* back = SceneSerializer::InstantiateFromText(b, text);
            CHECK(back != nullptr);
            auto all = back ? back->GetComponents<TweenAnimation>() : std::vector<TweenAnimation*>{};
            CHECK(all.size() == 2);
            if (all.size() == 2) {
                CHECK(all[0]->id == "one" && all[0]->tag == "t" && all[0]->decay == 3.0f);
                CHECK(all[1]->id == "two" && all[1]->speedBased);
            }
        }

        // Odd values: a tag with quotes and a backslash, a decay that is not a number or is negative.
        {
            Scene a("A");
            GameObject* g = a.CreateGameObject("G");
            auto* ta = g->AddComponent<TweenAnimation>();
            ta->tag = "a \"b\" \\ c"; ta->decay = kNan; ta->speedBased = true;
            Scene b("B");
            GameObject* back = SceneSerializer::InstantiateFromText(b, SceneSerializer::SerializeObject(*g));
            auto* tb = back ? back->GetComponent<TweenAnimation>() : nullptr;
            CHECK(tb && tb->tag == ta->tag && tb->decay == 0.0f && tb->speedBased);
            ta->decay = -3.0f; ta->speedBased = false; ta->tag.clear();
            Scene c("C");
            back = SceneSerializer::InstantiateFromText(c, SceneSerializer::SerializeObject(*g));
            tb = back ? back->GetComponent<TweenAnimation>() : nullptr;
            CHECK(tb && tb->decay == 0.0f && !tb->speedBased && tb->tag.empty());
            CHECK(quotes(lineOf(SceneSerializer::SerializeObject(*g))) == 2);   // back to no tail
        }

        // The fields reach the tween.
        {
            Scene p("P");
            GameObject* mv = p.CreateGameObject("Mover");
            auto* ta = mv->AddComponent<TweenAnimation>();
            ta->type = (int)TweenAnimation::Type::LocalMove;
            ta->endValue = {0, 6, 0}; ta->duration = 3.0f; ta->ease = (int)Ease::Linear;
            ta->speedBased = true; ta->tag = "menu";
            p.Start();
            CHECK(ta->Current() && ta->Current()->speedBased && ta->Current()->tag == "menu");
            CHECK(p.tweens().CountByTag("menu") == 1);
            p.Update(1.0f);
            CHECK_NEAR(mv->transform->localPosition.y, 3.0f, 1e-3);   // 6 units at 3 a second: two seconds
            p.Update(1.0f);
            CHECK_NEAR(mv->transform->localPosition.y, 6.0f, 1e-3);
            CHECK(p.tweens().Count() == 0);

            Scene q("Q");
            GameObject* hit = q.CreateGameObject("Hit");
            GameObject* soft = q.CreateGameObject("Soft");
            auto setup = [&](GameObject* o, float decay) {
                auto* t = o->AddComponent<TweenAnimation>();
                t->type = (int)TweenAnimation::Type::PunchPosition;
                t->endValue = {0, 4, 0}; t->duration = 1.0f; t->vibrato = 6; t->decay = decay; t->tag = "punch";
                return t;
            };
            auto* straight = setup(hit, 0.0f);
            auto* snappy = setup(soft, 8.0f);
            q.Start();
            CHECK(snappy->Current() && snappy->Current()->decay == 8.0f && straight->Current()->decay == 0.0f);
            CHECK(q.tweens().KillByTag("punch") == 2);
            CHECK(!straight->Current()->IsActive());
        }
        {   // the same punch, late in its life: the snappy one is quiet, the straight fade still moves
            Scene p("P");
            GameObject* a = p.CreateGameObject("A");
            GameObject* b = p.CreateGameObject("B");
            for (GameObject* o : {a, b}) {
                auto* t = o->AddComponent<TweenAnimation>();
                t->type = (int)TweenAnimation::Type::PunchPosition;
                t->endValue = {0, 4, 0}; t->duration = 1.0f; t->vibrato = 6; t->decay = o == b ? 8.0f : 0.0f;
            }
            p.Start();
            float maxA = 0, maxB = 0;
            for (int i = 0; i < 20; ++i) {
                p.Update(0.05f);
                if (i >= 8 && i < 18) { maxA = std::max(maxA, std::fabs(a->transform->localPosition.y)); maxB = std::max(maxB, std::fabs(b->transform->localPosition.y)); }
            }
            CHECK(maxA > 0.1f && maxB < maxA * 0.25f);
        }
    }

    // ------------------------------------------------ sequences ----
    BLOCK(4) {   // ending a sequence ends what it holds: a handle to a child stops reporting active and cannot write again
        TweenManager m;
        float v = 0, w = 0, x = 0;
        auto a = Lin(m, v, 1.0f, 1.0f);
        auto b = Lin(m, w, 1.0f, 1.0f);
        auto c = Lin(m, x, 1.0f, 1.0f);
        auto inner = m.Sequence();
        inner->Append(c);
        auto seq = m.Sequence();
        seq->Append(a).Append(b).Append(inner);
        int childKills = 0, seqKills = 0;
        a->OnKill([&] { ++childKills; });
        seq->OnKill([&] { ++seqKills; });
        m.Update(0.1f, 0.1f);
        CHECK(a->IsActive() && b->IsActive() && c->IsActive() && inner->IsActive());
        seq->Kill();
        CHECK(!seq->IsActive() && !a->IsActive() && !b->IsActive() && !inner->IsActive() && !c->IsActive());
        CHECK(seqKills == 1 && childKills == 0);                       // the children end silently
        CHECK(m.Count() == 0);

        // Kill(true) plays to the end first, then ends the lot.
        float y = 0;
        auto d = Lin(m, y, 1.0f, 1.0f);
        auto s2 = m.Sequence();
        s2->Append(d);
        s2->Kill(true);
        CHECK_NEAR(y, 1.0f, 1e-5);
        CHECK(!d->IsActive() && !s2->IsActive());

        // A manager that is cleared: a held child can neither run nor write (it used to write into a scene that was gone).
        TweenManager m2;
        int sets = 0;
        auto e = m2.Virtual(0.0f, 1.0f, 1.0f, [&](float) { ++sets; });
        auto s3 = m2.Sequence();
        s3->Append(e);
        m2.Update(0.5f, 0.5f);
        const int before = sets;
        CHECK(before > 0);
        m2.Clear();
        CHECK(!e->IsActive() && !s3->IsActive());
        e->Restart(); e->Goto(0.5f); e->Complete(); e->PlayBackwards(); e->Rewind();
        s3->Restart(); s3->Goto(0.5f, true);
        CHECK(sets == before);

        // Its object dies: the sequence and what it holds are gone at the end of the frame.
        Scene s;
        GameObject* go = s.CreateGameObject("Owner");
        float z = 0;
        auto child = Lin(s.tweens(), z, 1.0f, 1.0f);
        auto seq4 = s.tweens().Sequence();
        seq4->SetTarget(go);
        seq4->Append(child);
        s.Destroy(go);
        s.Update(0.1f);
        CHECK(!seq4->IsActive() && !child->IsActive() && s.tweens().Count() == 0);

        // A callback that throws inside a sequence ends it and what it holds.
        TweenManager m3;
        float q = 0;
        auto f = Lin(m3, q, 1.0f, 1.0f);
        auto s5 = m3.Sequence();
        s5->Append(f).AppendCallback([] { throw 3; });
        bool threw = false;
        try { m3.Update(2.0f, 2.0f); } catch (int) { threw = true; }
        CHECK(threw && !s5->IsActive() && !f->IsActive() && m3.Count() == 0);
    }

    BLOCK(5) {   // nested sequences with loops of every kind: Goto(T) shows what playing to T shows, and each ends once
        int mismatches = 0, badEnd = 0, probes = 0;
        for (unsigned seed = 1; seed <= 200; ++seed) {
            for (int probe = 0; probe < 3; ++probe) {
                Rig a, b;
                std::mt19937 ga(seed), gb(seed);
                a.root = Build(a, ga, 0); b.root = Build(b, gb, 0);
                std::mt19937 gl(seed * 7 + 1);
                a.root->SetLoops(1 + (int)(gl() % 3), (LoopType)(gl() % 3));
                b.root->SetLoops(a.root->loops, a.root->loopType);
                a.root->SetAutoKill(false); b.root->SetAutoKill(false);
                const double full = a.root->FullDurationD();
                const double T = full * (0.2 + 0.3 * probe) + 0.0137;     // off the loop boundaries, where the two orders may differ by a frame
                double t = 0;
                while (t + 0.05 <= T) { a.m.Update(0.05f, 0.05f); t += 0.05; }
                a.m.Update((float)(T - t), (float)(T - t));
                b.root->Goto((float)T, true);
                ++probes;
                for (std::size_t i = 0; i < a.vals.size(); ++i)
                    if (!std::isfinite(a.vals[i]) || std::fabs(a.vals[i] - b.vals[i]) > 2e-2f) { ++mismatches; break; }
            }
            Rig c;
            std::mt19937 gc(seed);
            c.root = Build(c, gc, 0);
            std::mt19937 gl(seed * 7 + 1);
            c.root->SetLoops(1 + (int)(gl() % 3), (LoopType)(gl() % 3));
            c.root->OnComplete([&c] { ++c.completes; });
            for (int i = 0; i < 6000 && c.m.Count() > 0; ++i) c.m.Update(0.1f, 0.1f);
            if (c.completes != 1 || c.m.Count() != 0) ++badEnd;
        }
        CHECK(probes == 600);
        CHECK(mismatches == 0);
        CHECK(badEnd == 0);
    }

    // ------------------------------------------------ scripts with bad numbers ----
    BLOCK(6) {   // loop counts, vibrato and jumps far out of range, and NaN into the ease functions
        Scene s("sc");
        s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Box");
        go->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var e1 = 5; var e2 = 5; var e3 = 5; var e4 = 5; var e5 = 5; var e6 = 5; var e7 = 5;\n"
            "function start() {\n"
            "  var nan = sqrt(0 - 1);\n"
            "  var a = tween_move(4, 0, 1.0, \"linear\"); tween_set_id(a, \"nan\");        tween_loops(a, nan, \"yoyo\");\n"
            "  var b = tween_move(4, 0, 1.0, \"linear\"); tween_set_id(b, \"huge\");       tween_loops(b, 1000000000000);\n"
            "  var c = tween_move(4, 0, 1.0, \"linear\"); tween_set_id(c, \"neg\");        tween_loops(c, 0 - 1000000000000);\n"
            "  var d = tween_move(4, 0, 1.0, \"linear\"); tween_set_id(d, \"three\");      tween_loops(d, 3.9);\n"
            "  tween_punch_pos(1, 1, 1.0, 1000000000000);\n"
            "  tween_punch_scale(1, 1.0, nan);\n"
            "  tween_shake(1, 10000000, 1000000000000);\n"
            "  tween_shake_rotate(1, 5.0, nan);\n"
            "  tween_jump(1, 1, 1, 1.0, 1000000000000);\n"
            "  e1 = ease_in(nan); e2 = ease_out(nan); e3 = ease_in_out(nan); e4 = ease_back(nan); e5 = ease_elastic(nan);\n"
            "  e6 = ease_bounce(nan); e7 = ease_in_out_cubic(nan);\n"
            "}\n");
        auto* sc = go->GetComponent<ScriptComponent>();
        s.Start();
        auto loops = [&](const char* id) { auto v = s.tweens().WithId(id); return v.size() == 1 ? v[0]->loops : -999; };
        CHECK(loops("nan") == 1);                    // NaN plays once (it used to loop for ever on some machines)
        CHECK(loops("huge") == 2000000000);          // a huge count is a lot of loops, not "negative: for ever"
        CHECK(loops("neg") < 0);                     // a negative count still means for ever
        CHECK(loops("three") == 3);
        for (int i = 0; i < 40; ++i) {
            s.Update(0.25f);
            const Vec3& p = go->transform->localPosition; const Vec3& sc3 = go->transform->localScale;
            CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
            CHECK(std::isfinite(sc3.x) && std::isfinite(sc3.y) && std::isfinite(sc3.z));
            const float rot = go->transform->localRotation.w;
            CHECK(std::isfinite(rot));
        }
        for (const char* n : {"e1", "e2", "e3", "e4", "e5", "e6", "e7"}) {
            const float v = sc->VM()->GetGlobal(n).AsFloat();
            CHECK(std::isfinite(v) && v >= -0.5f && v <= 1.5f);
        }
    }

    // ------------------------------------------------ the debug view on a big scene ----
    BLOCK(7) {   // a summary does not build the group tables or sort the oldest; the numbers are the same
        Scene s("S");
        s.physicsEnabled = false;
        const int N = 3000;
        std::vector<GameObject*> objs;
        for (int i = 0; i < N; ++i) objs.push_back(s.CreateGameObject("Obj" + std::to_string(i % 40)));
        for (int i = 0; i < N; ++i) {
            auto t = DOTween::LocalMove(objs[(std::size_t)i], {1, 2, 3}, 50.0f);
            t->SetTag(i % 3 == 0 ? "a" : "b").SetId("id" + std::to_string(i % 20));
            Scheduler::Options o; o.object = objs[(std::size_t)i]; o.tag = i % 2 ? "x" : "y";
            s.scheduler().InvokeRepeating(1.0f + (float)(i % 5), [] {}, -1, o);
            s.scheduler().Invoke(100.0f, [] {}, o);
        }
        s.Update(0.1f);
        TimerDebug::Options none;
        none.showGroups = false; none.showOldest = false; none.topGroups = 0; none.topOldest = 0;
        const auto lean = TimerDebug::Capture(s.scheduler(), none);
        const auto full = TimerDebug::Capture(s.scheduler());
        CHECK(lean.total == (std::size_t)(2 * N) && lean.total == full.total);
        CHECK(lean.byTag.empty() && lean.byOwner.empty() && lean.oldest.empty());
        CHECK(full.byTag.size() == 2 && full.byOwner.size() == 40 && full.oldest.size() == 3);
        CHECK(lean.delays == full.delays && lean.repeating == full.repeating && lean.paused == full.paused);
        CHECK(lean.nextHandle == full.nextHandle && lean.nextDue == full.nextDue);
        const auto tl = TimerDebug::Capture(s.tweens(), none);
        const auto tf = TimerDebug::Capture(s.tweens());
        CHECK(tl.total == (std::size_t)N && tl.playing == tf.playing && tl.withTarget == tf.withTarget);
        CHECK(tl.byId.empty() && tl.byTag.empty() && tl.oldest.empty());
        CHECK(tf.byId.size() == 20 && tf.byTag.size() == 2 && tf.oldest.size() == 3);
        // The view text is what it was.
        CHECK(TimerDebug::Summary(&s.scheduler(), &s.tweens()) == "Timers: 6000 (next 0.9s) | Tweens: 3000 (3000 playing)");
        TimerDebug::Options noGroups;
        noGroups.showGroups = false;
        const auto lines = TimerDebug::BuildLines(&s.scheduler(), &s.tweens(), noGroups);
        for (const std::string& l : lines) CHECK(l.find("tags:") == std::string::npos && l.find("owners:") == std::string::npos && l.find("ids:") == std::string::npos);

        // The oldest are the ones that have run longest, longest first; ties keep the order they were made in.
        Scheduler sch;
        const auto h1 = sch.InvokeRepeating(50.0f, [] {});
        sch.Update(2.0f);
        const auto h2 = sch.InvokeRepeating(50.0f, [] {});
        const auto h3 = sch.InvokeRepeating(50.0f, [] {});
        sch.Update(1.0f);
        const auto h4 = sch.InvokeRepeating(50.0f, [] {});
        sch.Update(1.0f);
        TimerDebug::Options two; two.topOldest = 3;
        const auto st = TimerDebug::Capture(sch, two);
        CHECK(st.oldest.size() == 3);
        if (st.oldest.size() == 3) CHECK(st.oldest[0].handle == h1 && st.oldest[1].handle == h2 && st.oldest[2].handle == h3);
        CHECK(h4 != Scheduler::InvalidHandle);
    }

    // ------------------------------------------------ many owners destroyed at once ----
    BLOCK(8) {   // twenty thousand objects with timers and tweens go in one frame: nothing runs after, nothing is left, every task is looked at once
        Scene s("S");
        s.physicsEnabled = false;
        const int N = 20000;
        std::vector<GameObject*> objs;
        for (int i = 0; i < N; ++i) objs.push_back(s.CreateGameObject("O"));
        int ran = 0, alivePolls = 0;
        for (int i = 0; i < N; ++i) {
            Scheduler::Options o; o.object = objs[(std::size_t)i]; o.tag = "wave";
            o.alive = [&alivePolls] { ++alivePolls; return true; };
            s.scheduler().InvokeRepeating(1.0f, [&ran] { ++ran; }, -1, o);
            s.scheduler().Invoke(5.0f, [&ran] { ++ran; }, o);
            DOTween::LocalMove(objs[(std::size_t)i], {1, 0, 0}, 10.0f);
        }
        CHECK(s.scheduler().CountByTag("wave") == (std::size_t)(2 * N) && s.tweens().Count() == (std::size_t)N);
        alivePolls = 0;
        s.Update(0.5f);
        CHECK(alivePolls == 2 * N);                                    // each task asks once per update, not once per object or per task of its group
        for (GameObject* g : objs) s.Destroy(g);
        s.Update(0.6f);                                                // the timers run (the objects are alive until the end of this frame) ...
        CHECK(ran == N);
        CHECK(s.tweens().Count() == 0);                                // ... the tweens end with the objects at once
        CHECK(s.Objects().empty());
        alivePolls = 0;
        s.Update(0.6f);                                                // ... and the tasks go without running
        CHECK(ran == N && s.scheduler().ActiveCount() == 0 && s.scheduler().CountByTag("wave") == 0);
        CHECK(alivePolls == 0);                                        // an object that is gone is found without asking its predicate

        // The same with the scheduler cleaning up (not waiting for the next update).
        Scene s2("S2");
        s2.physicsEnabled = false;
        std::vector<GameObject*> o2;
        for (int i = 0; i < N; ++i) o2.push_back(s2.CreateGameObject("O"));
        for (int i = 0; i < N; ++i) { Scheduler::Options o; o.object = o2[(std::size_t)i]; s2.scheduler().InvokeRepeating(1.0f, [] {}, -1, o); }
        for (GameObject* g : o2) s2.Destroy(g);
        s2.Update(0.1f);
        CHECK(s2.scheduler().PurgeDeadOwners() == N);
        CHECK(s2.scheduler().ActiveCount() == 0);
    }

    // ------------------------------------------------ a tween callback that updates its manager ----
    BLOCK(10) {   // the tween whose callback is running is left alone by the nested update: no re-entered callbacks, no lost OnComplete
        TweenManager m;
        float v = 0;
        int calls = 0, depth = 0, maxDepth = 0, steps = 0, completes = 0;
        auto t = Lin(m, v, 1.0f, 1.0f);
        t->SetLoops(3, LoopType::Restart);
        t->OnUpdate([&] {
            ++calls; ++depth; maxDepth = std::max(maxDepth, depth);
            if (depth < 50) m.Update(0.0f, 0.0f);                   // (with no end to the nesting a zero step recursed until the stack was gone)
            --depth;
        });
        t->OnStepComplete([&] { ++steps; });
        t->OnComplete([&] { ++completes; });
        m.Update(0.5f, 0.5f);
        CHECK(calls == 1 && maxDepth == 1);
        CHECK_NEAR(v, 0.5f, 1e-5);
        m.Update(3.0f, 3.0f);
        CHECK(calls == 2 && steps == 3 && completes == 1);
        CHECK(m.Count() == 0 && !t->IsActive());

        // A nested update with real time from OnStepComplete: the loops are counted once and OnComplete fires once
        // (the nested update used to finish and kill the tween, and the outer render never got to OnComplete).
        TweenManager m2;
        float w = 0;
        int stepsB = 0, compB = 0;
        auto u = Lin(m2, w, 1.0f, 1.0f);
        u->SetLoops(4, LoopType::Restart);
        u->OnStepComplete([&] { ++stepsB; m2.Update(1.0f, 1.0f); });
        u->OnComplete([&] { ++compB; });
        m2.Update(1.0f, 1.0f);
        CHECK(stepsB == 1 && compB == 0 && u->IsActive());          // one second: one loop; the nested update left this tween alone
        m2.Update(3.0f, 3.0f);
        CHECK(stepsB == 4 && compB == 1 && !u->IsActive());

        // The other tweens are advanced by the nested update, as by any update.
        TweenManager m3;
        float a = 0, b = 0;
        auto ta = Lin(m3, a, 10.0f, 10.0f);
        auto tb = Lin(m3, b, 10.0f, 10.0f);
        ta->OnUpdate([&] { if (a < 1.5f) m3.Update(1.0f, 1.0f); });
        m3.Update(1.0f, 1.0f);
        CHECK_NEAR(a, 1.0f, 1e-4);
        CHECK_NEAR(b, 2.0f, 1e-4);                                   // its own second and the nested one
        ta->Kill(); tb->Kill();

        // A sequence whose child's callback updates the manager: the sequence is left alone too.
        TweenManager m4;
        float c = 0;
        int seqCompletes = 0, childCalls = 0;
        auto child = Lin(m4, c, 1.0f, 1.0f);
        child->OnUpdate([&] { ++childCalls; m4.Update(0.0f, 0.0f); });
        auto seq = m4.Sequence();
        seq->Append(child);
        seq->OnComplete([&] { ++seqCompletes; });
        m4.Update(2.0f, 2.0f);
        CHECK(childCalls == 1 && seqCompletes == 1 && m4.Count() == 0);
    }

    // ------------------------------------------------ Clear and load around timers ----
    BLOCK(9) {   // a Clear asked for in a callback happens at the end of the frame and takes what the frame made too
        Scene s("S");
        s.physicsEnabled = false;
        GameObject* a = s.CreateGameObject("A");
        float av = 0;
        auto old = DOTween::LocalMove(a, {5, 0, 0}, 10.0f);
        std::shared_ptr<Tweener> during, after;
        Scheduler::Handle lateTimer = 0;
        int lateRan = 0, killed = 0;
        old->OnKill([&] { ++killed; });
        s.scheduler().Invoke(0.1f, [&] {
            s.Clear();                                                  // objects are in use: queued
            CHECK(s.HasPendingClear());
            during = s.tweens().To([&] { return av; }, [&](float x) { av = x; }, 1.0f, 1.0f);   // made after the call: goes with the scene
            lateTimer = s.scheduler().Invoke(5.0f, [&] { ++lateRan; });
        });
        s.Update(0.1f);
        CHECK(!s.HasPendingClear() && s.Objects().empty());
        CHECK(!old->IsActive() && during && !during->IsActive() && killed == 0);   // ended silently
        CHECK(lateTimer != Scheduler::InvalidHandle && !s.scheduler().IsScheduled(lateTimer));
        CHECK(s.scheduler().ActiveCount() == 0 && s.tweens().Count() == 0);
        s.Update(0.5f);
        CHECK(lateRan == 0);
        // What is made afterwards works as it does in a fresh scene.
        float bv = 0;
        auto fresh = s.tweens().To([&] { return bv; }, [&](float x) { bv = x; }, 1.0f, 1.0f);
        fresh->SetEase(Ease::Linear);
        s.Update(0.5f);
        CHECK_NEAR(bv, 0.5f, 1e-4);
        // Held handles of the old world are safe to use.
        old->Play(); old->Restart(); old->Complete(); old->Kill(true); old->Goto(1.0f, true);
        CHECK(!old->IsActive());

        // A process-wide timer bound to an object of the cleared scene goes without running, and the global one without an object stays.
        Scheduler& g = Scheduler::Global();
        g.Clear();
        GameObject* b = s.CreateGameObject("B");
        int boundRan = 0, freeRan = 0;
        Scheduler::Options ob; ob.object = b;
        g.InvokeRepeating(0.1f, [&] { ++boundRan; }, -1, ob);
        g.InvokeRepeating(0.1f, [&] { ++freeRan; });
        s.Clear();
        g.Update(0.1f);
        CHECK(boundRan == 0 && freeRan == 1);
        CHECK(g.ActiveCount() == 1);
        g.Clear();

        // A scene load requested from a callback: the callback's own later timers go with the old scene, the new objects' tweens play.
        Scene w("W");
        w.physicsEnabled = false;
        Scene src("Src");
        GameObject* logo = src.CreateGameObject("Logo");
        auto* ta = logo->AddComponent<TweenAnimation>();
        ta->type = (int)TweenAnimation::Type::LocalMove; ta->endValue = {0, 8, 0}; ta->duration = 2.0f;
        ta->ease = (int)Ease::Linear; ta->id = "intro";
        const std::string text = SceneSerializer::Serialize(src);
        GameObject* oldObj = w.CreateGameObject("Old");
        auto oldTween = DOTween::LocalMove(oldObj, {1, 1, 1}, 5.0f);
        int stale = 0;
        w.scheduler().Invoke(0.1f, [&] {
            w.RequestLoadText(text);
            w.scheduler().Invoke(5.0f, [&] { ++stale; });               // the same frame: the load happens at its end, with the rest of the old scene
        });
        w.Update(0.1f);
        CHECK(!oldTween->IsActive());
        GameObject* logo2 = w.Find("Logo");
        CHECK(logo2 != nullptr && w.Find("Old") == nullptr);
        w.Update(0.5f);
        CHECK(stale == 0);
        if (logo2) {
            auto* tb = logo2->GetComponent<TweenAnimation>();
            CHECK(tb && tb->id == "intro");
            w.Update(1.0f);
            CHECK(tb && tb->Current() && tb->Current()->IsActive());
            CHECK(logo2->transform->localPosition.y > 0.5f && w.tweens().WithId("intro").size() == 1);
        }
    }

    TEST_MAIN_RESULT();
}
