// The REAL ThirdPersonController on a rigged character (sibling Rigidbody3D,
// ModelAnimator, orbit camera) under fixed-step physics at 50 Hz with render
// interpolation on, walking straight and walking while turning, drawn at
// 30 / 60 / 144 fps. The controller writes localRotation every frame; that must
// not make the drawn root or the camera stutter.
#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay;

namespace {

struct Track {
    std::vector<Vec3> root, cam, camT;   // per rendered frame
    float dt = 0.0f;
    float yaw = 0.0f;
};

// Walk `seconds` with W held. `mousePxPerSec` sweeps the mouse so the camera
// orbits and the walking direction turns (0: straight).
Track Walk(float fps, float mousePxPerSec, float seconds, float hz = 50.0f) {
    Track t;
    t.dt = 1.0f / fps;
    righarness::Rig R = righarness::PlayableFromFile("mixamo_t_cm_clips.glb", hz);
    if (!R.player) return t;
    if (auto* tp = R.player->GetComponent<ThirdPersonController>()) tp->lockCursor = false;
    Camera* cam = R.scene->mainCamera;
    Vec2 mouse{400.0f, 300.0f};
    const int frames = (int)std::lround(seconds * fps);
    for (int i = 0; i < frames; ++i) {
        if (i > 0) mouse.x += mousePxPerSec * t.dt;
        Input::FeedMouse(mouse, 0);
        Input::FeedKeys({'W'});
        R.scene->Update(t.dt);
        t.root.push_back(R.player->transform->Position());
        t.cam.push_back(cam ? cam->RenderPosition() : Vec3::Zero);
        t.camT.push_back(cam && cam->transform ? cam->transform->Position() : Vec3::Zero);
    }
    if (auto* tp = R.player->GetComponent<ThirdPersonController>()) t.yaw = tp->yaw;
    return t;
}

// Spread of the per-frame step length around its own 0.25 s running mean,
// in metres (max - min), from `skipSeconds` on. The running mean removes the
// slow, real changes of speed and the straight-line baseline; what is left is
// frame-to-frame unevenness.
float Spread(const std::vector<Vec3>& p, float dt, float skipSeconds) {
    std::vector<float> step;
    for (std::size_t i = 1; i < p.size(); ++i) step.push_back((p[i] - p[i - 1]).Magnitude());
    const int half = std::max(1, (int)std::lround(0.125f / dt));
    float lo = 1e9f, hi = -1e9f;
    const int from = std::max(half, (int)std::lround(skipSeconds / dt));
    for (int i = from; i + half < (int)step.size(); ++i) {
        float m = 0.0f;
        for (int k = -half; k <= half; ++k) m += step[i + k];
        m /= (float)(2 * half + 1);
        lo = std::min(lo, step[i] - m);
        hi = std::max(hi, step[i] - m);
    }
    return hi > lo ? hi - lo : 0.0f;
}

float MeanSpeed(const std::vector<Vec3>& p, float dt, float skipSeconds) {
    const int from = std::max(1, (int)std::lround(skipSeconds / dt));
    if ((int)p.size() <= from + 1) return 0.0f;
    float len = 0.0f;
    for (int i = from + 1; i < (int)p.size(); ++i) len += (p[i] - p[i - 1]).Magnitude();
    return len / (dt * (float)(p.size() - from - 1));
}

} // namespace

int main() {
    RUN_SUITE("controller_interpolation");

    // 2 mm: at about 5 m/s a frame is 8 to 33 mm long, so 2 mm is a 6 to 25 percent wobble,
    // clearly visible as stutter and well above float noise (the straight walk is below 0.5 mm).
    const float kRootMm = 2.0f;
    for (float fps : {30.0f, 60.0f, 144.0f}) {
        const Track straight = Walk(fps, 0.0f, 5.0f);
        const Track turning = Walk(fps, 600.0f, 5.0f);
        CHECK(!straight.root.empty() && !turning.root.empty());
        if (straight.root.empty() || turning.root.empty()) continue;
        const float sr = Spread(straight.root, straight.dt, 2.0f) * 1000.0f;
        const float tr = Spread(turning.root, turning.dt, 2.0f) * 1000.0f;
        const float sc = Spread(straight.cam, straight.dt, 2.0f) * 1000.0f;
        const float tc = Spread(turning.cam, turning.dt, 2.0f) * 1000.0f;
        const float sct = Spread(straight.camT, straight.dt, 2.0f) * 1000.0f;
        const float tct = Spread(turning.camT, turning.dt, 2.0f) * 1000.0f;
        const Vec3 d = turning.root.back() - turning.root[turning.root.size() / 2];
        std::printf("fps %3.0f  speed straight %.2f turning %.2f m/s | root spread straight %.2f turning %.2f mm"
                    " | cam(render) straight %.2f turning %.2f mm | cam(transform) straight %.2f turning %.2f mm\n",
                    fps, MeanSpeed(straight.root, straight.dt, 2.0f), MeanSpeed(turning.root, turning.dt, 2.0f),
                    sr, tr, sc, tc, sct, tct);
        const Vec3 d0 = turning.root[turning.root.size() / 2] - turning.root[turning.root.size() / 2 - 20];
        const float turnDeg = std::fabs(std::atan2(d.x, d.z) - std::atan2(d0.x, d0.z)) * 57.2958f;
        std::printf("   turning path: net %.2f m, heading differs %.0f deg between mid and end, yaw %.1f\n", d.Magnitude(), turnDeg, turning.yaw);
        CHECK(MeanSpeed(turning.root, turning.dt, 2.0f) > 1.0f);   // really walking
        CHECK(tr < kRootMm);
        CHECK(tr < sr + 1.0f);
        CHECK(tc < sc + 1.0f);
    }

    TEST_MAIN_RESULT();
}
