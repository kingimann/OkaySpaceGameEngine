#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Animator.hpp"
#include "okay/Animation/ClipPresets.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

// Records every clip event that reaches the object's components.
struct EventSink : Component {
    std::vector<std::string> got;
    void OnAnimationEvent(const std::string& n) override { got.push_back(n); }
};

int main() {
    RUN_SUITE("anim_clip_tools");

    // ---- Keyframe reduction: straight runs collapse, curves stay within tolerance.
    {
        AnimationCurve ramp; ramp.smooth = false;
        for (int i = 0; i <= 100; ++i) ramp.AddKey(i / 30.0f, i * 0.1f);
        CHECK(ramp.Reduce(1e-4f) == 99);
        CHECK(ramp.Count() == 2);
        CHECK(std::fabs(ramp.Evaluate(50 / 30.0f) - 5.0f) < 1e-4f);

        AnimationCurve wave; wave.smooth = false;
        std::vector<std::pair<float, float>> orig;
        for (int i = 0; i <= 120; ++i) { float t = i / 30.0f, v = std::sin(t * 2.0f); wave.AddKey(t, v); orig.push_back({t, v}); }
        std::size_t removed = wave.Reduce(0.002f);
        CHECK(removed > 40);                                    // most of a smooth wave is near-linear locally
        float worst = 0.0f;
        for (auto& o : orig) worst = std::fmax(worst, std::fabs(wave.Evaluate(o.first) - o.second));
        CHECK(worst <= 0.002f + 1e-5f);
        CHECK(std::fabs(wave.Keys().front().time) < 1e-6f && std::fabs(wave.Keys().back().time - 4.0f) < 1e-5f);

        AnimationCurve smoothC;                                 // cubic curves are left alone
        for (int i = 0; i < 10; ++i) smoothC.AddKey((float)i, (float)i);
        CHECK(smoothC.Reduce(0.1f) == 0);

        // Clip-level: a held (constant) track drops to its two ends.
        AnimationClip c;
        c.smoothKeys = false;
        for (int i = 0; i <= 60; ++i) { c.AddKey("rotation.qw", i / 30.0f, 1.0f); c.AddKey("position.x", i / 30.0f, i * 2.0f); }
        std::size_t before = c.KeyCount();
        c.Reduce();
        CHECK(before == 122 && c.KeyCount() == 4);
    }

    // ---- Reduce skips its recheck on runs of equal values (a speed change for baked imports, docs/content/model_readers.md (FBX part)):
    // the kept keys must be exactly the ones the plain rule keeps (every key between two kept keys checked against the
    // straight line), on curves with long flat runs, steps, ramps, repeated times and non-finite values.
    {
        auto reference = [](const std::vector<Keyframe>& k, float tol) {
            std::vector<Keyframe> out; out.push_back(k[0]);
            std::size_t a = 0;
            while (a + 1 < k.size()) {
                std::size_t b = a + 1;
                while (b + 1 < k.size()) {
                    const std::size_t c = b + 1;
                    const float span = k[c].time - k[a].time;
                    bool fits = span > 0.0f;
                    for (std::size_t i = a + 1; i < c && fits; ++i) {
                        const float u = (k[i].time - k[a].time) / span;
                        if (std::fabs(k[a].value + (k[c].value - k[a].value) * u - k[i].value) > tol) fits = false;
                    }
                    if (!fits) break;
                    b = c;
                }
                out.push_back(k[b]); a = b;
            }
            return out;
        };
        unsigned seed = 12345;
        auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
        int same = 0, runs = 0;
        for (int trial = 0; trial < 400; ++trial) {
            AnimationCurve cv; cv.smooth = false;
            float t = 0.0f, v = 0.0f;
            const int n = 3 + (int)(rnd() % 300);
            for (int i = 0; i < n; ++i) {
                const int kind = (int)(rnd() % 10);
                if (kind < 5) {}                                   // flat: same value again
                else if (kind < 7) v += 0.01f * (float)(rnd() % 7);  // small steps
                else if (kind == 7) v = -0.0f;                     // signed zero
                else if (kind == 8 && trial % 7 == 0) v = (rnd() % 2) ? INFINITY : NAN;
                else v = (float)(rnd() % 100) * 0.5f;
                if (rnd() % 20) t += 1.0f / 30.0f;                 // sometimes a repeated time
                cv.AddKey(t, v);
            }
            const std::vector<Keyframe> want = reference(cv.Keys(), 0.0005f);
            cv.Reduce(0.0005f);
            bool eq = want.size() == cv.Keys().size();
            for (std::size_t i = 0; eq && i < want.size(); ++i)
                eq = std::memcmp(&want[i], &cv.Keys()[i], sizeof(Keyframe)) == 0;
            same += eq; ++runs;
        }
        CHECK(same == runs);
        AnimationCurve flat; flat.smooth = false;              // a long held track: two keys left, and quickly
        for (int i = 0; i < 200000; ++i) flat.AddKey((float)i / 30.0f, 1.0f);
        CHECK(flat.Reduce(0.0005f) == 199998);
    }

    // ---- Explicit length: short tracks hold instead of looping on their own span.
    {
        AnimationClip c;
        c.AddKey("position.x", 0.0f, 0.0f);
        c.AddKey("position.x", 1.0f, 10.0f);
        c.SetLength(2.0f);
        CHECK_NEAR(c.Length(), 2.0f, 1e-4f);
        bool f; CHECK_NEAR(c.Evaluate("position.x", 1.5f, f), 10.0f, 1e-3f);
        c.SetLength(0.5f);                   // keys past the length still count
        CHECK_NEAR(c.Length(), 1.0f, 1e-4f);
    }

    // ---- Interpolation per group.
    {
        AnimationClip c;
        c.AddKey("scale.x", 0.0f, 1.0f); c.AddKey("scale.x", 1.0f, 2.0f);
        c.AddKey("position.x", 0.0f, 0.0f); c.AddKey("position.x", 1.0f, 1.0f);
        CHECK(c.IsSmooth("scale."));
        c.SetSmooth("scale.", false);
        CHECK(!c.IsSmooth("scale.") && c.IsSmooth("position."));
        bool f; CHECK_NEAR(c.Evaluate("scale.x", 0.25f, f), 1.25f, 1e-4f);
    }

    // ---- Copy / paste keys.
    {
        AnimationClip c; c.loop = false;
        c.AddKey("position.x", 0.5f, 1.0f); c.AddKey("position.y", 0.5f, 2.0f);
        c.AddKey("position.x", 1.0f, 3.0f); c.AddKey("scale.x", 1.0f, 9.0f);
        auto keys = c.CopyKeys(0.5f, 1.0f, "position.");
        CHECK(keys.size() == 3);
        c.PasteKeys(keys, 2.0f);
        bool f;
        CHECK_NEAR(c.Evaluate("position.x", 2.0f, f), 1.0f, 1e-4f);
        CHECK_NEAR(c.Evaluate("position.x", 2.5f, f), 3.0f, 1e-4f);
        CHECK(c.Track("scale.x")->Count() == 1);   // scale wasn't copied
        CHECK_NEAR(c.Length(), 2.5f, 1e-4f);
    }

    // ---- Events: once per pass, wrap-around, non-loop clamp.
    {
        AnimationClip c; c.loop = true;
        c.AddKey("position.x", 0.0f, 0.0f); c.AddKey("position.x", 1.0f, 1.0f);
        c.AddEvent(0.0f, "start"); c.AddEvent(0.5f, "mid");
        std::vector<std::string> got;
        auto rec = [&](const std::string& n) { got.push_back(n); };
        Animator::ForEachEventBetween(c, 0.0f, 0.4f, true, rec);
        CHECK(got.size() == 1 && got[0] == "start");
        got.clear(); Animator::ForEachEventBetween(c, 0.4f, 0.6f, false, rec);
        CHECK(got.size() == 1 && got[0] == "mid");
        got.clear(); Animator::ForEachEventBetween(c, 0.9f, 1.1f, false, rec);   // wraps past 0
        CHECK(got.size() == 1 && got[0] == "start");
        got.clear(); Animator::ForEachEventBetween(c, 0.6f, 0.9f, false, rec);
        CHECK(got.empty());
        c.loop = false;
        got.clear(); Animator::ForEachEventBetween(c, 0.9f, 3.0f, false, rec);   // clamps at the end
        CHECK(got.empty());
    }

    // ---- Animator dispatches events to the object's components.
    {
        Scene scene;
        GameObject* go = scene.CreateGameObject("Box");
        auto* an = go->AddComponent<Animator>();
        auto* sink = go->AddComponent<EventSink>();
        an->clip.AddKey("position.y", 0.0f, 0.0f);
        an->clip.AddKey("position.y", 1.0f, 1.0f);
        an->clip.AddEvent(0.25f, "step");
        an->clip.AddEvent(0.75f, "step");
        int cb = 0; an->onEvent = [&](const std::string&) { ++cb; };
        for (int i = 0; i < 120; ++i) scene.Update(1.0f / 60.0f);   // two loops
        CHECK(sink->got.size() == 4);
        CHECK(cb == 4);
    }

    // ---- A looping Animator keeps its phase; the clock does not grow without bound.
    {
        Scene scene; scene.physicsEnabled = false;
        GameObject* go = scene.CreateGameObject("Spin");
        auto* an = go->AddComponent<Animator>();
        an->clip.AddKey("position.y", 0.0f, 0.0f);
        an->clip.AddKey("position.y", 1.0f, 1.0f);
        an->clip.loop = true;
        scene.Update(1000.25f);
        CHECK(an->Time() >= 0.0f && an->Time() < 1.0f);
        CHECK_NEAR(an->Time(), 0.25f, 1e-4f);
        bool found = false;
        const float sampled = an->clip.Evaluate("position.y", an->Time(), found);
        CHECK(found);
        CHECK_NEAR(go->transform->localPosition.y, sampled, 1e-3f);
        for (int i = 0; i < 4000; ++i) scene.Update(1.0f / 60.0f);
        CHECK(an->Time() >= 0.0f && an->Time() < 1.0f);
        an->clip.loop = false;
        an->SetTime(0.0f);
        scene.Update(3.0f);
        CHECK(an->Time() > 1.5f);   // a one-shot keeps the raw time past the end
    }

    // ---- Events reach OkayScript (named handler + on_anim_event) and visual scripts.
    {
        Scene scene("Events"); scene.physicsEnabled = false;
        GameObject* go = scene.CreateGameObject("Hero");
        auto* an = go->AddComponent<Animator>();
        an->clip.AddKey("position.x", 0.0f, 0.0f); an->clip.AddKey("position.x", 1.0f, 1.0f);
        an->clip.AddEvent(0.3f, "footstep");
        an->clip.AddEvent(0.6f, "land");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        sc->LoadSource(
            "var steps = 0\n"
            "var any = 0\n"
            "var last = \"\"\n"
            "function footstep() { steps = steps + 1 }\n"
            "function on_anim_event() { any = any + 1\n last = anim_event }\n");
        auto* vsc = go->AddComponent<VisualScriptComponent>();
        std::string err;
        auto graph = vs::NodeGraph::Parse(
            "node 1 OnAnimEvent\n"
            "node 2 AddVar lands\n"
            "node 3 Const 1\n"
            "exec 1 0 2\n"
            "data 2 0 3 0\n"
            "entry OnAnimEvent:land 1\n", &err);
        CHECK(graph != nullptr);
        if (graph) vsc->SetGraph(std::move(graph));
        scene.Start();
        for (int i = 0; i < 60; ++i) scene.Update(1.0f / 60.0f);   // one pass over both events
        auto* vm = sc->VM();
        CHECK(vm != nullptr);
        if (vm) {
            CHECK_NEAR(vm->GetGlobal("steps").AsFloat(), 1.0f, 1e-4f);
            CHECK_NEAR(vm->GetGlobal("any").AsFloat(), 2.0f, 1e-4f);
            CHECK(vm->GetGlobal("last").AsString() == "land");
        }
        CHECK_NEAR(vsc->GetVariable("lands").AsFloat(), 1.0f, 1e-4f);
    }

    // ---- Presets write keys around the current pose.
    {
        AnimationClip c;
        Vec3 p{1, 2, 3}, s{1, 1, 1};
        ApplyClipPreset(c, ClipPreset::Bob, p, Quat{}, s, 0.5f, 1.0f);
        bool f;
        CHECK_NEAR(c.Evaluate("position.y", 0.0f, f), 2.0f, 1e-4f);
        CHECK_NEAR(c.Evaluate("position.y", 0.5f, f), 2.5f, 1e-4f);
        CHECK_NEAR(c.Evaluate("position.x", 0.5f, f), 1.0f, 1e-4f);
        CHECK(c.loop && c.Spanned());
        CHECK_NEAR(c.Length(), 1.0f, 1e-4f);

        ApplyClipPreset(c, ClipPreset::Spin, p, Quat{}, s, 0.0f, 2.0f);
        CHECK(c.HasTrack("rotation.qw") && !c.IsSmooth("rotation."));
        CHECK_NEAR(c.Length(), 2.0f, 1e-4f);
        CHECK(c.HasTrack("position.y"));   // other groups kept
        // Half way through the spin it faces backwards (180 deg about Y).
        float qy = c.Evaluate("rotation.qy", 1.0f, f), qw = c.Evaluate("rotation.qw", 1.0f, f);
        CHECK_NEAR(std::fabs(qy), 1.0f, 1e-3f);
        CHECK_NEAR(qw, 0.0f, 1e-3f);

        AnimationClip pop;
        ApplyClipPreset(pop, ClipPreset::PopIn, p, Quat{}, Vec3{2, 2, 2}, 0.15f, 0.5f);
        CHECK(!pop.loop);
        CHECK_NEAR(pop.Evaluate("scale.x", 0.0f, f), 0.0f, 1e-4f);
        CHECK_NEAR(pop.Evaluate("scale.x", 0.5f, f), 2.0f, 1e-4f);

        for (int i = 0; i < (int)ClipPreset::Count; ++i) {   // every preset produces keys
            AnimationClip any;
            ApplyClipPreset(any, (ClipPreset)i, p, Quat{}, s, ClipPresetDefaultAmount((ClipPreset)i), 1.0f);
            CHECK(!any.Tracks().empty());
        }
    }

    // ---- Length, linear tracks and events survive save / load.
    {
        Scene scene;
        GameObject* go = scene.CreateGameObject("Anim");
        auto* an = go->AddComponent<Animator>();
        an->clip.AddKey("position.x", 0.0f, 0.0f); an->clip.AddKey("position.x", 1.0f, 4.0f);
        an->clip.AddKey("scale.x", 0.0f, 1.0f);    an->clip.AddKey("scale.x", 0.5f, 2.0f);
        an->clip.SetSmooth("scale.", false);
        an->clip.SetLength(3.0f);
        an->clip.AddEvent(0.4f, "foot step");
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded;
        CHECK(SceneSerializer::Deserialize(loaded, text));
        GameObject* g2 = loaded.Find("Anim");
        CHECK(g2 != nullptr);
        auto* a2 = g2 ? g2->GetComponent<Animator>() : nullptr;
        CHECK(a2 != nullptr);
        if (a2) {
            CHECK_NEAR(a2->clip.Length(), 3.0f, 1e-4f);
            CHECK(!a2->clip.IsSmooth("scale.") && a2->clip.IsSmooth("position."));
            CHECK(a2->clip.events.size() == 1 && a2->clip.events[0].name == "foot step");
            bool f; CHECK_NEAR(a2->clip.Evaluate("position.x", 2.0f, f), 4.0f, 1e-4f);   // held, not looped
        }
    }
    TEST_MAIN_RESULT();
}
