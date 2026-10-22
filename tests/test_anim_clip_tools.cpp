#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Animator.hpp"
#include "okay/Animation/ClipPresets.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
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
