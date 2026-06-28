#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("particles_serialize");

    Scene scene("FX");
    GameObject* go = scene.CreateGameObject("Emitter");
    auto* ps = go->AddComponent<ParticleSystem>();
    ps->emissionRate = 50.0f;
    ps->maxParticles = 128;
    ps->playing = false;
    ps->startLifetime = 2.5f;
    ps->startSize = 0.4f;
    ps->startColor = Color(0.2f, 0.4f, 0.8f, 0.9f);
    ps->startVelocity = {1.0f, 5.0f, -2.0f};
    ps->velocityRandom = 2.0f;
    ps->gravity = {0.0f, -9.8f, 0.5f};
    ps->fadeOverLife = false;
    ps->seed = 99887766ull;
    // Expanded (v2) fields.
    ps->endColor = Color(0.9f, 0.1f, 0.05f, 0.0f);
    ps->colorOverLife = true;
    ps->endSize = 0.05f;
    ps->sizeOverLife = true;
    ps->startLifetimeRandom = 0.5f;
    ps->startSizeRandom = 0.1f;
    ps->speedRandom = 0.25f;
    ps->shape = ParticleSystem::Shape::Cone;
    ps->shapeRadius = 1.75f;
    ps->shapeAngle = 33.0f;
    ps->boxSize = {2.0f, 0.5f, 3.0f};
    ps->gravityModifier = 1.5f;
    ps->damping = 0.8f;
    ps->duration = 4.0f;
    ps->loop = false;
    ps->burstCount = 25;
    ps->burstTime = 0.2f;
    // v4: sprite texture + billboard rotation.
    ps->texture = "smoke.png";
    ps->startRotation = 45.0f;
    ps->startRotationRandom = 15.0f;
    ps->rotationSpeed = 90.0f;
    ps->rotationSpeedRandom = 10.0f;

    std::string text = SceneSerializer::Serialize(scene);
    Scene loaded("L");
    std::string err;
    CHECK(SceneSerializer::Deserialize(loaded, text, &err));

    auto* r = loaded.Find("Emitter")->GetComponent<ParticleSystem>();
    CHECK(r != nullptr);
    CHECK_NEAR(r->emissionRate, 50.0f, 0.001f);
    CHECK(r->maxParticles == 128);
    CHECK(!r->playing);
    CHECK_NEAR(r->startLifetime, 2.5f, 0.001f);
    CHECK_NEAR(r->startSize, 0.4f, 0.001f);
    CHECK_NEAR(r->startColor.b, 0.8f, 0.01f);
    CHECK_NEAR(r->startVelocity.y, 5.0f, 0.001f);
    CHECK_NEAR(r->startVelocity.z, -2.0f, 0.001f);   // 3D z-component round-trips
    CHECK_NEAR(r->velocityRandom, 2.0f, 0.001f);
    CHECK_NEAR(r->gravity.y, -9.8f, 0.001f);
    CHECK_NEAR(r->gravity.z, 0.5f, 0.001f);
    CHECK(!r->fadeOverLife);
    CHECK(r->seed == 99887766ull);
    // Expanded (v2) fields round-trip.
    CHECK_NEAR(r->endColor.r, 0.9f, 0.01f);
    CHECK(r->colorOverLife);
    CHECK_NEAR(r->endSize, 0.05f, 0.001f);
    CHECK(r->sizeOverLife);
    CHECK_NEAR(r->startLifetimeRandom, 0.5f, 0.001f);
    CHECK_NEAR(r->startSizeRandom, 0.1f, 0.001f);
    CHECK_NEAR(r->speedRandom, 0.25f, 0.001f);
    CHECK(r->shape == ParticleSystem::Shape::Cone);
    CHECK_NEAR(r->shapeRadius, 1.75f, 0.001f);
    CHECK_NEAR(r->shapeAngle, 33.0f, 0.001f);
    CHECK_NEAR(r->boxSize.x, 2.0f, 0.001f);
    CHECK_NEAR(r->boxSize.z, 3.0f, 0.001f);
    CHECK_NEAR(r->gravityModifier, 1.5f, 0.001f);
    CHECK_NEAR(r->damping, 0.8f, 0.001f);
    CHECK_NEAR(r->duration, 4.0f, 0.001f);
    CHECK(!r->loop);
    CHECK(r->burstCount == 25);
    CHECK_NEAR(r->burstTime, 0.2f, 0.001f);
    // v4 fields round-trip.
    CHECK(r->texture == "smoke.png");
    CHECK_NEAR(r->startRotation, 45.0f, 0.001f);
    CHECK_NEAR(r->rotationSpeed, 90.0f, 0.001f);

    // Spin: a particle's billboard rotation advances by rotationSpeed * dt.
    Scene spin("SP");
    auto* ps5 = spin.CreateGameObject("E5")->AddComponent<ParticleSystem>();
    ps5->emissionRate = 0.0f; ps5->rotationSpeed = 100.0f; ps5->startLifetime = 5.0f;
    ps5->Awake();
    ps5->Emit(1);
    ps5->Update(0.5f);   // +50 degrees
    for (const auto& p : ps5->Particles())
        if (p.alive) CHECK_NEAR(p.rotation, 50.0f, 1.0f);

    // Behaviour: a cone emitter actually spawns and integrates particles.
    Scene sim("S");
    GameObject* e2 = sim.CreateGameObject("E2");
    auto* ps2 = e2->AddComponent<ParticleSystem>();
    ps2->shape = ParticleSystem::Shape::Cone;
    ps2->emissionRate = 100.0f;
    ps2->startLifetime = 1.0f;
    ps2->Awake();
    ps2->Update(0.1f);
    CHECK(ps2->AliveCount() > 0);
    // A burst spawns its exact count immediately.
    Scene sim2("S2");
    auto* ps3 = sim2.CreateGameObject("E3")->AddComponent<ParticleSystem>();
    ps3->emissionRate = 0.0f;
    ps3->Awake();
    ps3->Emit(10);
    CHECK(ps3->AliveCount() == 10);

    // 3D motion: a Sphere emitter flings particles in all directions, so their
    // positions spread along Z (not just the XY plane the old 2D system used).
    Scene sim3("S3");
    auto* ps4 = sim3.CreateGameObject("E4")->AddComponent<ParticleSystem>();
    ps4->shape = ParticleSystem::Shape::Sphere;
    ps4->shapeRadius = 1.0f;
    ps4->startVelocity = {0.0f, 0.0f, 0.0f};   // motion comes purely from the radial burst
    ps4->gravity = {0.0f, 0.0f, 0.0f};
    ps4->Awake();
    ps4->Emit(200);
    ps4->Update(0.2f);
    float minZ = 1e9f, maxZ = -1e9f;
    for (const auto& p : ps4->Particles())
        if (p.alive) { minZ = Mathf::Min(minZ, p.position.z); maxZ = Mathf::Max(maxZ, p.position.z); }
    CHECK((maxZ - minZ) > 0.2f);   // particles genuinely occupy 3D space

    TEST_MAIN_RESULT();
}
