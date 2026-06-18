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
    ps->startVelocity = {1.0f, 5.0f};
    ps->velocityRandom = 2.0f;
    ps->gravity = {0.0f, -9.8f};
    ps->fadeOverLife = false;
    ps->seed = 99887766ull;

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
    CHECK_NEAR(r->velocityRandom, 2.0f, 0.001f);
    CHECK_NEAR(r->gravity.y, -9.8f, 0.001f);
    CHECK(!r->fadeOverLife);
    CHECK(r->seed == 99887766ull);

    TEST_MAIN_RESULT();
}
