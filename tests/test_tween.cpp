// Headless tests for the DOTween-style tween engine: timing, delay, loops (restart,
// yoyo, incremental), From / relative, control (pause, restart, complete, kill),
// sequences, the DO... shortcuts and kill-on-destroy.
#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("tween");

    {   // A float goes 0 -> 10 linearly, completes once, then is removed.
        TweenManager m;
        float v = 0.0f; int done = 0;
        m.To([&] { return v; }, [&](float x) { v = x; }, 10.0f, 1.0f)->SetEase(Ease::Linear).OnComplete([&] { ++done; });
        m.Update(0.25f, 0.25f); CHECK_NEAR(v, 2.5f, 1e-4f);
        m.Update(0.5f, 0.5f);   CHECK_NEAR(v, 7.5f, 1e-4f);
        m.Update(0.5f, 0.5f);   CHECK_NEAR(v, 10.0f, 1e-4f);
        CHECK(done == 1);
        CHECK(m.Count() == 0);
        m.Update(1.0f, 1.0f);   CHECK(done == 1);
    }
    {   // The start value is read when the delay ends, not when it's created.
        TweenManager m;
        float v = 0.0f;
        m.To([&] { return v; }, [&](float x) { v = x; }, 10.0f, 1.0f)->SetEase(Ease::Linear).SetDelay(0.5f);
        m.Update(0.25f, 0.25f); CHECK_NEAR(v, 0.0f, 1e-4f);
        v = 4.0f;                                  // changed during the delay
        m.Update(0.5f, 0.5f);   CHECK_NEAR(v, 4.0f + 6.0f * 0.25f, 1e-3f);
    }
    {   // Yoyo: there and back; step-complete fires per loop.
        TweenManager m;
        float v = 0.0f; int steps = 0, done = 0;
        m.To([&] { return v; }, [&](float x) { v = x; }, 1.0f, 1.0f)->SetEase(Ease::Linear)
            .SetLoops(2, LoopType::Yoyo).OnStepComplete([&] { ++steps; }).OnComplete([&] { ++done; });
        m.Update(1.5f, 1.5f); CHECK_NEAR(v, 0.5f, 1e-4f);
        CHECK(steps == 1);
        m.Update(0.6f, 0.6f); CHECK_NEAR(v, 0.0f, 1e-4f);
        CHECK(steps == 2); CHECK(done == 1);
    }
    {   // Incremental: each loop keeps going.
        TweenManager m;
        float v = 1.0f;
        m.To([&] { return v; }, [&](float x) { v = x; }, 3.0f, 1.0f)->SetEase(Ease::Linear).SetLoops(3, LoopType::Incremental);
        m.Update(3.5f, 3.5f);
        CHECK_NEAR(v, 1.0f + 2.0f * 3.0f, 1e-4f);
    }
    {   // Endless loops never complete; Kill stops them.
        TweenManager m;
        float v = 0.0f; int done = 0, killed = 0;
        auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 1.0f, 0.5f);
        t->SetLoops(-1).OnComplete([&] { ++done; }).OnKill([&] { ++killed; });
        for (int i = 0; i < 20; ++i) m.Update(0.13f, 0.13f);
        CHECK(done == 0); CHECK(m.Count() == 1);
        t->Kill();
        m.Update(0.1f, 0.1f);
        CHECK(killed == 1); CHECK(m.Count() == 0);
    }
    {   // Pause / play / restart / complete / rewind.
        TweenManager m;
        float v = 0.0f;
        auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 10.0f, 1.0f);
        t->SetEase(Ease::Linear).SetAutoKill(false);
        m.Update(0.5f, 0.5f); CHECK_NEAR(v, 5.0f, 1e-4f);
        t->Pause(); m.Update(0.3f, 0.3f); CHECK_NEAR(v, 5.0f, 1e-4f);
        CHECK(!t->IsPlaying());
        t->Play(); m.Update(0.1f, 0.1f); CHECK_NEAR(v, 6.0f, 1e-4f);
        t->Complete(); CHECK_NEAR(v, 10.0f, 1e-4f); CHECK(t->IsComplete());
        m.Update(0.1f, 0.1f); CHECK(m.Count() == 1);   // autoKill off: kept
        t->Restart(); CHECK_NEAR(v, 0.0f, 1e-4f);
        m.Update(0.2f, 0.2f); CHECK_NEAR(v, 2.0f, 1e-4f);
        t->Rewind(); CHECK_NEAR(v, 0.0f, 1e-4f); CHECK(!t->IsPlaying());
        t->Goto(0.75f); CHECK_NEAR(v, 7.5f, 1e-4f);
        t->PlayBackwards(); m.Update(0.25f, 0.25f); CHECK_NEAR(v, 5.0f, 1e-4f);
        m.Update(2.0f, 2.0f); CHECK_NEAR(v, 0.0f, 1e-4f);
    }
    {   // Unscaled tweens use the unscaled time; speed scales it.
        TweenManager m;
        float a = 0, b = 0;
        m.To([&] { return a; }, [&](float x) { a = x; }, 1.0f, 1.0f)->SetEase(Ease::Linear).SetUpdate(true);
        m.To([&] { return b; }, [&](float x) { b = x; }, 1.0f, 1.0f)->SetEase(Ease::Linear).SetSpeed(2.0f);
        m.Update(0.0f, 0.5f);   // time scale 0 (paused game)
        CHECK_NEAR(a, 0.5f, 1e-4f); CHECK_NEAR(b, 0.0f, 1e-4f);
        m.Update(0.25f, 0.25f);
        CHECK_NEAR(b, 0.5f, 1e-4f);
    }
    {   // Sequence: appended tweens run one after another (the second starts where
        // the first ended), joined ones together, callbacks in place.
        Scene s;
        GameObject* go = s.CreateGameObject("Box");
        auto seq = s.tweens().Sequence();
        int calls = 0, done = 0;
        seq->Append(DOTween::LocalMove(go, {1, 0, 0}, 1.0f)->SetEase(Ease::Linear).shared_from_this());
        seq->Join(DOTween::Scale(go, 2.0f, 1.0f)->SetEase(Ease::Linear).shared_from_this());
        seq->AppendInterval(0.5f);
        seq->AppendCallback([&] { ++calls; });
        seq->Append(DOTween::LocalMove(go, {3, 0, 0}, 1.0f)->SetEase(Ease::Linear).shared_from_this());
        seq->OnComplete([&] { ++done; });
        CHECK_NEAR(seq->Duration(), 2.5f, 1e-4f);
        CHECK(s.tweens().Count() == 1);   // the children belong to the sequence
        s.Update(0.5f);
        CHECK_NEAR(go->transform->localPosition.x, 0.5f, 1e-3f);
        CHECK_NEAR(go->transform->localScale.x, 1.5f, 1e-3f);
        s.Update(1.0f);   // 1.5: in the interval
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-3f);
        CHECK(calls == 1);
        s.Update(0.5f);   // 2.0: half way through the second move (from 1 to 3)
        CHECK_NEAR(go->transform->localPosition.x, 2.0f, 1e-3f);
        s.Update(1.0f);
        CHECK_NEAR(go->transform->localPosition.x, 3.0f, 1e-3f);
        CHECK(done == 1); CHECK(calls == 1);
        CHECK(s.tweens().Count() == 0);
    }
    {   // A looping sequence rewinds its children each loop.
        Scene s;
        GameObject* go = s.CreateGameObject("Box");
        auto seq = s.tweens().Sequence();
        int calls = 0;
        seq->Append(DOTween::LocalMove(go, {2, 0, 0}, 1.0f)->SetEase(Ease::Linear).shared_from_this());
        seq->AppendCallback([&] { ++calls; });
        seq->SetLoops(3);
        s.Update(1.5f);
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-3f);
        CHECK(calls == 1);
        s.Update(2.0f);
        CHECK(calls == 3);
        CHECK_NEAR(go->transform->localPosition.x, 2.0f, 1e-3f);
    }
    {   // From() and relative moves.
        Scene s;
        GameObject* go = s.CreateGameObject("Box");
        go->transform->localPosition = {5, 0, 0};
        DOTween::LocalMove(go, {0, 0, 0}, 1.0f)->SetEase(Ease::Linear).From();
        s.Update(0.001f);
        CHECK_NEAR(go->transform->localPosition.x, 0.005f, 1e-3f);
        s.Update(1.0f);
        CHECK_NEAR(go->transform->localPosition.x, 5.0f, 1e-3f);
        DOTween::LocalMove(go, {0, 2, 0}, 1.0f)->SetRelative();
        s.Update(1.1f);
        CHECK_NEAR(go->transform->localPosition.y, 2.0f, 1e-3f);
        CHECK_NEAR(go->transform->localPosition.x, 5.0f, 1e-3f);
    }
    {   // Relative rotation makes full turns; absolute rotation lands on the angle.
        Scene s;
        GameObject* go = s.CreateGameObject("Spinner");
        DOTween::Rotate(go, {0, 0, 360}, 1.0f)->SetRelative().SetEase(Ease::Linear);
        s.Update(0.25f);
        CHECK_NEAR(go->transform->localRotation.ToEuler().z, 90.0f, 0.5f);
        s.Update(1.0f);
        DOTween::Rotate(go, {0, 0, 45}, 0.5f);
        s.Update(1.0f);
        CHECK_NEAR(go->transform->localRotation.ToEuler().z, 45.0f, 0.5f);
    }
    {   // Punch and shake settle back where they started; jump and path land on the end.
        Scene s;
        GameObject* go = s.CreateGameObject("Fx");
        go->transform->localPosition = {1, 1, 0};
        DOTween::PunchScale(go, {0.3f, 0.3f, 0.3f}, 0.4f);
        DOTween::ShakePosition(go, 0.4f, {1, 1, 0});
        s.Update(0.2f);
        CHECK(std::fabs(go->transform->localPosition.x - 1.0f) > 1e-4f || std::fabs(go->transform->localPosition.y - 1.0f) > 1e-4f);
        s.Update(0.3f);
        CHECK_NEAR(go->transform->localScale.x, 1.0f, 1e-3f);
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-3f);
        DOTween::Jump(go, {4, 1, 0}, 2.0f, 2, 1.0f)->SetEase(Ease::Linear);
        s.Update(0.25f);   // top of the first hop
        CHECK_NEAR(go->transform->localPosition.y, 3.0f, 1e-2f);
        s.Update(1.0f);
        CHECK_NEAR(go->transform->localPosition.x, 4.0f, 1e-3f);
        CHECK_NEAR(go->transform->localPosition.y, 1.0f, 1e-3f);
        DOTween::Path(go, {{4, 5, 0}, {0, 5, 0}}, 1.0f, DOTween::PathType::CatmullRom)->SetEase(Ease::Linear);
        s.Update(0.5f);
        CHECK_NEAR(go->transform->localPosition.y, 5.0f, 1e-3f);
        s.Update(0.6f);
        CHECK_NEAR(go->transform->localPosition.x, 0.0f, 1e-3f);
    }
    {   // Colour, fade, typewriter text and counters.
        Scene s;
        GameObject* go = s.CreateGameObject("Label");
        auto* tr = go->AddComponent<TextRenderer>();
        tr->color = Color::White;
        DOTween::Fade(go, 0.0f, 1.0f)->SetEase(Ease::Linear);
        DOTween::Text(go, "Hello", 1.0f);
        s.Update(0.6f);
        CHECK(tr->text == "Hel");
        CHECK_NEAR(tr->color.a, 0.4f, 1e-3f);
        s.Update(0.5f);
        CHECK(tr->text == "Hello");
        CHECK_NEAR(tr->color.a, 0.0f, 1e-4f);
        DOTween::Counter(go, 0, 100, 1.0f, "Score: ")->SetEase(Ease::Linear);
        s.Update(0.5f);
        CHECK(tr->text == "Score: 50");
    }
    {   // Destroying the target kills its tweens (no writes into a freed object).
        Scene s;
        GameObject* go = s.CreateGameObject("Doomed");
        go->AddComponent<SpriteRenderer>();
        int done = 0;
        DOTween::LocalMove(go, {5, 0, 0}, 1.0f)->OnComplete([&] { ++done; });
        DOTween::ColorTo(go, Color::Black, 1.0f);
        auto seq = s.tweens().Sequence();
        seq->Append(DOTween::Scale(go, 3.0f, 1.0f));
        CHECK(s.tweens().Count() == 3);
        CHECK(s.tweens().IsTweening(go));
        s.Update(0.1f);
        s.Destroy(go);
        s.FlushDestroyed();
        CHECK(s.tweens().Count() == 1);   // the sequence itself had no target
        s.Update(2.0f);                   // its child was dropped: nothing touches `go`
        CHECK(done == 0);
        CHECK(s.tweens().Count() == 0);
    }
    {   // Ids and targets.
        Scene s;
        GameObject* a = s.CreateGameObject("A");
        GameObject* b = s.CreateGameObject("B");
        DOTween::LocalMove(a, {1, 0, 0}, 1.0f)->SetId("ui");
        DOTween::LocalMove(b, {1, 0, 0}, 1.0f)->SetId("ui");
        DOTween::Scale(b, 2.0f, 1.0f);
        CHECK(s.tweens().Pause("ui") == 2);
        s.Update(0.5f);
        CHECK_NEAR(a->transform->localPosition.x, 0.0f, 1e-4f);
        CHECK(s.tweens().Play("ui") == 2);
        CHECK(DOTween::Kill(b) == 2);
        s.Update(0.1f);
        CHECK(s.tweens().Count() == 1);
        s.tweens().KillAll();
        s.Update(0.1f);
        CHECK(s.tweens().Count() == 0);
    }
    {   // Ease names parse forgivingly, and every ease starts at 0 and ends at 1.
        CHECK(Easing::FromName("out_quad") == Ease::QuadOut);
        CHECK(Easing::FromName("OutQuad") == Ease::QuadOut);
        CHECK(Easing::FromName("easeInOutSine") == Ease::SineInOut);
        CHECK(Easing::FromName("quad_in_out") == Ease::QuadInOut);
        CHECK(Easing::FromName("In Out Bounce") == Ease::BounceInOut);
        CHECK(Easing::FromName("nope", Ease::CubicOut) == Ease::CubicOut);
        for (int i = 0; i < (int)Ease::Count; ++i) {
            CHECK_NEAR(Easing::Evaluate((Ease)i, 0.0f), 0.0f, 1e-3f);
            CHECK_NEAR(Easing::Evaluate((Ease)i, 1.0f), 1.0f, 1e-3f);
        }
    }

    {   // Scripts: ids, settings that nest, sequences, kill, variables, typewriter, calls.
        Scene s("ts"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Box");
        go->AddComponent<TextRenderer>()->text = "";
        go->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "var speed = 0; var calls = 0; var spin = 0;\n"
            "function start() {\n"
            "  var seq = seq_new();\n"
            "  seq_append(seq, tween_move(2, 0, 1.0));\n"
            "  seq_callback(seq, \"hit\");\n"
            "  seq_append(seq, tween_move(2, 4, 1.0));\n"
            "  tween_var(\"speed\", 10, 1.0);\n"
            "  tween_text(\"Hello\", 1.0);\n"
            "  tween_call(0.5, \"hit\");\n"
            "  spin = tween_loops(tween_scale(2, 0.5), -1, \"yoyo\");\n"
            "}\n"
            "function hit() { calls = calls + 1; }\n"
            "function stop_spin() { tween_kill(spin); }\n");
        auto* sc = go->GetComponent<ScriptComponent>();
        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(0.1f);   // 0.5 s
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-3f);
        CHECK_NEAR(sc->VM()->GetGlobal("speed").AsFloat(), 5.0f, 1e-3f);
        CHECK(go->GetComponent<TextRenderer>()->text.size() >= 2);
        CHECK_NEAR(go->transform->localScale.x, 2.0f, 1e-3f);   // end of the first scale leg
        CHECK(sc->VM()->GetGlobal("calls").AsFloat() == 1.0f);           // the delayed call
        for (int i = 0; i < 10; ++i) s.Update(0.1f);  // 1.5 s
        CHECK(sc->VM()->GetGlobal("calls").AsFloat() == 2.0f);           // + the sequence callback
        CHECK_NEAR(go->transform->localPosition.y, 2.0f, 1e-3f);
        CHECK(go->GetComponent<TextRenderer>()->text == "Hello");
        CHECK_NEAR(go->transform->localScale.x, 2.0f, 1e-3f);   // yoyo: out again on its 4th leg
        sc->VM()->CallEvent("stop_spin");
        for (int i = 0; i < 10; ++i) s.Update(0.1f);
        CHECK_NEAR(go->transform->localPosition.y, 4.0f, 1e-3f);
        CHECK(!s.tweens().IsTweening(go));
    }
    {   // Tween Animation component: plays on start, saves, and starts by id.
        Scene s("ta");
        GameObject* go = s.CreateGameObject("Logo");
        auto* ta = go->AddComponent<TweenAnimation>();
        ta->type = (int)TweenAnimation::Type::LocalMove; ta->endValue = {0, 3, 0}; ta->duration = 1.0f;
        ta->ease = (int)Ease::Linear; ta->loops = 2; ta->loopType = (int)LoopType::Yoyo;
        auto* tb = go->AddComponent<TweenAnimation>();
        tb->type = (int)TweenAnimation::Type::Scale; tb->endValue = {2, 2, 2}; tb->duration = 0.5f;
        tb->autoPlay = false; tb->id = "grow"; tb->ease = (int)Ease::Linear;
        go->AddComponent<ScriptComponent>("okayscript")->LoadSource("function go() { tween_play_id(\"grow\"); }\n");
        s.Start();
        s.Update(0.5f);
        CHECK_NEAR(go->transform->localPosition.y, 1.5f, 1e-3f);
        CHECK_NEAR(go->transform->localScale.x, 1.0f, 1e-4f);   // not auto-played
        go->GetComponent<ScriptComponent>()->VM()->CallEvent("go");
        s.Update(1.0f);
        CHECK_NEAR(go->transform->localPosition.y, 1.5f, 1e-3f);   // on its way back
        CHECK_NEAR(go->transform->localScale.x, 2.0f, 1e-3f);

        const std::string text = SceneSerializer::SerializeObject(*go);
        Scene s2("tb");
        GameObject* back = SceneSerializer::InstantiateFromText(s2, text);
        CHECK(back != nullptr);
        auto list = back ? back->GetComponents<TweenAnimation>() : std::vector<TweenAnimation*>{};
        CHECK(list.size() == 2);
        if (list.size() == 2) {
            CHECK(list[0]->loops == 2 && list[0]->loopType == (int)LoopType::Yoyo);
            CHECK_NEAR(list[0]->endValue.y, 3.0f, 1e-4f);
            CHECK(!list[1]->autoPlay && list[1]->id == "grow");
            CHECK(list[1]->type == (int)TweenAnimation::Type::Scale);
        }
    }

    TEST_MAIN_RESULT();
}
