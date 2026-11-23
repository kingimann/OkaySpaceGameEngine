// Character: procedural locomotion, pose building, skinning and the clip/blend/crossfade playback, moved verbatim out of
// Character.cpp (docs/engine/modularization_plan.md, S5).
#include "CharacterInternal.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/FirstPersonHands.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace okay {

using namespace HumanoidMotion;   // the bone indices (B_HIPS ...)
using namespace character_rig;

// ---- Procedural locomotion -------------------------------------------------
namespace {
constexpr float kRad2Deg = 57.2957795f;
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float frac(float v) { return v - std::floor(v); }
} // namespace

// The built-in motion's inputs for a state at clock time t: the stride's speed,
// phase and style, which way the feet step, the landing dip.
HumanoidMotion::Inputs Character::MotionInputs(int state, float t) const {
    HumanoidMotion::Inputs in;
    in.speed = m_gaitSpeed;
    in.phase = GaitPhaseAt(t, state);
    in.runW = RunWeight(state);
    in.travelYaw = m_travelYaw;
    in.lowerYaw = m_lowerYaw;
    in.landK = m_landK;
    return in;
}

// Cycles per second at a state's natural pace (used when no speed is reported).
static float NominalFreq(int state) { return HumanoidMotion::CycleFreq(state, -1.0f, state == 3 ? 1.0f : 0.0f); }

float Character::GaitPhase() const {
    if (anim != 2 && anim != 3 && anim != 17) return 0.0f;
    return frac(m_gaitPhase);
}

// Metres per rig unit: the height scale times how much the object is scaled. The stride is laid out in rig
// units (a bigger body takes proportionally longer strides), so a ground speed in m/s is judged in them.
float Character::RigScale() const {
    float sc = height > 1e-3f ? height : 1.0f;
    if (gameObject && gameObject->transform) {
        Vec3 ls = gameObject->transform->LossyScale();
        float s = 0.5f * (std::fabs(ls.x) + std::fabs(ls.z));
        if (s > 1e-3f) sc *= s;
    }
    return sc;
}

// The gait phase at clock time t: the live phase at animTime, extrapolated at the
// current cadence for any other time (editor previews sample around it).
float Character::GaitPhaseAt(float t, int state) const {
    float f = m_gaitFreq != 0.0f ? m_gaitFreq : NominalFreq(state);
    return frac(m_gaitPhase + (t - animTime) * f);
}

float Character::GaitCycleSeconds(int state) const {
    if (state != 2 && state != 3 && state != 17) return 0.0f;
    float f = m_gaitFreq != 0.0f ? m_gaitFreq : NominalFreq(state);   // the rate GaitPhaseAt uses
    f = f < 0.0f ? -f : f;
    return f > 1e-4f ? 1.0f / f : 0.0f;
}

// 0 = walking style, 1 = running style. The live state eases between them; any
// other state asked for (a crossfade source) uses its own style outright.
float Character::RunWeight(int state) const {
    if (state == anim && (state == 2 || state == 3)) return m_runW;
    return state == 3 ? 1.0f : 0.0f;
}

bool Character::SprintingPose() const {
    if (anim == 3) return true;
    // The walk cruise sits near m_runW 0.3. Stay hidden only while the pose is
    // still mostly a run, so a fast walk shows its arms and a sprint doesn't
    // pop them back for the first frames of the blend.
    return anim == 2 && m_runW > 0.55f;
}

void Character::SetHeldHiddenFromOwner(bool hide) {
    if (!gameObject) return;
    auto* hold = gameObject->GetComponent<ProceduralObjectHolding>();
    if (!hold) return;
    if (GameObject* item = hold->Held())
        item->ownerCameraSkip = hide ? gameObject : nullptr;
}

// Advance the built-in animation clocks. The gait phase runs at the cadence the
// reported ground speed calls for (backwards when the body backpedals), so a stride
// always covers exactly the ground the body moved, and a state change never moves it.
void Character::AdvanceAnimClock(float dt) {
    const bool gait = (anim == 2 || anim == 3 || anim == 17);
    // Rig units per second from the reported ground speed (m/s). A bigger body
    // takes proportionally longer strides.
    auto toRig = [&](float metres) { return metres / RigScale(); };
    if (m_gaitFromDrive) {
        // DriveLocomotion already smoothed the metres/second. Don't smooth again.
        m_gaitFromDrive = false;
        m_gaitSpeed = moveSpeed < 0.0f ? -1.0f : toRig(moveSpeed);
    } else {
        float target = moveSpeed >= 0.0f ? toRig(moveSpeed) : -1.0f;
        if (target < 0.0f)           m_gaitSpeed = -1.0f;
        else if (m_gaitSpeed < 0.0f) m_gaitSpeed = target;
        else                         m_gaitSpeed += (target - m_gaitSpeed) *
                                                    (gaitSpeedSmoothing > 1e-4f ? 1.0f - std::exp(-dt / gaitSpeedSmoothing) : 1.0f);
    }

    // Travel direction relative to the facing: the hips and legs turn toward it
    // (up to 75°, the chest stays forward); pointing behind, they step backwards.
    float a = std::fmod(moveAngle + 540.0f, 360.0f) - 180.0f;
    float limit = m_gaitDir < 0.0f ? 80.0f : 100.0f;            // hysteresis around the side
    m_gaitDir = std::fabs(a) > limit ? -1.0f : 1.0f;
    float yaw = m_gaitDir < 0.0f ? (a > 0.0f ? a - 180.0f : a + 180.0f) : a;
    if ((!gait && anim != 7) || m_gaitSpeed < 0.1f) yaw = 0.0f;   // the crawl goes every way too
    float ek = 1.0f - std::exp(-10.0f * dt);
    m_travelYaw += (yaw - m_travelYaw) * ek;                    // where the feet actually step
    m_lowerYaw  += (clampf(yaw, -75.0f, 75.0f) - m_lowerYaw) * ek;   // how far the hips turn

    // Walk <-> run blends the two styles over ~0.2 s on the live poses.
    // DriveLocomotion sets the target from speed, so the threshold is a blend
    // and not a second crossfade on top of this one. Without it, a fast walk
    // takes the run's style (JogWeight): a walk cycle stretched to 4.5 m/s whips the feet.
    float wantRun = m_haveRunWant ? m_runWant
                  : (anim == 3 ? 1.0f : (anim == 2 ? HumanoidMotion::JogWeight(m_gaitSpeed) : 0.0f));
    m_haveRunWant = false;
    float step = dt / 0.2f;
    m_runW = m_runW < wantRun ? std::fmin(wantRun, m_runW + step) : std::fmax(wantRun, m_runW - step);

    float rate = dt * animSpeed;
    animTime += rate;
    if (gait)           m_gaitFreq = HumanoidMotion::CycleFreq(anim, m_gaitSpeed, m_runW) * m_gaitDir;
    else if (anim == 7) m_gaitFreq = HumanoidMotion::CycleFreq(7, m_gaitSpeed, 0.0f);   // the crawl follows the distance
    else {
        m_gaitFreq = 0.0f;
    }
    m_gaitPhase = frac(m_gaitPhase + rate * m_gaitFreq);

    // The Animator's gait trees: the travel direction (at least as long as the
    // slowest stride clip, so the direction always picks the clips) and speed.
    if (m_poseSource) {
        const float v = m_gaitSpeed >= 0.0f ? m_gaitSpeed : HumanoidMotion::NaturalSpeed(anim);
        const float dir = (m_travelYaw + (m_gaitDir < 0.0f ? 180.0f : 0.0f)) / kRad2Deg;
        const float r = std::fmax(v, 0.3f);
        m_poseSource->Gait(r * std::sin(dir), r * std::cos(dir), v);
    }
}

void Character::DriveLocomotion(const Locomotion& l) {
    // Measure what the body really did: a controller sets the velocity it WANTS
    // before physics runs, so pressed against a wall the velocity still says
    // "moving" while the body stands still. The position change since the previous
    // sample is the truth, so it doesn't matter whether the move landed this frame
    // (a controller moving the transform) or last frame (a physics step). A
    // teleport/respawn jump restarts the measurement, and a one-frame spike (a
    // hitch, a depenetration push) is set aside unless the next frame agrees.
    // The accepted speed is smoothed once, below — not averaged over a window
    // and then smoothed again.
    Vec3 vel = l.velocity;
    float vyNow = l.velocity.y;   // vertical speed right now
    if (gameObject && gameObject->transform && l.dt > 1e-5f) {
        Vec3 pos = gameObject->transform->Position();
        const float al = l.ignoreAxis.Magnitude();
        auto flat = [&](Vec3 d) {
            if (al > 1e-5f) { Vec3 a = l.ignoreAxis * (1.0f / al); d = d - a * Vec3::Dot(d, a); }
            return d;
        };
        m_moveClock += l.dt;
        if (m_histN > 0) {
            Vec3 d = flat(pos - m_hist[0].p);
            if (d.Magnitude() > 2.0f) m_histN = 0;   // a teleport: don't treat it as a stride
            else if (l.dt > 1e-5f) {
                vyNow = std::max(vyNow, (pos.y - m_hist[0].p.y) / l.dt);
                vel = d * (1.0f / l.dt);
            }
        }
        m_hist[0] = {pos, m_moveClock};
        m_histN = 1;
    }
    // On the ground: a fresh contact, or solid ground just under the feet (contact
    // callbacks stop for a body at rest, and stepping off a curb shouldn't count).
    bool ground = l.onGround;
    if (!ground && !l.jumped && gameObject && gameObject->transform && gameObject->scene()) {
        Scene* sc = gameObject->scene();
        Vec3 o = gameObject->transform->Position();
        RaycastHit3D h = sc->physics3D().Raycast(*sc, o + Vec3{0.0f, 0.15f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}, 0.45f,
                                                 gameObject, Physics3D::kAllLayers, false);
        ground = h.hit && vyNow < 1.0f;
    }
    Vec3 v{vel.x, 0.0f, vel.z};
    float hs = std::sqrt(v.x * v.x + v.z * v.z);
    const float jumpAt = 2.0f * m_measured + 2.0f;   // what the controller didn't ask for, for one frame
    if (hs > jumpAt && std::sqrt(l.velocity.x * l.velocity.x + l.velocity.z * l.velocity.z) < jumpAt && !m_spike) {
        m_spike = true;
        const float k = hs > 1e-5f ? m_measured / hs : 0.0f;
        v = v * k; hs = m_measured;
    } else {
        m_spike = false;
        m_measured = hs;
    }
    // One smooth, in metres/second. AdvanceAnimClock turns this into the stride
    // rate without smoothing it a second time.
    // The stride eases toward this frame's speed. Idle / walk / run still follow
    // the speed itself (with the hysteresis below), so a sprint changes state on
    // the frame it happens.
    if (moveSpeed < 0.0f) moveSpeed = hs;
    else if (l.dt > 1e-5f) {
        const float a = gaitSpeedSmoothing > 1e-4f ? 1.0f - std::exp(-l.dt / gaitSpeedSmoothing) : 1.0f;
        moveSpeed += (hs - moveSpeed) * a;
    }
    m_gaitFromDrive = true;
    Vec3 f{l.facing.x, 0.0f, l.facing.z};
    float fl = std::sqrt(f.x * f.x + f.z * f.z);
    if (fl > 1e-4f && hs > 0.05f) {
        f = f * (1.0f / fl);
        Vec3 right{-f.z, 0.0f, f.x};                          // the body's right
        moveAngle = std::atan2(v.x * right.x + v.z * right.z, v.x * f.x + v.z * f.z) * kRad2Deg;
    }
    if (l.swim) ground = true;                                 // water is holding the body
    m_airT = (ground && !l.jumped) ? 0.0f : m_airT + l.dt;
    if (l.jumped) m_airT = 1.0f;                               // leave the ground pose at once
    if (l.jumped && jumpCrouchTime > 0.0f) m_anticipation = jumpCrouchTime;
    // Standing still, turning the body twists the feet until they step.
    if (!l.jumped && m_airT <= 0.0f && hs < turnStillSpeed && l.dt > 0.0f) {
        m_twist += l.yawRate * l.dt;
        if (m_stepT < 0.0f && turnStepAngle > 1.0f && std::fabs(m_twist) >= turnStepAngle) {
            m_stepLeg = m_twist > 0.0f ? 1 : 0;
            m_stepT = 0.0f;
            m_twist = 0.0f;
        }
    } else if (l.dt > 0.0f) {
        m_twist *= std::exp(-l.dt / 0.12f);
    }
    if (l.swim) m_airT = 0.0f;
    m_swimming = l.swim;
    const float runAt = l.walkSpeed * (anim == 3 ? 1.05f : 1.15f);
    const bool run = hs > runAt || (l.sprint && hs > l.walkSpeed * 0.9f);
    if (HumanoidMotion::OldGait()) {
        // The look before the natural walk and run: it followed the controller's own speeds, starting at 65%
        // of its walk speed, so a controller whose walk was 4.5 m/s showed about a third of a run for plain W.
        const float rs = l.runSpeed > 0.1f ? l.runSpeed : 8.0f;
        const float lo = std::max(0.4f, l.walkSpeed * 0.65f);
        const float hi = std::max(lo + 0.75f, rs);
        float style = hs <= lo ? 0.0f : std::min(1.0f, (hs - lo) / (hi - lo));
        if (run) style = 1.0f;
        m_runWant = style;
        m_haveRunWant = true;
    } else {
        // How much the legs show a run follows how fast the body really moves, not the controller's settings:
        // a person walks up to about 2.4 m/s and jogs from 3.4, with a smooth change between them (JogWeight,
        // judged in rig units so a bigger or smaller body is judged by its own legs). A controller whose "walk"
        // is 2 m/s shows a walk, one whose "walk" is 4.5 shows the jog it really is, and the run state (above)
        // keeps its own hysteresis, so nothing flickers between the two.
        m_runWant = HumanoidMotion::JogWeight(moveSpeed / RigScale());
        m_haveRunWant = true;
    }
    m_stanceTarget = l.prone ? 2 : (l.crouch ? 1 : 0);
    m_stanceLevel = l.stance >= 0.0f ? l.stance : (float)m_stanceTarget;
    if (m_poseSource && m_poseSource->Live()) {   // the Animator's graph picks the state from these
        PoseSource::Movement m;
        m.speed = hs; m.run = l.swim ? false : run; m.crouch = l.swim ? false : l.crouch; m.prone = l.swim ? false : l.prone;
        m.stance = l.swim ? 0.0f : m_stanceLevel;
        m.airTime = m_airT; m.verticalSpeed = l.velocity.y;
        m.swim = l.swim;
        m_poseSource->Move(m);
        return;
    }
    const bool wasMoving = anim == 2 || anim == 3 || anim == 17;
    const bool moving = hs > (wasMoving ? 0.2f : 0.35f);       // hysteresis: no idle/walk flicker
    if (l.swim)                    anim = moving ? 2 : 1;   // no swim clip yet: paddle with the walk, tread with the idle
    else if (l.prone)              anim = 7;
    else if (l.jumped)             anim = 5;
    else if (m_airT > 0.12f)       anim = l.velocity.y > 0.4f ? 5 : 18;   // short drops keep the stride
    else if (l.crouch)             anim = moving ? 17 : 6;
    else if (!moving)              anim = 1;
    else                           anim = run ? 3 : 2;
}

std::vector<Vec3> Character::PoseAt(float t) const { return PoseFor(anim, t); }

std::vector<Vec3> Character::PoseFor(int state, float t) const {
    std::vector<Vec3> r(B_COUNT, Vec3{0, 0, 0});
    if (state == 0) {
        for (int i = 0; i < B_COUNT && i < (int)pose.size(); ++i) r[i] = pose[i];
        return r;
    }
    HumanoidMotion::Pose(state, t, MotionInputs(state, t), r);
    ApplyLookAndLean(r);
    return r;
}

// Head look: layer the (eased) gaze on top of whatever the animation set, so the
// head turns and tilts toward where the player is looking. Clamped so the neck
// never breaks. (The body is flipped 180° about Y in Apply(), which negates the
// pitch sense — hence the minus on X.)
void Character::ApplyLookAndLean(std::vector<Vec3>& r) const {
    r[B_HEAD].x += -Mathf::Clamp(m_headPitch, -55.0f, 55.0f);
    r[B_HEAD].y +=  Mathf::Clamp(m_headYaw,   -72.0f, 72.0f);
    // Body lean (peek): roll the torso sideways toward the lean direction.
    // Splitting a little onto the hips makes it read as a whole-body lean rather
    // than just a bent waist. (bodyLean > 0 = lean to the player's right, e.g. the
    // 'E' key; this rolls the body that way to match.)
    float lean = Mathf::Clamp(m_bodyLean, -40.0f, 40.0f);
    r[B_TORSO].z += lean * 0.7f;
    r[B_HIPS].z  += lean * 0.3f;
    // Speeding up tips the chest forward; a turn rolls it into the curve.
    r[B_TORSO].x += m_motionPitch * 0.7f;
    r[B_HIPS].x  += m_motionPitch * 0.3f;
    r[B_TORSO].z += m_motionRoll * 0.7f;
    r[B_HIPS].z  += m_motionRoll * 0.3f;
    // Takeoff crouch: a short knee dip on top of the jump, which has already started.
    if (m_jumpCrouchW > 0.0f && jumpCrouchBend > 0.0f) {
        const float bend = jumpCrouchBend * m_jumpCrouchW;
        r[B_LTHIGH].x += bend;       r[B_RTHIGH].x += bend;
        r[B_LSHIN].x  -= bend * 1.3f; r[B_RSHIN].x  -= bend * 1.3f;
    }
    // One foot lifts through a turn-in-place step.
    if (m_stepT >= 0.0f && turnStepLift > 0.0f) {
        const float lift = turnStepLift * std::sin(m_stepT * 3.14159265f);
        if (m_stepLeg == 0) { r[B_LTHIGH].x += lift; r[B_LSHIN].x -= lift * 0.6f; }
        else                { r[B_RTHIGH].x += lift; r[B_RSHIN].x -= lift * 0.6f; }
    }
}

void Character::TickFeel(float dt) {
    if (jumpCrouchTime > 1e-4f && m_anticipation > 0.0f) {
        const float u = std::min(1.0f, m_anticipation / jumpCrouchTime);
        m_jumpCrouchW = std::sin(u * 1.5707963f);   // 1 at the takeoff, 0 as it ends
        m_anticipation = std::max(0.0f, m_anticipation - dt);
        if (m_anticipation <= 0.0f) m_jumpCrouchW = 0.0f;
    } else {
        m_jumpCrouchW = 0.0f;
    }
    if (m_stepT >= 0.0f) {
        m_stepT += dt / std::max(turnStepTime, 1e-3f);
        if (gameObject)
            if (auto* fik = gameObject->GetComponent<FootIK>())
                fik->ReleaseLocks(std::max(dt, 0.02f) + 0.02f);
        if (m_stepT >= 1.0f) m_stepT = -1.0f;
    }
    UpdateMotionLean(dt);
}

void Character::UpdateMotionLean(float dt) {
    auto ease = [&](float& v, float want) {
        const float e = dt > 1e-5f ? 1.0f - std::exp(-dt / 0.1f) : 1.0f;
        v += (want - v) * e;
    };
    Transform* t = gameObject ? gameObject->transform : nullptr;
    if (!t || dt <= 1e-5f || motionLean <= 0.0f) {
        ease(m_motionPitch, 0.0f);
        ease(m_motionRoll, 0.0f);
        return;
    }
    const Vec3 p = t->Position();
    if (m_haveMotionLean) {
        Vec3 d = p - m_leanPos; d.y = 0.0f;
        Vec3 v = d.SqrMagnitude() > 4.0f ? m_leanVel : d * (1.0f / dt);
        const Vec3 vPrev = m_leanVel;
        m_leanVel = m_leanVel + (v - m_leanVel) * (1.0f - std::exp(-dt / 0.08f));
        const Vec3 a = (m_leanVel - vPrev) * (1.0f / dt);
        m_leanAcc = m_leanAcc + (a - m_leanAcc) * (1.0f - std::exp(-dt / 0.15f));
    }
    m_leanPos = p; m_haveMotionLean = true;
    const Quat q = t->Rotation();
    const Vec3 fwd = q * Vec3{0.0f, 0.0f, -1.0f}, right = q * Vec3{1.0f, 0.0f, 0.0f};
    const float along = Vec3::Dot(m_leanAcc, fwd), across = Vec3::Dot(m_leanAcc, right);
    auto cap = [](float v, float m) { return v > m ? m : (v < -m ? -m : v); };
    const float moving = std::fmin(1.0f, std::sqrt(m_leanVel.x * m_leanVel.x + m_leanVel.z * m_leanVel.z) / 0.8f);
    const float k = motionLean;
    const float capD = motionLeanMax > 0.0f ? motionLeanMax : 0.0f;
    ease(m_motionPitch, cap(along * 0.9f, capD) * k);
    ease(m_motionRoll,  cap(across * 1.2f, capD) * k * moving);
}

bool Character::AnimatorSample(std::vector<Vec3>& pose, Vec3& root) const {
    if (!AnimatorDriven() || !m_poseSource->AnimatorPose(pose, root)) return false;
    pose.resize(B_COUNT, Vec3{0, 0, 0});
    // Landing on the move: the stride gives at the knees (a standing landing
    // plays the Land state instead).
    if (m_landK > 0.0f && (anim == 2 || anim == 3)) root.y += HumanoidMotion::AbsorbLanding(pose, m_landK);
    return true;
}

Vec3 Character::StanceOffset() const {
    if (m_editorPosing) return m_editorOffset;        // a previewed pose carries its own
    {   // the Animator's clips carry the body's offset in their root track
        std::vector<Vec3> p; Vec3 root;
        if (AnimatorSample(p, root)) return root * height;
    }
    Vec3 o = RawStanceOffset(animTime);
    if (m_blendT < 1.0f && (m_blendFromAnim >= 0 || !m_blendFrom.empty())) {   // ease the hip height with the pose crossfade
        float w = m_blendT * m_blendT * (3.0f - 2.0f * m_blendT);
        float from = m_blendFromAnim >= 0 ? RawStanceOffsetFor(m_blendFromAnim, animTime).y : m_blendFromY;
        o.y = from + (o.y - from) * w;
    }
    return o;
}

Vec3 Character::RawStanceOffset(float t) const { return RawStanceOffsetFor(anim, t); }

Vec3 Character::RawStanceOffsetFor(int state, float t) const {
    return {0.0f, HumanoidMotion::HipY(state, MotionInputs(state, t)) * height, 0.0f};   // rig units scale with height
}

std::vector<Vec3> Character::PosedJoints(const std::vector<Vec3>& pose) const {
    std::vector<Bone> bones = Skeleton();
    const int n = (int)bones.size();
    std::vector<Mat4> poseW(n);
    std::vector<Vec3> out(n);
    for (int i = 0; i < n; ++i) {
        Vec3 jp = bones[i].parent >= 0 ? bones[bones[i].parent].joint : Vec3{0, 0, 0};
        Mat4 posed = Mat4::Translate(bones[i].joint - jp) *
                     Mat4::Rotate(Quat::Euler(i < (int)pose.size() ? pose[i] : Vec3{0, 0, 0}));
        poseW[i] = bones[i].parent >= 0 ? poseW[bones[i].parent] * posed : posed;
        out[i] = poseW[i].MultiplyPoint(Vec3{0, 0, 0});
    }
    return out;
}

void Character::Skin(Mesh& m, const std::vector<int>& bone, const std::vector<Vec3>& rot) const {
    std::vector<Bone> bones = Skeleton();
    int n = (int)bones.size();
    std::vector<Mat4> restW(n), poseW(n), skin(n);
    for (int i = 0; i < n; ++i) {
        Vec3 jp = bones[i].parent >= 0 ? bones[bones[i].parent].joint : Vec3{0, 0, 0};
        Vec3 local = bones[i].joint - jp;
        Mat4 rest = Mat4::Translate(local);
        Mat4 posed = Mat4::Translate(local) * Mat4::Rotate(Quat::Euler(i < (int)rot.size() ? rot[i] : Vec3{0, 0, 0}));
        if (bones[i].parent >= 0) { restW[i] = restW[bones[i].parent] * rest; poseW[i] = poseW[bones[i].parent] * posed; }
        else { restW[i] = rest; poseW[i] = posed; }
    }
    for (int i = 0; i < n; ++i) skin[i] = poseW[i] * restW[i].Inverse();
    for (std::size_t k = 0; k < m.vertices.size() && k < bone.size() && k < m_restPos.size(); ++k)
        m.vertices[k] = skin[bone[k]].MultiplyPoint(m_restPos[k]);
}

void Character::Apply() {
    if (!gameObject) return;
    // External-rig mode renders nothing of its own — keep the baked mesh off
    // (Start() calls Apply, which would otherwise flash the blocky body).
    if (driveExternal) {
        if (auto* emr = gameObject->GetComponent<MeshRenderer>()) emr->enabled = false;
        return;
    }
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    m_built = false; EnsureRest();
    Mesh m = m_rest;
    Skin(m, m_bone, PoseAt(animTime));
    // Face -Z: the engine's cameras look down -Z and the controllers move along
    // -Z, so the body (modelled facing +Z) is turned 180° about Y to match — this
    // is what stops the walk looking backwards (moonwalk). StanceOffset lowers the
    // whole body for crouch / prone (the rig's root is otherwise pinned at the hip).
    Vec3 so = StanceOffset();
    FaceMinusZ(m);
    for (Vec3& v : m.vertices) { v = v * height; v += so; }
    m.normals.clear();                     // boxy -> flat shading
    mr->mesh = std::move(m);
    mr->doubleSided = false;               // outward faces only: no view into the cavity
}

void Character::AddClip(AnimClip clip) {
    std::string name = clip.name;
    m_clips[name] = std::move(clip);
    ++m_clipsVer;
    if (m_activeClipName == name) m_activeClip = &m_clips[name];  // keep pointer valid on replace
}

int Character::LoadClips(const std::string& text, std::string* error, bool replaceExisting) {
    auto clips = AnimClip::ParseAll(text, &Character::BoneIndex, B_COUNT, error);
    for (auto& c : clips)
        if (replaceExisting || !HasClip(c.name)) AddClip(std::move(c));
    return (int)clips.size();
}

bool Character::LoadClipsFromFile(const std::string& path, std::string* error, bool replaceExisting) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return LoadClips(ss.str(), error, replaceExisting) > 0;
}

static std::string& ClipsBaseDir() { static std::string d; return d; }
void Character::SetAssetBaseDir(const std::string& dir) { ClipsBaseDir() = dir; }
const std::string& Character::AssetBaseDir() { return ClipsBaseDir(); }

std::string Character::ResolvedClipsFile() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(clipsFile);
    if (clipsFile.empty() || p.is_absolute() || ClipsBaseDir().empty() || fs::exists(p, ec)) return clipsFile;
    return (fs::path(ClipsBaseDir()) / p).string();
}

bool Character::SaveClipsToFile(const std::string& path, std::string* error) const {
    std::vector<AnimClip> clips;
    clips.reserve(m_clips.size());
    for (const auto& kv : m_clips) clips.push_back(kv.second);
    std::string text = AnimClip::WriteAll(clips, [](int i) { return std::string(BoneToken(i)); });
    std::ofstream f(path, std::ios::trunc);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    f << text;
    return (bool)f;
}

bool Character::PlayClip(const std::string& name) {
    if (name.empty()) { StopClip(); return true; }
    auto it = m_clips.find(name);
    if (it == m_clips.end()) return false;
    if (m_activeClipName != name) BeginBlend();   // crossfade from the current pose
    m_activeClip = &it->second;
    m_activeClipName = name;
    m_clipTime = 0.0f;
    return true;
}

void Character::StopClip() {
    if (m_activeClip) BeginBlend();               // crossfade back to the built-in animation
    m_activeClip = nullptr;
    m_activeClipName.clear();
    m_clipTime = 0.0f;
}

void Character::BeginBlend() {
    if (blendTime <= 1e-4f) { m_blendT = 1.0f; return; }   // instant
    m_blendFrom = CurrentSourcePose();                     // freeze where we are right now
    m_blendFromY = RawStanceOffset(animTime).y;
    m_blendFromAnim = -1;
    m_blendT = 0.0f;
    m_blendDur = blendTime;
}

void Character::FireClipEvents(float from, float to) {
    if (!m_activeClip || m_activeClip->events.empty() || to <= from) return;
    auto inRange = [&](float a, float b) {
        for (const auto& e : m_activeClip->events)
            if (e.time > a && e.time <= b && !e.name.empty()) {
                m_animEvents.push_back(e.name);
                if (onAnimEvent) onAnimEvent(e.name);
            }
    };
    float dur = m_activeClip->Duration();
    if (m_activeClip->loop && dur > 0.0f && to > dur) {
        inRange(from, dur);                       // up to the loop point
        float wrapped = std::fmod(to, dur);
        inRange(-1e-6f, wrapped);                 // and from the start after wrapping
    } else {
        inRange(from, to);
    }
}

void Character::AdvanceClip(float dt) {
    if (!m_activeClip) return;
    float from = m_clipTime;
    float dur = m_activeClip->Duration();
    // The jump binding holds its last frame while the body is still in that
    // state. A looping takeoff clip used to replay for the whole jump.
    const bool holdJump = m_stateDriven && !clipJump.empty() && m_activeClipName == clipJump;
    const float add = dt * animSpeed * m_activeClip->speed;
    m_clipTime = (holdJump && dur > 0.0f) ? std::min(from + add, dur) : from + add;
    FireClipEvents(from, m_clipTime);             // events on the raw (pre-wrap) timeline
    if (dur > 0.0f) {
        if (m_activeClip->loop && !holdJump) m_clipTime = std::fmod(m_clipTime, dur);
        else if (m_clipTime > dur) m_clipTime = dur;
    }
}

bool Character::RemoveClip(const std::string& name) {
    auto it = m_clips.find(name);
    if (it == m_clips.end()) return false;
    if (m_activeClipName == name) StopClip();   // before the erase: the pointer goes with it
    if (m_layerName == name) StopLayer();
    m_clips.erase(it);
    ++m_clipsVer;
    for (std::string* b : {&clipIdle, &clipWalk, &clipRun, &clipJump, &clipFall, &clipCrouch, &clipDive, &autoPlayClip})
        if (*b == name) b->clear();
    return true;
}

bool Character::RenameClip(const std::string& from, const std::string& to) {
    auto it = m_clips.find(from);
    if (it == m_clips.end() || to.empty() || from == to || m_clips.count(to)) return false;
    AnimClip c = std::move(it->second);
    m_clips.erase(it);
    c.name = to;
    AnimClip& stored = m_clips[to];
    stored = std::move(c);
    if (m_activeClipName == from) { m_activeClipName = to; m_activeClip = &stored; }
    if (m_layerName == from)      { m_layerName = to; m_layerClip = &stored; }
    for (std::string* b : {&clipIdle, &clipWalk, &clipRun, &clipJump, &clipFall, &clipCrouch, &clipDive, &autoPlayClip})
        if (*b == from) *b = to;
    for (BlendStop& st : m_blendStops) if (st.clip == from) st.clip = to;
    ++m_clipsVer;
    return true;
}

std::vector<std::string> Character::ClipNames() const {
    std::vector<std::string> names;
    names.reserve(m_clips.size());
    for (const auto& kv : m_clips) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    return names;
}

void Character::SyncStateClips() {
    if (m_poseSource) return;     // the Animator's states pick the clips
    if (m_blendTreeOn) return;    // the locomotion blend tree owns the base pose
    if (m_editorPosing) return;   // editor preview owns the pose
    // A manual clip (PlayClip / autoPlayClip) gives way to movement: a looping one
    // stops as soon as the character starts moving or jumps (the state changes into
    // walk/run/jump/fall while it plays), a one-shot once it has finished, so an
    // authored flourish never freezes the player in place. A script that plays a
    // clip while already running keeps it until the state changes. A clip flagged
    // overrideMovement keeps the body until it's stopped explicitly.
    const bool moving = anim == 2 || anim == 3 || anim == 5 || anim == 17 || anim == 18;
    const bool stateChanged = m_syncAnim >= 0 && anim != m_syncAnim;
    m_syncAnim = anim;
    if (m_activeClip && !m_stateDriven) {
        if (m_activeClip->overrideMovement) return;
        const bool finished = !m_activeClip->loop && m_clipTime >= m_activeClip->Duration();
        if (moving && (stateChanged || finished)) StopClip();
    }
    // No-code: when the user has bound a clip to a movement state, make that clip the
    // active one (so YOUR animation plays instead of the built-in for that state).
    const std::string* want = nullptr;
    switch (anim) {
        case 1:  want = &clipIdle; break;
        case 2:  want = &clipWalk; break;
        case 3:  want = &clipRun; break;
        case 5:  want = &clipJump; break;
        case 18: want = &clipFall; break;
        case 6:  want = &clipCrouch; break;
        default: break;
    }
    if (want && want->empty()) want = nullptr;
    if (want) {
        if (m_activeClipName != *want && m_clips.count(*want)) { PlayClip(*want); m_stateDriven = true; }
        else if (m_activeClipName == *want) m_stateDriven = true;
    } else if (m_stateDriven && m_activeClip) {
        // Entered a state with no binding: hand back to the built-in animation. (Only
        // when WE put the clip there — never stomp a manual PlayClip / autoPlayClip.)
        StopClip();
        m_stateDriven = false;
    }
}

void Character::NoteStateChange() {
    if (m_animHold > 0) anim = m_animHold;   // a script's held animation wins over the controller's pick
    // Landing recovery: coming out of the jump pose into any grounded state plays a
    // brief knee-bend "absorb" (sin arc in PoseAt/StanceOffset) so a landing has
    // weight to it instead of the legs snapping straight the frame you touch down.
    if (anim != m_prevAnim) {
        if ((m_prevAnim == 5 || m_prevAnim == 18) && (anim == 1 || anim == 2 || anim == 3)) m_landT = 0.0f;
        // Crossfade between the built-in animations. The Animator's table times
        // each pair (idle <-> walk longer than a clip switch, walk <-> run included,
        // air <-> ground each way). A blend already on screen is captured first,
        // before the stride phase is touched, so reversing mid-way continues from
        // that pose instead of the state we left.
        const bool reversing = m_blendT < 1.0f;
        if (!m_activeClip && !m_blendTreeOn && !m_editorPosing && m_prevAnim != 0 && anim != 0) {
            const float secs = CrossfadeSeconds(m_prevAnim, anim);
            if (secs <= 1e-4f) { m_blendT = 1.0f; m_blendFromAnim = -1; }
            else {
                CaptureLeavingPose();
                m_blendT = 0.0f;
                m_blendDur = secs;
            }
        }
        auto isGait = [](int a) { return a == 2 || a == 3 || a == 17; };
        if (!reversing && isGait(anim) && !isGait(m_prevAnim)) {
            // Stepping off: a landing comes down on the left foot (phase 0); from a
            // stand, start with the left foot planted under the hips and the right
            // one swinging through. A blend already playing keeps its phase.
            m_runW = anim == 3 ? 1.0f : 0.0f;
            m_gaitPhase = (m_prevAnim == 5 || m_prevAnim == 18) ? 0.0f
                        : HumanoidMotion::StartPhase(anim, m_gaitSpeed);
        }
        m_prevAnim = anim;
    }
}

float Character::CrossfadeSeconds(int from, int to) const {
    GaitCrossfades g;
    StanceTimes st;
    if (gameObject)
        for (AnimStateMachine* sm : gameObject->GetComponents<AnimStateMachine>())
            if (sm->layer == 0) { g = sm->crossfades; st = sm->stanceTimes; break; }
    auto air = [](int a) { return a == 5 || a == 18; };
    auto stand = [](int a) { return a == 1 || a == 2 || a == 3; };
    auto crouch = [](int a) { return a == 6 || a == 17; };
    auto prone = [](int a) { return a == 7; };
    if (air(from) && to != 0 && !air(to)) return g.airGround;
    if (from != 0 && !air(from) && air(to)) return g.groundAir;
    if ((from == 2 && to == 3) || (from == 3 && to == 2)) return g.walkRun;
    if ((from == 1 && to == 2) || (from == 2 && to == 1)) return g.idleWalk;
    if ((from == 1 && to == 3) || (from == 3 && to == 1)) return g.walkRun;
    if ((stand(from) && crouch(to)) || (crouch(from) && stand(to))) return st.standCrouch;
    if ((crouch(from) && prone(to)) || (prone(from) && crouch(to))) return st.crouchProne;
    if (prone(from) && stand(to)) return st.proneStand;
    if (stand(from) && prone(to)) return st.standCrouch + st.crouchProne;
    return blendTime;
}

void Character::CaptureLeavingPose() {
    // `anim` is already the state being entered. Sample the one on screen, whose
    // walk/run style follows m_runW only while it is the live state.
    const int entered = anim;
    anim = m_prevAnim;
    std::vector<Vec3> shown = PoseFor(m_prevAnim, animTime);
    const float toY = RawStanceOffset(animTime).y;
    if (m_blendT < 1.0f) {
        std::vector<Vec3> from = m_blendFromAnim >= 0 ? PoseFor(m_blendFromAnim, animTime) : m_blendFrom;
        const float u = std::min(std::max(m_blendT, 0.0f), 1.0f);
        const float w = u * u * (3.0f - 2.0f * u);
        if (from.size() == shown.size())
            for (std::size_t i = 0; i < shown.size(); ++i) shown[i] = from[i] + (shown[i] - from[i]) * w;
        const float fromY = m_blendFromAnim >= 0 ? RawStanceOffsetFor(m_blendFromAnim, animTime).y : m_blendFromY;
        m_blendFromY = fromY + (toY - fromY) * w;
        m_blendFrom = std::move(shown);
        m_blendFromAnim = -1;
    } else {
        m_blendFromAnim = m_prevAnim;
    }
    anim = entered;
}
} // namespace okay
