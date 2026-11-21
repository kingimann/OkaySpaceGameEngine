// Animation transitions on the built-in character with the default Animator graph, driven headless through
// DriveLocomotion like a controller. Measures the biggest speed any skinned vertex moves in the character's own
// space (m/s) from one frame to the next, at 30, 60 and 144 frames a second, through each scripted change of
// state. A pop is a vertex that jumps far in one frame; a smooth blend moves limbs at limb speeds only.
// Written from general animation-blending ideas; no code from another project.
#include "test_framework.hpp"
#include "walk_vs_run_measure.hpp"
#include <cstdio>
using namespace walkrun;

namespace {
struct Rig {
    Walker w; float z = 0, vy = 0, y = 0;
    std::vector<Vec3> prev; float worst = 0; int worstFrame = -1; int frame = 0;
    Rig() : w(true) {}
    void Local(std::vector<Vec3>& out) { out = w.mr->mesh.vertices; }
    // One frame. v = ground speed, air = off the ground.
    void Step(float dt, float v, bool air, bool sprint, bool jumped = false, bool crouch = false, bool measure = true) {
        z -= v * dt;
        w.go->transform->localPosition = {0, y, z};
        Character::Locomotion lo;
        lo.velocity = {0, vy, -v}; lo.facing = {0, 0, -1}; lo.onGround = !air; lo.sprint = sprint; lo.jumped = jumped;
        lo.crouch = crouch;
        lo.walkSpeed = w.walkSpeed; lo.runSpeed = w.runSpeed; lo.dt = dt;
        w.ch->DriveLocomotion(lo);
        w.scene.Update(dt);
        std::vector<Vec3> cur; Local(cur);
        if (measure && prev.size() == cur.size()) {
            float m = 0;
            for (std::size_t i = 0; i < cur.size(); ++i) m = std::fmax(m, (cur[i] - prev[i]).Magnitude());
            const float sp = m / dt;
            if (sp > worst) { worst = sp; worstFrame = frame; }
        }
        prev = cur; ++frame;
    }
    void Run(float secs, float dt, float v, bool air, bool sprint, bool crouch = false) {
        for (int i = 0; i < (int)(secs / dt + 0.5f); ++i) Step(dt, v, air, sprint, false, crouch);
    }
    void ResetWorst() { worst = 0; worstFrame = -1; }
};
}

int main() {
    RUN_SUITE("anim_transitions");
    for (float fps : {30.0f, 60.0f, 144.0f}) {
        const float dt = 1.0f / fps;
        std::printf("== %.0f fps\n", fps);
        struct Case { const char* name; float limit; };
        {   Rig r; r.Run(2, dt, 0, false, false);
            r.Run(2, dt, 5.5f, false, true); r.ResetWorst();   // settled running: the baseline limb speed
            r.Run(3, dt, 5.5f, false, true);
            const float base = r.worst; r.ResetWorst();
            r.Run(2, dt, 1.4f, false, false);   // run -> walk
            std::printf("  run base %.2f, run->walk worst %.2f m/s (frame %d)\n", base, r.worst, r.worstFrame);
            CHECK(r.worst < base * 1.6f + 1.0f);
        }
        {   Rig r; r.Run(2, dt, 0, false, false); r.ResetWorst();
            r.Run(2, dt, 1.4f, false, false);   // idle -> walk
            const float a = r.worst; r.ResetWorst();
            r.Run(2, dt, 0, false, false);      // walk -> stop
            std::printf("  idle->walk worst %.2f, walk->idle worst %.2f m/s\n", a, r.worst);
            CHECK(a < 12.0f); CHECK(r.worst < 12.0f);
        }
        {   Rig r; r.Run(2, dt, 0, false, false); r.ResetWorst();   // jump, fall, land
            r.Step(dt, 0, true, false, true); r.y = 0.05f;
            for (int i = 0; i < (int)(0.3f / dt); ++i) { r.vy = 3; r.Step(dt, 0, true, false); }
            for (int i = 0; i < (int)(0.3f / dt); ++i) { r.vy = -3; r.Step(dt, 0, true, false); }
            r.y = 0; r.vy = 0; r.Run(1.5f, dt, 0, false, false);
            std::printf("  jump/fall/land worst %.2f m/s (frame %d)\n", r.worst, r.worstFrame);
            CHECK(r.worst < 25.0f);
        }
        {   Rig r; r.Run(2, dt, 0, false, false); r.ResetWorst();   // crouch in and out
            r.Run(1, dt, 0, false, false, true); r.Run(1, dt, 0, false, false, false);
            std::printf("  crouch in/out worst %.2f m/s\n", r.worst);
            CHECK(r.worst < 12.0f);
        }
        {   // grounded flicker for one frame while walking must not trigger a fall/jump pose
            Rig r; r.Run(2, dt, 1.4f, false, false); r.ResetWorst();
            r.Step(dt, 1.4f, true, false); r.Run(1, dt, 1.4f, false, false);
            std::printf("  one-frame ungrounded blip worst %.2f m/s\n", r.worst);
            CHECK(r.worst < 12.0f);
        }
    }
    TEST_MAIN_RESULT();
}
