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
    float travelYaw = 0.0f;  ///< direction the feet step (or the crawl goes) along, relative to the facing (deg)
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
/// How much a walk at `speed` (m/s) takes the run's style (0..1). People break
/// into a jog at about 2.5 m/s: past that a walk cycle is a frantic speed-walk
/// (the swing foot whips forward at 10 m/s, the feet slap down), so a fast
/// "walk" eases into a jog between 2.4 and 3.4 m/s. The state stays the walk.
float JogWeight(float speed);
/// The phase a gait starts at when stepping off from a stand: the left foot
/// planted under the hips, the right one swinging through.
float StartPhase(int state, float speed);
/// A landing's knee give on a striding pose (walk or run): the hips drop by
/// 0.12·landK and both legs re-solve so the feet stay where they were, the
/// chest dips and the arms go out. Returns the hips' height change (rig units).
float AbsorbLanding(std::vector<Vec3>& pose, float landK);
/// The poses between lying prone and standing (getting up, dropping down):
/// 0 = on hands and knees, 1 = kneeling on the right knee. Returns the hips'
/// height offset for it (rig units, like HipY).
float KneelPose(int which, std::vector<Vec3>& out);

} // namespace HumanoidMotion
} // namespace okay
