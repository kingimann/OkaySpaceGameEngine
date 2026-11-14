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

    RUN_SUITE("culling bounds follow an in-place vertex edit");
    {
        Mesh m;
        m.vertices = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        Vec3 c; float r = -1.0f;
        MeshLocalSphere(m, c, r);
        CHECK(r < 1e-3f);
        m.vertices[1] = {100, 0, 0};      // not the first, middle or last vertex
        MeshLocalSphere(m, c, r);
        CHECK(r > 40.0f);
    }

    RUN_SUITE("a skin joint that is gone keeps its pose");
    {
        Scene s("skin");
        GameObject* meshGo = s.CreateGameObject("Mesh");
        auto* mr = meshGo->AddComponent<MeshRenderer>();
        auto* sm = meshGo->AddComponent<SkinnedMesh>();
        GameObject* joint = s.CreateGameObject("Joint");
        joint->transform->localPosition = {3, 0, 0};
        sm->bind.vertices = {Vec3{4, 0, 0}};
        sm->bind.triangles = {0, 0, 0};
        sm->joints = {joint->transform, nullptr};
        sm->jointIdx = {{1, 0, 0, 0}};   // bound to the missing joint only
        sm->jointWt = {{1, 0, 0, 0}};
        sm->inverseBind = {Mat4::Translate({-3, 0, 0}), Mat4::Translate({-3, 0, 0})};
        sm->Skin();
        // A missing bone leaves the vertex in its bind pose (it used to be dragged into bone space).
        CHECK_NEAR(mr->mesh.vertices[0].x, 4.0f, 1e-3f);
        // A bone that vanishes after moving holds the last pose.
        sm->jointIdx = {{0, 0, 0, 0}};
        sm->joints = {joint->transform, nullptr};
        joint->transform->localPosition = {3, 5, 0};
        sm->Skin();
        CHECK_NEAR(mr->mesh.vertices[0].y, 5.0f, 1e-3f);
        sm->joints[0] = nullptr;
        sm->Skin();
        CHECK_NEAR(mr->mesh.vertices[0].x, 4.0f, 1e-3f);
        CHECK_NEAR(mr->mesh.vertices[0].y, 5.0f, 1e-3f);
    }

    TEST_MAIN_RESULT();
}
