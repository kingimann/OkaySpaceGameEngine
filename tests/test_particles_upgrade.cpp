// The particle upgrade (docs/particles_upgrade.md): emission and shapes, motion modules, frame-rate independence,
// caps, collision, sub-emitters, the draw list (billboard modes, sheets, ribbons, culling, sorting), all presets,
// the scene round trip and the budgets. Everything is seeded: the same run gives the same numbers.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ParticleDraw.hpp"
#include "okay/Core/ImageWrite.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
#include "ParticlePresetList.hpp"

using namespace okay;

namespace {

// A plain system with nothing random and no wind, so the numbers are easy to predict.
void Quiet(ParticleSystem* ps) {
    ps->emissionRate = 0.0f; ps->velocityRandom = 0.0f; ps->speedRandom = 0.0f; ps->startVelocity = {0, 0, 0};
    ps->gravity = {0, 0, 0}; ps->windInfluence = 0.0f; ps->fadeOverLife = false; ps->startLifetime = 100.0f;
    ps->startLifetimeRandom = 0.0f; ps->startSizeRandom = 0.0f;
}

bool Finite(float v) { return std::isfinite(v); }

bool AllFinite(const ParticleSystem& ps) {
    for (int i = 0; i < ps.AliveCount(); ++i) {
        const auto p = ps.Get(i);
        if (!Finite(p.position.x) || !Finite(p.position.y) || !Finite(p.position.z) || !Finite(p.velocity.x) || !Finite(p.velocity.y) ||
            !Finite(p.velocity.z) || !Finite(p.size) || !Finite(p.rotation) || !Finite(p.color.r) || !Finite(p.color.a) || !Finite(p.life)) return false;
    }
    return true;
}

// Run `seconds` of game time at `fps`.
void Run(ParticleSystem* ps, float seconds, float fps) {
    const int frames = (int)std::lround(seconds * fps);
    for (int i = 0; i < frames; ++i) ps->Update(1.0f / fps);
}

std::vector<float> Snapshot(const ParticleSystem& ps) {
    std::vector<float> v;
    for (int i = 0; i < ps.AliveCount(); ++i) { const auto p = ps.Get(i); v.insert(v.end(), {p.position.x, p.position.y, p.position.z, p.life, p.size, p.color.a}); }
    return v;
}

} // namespace

int main() {
    RUN_SUITE("particles_upgrade");

    // ---- determinism: same seed, same particles; another seed, other particles ----
    {
        auto run = [](std::uint64_t seed) {
            Scene s("D"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
            ps->ApplyPreset(ParticleSystem::Preset::Fire); ps->prewarm = false; ps->seed = seed; ps->Awake();
            Run(ps, 2.0f, 60.0f); return Snapshot(*ps);
        };
        const auto a = run(5), b = run(5), c = run(6);
        CHECK(!a.empty() && a == b);
        CHECK(a != c);
    }

    // ---- rate emission is the same at 30 / 60 / 144 fps (smooth sub-frame spawning) ----
    {
        int counts[3]; float meanY[3]; int k = 0;
        for (float fps : {30.0f, 60.0f, 144.0f}) {
            Scene s("R"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
            Quiet(ps); ps->emissionRate = 100.0f; ps->maxParticles = 1000; ps->startVelocity = {0, 3, 0}; ps->Awake();
            Run(ps, 2.0f, fps);
            counts[k] = ps->AliveCount();
            float sum = 0; for (int i = 0; i < ps->AliveCount(); ++i) sum += ps->Get(i).position.y;
            meanY[k] = sum / std::max(1, ps->AliveCount());
            ++k;
        }
        std::printf("  emission at 30/60/144 fps: %d / %d / %d particles, mean height %.3f / %.3f / %.3f\n", counts[0], counts[1], counts[2], meanY[0], meanY[1], meanY[2]);
        for (int i = 0; i < 3; ++i) CHECK(std::abs(counts[i] - 200) <= 1);
        CHECK_NEAR(meanY[0], meanY[2], 0.03f); CHECK_NEAR(meanY[1], meanY[2], 0.03f);
    }

    // ---- motion is the same at 30 / 60 / 144 fps: gravity, drag, over-life curves ----
    {
        Vec3 end[3]; float size[3]; int k = 0;
        for (float fps : {30.0f, 60.0f, 144.0f}) {
            Scene s("M"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
            Quiet(ps); ps->startVelocity = {2, 6, 0}; ps->gravity = {0, -9.8f, 0}; ps->damping = 0.7f; ps->startLifetime = 3.0f;
            ps->sizeOverLife = true; ps->sizeCurve.SetLinear(1.0f, 0.2f); ps->startSize = 1.0f;
            ps->Awake(); ps->Emit(1); Run(ps, 1.5f, fps);
            const auto p = ps->Get(0); end[k] = p.position; size[k] = p.size; ++k;
        }
        std::printf("  motion at 30/60/144 fps: y = %.4f / %.4f / %.4f, x = %.4f / %.4f / %.4f\n", end[0].y, end[1].y, end[2].y, end[0].x, end[1].x, end[2].x);
        CHECK_NEAR(end[0].y, end[2].y, 0.03f); CHECK_NEAR(end[1].y, end[2].y, 0.01f);
        CHECK_NEAR(end[0].x, end[2].x, 0.02f);
        CHECK_NEAR(size[0], size[2], 1e-3f); CHECK_NEAR(size[1], size[2], 1e-3f);
    }

    // ---- exact: constant gravity (no drag) is exact at any step; drag follows exp(-k t) ----
    {
        Scene s("X"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(ps); ps->gravity = {0, -10, 0}; ps->startVelocity = {0, 0, 0}; ps->Awake(); ps->Emit(1);
        Run(ps, 1.0f, 30.0f);
        CHECK_NEAR(ps->Get(0).position.y, -5.0f, 1e-3f);   // y = -g t^2 / 2
        Scene s2("X2"); auto* p2 = s2.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p2); p2->startVelocity = {10, 0, 0}; p2->damping = 2.0f; p2->Awake(); p2->Emit(1);
        Run(p2, 1.0f, 60.0f);
        CHECK_NEAR(p2->Get(0).velocity.x, 10.0f * std::exp(-2.0f), 1e-3f);
    }

    // ---- shapes stay inside their bounds ----
    {
        auto make = [](ParticleSystem::Shape sh, Scene& s) {
            auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
            Quiet(ps); ps->shape = sh; ps->maxParticles = 2000; ps->Awake(); return ps;
        };
        { Scene s("a"); auto* ps = make(ParticleSystem::Shape::Sphere, s); ps->shapeRadius = 2.0f; ps->startVelocity = {0, 3, 0}; ps->Emit(1000);
          bool ok = true; for (int i = 0; i < 1000; ++i) { const auto p = ps->Get(i); if (p.position.Magnitude() > 2.0001f) ok = false; } CHECK(ok); }
        { Scene s("b"); auto* ps = make(ParticleSystem::Shape::Sphere, s); ps->shapeRadius = 2.0f; ps->shapeSurfaceOnly = true; ps->startVelocity = {0, 3, 0}; ps->Emit(500);
          bool ok = true; for (int i = 0; i < 500; ++i) if (std::abs(ps->Get(i).position.Magnitude() - 2.0f) > 1e-3f) ok = false; CHECK(ok); }
        { Scene s("c"); auto* ps = make(ParticleSystem::Shape::Hemisphere, s); ps->shapeRadius = 1.0f; ps->startVelocity = {0, 2, 0}; ps->Emit(500);
          bool ok = true; for (int i = 0; i < 500; ++i) { const auto p = ps->Get(i); if (p.position.y < -1e-5f || p.velocity.y < -1e-5f) ok = false; } CHECK(ok); }
        { Scene s("d"); auto* ps = make(ParticleSystem::Shape::Box, s); ps->boxSize = {2, 4, 6}; ps->Emit(500);
          bool ok = true; for (int i = 0; i < 500; ++i) { const auto p = ps->Get(i); if (std::abs(p.position.x) > 1.0001f || std::abs(p.position.y) > 2.0001f || std::abs(p.position.z) > 3.0001f) ok = false; } CHECK(ok); }
        { Scene s("e"); auto* ps = make(ParticleSystem::Shape::Box, s); ps->boxSize = {2, 4, 6}; ps->shapeSurfaceOnly = true; ps->Emit(500);
          bool ok = true; for (int i = 0; i < 500; ++i) { const auto p = ps->Get(i);
              if (std::abs(std::abs(p.position.x) - 1.0f) > 1e-4f && std::abs(std::abs(p.position.y) - 2.0f) > 1e-4f && std::abs(std::abs(p.position.z) - 3.0f) > 1e-4f) ok = false; } CHECK(ok); }
        { Scene s("f"); auto* ps = make(ParticleSystem::Shape::Cone, s); ps->shapeAngle = 20.0f; ps->startVelocity = {0, 5, 0}; ps->Emit(500);
          bool ok = true; for (int i = 0; i < 500; ++i) { const auto v = ps->Get(i).velocity; const float ang = std::acos(std::min(1.0f, v.y / v.Magnitude())) * 57.2958f;
              if (ang > 20.01f || std::abs(v.Magnitude() - 5.0f) > 1e-3f) ok = false; } CHECK(ok); }
        { Scene s("g"); auto* ps = make(ParticleSystem::Shape::Circle, s); ps->shapeRadius = 1.5f; ps->Emit(300);
          bool ok = true; for (int i = 0; i < 300; ++i) { const auto p = ps->Get(i); if (std::abs(std::hypot(p.position.x, p.position.z) - 1.5f) > 1e-3f || std::abs(p.position.y) > 1e-5f) ok = false; } CHECK(ok); }
        { Scene s("h"); auto* ps = make(ParticleSystem::Shape::Edge, s); ps->shapeRadius = 2.0f; ps->Emit(300);
          bool ok = true; for (int i = 0; i < 300; ++i) { const auto p = ps->Get(i); if (std::abs(p.position.x) > 2.0001f || std::abs(p.position.y) > 1e-5f) ok = false; } CHECK(ok); }
        { // Mesh surface: every particle sits on the skin of a 2x2x2 cube, and they reach all six faces.
          Scene s("i"); GameObject* go = s.CreateGameObject("E"); go->transform->localPosition = {10, 0, 0};
          auto* mr = go->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube();
          auto* ps = go->AddComponent<ParticleSystem>(); Quiet(ps); ps->shape = ParticleSystem::Shape::MeshSurface; ps->maxParticles = 3000; ps->startVelocity = {0, 1, 0}; ps->Awake(); ps->Emit(2000);
          const Mesh& m = mr->mesh; Vec3 lo = m.vertices[0], hi = m.vertices[0];
          for (const Vec3& v : m.vertices) { lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
          bool ok = true; int faces[6] = {0, 0, 0, 0, 0, 0};
          for (int i = 0; i < 2000; ++i) { const Vec3 p = ps->Get(i).position - Vec3{10, 0, 0};
              const float e = 1e-3f; bool on = false;
              if (std::abs(p.x - lo.x) < e) { on = true; ++faces[0]; } if (std::abs(p.x - hi.x) < e) { on = true; ++faces[1]; }
              if (std::abs(p.y - lo.y) < e) { on = true; ++faces[2]; } if (std::abs(p.y - hi.y) < e) { on = true; ++faces[3]; }
              if (std::abs(p.z - lo.z) < e) { on = true; ++faces[4]; } if (std::abs(p.z - hi.z) < e) { on = true; ++faces[5]; }
              if (!on) ok = false; }
          CHECK(ok); for (int f = 0; f < 6; ++f) CHECK(faces[f] > 100);
          std::printf("  mesh surface: 2000 particles on the cube skin, per face %d %d %d %d %d %d\n", faces[0], faces[1], faces[2], faces[3], faces[4], faces[5]); }
    }

    // ---- the emitter's own axes: a tilted object tilts the spray and the ring ----
    {
        Scene s("T"); GameObject* go = s.CreateGameObject("E"); go->transform->localRotation = Quat::Euler(0, 0, -90);   // +Y now points along +X
        auto* ps = go->AddComponent<ParticleSystem>(); Quiet(ps); ps->startVelocity = {0, 4, 0}; ps->Awake(); ps->Emit(1);
        CHECK_NEAR(ps->Get(0).velocity.x, 4.0f, 1e-3f); CHECK_NEAR(ps->Get(0).velocity.y, 0.0f, 1e-3f);
        Scene s2("T2"); GameObject* g2 = s2.CreateGameObject("E"); g2->transform->localRotation = Quat::Euler(90, 0, 0);
        auto* p2 = g2->AddComponent<ParticleSystem>(); Quiet(p2); p2->shape = ParticleSystem::Shape::Circle; p2->shapeRadius = 2.0f; p2->Awake(); p2->Emit(50);
        bool flatInZ = true; for (int i = 0; i < 50; ++i) if (std::abs(p2->Get(i).position.z) > 2.001f && std::abs(p2->Get(i).position.y) < 1e9f) flatInZ = false;
        float maxY = 0, maxZ = 0; for (int i = 0; i < 50; ++i) { maxY = std::max(maxY, std::abs(p2->Get(i).position.y)); maxZ = std::max(maxZ, std::abs(p2->Get(i).position.z)); }
        CHECK(flatInZ && maxY > 1.5f && maxZ < 1e-3f);   // the ring now stands in the XY plane
    }

    // ---- duration, loop, bursts, prewarm, distance emission ----
    {
        Scene s("L"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(ps); ps->maxParticles = 5000; ps->startLifetime = 100.0f; ps->emissionRate = 100.0f; ps->duration = 1.0f; ps->loop = false; ps->Awake();
        Run(ps, 3.0f, 60.0f);
        CHECK(std::abs(ps->AliveCount() - 100) <= 2);   // emits for one second, then stops
        Scene s2("L2"); auto* p2 = s2.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p2); p2->maxParticles = 5000; p2->burstCount = 10; p2->burstTime = 0.0f; p2->duration = 1.0f; p2->loop = true;
        p2->extraBurstCount = 2; p2->extraBursts[0] = {0.25f, 5}; p2->extraBursts[1] = {0.5f, 7}; p2->Awake();
        Run(p2, 2.6f, 60.0f);   // cycles start at 0, 1, 2 -> three times (10 + 5 + 7)
        CHECK(p2->AliveCount() == 3 * 22);
        Scene s3("P"); auto* p3 = s3.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p3); p3->maxParticles = 5000; p3->emissionRate = 50.0f; p3->startLifetime = 2.0f; p3->prewarm = true; p3->Awake();
        CHECK(std::abs(p3->AliveCount() - 100) <= 3);    // already at its steady count on the first frame
        Scene s4("DE"); GameObject* g4 = s4.CreateGameObject("E"); auto* p4 = g4->AddComponent<ParticleSystem>();
        Quiet(p4); p4->maxParticles = 5000; p4->emitPerDistance = 5.0f; p4->Awake();
        p4->Update(1.0f / 60.0f);
        for (int i = 1; i <= 60; ++i) { g4->transform->localPosition = {10.0f * i / 60.0f, 0, 0}; p4->Update(1.0f / 60.0f); }
        CHECK(std::abs(p4->AliveCount() - 50) <= 1);   // 10 units x 5 per unit
        float minx = 1e9f, maxx = -1e9f; for (int i = 0; i < p4->AliveCount(); ++i) { minx = std::min(minx, p4->Get(i).position.x); maxx = std::max(maxx, p4->Get(i).position.x); }
        CHECK(minx < 0.5f && maxx > 9.5f);   // spread along the path, not piled at one end
    }

    // ---- start colours, over-life modules ----
    {
        Scene s("C"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(ps); ps->maxParticles = 300; ps->startColor = {1, 0, 0, 1}; ps->startColor2 = {0, 0, 1, 1}; ps->randomStartColor = true; ps->colorOverLife = false; ps->Awake(); ps->Emit(200);
        bool varied = false, between = true;
        for (int i = 0; i < 200; ++i) { const auto c = ps->Get(i).color; if (c.r > 0.1f && c.r < 0.9f) varied = true; if (std::abs(c.r + c.b - 1.0f) > 1e-4f) between = false; }
        CHECK(varied && between);
        // Rotation over life: curve 0 stops the spin.
        Scene s2("R"); auto* p2 = s2.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p2); p2->rotationSpeed = 100.0f; p2->rotationOverLife = true; p2->rotationCurve.SetLinear(0.0f, 0.0f); p2->Awake(); p2->Emit(1); Run(p2, 1.0f, 60.0f);
        CHECK_NEAR(p2->Get(0).rotation, 0.0f, 1e-3f);
        // Radial velocity moves particles away from the emitter at the set speed; linear velocity drifts them.
        Scene s3("V"); auto* p3 = s3.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p3); p3->shape = ParticleSystem::Shape::Circle; p3->shapeRadius = 1.0f; p3->radialVelocity = 2.0f; p3->Awake(); p3->Emit(1); Run(p3, 1.0f, 60.0f);
        { const auto p = p3->Get(0); CHECK_NEAR(std::hypot(p.position.x, p.position.z), 3.0f, 0.05f); CHECK_NEAR(p.position.y, 0.0f, 1e-4f); }
        Scene s3b("V2"); auto* p3b = s3b.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p3b); p3b->linearVelocity = {0, 1, 0}; p3b->Awake(); p3b->Emit(1); Run(p3b, 1.0f, 60.0f);
        CHECK_NEAR(p3b->Get(0).position.y, 1.0f, 0.02f);
        // Force over life: constant push scaled by a curve.
        Scene s4("F"); auto* p4 = s4.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p4); p4->force = {4, 0, 0}; p4->forceCurve.SetLinear(0.0f, 1.0f); p4->startLifetime = 1.0f; p4->Awake(); p4->Emit(1);
        for (int i = 0; i < 59; ++i) p4->Update(1.0f / 60.0f);
        CHECK(p4->Get(0).velocity.x > 0.5f && p4->Get(0).velocity.x < 2.5f);   // ~ integral of 4*t over 1 s = 2 (cut at 59/60)
        // Gravity over life: a curve of 0 switches gravity off.
        Scene s5("G"); auto* p5 = s5.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p5); p5->gravity = {0, -10, 0}; p5->gravityCurve.SetLinear(0.0f, 0.0f); p5->Awake(); p5->Emit(1); Run(p5, 0.5f, 60.0f);
        CHECK(std::abs(p5->Get(0).position.y) < 0.5f * 10 * 0.25f * 0.5f);   // far less than free fall
    }

    // ---- noise: deterministic, finite, octaves change it ----
    {
        auto run = [](int oct) {
            Scene s("N"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
            Quiet(ps); ps->shape = ParticleSystem::Shape::Box; ps->boxSize = {4, 4, 4}; ps->noiseStrength = 3.0f; ps->noiseOctaves = oct; ps->Awake(); ps->Emit(50);
            Run(ps, 1.0f, 60.0f); CHECK(AllFinite(*ps)); return Snapshot(*ps);
        };
        CHECK(run(1) == run(1)); CHECK(run(3) == run(3)); CHECK(run(1) != run(3));
    }

    // ---- caps: DropNew never exceeds the cap; ReplaceOldest stays full and keeps the newest ----
    {
        Scene s("K"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(ps); ps->maxParticles = 100; ps->emissionRate = 1000.0f; ps->Awake(); Run(ps, 1.0f, 60.0f);
        CHECK(ps->AliveCount() == 100);
        Scene s2("K2"); auto* p2 = s2.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p2); p2->maxParticles = 100; p2->emissionRate = 1000.0f; p2->capPolicy = ParticleSystem::CapPolicy::ReplaceOldest; p2->Awake(); Run(p2, 1.0f, 60.0f);
        CHECK(p2->AliveCount() == 100);
        float youngest = 0, oldest = 0; for (int i = 0; i < 100; ++i) { const float a = p2->Age01(i); youngest = std::max(youngest, 1.0f - a); oldest = std::max(oldest, a); }
        CHECK(youngest > 0.99f);      // the newest particles are in the pool
        Scene s3("K3"); auto* p3 = s3.CreateGameObject("E")->AddComponent<ParticleSystem>();
        p3->maxParticles = -5; p3->Awake(); p3->Update(0.1f); p3->Emit(10); CHECK(p3->AliveCount() == 0);
        p3->maxParticles = 1000000; CHECK(p3->SafeMax() == 100000);
    }

    // ---- no NaN, caps respected: every preset, odd frame times ----
    {
        std::printf("  %-16s %6s %6s\n", "preset", "peak", "cap");
        for (int pi = 0; pi < (int)ParticleSystem::Preset::Count; ++pi) {
            Scene s("P"); GameObject* go = s.CreateGameObject("E"); auto* ps = go->AddComponent<ParticleSystem>();
            ps->ApplyPreset((ParticleSystem::Preset)pi); ps->Awake();
            for (float dt : {1.0f / 144.0f, 1.0f / 30.0f, 0.0f, 0.5f, 3.0f, 1.0f / 60.0f}) { for (int f = 0; f < 120; ++f) { ps->Update(dt); go->transform->localPosition = {std::sin(f * 0.1f), 0, 0}; } }
            int peak = 0; for (int f = 0; f < 180; ++f) { ps->Update(1.0f / 60.0f); peak = std::max(peak, ps->AliveCount()); }   // bursts come and go: look at the peak
            CHECK(AllFinite(*ps)); CHECK(ps->AliveCount() <= ps->SafeMax()); CHECK(peak > 0);
            CHECK(std::string(ParticleSystem::PresetName((ParticleSystem::Preset)pi)) != "?");
            CHECK(std::string(ParticleSystem::PresetHint((ParticleSystem::Preset)pi)).size() > 10);
            std::printf("  %-16s %6d %6d\n", ParticleSystem::PresetName((ParticleSystem::Preset)pi), peak, ps->SafeMax());
        }
        CHECK((int)ParticleSystem::Preset::Count == 17);
    }

    // ---- collision: ground bounce, die on hit, scene colliders under a ray budget ----
    {
        Scene s("G"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(ps); ps->gravity = {0, -10, 0}; ps->collision = true; ps->collisionY = 0.0f; ps->bounce = 0.5f; ps->collisionFriction = 0.0f; ps->Awake(); ps->Emit(1);
        { auto p = ps->Get(0); p.position = {0, 2, 0}; ps->Set(0, p); }
        float minY = 9; for (int i = 0; i < 180; ++i) { ps->Update(1.0f / 60.0f); minY = std::min(minY, ps->Get(0).position.y); }
        CHECK(minY >= -1e-4f);
        Scene s2("G2"); auto* p2 = s2.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(p2); p2->gravity = {0, -10, 0}; p2->collision = true; p2->dieOnCollision = true; p2->Awake(); p2->Emit(1);
        { auto p = p2->Get(0); p.position = {0, 1, 0}; p2->Set(0, p); }
        Run(p2, 2.0f, 60.0f); CHECK(p2->AliveCount() == 0);

        Scene w("W"); GameObject* floor = w.CreateGameObject("Floor"); floor->transform->localPosition = {0, -0.5f, 0};
        auto* fb = floor->AddComponent<BoxCollider3D>(); fb->size = {40, 1, 40};
        GameObject* eg = w.CreateGameObject("E"); eg->transform->localPosition = {0, 5, 0};
        auto* pw = eg->AddComponent<ParticleSystem>();
        Quiet(pw); pw->gravity = {0, -10, 0}; pw->maxParticles = 400; pw->worldCollision = true; pw->bounce = 0.3f; pw->collisionFriction = 0.2f; pw->collisionRaysPerFrame = 16;
        pw->shape = ParticleSystem::Shape::Box; pw->boxSize = {10, 0, 10}; pw->Awake(); pw->Emit(300);
        int maxRays = 0; float minY2 = 9;
        for (int i = 0; i < 240; ++i) { pw->Update(1.0f / 60.0f); maxRays = std::max(maxRays, pw->LastRays());
            for (int k = 0; k < pw->AliveCount(); ++k) minY2 = std::min(minY2, pw->Get(k).position.y); }
        float endMin = 9; for (int k = 0; k < pw->AliveCount(); ++k) endMin = std::min(endMin, pw->Get(k).position.y);
        std::printf("  world collision: lowest y ever %.3f, lowest at the end %.3f (floor top 0.0), most rays in a frame = %d (budget 16)\n", minY2, endMin, maxRays);
        CHECK(maxRays <= 16 && maxRays > 0);
        CHECK(endMin > -1.0f);   // with a small budget a particle can sink for a moment before its turn comes; none stays below
        int resting = 0; for (int k = 0; k < pw->AliveCount(); ++k) if (std::abs(pw->Get(k).position.y) < 0.3f) ++resting;
        CHECK(resting > 100);   // most were caught and now sit near the floor
    }

    // ---- sub-emitters: birth, death, collision ----
    {
        Scene s("S"); GameObject* parent = s.CreateGameObject("Parent");
        GameObject* kid = s.CreateGameObject("Puff"); kid->transform->SetParent(parent->transform);
        auto* kp = kid->AddComponent<ParticleSystem>(); Quiet(kp); kp->maxParticles = 1000; kp->Awake();
        auto* pp = parent->AddComponent<ParticleSystem>(); Quiet(pp); pp->maxParticles = 100; pp->startLifetime = 0.5f;
        pp->subEmitterBirth = "Puff"; pp->subBirthCount = 1; pp->subEmitterDeath = "Puff"; pp->subDeathCount = 3; pp->Awake();
        pp->Emit(10);
        pp->Update(0.01f);                       // births flush to the child
        CHECK(kp->AliveCount() == 10);
        Run(pp, 0.6f, 60.0f);                    // all ten end: 3 each
        CHECK(pp->AliveCount() == 0);
        CHECK(kp->AliveCount() == 10 + 30);
        // A missing or self-named child does nothing and never crashes.
        pp->subEmitterBirth = "Nope"; pp->Emit(3); pp->Update(0.01f); CHECK(AllFinite(*pp));
        // collision sub-emitter
        Scene s2("S2"); GameObject* par2 = s2.CreateGameObject("P"); GameObject* kid2 = s2.CreateGameObject("Splash"); kid2->transform->SetParent(par2->transform);
        auto* k2 = kid2->AddComponent<ParticleSystem>(); Quiet(k2); k2->Awake();
        auto* p2 = par2->AddComponent<ParticleSystem>(); Quiet(p2); p2->gravity = {0, -10, 0}; p2->collision = true; p2->dieOnCollision = true;
        p2->subEmitterCollision = "Splash"; p2->subCollisionCount = 4; p2->Awake(); p2->Emit(5);
        for (int i = 0; i < 5; ++i) { auto p = p2->Get(i); p.position = {(float)i, 0.5f, 0}; p2->Set(i, p); }
        Run(p2, 1.0f, 60.0f); CHECK(k2->AliveCount() == 20);
    }

    // ---- LOD and culling feed ----
    {
        auto count = [](float dist) {
            Scene s("O"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
            Quiet(ps); ps->maxParticles = 5000; ps->emissionRate = 200.0f; ps->lodNear = 10.0f; ps->lodFar = 50.0f; ps->lodMinimum = 0.25f; ps->Awake();
            ps->SetViewDistance(dist); Run(ps, 1.0f, 60.0f); return ps->AliveCount(); };
        const int near_ = count(5.0f), mid = count(30.0f), far_ = count(500.0f);
        std::printf("  LOD: %d particles near, %d mid, %d far\n", near_, mid, far_);
        CHECK(near_ > mid && mid > far_); CHECK(std::abs(far_ - near_ / 4) <= 3);
        Scene s("PS"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
        Quiet(ps); ps->emissionRate = 50; ps->simulateOffscreen = false; ps->Awake(); ps->SetCulled(true); Run(ps, 1.0f, 60.0f);
        CHECK(ps->AliveCount() == 0);   // paused while the renderer says it is off screen
        ps->SetCulled(false); Run(ps, 1.0f, 60.0f); CHECK(ps->AliveCount() > 0);
    }

    // ---- parallel job path gives exactly the serial answer ----
    {
        auto run = [](int threshold) {
            Scene s("J"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
            ps->ApplyPreset(ParticleSystem::Preset::Smoke); ps->prewarm = false; ps->maxParticles = 6000; ps->emissionRate = 3000.0f; ps->parallelThreshold = threshold; ps->Awake();
            Run(ps, 1.5f, 60.0f); return Snapshot(*ps); };
        const auto serial = run(0), parallel = run(64);
        CHECK(serial.size() > 600 && serial == parallel);
    }

    // ---- the draw list ----
    {
        Scene s("V"); GameObject* go = s.CreateGameObject("E"); go->transform->localPosition = {0, 0, 0};
        auto* ps = go->AddComponent<ParticleSystem>(); Quiet(ps); ps->maxParticles = 500; ps->startSize = 0.4f; ps->shape = ParticleSystem::Shape::Sphere; ps->shapeRadius = 1.5f; ps->startVelocity = {0, 0.01f, 0}; ps->Awake(); ps->Emit(200);
        ParticleView view; view.w = 400; view.h = 300;
        const Vec3 eye{0, 2, 8};
        view.camPos = eye; view.vp = Mat4::Perspective(60.0f, 4.0f / 3.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, 0, 0}, {0, 1, 0});
        std::vector<ParticleDrawItem> items; ParticleDrawStats st;
        BuildParticleDraw(s, view, items, &st);
        CHECK(st.systems == 1 && st.culled == 0 && (int)items.size() == 200);
        bool sorted = true; for (size_t i = 1; i < items.size(); ++i) if (items[i - 1].key < items[i].key) sorted = false; CHECK(sorted);
        bool inside = true; for (const auto& it : items) if (it.sx < -50 || it.sx > 450 || it.sy < -50 || it.sy > 350) inside = false; CHECK(inside);
        // A nearer particle is drawn bigger than a farther one.
        { float nearR = 0, farR = 0, nearD = 1e9, farD = 0; for (const auto& it : items) { if (it.key < nearD) { nearD = it.key; nearR = it.rad; } if (it.key > farD) { farD = it.key; farR = it.rad; } } CHECK(nearR > farR); }
        // Culling: camera looking away from the system.
        ParticleView away = view; away.vp = Mat4::Perspective(60.0f, 4.0f / 3.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, {0, 2, 20}, {0, 1, 0});
        BuildParticleDraw(s, away, items, &st); CHECK(st.culled == 1 && items.empty() && ps->Culled());
        BuildParticleDraw(s, view, items, &st); CHECK(!ps->Culled());
        // Billboard modes change the shape on screen.
        auto axes = [&](ParticleSystem::RenderMode m) {
            ps->renderMode = m; ps->shapeRadius = 0.0f; BuildParticleDraw(s, view, items, &st);
            return std::pair<float, float>(items.empty() ? 0.f : std::hypot(items[0].axx, items[0].axy), items.empty() ? 0.f : std::hypot(items[0].ayx, items[0].ayy)); };
        ps->shape = ParticleSystem::Shape::Point; ps->Restart(); ps->Emit(1);
        { auto p = ps->Get(0); p.position = {0, 0, 0}; p.velocity = {3, 0, 0}; ps->Set(0, p); }
        const auto bb = axes(ParticleSystem::RenderMode::Billboard);
        CHECK_NEAR(bb.first, bb.second, bb.first * 0.02f);
        const auto st2 = axes(ParticleSystem::RenderMode::Stretch);
        CHECK(st2.first > bb.first * 1.2f);                     // longer along the velocity
        const auto fl = axes(ParticleSystem::RenderMode::Horizontal);
        CHECK(fl.second < fl.first * 0.9f && fl.first > 0.0f);   // seen from a low angle the flat quad is squashed
        axes(ParticleSystem::RenderMode::Mesh); CHECK(!items.empty() && items[0].kind == ParticleDrawItem::Poly);
        // Sheet animation: UVs stay inside a cell; frames cross-fade.
        ps->renderMode = ParticleSystem::RenderMode::Billboard; ps->texture = "sheet.png"; ps->sheetColumns = 4; ps->sheetRows = 4; ps->sheetCycles = 1.0f;
        ps->startLifetime = 2.0f; ps->Restart(); ps->Emit(1); Run(ps, 0.55f, 60.0f);
        BuildParticleDraw(s, view, items, &st);
        CHECK(items.size() == 2);   // two frames blended
        for (const auto& it : items) { CHECK(it.u1 - it.u0 > 0.249f && it.u1 - it.u0 < 0.251f && it.v1 - it.v0 > 0.249f && it.u0 >= 0 && it.u1 <= 1.0001f && it.v1 <= 1.0001f); }
        CHECK(items[0].color.a + items[1].color.a <= 1.0001f);
        // Ribbons.
        ps->texture.clear(); ps->sheetColumns = ps->sheetRows = 1; ps->trailLength = 6; ps->startVelocity = {2, 0, 0}; ps->Restart(); ps->Emit(1); Run(ps, 0.4f, 60.0f);
        BuildParticleDraw(s, view, items, &st); CHECK(st.ribbonPieces >= 4);
        // Age sorting puts the older one behind.
        ps->trailLength = 0; ps->sortMode = ParticleSystem::SortMode::OldestFirst; ps->Restart(); ps->startVelocity = {0, 0, 0}; ps->Emit(1); Run(ps, 0.5f, 60.0f); ps->Emit(1);
        BuildParticleDraw(s, view, items, &st); CHECK(items.size() == 2 && items[0].key >= items[1].key);
        // Raster smoke test: the soft puff lights pixels and stays in the picture.
        std::vector<std::uint8_t> img(400 * 300 * 4, 0);
        RasterizeParticleDraw(items, 400, 300, img.data());
        int lit = 0; for (size_t i = 0; i < img.size(); i += 4) if (img[i] || img[i + 1] || img[i + 2]) ++lit; CHECK(lit > 20);
    }

    // ---- scene round trip: new fields survive; old scenes load; defaults write nothing new ----
    {
        Scene a("A"); auto* ps = a.CreateGameObject("E")->AddComponent<ParticleSystem>();
        const std::string plain = SceneSerializer::Serialize(a);
        CHECK(plain.find("particlesx") == std::string::npos);   // a default system writes exactly what older builds wrote
        ps->ApplyPreset(ParticleSystem::Preset::Portal);
        ps->extraBurstCount = 2; ps->extraBursts[0] = {0.5f, 9}; ps->extraBursts[1] = {1.5f, 4};
        ps->emitPerDistance = 2.5f; ps->capPolicy = ParticleSystem::CapPolicy::ReplaceOldest; ps->shapeSurfaceOnly = true;
        ps->sheetColumns = 4; ps->sheetRows = 2; ps->sheetMode = ParticleSystem::SheetMode::Loop; ps->sheetCycles = 12.0f; ps->softDistance = 0.7f; ps->lit = true;
        ps->subEmitterDeath = "Puff \"1\""; ps->subDeathCount = 5; ps->worldCollision = true; ps->collisionRaysPerFrame = 20; ps->dieOnCollision = true;
        ps->rotationCurve.SetLinear(1.0f, 0.25f); ps->rotationCurve.smooth = true; ps->rotationOverLife = true; ps->forceCurve.Add(0.0f, 2.0f); ps->forceCurve.Add(1.0f, 0.0f);
        ps->linearVelocity = {1, 2, 3}; ps->radialVelocity = -0.5f; ps->force = {0, 0.5f, 0}; ps->noiseOctaves = 3; ps->lodNear = 12; ps->lodFar = 90; ps->lodMinimum = 0.1f;
        ps->windInfluence = 0.5f; ps->startColor2 = {0.1f, 0.2f, 0.3f, 1.0f}; ps->randomStartColor = true; ps->blendMode = ParticleSystem::BlendMode::Premultiplied; ps->sortMode = ParticleSystem::SortMode::YoungestFirst;
        ps->simulateOffscreen = false; ps->parallelThreshold = 99;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("B"); std::string err; CHECK(SceneSerializer::Deserialize(b, text, &err));
        auto* q = b.Find("E") ? b.Find("E")->GetComponent<ParticleSystem>() : nullptr;
        CHECK(q != nullptr);
        if (q) {
            CHECK(q->trailLength == ps->trailLength && q->blendMode == ParticleSystem::BlendMode::Premultiplied && q->sortMode == ParticleSystem::SortMode::YoungestFirst);
            CHECK(q->extraBurstCount == 2 && q->extraBursts[0].count == 9 && q->extraBursts[1].time == 1.5f);
            CHECK(q->emitPerDistance == 2.5f && q->capPolicy == ParticleSystem::CapPolicy::ReplaceOldest && q->shapeSurfaceOnly);
            CHECK(q->sheetColumns == 4 && q->sheetRows == 2 && q->sheetMode == ParticleSystem::SheetMode::Loop && q->sheetCycles == 12.0f && q->softDistance == 0.7f && q->lit);
            CHECK(q->subEmitterDeath == "Puff \"1\"" && q->subDeathCount == 5 && q->worldCollision && q->collisionRaysPerFrame == 20 && q->dieOnCollision);
            CHECK(q->rotationCurve.count == 2 && q->rotationCurve.smooth && q->rotationOverLife && q->forceCurve.count == 2 && q->forceCurve.keys[0].value == 2.0f);
            CHECK(q->linearVelocity.z == 3.0f && q->radialVelocity == -0.5f && q->force.y == 0.5f && q->noiseOctaves == 3);
            CHECK(q->lodNear == 12 && q->lodFar == 90 && q->lodMinimum == 0.1f && q->windInfluence == 0.5f);
            CHECK(q->randomStartColor && q->startColor2.g == 0.2f && !q->simulateOffscreen && q->parallelThreshold == 99);
            CHECK(q->prewarm == ps->prewarm && q->orbitSpeed == ps->orbitSpeed && q->colorGradient.count == ps->colorGradient.count);
            // And once more: saving the loaded copy gives the same text.
            CHECK(SceneSerializer::Serialize(b) == text);
        }
        // An old scene (only the original lines) loads with the new fields at their defaults.
        Scene c("C"); std::string e2; CHECK(SceneSerializer::Deserialize(c, plain, &e2));
        auto* o = c.Find("E") ? c.Find("E")->GetComponent<ParticleSystem>() : nullptr;
        CHECK(o && o->trailLength == 0 && o->blendMode == ParticleSystem::BlendMode::Alpha && o->sheetColumns == 1 && o->simulateOffscreen && o->parallelThreshold == 4096);
    }

    // ---- the editor's preset list agrees with the engine's ----
    {
        std::size_t n = 0; const auto* items = oked::particlepresets::Items(n);
        CHECK((int)n == (int)ParticleSystem::Preset::Count);
        for (std::size_t i = 0; i < n && i < (std::size_t)ParticleSystem::Preset::Count; ++i) {
            CHECK(std::string(items[i].name) == ParticleSystem::PresetName((ParticleSystem::Preset)i));
            CHECK(std::string(items[i].hint) == ParticleSystem::PresetHint((ParticleSystem::Preset)i));
        }
    }

    // ---- the shipped example scene (examples/particles): every preset on a pedestal with a label ----
    {
        auto build = [](Scene& sc) {
            std::size_t n = 0; const auto* items = oked::particlepresets::Items(n);
            GameObject* sun = sc.CreateGameObject("Directional Light"); sun->AddComponent<Light>();
            sun->transform->localRotation = Quat::Euler(50, -30, 0);
            GameObject* ground = sc.CreateGameObject("Ground"); ground->transform->localPosition = {0, -0.5f, 0}; ground->transform->localScale = {60, 1, 30};
            { auto* mr = ground->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(); mr->color = {0.25f, 0.3f, 0.27f, 1}; }
            GameObject* cam = sc.CreateGameObject("Camera"); cam->transform->localPosition = {0, 12, 15};
            cam->transform->localRotation = Quat::Euler(-32, 0, 0);   // the engine camera looks down -Z; a negative turn about X looks down
            { auto* c = cam->AddComponent<Camera>(); c->fieldOfView = 60.0f; c->nearClip = 0.2f; sc.mainCamera = c; }
            cam->AddComponent<FreeRoamController>();
            const int perRow = 6;
            for (std::size_t i = 0; i < n; ++i) {
                const int row = (int)i / perRow, col = (int)i % perRow;
                const float x = (col - (perRow - 1) * 0.5f) * 6.0f, z = -row * 7.0f;
                GameObject* ped = sc.CreateGameObject(std::string("Pedestal ") + items[i].name);
                ped->transform->localPosition = {x, 0.15f, z}; ped->transform->localScale = {2.2f, 0.3f, 2.2f};
                { auto* mr = ped->AddComponent<MeshRenderer>(); mr->mesh = Mesh::Cube(); mr->color = {0.55f, 0.55f, 0.6f, 1}; }
                GameObject* fx = sc.CreateGameObject(items[i].name);
                auto* ps = fx->AddComponent<ParticleSystem>();
                ps->ApplyPreset((ParticleSystem::Preset)i);
                const bool falls = ps->shape == ParticleSystem::Shape::Box && ps->boxSize.x > 5.0f;   // rain, snow, dust, leaves: a small patch above the pedestal
                if (falls) { ps->boxSize = {3.0f, ps->boxSize.y, 3.0f}; }
                const auto pr = (ParticleSystem::Preset)i;
                float y = 0.4f;
                if (pr == ParticleSystem::Preset::Rain || pr == ParticleSystem::Preset::Snow || pr == ParticleSystem::Preset::Leaves) y = 4.5f;
                if (pr == ParticleSystem::Preset::WaterfallMist) y = 3.0f;
                if (pr == ParticleSystem::Preset::Fireflies || pr == ParticleSystem::Preset::Dust) { y = 1.5f; ps->boxSize = {3.0f, 2.0f, 3.0f}; }
                if (pr == ParticleSystem::Preset::Portal) { y = 1.6f; fx->transform->localRotation = Quat::Euler(90, 0, 0); }   // stand the ring up
                if (pr == ParticleSystem::Preset::Fireflies) ps->emissionRate = 4.0f;
                if (pr == ParticleSystem::Preset::MuzzleFlash) { fx->transform->localRotation = Quat::Euler(-90, 0, 0); y = 1.0f; }
                fx->transform->localPosition = {x, y, z};
                if (falls) ps->collision = (pr == ParticleSystem::Preset::Rain || pr == ParticleSystem::Preset::Snow) ? false : ps->collision;
                GameObject* lab = sc.CreateGameObject(std::string("Label ") + items[i].name);
                lab->transform->localPosition = {x, 3.6f, z + 1.2f};
                auto* tr = lab->AddComponent<TextRenderer>(); tr->text = items[i].name; tr->screenSpace = true; tr->pixelSize = 3.0f; tr->align = 1; tr->anchor = UIAnchor::Center;
                tr->color = {1, 1, 1, 1}; tr->shadow = true;
                auto* w = lab->AddComponent<WorldSpaceUI>(); w->pixelsPerUnit = 100.0f; w->billboard = true;
            }
        };
        if (const char* out = std::getenv("OKAY_WRITE_PARTICLE_EXAMPLE")) {
            Scene sc("Particles"); build(sc);
            std::ofstream f(out, std::ios::binary); f << SceneSerializer::Serialize(sc);
            std::printf("  wrote the example scene to %s\n", out);
        }
#ifdef OKAY_EXAMPLES_DIR
        std::ifstream in(std::string(OKAY_EXAMPLES_DIR) + "/particles/particles.okayscene", std::ios::binary);
        CHECK(in.good());
        if (in.good()) {
            std::stringstream ss; ss << in.rdbuf();
            Scene sc("Loaded"); std::string err;
            CHECK(SceneSerializer::Deserialize(sc, ss.str(), &err));
            int systems = 0, labels = 0, withCam = 0;
            std::vector<ParticleSystem*> all;
            for (const auto& up : sc.Objects()) {
                if (auto* p = up->GetComponent<ParticleSystem>()) { ++systems; all.push_back(p); }
                if (up->GetComponent<TextRenderer>() && up->GetComponent<WorldSpaceUI>()) ++labels;
                if (up->GetComponent<Camera>()) ++withCam;
            }
            CHECK(systems == (int)ParticleSystem::Preset::Count && labels == systems && withCam == 1);
            for (ParticleSystem* p : all) { p->Awake(); int peak = 0; for (int f = 0; f < 180; ++f) { p->Update(1.0f / 60.0f); peak = std::max(peak, p->AliveCount()); } CHECK(AllFinite(*p) && peak > 0); }
            // The scene looks right: draw it from the camera headlessly (and keep the picture when asked).
            Camera* cam = nullptr; for (const auto& up : sc.Objects()) if (auto* c = up->GetComponent<Camera>()) cam = c;
            if (cam) {
                ParticleView view; view.w = 960; view.h = 540;
                const Vec3 eye = cam->transform->Position(), fwd = cam->transform->Rotation() * Vec3{0, 0, -1};
                view.camPos = eye; view.vp = Mat4::Perspective(60.0f, 960.0f / 540.0f, 0.2f, 200.0f) * Mat4::LookAt(eye, eye + fwd, Vec3{0, 1, 0});
                std::vector<ParticleDrawItem> items; ParticleDrawStats st;
                BuildParticleDraw(sc, view, items, &st);
                std::vector<std::uint8_t> img(960 * 540 * 4, 0);
                for (size_t i = 0; i < img.size(); i += 4) { img[i] = 24; img[i + 1] = 30; img[i + 2] = 44; img[i + 3] = 255; }
                RasterizeParticleDraw(items, 960, 540, img.data());
                std::printf("  example scene seen from its camera: %d systems drawn, %d shapes\n", st.systems, st.items);
                CHECK(st.systems >= 12 && st.items > 500);
                if (const char* dir = std::getenv("OKAY_PARTICLE_RENDER_DIR")) ImageWrite::WritePNG(std::string(dir) + "/example_scene.png", 960, 540, 4, img.data());
            }
        }
#endif
    }

    // ---- headless renders of every preset (when OKAY_PARTICLE_RENDER_DIR is set): the pictures in the docs ----
    if (const char* dir = std::getenv("OKAY_PARTICLE_RENDER_DIR")) {
        for (int pi = 0; pi < (int)ParticleSystem::Preset::Count; ++pi) {
            Scene s("R"); GameObject* go = s.CreateGameObject("E"); auto* ps = go->AddComponent<ParticleSystem>();
            ps->ApplyPreset((ParticleSystem::Preset)pi); ps->Awake();
            Run(ps, 2.2f, 60.0f);
            ParticleView view; view.w = 320; view.h = 240;
            const Vec3 eye{0, 2.2f, 7.0f}, tgt{0, 1.6f, 0};
            view.camPos = eye; view.vp = Mat4::Perspective(55.0f, 320.0f / 240.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, tgt, {0, 1, 0});
            std::vector<ParticleDrawItem> items; ParticleDrawStats st; BuildParticleDraw(s, view, items, &st);
            std::vector<std::uint8_t> img(320 * 240 * 4, 0);
            for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) { std::uint8_t* q = &img[((size_t)y * 320 + x) * 4]; const int g = 22 + y / 8; q[0] = (std::uint8_t)g; q[1] = (std::uint8_t)(g + 6); q[2] = (std::uint8_t)(g + 22); q[3] = 255; }
            RasterizeParticleDraw(items, 320, 240, img.data());
            std::string name = ParticleSystem::PresetName((ParticleSystem::Preset)pi); for (char& c : name) if (c == ' ') c = '_';
            ImageWrite::WritePNG(std::string(dir) + "/" + name + ".png", 320, 240, 4, img.data());
        }
    }

    // ---- budgets: time and memory, measured and printed ----
    {
        Scene s("B"); auto* ps = s.CreateGameObject("E")->AddComponent<ParticleSystem>();
        ps->ApplyPreset(ParticleSystem::Preset::Fire); ps->prewarm = false; ps->maxParticles = 20000; ps->emissionRate = 10000.0f; ps->startLifetime = 2.0f; ps->Awake();
        Run(ps, 2.5f, 60.0f);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 60; ++i) ps->Update(1.0f / 60.0f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 60.0;
        std::printf("  budget: %d live particles: %.3f ms per Update (%.0f particles per ms), pool memory %.2f MB (%.0f bytes per slot)\n",
                    ps->AliveCount(), ms, ps->AliveCount() / ms, ps->MemoryBytes() / 1048576.0, (double)ps->MemoryBytes() / 20000.0);
        CHECK(ps->AliveCount() > 10000);
        CHECK(ms < 250.0);                                     // a generous gate: a loaded CI machine must not fail it
        CHECK(ps->MemoryBytes() < 20000u * 130u);              // about 100 bytes a slot, no hidden growth
        ps->maxParticles = 200; Run(ps, 0.1f, 60.0f); CHECK(ps->AliveCount() <= 200);   // shrinking the cap shrinks the pool
    }

    TEST_MAIN_RESULT();
}
