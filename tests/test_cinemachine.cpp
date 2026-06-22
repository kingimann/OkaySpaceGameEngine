#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("cinemachine");

    // A brain drives the main camera to the live virtual camera's solved pose.
    {
        Scene s("CM"); s.physicsEnabled = false;

        GameObject* player = s.CreateGameObject("Player");
        player->transform->localPosition = {5, 0, 0};

        GameObject* camGO = s.CreateGameObject("MainCamera");
        camGO->AddComponent<Camera>();
        auto* brain = camGO->AddComponent<CinemachineBrain>();
        brain->blendTime = 0.0f;   // cut, for a deterministic check

        GameObject* vcGO = s.CreateGameObject("VCam");
        auto* vc = vcGO->AddComponent<VirtualCamera>();
        vc->follow = "Player";
        vc->followOffset = {0, 2, -10};
        vc->positionDamping = 0.0f;   // instant
        vc->rotationDamping = 0.0f;
        vc->priority = 10;

        s.Start();
        for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);

        Vec3 p = camGO->transform->Position();
        CHECK_NEAR(p.x, 5.0f, 0.05f);
        CHECK_NEAR(p.y, 2.0f, 0.05f);
        CHECK_NEAR(p.z, -10.0f, 0.05f);
    }

    // Highest priority wins; a higher-priority vcam takes over the shot.
    {
        Scene s("CM2"); s.physicsEnabled = false;
        GameObject* t = s.CreateGameObject("T");
        t->transform->localPosition = {0, 0, 0};

        GameObject* camGO = s.CreateGameObject("Cam");
        camGO->AddComponent<Camera>();
        camGO->AddComponent<CinemachineBrain>()->blendTime = 0.0f;

        GameObject* a = s.CreateGameObject("A");
        auto* va = a->AddComponent<VirtualCamera>();
        va->follow = "T"; va->followOffset = {0, 0, -5};
        va->positionDamping = 0.0f; va->priority = 10;

        GameObject* b = s.CreateGameObject("B");
        auto* vb = b->AddComponent<VirtualCamera>();
        vb->follow = "T"; vb->followOffset = {0, 0, 20};
        vb->positionDamping = 0.0f; vb->priority = 5;

        s.Start();
        s.Update(0.016f);
        CHECK_NEAR(camGO->transform->Position().z, -5.0f, 0.05f);  // A is live

        vb->priority = 99;   // B now outranks A
        s.Update(0.016f);
        CHECK_NEAR(camGO->transform->Position().z, 20.0f, 0.05f);  // B took over
    }

    // Serialization round-trips virtual camera + brain fields.
    {
        Scene s("CM3"); s.physicsEnabled = false;
        GameObject* camGO = s.CreateGameObject("Cam");
        camGO->AddComponent<Camera>();
        camGO->AddComponent<CinemachineBrain>()->blendTime = 2.5f;
        GameObject* vcGO = s.CreateGameObject("VCam");
        auto* vc = vcGO->AddComponent<VirtualCamera>();
        vc->priority = 42; vc->follow = "Hero"; vc->lookAt = "Boss";
        vc->followOffset = {1, 2, 3}; vc->positionDamping = 3.5f; vc->rotationDamping = 7.5f;
        vc->fieldOfView = 35.0f; vc->shakeAmplitude = 0.5f; vc->shakeFrequency = 4.0f;

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);

        auto* vc2 = s2.Find("VCam") ? s2.Find("VCam")->GetComponent<VirtualCamera>() : nullptr;
        CHECK(vc2 != nullptr);
        if (vc2) {
            CHECK(vc2->priority == 42);
            CHECK(vc2->follow == "Hero");
            CHECK(vc2->lookAt == "Boss");
            CHECK_NEAR(vc2->followOffset.y, 2.0f, 1e-4f);
            CHECK_NEAR(vc2->positionDamping, 3.5f, 1e-4f);
            CHECK_NEAR(vc2->fieldOfView, 35.0f, 1e-4f);
            CHECK_NEAR(vc2->shakeAmplitude, 0.5f, 1e-4f);
        }
        auto* cb2 = s2.Find("Cam") ? s2.Find("Cam")->GetComponent<CinemachineBrain>() : nullptr;
        CHECK(cb2 != nullptr);
        if (cb2) CHECK_NEAR(cb2->blendTime, 2.5f, 1e-4f);
    }

    TEST_MAIN_RESULT();
}
