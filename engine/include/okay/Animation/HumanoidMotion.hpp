#pragma once
// ---------------------------------------------------------------------------
// HumanoidMotion — the built-in humanoid's procedural motion: a stride laid out
// from the ground speed (foot plants, hip bob, knee bend; walk, run and
// crouch-walk styles), the stance and air poses, the prone crawl, the gestures
// and emotions. Pure functions of the state, the clock and a few gait inputs;
// the default clip library is baked from them.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include <vector>

namespace okay {
namespace HumanoidMotion {

/// The 15-bone rig, in the Character's order.
enum Bone { B_HIPS, B_TORSO, B_HEAD,
            B_LUPARM, B_LFORE, B_LHAND, B_RUPARM, B_RFORE, B_RHAND,
            B_LTHIGH, B_LSHIN, B_LFOOT, B_RTHIGH, B_RSHIN, B_RFOOT, B_COUNT };

/// What the motion needs besides the state and the clock.
struct Inputs {
    float speed = -1.0f;     ///< ground speed (rig units/s); < 0 = not reported: the state's natural pace
    float phase = 0.0f;      ///< stride cycle 0..1 (0 = left foot touching down): gaits and the crawl
    float runW = 0.0f;       ///< walk (0) .. run (1) style, for walk and run
    float travelYaw = 0.0f;  ///< direction the feet step along, relative to the facing (deg)
    float lowerYaw = 0.0f;   ///< how far the hips and legs turn toward it (deg)
    float landK = 0.0f;      ///< landing-absorb envelope 0..1 (knees give after a landing)
};

/// The pose (per-bone Euler, degrees) of built-in state `state` — the Character's
/// `anim` numbering, 1..19 — at clock time `t`. Bones a state doesn't move are 0.
void Pose(int state, float t, const Inputs& in, std::vector<Vec3>& out);
/// The hips' height offset for the same (rig units, before the height scale):
/// the stride's bob, the crouch and prone drops, the landing dip.
float HipY(int state, const Inputs& in);
/// A state's natural pace (rig units/s), used when no speed is reported.
float NaturalSpeed(int state);
/// Stride cycles per second at `speed` (< 0 = natural pace), walk and run
/// blended by `runW`; 0 for a state with no stride.
float CycleFreq(int state, float speed, float runW);
/// The phase a gait starts at when stepping off from a stand: the left foot
/// planted under the hips, the right one swinging through.
float StartPhase(int state, float speed);

} // namespace HumanoidMotion
} // namespace okay
