#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <string>

using namespace okay;

// Visual-script nodes for music, camera and particles: each node runs inside a
// real graph on a real component (MusicPlayer, Camera, ParticleSystem).

namespace {

struct World {
    Scene scene{"VsMedia"};
    GameObject* hero = nullptr;
    GameObject* other = nullptr;
    GameObject* cam = nullptr;
    MusicPlayer* music = nullptr;
    VisualScriptComponent* vs = nullptr;
    ParticleSystem* heroFx = nullptr;
    ParticleSystem* otherFx = nullptr;
    std::string a = "test_vs_media_a.wav", b = "test_vs_media_b.wav";

    World() {
        scene.physicsEnabled = false;
        AudioClip::Const(0.5f, 60000, 1000).SaveWAV(a);
        AudioClip::Const(0.5f, 60000, 1000).SaveWAV(b);
        music = scene.CreateGameObject("Music")->AddComponent<MusicPlayer>();
        music->tracks = {a, b};
        music->playOnStart = false;
        cam = scene.CreateGameObject("Cam");
        cam->AddComponent<Camera>();
        other = scene.CreateGameObject("Other");
        otherFx = other->AddComponent<ParticleSystem>();
        otherFx->playing = false;
        hero = scene.CreateGameObject("Hero");
        hero->transform->localPosition = {4, 5, 6};
        heroFx = hero->AddComponent<ParticleSystem>();
        heroFx->playing = false;
        vs = hero->AddComponent<VisualScriptComponent>();
    }
    ~World() { std::remove(a.c_str()); std::remove(b.c_str()); }

    // Load and run `body` (a chain of nodes after OnStart) once.
    bool Run(const std::string& body) {
        std::string err;
        const bool ok = vs->LoadFromText("node 1 OnStart\n" + body + "entry OnStart 1\n", &err);
        if (!ok) std::printf("  graph error: %s\n", err.c_str());
        scene.Start();
        scene.Update(0.0f);
        return ok;
    }
    // Swap in another graph on the started scene and run its OnStart chain.
    bool Again(const std::string& body) {
        std::string err;
        const bool ok = vs->LoadFromText("node 1 OnStart\n" + body + "entry OnStart 1\n", &err);
        if (!ok) std::printf("  graph error: %s\n", err.c_str());
        vs->Start();
        return ok;
    }
};

} // namespace

int main() {
    RUN_SUITE("vs_media");

    // Play Music: by playlist number, by file name; Next wraps; Pause / Resume; Stop.
    {
        World w;
        CHECK(w.Run("node 2 MusicPlay 1\nexec 1 0 2\n"));
        CHECK(w.music->CurrentIndex() == 1 && w.music->IsPlaying());
    }
    {
        World w;
        CHECK(w.Run("node 2 MusicPlay " + w.b + "\nnode 3 MusicNext\nexec 1 0 2\nexec 2 0 3\n"));
        CHECK(w.music->CurrentIndex() == 0);   // b was track 1, Next wraps to 0
    }
    {
        World w;
        CHECK(w.Run("node 2 MusicPlay\nexec 1 0 2\n"));   // no track: the first
        CHECK(w.music->CurrentIndex() == 0 && w.music->IsPlaying());
        CHECK(w.Again("node 2 MusicPause\nexec 1 0 2\n"));
        CHECK(!w.music->IsPlaying());
        CHECK(w.Again("node 2 MusicResume\nexec 1 0 2\n"));
        CHECK(w.music->IsPlaying());
        CHECK(w.Again("node 2 MusicStop\nnode 3 Const 0\nexec 1 0 2\ndata 2 0 3 0\n"));
        CHECK(!w.music->IsPlaying());
    }

    // Camera Shake stacks; Set Camera FOV clamps; Camera Follow adds the follower.
    {
        World w;
        CHECK(w.Run("node 2 CameraShake\nnode 3 Const 0.25\nnode 4 CameraShake\nnode 5 Const 0.5\n"
                    "node 6 SetFov\nnode 7 Const 500\n"
                    "exec 1 0 2\nexec 2 0 4\nexec 4 0 6\ndata 2 0 3 0\ndata 4 0 5 0\ndata 6 0 7 0\n"));
        CHECK(w.scene.mainCamera != nullptr);
        if (w.scene.mainCamera) {
            CHECK_NEAR(w.scene.mainCamera->ShakeTrauma(), 0.75f, 0.05f);
            CHECK_NEAR(w.scene.mainCamera->fieldOfView, 179.0f, 1e-4f);
        }
    }
    {
        World w;
        CHECK(w.Run("node 2 SetFov\nnode 3 Const 90\nexec 1 0 2\ndata 2 0 3 0\n"));
        CHECK(w.scene.mainCamera && std::fabs(w.scene.mainCamera->fieldOfView - 90.0f) < 1e-4f);
    }
    {
        World w;
        CHECK(w.Run("node 2 CameraFollow\nnode 3 Const \"Hero\"\nnode 4 Const 0\nexec 1 0 2\ndata 2 0 3 0\ndata 2 1 4 0\n"));
        auto* f = w.cam->GetComponent<CameraFollow>();
        CHECK(f != nullptr && f->targetName == "Hero" && f->smoothing == 0.0f);
        w.scene.Update(1.0f / 60.0f);
        const Vec3 p = w.cam->transform->Position();
        CHECK(std::fabs(p.x - 4.0f) < 1e-3f && std::fabs(p.y - 5.0f) < 1e-3f && std::fabs(p.z - 6.0f) < 1e-3f);
    }

    // The script builtin the Camera Follow node's code uses.
    {
        World w;
        auto* sc = w.other->AddComponent<ScriptComponent>();
        std::string err;
        CHECK(sc->LoadSource("func start() {\n  camera_follow(\"Hero\", 0)\n}\n", &err));
        w.scene.Start();
        w.scene.Update(1.0f / 60.0f);
        auto* f = w.cam->GetComponent<CameraFollow>();
        CHECK(f != nullptr && f->targetName == "Hero");
        const Vec3 p = w.cam->transform->Position();
        CHECK(std::fabs(p.x - 4.0f) < 1e-3f && std::fabs(p.z - 6.0f) < 1e-3f);
    }

    // Particles: play / stop / burst on this object, and on the Target.
    {
        World w;
        CHECK(w.Run("node 2 ParticlePlay\nexec 1 0 2\n"));
        CHECK(w.heroFx->playing);
        CHECK(w.Again("node 2 ParticleStop\nexec 1 0 2\n"));
        CHECK(!w.heroFx->playing);
        const int before = w.heroFx->AliveCount();
        CHECK(w.Again("node 2 ParticleBurst\nnode 3 Const 7\nexec 1 0 2\ndata 2 0 3 0\n"));
        CHECK(w.heroFx->AliveCount() == before + 7);
        CHECK(w.otherFx->AliveCount() == 0);
        // Target pin (after Count): the burst goes to "Other".
        CHECK(w.Again("node 2 ParticleBurst\nnode 3 Const 3\nnode 4 Const \"Other\"\n"
                                 "exec 1 0 2\ndata 2 0 3 0\ndata 2 1 4 0\n"));
        CHECK(w.otherFx->AliveCount() == 3 && w.heroFx->AliveCount() == before + 7);
    }

    // Factory, pin table and the Target pin.
    {
        std::string err;
        for (const char* t : {"MusicPlay", "MusicStop", "MusicPause", "MusicResume", "MusicNext", "CameraShake",
                              "SetFov", "CameraFollow", "ParticlePlay", "ParticleStop", "ParticleBurst"}) {
            CHECK(vs::MakeNode(t, {}, &err) != nullptr);
            CHECK(vs::SignatureFor(t) != nullptr);
        }
        CHECK(vs::SignatureFor("ParticleBurst")->ins == "NO");
        CHECK(vs::SignatureFor("ParticlePlay")->ins == "O");
        CHECK(vs::SignatureFor("MusicStop")->ins == "N");
        CHECK(vs::SignatureFor("CameraFollow")->ins == "TN");
    }

    TEST_MAIN_RESULT();
}
