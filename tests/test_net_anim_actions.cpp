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
        // A cut tail keeps the gait and does not invent an action or a direction.
        AnimSyncView cut;
        CHECK(ParseAnimSync("2 0 1.5 30 4", cut));
        CHECK(cut.haveGait && !cut.haveAction && !cut.haveParam);
        AnimSyncView lookOnly;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4", lookOnly));
        CHECK(lookOnly.haveAction && !lookOnly.haveParam && lookOnly.actionId == 2);
        AnimSyncView full;
        CHECK(ParseAnimSync("2 0 1.5 30 7 2 -10 4 3", full));
        CHECK(full.haveAction && full.haveParam && full.actionId == 2);
        CHECK(std::fabs(full.actionParam - 3.0f) < 1e-4f);
        // Heading 90 degrees, 2 m/s: strafe right, no forward component.
        Scene axes("axes");
        GameObject* body = axes.CreateGameObject("Body");
        auto* axisSm = body->AddComponent<AnimStateMachine>();
        GameObject* model = axes.CreateGameObject("Model");
        model->transform->SetParent(body->transform, false);
        auto* modelSm = model->AddComponent<AnimStateMachine>();
        FeedProxyLocomotionAxes(body, 2.0f, 90.0f);
        CHECK(std::fabs(axisSm->GetFloat("moveX") - 2.0f) < 1e-3f);
        CHECK(std::fabs(axisSm->GetFloat("moveZ")) < 1e-3f);
        CHECK(std::fabs(modelSm->GetFloat("moveX") - 2.0f) < 1e-3f);
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

    // Hostile values: huge / non-finite look, unknown or negative action ids.
    // Raw characters (no state machine rewriting netActionId) stand in for a
    // peer that writes whatever it likes. "Raw" is host-owned, read by the
    // client; "Pilot" is client-owned, read by the host.
    GameObject* raw = ss.CreateGameObject("Raw");
    auto* rChar = raw->AddComponent<Character>(); rChar->Apply();
    auto* rNs = raw->AddComponent<NetworkSync>();
    rNs->netId = "raw"; rNs->authority = NetworkSync::Authority::Host;
    rNs->Start();
    GameObject* rProxy = cs.CreateGameObject("Raw");
    auto* rpChar = rProxy->AddComponent<Character>(); rpChar->Apply();
    auto* rpNs = rProxy->AddComponent<NetworkSync>();
    rpNs->netId = "raw"; rpNs->authority = NetworkSync::Authority::Host;
    rpNs->Start();

    GameObject* pilot = cs.CreateGameObject("Pilot");
    auto* pChar = pilot->AddComponent<Character>(); pChar->Apply();
    auto* pNs = pilot->AddComponent<NetworkSync>();
    pNs->netId = "pilot"; pNs->authority = NetworkSync::Authority::Mine;
    pNs->Start();
    GameObject* pHost = ss.CreateGameObject("Pilot");
    auto* phChar = pHost->AddComponent<Character>(); phChar->Apply();
    auto* phNs = pHost->AddComponent<NetworkSync>();
    phNs->netId = "pilot"; phNs->authority = NetworkSync::Authority::Manual; phNs->owned = false;
    phNs->Start();
    pump(15);

    auto hasAnim = [](GameObject* g) { return g->GetComponent<AnimStateMachine>() != nullptr; };
    const struct { float yaw, pitch; int id; } bad[] = {
        {3e38f, -3e38f, 99}, {NAN, 10.0f, 2}, {10.0f, INFINITY, 1}, {1e9f, 1e9f, -1}, {500.0f, -500.0f, 1000000}};
    for (const auto& b : bad) {
        rChar->lookYaw = b.yaw; rChar->lookPitch = b.pitch; rChar->netActionId = b.id; ++rChar->netActionSeq;
        pChar->lookYaw = b.yaw; pChar->lookPitch = b.pitch; pChar->netActionId = b.id; ++pChar->netActionSeq;
        pump(25);
        for (Character* c : {rpChar, phChar}) {
            CHECK(std::isfinite(c->lookYaw) && std::isfinite(c->lookPitch));
            CHECK(std::fabs(c->lookYaw) <= 90.0f && std::fabs(c->lookPitch) <= 90.0f);
        }
        // A bad id is dropped whole: neither side gains an animator from it.
        CHECK(!hasAnim(rProxy) && !hasAnim(pHost));
    }
    // A clamped (finite) value is still followed.
    rChar->lookYaw = 500.0f; rChar->lookPitch = -500.0f; rChar->netActionId = 0; ++rChar->netActionSeq;
    pChar->lookYaw = 500.0f; pChar->lookPitch = -500.0f; pChar->netActionId = 0; ++pChar->netActionSeq;
    pump(40);
    CHECK(std::fabs(rpChar->lookYaw - 90.0f) < 0.5f && std::fabs(rpChar->lookPitch + 90.0f) < 0.5f);
    CHECK(std::fabs(phChar->lookYaw - 90.0f) < 0.5f && std::fabs(phChar->lookPitch + 90.0f) < 0.5f);

    // A roll plays once. The owner keeps reporting "rolling" after the proxy's
    // roll is over; the proxy must not start it again every frame.
    rChar->lookYaw = 0.0f; rChar->lookPitch = 0.0f;
    rChar->netActionParam = 2.0f;   // roll left; an omitted param would stay forward
    rChar->netActionId = 2; ++rChar->netActionSeq;
    int rollStarts = 0; bool wasRoll = false; bool sawDir = false;
    for (int i = 0; i < 400; ++i) {
        pump(1);
        AnimStateMachine* m = rProxy->GetComponent<AnimStateMachine>();
        const bool inRoll = m && ProxyInAction(rpChar, 2);
        if (inRoll && !wasRoll) {
            ++rollStarts;
            if (m && std::fabs(m->GetFloat("dodgeDir") - 2.0f) < 0.1f) sawDir = true;
        }
        wasRoll = inRoll;
    }
    CHECK(rollStarts == 1);
    CHECK(sawDir);
    CHECK(!ProxyInAction(rpChar, 2));

    (void)sNs; (void)cNs; (void)rNs; (void)rpNs; (void)pNs; (void)phNs;
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
