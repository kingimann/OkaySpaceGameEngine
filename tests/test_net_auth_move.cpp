#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/PlayerInput.hpp"
#include "okay/Components/CharacterController3D.hpp"

#include <chrono>
#include <cmath>
#include <string>
#include <thread>

using namespace okay;

// Net plan 5b: server-authoritative movement. With it on, a client sends its
// inputs, the server runs the player's controller (Simulate) with them at the
// fixed tick, and the server alone says where the player is: a client that
// sends positions or teleports can't move it, and sending inputs faster only
// fills a queue. Off, a client moves its own player as before.
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

struct AuthGame {
    Scene hs{"Host"}, as{"A"}, bs{"B"};
    NetworkManager *host = nullptr, *a = nullptr, *b = nullptr;
    GameObject *pawnA = nullptr;
    std::string idA;
    PlayerInput inA;
    float bDt = 1.0f / 30.0f;

    bool Open(bool authoritative, const std::string& prefab) {
        host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        host->netTickRate = 30.0f;
        host->authoritativeMovement = authoritative;
        host->maxMessagesPerSecond = 0;
        host->interpolationRate = 0.0f;
        if (!host->StartServer(0)) return false;
        a = as.CreateGameObject("Net")->AddComponent<NetworkManager>();
        b = bs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        a->interpolationRate = b->interpolationRate = 0.0f;
        if (!a->StartClient("127.0.0.1", host->ServerPort()) || !b->StartClient("127.0.0.1", host->ServerPort())) return false;
        hs.Start(); as.Start(); bs.Start();
        for (int i = 0; i < 300 && !(a->Joined() && b->Joined()); ++i) Pump(1);
        if (!a->Joined() || !b->Joined()) return false;
        pawnA = a->SpawnOwned(prefab, Vec3(0.0f, 0.0f, 0.0f));
        if (!pawnA) return false;
        idA = pawnA->GetComponent<NetworkSync>()->netId;
        a->SetPawn(pawnA);
        a->SetInputSource([this] { return inA; });
        Pump(20);
        return true;
    }
    void Pump(int frames) {
        for (int i = 0; i < frames; ++i) {
            hs.Update(1.0f / 30.0f); as.Update(1.0f / 30.0f); bs.Update(bDt);
            Nap();
        }
    }
    Vec3 PosOf(Scene& s) const {
        for (const auto& g : s.Objects())
            if (auto* ns = g->GetComponent<NetworkSync>(); ns && ns->netId == idA && g->transform) return g->transform->localPosition;
        return Vec3(-999.0f, -999.0f, -999.0f);
    }
};
} // namespace

int main() {
    RUN_SUITE("net_auth_move");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // A player: CharacterController3D (5 u/s, no rigidbody, so it moves the
    // transform directly).
    const std::string prefab = "/tmp/okay_auth_pawn.okayprefab";
    {
        Scene t("T");
        GameObject* p = t.CreateGameObject("Pawn");
        auto* cc = p->AddComponent<CharacterController3D>();
        cc->speed = 5.0f; cc->canJump = false; cc->fallResetY = 0.0f; cc->driveAnimation = false;
        CHECK(SceneSerializer::SaveObjectToFile(*p, prefab));
    }

    // ---- The controller's split: Simulate moves the same as local play -----------------
    {
        Scene t("T");
        auto* cc = t.CreateGameObject("P")->AddComponent<CharacterController3D>();
        cc->speed = 5.0f; cc->canJump = false; cc->fallResetY = 0.0f; cc->driveAnimation = false;
        t.Start();
        CHECK(FindPlayerSim(cc->gameObject) == cc);
        PlayerInput in; in.move = Vec2(1.0f, 1.0f);       // diagonal: normalised, as before
        for (int i = 0; i < 10; ++i) cc->Simulate(in, 0.1f);
        const Vec3 p = cc->transform->localPosition;
        CHECK(std::fabs(std::sqrt(p.x * p.x + p.z * p.z) - 5.0f) < 0.01f);
        PlayerInput bad; bad.move = Vec2(30.0f, 0.0f); bad.pitch = 500.0f; bad.yaw = 725.0f;
        bad.Clamp();
        CHECK(std::fabs(bad.move.x - 1.0f) < 1e-5f && bad.pitch == 89.0f && std::fabs(bad.yaw - 5.0f) < 1e-3f);
    }

    // ---- On: inputs move the player, on the server -------------------------------------
    {
        AuthGame g;
        CHECK(g.Open(true, prefab));
        CHECK(g.a->AuthoritativeMovement() && g.host->AuthoritativeMovement());
        const Vec3 start = g.PosOf(g.hs);
        g.inA.move = Vec2(0.0f, 1.0f);                     // forward (+z) for 2 s
        g.Pump(60);
        g.inA.move = Vec2(0.0f, 0.0f);
        g.Pump(20);
        const Vec3 server = g.PosOf(g.hs);
        const float moved = server.z - start.z;
        CHECK(moved > 8.0f && moved < 10.6f);               // ~5 u/s x 2 s, by the server's own simulation
        CHECK(std::fabs(server.x) < 0.01f);
        // The player's own copy and everyone else's follow the server.
        CHECK(std::fabs(g.PosOf(g.as).z - server.z) < 0.5f);
        CHECK(std::fabs(g.PosOf(g.bs).z - server.z) < 0.5f);
        CHECK(g.a->LastAckedInput() > 50);

        // A modified client: teleports its copy and sends the position (__xform), and a
        // State far away. The server doesn't move; nor does anyone else's copy.
        g.pawnA->transform->localPosition = Vec3(100.0f, 0.0f, 100.0f);
        g.a->Send("__xform", g.idA + "|100 0 100 0 0 0 1");
        g.Pump(30);
        CHECK(std::fabs(g.PosOf(g.hs).z - server.z) < 0.01f && std::fabs(g.PosOf(g.hs).x) < 0.01f);
        CHECK(std::fabs(g.PosOf(g.bs).x) < 0.5f);
        CHECK(std::fabs(g.PosOf(g.as).x) < 0.5f);           // its own copy is put back by the server

        // Input values out of range are clamped on the server: a move vector of
        // length 20 moves no faster than walking.
        const Vec3 before = g.PosOf(g.hs);
        g.inA.move = Vec2(0.0f, 20.0f);
        g.Pump(30);
        g.inA.move = Vec2(0.0f, 0.0f);
        g.Pump(20);
        CHECK(g.PosOf(g.hs).z - before.z < 5.0f * 1.0f + 0.6f);
    }

    // ---- A speed hack only fills the queue: one input per tick --------------------------
    {
        AuthGame g;
        g.bDt = 1.0f / 30.0f;
        CHECK(g.Open(true, prefab));
        // A's game runs 3x fast (its tick, and so its inputs, come 3x as often).
        const Vec3 start = g.PosOf(g.hs);
        g.inA.move = Vec2(0.0f, 1.0f);
        for (int i = 0; i < 60; ++i) {                     // 2 s for the server ...
            g.hs.Update(1.0f / 30.0f); g.as.Update(3.0f / 30.0f); g.bs.Update(1.0f / 30.0f);
            Nap();
        }
        g.inA.move = Vec2(0.0f, 0.0f);
        g.Pump(20);
        const float moved = g.PosOf(g.hs).z - start.z;
        // ... so at most 2 s of walking (60 ticks), plus the queue it may have
        // banked (8 ticks) and the late-input cover (3): 71 ticks x 5/30 u. At
        // its own 3x rate it would have gone ~30.
        CHECK(moved < 71.0f * 5.0f / 30.0f + 0.05f);
        CHECK(moved > 6.0f);
        CHECK(g.host->InputsDropped() > 0);
    }

    // ---- Off: as before, the client moves its own player ------------------------------
    {
        AuthGame g;
        CHECK(g.Open(false, prefab));
        CHECK(!g.a->AuthoritativeMovement());
        g.pawnA->transform->localPosition = Vec3(7.0f, 0.0f, 3.0f);
        g.Pump(30);
        CHECK(std::fabs(g.PosOf(g.hs).x - 7.0f) < 0.01f && std::fabs(g.PosOf(g.bs).x - 7.0f) < 0.01f);
    }

    // ---- The setting is kept in scenes ------------------------------------------------
    {
        Scene a("A");
        auto* nm = a.CreateGameObject("Net")->AddComponent<NetworkManager>();
        nm->authoritativeMovement = true;
        const std::string path = "/tmp/okay_auth_move.okayscene";
        CHECK(SceneSerializer::SaveToFile(a, path));
        Scene b("B");
        CHECK(SceneSerializer::LoadFromFile(b, path));
        auto* back = b.Find("Net") ? b.Find("Net")->GetComponent<NetworkManager>() : nullptr;
        CHECK(back && back->authoritativeMovement && back->netTickRate == 0.0f);
        std::remove(path.c_str());
    }

    std::remove(prefab.c_str());
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
