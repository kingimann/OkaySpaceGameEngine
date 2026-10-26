#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/PlayerInput.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/FirstPersonController.hpp"

#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Net plan 5c: with authoritative movement the client predicts its own player
// (it moves at once) and reconciles with the position the host acks for each
// input. Tested through a proxy that delays every packet both ways, so the
// round trip is 100-150 ms of game time.
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

// A UDP proxy: the client talks to it, it talks to the host from one socket per
// client, and every datagram waits `oneWay` seconds of game time each way.
struct DelayProxy {
    struct Held { double at; std::vector<std::uint8_t> bytes; net::Endpoint to; net::UdpSocket* via; };
    net::UdpSocket front;
    net::Endpoint host;
    double oneWay, now = 0.0;
    std::map<std::uint64_t, std::unique_ptr<net::UdpSocket>> up;   // client endpoint -> upstream socket
    std::map<std::uint64_t, net::Endpoint> back;
    std::deque<Held> q;
    DelayProxy(std::uint16_t hostPort, double delay) : oneWay(delay) {
        host = net::Endpoint{0x7F000001u, hostPort};
        front.Open(); front.Bind(0);
    }
    std::uint16_t Port() const { return front.LocalPort(); }
    void Pump(double dt) {
        now += dt;
        std::uint8_t buf[4096]; net::Endpoint from; int n;
        while ((n = front.RecvFrom(buf, sizeof buf, from)) > 0) {
            const std::uint64_t key = (std::uint64_t(from.address) << 16) | from.port;
            auto& u = up[key];
            if (!u) { u = std::make_unique<net::UdpSocket>(); u->Open(); u->Bind(0); back[key] = from; }
            q.push_back({now + oneWay, std::vector<std::uint8_t>(buf, buf + n), host, u.get()});
        }
        for (auto& [key, u] : up)
            while ((n = u->RecvFrom(buf, sizeof buf, from)) > 0)
                q.push_back({now + oneWay, std::vector<std::uint8_t>(buf, buf + n), back[key], &front});
        while (!q.empty() && q.front().at <= now + 1e-9) {
            q.front().via->SendTo(q.front().to, q.front().bytes.data(), q.front().bytes.size());
            q.pop_front();
        }
    }
};

struct PredGame {
    Scene hs{"Host"}, cs{"Client"};
    NetworkManager *host = nullptr, *cli = nullptr;
    std::unique_ptr<DelayProxy> proxy;
    GameObject* pawn = nullptr;
    std::string id;
    PlayerInput in;
    const float dt = 1.0f / 60.0f;

    bool Open(double oneWay, const std::string& prefab) {
        host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        host->netTickRate = 30.0f;
        host->authoritativeMovement = true;
        host->spawnablePrefabs = {prefab};
        host->maxMessagesPerSecond = 0;
        host->interpolationRate = 0.0f;
        if (!host->StartServer(0)) return false;
        proxy = std::make_unique<DelayProxy>(host->ServerPort(), oneWay);
        cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli->interpolationRate = 0.0f;
        if (!cli->StartClient("127.0.0.1", proxy->Port())) return false;
        hs.Start(); cs.Start();
        for (int i = 0; i < 600 && !cli->Joined(); ++i) Frame();
        if (!cli->Joined()) return false;
        pawn = cli->SpawnOwned(prefab, Vec3(0.0f, 0.0f, 0.0f));
        if (!pawn) return false;
        id = pawn->GetComponent<NetworkSync>()->netId;
        cli->SetPawn(pawn);
        cli->SetInputSource([this] { return in; });
        for (int i = 0; i < 60; ++i) Frame();
        return true;
    }
    void Frame() { hs.Update(dt); proxy->Pump(dt); cs.Update(dt); Nap(); }
    GameObject* HostPawn() {
        for (const auto& g : hs.Objects())
            if (auto* ns = g->GetComponent<NetworkSync>(); ns && ns->netId == id) return g.get();
        return nullptr;
    }
    Vec3 Server() { GameObject* g = HostPawn(); return g ? g->transform->localPosition : Vec3(-999.0f, -999.0f, -999.0f); }
    Vec3 Mine() const { return pawn->transform->localPosition; }
};

float Dist(const Vec3& a, const Vec3& b) {
    const Vec3 d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}
} // namespace

int main() {
    RUN_SUITE("net_predict");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    const std::string prefab = "okay_predict_pawn.okayprefab";
    {
        Scene t("T");
        GameObject* p = t.CreateGameObject("Pawn");
        auto* cc = p->AddComponent<CharacterController3D>();
        cc->speed = 5.0f; cc->canJump = false; cc->fallResetY = 0.0f; cc->driveAnimation = false;
        CHECK(SceneSerializer::SaveObjectToFile(*p, prefab));
    }

    // One-way delays for 100, 133 and 167 ms round trips (the ping is
    // answered on the 30 Hz tick, so it moves in 33 ms steps; the last is past
    // the 100-150 ms the plan asks for, as a margin), and none at all (an ack
    // can then come before the client has finished running that input).
    for (double oneWay : {0.02, 0.05, 0.06, 0.0}) {
        PredGame g;
        CHECK(g.Open(oneWay, prefab));
        CHECK(g.cli->AuthoritativeMovement() && g.cli->Predicting());
        if (oneWay > 0.0) CHECK(g.cli->RttMs() >= 90.0f && g.cli->RttMs() <= 170.0f);

        // ---- Local movement is immediate: within a tick (the prediction runs the
        // input it sends, from the next tick), long before the host hears of it.
        const Vec3 start = g.Mine();
        const Vec3 serverStart = g.Server();
        g.in.move = Vec2(0.0f, 1.0f);
        int frames = 0;
        while (frames < 10 && g.Mine().z - start.z < 1e-4f) { g.Frame(); ++frames; }
        CHECK(frames <= 3);                                // 2 frames a tick at 60 fps / 30 Hz
        CHECK(std::fabs(g.Server().z - serverStart.z) < 1e-4f);   // the host hasn't heard yet
        CHECK(std::fabs((g.Mine().z - start.z) - 5.0f * g.dt) < 1e-4f);   // walking speed, no catch-up jump

        // ---- No rubber-banding while walking: never pulled back, no jumps ----------
        bool back = false, jump = false;
        float prev = g.Mine().z;
        for (int i = 0; i < 120; ++i) {                    // 2 s forward
            g.Frame();
            const float z = g.Mine().z;
            back = back || z < prev - 1e-4f;
            jump = jump || z - prev > 5.0f * g.dt * 1.5f;
            prev = z;
        }
        CHECK(!back && !jump);
        g.in.move = Vec2(0.0f, 0.0f);
        for (int i = 0; i < 40; ++i) { g.Frame(); back = back || g.Mine().z < prev - 1e-4f; prev = g.Mine().z; }
        CHECK(!back);
        CHECK(g.cli->PredictionSnaps() == 0);
        // Where the player ended up is the host's word, and agrees with the prediction.
        CHECK(Dist(g.Mine(), g.Server()) < 0.05f);
        CHECK(g.Server().z - serverStart.z > 9.0f && g.Server().z - serverStart.z < 10.6f);

        // ---- The host moves it a long way (a teleport): the client snaps ------------
        g.HostPawn()->transform->localPosition = g.Server() + Vec3(10.0f, 0.0f, 0.0f);
        g.in.move = Vec2(0.0f, 0.1f);                      // inputs flowing, so acks come
        for (int i = 0; i < 30; ++i) g.Frame();
        CHECK(g.cli->PredictionSnaps() >= 1);
        CHECK(Dist(g.Mine(), g.Server()) < 0.1f);

        // ---- A small difference is eased in over a few frames, not snapped ----------
        const std::uint32_t snaps = g.cli->PredictionSnaps(), corr = g.cli->PredictionCorrections();
        g.HostPawn()->transform->localPosition = g.HostPawn()->transform->localPosition + Vec3(0.5f, 0.0f, 0.0f);
        float maxStep = 0.0f;
        float lastX = g.Mine().x;
        for (int i = 0; i < 40; ++i) {
            g.Frame();
            maxStep = std::max(maxStep, std::fabs(g.Mine().x - lastX));
            lastX = g.Mine().x;
        }
        CHECK(g.cli->PredictionSnaps() == snaps && g.cli->PredictionCorrections() > corr);
        CHECK(maxStep > 0.0f && maxStep < 0.5f * 0.5f);   // spread over frames
        CHECK(Dist(g.Mine(), g.Server()) < 0.1f);

        // ---- A client that teleports itself is put back (and the host never moved) ---
        g.in.move = Vec2(0.0f, 0.0f);
        for (int i = 0; i < 20; ++i) g.Frame();
        const Vec3 at = g.Server();
        g.pawn->transform->localPosition = Vec3(100.0f, 0.0f, 100.0f);
        g.cli->Send("__xform", g.id + "|100 0 100 0 0 0 1");
        g.in.move = Vec2(0.0f, 0.01f);
        for (int i = 0; i < 30; ++i) g.Frame();
        CHECK(Dist(g.Server(), at) < 0.2f);
        CHECK(Dist(g.Mine(), g.Server()) < 0.1f);
        g.cli->Stop(); g.host->Stop();
    }

    // ---- First person: the look shows at once on the player's own copy ------------
    {
        const std::string fpPrefab = "okay_predict_fp.okayprefab";
        {
            Scene t("T");
            GameObject* p = t.CreateGameObject("Pawn");
            auto* fp = p->AddComponent<FirstPersonController>();
            fp->walkSpeed = 5.0f; fp->lockCursor = false; fp->fallResetY = 0.0f; fp->driveAnimation = false;
            GameObject* cam = t.CreateGameObject("Eye");
            cam->AddComponent<Camera>();
            cam->transform->SetParent(p->transform, false);
            CHECK(SceneSerializer::SaveObjectToFile(*p, fpPrefab));
        }
        PredGame g;
        CHECK(g.Open(0.05, fpPrefab));
        auto* fp = g.pawn->GetComponent<FirstPersonController>();
        CHECK(fp != nullptr && g.cli->Predicting());
        g.in.yaw = 90.0f; g.in.pitch = -30.0f;
        g.Frame();
        CHECK(fp && fp->yaw == 90.0f && fp->pitch == -30.0f);
        const Vec3 fwd = g.pawn->transform->localRotation * Vec3(0.0f, 0.0f, -1.0f);
        CHECK(std::fabs(fwd.x + 1.0f) < 1e-3f);            // the body turned this frame
        Transform* eye = g.pawn->transform->Children().empty() ? nullptr : g.pawn->transform->Children()[0];
        CHECK(eye && std::fabs(eye->localRotation.ToEuler().x + 30.0f) < 0.05f);   // and the camera tilted
        // Walking that way: prediction and host agree, nothing snapped.
        g.in.move = Vec2(0.0f, 1.0f);
        for (int i = 0; i < 60; ++i) g.Frame();
        g.in.move = Vec2(0.0f, 0.0f);
        for (int i = 0; i < 40; ++i) g.Frame();
        CHECK(g.cli->PredictionSnaps() == 0 && Dist(g.Mine(), g.Server()) < 0.05f);
        CHECK(g.Server().x < -4.0f);
        g.cli->Stop(); g.host->Stop();
        std::remove(fpPrefab.c_str());
    }

    std::remove(prefab.c_str());
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
