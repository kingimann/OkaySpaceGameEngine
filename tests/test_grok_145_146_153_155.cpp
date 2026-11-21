#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/Terrain.hpp"
#include <cmath>
#include <cstdint>

using namespace okay;

// Issue #888, Grok findings 145, 146, 153 and 155. Each block below failed on main before the fix.

namespace {
void Tick(Scene& s, int frames, float dt = 1.0f / 60.0f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

// A one-bone clip whose x goes from `x0` at time 0 to `x1` at time `len`.
ModelAnimator::Clip RampClip(const char* name, float x0, float x1, float len) {
    ModelAnimator::Clip c; c.name = name;
    ModelAnimator::NodeClip nc{"bone", AnimationClip{}};
    nc.clip.AddKey("position.x", 0.0f, x0);
    nc.clip.AddKey("position.x", len, x1);
    c.nodes.push_back(nc);
    return c;
}
}

int main() {
    RUN_SUITE("grok_145_146_153_155");

    // ---- 146: changing a Terrain's resolution keeps the sculpted heights and the painted layers ----
    {
        Terrain t;
        t.Resize(64);
        // A known shape: a smooth slope, height = 10 * (x / Dim), so a resample can be checked exactly.
        for (int z = 0; z < t.Dim(); ++z)
            for (int x = 0; x < t.Dim(); ++x) t.SetHeight(x, z, 10.0f * x / (float)t.resolution);
        TerrainLayer a; a.name = "Grass"; TerrainLayer b; b.name = "Rock";
        t.AddLayer(a); t.AddLayer(b);
        // Paint the right half fully with layer 1 (Rock).
        const std::size_t n0 = (std::size_t)t.Dim() * t.Dim();
        for (int z = 0; z < t.Dim(); ++z)
            for (int x = t.Dim() / 2 + 2; x < t.Dim(); ++x) {
                t.splat[0 * n0 + (std::size_t)z * t.Dim() + x] = 0;
                t.splat[1 * n0 + (std::size_t)z * t.Dim() + x] = 255;
            }
        float lo0 = 0, hi0 = 0; t.HeightRange(lo0, hi0);
        CHECK_NEAR(hi0, 10.0f, 1e-4f);

        t.Resize(70);   // one drag step in the Inspector
        CHECK(t.resolution == 70);
        CHECK(t.heights.size() == (std::size_t)71 * 71);
        float lo1 = 0, hi1 = 0; t.HeightRange(lo1, hi1);
        CHECK_NEAR(lo1, 0.0f, 1e-3f);
        CHECK_NEAR(hi1, 10.0f, 1e-3f);           // before the fix: 0 (everything flat)
        // The middle of the slope is still half way up.
        CHECK_NEAR(t.GetHeight(35, 10), 5.0f, 1e-3f);
        // The painted layers follow the new grid: left edge Grass, right edge Rock, and every vertex sums to 255.
        const std::size_t n1 = (std::size_t)t.Dim() * t.Dim();
        CHECK(t.splat.size() == n1 * 2);
        if (t.splat.size() == n1 * 2) {
            CHECK(t.splat[0 * n1 + 5 * t.Dim() + 0] == 255);
            CHECK(t.splat[1 * n1 + 5 * t.Dim() + 70] == 255);   // before the fix: 0 (paint reset to the base layer)
            bool sums = true;
            for (std::size_t v = 0; v < n1; ++v) if ((int)t.splat[v] + (int)t.splat[n1 + v] != 255) sums = false;
            CHECK(sums);
        }
        // Going down keeps it too.
        t.Resize(16);
        t.HeightRange(lo1, hi1);
        CHECK_NEAR(hi1, 10.0f, 1e-3f);
        CHECK_NEAR(t.GetHeight(8, 3), 5.0f, 1e-3f);
    }

    // ---- 153: a Light under an inactive parent gives no light ----
    {
        Scene scene;
        GameObject* room = scene.CreateGameObject("DisabledRoom");
        GameObject* lamp = scene.CreateGameObject("Lamp");
        lamp->transform->SetParent(room->transform, false);
        auto* l = lamp->AddComponent<Light>();
        l->type = Light::Type::Point; l->intensity = 4.0f; l->range = 14.0f;
        room->active = false;
        CHECK(lamp->active);
        CHECK(!lamp->ActiveInHierarchy());
        ApplySceneLight(scene);
        int points = 0;
        for (const LightSample& s : SceneLights::List()) if (s.type == (int)Light::Type::Point) ++points;
        CHECK(points == 0);                       // before the fix: 1
        // With no usable Light, the default sun is used, as in a scene without the lamp.
        CHECK(SceneLights::List().size() == 1);
        // Switching the room on brings the lamp back.
        room->active = true;
        ApplySceneLight(scene);
        points = 0;
        for (const LightSample& s : SceneLights::List()) if (s.type == (int)Light::Type::Point) ++points;
        CHECK(points == 1);
        SceneLights::Clear();
    }

    // ---- 155: a one-shot state played backwards plays from its end and fires On Clip End ----
    {
        Scene scene;
        GameObject* model = scene.CreateGameObject("Model");
        GameObject* bone = scene.CreateGameObject("bone");
        bone->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->autoPlay = false;
        ma->blendTime = 0.0f;
        ma->clips = {RampClip("idle", 0, 0, 1), RampClip("open", 0, 4, 1)};
        auto* sm = model->AddComponent<AnimStateMachine>();
        AnimStateMachine::State idle; idle.name = "Idle"; idle.clip = "idle"; idle.loop = true;
        AnimStateMachine::Transition go; go.to = "Open"; go.cond = AnimStateMachine::Cond::Trigger; go.param = "open"; go.blend = 0.0f;
        idle.transitions.push_back(go);
        AnimStateMachine::State open; open.name = "Open"; open.clip = "open"; open.loop = false; open.speedParam = "dir";
        AnimStateMachine::Transition back; back.to = "Idle"; back.cond = AnimStateMachine::Cond::OnClipEnd; back.blend = 0.0f;
        open.transitions.push_back(back);
        sm->states = {idle, open};
        scene.Start();
        sm->SetFloat("dir", -1.0f);
        Tick(scene, 2);
        CHECK(sm->Current() == "Idle");
        sm->SetTrigger("open");
        Tick(scene, 6);
        CHECK(sm->Current() == "Open");
        // Played backwards from the end: the bone starts near x = 4 and moves down (before the fix: 0 the whole time).
        const float early = bone->transform->localPosition.x;
        CHECK(early > 3.0f);
        Tick(scene, 20);
        CHECK(bone->transform->localPosition.x < early);
        // After the 1 s clip has run back to its start, On Clip End leaves the state (before the fix: never).
        Tick(scene, 60);
        CHECK(sm->Current() == "Idle");

        // Forward still works the same way.
        sm->SetFloat("dir", 1.0f);
        sm->SetTrigger("open");
        Tick(scene, 6);
        CHECK(sm->Current() == "Open");
        CHECK(bone->transform->localPosition.x < 1.0f);
        Tick(scene, 70);
        CHECK(sm->Current() == "Idle");
    }

    // ---- 145: Play() on a finished one-shot particle effect plays it again; a burst at the end of a loop fires ----
    {
        Scene scene;
        GameObject* fx = scene.CreateGameObject("Fx");
        auto* ps = fx->AddComponent<ParticleSystem>();
        ps->emissionRate = 0.0f; ps->burstCount = 50; ps->burstTime = 0.0f;
        ps->duration = 1.0f; ps->loop = false; ps->startLifetime = 0.5f; ps->startLifetimeRandom = 0.0f;
        scene.Start();
        Tick(scene, 3);
        CHECK(ps->AliveCount() == 50);
        Tick(scene, 120);
        CHECK(ps->AliveCount() == 0);
        ps->Play();
        Tick(scene, 2);
        CHECK(ps->AliveCount() == 50);            // before the fix: 0
        Tick(scene, 120);
        ps->Stop(); ps->Play();
        Tick(scene, 2);
        CHECK(ps->AliveCount() == 50);            // before the fix: 0

        // Play() on a running looping effect does not restart its cycle.
        GameObject* fx2 = scene.CreateGameObject("Fx2");
        auto* lp = fx2->AddComponent<ParticleSystem>();
        lp->emissionRate = 0.0f; lp->burstCount = 10; lp->burstTime = 0.5f; lp->duration = 1.0f; lp->loop = true;
        lp->startLifetime = 0.2f; lp->startLifetimeRandom = 0.0f;
        lp->Awake();
        Tick(scene, 12);              // 0.2 s into the cycle, no burst yet
        CHECK(lp->AliveCount() == 0);
        lp->Play();
        Tick(scene, 24);              // 0.6 s: the burst fired at 0.5 s
        CHECK(lp->AliveCount() == 10);

        // A burst at burstTime == duration on a looping effect fires once per cycle.
        GameObject* fx3 = scene.CreateGameObject("Fx3");
        auto* end = fx3->AddComponent<ParticleSystem>();
        end->emissionRate = 0.0f; end->burstCount = 20; end->burstTime = 1.0f; end->duration = 1.0f; end->loop = true;
        end->startLifetime = 0.25f; end->startLifetimeRandom = 0.0f;
        end->Awake();
        int maxAlive = 0, cycles = 0; bool was = false;
        for (int i = 0; i < 60 * 5; ++i) {
            scene.Update(1.0f / 60.0f);
            const int a = end->AliveCount();
            if (a > maxAlive) maxAlive = a;
            if (a > 0 && !was) ++cycles;
            was = a > 0;
        }
        CHECK(maxAlive == 20);                    // before the fix: 0
        CHECK(cycles >= 4 && cycles <= 5);
    }

    TEST_MAIN_RESULT();
}
