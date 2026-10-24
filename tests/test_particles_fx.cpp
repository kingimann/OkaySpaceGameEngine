#include "test_framework.hpp"
#include <Okay.hpp>
#include <sstream>

using namespace okay;

// Gradients, curves, presets, the motion modules (noise / orbit / vortex /
// inherited velocity / local space) and their scene round-trip.
int main() {
    RUN_SUITE("particles_fx");

    // ---- Gradient evaluation ----
    {
        ParticleGradient g;
        CHECK(!g.Active());
        g.Add(0.0f, {1, 0, 0, 1});
        g.Add(1.0f, {0, 0, 1, 0});
        g.Add(0.5f, {0, 1, 0, 1});   // stored out of order on purpose
        CHECK(g.count == 3);
        Color c = g.Evaluate(0.25f);   // halfway red -> green
        CHECK_NEAR(c.r, 0.5f, 1e-4f); CHECK_NEAR(c.g, 0.5f, 1e-4f); CHECK_NEAR(c.b, 0.0f, 1e-4f);
        c = g.Evaluate(0.75f);         // halfway green -> blue, alpha 1 -> 0
        CHECK_NEAR(c.g, 0.5f, 1e-4f); CHECK_NEAR(c.b, 0.5f, 1e-4f); CHECK_NEAR(c.a, 0.5f, 1e-4f);
        CHECK_NEAR(g.Evaluate(0.5f).g, 1.0f, 1e-4f);   // exactly on a stop
        CHECK_NEAR(g.Evaluate(-1.0f).r, 1.0f, 1e-4f);  // clamps before the first stop
        CHECK_NEAR(g.Evaluate(2.0f).b, 1.0f, 1e-4f);   // ...and after the last
        g.Remove(1);   // drop the blue end
        CHECK(g.count == 2);
        CHECK_NEAR(g.Evaluate(1.0f).g, 1.0f, 1e-4f);
        // A single stop is a flat colour; the array never overflows.
        ParticleGradient one; one.Add(0.3f, {0.2f, 0.4f, 0.6f, 1});
        CHECK_NEAR(one.Evaluate(0.9f).b, 0.6f, 1e-4f);
        for (int i = 0; i < 20; ++i) one.Add(i / 20.0f, Color::White);
        CHECK(one.count == ParticleGradient::kMaxStops);
    }

    // ---- Curve evaluation ----
    {
        ParticleCurve c;
        CHECK_NEAR(c.Evaluate(0.5f), 1.0f, 1e-6f);   // unused curve = x1
        c.SetLinear(2.0f, 0.0f);
        CHECK_NEAR(c.Evaluate(0.0f), 2.0f, 1e-5f);
        CHECK_NEAR(c.Evaluate(0.25f), 1.5f, 1e-5f);
        CHECK_NEAR(c.Evaluate(1.0f), 0.0f, 1e-5f);
        c.Add(0.5f, 3.0f);   // peak in the middle
        CHECK_NEAR(c.Evaluate(0.5f), 3.0f, 1e-5f);
        CHECK_NEAR(c.Evaluate(0.75f), 1.5f, 1e-5f);
        CHECK_NEAR(c.MaxValue(), 3.0f, 1e-6f);
        // Smooth: same end points, eased in between (smoothstep at u=0.25 = 0.15625).
        c.smooth = true;
        CHECK_NEAR(c.Evaluate(0.5f), 3.0f, 1e-5f);
        CHECK_NEAR(c.Evaluate(0.125f), 2.0f + (3.0f - 2.0f) * 0.15625f, 1e-5f);
    }

    // ---- A default-constructed system behaves exactly as it did before ----
    // Golden values recorded from the pre-upgrade ParticleSystem (same seed,
    // same frames, the emitter moved mid-run): world space ignores the move and
    // none of the new modules touch the RNG or the integration.
    {
        Scene s("D");
        GameObject* go = s.CreateGameObject("E");
        go->transform->localPosition = {1, 2, 3};
        auto* ps = go->AddComponent<ParticleSystem>();
        CHECK(ps->simulationSpace == ParticleSystem::SimulationSpace::World);
        CHECK(!ps->colorGradient.Active() && !ps->sizeCurve.Active() && !ps->speedOverLife);
        CHECK(ps->noiseStrength == 0.0f && ps->orbitSpeed == 0.0f && ps->inheritVelocity == 0.0f);
        ps->Awake();
        for (int f = 0; f < 90; ++f) {
            if (f == 40) go->transform->localPosition = {4, 2, 3};
            ps->Update(1.0f / 30.0f);
        }
        CHECK(ps->AliveCount() == 30);
        int k = 0;
        for (const auto& p : ps->Particles()) {
            if (!p.alive) continue;
            if (k == 0) { CHECK_NEAR(p.position.x, 3.851500f, 1e-4f); CHECK_NEAR(p.position.y, 2.167596f, 1e-4f);
                          CHECK_NEAR(p.position.z, 4.133247f, 1e-4f); CHECK_NEAR(p.color.a, 0.0f, 1e-4f); }
            if (k == 1) { CHECK_NEAR(p.position.x, 5.671357f, 1e-4f); CHECK_NEAR(p.position.y, 6.232997f, 1e-4f);
                          CHECK_NEAR(p.position.z, 3.987682f, 1e-4f); }
            ++k;
        }
    }

    // ---- Gradient / size curve / speed curve drive the particles ----
    {
        Scene s("G");
        auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
        ps->emissionRate = 0.0f; ps->startLifetime = 1.0f; ps->startSize = 2.0f;
        ps->velocityRandom = 0.0f; ps->startVelocity = {1, 0, 0}; ps->gravity = {0, 0, 0};
        ps->fadeOverLife = false;
        ps->colorOverLife = true;
        ps->colorGradient.Add(0.0f, {1, 0, 0, 1}); ps->colorGradient.Add(0.5f, {0, 1, 0, 1}); ps->colorGradient.Add(1.0f, {0, 0, 1, 1});
        ps->sizeOverLife = true; ps->sizeCurve.SetLinear(1.0f, 0.0f);
        ps->Awake();
        ps->Emit(1);
        ps->Update(0.5f);   // halfway through life
        for (const auto& p : ps->Particles()) if (p.alive) {
            CHECK_NEAR(p.color.g, 1.0f, 1e-3f);   // middle stop
            CHECK_NEAR(p.size, 1.0f, 1e-3f);      // 2.0 * 0.5
        }
        // Speed curve x0 freezes motion; x1 elsewhere matches plain integration.
        Scene s2("SP");
        auto* sp = s2.CreateGameObject("E")->AddComponent<ParticleSystem>();
        sp->emissionRate = 0.0f; sp->startLifetime = 10.0f; sp->velocityRandom = 0.0f;
        sp->startVelocity = {2, 0, 0}; sp->gravity = {0, 0, 0};
        sp->speedOverLife = true; sp->speedCurve.SetLinear(0.0f, 0.0f);
        sp->Awake(); sp->Emit(1); sp->Update(0.5f);
        for (const auto& p : sp->Particles()) if (p.alive) CHECK_NEAR(p.position.x, 0.0f, 1e-5f);
        sp->speedCurve.SetLinear(0.5f, 0.5f);
        sp->Update(0.5f);   // moves half as far as 2 u/s would
        for (const auto& p : sp->Particles()) if (p.alive) CHECK_NEAR(p.position.x, 0.5f, 1e-4f);
    }

    // ---- Motion: noise, orbit, vortex, inherit velocity, local space ----
    {
        // Noise displaces otherwise-still particles, deterministically.
        auto runNoise = [](float strength) {
            Scene s("N");
            auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
            ps->emissionRate = 0.0f; ps->startLifetime = 10.0f; ps->velocityRandom = 0.0f;
            ps->startVelocity = {0, 0, 0}; ps->gravity = {0, 0, 0};
            ps->shape = ParticleSystem::Shape::Box; ps->boxSize = {4, 4, 4};
            ps->noiseStrength = strength; ps->noiseFrequency = 1.3f;
            ps->Awake(); ps->Emit(8);
            std::vector<Vec3> before;
            for (const auto& p : ps->Particles()) if (p.alive) before.push_back(p.position);
            for (int i = 0; i < 20; ++i) ps->Update(0.05f);
            float moved = 0.0f, lastX = 0.0f; size_t k = 0;
            for (const auto& p : ps->Particles()) if (p.alive) { moved += (p.position - before[k++]).Magnitude(); lastX = p.position.x; }
            return std::pair<float, float>(moved, lastX);
        };
        CHECK_NEAR(runNoise(0.0f).first, 0.0f, 1e-6f);
        auto a = runNoise(2.0f), b = runNoise(2.0f);
        CHECK(a.first > 0.1f);
        CHECK(a.second == b.second);   // same seed -> same result

        // Orbit keeps the radius and swings the particle around the Y axis.
        Scene so("O");
        auto* po = so.CreateGameObject("E")->AddComponent<ParticleSystem>();
        po->emissionRate = 0.0f; po->startLifetime = 10.0f; po->velocityRandom = 0.0f;
        po->startVelocity = {0, 0, 0}; po->gravity = {0, 0, 0}; po->orbitSpeed = 90.0f;
        po->Awake(); po->Emit(1);
        for (auto& pp : const_cast<std::vector<ParticleSystem::Particle>&>(po->Particles()))
            if (pp.alive) pp.position = {2, 0, 0};
        for (int i = 0; i < 10; ++i) po->Update(0.1f);   // 90 degrees total
        for (const auto& p : po->Particles()) if (p.alive) {
            CHECK_NEAR(std::sqrt(p.position.x * p.position.x + p.position.z * p.position.z), 2.0f, 1e-3f);
            CHECK_NEAR(p.position.x, 0.0f, 1e-3f);
            CHECK_NEAR(std::fabs(p.position.z), 2.0f, 1e-3f);
        }

        // Vortex pull accelerates a particle towards the axis.
        Scene sv("V");
        auto* pv = sv.CreateGameObject("E")->AddComponent<ParticleSystem>();
        pv->emissionRate = 0.0f; pv->startLifetime = 10.0f; pv->velocityRandom = 0.0f;
        pv->startVelocity = {0, 0, 0}; pv->gravity = {0, 0, 0}; pv->vortexPull = 4.0f; pv->vortexStrength = 1.0f;
        pv->Awake(); pv->Emit(1);
        for (auto& pp : const_cast<std::vector<ParticleSystem::Particle>&>(pv->Particles()))
            if (pp.alive) pp.position = {3, 0, 0};
        pv->Update(0.1f);
        for (const auto& p : pv->Particles()) if (p.alive) {
            CHECK(p.velocity.x < 0.0f);                 // pulled inward
            CHECK(std::fabs(p.velocity.z) > 0.01f);     // and swirled sideways
            CHECK_NEAR(p.velocity.y, 0.0f, 1e-6f);      // nothing along the axis
        }

        // Inherit velocity: a moving emitter hands its speed to new particles.
        Scene si("I");
        GameObject* gi = si.CreateGameObject("E");
        auto* pi = gi->AddComponent<ParticleSystem>();
        pi->emissionRate = 0.0f; pi->velocityRandom = 0.0f; pi->speedRandom = 0.0f;
        pi->startVelocity = {0, 0, 0}; pi->inheritVelocity = 0.5f;
        pi->Awake();
        pi->Update(0.1f);
        gi->transform->localPosition = {1, 0, 0};   // 10 units/second
        pi->Update(0.1f);
        pi->Emit(1);
        for (const auto& p : pi->Particles()) if (p.alive) CHECK_NEAR(p.velocity.x, 5.0f, 1e-3f);

        // Local space: live particles follow the emitter; world space leaves them.
        for (int local = 0; local < 2; ++local) {
            Scene sl("L");
            GameObject* gl = sl.CreateGameObject("E");
            auto* pl = gl->AddComponent<ParticleSystem>();
            pl->emissionRate = 0.0f; pl->startLifetime = 10.0f; pl->velocityRandom = 0.0f;
            pl->startVelocity = {0, 0, 0}; pl->gravity = {0, 0, 0};
            pl->simulationSpace = local ? ParticleSystem::SimulationSpace::Local : ParticleSystem::SimulationSpace::World;
            pl->Awake(); pl->Update(0.01f); pl->Emit(1);
            gl->transform->localPosition = {5, 0, 0};
            pl->Update(0.01f);
            for (const auto& p : pl->Particles()) if (p.alive) CHECK_NEAR(p.position.x, local ? 5.0f : 0.0f, 1e-4f);
            // Rotation is carried too (Local only): a particle at +X swings to -Z.
            if (local) {
                for (auto& pp : const_cast<std::vector<ParticleSystem::Particle>&>(pl->Particles()))
                    if (pp.alive) pp.position = {6, 0, 0};
                gl->transform->localRotation = Quat::Euler(0, 90, 0);
                pl->Update(0.01f);
                Vec3 expect = Vec3{5, 0, 0} + Quat::Euler(0, 90, 0) * Vec3{1, 0, 0};
                for (const auto& p : pl->Particles()) if (p.alive) {
                    CHECK_NEAR(p.position.x, expect.x, 1e-3f);
                    CHECK_NEAR(p.position.z, expect.z, 1e-3f);
                }
            }
        }
    }

    // ---- Presets ----
    {
        Scene s("P");
        auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
        ps->texture = "keep.png"; ps->seed = 42u;
        CHECK(std::string(ParticleSystem::PresetName(ParticleSystem::Preset::Explosion)) == "Explosion");
        CHECK(std::string(ParticleSystem::PresetName(ParticleSystem::Preset::Count)) == "?");
        for (int i = 0; i < (int)ParticleSystem::Preset::Count; ++i) {
            ps->collision = true; ps->orbitSpeed = 77.0f;   // leftovers a preset must reset
            ps->ApplyPreset((ParticleSystem::Preset)i);
            CHECK(ps->texture == "keep.png" && ps->seed == 42u);
            CHECK(!ps->collision);
            CHECK(ps->AliveCount() == 0);   // restarted
            CHECK(ps->emissionRate > 0.0f || ps->burstCount > 0);
            ps->Awake();
            for (int f = 0; f < 30; ++f) ps->Update(1.0f / 30.0f);
            CHECK(ps->AliveCount() > 0);   // every preset actually produces particles
            for (const auto& p : ps->Particles()) if (p.alive) {
                CHECK(std::isfinite(p.position.x) && std::isfinite(p.position.y) && std::isfinite(p.size));
            }
        }
        ps->ApplyPreset(ParticleSystem::Preset::Fire);
        CHECK(ps->colorGradient.count >= 3 && ps->sizeCurve.Active() && ps->noiseStrength > 0.0f);
        ps->ApplyPreset(ParticleSystem::Preset::Explosion);
        CHECK(ps->burstCount > 0 && ps->shape == ParticleSystem::Shape::Sphere);
        ps->ApplyPreset(ParticleSystem::Preset::Magic);
        CHECK(ps->orbitSpeed != 0.0f);
        CHECK(ps->orbitSpeed != 77.0f);
        ps->ApplyPreset(ParticleSystem::Preset::Rain);
        CHECK(ps->renderMode == ParticleSystem::RenderMode::Stretch && !ps->colorGradient.Active());
    }

    // ---- Serialization round-trip of every new field ----
    {
        Scene scene("FX");
        auto* ps = scene.CreateGameObject("Emitter")->AddComponent<ParticleSystem>();
        ps->simulationSpace = ParticleSystem::SimulationSpace::Local;
        ps->inheritVelocity = 0.75f;
        ps->noiseStrength = 1.25f; ps->noiseFrequency = 2.5f; ps->noiseScroll = 0.125f;
        ps->orbitSpeed = -45.0f; ps->orbitAxis = {0.0f, 0.0f, 1.0f};
        ps->vortexStrength = 3.5f; ps->vortexPull = -1.5f;
        ps->speedOverLife = true;
        ps->colorGradient.Add(0.0f, {1, 0.5f, 0.25f, 1}); ps->colorGradient.Add(0.4f, {0.1f, 0.2f, 0.3f, 0.4f});
        ps->colorGradient.Add(1.0f, {0, 0, 0, 0});
        ps->sizeCurve.Add(0.0f, 0.5f); ps->sizeCurve.Add(0.3f, 2.0f); ps->sizeCurve.smooth = true;
        ps->speedCurve.SetLinear(1.0f, 0.25f);
        ps->texture = "with space.png";   // the quoted token before the new line still parses

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("Emitter")->GetComponent<ParticleSystem>();
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->simulationSpace == ParticleSystem::SimulationSpace::Local);
            CHECK_NEAR(r->inheritVelocity, 0.75f, 1e-5f);
            CHECK_NEAR(r->noiseStrength, 1.25f, 1e-5f);
            CHECK_NEAR(r->noiseFrequency, 2.5f, 1e-5f);
            CHECK_NEAR(r->noiseScroll, 0.125f, 1e-5f);
            CHECK_NEAR(r->orbitSpeed, -45.0f, 1e-4f);
            CHECK_NEAR(r->orbitAxis.z, 1.0f, 1e-5f);
            CHECK_NEAR(r->vortexStrength, 3.5f, 1e-5f);
            CHECK_NEAR(r->vortexPull, -1.5f, 1e-5f);
            CHECK(r->speedOverLife);
            CHECK(r->colorGradient.count == 3);
            CHECK_NEAR(r->colorGradient.stops[1].time, 0.4f, 1e-5f);
            CHECK_NEAR(r->colorGradient.stops[1].color.a, 0.4f, 1e-5f);
            CHECK_NEAR(r->colorGradient.stops[0].color.g, 0.5f, 1e-5f);
            CHECK(r->sizeCurve.count == 2 && r->sizeCurve.smooth);
            CHECK_NEAR(r->sizeCurve.keys[1].value, 2.0f, 1e-5f);
            CHECK(r->speedCurve.count == 2 && !r->speedCurve.smooth);
            CHECK_NEAR(r->speedCurve.keys[1].value, 0.25f, 1e-5f);
            CHECK(r->texture == "with space.png");
        }
        // Re-serialising the loaded scene gives the same text (stable format).
        CHECK(SceneSerializer::Serialize(loaded) == text);
    }

    // ---- Old scenes (no "particlesfx" line) load with the old defaults ----
    {
        Scene scene("Old");
        auto* ps = scene.CreateGameObject("Emitter")->AddComponent<ParticleSystem>();
        ps->colorOverLife = true; ps->endColor = {1, 0, 0, 0};
        std::string text = SceneSerializer::Serialize(scene);
        // Strip the new line to mimic a scene saved before this upgrade.
        std::istringstream in(text);
        std::string line, old;
        bool sawFx = false;
        while (std::getline(in, line)) {
            if (line.find("particlesfx") != std::string::npos) { sawFx = true; continue; }
            old += line + "\n";
        }
        CHECK(sawFx);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, old, &err));
        auto* r = loaded.Find("Emitter")->GetComponent<ParticleSystem>();
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->colorOverLife && !r->colorGradient.Active());
            CHECK(!r->sizeCurve.Active() && !r->speedOverLife && !r->speedCurve.Active());
            CHECK(r->simulationSpace == ParticleSystem::SimulationSpace::World);
            CHECK(r->noiseStrength == 0.0f && r->orbitSpeed == 0.0f && r->vortexStrength == 0.0f);
            CHECK(r->vortexPull == 0.0f && r->inheritVelocity == 0.0f);
            CHECK_NEAR(r->endColor.r, 1.0f, 1e-5f);
        }
        // A truncated/hand-edited fx line keeps defaults for what's missing and
        // clamps absurd counts instead of overrunning the arrays.
        std::string bad = old;
        size_t at = bad.find("  particles ");
        CHECK(at != std::string::npos);
        size_t eol = bad.find('\n', at);
        // ver space inherit noise(str freq scroll) orbit axis(xyz) vortex pull speedLife | gradient count 999, one stop, then cut off
        bad.insert(eol + 1, "  particlesfx 1 1 0.5 2 1 0 0 1 0 0 0 0 0 999 0 0 0 0 1\n");
        Scene l2("L2");
        CHECK(SceneSerializer::Deserialize(l2, bad, &err));
        auto* r2 = l2.Find("Emitter")->GetComponent<ParticleSystem>();
        CHECK(r2 && r2->simulationSpace == ParticleSystem::SimulationSpace::Local);
        CHECK(r2 && r2->colorGradient.count <= ParticleGradient::kMaxStops);
        CHECK(r2 && r2->noiseStrength == 2.0f);
    }

    TEST_MAIN_RESULT();
}
