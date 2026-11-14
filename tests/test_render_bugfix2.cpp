// Regression tests for renderer bugs found in audit round 2 (CPU side, headless).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/Culling.hpp"
#include <cmath>
#include <limits>

using namespace okay;

int main() {
    RUN_SUITE("gradient and curve survive a NaN time");
    {
        // Array of two so that reading stops[-1] / keys[-1] would hit the first one's data.
        ParticleGradient g[2];
        g[0].count = ParticleGradient::kMaxStops;
        for (int i = 0; i < ParticleGradient::kMaxStops; ++i) g[0].stops[i] = {0.1f * i, Color(0.77f, 0.77f, 0.77f, 0.77f)};
        g[1].Add(0.0f, Color(1, 0, 0, 1));
        g[1].Add(1.0f, Color(0, 0, 1, 1));
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Color c = g[1].Evaluate(nan);
        CHECK(c.r == 1.0f && c.g == 0.0f && c.b == 0.0f);   // the first stop, not memory before the array
        ParticleCurve k[2];
        k[0].count = ParticleCurve::kMaxKeys;
        for (int i = 0; i < ParticleCurve::kMaxKeys; ++i) k[0].keys[i] = {0.1f * i, 42.0f};
        k[1].SetLinear(2.0f, 4.0f);
        CHECK(k[1].Evaluate(nan) == 2.0f);
    }

    RUN_SUITE("particles fade out without compounding");
    {
        Scene s;
        GameObject* go = s.CreateGameObject("Fx");
        auto* ps = go->AddComponent<ParticleSystem>();
        ps->Awake();
        ps->emissionRate = 0.0f;
        ps->maxParticles = 4;
        ps->startLifetime = 1.0f; ps->startLifetimeRandom = 0.0f;
        ps->startColor = Color(1, 1, 1, 1);
        ps->colorOverLife = false;        // the defaults: no colour over life...
        ps->fadeOverLife = true;          // ...but fade out
        ps->Emit(1);
        for (int i = 0; i < 5; ++i) ps->Update(0.1f);   // life left 0.5 of 1.0 (the first step also emitted)
        float a = -1.0f;
        for (const auto& p : ps->Particles()) if (p.alive) a = p.color.a;
        CHECK(a > 0.0f);
        CHECK_NEAR(a, 0.5f, 0.02f);       // linear in remaining life; it used to be 0.9*0.8*0.7*0.6*0.5
    }

    TEST_MAIN_RESULT();
}
