// Issue #888 finding 154 and the OkayScript particles_on() fix.
//
// 1. Finding 154: switching a parent off and on again in the editor (Inspector Active box, Hierarchy eye,
//    H / Alt+H) must not switch on a child that was off on purpose. Each object keeps its own `active`
//    flag; children follow their parent through ActiveInHierarchy(). Goes through oked::SetObjectActive,
//    the one helper every editor path uses, and checks the flags survive a save / load.
// 2. particles_on() / particles_on(false) in OkayScript go through ParticleSystem::Play() / Stop(), so a
//    one-shot effect that has finished plays again when a script turns it on. The replay part needs
//    Play() to start a new cycle on a finished one-shot (PR #1093).
#include "test_framework.hpp"
#include "InspectorModel.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include <iostream>
#include <string>

using namespace okay;

static GameObject* Make(Scene& s, const std::string& name, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    return g;
}

static void ActiveFlagsKept() {
    Scene s("g154");
    GameObject* parent = Make(s, "Parent");
    GameObject* on     = Make(s, "ChildOn", parent);
    GameObject* off    = Make(s, "ChildOff", parent);
    GameObject* deep   = Make(s, "DeepOff", on);     // a switched-off grandchild
    off->active = false;
    deep->active = false;

    oked::SetObjectActive(parent, false);             // untick Active
    CHECK(!parent->active);
    CHECK(!on->ActiveInHierarchy() && !off->ActiveInHierarchy() && !deep->ActiveInHierarchy());
    CHECK(on->active);                                 // children keep their own flags
    CHECK(!off->active && !deep->active);

    // While the parent is off, a save / load keeps every object's own flag.
    {
        Scene t("g154_load");
        std::string err;
        CHECK(SceneSerializer::Deserialize(t, SceneSerializer::Serialize(s), &err));
        GameObject* tp = t.Find("Parent"); GameObject* ton = t.Find("ChildOn");
        GameObject* toff = t.Find("ChildOff"); GameObject* tdeep = t.Find("DeepOff");
        CHECK(tp && ton && toff && tdeep);
        if (tp && ton && toff && tdeep) {
            CHECK(!tp->active && ton->active && !toff->active && !tdeep->active);
            CHECK(!ton->ActiveInHierarchy());
        }
    }

    oked::SetObjectActive(parent, true);              // tick it again
    CHECK(parent->active);
    CHECK(on->active && on->ActiveInHierarchy());
    CHECK(!off->active);                               // the bug: this came back on
    CHECK(!off->ActiveInHierarchy());
    CHECK(!deep->active && !deep->ActiveInHierarchy());
    std::cout << "  154: after off/on  ChildOn=" << bool(on->active) << " ChildOff=" << bool(off->active)
              << " DeepOff=" << bool(deep->active) << " (want 1 0 0)\n";

    // Switching a child itself still works.
    oked::SetObjectActive(off, true);
    CHECK(off->active && off->ActiveInHierarchy());
}

// A one-shot effect: a burst of 10 at the start of a 0.5 s cycle, no steady emission, short-lived particles.
static void OneShot(ParticleSystem& ps) {
    ps.emissionRate = 0.0f; ps.maxParticles = 50;
    ps.duration = 0.5f; ps.loop = false;
    ps.burstCount = 10; ps.burstTime = 0.0f;
    ps.startLifetime = 0.2f;
    ps.playing = true;
}
static void Run(ParticleSystem& ps, int frames) { for (int i = 0; i < frames; ++i) ps.Update(1.0f / 60.0f); }

static void ParticlesOnReplays() {
    Scene s("g154_fx");
    GameObject* go = Make(s, "Boom");
    auto* ps = go->AddComponent<ParticleSystem>();
    OneShot(*ps);

    ScriptHost host; host.gameObject = go; host.transform = go->transform;
    OkayScriptVM vm;
    vm.Bind(&host);
    std::string err;
    CHECK(vm.Load("fx_on() { particles_on() }\nfx_off() { particles_on(false) }\n", &err));
    if (!err.empty()) std::cerr << "  load error: " << err << "\n";

    Run(*ps, 1);
    const int first = ps->AliveCount();
    CHECK(first == 10);                                // the burst fired once
    Run(*ps, 60);                                      // 1 s: the cycle and every particle are over
    CHECK(ps->AliveCount() == 0);

    // The same finished effect, turned on with Play() from C++ (what particles_on() should match).
    Scene s2("g154_fx2");
    auto* twin = Make(s2, "Twin")->AddComponent<ParticleSystem>();
    OneShot(*twin);
    Run(*twin, 61);
    twin->Play();
    Run(*twin, 1);

    vm.CallEvent("fx_on");                             // particles_on(): play it again
    CHECK(ps->playing);
    Run(*ps, 1);
    const int replay = ps->AliveCount();
    CHECK(replay == twin->AliveCount());               // particles_on() does what Play() does
    CHECK(replay == 10);                               // and the one-shot really plays again (needs Play() from PR #1093)

    vm.CallEvent("fx_off");                            // particles_on(false): Stop()
    CHECK(!ps->playing);
    Run(*ps, 60);
    CHECK(ps->AliveCount() == 0);
    vm.CallEvent("fx_on");                             // a stopped effect starts a new cycle
    Run(*ps, 1);
    const int again = ps->AliveCount();
    CHECK(again == 10);
    std::cout << "  particles_on: first=" << first << " replay=" << replay << " twin(Play)=" << twin->AliveCount()
              << " after stop+on=" << again << " (want 10 10 10 10)\n";
}

int main() {
    RUN_SUITE("grok_154_particles_on");
    ActiveFlagsKept();
    ParticlesOnReplays();
    TEST_MAIN_RESULT();
}
