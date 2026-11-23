#pragma once
// Shared test rig for the character-movement tests (test_player_feel, test_controller_feel2): a headless scene with
// fixed-step physics and an optional big floor, the real player built the way the editor builds it (body, Player
// Controller, an optional controller preset, eye camera), and fake-key frame stepping. Written for this engine.
#include <Okay.hpp>
#include "okay/Components/ControllerPresets.hpp"
#include <cmath>
#include <memory>
#include <vector>

namespace okay_test {

struct PlayerTestRig {
    using Mode = okay::PlayerController::ViewMode;
    std::unique_ptr<okay::Scene> sc;
    okay::GameObject* p = nullptr;
    okay::Rigidbody3D* rb = nullptr;
    float dt = 1.0f / 60.0f;

    // sceneName: the scene's name. floor: add a 400 x 400 m floor box (top at y = 0) named floorName. fps: the frame
    // step Frame() uses unless it is given one.
    explicit PlayerTestRig(const char* sceneName, bool floor = true, float fps = 60.0f, const char* floorName = "Floor")
        : dt(1.0f / fps) {
        sc = std::make_unique<okay::Scene>(sceneName);
        sc->physicsEnabled = true;
        sc->UseFixedStepPhysics(true);
        if (floor) Box({0, -0.5f, 0}, {400, 1, 400}, {0, 0, 0}, floorName);
    }
    okay::GameObject* Box(okay::Vec3 at, okay::Vec3 size, okay::Vec3 euler = {0, 0, 0}, const char* name = "Box") {
        okay::GameObject* f = sc->CreateGameObject(name);
        f->transform->localPosition = at;
        f->transform->localRotation = okay::Quat::Euler(euler.x, euler.y, euler.z);
        f->AddComponent<okay::BoxCollider3D>()->size = size;
        return f;
    }
    // Build the player at `at`; preset >= 0 applies that ControllerPresets entry before the camera is mounted.
    void Spawn(Mode m, okay::Vec3 at = {0, 0.02f, 0}, int preset = -1) {
        p = okay::Templates::BuildPlayerBody(*sc, at, "Player");
        rb = p->GetComponent<okay::Rigidbody3D>();
        okay::PlayerController::Add(p, m, m == Mode::Third ? okay::PlayerView::View::Third : okay::PlayerView::View::First);
        if (preset >= 0) okay::ControllerPresets::ApplyTo(p, preset);
        okay::Templates::MountEyeCamera(*sc, p, "Main Camera");
        sc->Start();
    }
    void Spawn(Mode m, int preset, float y = 0.02f) { Spawn(m, okay::Vec3{0, y, 0}, preset); }
    // One frame with these keys held; d > 0 overrides the rig's frame step for this frame.
    void Frame(const std::vector<char>& k, float d = 0.0f) { okay::Input::FeedKeys(k); sc->Update(d > 0 ? d : dt); }
    void Frames(double s, const std::vector<char>& k) { for (int i = 0, n = (int)std::lround(s / dt); i < n; ++i) Frame(k); }
    okay::Vec3 Pos() const { return p->transform->Position(); }
    ~PlayerTestRig() { okay::Input::FeedKeys({}); }
};

// Horizontal (ground-plane) speed of the rig's player.
inline float HSpeed(const PlayerTestRig& r) { return std::hypot(r.rb->velocity.x, r.rb->velocity.z); }

}  // namespace okay_test
