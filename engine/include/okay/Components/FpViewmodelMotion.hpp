#pragma once
// ---------------------------------------------------------------------------
// FpViewmodelMotion — the "alive" layer for your own hands in first person.
//
// A small, pure model (no scene, no camera, no input): you feed it what the body is doing this frame
// (FpMotionInput) and it gives back one camera-space offset and rotation (FpMotionPose) that Character
// applies to the whole viewmodel (arm, forearm, hand and the held item together, so the item bob follows the
// hands). Because it is pure, the tests step it for thousands of frames and check that nothing jumps.
//
// What it layers (each is eased by its own critically damped spring, so a state change never pops):
//   idle breathing, walk / run bob and arm swing locked to the stride phase, sprint pose, crouch lowering,
//   jump lift and landing dip, strafe tilt, aim-down-sights pull to the centre, grip pose per held-item kind,
//   wall pull-back (lower and tilt when a surface is close), and one-shot named events (equip, unequip,
//   use, swing, fire, reload) that scripts and the sandbox dig / place actions can trigger.
//
// Reduce motion (Accessibility): ambient motion (breathing, bob, swing, strafe tilt, air float) goes to zero and
// the one-shot kicks shrink to a third. Poses that tell you a state (sprint, crouch, grip, aim, wall) stay,
// because they are not shaking, they are the hand moving to where it belongs.
//
// Written from this engine's own player code and the general ideas of springs and easing; no code, constants or
// tests from any other project.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include "okay/Components/HoldableObject.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace okay {

/// How the hands hold what they hold. Picked from the HoldableObject's style (see FpGripKindFor).
enum class FpGripKind { Empty = 0, Tool, OneHand, TwoHand, Pistol, Rifle };
inline const char* FpGripName(FpGripKind k) {
    switch (k) {
        case FpGripKind::Empty:   return "empty hand";
        case FpGripKind::Tool:    return "tool";
        case FpGripKind::OneHand: return "one-handed";
        case FpGripKind::TwoHand: return "two-handed";
        case FpGripKind::Pistol:  return "pistol";
        case FpGripKind::Rifle:   return "rifle";
    }
    return "?";
}
/// Nothing held = Empty. Rifle, Pistol, Flashlight / Sword / Cup (tools) and the two-handed styles keep their
/// own pose; anything else is one hand, or two when the object wants both.
inline FpGripKind FpGripKindFor(const HoldableObject* ho) {
    if (!ho) return FpGripKind::Empty;
    using S = HoldableObject::Style;
    const S s = ho->ResolvedStyle();
    if (s == S::Rifle) return FpGripKind::Rifle;
    if (s == S::Pistol) return FpGripKind::Pistol;   // a support hand is the second hand, the pose is the same
    if (s == S::Flashlight || s == S::Sword || s == S::Cup) return ho->WantsTwoHands() ? FpGripKind::TwoHand : FpGripKind::Tool;
    return ho->WantsTwoHands() ? FpGripKind::TwoHand : FpGripKind::OneHand;
}

/// The named one-shot animations. Names are what scripts pass (case does not matter).
enum class FpEventKind { None = 0, Equip, Unequip, Use, Swing, Fire, Reload };
inline const char* FpEventName(FpEventKind k) {
    switch (k) {
        case FpEventKind::Equip:   return "equip";
        case FpEventKind::Unequip: return "unequip";
        case FpEventKind::Use:     return "use";
        case FpEventKind::Swing:   return "swing";
        case FpEventKind::Fire:    return "fire";
        case FpEventKind::Reload:  return "reload";
        default:                   return "none";
    }
}
/// "equip" "unequip" "use" "swing" "fire" "reload", plus the aliases the sandbox uses:
/// "dig" and "hit" are a swing, "place" and "interact" are a use, "shoot" is a fire.
inline FpEventKind FpEventFromName(const std::string& name) {
    std::string n;
    for (char c : name) n += (char)std::tolower((unsigned char)c);
    if (n == "equip" || n == "draw" || n == "raise") return FpEventKind::Equip;
    if (n == "unequip" || n == "holster" || n == "lower") return FpEventKind::Unequip;
    if (n == "use" || n == "place" || n == "interact") return FpEventKind::Use;
    if (n == "swing" || n == "dig" || n == "hit" || n == "attack") return FpEventKind::Swing;
    if (n == "fire" || n == "shoot") return FpEventKind::Fire;
    if (n == "reload") return FpEventKind::Reload;
    return FpEventKind::None;
}

/// What the body is doing this frame.
struct FpMotionInput {
    float dt = 0.016f;
    float speed01 = 0.0f;        ///< ground speed over the run speed (0 still .. 1 running; may go a little over)
    float phase = 0.0f;          ///< the stride cycle 0..1 (the character's own gait phase)
    bool  stepping = false;      ///< feet are stepping (walk / run / crouch-walk)
    float sprint = 0.0f;         ///< 0..1 how much the body is in the run style
    bool  airborne = false;      ///< jumping or falling
    bool  rising = false;        ///< airborne and going up
    float landK = 0.0f;          ///< 0..1 landing envelope (0 = not landing)
    float jumpCrouch = 0.0f;     ///< 0..1 takeoff dip
    float stance = 0.0f;         ///< 0 stand .. 1 crouch .. 2 prone
    float strafe = 0.0f;         ///< -1 left .. +1 right, how much the body is moving sideways
    bool  aim = false;           ///< aim down sights held (only pistol / rifle grips use it)
    float wall = 0.0f;           ///< 0..1 how close a surface is in front of the lens
    FpGripKind grip = FpGripKind::Empty;
    float side = 1.0f;           ///< +1 when the primary hand is on the screen's right, -1 on the left
    // Settings
    float bobAmount = 1.0f;      ///< FirstPersonHands.bobAmount
    float swayAmount = 1.0f;     ///< FirstPersonHands.swayAmount (breathing and strafe tilt)
    float punchAmount = 1.0f;    ///< FirstPersonHands.punchAmount (the one-shot kicks)
    bool  reduceMotion = false;  ///< Accessibility reduce motion
};

/// One offset and rotation for the whole viewmodel. Camera space: +x right, +y up, -z away from the lens (so
/// a positive z comes TOWARD the lens). Euler degrees (pitch about x, yaw about y, roll about z).
struct FpMotionPose {
    Vec3 pos{0, 0, 0};
    Vec3 euler{0, 0, 0};
};

class FpViewmodelMotion {
public:
    static constexpr int kSlots = 4;

    /// Advance one frame. Returns the new pose.
    const FpMotionPose& Step(const FpMotionInput& in) {
        float dt = in.dt;
        if (!(dt > 0.0f)) return m_pose;
        if (dt > 0.1f) dt = 0.1f;
        m_time += dt;
        const float rm = in.reduceMotion ? 0.0f : 1.0f;       // ambient motion
        const float kick = in.reduceMotion ? 0.33f : 1.0f;    // one-shot events
        const float side = in.side < 0.0f ? -1.0f : 1.0f;

        // ---- eased state weights ----
        const float moving = in.stepping ? Clamp01(in.speed01 / 0.35f) : 0.0f;
        const float move_ = Ease(MOVE, moving, 9.0f, dt);
        const float still_ = Ease(STILL, 1.0f - Clamp01(in.speed01 / 0.25f), 6.0f, dt);
        const float sprint_ = Ease(SPRINT, Clamp01(in.sprint), 8.0f, dt);
        const float crouch_ = Ease(CROUCH, Clamp01(in.stance), 9.0f, dt);
        const float prone_ = Ease(PRONE, Clamp01(in.stance - 1.0f), 9.0f, dt);
        const float air_ = Ease(AIR, in.airborne ? 1.0f : 0.0f, 9.0f, dt);
        const float rise_ = Ease(RISE, (in.airborne && in.rising) ? 1.0f : 0.0f, 10.0f, dt);
        const float land_ = Ease(LAND, Clamp01(in.landK), 18.0f, dt);
        const float dip_ = Ease(DIP, Clamp01(in.jumpCrouch), 18.0f, dt);
        const float strafe_ = Ease(STRAFE, Clamp11(in.strafe) * (in.stepping ? 1.0f : 0.0f), 7.0f, dt);
        const bool canAim = in.grip == FpGripKind::Pistol || in.grip == FpGripKind::Rifle;
        const float aim_ = Ease(AIM, (in.aim && canAim) ? 1.0f : 0.0f, 12.0f, dt);
        const float wall_ = Ease(WALL, Clamp01(in.wall), 16.0f, dt);
        // The grip pose is eased as numbers, so Rifle -> Empty glides instead of switching.
        float gp[6];
        GripTarget(in.grip, side, gp);
        float grip[6];
        for (int i = 0; i < 6; ++i) grip[i] = Ease(GRIP0 + i, gp[i], 11.0f, dt);

        // ---- idle breathing: slow, small, fades out while moving ----
        const float breathAmp = still_ * rm * in.swayAmount;
        const float br = std::sin(m_time * 6.2831853f * 0.23f);          // about one breath every 4 s
        const float br2 = std::sin(m_time * 6.2831853f * 0.11f + 1.3f);  // a slower drift on top
        Vec3 pos{0.0025f * br2 * breathAmp, 0.0040f * br * breathAmp, 0.0030f * br * breathAmp};
        Vec3 eul{0.35f * br * breathAmp, 0.18f * br2 * breathAmp, 0.22f * br2 * breathAmp};

        // ---- walk / run bob and arm swing, locked to the stride phase ----
        // Two dips per stride (every footfall), one side-to-side per stride. A crouch walk is smaller.
        const float tau = 6.2831853f;
        const float strideW = move_ * rm * in.bobAmount * (1.0f - 0.45f * crouch_) * (1.0f - 0.7f * air_);
        const float run = 1.0f + 0.75f * sprint_;
        const float ph = in.phase;
        const float s1 = std::sin(tau * ph), c1 = std::cos(tau * ph);
        const float s2 = std::sin(2.0f * tau * ph), c2 = std::cos(2.0f * tau * ph);
        const float amp = strideW * (0.35f + 0.65f * Clamp01(in.speed01)) * run;
        pos.y += amp * (-0.0100f * (0.5f - 0.5f * c2) + 0.0030f * s2);
        pos.x += amp * 0.0090f * s1;
        pos.z += amp * 0.0050f * c2;
        eul.x += amp * (0.9f * s2);
        eul.y += amp * (-1.3f * s1);        // the arm swings with the stride
        eul.z += amp * (1.1f * s1) * side;

        // ---- sprint pose: hand pulled in, down and tipped, held steady by the pose weight ----
        pos.x += -side * 0.030f * sprint_;
        pos.y += -0.050f * sprint_;
        pos.z += 0.045f * sprint_;
        eul.x += 14.0f * sprint_;
        eul.y += -side * 10.0f * sprint_;
        eul.z += side * 8.0f * sprint_;

        // ---- crouch / prone lowering (the camera already drops; the hands sit a little lower and closer) ----
        pos.y += -0.020f * crouch_ - 0.030f * prone_;
        pos.z += 0.015f * crouch_ + 0.030f * prone_;
        eul.x += 3.0f * crouch_ + 6.0f * prone_;

        // ---- jump: a dip on takeoff, a lift while airborne, a landing dip ----
        pos.y += -0.025f * dip_ * kick;
        eul.x += 4.0f * dip_ * kick;
        pos.y += (0.012f * rise_ + 0.006f * (air_ - rise_)) * rm;
        eul.x += (-3.0f * rise_ + 1.5f * (air_ - rise_)) * rm;
        pos.y += -0.070f * land_ * kick;
        pos.z += 0.020f * land_ * kick;
        eul.x += 7.0f * land_ * kick;

        // ---- strafe tilt: roll and a little sway into the direction of travel ----
        const float sw = strafe_ * rm * in.swayAmount;
        eul.z += -3.5f * sw;
        pos.x += -0.008f * sw;
        eul.y += 1.5f * sw;

        // ---- grip pose ----
        pos = pos + Vec3{grip[0], grip[1], grip[2]};
        eul = eul + Vec3{grip[3], grip[4], grip[5]};

        // ---- aim down sights: toward the centre and up ----
        pos.x += -side * 0.110f * aim_;
        pos.y += 0.045f * aim_;
        pos.z += -0.040f * aim_;
        eul.x += -2.0f * aim_;
        eul.y += side * 1.5f * aim_;
        eul.z += -side * 3.0f * aim_;

        // ---- wall pull-back: lower, come back toward you and tip up so nothing pokes into the surface ----
        pos.x += side * 0.015f * wall_;
        pos.y += -0.110f * wall_;
        pos.z += 0.090f * wall_;
        eul.x += 26.0f * wall_;
        eul.z += side * 7.0f * wall_;

        // ---- one-shot named events ----
        for (Slot& s : m_slot) {
            if (s.kind == FpEventKind::None) continue;
            const EventDef& d = Def(s.kind);
            s.t += dt / d.seconds;
            if (s.t >= 1.0f) { s.kind = FpEventKind::None; continue; }
            const float e = Envelope(d, s.t) * s.strength * kick * in.punchAmount;
            pos = pos + Vec3{d.pos.x * side, d.pos.y, d.pos.z} * e;
            eul = eul + Vec3{d.rot.x, d.rot.y * side, d.rot.z * side} * e;
        }

        m_aimW = aim_; m_wallW = wall_; m_sprintW = sprint_;
        for (int i = 0; i < 6; ++i) m_gripW[i] = grip[i];
        m_pose.pos = pos;
        m_pose.euler = eul;
        return m_pose;
    }

    /// Start a named animation. The same one already playing is not restarted (it keeps playing), and a
    /// different one plays on top. Returns false for an unknown name.
    bool Trigger(const std::string& name, float strength = 1.0f) { return Trigger(FpEventFromName(name), strength); }
    bool Trigger(FpEventKind k, float strength = 1.0f) {
        if (k == FpEventKind::None) return false;
        for (Slot& s : m_slot)
            if (s.kind == k) { s.strength = std::max(s.strength, strength); return true; }
        for (Slot& s : m_slot)
            if (s.kind == FpEventKind::None) { s.kind = k; s.t = 0.0f; s.strength = strength; ++m_count; return true; }
        // All slots busy: the oldest (furthest along) is replaced where it is nearly over.
        Slot* best = &m_slot[0];
        for (Slot& s : m_slot) if (s.t > best->t) best = &s;
        if (best->t < 0.7f) return true;      // everything is mid-play: drop it rather than jump
        best->kind = k; best->t = 0.0f; best->strength = strength; ++m_count;
        return true;
    }
    bool Playing(FpEventKind k) const {
        for (const Slot& s : m_slot) if (s.kind == k) return true;
        return false;
    }
    bool AnyPlaying() const {
        for (const Slot& s : m_slot) if (s.kind != FpEventKind::None) return true;
        return false;
    }
    /// How many events were ever started (a test and a script can read it).
    int Started() const { return m_count; }
    const FpMotionPose& Pose() const { return m_pose; }
    /// The eased grip weights (for tests): x y z pitch yaw roll.
    const float* GripPose() const { return m_gripW; }
    float AimWeight() const { return m_aimW; }
    float WallWeight() const { return m_wallW; }
    float SprintWeight() const { return m_sprintW; }
    void Reset() { *this = FpViewmodelMotion(); }

    /// Seconds an event plays for (a script can wait on it).
    static float Seconds(FpEventKind k) { return Def(k).seconds; }

private:
    struct EventDef { Vec3 pos; Vec3 rot; float seconds; float attack; float hold; };
    static const EventDef& Def(FpEventKind k) {
        // pos: metres (x is mirrored for a left-hand primary); rot: degrees. attack / hold are fractions of the time.
        static const EventDef none{{0, 0, 0}, {0, 0, 0}, 0.2f, 0.5f, 0.0f};
        static const EventDef equip{{0.0f, -0.120f, 0.040f}, {22.0f, 0.0f, 0.0f}, 0.55f, 0.18f, 0.0f};
        static const EventDef unequip{{0.0f, -0.140f, 0.050f}, {26.0f, 0.0f, 0.0f}, 0.40f, 0.45f, 0.0f};
        static const EventDef use{{0.0f, -0.020f, -0.060f}, {-7.0f, 0.0f, 0.0f}, 0.32f, 0.30f, 0.0f};
        static const EventDef swing{{-0.130f, -0.050f, -0.090f}, {24.0f, -22.0f, 14.0f}, 0.46f, 0.38f, 0.0f};
        static const EventDef fire{{0.0f, 0.014f, 0.055f}, {-4.5f, 0.0f, 0.8f}, 0.20f, 0.28f, 0.0f};
        static const EventDef reload{{0.020f, -0.150f, 0.050f}, {28.0f, 0.0f, 10.0f}, 1.60f, 0.15f, 0.65f};
        switch (k) {
            case FpEventKind::Equip: return equip;
            case FpEventKind::Unequip: return unequip;
            case FpEventKind::Use: return use;
            case FpEventKind::Swing: return swing;
            case FpEventKind::Fire: return fire;
            case FpEventKind::Reload: return reload;
            default: return none;
        }
    }
    struct Slot { FpEventKind kind = FpEventKind::None; float t = 0.0f; float strength = 1.0f; };

    static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float Clamp11(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    static float Smooth(float x) { x = Clamp01(x); return x * x * (3.0f - 2.0f * x); }
    /// 0 -> 1 over `attack`, flat for `hold`, 1 -> 0 over the rest. Zero slope at both ends.
    static float Envelope(const EventDef& d, float t) {
        if (t < d.attack) return Smooth(t / d.attack);
        if (t < d.attack + d.hold) return 1.0f;
        const float rest = 1.0f - d.attack - d.hold;
        return rest > 1e-4f ? Smooth(1.0f - (t - d.attack - d.hold) / rest) : 0.0f;
    }
    /// Critically damped spring toward a target. Each state has one value and one velocity, so the value and its
    /// slope are continuous: a changed target bends the curve, it never steps it.
    struct Sp { float x = 0.0f; float v = 0.0f; };
    static void EaseSp(Sp& s, float target, float omega, float dt) {
        const float d = s.x - target;
        const float e = std::exp(-omega * dt);
        const float t = (s.v + omega * d) * dt;
        s.x = target + (d + t) * e;
        s.v = (s.v - omega * t) * e;
    }
    enum { MOVE, STILL, SPRINT, CROUCH, PRONE, AIR, RISE, LAND, DIP, STRAFE, AIM, WALL, GRIP0, N = GRIP0 + 6 };
    float Ease(int i, float target, float omega, float dt) {
        if (!m_init) { for (int k = 0; k < N; ++k) m_sp[k] = Sp{}; m_sp[STILL].x = 1.0f; m_init = true; }
        EaseSp(m_sp[i], target, omega, dt);
        return m_sp[i].x;
    }
    static void GripTarget(FpGripKind k, float side, float out[6]) {
        // x y z (camera space, metres) then pitch yaw roll (degrees), relative to the plain hand.
        float v[6] = {0, 0, 0, 0, 0, 0};
        switch (k) {
            case FpGripKind::Empty:   break;
            case FpGripKind::Tool:    v[0] = -0.030f * side; v[1] = 0.030f; v[2] = -0.020f; v[3] = -10.0f; v[5] = 6.0f * side; break;
            case FpGripKind::OneHand: v[3] = -4.0f; break;
            case FpGripKind::TwoHand: v[0] = -0.040f * side; v[1] = 0.040f; v[2] = -0.030f; v[3] = -6.0f; break;
            case FpGripKind::Pistol:  v[0] = -0.070f * side; v[1] = 0.060f; v[2] = -0.050f; v[3] = -10.0f; v[4] = 4.0f * side; break;
            case FpGripKind::Rifle:   v[0] = -0.090f * side; v[1] = 0.050f; v[2] = -0.080f; v[3] = -6.0f; v[4] = 6.0f * side; break;
        }
        for (int i = 0; i < 6; ++i) out[i] = v[i];
    }

    Sp m_sp[N];
    bool m_init = false;
    float m_aimW = 0, m_wallW = 0, m_sprintW = 0, m_gripW[6] = {0, 0, 0, 0, 0, 0};
    float m_time = 0.0f;
    int   m_count = 0;
    Slot  m_slot[kSlots];
    FpMotionPose m_pose;
};

} // namespace okay
