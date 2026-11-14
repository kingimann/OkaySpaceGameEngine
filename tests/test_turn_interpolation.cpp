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
    float degPerSecond = 180.0f;
    Vec3 axis = Vec3::Up;
    void Update(float dt) override {
        yaw += degPerSecond * dt;
        transform->localRotation = Quat::AngleAxis(yaw, axis);
    }
    Quat Wanted() const { return Quat::AngleAxis(yaw, axis); }
};

// Chases a target a fraction at a time, like the controller's Slerp smoothing.
class Chaser : public Component {
public:
    Quat target = Quat::AngleAxis(30.0f, Vec3::Up);
    void Update(float) override { transform->localRotation = Quat::Slerp(transform->localRotation, target, 0.1f); }
};

bool QFinite(const Quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }
float AngleBetween(const Quat& a, const Quat& b) {
    float d = std::fabs(Quat::Dot(a.Normalized(), b.Normalized()));
    return 2.0f * std::acos(d > 1.0f ? 1.0f : d) * 57.29578f;
}

// Drawn distance moved per render frame, in metres, for a body at 4.5 m/s.
std::vector<float> FrameSteps(float fps, float physicsHz, bool turn, float* worstRotErrDeg = nullptr) {
    Scene s("turn");
    s.SetPhysicsStepsPerSecond(physicsHz);
    s.interpolatePhysics = true;
    GameObject* o = s.CreateGameObject("Body");
    auto* rb = o->AddComponent<Rigidbody3D>();
    rb->gravityScale = 0.0f;
    rb->velocity = Vec3{4.5f, 0.0f, 0.0f};
    Turner* tn = turn ? o->AddComponent<Turner>() : nullptr;
    float worstRot = 0.0f;
    s.Start();
    std::vector<float> d;
    float prev = 0.0f;
    const int frames = (int)fps * 2;
    for (int f = 0; f < frames; ++f) {
        s.Update(1.0f / fps);
        if (tn) worstRot = std::max(worstRot, AngleBetween(o->transform->localRotation, tn->Wanted()));
        const float x = o->transform->Position().x;
        if (f >= 10) d.push_back(x - prev);
        prev = x;
    }
    if (worstRotErrDeg) *worstRotErrDeg = worstRot;
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
        float rotErr = 0.0f;
        const auto turning = FrameSteps(fps, 50.0f, true, &rotErr);
        std::printf("fps %.0f straight spread %.2f mm, turning spread %.2f mm\n",
                    fps, Spread(straight) * 1000.0f, Spread(turning) * 1000.0f);
        CHECK(Spread(straight) <= 0.002f);
        CHECK(Spread(turning) <= 0.002f);
        // The drawn rotation is exactly what game code set (not a blend of old poses).
        std::printf("  worst rotation error %.5f deg\n", rotErr);
        CHECK(rotErr < 0.01f);
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


    // Game turn AND physics spin in the same frame: no crash, finite, and the documented
    // limit: the game's rotation wins (no blend), plus what the steps of that frame spun
    // (at most 3 rad/s * 0.02 s = 3.5 degrees per step, a handful of steps at most).
    {
        Scene s("both");
        s.SetPhysicsStepsPerSecond(50.0f);
        s.interpolatePhysics = true;
        GameObject* o = s.CreateGameObject("Body");
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->freezeRotation = false;
        rb->angularVelocity = Vec3{0.0f, 3.0f, 0.0f};
        rb->velocity = Vec3{4.5f, 0.0f, 0.0f};
        auto* tn = o->AddComponent<Turner>();
        tn->axis = Vec3::Right;               // a different axis from the spin
        s.Start();
        bool ok = true; float worst = 0.0f;
        for (int f = 0; f < 120; ++f) {
            s.Update(1.0f / 60.0f);
            ok = ok && QFinite(o->transform->Rotation()) && std::isfinite(o->transform->Position().x);
            worst = std::max(worst, AngleBetween(o->transform->localRotation, tn->Wanted()));
        }
        std::printf("  game turn + spin: worst gap to the game's rotation %.2f deg\n", worst);
        CHECK(ok);
        CHECK(worst < 8.0f);
    }

    // A parented body turned by game code: even steps, rotation as set.
    {
        Scene s("parented");
        s.SetPhysicsStepsPerSecond(50.0f);
        s.interpolatePhysics = true;
        GameObject* parent = s.CreateGameObject("Parent");
        parent->transform->localPosition = Vec3{0.0f, 2.0f, 0.0f};
        GameObject* o = s.CreateGameObject("Body");
        o->transform->SetParent(parent->transform);
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->velocity = Vec3{4.5f, 0.0f, 0.0f};
        auto* tn = o->AddComponent<Turner>();
        s.Start();
        std::vector<float> d; float prev = 0.0f, worst = 0.0f;
        for (int f = 0; f < 120; ++f) {
            s.Update(1.0f / 60.0f);
            const float x = o->transform->Position().x;
            if (f >= 10) d.push_back(x - prev);
            prev = x;
            worst = std::max(worst, AngleBetween(o->transform->localRotation, tn->Wanted()));
        }
        std::printf("  parented turning spread %.2f mm, rotation gap %.4f deg\n", Spread(d) * 1000.0f, worst);
        CHECK(Spread(d) <= 0.002f);
        CHECK(worst < 0.01f);
    }

    // A 2D body turned by game code.
    {
        Scene s("two-d");
        s.SetPhysicsStepsPerSecond(50.0f);
        s.interpolatePhysics = true;
        GameObject* o = s.CreateGameObject("Body");
        auto* rb = o->AddComponent<Rigidbody2D>();
        rb->gravityScale = 0.0f;
        rb->velocity = Vec2{4.5f, 0.0f};
        auto* tn = o->AddComponent<Turner>();
        tn->axis = Vec3::Forward;
        s.Start();
        std::vector<float> d; float prev = 0.0f, worst = 0.0f;
        for (int f = 0; f < 120; ++f) {
            s.Update(1.0f / 60.0f);
            const float x = o->transform->Position().x;
            if (f >= 10) d.push_back(x - prev);
            prev = x;
            worst = std::max(worst, AngleBetween(o->transform->localRotation, tn->Wanted()));
        }
        std::printf("  2D turning spread %.2f mm, rotation gap %.4f deg\n", Spread(d) * 1000.0f, worst);
        CHECK(Spread(d) <= 0.002f);
        CHECK(worst < 0.01f);
    }

    // Slow turns: a smoothed chase moves a fraction of a degree per frame near its target.
    // That must still arrive (it used to stall about 1.6 degrees short).
    {
        Scene s("chase");
        s.SetPhysicsStepsPerSecond(50.0f);
        s.interpolatePhysics = true;
        GameObject* o = s.CreateGameObject("Body");
        auto* rb = o->AddComponent<Rigidbody3D>();
        rb->gravityScale = 0.0f;
        rb->velocity = Vec3{1.0f, 0.0f, 0.0f};
        auto* ch = o->AddComponent<Chaser>();
        s.Start();
        for (int f = 0; f < 600; ++f) s.Update(1.0f / 60.0f);
        const float left = AngleBetween(o->transform->localRotation, ch->target);
        std::printf("  chase: %.4f deg short of a 30 degree target\n", left);
        CHECK(left < 0.05f);
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
