#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/PlayerInput.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/CharacterController2D.hpp"
#include "okay/Components/TopDownController2D.hpp"

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <type_traits>

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
    GameObject* ObjOf(Scene& s) const {
        for (const auto& g : s.Objects())
            if (auto* ns = g->GetComponent<NetworkSync>(); ns && ns->netId == idA) return g.get();
        return nullptr;
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

    // ---- First- and third-person: Simulate reads only its input --------------------------
    // (Local play is ReadLocalInput + Simulate every frame; the old and new controllers
    // were run side by side over a scripted session and matched frame for frame.)
    auto splitChecks = [](auto* c) {
        c->lockCursor = false; c->fallResetY = 0.0f; c->driveAnimation = false;
        c->transform->localPosition = Vec3(0.0f, 0.0f, 0.0f);
        c->yaw = 0.0f; c->pitch = 0.0f;
        // The look adds up through ReadLocalInput alone (a client whose host moves
        // it reads its input every frame but doesn't run Simulate).
        Input::FeedMouse(Vec2(0.0f, 0.0f), 0); c->ReadLocalInput();
        PlayerInput look;
        for (int i = 1; i <= 5; ++i) { Input::FeedMouse(Vec2(10.0f * i, 0.0f), 0); look = c->ReadLocalInput(); }
        CHECK(std::fabs(std::fabs(look.yaw) - 50.0f * c->mouseSensitivity) < 1e-3f && look.yaw == c->yaw);
        c->yaw = 0.0f;
        // Keys held, but an empty command: nothing moves.
        Input::FeedKeys({'w', 'q', Input::KeyShift});
        c->Simulate(PlayerInput{}, 0.1f);
        CHECK(c->transform->localPosition.z == 0.0f && c->lean() == 0.0f);
        // The keys are what ReadLocalInput reports.
        Input::FeedKeys({});
        Input::FeedKeys({'w', 'q', Input::KeyShift, ' '});
        const PlayerInput r = c->ReadLocalInput();
        CHECK(r.move.y > 0.9f && r.Held(PlayerInput::LeanLeft) && r.Held(PlayerInput::Sprint));
        CHECK(r.Pressed(PlayerInput::Jump) && r.Pressed(PlayerInput::Sprint) && !r.Held(PlayerInput::Crouch));
        Input::FeedKeys({});
        // A command: walks where its yaw faces, and the look follows the command.
        PlayerInput in; in.move = Vec2(0.0f, 1.0f); in.yaw = 90.0f; in.pitch = 30.0f;
        in.held = PlayerInput::LeanRight;
        for (int i = 0; i < 10; ++i) c->Simulate(in, 0.1f);
        const Vec3 p = c->transform->localPosition;
        CHECK(std::fabs(std::fabs(p.x) - c->walkSpeed) < 0.05f && std::fabs(p.z) < 0.05f);
        CHECK(c->yaw == 90.0f && c->pitch == 30.0f && c->lean() > 0.5f);
        in.pitch = 500.0f; c->Simulate(in, 0.1f);
        CHECK(c->pitch == c->maxPitch);                    // the controller's own limits
        // Crouch is a press (toggle by default): held without a new press stays put.
        PlayerInput cr; cr.held = cr.pressed = PlayerInput::Crouch;
        c->Simulate(cr, 0.1f);
        CHECK(c->stance() == std::remove_reference_t<decltype(*c)>::Stance::Crouch);
        cr.pressed = 0; c->Simulate(cr, 0.1f);
        CHECK(c->stance() == std::remove_reference_t<decltype(*c)>::Stance::Crouch);
    };
    {
        Scene t("FP");
        auto* fp = t.CreateGameObject("P")->AddComponent<FirstPersonController>();
        t.Start();
        CHECK(FindPlayerSim(fp->gameObject) == fp);
        splitChecks(fp);
    }
    {
        Scene t("TP");
        auto* tp = t.CreateGameObject("P")->AddComponent<ThirdPersonController>();
        t.Start();
        CHECK(FindPlayerSim(tp->gameObject) == tp);
        splitChecks(tp);
    }

    // ---- The 2D controllers: the same split ------------------------------------------
    {
        Scene t("CC2");
        auto* c = t.CreateGameObject("P")->AddComponent<CharacterController2D>();
        c->speed = 5.0f; c->runSpeed = 8.0f; c->sprintKey = 'r';
        t.Start();
        CHECK(FindPlayerSim(c->gameObject) == c);
        Input::FeedKeys({});
        Input::FeedKeys({'w', 'r', ' '});
        c->Simulate(PlayerInput{}, 0.5f);                  // keys held, empty command
        CHECK(c->transform->localPosition.y == 0.0f);
        const PlayerInput r = c->ReadLocalInput();
        CHECK(r.move.y > 0.9f && r.Held(PlayerInput::Sprint) && r.Held(PlayerInput::Jump) && r.Pressed(PlayerInput::Jump));
        Input::FeedKeys({});
        PlayerInput in; in.move = Vec2(1.0f, 0.0f);
        for (int i = 0; i < 10; ++i) c->Simulate(in, 0.1f);
        CHECK(std::fabs(c->transform->localPosition.x - 5.0f) < 1e-3f);
        in.held = PlayerInput::Sprint;
        for (int i = 0; i < 10; ++i) c->Simulate(in, 0.1f);
        CHECK(std::fabs(c->transform->localPosition.x - 13.0f) < 1e-3f);
    }
    {
        Scene t("TD2");
        auto* c = t.CreateGameObject("P")->AddComponent<TopDownController2D>();
        c->speed = 5.0f; c->dashKey = 'x'; c->dashSpeed = 16.0f; c->dashDuration = 0.2f; c->driveAnimation = false;
        t.Start();
        CHECK(FindPlayerSim(c->gameObject) == c);
        Input::FeedKeys({});
        Input::FeedKeys({'d', 'x'});
        c->Simulate(PlayerInput{}, 0.1f);
        CHECK(c->transform->localPosition.x == 0.0f);
        const PlayerInput r = c->ReadLocalInput();
        CHECK(r.move.x > 0.9f && r.Pressed(PlayerInput::Dash) && !r.Held(PlayerInput::AltFire));
        Input::FeedKeys({});
        PlayerInput in; in.move = Vec2(1.0f, 0.0f); in.pressed = PlayerInput::Dash;
        c->Simulate(in, 0.1f);                              // dash: 16 u/s
        CHECK(std::fabs(c->transform->localPosition.x - 1.6f) < 1e-3f);
        in.pressed = 0;                                     // held dash key doesn't re-dash
        c->Simulate(in, 0.1f); c->Simulate(in, 0.1f);
        CHECK(std::fabs(c->transform->localPosition.x - 1.6f - 1.6f - 0.5f) < 1e-3f);
        // Mouse facing from a command: the yaw, when it says there is an aim.
        c->faceMode = (int)TopDownController2D::FaceMode::Mouse;
        c->spriteForward = (int)TopDownController2D::Forward::Right;
        PlayerInput aim; aim.yaw = 180.0f;
        const float was = c->transform->localRotation.ToEuler().z;
        c->Simulate(aim, 0.1f);                             // no aim: stays
        CHECK(c->transform->localRotation.ToEuler().z == was);
        aim.held = PlayerInput::AltFire;
        c->Simulate(aim, 0.1f);
        CHECK(std::fabs(std::fabs(c->transform->localRotation.ToEuler().z) - 180.0f) < 0.01f && c->facing == 2);
    }

    // ---- ... and they play server-authoritative: the server walks them by the commands --
    auto authPawn = [&](const char* kind) {
        const std::string pf = std::string("/tmp/okay_auth_") + kind + ".okayprefab";
        {
            Scene t("T");
            GameObject* p = t.CreateGameObject("Pawn");
            auto setup = [](auto* c) { c->walkSpeed = 5.0f; c->lockCursor = false; c->fallResetY = 0.0f; c->driveAnimation = false; };
            const std::string k = kind;
            if (k == "fp")      setup(p->AddComponent<FirstPersonController>());
            else if (k == "tp") setup(p->AddComponent<ThirdPersonController>());
            else if (k == "cc2") p->AddComponent<CharacterController2D>()->speed = 5.0f;   // top-down mode
            else { auto* td = p->AddComponent<TopDownController2D>(); td->speed = 5.0f; td->driveAnimation = false;
                   td->faceMode = (int)TopDownController2D::FaceMode::Mouse; td->spriteForward = (int)TopDownController2D::Forward::Right; }
            CHECK(SceneSerializer::SaveObjectToFile(*p, pf));
        }
        AuthGame g;
        CHECK(g.Open(true, pf));
        const bool is2D = kind[2] == '2';
        const Vec3 start = g.PosOf(g.hs);
        g.inA.move = Vec2(0.0f, 1.0f); g.inA.yaw = 90.0f;
        if (is2D) g.inA.held = PlayerInput::AltFire;          // the top-down one aims along the yaw
        g.Pump(60);
        g.inA.move = Vec2(0.0f, 0.0f);
        g.Pump(20);
        const Vec3 server = g.PosOf(g.hs);
        // 3D: forward along yaw 90 is along x. 2D: up the screen is +y.
        const float along = is2D ? server.y - start.y : std::fabs(server.x - start.x);
        const float across = is2D ? server.x - start.x : server.z - start.z;
        CHECK(along > 8.0f && along < 10.6f);
        CHECK(std::fabs(across) < 0.05f);
        GameObject* hostPawn = g.ObjOf(g.hs);
        IPlayerSim* sim = hostPawn ? FindPlayerSim(hostPawn) : nullptr;
        CHECK(sim != nullptr);
        if (auto* fp = hostPawn ? hostPawn->GetComponent<FirstPersonController>() : nullptr) CHECK(fp->yaw == 90.0f);
        if (auto* tp = hostPawn ? hostPawn->GetComponent<ThirdPersonController>() : nullptr) CHECK(tp->yaw == 90.0f);
        if (auto* td = hostPawn ? hostPawn->GetComponent<TopDownController2D>() : nullptr)
            CHECK(std::fabs(hostPawn->transform->localRotation.ToEuler().z - 90.0f) < 0.01f && td->facing == 1);
        CHECK(std::fabs(g.PosOf(g.bs).x - server.x) < 0.5f && std::fabs(g.PosOf(g.bs).y - server.y) < 0.5f);
        // A teleport from the client moves nothing.
        g.pawnA->transform->localPosition = Vec3(100.0f, 0.0f, 100.0f);
        g.a->Send("__xform", g.idA + "|100 0 100 0 0 0 1");
        g.Pump(30);
        CHECK(std::fabs(g.PosOf(g.hs).z - server.z) < 0.01f && std::fabs(g.PosOf(g.hs).x - server.x) < 0.01f
              && std::fabs(g.PosOf(g.hs).y - server.y) < 0.01f);
        std::remove(pf.c_str());
    };
    authPawn("fp");
    authPawn("tp");
    authPawn("cc2");
    authPawn("td2");

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
