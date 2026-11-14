// A character that turns every frame must still be drawn smoothly.
// Game code (like ThirdPersonController) writes localRotation in Update, which runs
// before StepPhysics. That must not stop the physics pose being put back before a step.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace okay;

namespace {

// Turns its object a little every frame, the way the third-person controller does.
class Turner : public Component {
public:
    float yaw = 0.0f;
    void Update(float dt) override {
        yaw += 180.0f * dt;
        transform->localRotation = Quat::AngleAxis(yaw, Vec3::Up);
    }
};

// Drawn distance moved per render frame, in metres, for a body at 4.5 m/s.
std::vector<float> FrameSteps(float fps, float physicsHz, bool turn, bool spin = false) {
    Scene s("turn");
    s.SetPhysicsStepsPerSecond(physicsHz);
    s.interpolatePhysics = true;
    GameObject* o = s.CreateGameObject("Body");
    auto* rb = o->AddComponent<Rigidbody3D>();
    rb->gravityScale = 0.0f;
    rb->velocity = Vec3{4.5f, 0.0f, 0.0f};
    if (spin) rb->angularVelocity = Vec3{0.0f, 3.0f, 0.0f};
    if (turn) o->AddComponent<Turner>();
    s.Start();
    std::vector<float> d;
    float prev = 0.0f;
    const int frames = (int)fps * 2;
    for (int f = 0; f < frames; ++f) {
        s.Update(1.0f / fps);
        const float x = o->transform->Position().x;
        if (f >= 10) d.push_back(x - prev);
        prev = x;
    }
    return d;
}

float Spread(const std::vector<float>& d) {
    return *std::max_element(d.begin(), d.end()) - *std::min_element(d.begin(), d.end());
}

} // namespace

int main() {
    RUN_SUITE("turn_interpolation");

    for (float fps : {30.0f, 60.0f, 144.0f}) {
        const auto straight = FrameSteps(fps, 50.0f, false);
        const auto turning = FrameSteps(fps, 50.0f, true);
        std::printf("fps %.0f straight spread %.2f mm, turning spread %.2f mm\n",
                    fps, Spread(straight) * 1000.0f, Spread(turning) * 1000.0f);
        CHECK(Spread(straight) <= 0.002f);
        CHECK(Spread(turning) <= 0.002f);
        // Same distance per frame as the straight case.
        float worst = 0.0f;
        for (size_t i = 0; i < std::min(straight.size(), turning.size()); ++i)
            worst = std::max(worst, std::fabs(straight[i] - turning[i]));
        CHECK(worst <= 0.002f);
    }

    // A body that physics itself spins is still blended between steps (rotation not game-driven).
    {
        Scene s("spin");
        s.SetPhysicsStepsPerSecond(50.0f);
        s.interpolatePhysics = true;
        GameObject* o = s.CreateGameObject("Body");
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->angularVelocity = Vec3{0.0f, 3.0f, 0.0f};
        rb->freezeRotation = false;
        s.Start();
        float prev = 0.0f; bool first = true; float lo = 1e9f, hi = -1e9f;
        for (int f = 0; f < 120; ++f) {
            s.Update(1.0f / 144.0f);
            float deg; Vec3 ax;
            o->transform->Rotation().ToAngleAxis(deg, ax);
            const float a = deg * (ax.y >= 0 ? 1.0f : -1.0f);
            if (f > 10) {
                if (!first) { float dd = std::fabs(a - prev); lo = std::min(lo, dd); hi = std::max(hi, dd); }
                first = false;
            }
            prev = a;
        }
        std::printf("spin lo %.5f hi %.5f\n", lo, hi);
        CHECK(lo > 0.0f);                 // moves every frame, not only on step frames
        CHECK(hi - lo < 0.2f);            // about 1.2 deg per frame, evenly
    }

    // A real teleport still wins and still clears the history.
    {
        Scene s("tele");
        s.UseFixedStepPhysics(true);
        GameObject* o = s.CreateGameObject("Body");
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->velocity = Vec3{1.0f, 0.0f, 0.0f};
        o->AddComponent<Turner>();
        s.Start();
        for (int f = 0; f < 10; ++f) s.Update(1.0f / 144.0f);
        o->transform->SetPosition(Vec3{100.0f, 5.0f, 0.0f});
        s.Update(1.0f / 144.0f);
        CHECK(o->transform->Position().x >= 100.0f);
        CHECK(o->transform->Position().x < 100.1f);
    }

    TEST_MAIN_RESULT();
}
