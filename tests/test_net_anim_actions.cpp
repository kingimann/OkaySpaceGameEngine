// A proxy plays the owner's death and aims its head where the owner looks.
// The action and the look ride on the end of the animation sync message, so a
// peer that only knows the older four fields still reads the gait.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include <chrono>
#include <cmath>
#include <thread>

using namespace okay;

int main() {
    RUN_SUITE("net_anim_actions");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // An older four-field message still parses: the extra reads fail and the
    // gait values that were present are kept. Checked on the same stream the
    // receiver uses.
    {
        std::stringstream ss("2 0 1.5 30");
        int a = 0, seq = 0; ss >> a >> seq;
        float sp = -1.0f, ang = 0.0f;
        CHECK(ss >> sp >> ang);
        int aseq = 0, aid = 0; float ly = 0.0f, lp = 0.0f;
        CHECK(!(ss >> aseq >> aid >> ly >> lp));
        CHECK(a == 2 && std::fabs(sp - 1.5f) < 1e-4f && std::fabs(ang - 30.0f) < 1e-4f);
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
    sChar->lookYaw = 35.0f;
    sChar->lookPitch = -12.0f;
    pump(40);

    AnimStateMachine* pSm = proxy->GetComponent<AnimStateMachine>();
    CHECK(pSm != nullptr);
    if (pSm) {
        const std::string& cur = pSm->Current();
        CHECK(cur == "Death" || cur == "DeadHold");
    }
    CHECK(std::fabs(cChar->lookYaw - 35.0f) < 0.5f);
    CHECK(std::fabs(cChar->lookPitch - (-12.0f)) < 0.5f);

    (void)sNs; (void)cNs;
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
