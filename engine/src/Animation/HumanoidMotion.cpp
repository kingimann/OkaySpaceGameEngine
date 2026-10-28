#include "okay/Animation/HumanoidMotion.hpp"
#include <cmath>

namespace okay {
namespace HumanoidMotion {

// Rig conventions (measured on this skeleton): the rig faces +Z before the 180°
// display flip. On a bone hanging DOWN (thigh, shin, upper arm, forearm) +X swings
// its far end BACKWARD; on a bone pointing UP (torso, head) +X tips it FORWARD. So
// a knee or elbow bends naturally with shin +X / forearm -X, and +X on a foot
// points the toes down. Upper-arm Z is not a mirror: +Z pulls the LEFT arm in
// toward the ribs and spreads the RIGHT arm out. Leg: hip joint 0.92 above the
// soles, knee 0.55, ankle 0.12.
namespace {
constexpr float kThigh = 0.37f, kShin = 0.43f, kHipToAnkle = 0.80f, kAnkleH = 0.12f;
constexpr float kPi = 3.14159265f, kRad2Deg = 57.2957795f;
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float frac(float v) { return v - std::floor(v); }
float smooth01(float u) { u = clampf(u, 0.0f, 1.0f); return u * u * (3.0f - 2.0f * u); }

// Two-bone leg IK in the hips' frame. The ankle goes `fwd` metres ahead of the hip
// joint, `down` below it and `side` out to the rig's +X; `pitch` is the foot's
// angle to the ground (deg, + = toes down). The knee always bends forward.
void LegIK(float fwd, float down, float side, float pitch, Vec3& thigh, Vec3& shin, Vec3& foot) {
    const float a = kThigh, b = kShin;
    float dz = std::sqrt(down * down + side * side);
    float abd = std::atan2(side, down);
    float d = clampf(std::sqrt(fwd * fwd + dz * dz), 0.12f, (a + b) * 0.9995f);
    float phi = std::atan2(fwd, dz);                                         // + = ankle ahead of the hip
    float alpha = std::acos(clampf((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f));
    float gamma = std::acos(clampf((a * a + b * b - d * d) / (2.0f * a * b), -1.0f, 1.0f));
    float tx = -(phi + alpha) * kRad2Deg;                                    // thigh forward (knee leads)
    float kx = (kPi - gamma) * kRad2Deg;                                     // knee flexion
    thigh = {tx, 0.0f, abd * kRad2Deg};
    shin  = {kx, 0.0f, 0.0f};
    foot  = {pitch - (tx + kx), 0.0f, 0.0f};                                 // world foot angle = pitch
}
// Ankle height above the ground for a foot pitched by `pitch` degrees with the
// toe (0.2 ahead) or heel (0.1 behind) as the contact point.
float AnkleLiftForPitch(float pitch) {
    float r = pitch * (kPi / 180.0f);
    return r >= 0.0f ? kAnkleH * std::cos(r) + 0.20f * std::sin(r) - kAnkleH
                     : kAnkleH * std::cos(r) - 0.10f * std::sin(r) - kAnkleH;
}

// One stride layout. Distances are in the rig's metres (feet at y = 0).
struct Gait {
    float speed;     // ground speed the stride covers
    float freq;      // full cycles (two steps) per second
    float beta;      // fraction of the cycle each foot is planted (< 0.5 = flight: running)
    float reach;     // how far the planted foot travels under the hip (front to back)
    float lift;      // swing foot's peak height
    float baseDrop;  // constant hip lowering (knee softness / crouch)
    float sag;       // extra knee give at mid-stance
    bool  crouch;
    bool  run;       // running style: flight, forefoot strike, heel kicked up behind
};

// The leg is only 0.80 from hip to ankle, so every centimetre the hips sink folds
// the knee by several degrees: a walk keeps the stance leg nearly straight, a run
// gives at the knee (the leg's spring), more the faster it goes.
Gait GaitFor(int state, float v) {
    Gait g{};
    g.crouch = (state == 17);
    g.run = (state == 3);
    v = v < 0.0f ? 0.0f : v;
    g.speed = v;
    if (g.crouch) {
        g.freq = clampf(0.75f + 0.30f * v, 0.75f, 1.7f);
        float stride = v / g.freq;
        g.beta = stride > 1e-4f ? clampf(0.66f / stride, 0.5f, 0.62f) : 0.62f;
        g.reach = stride * g.beta;
        g.baseDrop = 0.17f;
        g.lift = clampf(0.05f + 0.025f * v, 0.05f, 0.12f);
        g.sag = 0.0f;
    } else if (g.run) {
        // Cadence rises with speed, then levels off and the stride lengthens instead
        // (people run faster by bounding farther). The reach a leg can sweep under the
        // hips caps the stance, so both feet always leave the ground for a while.
        g.freq = clampf(std::fmin(0.95f + 0.30f * v, 1.45f + 0.08f * v), 0.95f, 2.2f);
        float stride = v / g.freq;
        g.beta = stride > 1e-4f ? clampf(0.62f / stride, 0.16f, 0.42f) : 0.42f;
        g.reach = stride * g.beta;
        float k = clampf(v, 2.5f, 8.0f) - 2.5f;
        g.baseDrop = 0.002f * k;
        g.sag = 0.03f + 0.004f * k;
        g.lift = clampf(0.05f + 0.04f * v, 0.08f, 0.32f);
    } else {
        // Always a foot on the ground (double support), heel strike, low lift. A step
        // is capped near 0.97 m and the planted foot's sweep near 0.96 m (the hips
        // would have to drop too far past that), so a faster walk quickens the steps.
        g.beta = 0.62f - 0.04f * clampf(v - 1.0f, 0.0f, 2.5f);
        g.freq = std::fmax(0.55f + 0.22f * v, std::fmax(v / 1.95f, v * g.beta / 0.96f));
        g.reach = v / g.freq * g.beta;
        g.baseDrop = 0.0f;
        g.sag = 0.004f;
        g.lift = clampf(0.05f + 0.015f * v, 0.05f, 0.11f);
    }
    return g;
}

float SpeedFor(int state, float speed) { return speed >= 0.0f ? speed : NaturalSpeed(state); }

struct FootState { float fwd, lift, pitch; bool planted; float u; };
// Where one foot is at cycle phase p (0 = touchdown).
FootState FootAt(float p, float beta, float reach, float lift, float speed, bool crouch, bool run) {
    FootState f{};
    p = frac(p);
    float off = crouch ? 0.0f : (14.0f + 2.5f * clampf(speed, 0.0f, 6.0f));   // toes down pushing off
    float land = crouch ? 0.0f : (run ? 12.0f : -9.0f);    // walkers land heel first, runners on the forefoot
    if (p < beta) {                                     // planted: slides back under the hip
        float u = p / beta;
        f.planted = true; f.u = u;
        f.fwd = reach * (0.5f - u);
        float heelOff = crouch ? 0.0f : clampf((u - 0.65f) / 0.35f, 0.0f, 1.0f);
        float strike = crouch ? 0.0f : clampf(1.0f - u / 0.2f, 0.0f, 1.0f);
        f.pitch = off * heelOff * heelOff + land * strike * strike;
        f.lift = AnkleLiftForPitch(f.pitch);
    } else {                                            // swinging forward to the next step
        float u = (p - beta) / (1.0f - beta);
        f.planted = false; f.u = u;
        // The heel folds up behind first (toward the seat when running), then the
        // knee brings the foot through and the leg reaches forward to land.
        float k = run ? clampf((speed - 2.0f) / 4.0f, 0.0f, 1.0f) : 0.0f;
        f.fwd = reach * (smooth01(std::pow(u, 1.0f + 0.35f * k)) - 0.5f);
        // A runner's foot keeps travelling back after push-off, then the knee drives
        // it well ahead before it comes back to land: the legs scissor in the air.
        f.fwd += k * (-0.14f * std::sin(kPi * clampf(u / 0.4f, 0.0f, 1.0f))
                      + 0.16f * std::sin(kPi * clampf((u - 0.4f) / 0.6f, 0.0f, 1.0f)));
        f.lift = lift * std::sin(kPi * std::pow(u, 0.75f - 0.2f * k));
        f.pitch = off * (1.0f - u) * (1.0f - u) + land * u * u;
        f.lift += AnkleLiftForPitch(f.pitch);   // same rule as stance: no seam at lift-off / touch-down
    }
    return f;
}

float GaitHipYFor(const Gait& g, float ph) {
    const float maxLeg = kHipToAnkle * 0.998f;
    // How far the hips must come down for a planted foot to reach the ground.
    auto need = [&](const FootState& f) {
        float n = kHipToAnkle - std::sqrt(std::fmax(0.0f, maxLeg * maxLeg - f.fwd * f.fwd)) - f.lift;
        return n + g.sag * std::sin(kPi * f.u);
    };
    float drop = -1.0f;
    for (int leg = 0; leg < 2; ++leg) {
        FootState f = FootAt(ph + 0.5f * leg, g.beta, g.reach, g.lift, g.speed, g.crouch, g.run);
        if (f.planted) drop = std::fmax(drop, need(f));
    }
    if (drop < -0.5f) {
        // Both feet off the ground (running): a short ballistic arc that leaves
        // exactly where the last foot pushed off and lands exactly where the next
        // one touches down, so the hips never jump and the feet never dip.
        float s = frac(ph) - g.beta; if (s >= 0.5f) s -= 0.5f;     // cycles into this flight
        float flightCycles = 0.5f - g.beta;
        float w = clampf(s / flightCycles, 0.0f, 1.0f);
        FootState off = FootAt(g.beta - 1e-4f, g.beta, g.reach, g.lift, g.speed, g.crouch, g.run);
        FootState on  = FootAt(1e-4f, g.beta, g.reach, g.lift, g.speed, g.crouch, g.run);
        float a = need(off), b = need(on);
        float flight = flightCycles / g.freq;                        // seconds
        float rise = 0.5f * 9.81f * (0.25f * flight * flight) * 4.0f * w * (1.0f - w);
        drop = a + (b - a) * w - rise;
    }
    return -(g.baseDrop + drop);
}

// Walk and run share one phase; between them the two styles blend by runW.
float GaitHipY(int state, const Inputs& in) {
    const float v = SpeedFor(state, in.speed);
    if (state == 17) return GaitHipYFor(GaitFor(17, v), in.phase);
    const float w = in.runW;
    float a = w < 1.0f ? GaitHipYFor(GaitFor(2, v), in.phase) : 0.0f;
    float b = w > 0.0f ? GaitHipYFor(GaitFor(3, v), in.phase) : 0.0f;
    return a + (b - a) * w;
}

void GaitPoseFor(std::vector<Vec3>& r, const Gait& g, float ph, float hipY, const Inputs& in) {
    float down = kHipToAnkle + hipY;                       // hip joint to ankle, at rest on the ground
    FootState fl = FootAt(ph, g.beta, g.reach, g.lift, g.speed, g.crouch, g.run);
    FootState fr = FootAt(ph + 0.5f, g.beta, g.reach, g.lift, g.speed, g.crouch, g.run);
    float half = g.reach > 1e-4f ? 0.5f * g.reach : 1.0f;
    float sl = clampf(fl.fwd / half, -1.0f, 1.0f), sr = clampf(fr.fwd / half, -1.0f, 1.0f);
    float v = g.speed;
    float twist = g.crouch ? 3.0f : (g.run ? clampf(3.0f + 1.0f * v, 3.0f, 9.0f) : clampf(3.0f + 1.5f * v, 3.0f, 8.0f));
    // Running feet land closer to the centre line.
    float narrow = g.run ? clampf((v - 2.0f) * 0.012f, 0.0f, 0.05f) : 0.0f;
    // Feet are placed in the travel frame, then brought into the twisting pelvis's
    // frame: the hip joints absorb the pelvis turn, so a planted foot stays put.
    float tw = twist * sl * (kPi / 180.0f), c = std::cos(tw), sn = std::sin(tw);
    // Past the hips' turn limit the rest of the travel angle is a side-step: the
    // stride runs along the real travel direction (+ = the body's right = rig -X).
    float rho = (in.travelYaw - in.lowerYaw) * (kPi / 180.0f), rs = std::sin(rho), rc = std::cos(rho);
    auto place = [&](const FootState& f, float hipX, float side, Vec3& th, Vec3& sh, Vec3& ft) {
        float x = hipX + side - f.fwd * rs, z = f.fwd * rc;  // foot, relative to the pelvis centre
        float lx = x * c - z * sn, lz = x * sn + z * c;      // in the pelvis frame
        LegIK(lz, down - f.lift, lx - hipX, f.pitch, th, sh, ft);
    };
    place(fl, -0.105f,  narrow, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
    place(fr,  0.105f, -narrow, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);

    // Arms swing against the legs (left arm back while the left foot is forward). A
    // walk swings them loosely with the elbows nearly straight; a run pumps them
    // harder with the elbows held bent.
    float armA, elbow;
    if (g.crouch)   { armA = 8.0f + 5.0f * v;                     elbow = 30.0f; }
    else if (g.run) { armA = clampf(8.0f + 6.0f * v, 18.0f, 55.0f); elbow = clampf(70.0f + 3.0f * v, 70.0f, 95.0f); }
    else            { armA = clampf(10.0f + 5.0f * v, 10.0f, 30.0f); elbow = clampf(8.0f + 4.0f * v, 8.0f, 25.0f); }
    float lArm = armA * sl, rArm = armA * sr;
    float base = g.crouch ? -18.0f : 0.0f;                 // crouched: hands carried in front
    // Arms hang a little out from the ribs. +Z on the left arm is inward, so the
    // spread is -Z on the left and +Z on the right (the opposite crosses the hands
    // into the hips).
    r[B_LUPARM] = {base + lArm, 0.0f, -8.0f};
    r[B_RUPARM] = {base + rArm, 0.0f,  8.0f};
    r[B_LFORE]  = {-(elbow + 0.45f * std::fmax(0.0f, -lArm)), 0.0f, 0.0f};
    r[B_RFORE]  = {-(elbow + 0.45f * std::fmax(0.0f, -rArm)), 0.0f, 0.0f};

    // Pelvis turns with the stride, shoulders counter-turn; the chest leans into speed
    // and the head stays level.
    float lean = g.crouch ? 28.0f : (g.run ? clampf(2.0f + 1.2f * v, 2.0f, 12.0f) : clampf(1.0f + 0.8f * v, 1.0f, 5.0f));
    r[B_HIPS]  = {0.0f, twist * sl - in.lowerYaw, 0.0f};
    r[B_TORSO] = {lean + 9.0f * in.landK, -1.8f * twist * sl + in.lowerYaw, 0.0f};
    r[B_HEAD]  = {-0.65f * lean, 0.8f * twist * sl, 0.0f};
}

void GaitPose(std::vector<Vec3>& r, int state, const Inputs& in) {
    const float ph = in.phase, v = SpeedFor(state, in.speed);
    if (state == 17) { Gait g = GaitFor(17, v); GaitPoseFor(r, g, ph, GaitHipYFor(g, ph) - 0.12f * in.landK, in); return; }
    const float w = in.runW;
    if (w <= 0.0f || w >= 1.0f) {
        Gait g = GaitFor(w >= 1.0f ? 3 : 2, v);
        GaitPoseFor(r, g, ph, GaitHipYFor(g, ph) - 0.12f * in.landK, in);
        return;
    }
    // Mid-switch: both styles at the same phase, blended.
    std::vector<Vec3> a(r.size(), Vec3{0, 0, 0}), b(r.size(), Vec3{0, 0, 0});
    Gait ga = GaitFor(2, v), gb = GaitFor(3, v);
    GaitPoseFor(a, ga, ph, GaitHipYFor(ga, ph) - 0.12f * in.landK, in);
    GaitPoseFor(b, gb, ph, GaitHipYFor(gb, ph) - 0.12f * in.landK, in);
    for (std::size_t i = 0; i < r.size(); ++i) r[i] = a[i] + (b[i] - a[i]) * w;
}
} // namespace

float NaturalSpeed(int state) {
    return state == 3 ? 4.8f : (state == 17 ? 1.0f : (state == 7 ? 0.8f : 1.5f));
}

float JogWeight(float speed) {
    const float t = clampf((speed - 2.4f) / 1.0f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float CycleFreq(int state, float speed, float runW) {
    const float v = SpeedFor(state, speed);
    if (state == 17) return GaitFor(17, v).freq;
    if (state == 2 || state == 3) return GaitFor(2, v).freq + (GaitFor(3, v).freq - GaitFor(2, v).freq) * runW;
    if (state == 7) {   // the crawl cycle follows the distance
        const float nominal = GaitFor(7, NaturalSpeed(7)).freq;
        return speed >= 0.0f ? nominal * speed / NaturalSpeed(7) : nominal;
    }
    return 0.0f;
}

float AbsorbLanding(std::vector<Vec3>& r, float landK) {
    if (landK <= 0.0f || (int)r.size() < B_COUNT) return 0.0f;
    const float drop = 0.12f * landK;
    // Where each ankle is (LegIK run backwards), then the same ankle from hips that much lower.
    auto resolve = [&](Vec3& thigh, Vec3& shin, Vec3& foot) {
        const float a = kThigh, b = kShin, d2r = kPi / 180.0f;
        const float gamma = kPi - shin.x * d2r;
        const float d = std::sqrt(std::fmax(a * a + b * b - 2.0f * a * b * std::cos(gamma), 1e-6f));
        const float alpha = std::acos(clampf((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f));
        const float phi = -thigh.x * d2r - alpha;
        const float dz = d * std::cos(phi), abd = thigh.z * d2r;
        const float pitch = foot.x + thigh.x + shin.x;
        Vec3 th, sh, ft;
        LegIK(d * std::sin(phi), dz * std::cos(abd) - drop, dz * std::sin(abd), pitch, th, sh, ft);
        thigh.x = th.x; thigh.z = th.z; shin.x = sh.x; foot.x = ft.x;
    };
    resolve(r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
    resolve(r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);
    r[B_TORSO].x  += 9.0f * landK;
    r[B_LUPARM].z -= 10.0f * landK;   r[B_RUPARM].z += 10.0f * landK;   // out, not into the ribs
    return -drop;
}

float KneelPose(int which, std::vector<Vec3>& r) {
    r.assign(B_COUNT, Vec3{0, 0, 0});
    if (which == 0) {   // hands and knees: the hips 0.42 up, thighs straight down, shins back along the ground
        r[B_HIPS]   = {60, 0, 0};        // body tipped forward (+ the chest's 10 = 70 deg)
        r[B_TORSO]  = {10, 0, 0};
        r[B_HEAD]   = {-55, 0, 0};       // looking ahead, not at the floor
        r[B_LTHIGH] = {-60, 0, -4};      r[B_RTHIGH] = {-60, 0, 4};    // straight down in the world
        r[B_LSHIN]  = {90, 0, 0};        r[B_RSHIN]  = {90, 0, 0};     // shins flat behind
        r[B_LFOOT]  = {-30, 0, 0};       r[B_RFOOT]  = {-30, 0, 0};
        r[B_LUPARM] = {-70, 0, -8};      r[B_RUPARM] = {-70, 0, 8};    // arms down, hands outside the knees
        return 0.42f - (kHipToAnkle + kAnkleH);
    }
    // Kneeling on the right knee, the left foot planted ahead, a hand on that knee.
    const float hipH = 0.42f;
    r[B_TORSO]  = {15, 0, 0};
    r[B_HEAD]   = {-10, 0, 0};
    LegIK(0.30f, hipH - kAnkleH, 0.0f, 0.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
    r[B_RTHIGH] = {8, 0, 0};             // knee down, just behind the hips
    r[B_RSHIN]  = {88, 0, 0};            // shin flat behind
    r[B_RFOOT]  = {-35, 0, 0};           // toes tucked under
    r[B_LUPARM] = {-30, 0, 8};   r[B_LFORE] = {-45, 0, 0};    // hand resting on the front knee
    r[B_RUPARM] = {0, 0, -6};
    return hipH - (kHipToAnkle + kAnkleH);
}

float StartPhase(int state, float speed) { return 0.5f * GaitFor(state, SpeedFor(state, speed)).beta; }

float HipY(int state, const Inputs& in) {
    // Walk / run / crouch-walk: the hips ride the stride (lower while a planted
    // foot is far ahead or behind, a small rise in a running flight) so the legs
    // reach the ground instead of the feet floating.
    if (state == 2 || state == 3 || state == 17) return GaitHipY(state, in) - 0.12f * in.landK;
    if (state == 6) return -0.17f;   // crouch: hips down, knees bent
    if (state == 7) return -0.78f;   // prone: lay the body on the ground
    // Landing recovery: the flexed knees momentarily lower the whole body.
    if (in.landK > 0.0f && state == 1) return -0.12f * in.landK;
    return 0.0f;
}

void Pose(int state, float t, const Inputs& in, std::vector<Vec3>& out) {
    std::vector<Vec3>& r = out;
    r.assign(B_COUNT, Vec3{0, 0, 0});
    if (state == 1) {                       // idle
        float s = std::sin(t * 1.8f);
        r[B_TORSO] = {1.5f * s, 0, 0};
        // Relaxed hang: a little forward, elbows softly bent, hands clear of the
        // thighs. The breathe narrows them slightly and never pulls them into the hip.
        r[B_LUPARM] = {-6.0f, 0, -10.0f + 1.2f * s};
        r[B_RUPARM] = {-6.0f, 0,  10.0f - 1.2f * s};
        r[B_LFORE]  = {-22.0f, 0, 0};
        r[B_RFORE]  = {-22.0f, 0, 0};
        // Idle fidgets: a slow side-to-side weight shift (hips roll one way, the
        // torso counter-rolls to stay balanced) plus an occasional glance to one
        // side — alternating sides each ~9s cycle — so a standing character reads
        // as alive rather than a breathing statue.
        float sway = std::sin(t * 0.45f);
        r[B_HIPS].z  += 2.2f * sway;
        r[B_TORSO].z -= 1.4f * sway;
        float cyc = std::fmod(t, 9.0f);
        if (cyc > 6.0f && cyc < 8.0f) {
            float e = std::sin((cyc - 6.0f) * 0.5f * 3.14159265f);   // ease 0..1..0
            float side = std::fmod(t / 9.0f, 2.0f) < 1.0f ? 1.0f : -1.0f;
            r[B_HEAD].y += side * 22.0f * e;
            r[B_HEAD].x += 2.0f * e;                                 // slight tilt with it
        }
    } else if (state == 2 || state == 3) {   // walk / run: a stride laid out from the real ground speed
        GaitPose(r, state, in);
    } else if (state == 4) {                // wave
        r[B_RUPARM] = {0, 0, 125};                                 // up and out on the body's right
        r[B_RFORE]  = {-8, 0, 22 * std::sin(t * 8.0f)};
        r[B_LUPARM] = {-6, 0, -8};
        r[B_LFORE]  = {-18, 0, 0};
    } else if (state == 5) {                // jump: lead knee driven up, trail leg pushing off, arms swing up
        LegIK( 0.26f, 0.42f, 0.0f, 10.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);   // lead leg tucked
        LegIK(-0.16f, 0.72f, 0.0f, 35.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);   // trail leg, toes pointed
        r[B_RUPARM] = {-48, 0, 8};     r[B_RFORE] = {-62, 0, 0};    // opposite arm drives forward-up, elbow bent
        r[B_LUPARM] = {28, 0, -8};     r[B_LFORE] = {-18, 0, 0};    // the other swings back, clear of the hip
        r[B_TORSO]  = {6, 4, 0};
        r[B_HEAD]   = {-6, 0, 0};
    } else if (state == 18) {               // fall: legs reach down for the landing, arms out for balance
        LegIK( 0.10f, 0.66f, 0.03f, -4.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
        LegIK(-0.02f, 0.73f, -0.03f, 4.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);
        r[B_LUPARM] = {-12, 0, -58};   r[B_RUPARM] = {-12, 0, 58};
        r[B_LFORE]  = {-22, 0, 0};     r[B_RFORE]  = {-22, 0, 0};
        r[B_TORSO]  = {4, 0, 0};
        r[B_HEAD]   = {8, 0, 0};       // eyes on the ground coming up
    } else if (state == 19) {               // hang: arms straight up to the ledge, legs hanging loose
        r[B_LUPARM] = {-170, 0, 6};    r[B_RUPARM] = {-170, 0, -6};
        r[B_LFORE]  = {-8, 0, 0};      r[B_RFORE]  = {-8, 0, 0};
        float s = std::sin(t * 1.6f);  // a slight sway
        LegIK( 0.04f + 0.02f * s, 0.76f, 0.02f, 25.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
        LegIK(-0.02f + 0.02f * s, 0.77f, -0.02f, 25.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);
        r[B_HEAD]   = {-14, 0, 0};     // looking up at the hands
    } else if (state == 6) {                // crouch: hips back and down, knees forward over the toes, chest low
        float s = std::sin(t * 2.2f);      // breathing
        const float down = kHipToAnkle - 0.17f;
        LegIK( 0.12f, down, 0.03f, 0.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
        LegIK( 0.04f, down, -0.03f, 0.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);   // one foot a little behind
        r[B_TORSO]  = {28 + 1.5f * s, 0, 0};
        r[B_HEAD]   = {-20, 0, 0};                           // eyes forward, not at the floor
        r[B_LUPARM] = {-20, 0, -12}; r[B_RUPARM] = {-20, 0, 12};   // hands in front, outside the knees
        r[B_LFORE]  = {-32, 0, 0};   r[B_RFORE]  = {-32, 0, 0};
    } else if (state == 17) {               // crouch-walk: the same crouch, stepping heel-to-toe
        GaitPose(r, 17, in);
    } else if (state == 7) {                // prone (whole body flat on the ground)
        // Rotate ONLY the hips 90° so the body lies flat: the torso/head swing
        // forward along the ground and the legs swing straight back — no counter
        // rotation on the legs (that was what stood it back up). The hip offset
        // then drops the whole body down onto the floor.
        r[B_HIPS]   = {90, 0, 0};
        r[B_HEAD]   = {-52, 0, 0};         // lift the head to look ahead
        // Lying down, the arms reach forward past the head (-X swings an arm
        // forward; -90 would point it at the ground) with the forearms flat on the
        // ground in front, elbows near the shoulders.
        r[B_LUPARM] = {-158, 0, 12};  r[B_RUPARM] = {-158, 0, -12};
        r[B_LFORE]  = {-20, 0, 0};    r[B_RFORE]  = {-20, 0, 0};
        r[B_LTHIGH] = {0, 0, -6};     r[B_RTHIGH] = {0, 0, 6};      // legs a little apart
        // Crawling: when the body moves, the elbows reach and pull in turn and the
        // opposite knee drags up along the ground (distance-driven like the walk).
        if (in.speed > 0.15f) {
            float c = std::sin(in.phase * 2.0f * kPi);
            float kl = std::fmax(0.0f, -c), kr = std::fmax(0.0f, c);
            r[B_LUPARM].x += 20.0f * c;   r[B_RUPARM].x -= 20.0f * c;
            r[B_LFORE].x  -= 25.0f * kr;  r[B_RFORE].x  -= 25.0f * kl;
            r[B_LTHIGH] = {0, 0, -(6.0f + 24.0f * kl)};   r[B_LSHIN] = {0, 0,  34.0f * kl};   // knee out, foot back in
            r[B_RTHIGH] = {0, 0,  (6.0f + 24.0f * kr)};   r[B_RSHIN] = {0, 0, -34.0f * kr};
            r[B_TORSO].y = 4.0f * c;
            // Crawling sideways (travelYaw +-90 = the body's right / left): the arm on
            // that side reaches out wide and the other tucks in to push, the chest
            // bends toward the travel and the knee on that side draws up higher.
            const float side = std::sin(in.travelYaw * (kPi / 180.0f));
            const float toR = std::fmax(0.0f, side), toL = std::fmax(0.0f, -side);
            r[B_LUPARM].z -= 28.0f * toL - 10.0f * toR;   // travel-side arm reaches out
            r[B_RUPARM].z += 28.0f * toR - 10.0f * toL;
            r[B_TORSO].z  += 10.0f * (toL - toR);
            r[B_LTHIGH].z -= 14.0f * toL;  r[B_RTHIGH].z += 14.0f * toR;
        }
    }
    // ---- Hand gestures (8-12) and emotions (13-16) -----------------------------
    // The rig is blocky with no face, so these read through posture. On a hanging
    // arm -X reaches forward (and up, once it passes horizontal) and +X reaches
    // backward. A forearm bends with -X; +X hyperextends the elbow. +Z on the left
    // arm moves that hand inward and +Z on the right arm moves it outward.
    else if (state == 8) {                  // point forward (right arm)
        r[B_RUPARM] = {-88, 0, 8};         // horizontal, ahead and slightly out
        r[B_RFORE]  = {-4, 0, 0};
        r[B_LUPARM] = {-6, 0, -8};         // left arm rests
        r[B_LFORE]  = {-18, 0, 0};
        r[B_TORSO]  = {0, -6, 0};          // slight turn into the point
    } else if (state == 9) {                // clap (hands meet in front, repeating)
        float c = 18.0f * (0.5f + 0.5f * std::sin(t * 7.0f));   // 0..18 open/close
        r[B_LUPARM] = {-50, 0, 30 - c};    r[B_RUPARM] = {-50, 0, -30 + c};
        r[B_LFORE]  = {-46, 0, 0};         r[B_RFORE]  = {-46, 0, 0};
        r[B_HEAD]   = {-4, 0, 0};
    } else if (state == 10) {               // thumbs up (right fist up beside the chest)
        r[B_RUPARM] = {-12, 0, 22};
        r[B_RFORE]  = {-130, 0, 0};
        r[B_LUPARM] = {-6, 0, -8};
        r[B_LFORE]  = {-18, 0, 0};
    } else if (state == 11) {               // salute (right hand to the brow)
        r[B_RUPARM] = {-110, 0, -5};
        r[B_RFORE]  = {-105, 0, -35};      // folded in onto the forehead
        r[B_LUPARM] = {-6, 0, -8};
        r[B_LFORE]  = {-18, 0, 0};
        r[B_HEAD]   = {-6, 0, 0};
        r[B_TORSO]  = {-2, 0, 0};          // stand tall
    } else if (state == 12) {               // wave-both / surrender-style open hands
        float s2 = std::sin(t * 6.0f);
        r[B_LUPARM] = {-8, 0, -125};       r[B_RUPARM] = {-8, 0, 125};   // both arms up and out
        r[B_LFORE]  = {-8, 0, 16 * s2};    r[B_RFORE]  = {-8, 0, -16 * s2};
    } else if (state == 13) {               // happy / cheer (arms up in a V, bouncing)
        float b = std::sin(t * 6.0f);
        r[B_LUPARM] = {-30, 0, -75};       r[B_RUPARM] = {-30, 0, 75};
        r[B_LFORE]  = {-15, 0, 0};         r[B_RFORE]  = {-15, 0, 0};
        r[B_HEAD]   = {-12, 0, 0};         // chin up, upbeat
        r[B_TORSO]  = {-4 + 2.0f * b, 0, 0};
    } else if (state == 14) {               // sad / dejected (slumped, head down)
        float b = std::sin(t * 1.4f);
        r[B_TORSO]  = {20 + 1.0f * b, 0, 0};   // slump forward
        r[B_HEAD]   = {26, 0, 0};              // look down
        r[B_LUPARM] = {-10, 0, -6};        r[B_RUPARM] = {-10, 0, 6};    // arms hang limp, slightly forward
        r[B_LFORE]  = {-8, 0, 0};          r[B_RFORE]  = {-8, 0, 0};
    } else if (state == 15) {               // angry (lean in, fists up)
        float b = std::sin(t * 5.0f);
        r[B_TORSO]  = {14, 1.5f * b, 0};       // hunch + small aggressive sway
        r[B_HEAD]   = {12, 0, 0};              // head down/forward (glaring)
        r[B_LUPARM] = {-22, 0, -10};       r[B_RUPARM] = {-22, 0, 10};   // fists in front of the chest
        r[B_LFORE]  = {-85, 0, 0};         r[B_RFORE]  = {-85, 0, 0};
    } else if (state == 16) {               // think / idle-curious (hand near chin)
        r[B_RUPARM] = {-60, 0, 0};
        r[B_RFORE]  = {-125, 0, -25};
        r[B_HEAD]   = {-4, 14, 0};         // slight quizzical tilt
        r[B_LUPARM] = {-6, 0, -8};
        r[B_LFORE]  = {-18, 0, 0};
    }

    // Landing recovery: a brief knee-bend absorb right after a jump lands — hips
    // drop, knees bend forward, chest dips, arms out for balance — springing back
    // over ~0.3s. Walk/run fold it into their own stride (GaitPose).
    if (in.landK > 0.0f && state == 1) {
        float lk = in.landK;
        const float down = kHipToAnkle - 0.12f * lk;
        LegIK(0.03f, down, 0.0f, 0.0f, r[B_LTHIGH], r[B_LSHIN], r[B_LFOOT]);
        LegIK(0.03f, down, 0.0f, 0.0f, r[B_RTHIGH], r[B_RSHIN], r[B_RFOOT]);
        r[B_TORSO].x  += 12.0f * lk;
        r[B_LUPARM].z -= 14.0f * lk;   r[B_RUPARM].z += 14.0f * lk;   // arms out for balance
    }
    if (in.landK > 0.0f && (state == 2 || state == 3)) {
        r[B_LUPARM].z -= 10.0f * in.landK;   r[B_RUPARM].z += 10.0f * in.landK;
    }
}

} // namespace HumanoidMotion
} // namespace okay
