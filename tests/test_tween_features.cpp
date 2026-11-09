// Tween additions from the 1.2 pass: custom ease curves (bezier, steps, text), tweens
// that follow a named clock channel, tweening any registered property by path, the
// TweenAnimation component's new fields and their scene round trip, and the debug dump.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Clock.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <cmath>
#include <cstring>
#include <string>

using namespace okay;

static bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

int main() {
    RUN_SUITE("tween_features");

    auto linear = [](TweenManager& m, float& v, float to, float d) {
        auto t = m.To([&v] { return v; }, [&v](float x) { v = x; }, to, d);
        t->SetEase(Ease::Linear);
        return t;
    };

    // ------------------------------------------------------- ease curves ----
    {   // Steps: a staircase over the tween.
        TweenManager m; float v = 0;
        auto t = linear(m, v, 1.0f, 1.0f);
        t->SetEaseSteps(4);
        m.Update(0.3f, 0.3f);
        CHECK(v == 0.25f);
        m.Update(0.4f, 0.4f);
        CHECK(v == 0.5f);
        m.Update(0.4f, 0.4f);                        // past the end: exactly the end value
        CHECK(v == 1.0f && m.Count() == 0);
    }
    {   // Swapping the ease: bezier, own function, preset, text. A preset call clears a curve.
        TweenManager m; float v = 0;
        auto t = linear(m, v, 1.0f, 1.0f);
        t->SetAutoKill(false);
        t->SetEaseBezier(0.0f, 0.0f, 1.0f, 1.0f);    // a straight line
        t->Goto(0.5f);
        CHECK_NEAR(v, 0.5f, 1e-4f);
        t->SetEaseCurve([](float u) { return u * u; });
        t->Goto(0.5f);
        CHECK_NEAR(v, 0.25f, 1e-5f);
        t->SetEase(Ease::Linear);
        CHECK(!t->easeCurve);
        t->Goto(0.5f);
        CHECK_NEAR(v, 0.5f, 1e-5f);
        t->SetEaseSpec("steps(2)");
        t->Goto(0.6f);
        CHECK(v == 0.5f);
        t->SetEaseSpec("OutQuad");
        CHECK(!t->easeCurve && t->ease == Ease::QuadOut);
        t->Goto(0.5f);
        CHECK_NEAR(v, 0.75f, 1e-5f);
        t->SetEaseSpec("cubic-bezier(0.42, 0, 0.58, 1)");
        t->Goto(0.5f);
        CHECK_NEAR(v, 0.5f, 1e-4f);
        t->SetEaseSpec("no such ease");              // not understood: unchanged
        t->Goto(0.25f);
        CHECK(v < 0.25f);                            // still the in-out curve
        // A curve that overshoots shows up in the value; the ends are exact.
        t->SetEaseBezier(0.3f, 1.8f, 0.6f, 1.0f);
        float peak = 0;
        for (int i = 1; i < 20; ++i) { t->Goto((float)i / 20.0f); peak = std::max(peak, v); }
        CHECK(peak > 1.0f);
        t->Goto(1.0f);
        CHECK(v == 1.0f);
        t->Goto(0.0f);
        CHECK(v == 0.0f);
    }
    {   // Yoyo and loops with a curve still land exactly on the end values.
        TweenManager m; float v = 0.25f;
        auto t = linear(m, v, 1.0f, 1.0f);
        t->SetEaseSteps(3).SetLoops(2, LoopType::Yoyo);
        m.Update(9.0f, 9.0f);
        CHECK(v == 0.25f);
    }

    // ----------------------------------------------------- clock channels ----
    {   // A pause menu freezes the game's tweens while the UI's keep running.
        ClockSet cs;
        TweenManager m; m.SetClockSet(&cs);
        float game = 0, ui = 0, cine = 0, plain = 0;
        linear(m, game, 1.0f, 1.0f)->SetClock("game");
        linear(m, ui, 1.0f, 1.0f)->SetClock("ui");
        linear(m, cine, 1.0f, 1.0f)->SetClock("cinematic");
        linear(m, plain, 1.0f, 1.0f);
        auto frame = [&](float scaled, float unscaled, bool paused) {
            cs.Tick(scaled, unscaled, paused);
            m.Update(scaled, unscaled);
        };
        frame(0.1f, 0.1f, false);
        CHECK_NEAR(game, 0.1f, 1e-6f); CHECK_NEAR(ui, 0.1f, 1e-6f); CHECK_NEAR(cine, 0.1f, 1e-6f); CHECK_NEAR(plain, 0.1f, 1e-6f);
        for (int i = 0; i < 2; ++i) frame(0.0f, 0.2f, true);       // the game is paused
        CHECK_NEAR(game, 0.1f, 1e-6f);
        CHECK_NEAR(ui, 0.5f, 1e-6f);
        CHECK_NEAR(cine, 0.1f, 1e-6f);               // a cutscene holds with the game
        CHECK_NEAR(plain, 0.1f, 1e-6f);
        cs.Get("ui").SetScale(2.0f);
        frame(0.1f, 0.1f, false);
        CHECK_NEAR(ui, 0.7f, 1e-6f);
        CHECK_NEAR(game, 0.2f, 1e-6f);
        // Two updates in one tick do not run the channel twice.
        cs.Tick(0.1f, 0.1f, false);
        m.Update(0.1f, 0.1f); m.Update(0.1f, 0.1f);
        CHECK_NEAR(ui, 0.9f, 1e-5f);
    }
    {   // Pause and Play on the tween bank no channel time; SetClock("") goes back to scene time; the clock wins over SetUpdate.
        ClockSet cs;
        TweenManager m; m.SetClockSet(&cs);
        float v = 0, w = 0;
        auto t = linear(m, v, 1.0f, 1.0f);
        t->SetClock("ui");
        auto u = linear(m, w, 1.0f, 1.0f);
        u->SetUpdate(true).SetClock("ui");
        cs.Tick(0.1f, 0.1f, false); m.Update(0.5f, 0.5f);
        CHECK_NEAR(v, 0.1f, 1e-6f); CHECK_NEAR(w, 0.1f, 1e-6f);    // not the 0.5 given to Update
        t->Pause();
        for (int i = 0; i < 5; ++i) { cs.Tick(0.1f, 0.1f, false); m.Update(0.1f, 0.1f); }
        CHECK_NEAR(v, 0.1f, 1e-6f);
        t->Play();
        cs.Tick(0.1f, 0.1f, false); m.Update(0.1f, 0.1f);
        CHECK_NEAR(v, 0.2f, 1e-6f);                  // no jump after the pause
        t->SetClock("");
        cs.Tick(0.1f, 0.1f, false); m.Update(0.05f, 0.05f);
        CHECK_NEAR(v, 0.25f, 1e-6f);                 // scene time again
    }
    {   // A sequence follows its channel; the tweens inside take their time from it.
        ClockSet cs;
        TweenManager m; m.SetClockSet(&cs);
        float a = 0, b = 0;
        auto seq = m.Sequence();
        auto ta = linear(m, a, 1.0f, 1.0f);
        auto tb = linear(m, b, 1.0f, 1.0f);
        tb->SetClock("game");                        // ignored: only the outermost tween reads a clock
        seq->Append(ta).Append(tb);
        seq->SetClock("ui");
        cs.Get("ui").SetScale(0.5f);
        for (int i = 0; i < 4; ++i) { cs.Tick(0.5f, 0.5f, false); m.Update(0.5f, 0.5f); }
        CHECK_NEAR(a, 1.0f, 1e-5f);                  // 4 x 0.25 s of channel time: the first of its two 1 s tweens is done
        CHECK(b == 0.0f);
    }
    {   // The default clock set is the process-wide one.
        ClockSet::Global().Reset();
        TweenManager m;
        float v = 0;
        linear(m, v, 1.0f, 1.0f)->SetClock("ui");
        Time::Step(0.25f); ClockSet::Global().TickFromTime(); m.Update(Time::DeltaTime(), Time::UnscaledDeltaTime());
        CHECK_NEAR(v, 0.25f, 1e-6f);
        Clock::Get("ui").SetPaused(true);
        Time::Step(0.25f); ClockSet::Global().TickFromTime(); m.Update(Time::DeltaTime(), Time::UnscaledDeltaTime());
        CHECK_NEAR(v, 0.25f, 1e-6f);
        ClockSet::Global().Reset();
    }

    {   // No explicit tick: the manager ticks the process-wide clocks once per Time frame itself.
        ClockSet::Global().Reset();
        TweenManager m;
        float v = 0, w = 0;
        linear(m, v, 1.0f, 1.0f)->SetClock("ui");
        linear(m, w, 1.0f, 1.0f)->SetClock("ui");
        Time::Step(0.25f); m.Update(Time::DeltaTime(), Time::UnscaledDeltaTime());
        CHECK_NEAR(v, 0.25f, 1e-6f); CHECK_NEAR(w, 0.25f, 1e-6f);
        Time::Step(0.25f); ClockSet::Global().TickFromTime();       // a host that ticks itself: no second tick
        m.Update(Time::DeltaTime(), Time::UnscaledDeltaTime()); m.Update(Time::DeltaTime(), Time::UnscaledDeltaTime());
        CHECK_NEAR(v, 0.5f, 1e-6f);
        ClockSet::Global().Reset();
    }

    // ------------------------------------------------------ property tween ----
    {   // Any registered property by path: a float, a vector, one vector component, a colour, a rotation, an int.
        Scene s("S");
        GameObject* go = s.CreateGameObject("G");
        auto* light = go->AddComponent<Light>();
        light->intensity = 1.0f;
        auto* sprite = go->AddComponent<SpriteRenderer>();
        sprite->color = Color{0, 0, 0, 1};
        std::string err;
        auto fl = DOTween::Property(go, "Light.intensity", PropertyValue::MakeFloat(5.0f), 1.0f, &err);
        fl->SetEase(Ease::Linear);
        auto pos = DOTween::Property(go, "Transform.position", PropertyValue::MakeVec3(Vec3{2, 4, 6}), 1.0f);
        pos->SetEase(Ease::Linear);
        auto y = DOTween::Property(go, "Transform.scale.y", PropertyValue::MakeFloat(3.0f), 1.0f);
        y->SetEase(Ease::Linear);
        auto col = DOTween::Property(go, "SpriteRenderer.color", PropertyValue::MakeColor(Color{1, 0.5f, 0, 1}), 1.0f);
        col->SetEase(Ease::Linear);
        auto rot = DOTween::Property(go, "Transform.rotation", PropertyValue::MakeQuat(Quat::Euler(0, 90, 0)), 1.0f);
        rot->SetEase(Ease::Linear);
        auto layer = DOTween::Property(go, "GameObject.layer", PropertyValue::MakeInt(10), 1.0f);
        layer->SetEase(Ease::Linear);
        CHECK(err.empty());
        CHECK(fl->IsActive() && pos->IsActive() && y->IsActive() && col->IsActive() && rot->IsActive() && layer->IsActive());
        CHECK(s.tweens().Count() == 6);
        s.Update(0.5f);
        CHECK_NEAR(light->intensity, 3.0f, 1e-4f);
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-4f);
        CHECK_NEAR(go->transform->localPosition.z, 3.0f, 1e-4f);
        CHECK_NEAR(go->transform->localScale.y, 2.0f, 1e-4f);
        CHECK_NEAR(go->transform->localScale.x, 1.0f, 1e-6f);        // only the y component moved
        CHECK_NEAR(sprite->color.r, 0.5f, 1e-4f);
        CHECK_NEAR(sprite->color.g, 0.25f, 1e-4f);
        CHECK_NEAR(Quat::Angle(go->transform->localRotation, Quat::Euler(0, 45, 0)), 0.0f, 0.2f);
        CHECK(go->layer == 5);
        s.Update(0.5f);
        CHECK_NEAR(light->intensity, 5.0f, 1e-5f);
        CHECK_NEAR(go->transform->localPosition.y, 4.0f, 1e-5f);
        CHECK_NEAR(sprite->color.g, 0.5f, 1e-5f);
        CHECK_NEAR(Quat::Angle(go->transform->localRotation, Quat::Euler(0, 90, 0)), 0.0f, 0.2f);
        CHECK(go->layer == 10);
        CHECK(s.tweens().Count() == 0);
    }
    {   // Relative, From, delay, loops and callbacks work like any other tween; an Int is accepted for a Float.
        Scene s("S");
        GameObject* go = s.CreateGameObject("G");
        go->transform->localPosition = Vec3{1, 1, 1};
        DOTween::Property(go, "Transform.position", PropertyValue::MakeVec3(Vec3{2, 0, 0}), 1.0f)
            ->SetRelative().SetEase(Ease::Linear);
        s.Update(1.0f);
        CHECK_NEAR(go->transform->localPosition.x, 3.0f, 1e-4f);
        go->transform->localPosition = Vec3{0, 0, 0};
        int done = 0;
        DOTween::Property(go, "Transform.position.x", PropertyValue::MakeInt(4), 1.0f)
            ->SetEase(Ease::Linear).SetDelay(0.5f).OnComplete([&] { ++done; });
        s.Update(0.5f);
        CHECK_NEAR(go->transform->localPosition.x, 0.0f, 1e-6f);
        s.Update(0.5f);
        CHECK_NEAR(go->transform->localPosition.x, 2.0f, 1e-4f);
        s.Update(0.5f);
        CHECK_NEAR(go->transform->localPosition.x, 4.0f, 1e-4f);
        CHECK(done == 1);
        auto from = DOTween::Property(go, "Transform.position.y", PropertyValue::MakeFloat(10.0f), 1.0f);
        from->From().SetEase(Ease::Linear);
        s.Update(0.0f);
        s.Update(0.5f);
        CHECK_NEAR(go->transform->localPosition.y, 5.0f, 1e-3f);    // plays from 10 back to where it was
        s.Update(0.5f);
        CHECK_NEAR(go->transform->localPosition.y, 0.0f, 1e-4f);
    }
    {   // What cannot be tweened gives an inactive tween and says why; nothing throws or crashes.
        Scene s("S");
        GameObject* go = s.CreateGameObject("G");
        std::string err;
        auto a = DOTween::Property(go, "Light.intensity", PropertyValue::MakeFloat(2.0f), 1.0f, &err);
        CHECK(!a->IsActive() && !err.empty());          // no Light on this object
        go->AddComponent<Light>();
        auto b = DOTween::Property(go, "Light.intensity", PropertyValue::MakeVec3(Vec3{1, 2, 3}), 1.0f, &err);
        CHECK(!b->IsActive() && err.find("Float") != std::string::npos);
        auto c = DOTween::Property(go, "Light.nope", PropertyValue::MakeFloat(1.0f), 1.0f, &err);
        CHECK(!c->IsActive() && !err.empty());
        auto d = DOTween::Property(go, "GameObject.name", PropertyValue::MakeString("x"), 1.0f, &err);
        CHECK(!d->IsActive() && err.find("cannot be tweened") != std::string::npos);
        auto e = DOTween::Property(nullptr, "Light.intensity", PropertyValue::MakeFloat(1.0f), 1.0f, &err);
        CHECK(!e->IsActive());
        auto f = DOTween::Property(go, "Light.intensity", PropertyValue{}, 1.0f);
        CHECK(!f->IsActive());
        CHECK(s.tweens().Count() == 0);
        // Chaining on the inactive tween is harmless.
        a->SetEase(Ease::BackOut).SetLoops(-1).OnComplete([] {});
        s.Update(1.0f);
    }
    {   // The component or the object going away mid-tween just ends the effect.
        Scene s("S");
        GameObject* go = s.CreateGameObject("G");
        auto* light = go->AddComponent<Light>();
        light->intensity = 1.0f;
        DOTween::Property(go, "Light.intensity", PropertyValue::MakeFloat(9.0f), 1.0f);
        s.Update(0.2f);
        go->RemoveComponent(light);
        s.Update(0.2f); s.Update(1.0f);                 // nothing writes into the freed Light
        CHECK(s.tweens().Count() == 0);
        GameObject* other = s.CreateGameObject("Other");
        DOTween::Property(other, "Transform.position", PropertyValue::MakeVec3(Vec3{5, 5, 5}), 1.0f);
        s.Update(0.2f);
        s.Destroy(other);
        s.Update(0.1f);
        CHECK(s.tweens().Count() == 0);
        s.Update(1.0f);
        // Values are clamped by the property's own range, like any write through the registry.
        GameObject* c = s.CreateGameObject("C");
        auto* l = c->AddComponent<Light>();
        DOTween::Property(c, "Light.intensity", PropertyValue::MakeFloat(5000.0f), 1.0f)->SetEase(Ease::Linear);
        s.Update(1.0f);
        CHECK(l->intensity <= 1000.0f && l->intensity > 0.0f);
    }

    // --------------------------------------------------- TweenManager dump ----
    {
        ClockSet cs;
        TweenManager m; m.SetClockSet(&cs);
        CHECK(m.Dump() == "Tweens: 0 (0 playing)");
        float a = 0, b = 0, c = 0;
        linear(m, a, 1.0f, 1.0f)->SetId("fade");
        auto tb = linear(m, b, 1.0f, 2.0f);
        tb->SetClock("ui").SetLoops(-1);
        auto tc = m.Sequence();
        tc->AppendInterval(1.0f);
        tc->SetUpdate(true);
        auto td = linear(m, c, 1.0f, 1.0f);
        td->Pause();
        cs.Tick(0.25f, 0.25f, false);
        m.Update(0.25f, 0.25f);
        const std::string d = m.Dump();
        CHECK(d.rfind("Tweens: 4 (3 playing)", 0) == 0);
        CHECK(d.find("value \"fade\" 0.25/1s loop 1/1") != std::string::npos);
        CHECK(d.find("value 0.25/2s loop 1/forever [ui]") != std::string::npos);
        CHECK(d.find("sequence 0.25/1s loop 1/1 [unscaled]") != std::string::npos);
        CHECK(d.find("value 0/1s loop 1/1 [paused]") != std::string::npos);
    }

    // --------------------------------------------- TweenAnimation component ----
    {   // New fields, all fields exact through a scene round trip, and an older record still loads.
        Scene s("S");
        GameObject* go = s.CreateGameObject("Logo");
        auto* ta = go->AddComponent<TweenAnimation>();
        ta->type = (int)TweenAnimation::Type::Jump;
        ta->endValue = Vec3{0.1f, 1.0f / 3.0f, 1.0e-7f};
        ta->endColor = Color{0.1f, 0.2f, 1.0f / 3.0f, 0.7f};
        ta->endFloat = 12345.678f;
        ta->relative = true; ta->from = true;
        ta->duration = 0.33333334f; ta->delay = 0.1f;
        ta->ease = (int)Ease::BounceInOut;
        ta->loops = -1; ta->loopType = (int)LoopType::Incremental;
        ta->autoPlay = false; ta->autoKill = false; ta->unscaled = true;
        ta->vibrato = 17; ta->elasticity = 0.7f; ta->jumps = 4;
        ta->id = "hop \"x\"";
        ta->clock = "ui";
        ta->easeCurve = "cubic-bezier(0.4,0,0.2,1)";
        auto* plain = go->AddComponent<TweenAnimation>();   // defaults: nothing extra is written
        const std::string text = SceneSerializer::SerializeObject(*go);
        CHECK(text.find("\"ui\" \"cubic-bezier(0.4,0,0.2,1)\"") != std::string::npos);
        Scene s2("S2");
        GameObject* back = SceneSerializer::InstantiateFromText(s2, text);
        CHECK(back != nullptr);
        auto list = back ? back->GetComponents<TweenAnimation>() : std::vector<TweenAnimation*>{};
        CHECK(list.size() == 2);
        if (list.size() == 2) {
            const TweenAnimation* r = list[0];
            CHECK(r->type == ta->type);
            CHECK(SameBits(r->endValue.x, ta->endValue.x) && SameBits(r->endValue.y, ta->endValue.y) && SameBits(r->endValue.z, ta->endValue.z));
            CHECK(SameBits(r->endColor.r, ta->endColor.r) && SameBits(r->endColor.g, ta->endColor.g) &&
                  SameBits(r->endColor.b, ta->endColor.b) && SameBits(r->endColor.a, ta->endColor.a));
            CHECK(SameBits(r->endFloat, ta->endFloat) && SameBits(r->duration, ta->duration) && SameBits(r->delay, ta->delay));
            CHECK(SameBits(r->elasticity, ta->elasticity));
            CHECK(r->relative && r->from && !r->autoPlay && !r->autoKill && r->unscaled);
            CHECK(r->ease == ta->ease && r->loops == -1 && r->loopType == (int)LoopType::Incremental);
            CHECK(r->vibrato == 17 && r->jumps == 4);
            CHECK(r->id == ta->id && r->clock == "ui" && r->easeCurve == ta->easeCurve);
            CHECK(list[1]->clock.empty() && list[1]->easeCurve.empty() && list[1]->id.empty());
            (void)plain;
        }
        // A record written before the tail existed (it ends after the id) loads with empty extras.
        Scene s3("S3");
        GameObject* g3 = s3.CreateGameObject("Old");
        g3->AddComponent<TweenAnimation>()->id = "go";
        const std::string oldText = SceneSerializer::SerializeObject(*g3);
        CHECK(oldText.find("\"go\"\n") != std::string::npos);   // the id is the last token: the older layout
        Scene s5("S5");
        GameObject* old = SceneSerializer::InstantiateFromText(s5, oldText);
        auto ol = old ? old->GetComponents<TweenAnimation>() : std::vector<TweenAnimation*>{};
        CHECK(ol.size() == 1);
        if (ol.size() == 1) CHECK(ol[0]->id == "go" && ol[0]->clock.empty() && ol[0]->easeCurve.empty());
        // The text of a default component is unchanged from before (no tail).
        Scene s4("S4");
        GameObject* g4 = s4.CreateGameObject("P");
        g4->AddComponent<TweenAnimation>();
        const std::string t4 = SceneSerializer::SerializeObject(*g4);
        CHECK(t4.find("tweenanim") != std::string::npos);
        const std::size_t at = t4.find("tweenanim");
        const std::string line = t4.substr(at, t4.find('\n', at) - at);
        CHECK(line.size() >= 3 && line.substr(line.size() - 3) == " \"\"");
        CHECK(std::count(line.begin(), line.end(), '"') == 2);
    }
    {   // The component plays on its clock channel with its own ease text.
        ClockSet::Global().Reset();
        Scene s("S");
        s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Menu");
        auto* ta = go->AddComponent<TweenAnimation>();
        ta->type = (int)TweenAnimation::Type::LocalMove;
        ta->endValue = Vec3{4, 0, 0};
        ta->duration = 1.0f;
        ta->clock = "ui";
        ta->easeCurve = "steps(4)";
        s.Start();
        auto cur = ta->Current();
        CHECK(cur && cur->clock == "ui" && (bool)cur->easeCurve);
        Time::Step(0.3f); ClockSet::Global().TickFromTime(); s.Update(Time::DeltaTime());
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-5f);    // one step of four
        Clock::Get("ui").Pause();
        Time::Step(0.3f); ClockSet::Global().TickFromTime(); s.Update(Time::DeltaTime());
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-5f);    // the ui clock is paused
        Clock::Get("ui").Resume();
        Time::Step(0.5f); ClockSet::Global().TickFromTime(); s.Update(Time::DeltaTime());
        CHECK_NEAR(go->transform->localPosition.x, 3.0f, 1e-5f);    // 0.8 s: three steps
        ClockSet::Global().Reset();
    }

    // ----------------------------------------------------------- scripting ----
    {   // tween_prop from a script: a float property, one vector component, an integer property; a bad path does nothing.
        Scene s("sp"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Lamp");
        auto* light = go->AddComponent<Light>();
        light->intensity = 1.0f;
        auto* sprite = go->AddComponent<SpriteRenderer>();
        sprite->color = Color{1, 1, 1, 1};
        go->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "function start() {\n"
            "  tween_prop(\"Light.intensity\", 5, 1.0, \"linear\");\n"
            "  tween_prop(\"Transform.scale.y\", 3, 1.0, \"linear\");\n"
            "  tween_prop(\"GameObject.layer\", 7, 1.0, \"linear\");\n"
            "  tween_prop(\"SpriteRenderer.color.a\", 0, 1.0, \"linear\");\n"
            "  var id = tween_prop(\"Nope.nothing\", 1, 1.0);\n"
            "  tween_prop(\"Light.intensity\", 1);\n"
            "}\n");
        s.Start();
        CHECK(s.tweens().Count() == 4);
        s.Update(0.5f);
        CHECK_NEAR(light->intensity, 3.0f, 1e-3f);
        CHECK_NEAR(go->transform->localScale.y, 2.0f, 1e-3f);
        CHECK(go->layer == 4);                      // 3.5 rounds to 4
        CHECK_NEAR(sprite->color.a, 0.5f, 1e-3f);
        s.Update(0.5f);
        CHECK_NEAR(light->intensity, 5.0f, 1e-4f);
        CHECK_NEAR(go->transform->localScale.y, 3.0f, 1e-4f);
        CHECK(go->layer == 7);
        CHECK_NEAR(sprite->color.a, 0.0f, 1e-4f);
        CHECK(s.tweens().Count() == 0);
    }
    {   // tween_clock and ease text from a script.
        ClockSet::Global().Reset();
        Scene s("ts"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Box");
        go->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "function start() {\n"
            "  var t = tween_move(4, 0, 1.0, \"steps(4)\");\n"
            "  tween_set_id(t, \"mv\");\n"
            "  tween_clock(t, \"ui\");\n"
            "  var u = tween_scale(2, 1.0);\n"
            "  tween_ease(u, \"cubic-bezier(0, 0, 1, 1)\");\n"
            "  tween_set_id(u, \"sc\");\n"
            "}\n");
        s.Start();
        auto mv = s.tweens().WithId("mv");
        auto sc = s.tweens().WithId("sc");
        CHECK(mv.size() == 1 && sc.size() == 1);
        if (mv.size() == 1 && sc.size() == 1) {
            CHECK(mv[0]->clock == "ui" && (bool)mv[0]->easeCurve);
            CHECK(sc[0]->clock.empty() && (bool)sc[0]->easeCurve);
            CHECK_NEAR(mv[0]->Eased(0.3f), 0.25f, 1e-6f);
            CHECK_NEAR(sc[0]->Eased(0.3f), 0.3f, 1e-4f);
        }
        ClockSet::Global().Reset();
    }

    TEST_MAIN_RESULT();
}
