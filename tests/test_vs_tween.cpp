#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Math/Spring.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace okay;

// Visual-script nodes for eases, springs and tweens (1.2): Ease With Curve, Spring Step, Tween Move / Tween Move By
// Speed and Kill / Pause / Play Tweens With Tag. Each runs inside a real graph on a real scene.

namespace {

struct World {
    Scene scene{"VsTween"};
    GameObject* hero = nullptr;
    GameObject* other = nullptr;
    VisualScriptComponent* vs = nullptr;

    World() {
        scene.physicsEnabled = false;
        other = scene.CreateGameObject("Other");
        hero = scene.CreateGameObject("Hero");
        vs = hero->AddComponent<VisualScriptComponent>();
    }
    bool Load(const std::string& graph) {
        std::string err;
        const bool ok = vs->LoadFromText(graph, &err);
        if (!ok) std::printf("  graph error: %s\n", err.c_str());
        return ok;
    }
    // Load and run `body` (a chain of nodes after OnStart) once.
    bool Run(const std::string& body) {
        const bool ok = Load("node 1 OnStart\n" + body + "entry OnStart 1\n");
        scene.Start();
        scene.Update(0.0f);
        return ok;
    }
    // Load `body` under OnUpdate; the caller steps the scene.
    bool Frames(const std::string& body) {
        const bool ok = Load("node 1 OnUpdate\n" + body + "entry OnUpdate 1\n");
        scene.Start();
        return ok;
    }
    float Var(const char* name) const { return vs->GetVariable(name).AsFloat(); }
};

// The eased value of `t` through an Ease With Curve node whose text field is `arg` (already quoted if it needs to be).
float EaseOf(const std::string& arg, const std::string& tValue) {
    World w;
    w.Run("node 2 SetVar out\nnode 3 EaseCurve " + arg + "\nnode 4 Const " + tValue + "\nexec 1 0 2\ndata 2 0 3 0\ndata 3 0 4 0\n");
    return w.Var("out");
}

} // namespace

int main() {
    RUN_SUITE("vs_tween");

    // ---- Ease With Curve: presets, bezier, steps, the CSS names; unknown text and bad input.
    CHECK_NEAR(EaseOf("InQuad", "0.5"), 0.25f, 1e-5);
    CHECK_NEAR(EaseOf("OutQuad", "0.5"), 0.75f, 1e-5);
    CHECK_NEAR(EaseOf("out_back", "0.5"), Easing::Evaluate(Ease::BackOut, 0.5f), 1e-5);
    CHECK_NEAR(EaseOf("\"cubic-bezier(0.4, 0, 0.2, 1)\"", "0.5"), Easing::CubicBezier(0.4f, 0.0f, 0.2f, 1.0f, 0.5f), 1e-5);
    CHECK_NEAR(EaseOf("cubic-bezier(0.4, 0, 0.2, 1)", "0.5"), Easing::CubicBezier(0.4f, 0.0f, 0.2f, 1.0f, 0.5f), 1e-5);   // split at its spaces
    CHECK_NEAR(EaseOf("\"steps(4)\"", "0.3"), 0.25f, 1e-6);
    CHECK_NEAR(EaseOf("\"steps(4, start)\"", "0.3"), 0.5f, 1e-6);
    CHECK_NEAR(EaseOf("ease-in-out", "0.5"), Easing::CubicBezier(0.42f, 0.0f, 0.58f, 1.0f, 0.5f), 1e-5);
    CHECK_NEAR(EaseOf("nonsense", "0.3"), 0.3f, 1e-6);          // unknown: a straight line
    CHECK_NEAR(EaseOf("InQuad", "-4"), 0.0f, 1e-6);             // time is clamped to 0..1
    CHECK_NEAR(EaseOf("InQuad", "9"), 1.0f, 1e-6);
    CHECK_NEAR(EaseOf("InQuad", "nan"), 0.0f, 1e-6);            // NaN is 0
    CHECK(EaseOf("OutBack", "0.8") > 1.0f);                     // an overshooting ease overshoots
    {   // Unwired: time 0. The node is a data node with one number in and one out.
        World w;
        CHECK(w.Run("node 2 SetVar out\nnode 3 EaseCurve OutQuad\nexec 1 0 2\ndata 2 0 3 0\n"));
        CHECK_NEAR(w.Var("out"), 0.0f, 1e-6);
    }

    // ---- Spring Step: the same numbers as Spring::Step, no overshoot at critical damping, retargeting bends.
    {
        World w;
        CHECK(w.Frames("node 2 SpringStep 0\ndefault 2 0 10\ndefault 2 1 100\ndefault 2 2 20\n"
                       "node 3 SetVar x\nnode 4 SetVar v\nexec 1 0 2\nexec 2 0 3\nexec 3 0 4\ndata 3 0 2 0\ndata 4 0 2 1\n"));
        float ref = 0.0f, refV = 0.0f, prev = 0.0f;
        bool monotone = true, overshoot = false;
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 180; ++i) {
            w.scene.Update(dt);
            Spring::Step(ref, refV, 10.0f, 100.0f, 20.0f, dt);
            if (i < 3 || i == 59) { CHECK_NEAR(w.Var("x"), ref, 1e-4); CHECK_NEAR(w.Var("v"), refV, 1e-3); }
            monotone = monotone && w.Var("x") >= prev - 1e-6f;
            overshoot = overshoot || w.Var("x") > 10.0f + 1e-4f;
            prev = w.Var("x");
        }
        CHECK(monotone && !overshoot);
        CHECK_NEAR(w.Var("x"), 10.0f, 0.02);                    // 3 seconds in: arrived
        CHECK(std::fabs(w.Var("v")) < 0.05f);
    }
    {   // No start text: it starts at the first target (no swing in); then a new target pulls it along with its velocity kept.
        World w;
        CHECK(w.Frames("node 2 SpringStep\nnode 3 GetVar goal\nnode 4 SetVar x\nnode 5 SetVar v\n"
                       "exec 1 0 2\nexec 2 0 4\nexec 4 0 5\ndata 2 0 3 0\ndata 4 0 2 0\ndata 5 0 2 1\n"));
        w.vs->SetVariable("goal", vs::VsValue{7.0f});
        w.scene.Update(1.0f / 60.0f);
        CHECK_NEAR(w.Var("x"), 7.0f, 1e-5);
        CHECK_NEAR(w.Var("v"), 0.0f, 1e-5);
        w.vs->SetVariable("goal", vs::VsValue{9.0f});
        for (int i = 0; i < 10; ++i) w.scene.Update(1.0f / 60.0f);
        CHECK(w.Var("x") > 7.0f && w.Var("x") < 9.5f && w.Var("v") > 0.0f);
        for (int i = 0; i < 240; ++i) w.scene.Update(1.0f / 60.0f);
        CHECK_NEAR(w.Var("x"), 9.0f, 0.05);                     // the default spring rings a little, then settles
    }
    {   // A step of any length lands where the small ones do; a NaN target and a zero step change nothing.
        World w;
        CHECK(w.Frames("node 2 SpringStep 0\ndefault 2 0 5\nnode 3 SetVar x\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n"));
        w.scene.Update(0.0f);
        CHECK_NEAR(w.Var("x"), 0.0f, 1e-6);
        w.scene.Update(100.0f);
        CHECK_NEAR(w.Var("x"), 5.0f, 1e-3);
        World n;
        CHECK(n.Frames("node 2 SpringStep 1\nnode 3 Const nan\nnode 4 SetVar x\nexec 1 0 2\nexec 2 0 4\ndata 2 0 3 0\ndata 4 0 2 0\n"));
        for (int i = 0; i < 5; ++i) n.scene.Update(0.1f);
        CHECK_NEAR(n.Var("x"), 1.0f, 1e-6);
        World nan;
        CHECK(nan.Frames("node 2 SpringStep\nnode 3 Const nan\nnode 4 SetVar x\nexec 1 0 2\nexec 2 0 4\ndata 2 0 3 0\ndata 4 0 2 0\n"));
        nan.scene.Update(0.1f);
        CHECK(std::isfinite(nan.Var("x")));                     // started at 0 because the first target was not a number
    }

    // ---- Tween Move / Tween Move By Speed.
    {   // By speed: 6 units at 3 a second last 2 s. The y and z of the object stay (unwired).
        World w;
        w.hero->transform->localPosition = {0, 2, 1};
        CHECK(w.Run("node 2 TweenMoveSpeed intro\ndefault 2 0 6\ndefault 2 3 3\ndefault 2 4 Linear\nexec 1 0 2\n"));
        CHECK(w.scene.tweens().CountByTag("intro") == 1);
        w.scene.Update(1.0f);
        CHECK_NEAR(w.hero->transform->localPosition.x, 3.0f, 1e-3);
        CHECK_NEAR(w.hero->transform->localPosition.y, 2.0f, 1e-6);
        CHECK_NEAR(w.hero->transform->localPosition.z, 1.0f, 1e-6);
        w.scene.Update(1.0f);
        CHECK_NEAR(w.hero->transform->localPosition.x, 6.0f, 1e-3);
        CHECK(w.scene.tweens().Count() == 0);
    }
    {   // In seconds, to a point in three axes, with a curve as the ease.
        World w;
        CHECK(w.Run("node 2 TweenMove\ndefault 2 0 4\ndefault 2 1 8\ndefault 2 2 -2\ndefault 2 3 2\ndefault 2 4 \"steps(2)\"\nexec 1 0 2\n"));
        w.scene.Update(0.9f);
        CHECK_NEAR(w.hero->transform->localPosition.x, 0.0f, 1e-4);   // the first step of two has not come yet
        w.scene.Update(0.2f);
        CHECK_NEAR(w.hero->transform->localPosition.x, 2.0f, 1e-4);   // 1.1 s of 2: half way
        CHECK_NEAR(w.hero->transform->localPosition.y, 4.0f, 1e-4);
        CHECK_NEAR(w.hero->transform->localPosition.z, -1.0f, 1e-4);
        w.scene.Update(1.0f);
        CHECK_NEAR(w.hero->transform->localPosition.x, 4.0f, 1e-4);
    }
    {   // The Target pin (after the ease): "Other" moves, Hero does not. Speed zero or less is at once; an unknown ease keeps Out Quad.
        World w;
        CHECK(w.Run("node 2 TweenMoveSpeed\ndefault 2 0 5\ndefault 2 3 0\ndefault 2 4 nonsense\nnode 3 Const \"Other\"\nexec 1 0 2\ndata 2 5 3 0\n"));
        w.scene.Update(0.1f);
        CHECK_NEAR(w.other->transform->localPosition.x, 5.0f, 1e-4);
        CHECK_NEAR(w.hero->transform->localPosition.x, 0.0f, 1e-6);
    }
    {   // Bad numbers never reach the transform.
        World w;
        CHECK(w.Run("node 2 TweenMove\nnode 3 Const nan\nnode 4 Const inf\nexec 1 0 2\ndata 2 0 3 0\ndata 2 1 4 0\ndata 2 3 3 0\n"));
        for (int i = 0; i < 5; ++i) w.scene.Update(0.1f);
        const Vec3 p = w.hero->transform->localPosition;
        CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    }

    // ---- Kill / Pause / Play Tweens With Tag.
    {
        World w;
        float a = 0, b = 0, c = 0;
        auto ta = w.scene.tweens().Virtual(0.0f, 1.0f, 2.0f, [&](float v) { a = v; });
        auto tb = w.scene.tweens().Virtual(0.0f, 1.0f, 2.0f, [&](float v) { b = v; });
        auto tc = w.scene.tweens().Virtual(0.0f, 1.0f, 2.0f, [&](float v) { c = v; });
        for (auto* t : {ta.get(), tb.get()}) t->SetTag("menu").SetEase(Ease::Linear);
        tc->SetEase(Ease::Linear);
        CHECK(w.Run("node 2 TweenPauseTag menu\nnode 3 SetVar paused\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n"));
        CHECK(w.Var("paused") == 2.0f);
        w.scene.Update(1.0f);
        CHECK_NEAR(a, 0.0f, 1e-6); CHECK_NEAR(b, 0.0f, 1e-6); CHECK_NEAR(c, 0.5f, 1e-4);   // the tag only
        CHECK(w.Load("node 1 OnStart\nnode 2 TweenPlayTag menu\nnode 3 SetVar played\nentry OnStart 1\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n"));
        w.vs->Start();
        CHECK(w.Var("played") == 2.0f);
        w.scene.Update(0.5f);
        CHECK_NEAR(a, 0.25f, 1e-4); CHECK_NEAR(b, 0.25f, 1e-4);
        // Kill, jumping to the end first: the end value is written, the tweens go, the count says how many.
        CHECK(w.Load("node 1 OnStart\nnode 2 TweenKillTag menu\ndefault 2 0 true\nnode 3 SetVar killed\nentry OnStart 1\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n"));
        w.vs->Start();
        CHECK(w.Var("killed") == 2.0f);
        CHECK_NEAR(a, 1.0f, 1e-5); CHECK_NEAR(b, 1.0f, 1e-5);
        CHECK(!ta->IsActive() && !tb->IsActive() && tc->IsActive());
        CHECK(w.scene.tweens().CountByTag("menu") == 0);
        // Nothing with that tag, an empty tag: nothing happens and the count is 0.
        w.vs->Start();
        CHECK(w.Var("killed") == 0.0f);
        CHECK(w.Load("node 1 OnStart\nnode 2 TweenKillTag\nnode 3 SetVar killed\nentry OnStart 1\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n"));
        w.vs->Start();
        CHECK(w.Var("killed") == 0.0f && tc->IsActive());
    }

    // ---- The factory, the pin table and the Target pin; every new type runs unwired.
    {
        std::string err;
        const char* types[] = {"EaseCurve", "SpringStep", "TweenMove", "TweenMoveSpeed", "TweenKillTag", "TweenPauseTag", "TweenPlayTag"};
        for (const char* t : types) {
            CHECK(vs::MakeNode(t, {}, &err) != nullptr);
            CHECK(vs::SignatureFor(t) != nullptr);
            const auto all = vs::AllNodeTypes();
            CHECK(std::find(all.begin(), all.end(), std::string(t)) != all.end());
            World w;
            const auto node = vs::MakeNode(t, {}, &err);
            const bool exec = node && node->ExecOutCount() > 0;
            const std::string graph = exec
                ? std::string("node 1 OnUpdate\nnode 2 ") + t + "\nentry OnUpdate 1\nexec 1 0 2\n"
                : std::string("node 1 OnUpdate\nnode 2 SetVar out\nnode 3 ") + t + "\nentry OnUpdate 1\nexec 1 0 2\ndata 2 0 3 0\n";
            CHECK(w.Load(graph));
            w.scene.Start();
            for (int i = 0; i < 3; ++i) w.scene.Update(1.0f / 60.0f);
        }
        CHECK(vs::SignatureFor("EaseCurve")->ins == "N" && vs::SignatureFor("EaseCurve")->outs == "N" && !vs::SignatureFor("EaseCurve")->execIn);
        CHECK(vs::SignatureFor("SpringStep")->ins == "NNN" && vs::SignatureFor("SpringStep")->outs == "NN" && vs::SignatureFor("SpringStep")->execIn);
        CHECK(vs::SignatureFor("TweenMove")->ins == "NNNNTO");           // x y z seconds ease, then the Target
        CHECK(vs::SignatureFor("TweenMoveSpeed")->ins == "NNNNTO");
        CHECK(vs::SignatureFor("TweenKillTag")->ins == "B" && vs::SignatureFor("TweenKillTag")->outs == "N");
        CHECK(vs::TargetPinFor("TweenMove") == 5 && vs::TargetPinFor("TweenMoveSpeed") == 5 && vs::TargetPinFor("SpringStep") == -1);
    }

    // ---- The script builtin the Ease With Curve node's code uses: ease_curve(t, text).
    {
        Scene s("sc");
        s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Box");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var a = 0; var b = 0; var c = 0; var d = 0; var e = 0; var f = 0;\n"
            "function start() {\n"
            "  a = ease_curve(0.5, \"InQuad\");\n"
            "  b = ease_curve(0.3, \"steps(4)\");\n"
            "  c = ease_curve(0.5, \"cubic-bezier(0.4, 0, 0.2, 1)\");\n"
            "  d = ease_curve(0.3, \"nonsense\");\n"
            "  e = ease_curve(sqrt(0 - 1), \"OutQuad\");\n"
            "  f = ease_curve(0.5);\n"
            "}\n"));
        s.Start();
        CHECK_NEAR(sc->VM()->GetGlobal("a").AsFloat(), 0.25f, 1e-5);
        CHECK_NEAR(sc->VM()->GetGlobal("b").AsFloat(), 0.25f, 1e-6);
        CHECK_NEAR(sc->VM()->GetGlobal("c").AsFloat(), Easing::CubicBezier(0.4f, 0.0f, 0.2f, 1.0f, 0.5f), 1e-5);
        CHECK_NEAR(sc->VM()->GetGlobal("d").AsFloat(), 0.3f, 1e-6);
        CHECK_NEAR(sc->VM()->GetGlobal("e").AsFloat(), 0.0f, 1e-6);
        CHECK_NEAR(sc->VM()->GetGlobal("f").AsFloat(), 0.5f, 1e-6);
    }

    TEST_MAIN_RESULT();
}
