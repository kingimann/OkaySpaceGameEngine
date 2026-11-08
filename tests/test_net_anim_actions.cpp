// A proxy plays the owner's death and aims its head where the owner looks.
// The action and the look ride on the end of the animation sync message, so a
// peer that only knows the older four fields still reads the gait.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/NetworkSync.hpp"
#include <chrono>
#include <cmath>
#include <thread>

using namespace okay;

int main() {
    RUN_SUITE("net_anim_actions");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // The same parser the receiver uses. A four-field message keeps the gait
    // and does not invent an action. Eight fields are the action and the look
    // without a direction. Nine fields add the direction.
    {
        AnimSyncView oldv;
        CHECK(ParseAnimSync("2 0 1.5 30", oldv));
        CHECK(oldv.anim == 2 && oldv.haveGait && !oldv.haveAction);
        CHECK(std::fabs(oldv.speed - 1.5f) < 1e-4f && std::fabs(oldv.angle - 30.0f) < 1e-4f);
        AnimSyncView mid;
        CHECK(ParseAnimSync("2 0 1.5 30 4 1 35 -12", mid));
        CHECK(mid.haveAction && !mid.haveParam && mid.actionId == 1);
        CHECK(std::fabs(mid.lookYaw - 35.0f) < 1e-3f);
        AnimSyncView full;
        CHECK(ParseAnimSync("2 0 1.5 30 4 1 35 -12 1", full));
        CHECK(full.haveParam && std::fabs(full.actionParam - 1.0f) < 1e-4f);
    }

    Scene ss("Server");
    auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
    CHECK(srv->StartServer(0));
    GameObject* hero = ss.CreateGameObject("Hero");
    auto* sChar = hero->AddComponent<Character>();
    sChar->Apply();
    auto* sNs = hero->AddComponent<NetworkSync>();
    sNs->netId = "hero"; sNs->authority = NetworkSync::Authority::Host;
    AnimStateMachine* sSm = HumanoidLibrary::EnsureAnimator(hero);
    CHECK(sSm != nullptr);
    if (sSm) HumanoidActions::MergeMissing(*sSm, sChar, true);
    ss.Start();

    Scene cs("Client");
    auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
    CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
    GameObject* proxy = cs.CreateGameObject("Hero");
    auto* cChar = proxy->AddComponent<Character>();
    cChar->Apply();
    auto* cNs = proxy->AddComponent<NetworkSync>();
    cNs->netId = "hero"; cNs->authority = NetworkSync::Authority::Host;
    cs.Start();

    auto pump = [&](int n) {
        for (int i = 0; i < n; ++i) {
            ss.Update(0.02f); cs.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    for (int i = 0; i < 300 && !cli->Joined(); ++i) pump(1);
    CHECK(cli->Joined());
    pump(15);

    CHECK(HumanoidActions::PlayDeath(hero, nullptr));
    if (sSm) sSm->SetFloat("deathDir", 1.0f);
    sChar->lookYaw = 35.0f;
    sChar->lookPitch = -12.0f;
    pump(40);

    AnimStateMachine* pSm = proxy->GetComponent<AnimStateMachine>();
    CHECK(pSm != nullptr);
    if (pSm) {
        const std::string& cur = pSm->Current();
        CHECK(cur == "Death" || cur == "DeadHold");
        CHECK(std::fabs(pSm->GetFloat("deathDir") - 1.0f) < 0.5f);
    }
    CHECK(std::fabs(cChar->lookYaw - 35.0f) < 0.5f);
    CHECK(std::fabs(cChar->lookPitch - (-12.0f)) < 0.5f);

    // The proxy prefab looks at the local camera. That must not overwrite the
    // owner's look, or the head jitters between the two.
    GameObject* camGo = cs.CreateGameObject("Cam");
    auto* cam = camGo->AddComponent<Camera>();
    camGo->transform->SetPosition(Vec3{8.0f, 1.0f, 0.0f});
    cs.mainCamera = cam;
    cChar->lookAtCamera = true;
    pump(8);
    CHECK(std::fabs(cChar->lookYaw - 35.0f) < 0.5f);
    (void)cam;

    HumanoidActions::RequestRevive(hero);
    if (sSm) { sSm->SetBool("dead", false); sSm->GoTo("Idle", 0.0f); }
    pump(20);
    CHECK(HumanoidActions::RequestRoll(hero, 2));
    bool sawRoll = false;
    for (int i = 0; i < 40 && !sawRoll; ++i) {
        pump(1);
        if (pSm && (pSm->Current() == "Roll" || pSm->Current() == "Dodge") && pSm->StateTime() > 0.08f)
            sawRoll = true;
    }
    CHECK(sawRoll);
    if (pSm) CHECK(std::fabs(pSm->GetFloat("dodgeDir") - 2.0f) < 0.5f);
    if (pSm) pSm->GoTo("Idle", 0.0f);
    pump(8);
    if (pSm) {
        const std::string& cur = pSm->Current();
        CHECK(cur != "Roll" && cur != "Dodge");
    }

    HumanoidActions::SetSwimming(hero, true, 1.5f);
    pump(20);
    if (pSm) CHECK(std::fabs(pSm->GetFloat("swimSpeed") - 1.5f) < 0.25f);

    sChar->moveAngle = 90.0f;
    sChar->moveSpeed = 2.0f;
    for (int i = 0; i < 10; ++i) {
        proxy->transform->SetPosition(proxy->transform->Position() + Vec3{0.05f, 0.0f, 0.0f});
        pump(1);
    }
    if (pSm) CHECK(pSm->GetFloat("moveX") > 0.2f);

    (void)sNs; (void)cNs;
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
