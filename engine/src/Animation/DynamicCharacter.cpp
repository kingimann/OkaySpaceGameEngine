// DynamicCharacter - see the header for what it is and what it was written from.
#include "okay/Animation/DynamicCharacter.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {
namespace dyn {

namespace {
enum {
    Hips = 0, Torso, Head,
    LUpperArm, LForeArm, LHand,
    RUpperArm, RForeArm, RHand,
    LThigh, LShin, LFoot,
    RThigh, RShin, RFoot,
};
constexpr int kParent[kBones] = {-1, Hips, Torso, Torso, LUpperArm, LForeArm, Torso, RUpperArm, RForeArm,
                                 Hips, LThigh, LShin, Hips, RThigh, RShin};
constexpr int kChild[kBones] = {Torso, Head, -1, LForeArm, LHand, -1, RForeArm, RHand, -1,
                                LShin, LFoot, -1, RShin, RFoot, -1};
// Segment mass as a fraction of the body (Winter 2009, table 4.1: trunk split
// into pelvis and thorax-abdomen; the neck goes with the head).
constexpr float kMassFrac[kBones] = {0.142f, 0.355f, 0.081f, 0.028f, 0.016f, 0.006f, 0.028f, 0.016f, 0.006f,
                                     0.100f, 0.0465f, 0.0145f, 0.100f, 0.0465f, 0.0145f};
// Capsule radius as a fraction of standing height (our own estimate).
constexpr float kRadiusFrac[kBones] = {0.062f, 0.072f, 0.058f, 0.030f, 0.027f, 0.025f, 0.030f, 0.027f, 0.025f,
                                       0.045f, 0.035f, 0.022f, 0.045f, 0.035f, 0.022f};
constexpr float kStep = 1.0f / 60.0f;
constexpr int kMaxSteps = 4;

bool Fin(float f) { return std::isfinite(f); }
bool Fin(const Vec3& v) { return Fin(v.x) && Fin(v.y) && Fin(v.z); }
bool Fin(const Quat& q) { return Fin(q.x) && Fin(q.y) && Fin(q.z) && Fin(q.w); }
Vec3 Flat(const Vec3& v) { return Vec3{v.x, 0.0f, v.z}; }
Vec3 SafeNorm(const Vec3& v, const Vec3& fallback) {
    const float m = v.Magnitude();
    return m > 1e-6f && Fin(m) ? v * (1.0f / m) : fallback;
}
float Smooth(float t) { t = Mathf::Clamp01(t); return t * t * (3.0f - 2.0f * t); }
int  Side(int bone) { return (bone >= LUpperArm && bone <= LHand) || (bone >= LThigh && bone <= LFoot) ? 0 : 1; }
bool IsLeg(int b) { return b >= LThigh; }
bool IsArm(int b) { return b >= LUpperArm && b <= RHand; }

// Yaw that turns flat direction `from` into flat direction `to`.
Quat YawBetween(const Vec3& from, const Vec3& to) {
    const Vec3 a = SafeNorm(Flat(from), Vec3::Forward), b = SafeNorm(Flat(to), a);
    const float ang = std::atan2(Vec3::Dot(Vec3::Cross(a, b), Vec3::Up), Vec3::Dot(a, b));
    return Quat::AngleAxis(ang * Mathf::Rad2Deg, Vec3::Up);
}

Vec3 RotVecOf(Quat q) {
    if (q.w < 0.0f) q = Quat{-q.x, -q.y, -q.z, -q.w};
    const Vec3 v{q.x, q.y, q.z};
    const float s = v.Magnitude();
    if (s < 1e-9f) return v * 2.0f;
    return v * (2.0f * std::atan2(s, q.w) / s);
}

// Andrew's monotone chain on the xz plane; returns the hull size (counter-clockwise).
int Hull(Vec3* p, int n, Vec3* out) {
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && (p[j].x < p[j - 1].x || (p[j].x == p[j - 1].x && p[j].z < p[j - 1].z)); --j) {
            const Vec3 t = p[j]; p[j] = p[j - 1]; p[j - 1] = t;
        }
    if (n < 3) { for (int i = 0; i < n; ++i) out[i] = p[i]; return n; }
    auto cross = [](const Vec3& o, const Vec3& a, const Vec3& b) {
        return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
    };
    int k = 0;
    for (int i = 0; i < n; ++i) {
        while (k >= 2 && cross(out[k - 2], out[k - 1], p[i]) <= 0.0f) --k;
        out[k++] = p[i];
    }
    for (int i = n - 2, t = k + 1; i >= 0; --i) {
        while (k >= t && cross(out[k - 2], out[k - 1], p[i]) <= 0.0f) --k;
        out[k++] = p[i];
    }
    return k - 1;
}

// How far `q` lies outside the hull (<= 0 inside), on the xz plane.
float Outside(const Vec3* h, int n, const Vec3& q) {
    if (n <= 0) return 1e9f;
    if (n == 1) return Vec3::Distance(Flat(h[0]), Flat(q));
    if (n == 2) {
        const Vec3 a = Flat(h[0]), b = Flat(h[1]), p = Flat(q);
        const Vec3 ab = b - a;
        const float t = Mathf::Clamp01(Vec3::Dot(p - a, ab) / Mathf::Max(1e-9f, ab.SqrMagnitude()));
        return Vec3::Distance(a + ab * t, p);
    }
    float worst = -1e9f;
    for (int i = 0; i < n; ++i) {
        const Vec3 a = h[i], b = h[(i + 1) % n];
        const Vec3 e{b.x - a.x, 0.0f, b.z - a.z};
        const float len = e.Magnitude();
        if (len < 1e-6f) continue;
        // Counter-clockwise: the outward normal of edge (ex, ez) is (ez, -ex).
        const float d = ((q.x - a.x) * e.z - (q.z - a.z) * e.x) / len;
        if (d > worst) worst = d;
    }
    return worst;
}
} // namespace

const char* StateName(CharState s) {
    switch (s) {
        case CharState::Animated:  return "Animated";
        case CharState::Balancing: return "Balancing";
        case CharState::Falling:   return "Falling";
        case CharState::Down:      return "Down";
        case CharState::GettingUp: return "GettingUp";
        case CharState::Blending:  return "Blending";
        case CharState::Dead:      return "Dead";
    }
    return "?";
}

Quat DynamicCharacter::Pitch(float degrees, const Vec3& fwd) {
    const Vec3 axis = SafeNorm(Vec3::Cross(Vec3::Up, SafeNorm(Flat(fwd), Vec3::Forward)), Vec3::Right);
    return Quat::AngleAxis(degrees, axis);
}

// =============================================================================
// Behaviour modules
// =============================================================================
namespace {

// Hit reaction: a struck region goes soft for a moment (its drives weaken),
// so the push shows as a real sway of that part. The relax timers are set by
// Hit / Shot / Explosion and fade over Tuning::hitRelaxTime.
class HitReaction : public DynBehaviour {
public:
    const char* Name() const override { return "HitReaction"; }
    void Run(DynamicCharacter& c, Frame& f, float) override {
        for (int b = 0; b < kBones; ++b) f.str[b] *= 1.0f - 0.85f * Mathf::Clamp01(c.Relax(b));
    }
};

// Stumble and recover: centre of mass over the support polygon with a virtual
// force (Pratt 2001), torso held upright in the world through the stance hips
// (SIMBICON), and a step toward the capture point (Pratt 2006) when the capture
// point leaves the feet. Falls when it cannot catch itself.
class Balance : public DynBehaviour {
public:
    const char* Name() const override { return "StumbleRecover"; }
    bool  stepping = false;
    int   swing = 0;
    float t = 0.0f;
    Vec3  from, land;

    void Reset() { stepping = false; t = 0.0f; }

    // Where the swing foot should land so the capture point is inside the new
    // support when it lands: the capture point moves away from the stance
    // foot's centre of pressure p as xi(t) = p + (xi0 - p) e^(w t) (linear
    // inverted pendulum, Pratt 2006). Returns how far the target had to be
    // pulled in (0 = the step can capture).
    static float Plan(DynamicCharacter& c, int sw, float remaining, float w0, float ground, Vec3& out) {
        const int st = 1 - sw;
        const float scale = c.Height() / 1.8f;
        Vec3 p;
        if (!c.FootSupport(st, p)) p = c.Anchor(st == 0 ? LFoot : RFoot);
        const Vec3 xi = c.m_capture;
        const float grow = std::exp(w0 * Mathf::Clamp(remaining, 0.0f, 0.6f));
        Vec3 want = Flat(p) + Flat(xi - p) * grow;
        const Vec3 dir = SafeNorm(Flat(xi - p), c.AnimForward());
        want = want + dir * (0.05f * scale) + c.Lateral(sw) * (0.05f * scale);
        const Vec3 ank = c.Anchor(st == 0 ? LFoot : RFoot);
        Vec3 d = Flat(want) - Flat(ank);
        const float maxStep = c.tuning.maxStep * scale;
        float pulled = 0.0f;
        if (d.Magnitude() > maxStep) { pulled += d.Magnitude() - maxStep; d = d * (maxStep / d.Magnitude()); }
        // Never cross over the stance foot.
        const float across = Vec3::Dot(d, c.Lateral(sw)), minAcross = 0.12f * scale;
        if (across < minAcross) { pulled += minAcross - across; d = d + c.Lateral(sw) * (minAcross - across); }
        out = Vec3{ank.x + d.x, ground + c.AnkleHeight(), ank.z + d.z};
        return pulled;
    }

    void Run(DynamicCharacter& c, Frame& f, float h) override {
        if (c.State() != CharState::Balancing) { Reset(); return; }
        Vec3 pts[16];
        const int n = c.SupportPoints(pts, 16);
        const Vec3 com = c.CentreOfMass(), vel = c.CentreOfMassVelocity();
        const Vec3 fwd = c.AnimForward();
        if (n == 0) {
            c.m_airTime += h;
            if (c.m_airTime > 0.25f) { c.ChangeState(CharState::Falling); return; }
        } else c.m_airTime = 0.0f;
        float ground = c.GroundY();
        if (n > 0) {
            float lo = 1e9f;
            for (int i = 0; i < n; ++i) lo = Mathf::Min(lo, pts[i].y);
            ground = lo - c.FootSphereRadius();
        }
        const float z = Mathf::Max(0.2f, com.y - ground);
        const float w0 = std::sqrt(9.81f / z);
        c.m_capture = Vec3{com.x + vel.x / w0, ground, com.z + vel.z / w0};
        const float scale = c.Height() / 1.8f;
        if (c.TorsoLean() > c.tuning.fallAngle || c.PelvisHeight() < 0.55f * c.StandingHeight()) {
            c.ChangeState(CharState::Falling);
            return;
        }
        Vec3 hull[40];
        const int hn = Hull(pts, n, hull);
        const float out = n > 0 ? Outside(hull, hn, c.m_capture) : 1e9f;

        // Upright: the pelvis is held in its animated world rotation by the hip
        // drives pushing against the legs (the stance-hip torque of SIMBICON),
        // plus the stated upright helper on the chest (Tuning::uprightHelp).
        f.want[Hips] = c.AnimBodyRotation(Hips);
        f.has[Hips] = true;
        if (c.tuning.uprightHelp > 0.0f)
            c.Physics()->SetHelper(Torso, Vec3::Zero, 0.0f, c.AnimBodyRotation(Torso), c.TotalMass() * c.tuning.uprightHelp, 8.0f);
        // Hold the standing height: the stance legs reach from a hip a little
        // higher than it is when the body sags (a small step each frame).
        const float sagGain = 0.01f;
        // Stated balance helper (Tuning::balanceHelp): while a foot is down, a
        // capped outside push on the pelvis toward keeping the capture point
        // over the feet. 0 = purely the body's own forces.
        if (n > 0 && c.tuning.balanceHelp > 0.0f) {
            Vec3 mid;
            for (int i = 0; i < n; ++i) mid = mid + pts[i];
            mid = mid * (1.0f / (float)n);
            const float M = c.TotalMass();
            Vec3 F = Flat(mid - c.m_capture) * (M * 25.0f) - Flat(vel) * (M * 2.0f);
            F = Vec3::ClampMagnitude(F, c.tuning.balanceHelp * M * 9.81f);
            c.Physics()->AddForce(Hips, F);
        }
        const Vec3 lift = Vec3::Up * Mathf::Clamp(c.StandingHeight() - c.PelvisHeight(), -sagGain, sagGain);

        if (stepping) {
            t += h;
            const float T = Mathf::Max(0.05f, c.tuning.stepTime);
            const float s = t / T;
            const int stance = 1 - swing;
            // Re-plan the landing as the body moves (only while the foot is in the air).
            if (s < 0.75f) {
                Vec3 again;
                Plan(c, swing, T - t, w0, ground, again);
                land = Vec3::MoveTowards(land, again, 2.5f * h);
            }
            Vec3 a = Vec3::Lerp(from, land, Smooth(s));
            a = a + Vec3::Up * (0.07f * scale * std::sin(Mathf::PI * Mathf::Clamp01(s)));
            c.LegIK(f, swing, c.Anchor(swing == 0 ? LThigh : RThigh), a, fwd);
            c.FlatFoot(f, swing);
            {
                // The hip muscles carry the foot along its path: a force on the
                // foot toward the path, the same force back on the pelvis.
                const int ft = swing == 0 ? LFoot : RFoot;
                const BodyState fb = c.Body(ft), pb = c.Body(Hips);
                const float m = c.Physics()->Mass(ft) + c.Physics()->Mass(ft - 1);
                const float k = 900.0f * m, d = 2.0f * std::sqrt(k * m);
                Vec3 F = (a - c.Anchor(ft)) * k - (fb.velocity - pb.velocity) * d;
                Vec3 sc0;
                if (F.y < 0.0f) F.y = 0.0f;   // never press the foot into the ground (it would throw the body up)
                if (s > 0.5f && c.FootSupport(swing, sc0)) F = Vec3::Zero;
                F = Vec3::ClampMagnitude(F, 0.6f * c.TotalMass() * 9.81f);
                c.Physics()->AddForce(ft, F);
                c.Physics()->AddForce(Hips, F * -1.0f);
            }
            c.LegIK(f, stance, c.Anchor(stance == 0 ? LThigh : RThigh) + lift, c.Anchor(stance == 0 ? LFoot : RFoot), fwd);
            c.FlatFoot(f, stance);
            f.str[swing == 0 ? LThigh : RThigh] *= 1.3f;
            f.str[swing == 0 ? LShin : RShin] *= 1.3f;
            Vec3 sc;
            if ((s >= 1.0f && c.FootSupport(swing, sc)) || s >= 1.8f) { stepping = false; ++c.m_steps; }
            c.m_stableTime = 0.0f;
            return;
        }

        const float margin = 0.03f * scale;
        if (enabled && c.tuning.stumble && n > 0 && out > margin) {
            if (c.m_steps >= c.tuning.maxSteps) { c.ChangeState(CharState::Falling); return; }
            Vec3 fl, fr;
            const bool hasL = c.FootSupport(0, fl), hasR = c.FootSupport(1, fr);
            const float T = Mathf::Max(0.05f, c.tuning.stepTime);
            Vec3 l0, l1;
            const float c0 = Plan(c, 0, T, w0, ground, l0), c1 = Plan(c, 1, T, w0, ground, l1);
            if (!hasR) swing = 0;           // only the left can be lifted if the right is not down... and the other way
            if (!hasL) swing = 1;
            if (hasL && hasR) {
                if (std::fabs(c0 - c1) > 0.02f * scale) swing = c0 < c1 ? 0 : 1;
                else {
                    // Same reach: the foot further from the capture point steps.
                    swing = Vec3::Distance(Flat(fl), Flat(c.m_capture)) > Vec3::Distance(Flat(fr), Flat(c.m_capture)) ? 0 : 1;
                }
            }
            const float cost = swing == 0 ? c0 : c1;
            // Too far to catch even with steps to come: give up (Pratt's N-step capturability, roughly).
            const int left = Mathf::Max(1, c.tuning.maxSteps - c.m_steps);
            if (cost > 0.6f * c.tuning.maxStep * scale * (float)left) { c.ChangeState(CharState::Falling); return; }
            land = swing == 0 ? l0 : l1;
            from = c.Anchor(swing == 0 ? LFoot : RFoot);
            stepping = true;
            t = 0.0f;
            return;
        }
        // Both feet down: legs reach their planted ankles; a virtual force moves
        // the centre of mass back over the middle of the feet and the feet push
        // the other way (virtual model control).
        for (int side = 0; side < 2; ++side) {
            c.LegIK(f, side, c.Anchor(side == 0 ? LThigh : RThigh) + lift, c.Anchor(side == 0 ? LFoot : RFoot), fwd);
            c.FlatFoot(f, side);
        }
        if (n > 0) {
            Vec3 mid;
            for (int i = 0; i < hn; ++i) mid = mid + hull[i];
            mid = mid * (1.0f / (float)hn);
            const float M = c.TotalMass();
            Vec3 F = Flat((mid - com) * 30.0f - vel * 11.0f) * M;
            F = Vec3::ClampMagnitude(F, c.tuning.balanceForce * M * 9.81f);
            c.Physics()->AddForce(Hips, F * 0.5f);
            c.Physics()->AddForce(Torso, F * 0.5f);
            Vec3 sc;
            const int feet = (c.FootSupport(0, sc) ? 1 : 0) + (c.FootSupport(1, sc) ? 1 : 0);
            for (int side = 0; side < 2 && feet > 0; ++side)
                if (c.FootSupport(side, sc)) c.Physics()->AddForce(side == 0 ? LFoot : RFoot, F * (-1.0f / (float)feet));
        }
        const bool calm = out <= 0.0f && Flat(vel).Magnitude() < 0.35f && c.TorsoLean() < 15.0f;
        c.m_stableTime = calm ? c.m_stableTime + h : 0.0f;
        if (c.m_stableTime > 0.45f && c.StateTime() > 0.4f && c.mode != RagdollMode::Powered)
            c.ChangeState(CharState::Blending);
        else if (c.m_stableTime > 0.45f && c.mode == RagdollMode::Powered)
            c.m_steps = 0;
    }
};

// Grab to stabilise: while stumbling or starting to fall, reach for a wall or
// a table top within arm's length in the direction of the fall, and hold on
// with a soft pin that lets go if pulled too far.
class Grab : public DynBehaviour {
public:
    const char* Name() const override { return "Grab"; }
    int side = 0;
    Vec3 point;

    void Release(DynamicCharacter& c) {
        if (c.m_grabBody >= 0) c.Physics()->ClearPin(c.m_grabBody);
        c.m_grabBody = -1;
    }

    void Run(DynamicCharacter& c, Frame& f, float) override {
        const CharState s = c.State();
        const bool active = s == CharState::Balancing || (s == CharState::Falling && c.StateTime() < 0.8f);
        if (!enabled || !c.tuning.grab || !active || !c.World()) { Release(c); return; }
        const int ua = side == 0 ? LUpperArm : RUpperArm;
        const int fa = ua + 1, hand = ua + 2;
        if (c.m_grabBody >= 0) {
            const BodyState hs = c.Body(hand);
            const Vec3 tip = hs.position + hs.rotation * (Vec3::Up * c.BodyHalfLength(hand));
            if (Vec3::Distance(tip, point) > 0.3f * c.Height() / 1.8f) { Release(c); return; }
            Aim(c, f, ua, fa, hand, point);
            return;
        }
        const Vec3 vel = Flat(c.CentreOfMassVelocity());
        if (vel.Magnitude() < 0.4f) return;
        const Vec3 dir = vel.Normalized();
        for (int pass = 0; pass < 2; ++pass) {
            const int sd = pass == 0 ? (Vec3::Dot(dir, c.Lateral(0)) > 0.0f ? 0 : 1) : (Vec3::Dot(dir, c.Lateral(0)) > 0.0f ? 1 : 0);
            const int u = sd == 0 ? LUpperArm : RUpperArm;
            const Vec3 sh = c.Anchor(u);
            const float reach = c.SegmentLength(u) + c.SegmentLength(u + 1) + c.SegmentLength(u + 2);
            const Vec3 dirs[3] = {dir, SafeNorm(dir + Vec3::Down * 0.6f, dir), SafeNorm(dir + c.Lateral(sd) * 0.5f, dir)};
            for (const Vec3& d : dirs) {
                Vec3 p, nrm;
                if (!c.World()->Cast(sh, sh + d * reach, p, nrm)) continue;
                const bool wall = Vec3::Dot(nrm, d) < -0.3f && std::fabs(nrm.y) < 0.7f;
                const bool table = nrm.y > 0.7f && p.y > c.GroundY() + 0.3f * c.Height();
                if (!wall && !table) continue;
                side = sd;
                point = p + nrm * (c.BodyRadius(u + 2));
                Aim(c, f, u, u + 1, u + 2, point);
                const BodyState hs = c.Body(u + 2);
                const Vec3 tip = hs.position + hs.rotation * (Vec3::Up * c.BodyHalfLength(u + 2));
                // The hand grips wherever it meets the surface (it may have slid
                // along it on the way), not only at the aimed point.
                const float gap = Vec3::Dot(tip - p, nrm);
                if (gap < 0.08f * c.Height() / 1.8f && Vec3::Distance(tip, point) < 0.8f * reach) {
                    point = tip - nrm * gap + nrm * c.BodyRadius(u + 2);
                    c.m_grabBody = u + 2;
                    c.Physics()->SetPin(u + 2, Vec3::Up * c.BodyHalfLength(u + 2), point,
                                        c.TotalMass() * 9.81f / (0.06f * c.Height() / 1.8f));
                }
                return;
            }
        }
    }

    static void Aim(DynamicCharacter& c, Frame& f, int ua, int fa, int hand, const Vec3& p) {
        const Vec3 fwd = c.AnimForward();
        const Vec3 d = SafeNorm(p - c.Anchor(ua), fwd);
        f.want[ua] = c.Aim(ua, d, fwd); f.has[ua] = true;
        f.want[fa] = c.Aim(fa, d, fwd); f.has[fa] = true;
        f.want[hand] = c.Aim(hand, d, fwd); f.has[hand] = true;
        f.str[ua] *= 1.5f; f.str[fa] *= 1.5f;
    }
};

// Brace for the fall: both arms reach out toward where the body will meet the
// ground, a little apart, stiff enough to take some of the landing.
class Brace : public DynBehaviour {
public:
    const char* Name() const override { return "BraceForFall"; }
    void Run(DynamicCharacter& c, Frame& f, float) override {
        if (!enabled || !c.tuning.brace || c.State() != CharState::Falling) return;
        const Vec3 dir = FallDirection(c);
        if (Backward(c, dir) && c.tuning.protectHead) return;   // the arms protect the head instead
        const Vec3 com = c.CentreOfMass();
        for (int side = 0; side < 2; ++side) {
            const int ua = side == 0 ? LUpperArm : RUpperArm;
            if (c.m_grabBody == ua + 2) continue;
            const Vec3 sh = c.Anchor(ua);
            Vec3 p = Vec3{com.x, c.GroundY(), com.z} + dir * (0.45f * c.Height()) + c.Lateral(side) * (0.12f * c.Height());
            const Vec3 d = SafeNorm(p - sh, Vec3::Down);
            const Vec3 d2 = SafeNorm(d + Vec3::Up * 0.15f, d);
            f.want[ua] = c.Aim(ua, d, dir); f.has[ua] = true;
            f.want[ua + 1] = c.Aim(ua + 1, d2, dir); f.has[ua + 1] = true;
            f.want[ua + 2] = c.Aim(ua + 2, d2, dir); f.has[ua + 2] = true;
            f.str[ua] = Mathf::Max(f.str[ua], 1.2f);
            f.str[ua + 1] = Mathf::Max(f.str[ua + 1], 1.2f);
        }
    }
    static Vec3 FallDirection(DynamicCharacter& c) {
        const Vec3 v = Flat(c.CentreOfMassVelocity());
        if (v.Magnitude() > 0.3f) return v.Normalized();
        const BodyState t = c.Body(Torso);
        return SafeNorm(Flat(t.rotation * Vec3::Up), c.AnimForward());
    }
    static bool Backward(DynamicCharacter& c, const Vec3& dir) {
        return Vec3::Dot(dir, c.BodyForward()) < -0.35f;
    }
};

// Protect the head: falling backwards (or the head moving fast toward the
// ground), the hands come up beside the head and the chin tucks in.
class ProtectHead : public DynBehaviour {
public:
    const char* Name() const override { return "ProtectHead"; }
    void Run(DynamicCharacter& c, Frame& f, float) override {
        if (!enabled || !c.tuning.protectHead || c.State() != CharState::Falling) return;
        const Vec3 dir = Brace::FallDirection(c);
        const bool fastHead = c.Body(Head).velocity.y < -2.5f;
        if (!Brace::Backward(c, dir) && !fastHead) return;
        // Poses in the upright heading frame, turned the way the chest is now.
        const Vec3 fwd = c.AnimForward();
        const Quat R = (c.Body(Torso).rotation * c.HeadingPose(Torso, fwd).Inverse()).Normalized();
        for (int side = 0; side < 2; ++side) {
            const int ua = side == 0 ? LUpperArm : RUpperArm;
            f.want[ua] = (R * c.Aim(ua, c.FromHeading(Vec3{0.55f, 0.55f, 0.6f}, fwd, side), fwd)).Normalized();
            f.want[ua + 1] = (R * c.Aim(ua + 1, c.FromHeading(Vec3{-0.6f, 0.75f, -0.25f}, fwd, side), fwd)).Normalized();
            f.want[ua + 2] = f.want[ua + 1];
            f.has[ua] = f.has[ua + 1] = f.has[ua + 2] = true;
            f.str[ua] = Mathf::Max(f.str[ua], 1.0f);
            f.str[ua + 1] = Mathf::Max(f.str[ua + 1], 1.0f);
        }
        // Chin to chest.
        f.want[Head] = (R * (DynamicCharacter::Pitch(25.0f, fwd) * c.HeadingPose(Head, fwd))).Normalized();
        f.has[Head] = true;
        f.str[Head] = Mathf::Max(f.str[Head], 1.0f);
    }
};

// Get up from the floor, face down (all fours, kneel, stand) or face up (sit,
// squat, stand). Poses are ours; a helper spring on the pelvis and chest does
// the lifting (not physical, stated in the docs), the drives shape the limbs.
class GetUpModule : public DynBehaviour {
public:
    const char* Name() const override { return "GetUp"; }
    float still = 0.0f;
    Vec3  fwd, startXZ;
    Quat  pelvis0, torso0;
    float h0 = 0.0f;
    int   tries = 0;
    bool  requested = false;

    struct Key { float s, h, pelvisPitch, torsoPitch; Vec3 thigh, shin, upper, fore; };

    void Begin(DynamicCharacter& c) {
        const BodyState t = c.Body(Torso), p = c.Body(Hips);
        const Vec3 axis = t.rotation * Vec3::Up;                    // hips -> head
        c.m_faceDown = c.ChestFront().y < 0.0f;
        fwd = SafeNorm(Flat(c.m_faceDown ? axis : axis * -1.0f), c.AnimForward());
        startXZ = Vec3{p.position.x, 0.0f, p.position.z};
        pelvis0 = p.rotation; torso0 = t.rotation;
        h0 = c.PelvisHeight();
        c.ChangeState(CharState::GettingUp);
    }

    void Run(DynamicCharacter& c, Frame& f, float h) override {
        const CharState s = c.State();
        if (s == CharState::Down) {
            if (!enabled || c.Dead() || c.mode == RagdollMode::Limp) { still = 0.0f; return; }
            still = c.MaxSpeed() < 0.35f ? still + h : 0.0f;
            if ((c.tuning.autoGetUp || requested) && (still > c.tuning.getUpDelay || (requested && still > 0.2f))) {
                requested = false;
                Begin(c);
            }
            return;
        }
        if (s != CharState::GettingUp) { still = 0.0f; return; }
        const float T = Mathf::Max(0.5f, c.tuning.getUpTime);
        const float u = c.StateTime() / T;
        const float H = c.StandingHeight();
        const float L1 = c.SegmentLength(LThigh);
        Key keys[4];
        if (c.m_faceDown) {
            keys[0] = {0.30f, L1 * 0.95f, 85.0f, 80.0f, {0, -1, 0}, {0, -0.15f, -1}, {0.05f, -1, 0.1f}, {0, -1, 0.05f}};
            keys[1] = {0.60f, L1 * 1.05f + 0.1f * H, 15.0f, 15.0f, {0, -1, 0.1f}, {0, -0.1f, -1}, {0.25f, -1, 0}, {0.2f, -1, 0}};
            keys[2] = {0.82f, 0.78f * H, 20.0f, 15.0f, {0, -0.9f, 0.35f}, {0, -1, -0.2f}, {0.25f, -1, 0.2f}, {0.2f, -1, 0.2f}};
            keys[3] = {1.00f, H, 0.0f, 0.0f, {0, -1, 0}, {0, -1, 0}, {0.15f, -1, 0}, {0.1f, -1, 0}};
        } else {
            keys[0] = {0.30f, 0.25f * H, -5.0f, 25.0f, {0.05f, 0, 1}, {0.05f, -0.05f, 1}, {0.3f, -0.8f, -0.5f}, {0.2f, -0.9f, -0.3f}};
            keys[1] = {0.58f, 0.45f * H, 30.0f, 45.0f, {0.1f, 0.3f, 1}, {0, -1, -0.35f}, {0.25f, -0.2f, 1}, {0.2f, -0.1f, 1}};
            keys[2] = {0.82f, 0.74f * H, 22.0f, 25.0f, {0.05f, -0.6f, 0.8f}, {0, -1, -0.2f}, {0.2f, -0.4f, 1}, {0.2f, -0.3f, 1}};
            keys[3] = {1.00f, H, 0.0f, 0.0f, {0, -1, 0}, {0, -1, 0}, {0.15f, -1, 0}, {0.1f, -1, 0}};
        }
        int k = 0;
        while (k < 3 && u > keys[k].s) ++k;
        const Key& b = keys[k];
        const Key* a = k > 0 ? &keys[k - 1] : nullptr;
        const float a0 = a ? a->s : 0.0f;
        const float w = Smooth((u - a0) / Mathf::Max(1e-3f, b.s - a0));
        auto mixv = [&](const Vec3& x, const Vec3& y) { return a ? SafeNorm(Vec3::Lerp(x, y, w), y) : SafeNorm(y, y); };
        const Vec3 thigh = mixv(a ? a->thigh : b.thigh, b.thigh), shin = mixv(a ? a->shin : b.shin, b.shin);
        const Vec3 up = mixv(a ? a->upper : b.upper, b.upper), fore = mixv(a ? a->fore : b.fore, b.fore);
        const float height = a ? Mathf::Lerp(a->h, b.h, w) : Mathf::Lerp(h0, b.h, w);
        const Quat pelvisKey = (DynamicCharacter::Pitch(b.pelvisPitch, fwd) * c.HeadingPose(Hips, fwd)).Normalized();
        const Quat torsoKey = (DynamicCharacter::Pitch(b.torsoPitch, fwd) * c.HeadingPose(Torso, fwd)).Normalized();
        Quat pelvisWant, torsoWant;
        if (a) {
            pelvisWant = Quat::Slerp((DynamicCharacter::Pitch(a->pelvisPitch, fwd) * c.HeadingPose(Hips, fwd)).Normalized(), pelvisKey, w);
            torsoWant = Quat::Slerp((DynamicCharacter::Pitch(a->torsoPitch, fwd) * c.HeadingPose(Torso, fwd)).Normalized(), torsoKey, w);
        } else {
            pelvisWant = Quat::Slerp(pelvis0, pelvisKey, w);
            torsoWant = Quat::Slerp(torso0, torsoKey, w);
        }
        for (int side = 0; side < 2; ++side) {
            const int th = side == 0 ? LThigh : RThigh;
            const int ua = side == 0 ? LUpperArm : RUpperArm;
            f.want[th] = c.Aim(th, c.FromHeading(thigh, fwd, side), fwd); f.has[th] = true;
            f.want[th + 1] = c.Aim(th + 1, c.FromHeading(shin, fwd, side), fwd); f.has[th + 1] = true;
            f.want[th + 2] = c.HeadingPose(th + 2, fwd); f.has[th + 2] = true;
            f.want[ua] = c.Aim(ua, c.FromHeading(up, fwd, side), fwd); f.has[ua] = true;
            f.want[ua + 1] = c.Aim(ua + 1, c.FromHeading(fore, fwd, side), fwd); f.has[ua + 1] = true;
            f.want[ua + 2] = f.want[ua + 1]; f.has[ua + 2] = true;
        }
        f.want[Hips] = pelvisWant; f.has[Hips] = true;
        f.want[Torso] = torsoWant; f.has[Torso] = true;
        f.want[Head] = c.HeadingPose(Head, fwd); f.has[Head] = true;
        const float M = c.TotalMass(), help = Mathf::Max(0.0f, c.tuning.helperForce);
        const Vec3 target{startXZ.x, c.GroundY() + height, startXZ.z};
        const BodyState p = c.Body(Hips);
        // Hold the pelvis over its start point but let it settle where the legs put it.
        const Vec3 aim{Mathf::Lerp(p.position.x, target.x, 0.3f), target.y, Mathf::Lerp(p.position.z, target.z, 0.3f)};
        c.Physics()->SetHelper(Hips, aim, help * M * 60.0f, pelvisWant, help * M * 25.0f, 6.0f * Mathf::Min(1.0f, help));
        c.Physics()->SetHelper(Torso, Vec3::Zero, 0.0f, torsoWant, help * M * 15.0f, 4.0f * Mathf::Min(1.0f, help));
        c.Physics()->AddForce(Hips, Vec3::Up * (0.5f * M * 9.81f * help));
        if (u >= 1.0f) {
            c.Physics()->ClearHelpers();
            const bool up = c.PelvisHeight() > 0.82f * H && c.TorsoLean() < 25.0f;
            if (up) {
                tries = 0;
                c.ChangeState(c.mode == RagdollMode::Powered ? CharState::Balancing : CharState::Blending);
            } else {
                ++tries;
                c.ChangeState(CharState::Down);
            }
        }
    }
};

} // namespace

// =============================================================================
// DynamicCharacter
// =============================================================================
DynamicCharacter::DynamicCharacter() : m_phys(new XpbdRagdoll()) {
    m_mod[0].reset(new HitReaction());
    m_mod[1].reset(new Balance());
    m_mod[2].reset(new Grab());
    m_mod[3].reset(new Brace());
    m_mod[4].reset(new ProtectHead());
    m_mod[5].reset(new GetUpModule());
    for (int b = 0; b < kBones; ++b) { m_joint[b] = -1; m_bindQ[b] = m_off[b] = m_animQ[b] = m_relAnim[b] = Quat::Identity; }
}

DynamicCharacter::~DynamicCharacter() = default;

int DynamicCharacter::BehaviourCount() const { return 6; }
DynBehaviour* DynamicCharacter::Behaviour(int i) { return i >= 0 && i < 6 ? m_mod[i].get() : nullptr; }
DynBehaviour* DynamicCharacter::FindBehaviour(const char* name) {
    if (!name) return nullptr;
    for (auto& m : m_mod) {
        const char* a = m->Name();
        const char* b = name;
        while (*a && *b && ((*a | 32) == (*b | 32))) { ++a; ++b; }
        if (!*a && !*b) return m.get();
    }
    return nullptr;
}

bool DynamicCharacter::Build(const RigInput& rig) {
    m_built = false;
    m_phys->Clear();
    for (int b = 0; b < kBones; ++b)
        if (!Fin(rig.pos[b]) || !Fin(rig.rot[b]) || rig.rot[b].Magnitude() < 1e-4f) return false;
    m_mass = Fin(rig.mass) ? Mathf::Clamp(rig.mass, 1.0f, 1000.0f) : 70.0f;
    m_groundY = Fin(rig.groundY) ? rig.groundY : Mathf::Min(rig.pos[LFoot].y, rig.pos[RFoot].y);
    const float H = (rig.pos[Head].y - m_groundY) / 0.87f;
    if (!(H > 0.2f) || H > 100.0f) return false;
    m_height = H;
    Vec3 lat = Flat(rig.pos[LUpperArm] - rig.pos[RUpperArm]);
    if (lat.Magnitude() < 0.05f * H) lat = Flat(rig.pos[LThigh] - rig.pos[RThigh]);
    if (lat.Magnitude() < 0.02f * H) return false;
    lat = lat.Normalized();
    Vec3 fwd = Flat(Fin(rig.forward) ? rig.forward : Vec3::Forward);
    fwd = fwd - lat * Vec3::Dot(fwd, lat);
    if (fwd.Magnitude() < 1e-3f) fwd = Vec3::Cross(lat, Vec3::Up);
    fwd = fwd.Normalized();
    m_fwd0 = fwd;
    m_lat0[0] = lat; m_lat0[1] = lat * -1.0f;

    Vec3 start[kBones], end[kBones];
    for (int b = 0; b < kBones; ++b) {
        start[b] = rig.pos[b];
        if (kChild[b] >= 0) { end[b] = rig.pos[kChild[b]]; continue; }
        if (b == Head) end[b] = rig.pos[Head] + Vec3::Up * (0.13f * H);
        else if (b == LFoot || b == RFoot) {
            start[b] = rig.pos[b] - fwd * (0.02f * H);
            end[b] = rig.pos[b] + fwd * (0.12f * H);
        } else end[b] = rig.pos[b] + SafeNorm(rig.pos[b] - rig.pos[kParent[b]], Vec3::Down) * (0.08f * H);
    }
    Vec3 centre[kBones];
    for (int b = 0; b < kBones; ++b) {
        const Vec3 axis = end[b] - start[b];
        const float L = axis.Magnitude();
        if (!(L > 0.015f * H)) return false;
        m_segLen[b] = Vec3::Distance(rig.pos[b], kChild[b] >= 0 ? rig.pos[kChild[b]] : end[b]);
        m_half[b] = 0.5f * L;
        m_radius[b] = kRadiusFrac[b] * H;
        centre[b] = (start[b] + end[b]) * 0.5f;
        const Vec3 ax = axis * (1.0f / L);
        Quat q = Quat::FromToRotation(Vec3::Up, ax);
        Vec3 ref = fwd - ax * Vec3::Dot(fwd, ax);
        if (ref.Magnitude() < 0.3f) ref = Vec3::Up - ax * Vec3::Dot(Vec3::Up, ax);
        const Vec3 z = q * Vec3::Forward;
        q = (Quat::FromToRotation(z, ref.Normalized()) * q).Normalized();
        m_bindQ[b] = q;
        m_bindX[b] = centre[b];
    }
    m_footR = 0.012f * H;
    m_ankleH = Mathf::Max(0.0f, 0.5f * (rig.pos[LFoot].y + rig.pos[RFoot].y) - m_groundY);
    for (int b = 0; b < kBones; ++b) {
        BodyDesc d;
        d.position = centre[b];
        d.rotation = m_bindQ[b];
        const float m = kMassFrac[b] * m_mass, r = m_radius[b], L = 2.0f * m_half[b];
        d.mass = m;
        const float Ip = m * (3.0f * r * r + L * L) / 12.0f, Ia = 0.5f * m * r * r;
        d.inertia = Vec3{Ip, Ia, Ip};
        d.friction = (b == LFoot || b == RFoot) ? 1.0f : 0.6f;
        const Quat inv = m_bindQ[b].Inverse();
        if (b == LFoot || b == RFoot) {
            const Vec3 ankle = rig.pos[b];
            const Vec3 l = (b == LFoot ? m_lat0[0] : m_lat0[1]);
            for (int i = 0; i < 4; ++i) {
                const float along = (i < 2) ? -0.035f * H : 0.11f * H;
                const float side = (i % 2 == 0) ? -0.022f * H : 0.022f * H;
                Vec3 p = ankle + fwd * along + l * side;
                p.y = m_groundY + m_footR;
                d.spheres.push_back({inv * (p - centre[b]), m_footR});
            }
        } else {
            d.spheres.push_back({Vec3::Up * m_half[b], r});
            d.spheres.push_back({Vec3::Up * -m_half[b], r});
            if (b == Torso || b == Hips) d.spheres.push_back({Vec3::Zero, r});
        }
        m_phys->AddBody(d);
    }
    for (int b = 1; b < kBones; ++b) {
        JointDesc j;
        j.parent = kParent[b]; j.child = b;
        j.anchor = rig.pos[b];
        switch (b) {
            case Torso: j.swing = 0.6f; j.twist = 0.5f; break;
            case Head:  j.swing = 0.7f; j.twist = 0.9f; break;
            case LUpperArm: case RUpperArm: j.swing = 2.3f; j.twist = 1.2f; break;
            case LHand: case RHand: j.swing = 0.9f; j.twist = 0.4f; break;
            case LThigh: case RThigh: j.swing = 1.7f; j.twist = 0.6f; break;
            case LFoot: case RFoot: j.swing = 0.6f; j.twist = 0.3f; break;
            case LForeArm: case RForeArm: {
                const Vec3 armDir = SafeNorm(rig.pos[b] - rig.pos[kParent[b]], Vec3::Down);
                j.hinge = true;
                j.hingeAxis = SafeNorm(Vec3::Cross(armDir, fwd), lat);
                j.hingeMin = -0.05f; j.hingeMax = 2.6f;
                break;
            }
            case LShin: case RShin: {
                const Vec3 thighDir = SafeNorm(rig.pos[b] - rig.pos[kParent[b]], Vec3::Down);
                j.hinge = true;
                j.hingeAxis = SafeNorm(Vec3::Cross(fwd, thighDir), lat);
                j.hingeMin = -0.05f; j.hingeMax = 2.5f;
                break;
            }
            default: break;
        }
        m_joint[b] = m_phys->AddJoint(j);
        if (m_joint[b] < 0) { m_phys->Clear(); return false; }
        m_anchorLocal[b] = m_bindQ[kParent[b]].Inverse() * (rig.pos[b] - centre[kParent[b]]);
    }
    // What each joint carries, for its stiffness.
    float arm = 0.0f;
    for (int b = LUpperArm; b <= LHand; ++b) arm += kMassFrac[b];
    for (int b = 0; b < kBones; ++b) {
        float carried = kMassFrac[b];
        if (b == Torso) carried = kMassFrac[Torso] + kMassFrac[Head] + 2.0f * arm;
        else if (b == LUpperArm || b == RUpperArm) carried = arm;
        else if (b == LForeArm || b == RForeArm) carried = kMassFrac[LForeArm] + kMassFrac[LHand];
        else if (IsLeg(b)) carried = 0.6f;
        m_support[b] = carried * m_mass;
    }
    for (int b = 0; b < kBones; ++b) {
        m_off[b] = (rig.rot[b].Normalized().Inverse() * m_bindQ[b]).Normalized();
        m_posOff[b] = rig.rot[b].Normalized().Inverse() * (centre[b] - rig.pos[b]);
        m_animX[b] = m_animPrevX[b] = m_bindX[b];
        m_animQ[b] = m_animPrevQ[b] = m_bindQ[b];
        m_animV[b] = m_animW[b] = Vec3::Zero;
        m_relax[b] = 0.0f;
    }
    for (int b = 1; b < kBones; ++b) m_relAnim[b] = (m_bindQ[kParent[b]].Inverse() * m_bindQ[b]).Normalized();
    m_chestFwdLocal = m_bindQ[Torso].Inverse() * fwd;
    m_pelvisH0 = centre[Hips].y - m_groundY;
    m_groundNow = m_groundY;
    m_footCentreH = centre[LFoot].y - m_groundY;
    m_comH0 = CentreOfMass().y - m_groundY;
    m_built = true;
    m_haveAnim = false;
    m_haveAnimPrev = false;
    m_state = CharState::Animated;
    m_stateTime = 0.0f;
    m_dead = false;
    SetMode(mode);
    return true;
}

void DynamicCharacter::SetAnimationPose(const Vec3 pos[kBones], const Quat rot[kBones]) {
    if (!m_built || !pos || !rot) return;
    for (int b = 0; b < kBones; ++b)
        if (!Fin(pos[b]) || !Fin(rot[b]) || rot[b].Magnitude() < 1e-4f) return;   // keep last good pose
    for (int b = 0; b < kBones; ++b) {
        const Quat r = rot[b].Normalized();
        m_animQ[b] = (r * m_off[b]).Normalized();
        m_animX[b] = pos[b] + r * m_posOff[b];
    }
    for (int b = 1; b < kBones; ++b) m_relAnim[b] = (m_animQ[kParent[b]].Inverse() * m_animQ[b]).Normalized();
    if (!m_haveAnim) {
        for (int b = 0; b < kBones; ++b) { m_animPrevX[b] = m_animX[b]; m_animPrevQ[b] = m_animQ[b]; }
    }
    m_haveAnim = true;
}

void DynamicCharacter::Tick(float dt, const ContactWorld* world) {
    if (!m_built || !Fin(dt) || dt <= 0.0f) return;
    m_world = world;
    dt = Mathf::Min(dt, 0.25f);
    // Velocity of the animated bodies (to hand over motion when physics starts).
    if (m_haveAnim) {
        for (int b = 0; b < kBones; ++b) {
            m_animV[b] = Vec3::ClampMagnitude((m_animX[b] - m_animPrevX[b]) * (1.0f / dt), 20.0f);
            m_animW[b] = Vec3::ClampMagnitude(RotVecOf(m_animQ[b] * m_animPrevQ[b].Inverse()) * (1.0f / dt), 30.0f);
            m_animPrevX[b] = m_animX[b]; m_animPrevQ[b] = m_animQ[b];
        }
    }
    m_accum = Mathf::Min(m_accum + dt, kStep * kMaxSteps);
    while (m_accum >= kStep) { FixedStep(kStep); m_accum -= kStep; }
}

void DynamicCharacter::TrackAnimation(float) {
    for (int b = 0; b < kBones; ++b) m_phys->SetState(b, {m_animX[b], m_animQ[b], m_animV[b], m_animW[b]});
}

void DynamicCharacter::StartPhysics() {
    if (m_state != CharState::Animated && m_state != CharState::Blending) return;
    if (m_state == CharState::Blending) {
        for (int b = 0; b < kBones; ++b) {
            const float w = PhysicsWeight();
            const Vec3 x = Vec3::Lerp(m_animX[b], m_frozenX[b], w);
            const Quat q = Quat::Slerp(m_animQ[b], m_frozenQ[b], w);
            m_phys->SetState(b, {x, q, m_animV[b], m_animW[b]});
        }
    } else TrackAnimation(0.0f);
    m_groundNow = Mathf::Min(m_animX[LFoot].y, m_animX[RFoot].y) - m_footCentreH;
    for (int b = 1; b < kBones; ++b) m_relDrive[b] = m_relAnim[b];
    m_haveDrive = true;
    m_disturbedAt = m_clock;
    m_steps = 0;
    m_stableTime = 0.0f;
    m_airTime = 0.0f;
    ChangeState(CharState::Balancing);
}

void DynamicCharacter::ChangeState(CharState s) {
    if (s == m_state) return;
    const CharState was = m_state;
    if (s == CharState::Blending) {
        // Freeze the shown physics pose and ask the owner to move the root
        // under the body, facing where the body faces.
        for (int b = 0; b < kBones; ++b) {
            const BodyState st = m_phys->State(b);
            m_frozenX[b] = st.position; m_frozenQ[b] = st.rotation;
        }
        const Vec3 animHips = m_animX[Hips], physHips = m_frozenX[Hips];
        m_rootDelta = Vec3{physHips.x - animHips.x, 0.0f, physHips.z - animHips.z};
        const Vec3 bodyFwd = BodyForward();
        m_rootFwd = bodyFwd;
        m_rootGround = m_groundNow;
        m_rootMove = true;
        m_blendFrom = 1.0f;
        m_phys->ClearHelpers();
    }
    if (s == CharState::Falling && was != CharState::Falling) ++m_falls;
    if (s == CharState::Animated && was == CharState::Blending) m_lastRecovery = m_clock - m_disturbedAt;
    if (was == CharState::GettingUp && (s == CharState::Blending || s == CharState::Balancing)) ++m_getUps;
    if (s != CharState::GettingUp) m_phys->ClearHelpers();
    if (s != CharState::Balancing && s != CharState::Falling && m_grabBody >= 0) {
        m_phys->ClearPin(m_grabBody);   // a grip never outlives the stumble
        m_grabBody = -1;
    }
    m_state = s;
    m_stateTime = 0.0f;
    m_stableTime = 0.0f;
}

bool DynamicCharacter::RootMoveWanted(Vec3& delta, Vec3& forward, float* groundY) const {
    if (!m_rootMove) return false;
    if (groundY) *groundY = m_rootGround;
    delta = m_rootDelta;
    forward = m_rootFwd;
    return true;
}

void DynamicCharacter::RootMoved() { m_rootMove = false; }

float DynamicCharacter::RegionStrength(int b) const {
    float r = tuning.strength;
    if (b == Hips || b == Torso) r *= tuning.spineStrength;
    else if (b == Head) r *= tuning.neckStrength;
    else if (IsArm(b)) r *= tuning.armStrength;
    else r *= tuning.legStrength;
    return Fin(r) ? Mathf::Clamp(r, 0.0f, 10.0f) : 1.0f;
}

void DynamicCharacter::FixedStep(float h) {
    m_clock += h;
    m_stateTime += h;
    const float relaxRate = h / Mathf::Max(0.05f, tuning.hitRelaxTime);
    for (int b = 0; b < kBones; ++b) m_relax[b] = Mathf::Max(0.0f, m_relax[b] - relaxRate);

    if (m_state == CharState::Animated) {
        if (mode == RagdollMode::Powered) StartPhysics();
        else { TrackAnimation(h); return; }
    }
    if (m_state == CharState::Blending) {
        if (m_stateTime >= Mathf::Max(0.0f, tuning.blendTime)) {
            ChangeState(CharState::Animated);
            TrackAnimation(h);
        }
        return;
    }
    Frame f;
    for (int b = 0; b < kBones; ++b) { f.str[b] = RegionStrength(b); f.has[b] = false; }
    float scaleLeg = 1.0f, scaleSpine = 1.0f, scaleNeck = 1.0f, scaleArm = 1.0f, friction = 0.5f;
    switch (m_state) {
        case CharState::Falling:   scaleLeg = 0.3f; scaleSpine = 0.5f; scaleNeck = 0.6f; scaleArm = 0.7f; break;
        case CharState::Down:      scaleLeg = scaleSpine = scaleNeck = scaleArm = 0.12f; friction = 3.0f; break;
        case CharState::GettingUp: scaleLeg = 1.3f; scaleSpine = 1.3f; break;
        case CharState::Dead:      scaleLeg = scaleSpine = scaleNeck = scaleArm = 0.02f; friction = 3.0f; break;
        default: break;
    }
    if (mode == RagdollMode::Limp && m_state == CharState::Down) { scaleLeg = scaleSpine = scaleNeck = scaleArm = 0.02f; }
    for (int b = 0; b < kBones; ++b) {
        if (b == Hips || b == Torso) f.str[b] *= scaleSpine;
        else if (b == Head) f.str[b] *= scaleNeck;
        else if (IsArm(b)) f.str[b] *= scaleArm;
        else f.str[b] *= scaleLeg;
    }
    if (m_state != CharState::Dead) {
        for (auto& m : m_mod) {
            const CharState before = m_state;
            m->Run(*this, f, h);
            if (m_state != before && m_state == CharState::Blending) return;
        }
    }
    // A fall ends on the ground.
    if (m_state == CharState::Falling && (TorsoTouching() || BodyTouching(Hips)) && m_stateTime > 0.25f)
        ChangeState(CharState::Down);
    if (m_state == CharState::Falling && m_stateTime > 0.25f && MaxSpeed() < 0.3f) ChangeState(CharState::Down);
    ApplyDrives(f);
    m_phys->SetJointFriction(friction);
    m_phys->Step(h, m_world);
    TrackGround();
}

void DynamicCharacter::TrackGround() {
    // The lowest point of anything touching, feet first.
    float lo = 1e30f;
    for (int b = 0; b < kBones; ++b) {
        const float r = (b == LFoot || b == RFoot) ? m_footR : m_radius[b];
        for (int i = 0; i < m_phys->SphereCount(b); ++i) {
            Vec3 c;
            if (m_phys->SphereTouching(b, i, &c)) lo = Mathf::Min(lo, c.y - r);
        }
    }
    if (lo < 1e29f && Fin(lo)) m_groundNow = lo;
}

bool DynamicCharacter::BodyTouching(int b) const { return m_phys->BodyTouching(b); }

void DynamicCharacter::ApplyDrives(const Frame& f) {
    Quat dq[kBones];
    dq[Hips] = f.has[Hips] ? f.want[Hips] : m_phys->State(Hips).rotation;
    for (int b = 1; b < kBones; ++b) {
        const int p = kParent[b];
        Quat rel;
        if (f.has[b] && Fin(f.want[b])) { rel = (dq[p].Inverse() * f.want[b]).Normalized(); dq[b] = f.want[b]; }
        else { rel = m_relAnim[b]; dq[b] = (dq[p] * rel).Normalized(); }
        if (m_haveDrive) {
            const float maxDeg = Mathf::Max(1.0f, tuning.targetSpeed) * kStep;
            rel = Quat::RotateTowards(m_relDrive[b], rel, maxDeg).Normalized();
        }
        m_relDrive[b] = rel;
        const float s = Mathf::Max(0.0f, f.str[b]);
        const float k = Mathf::Max(0.0f, tuning.stiffnessPerKg) * m_support[b] * s;
        const float cap = (Mathf::Max(0.0f, tuning.torquePerKg) * m_support[b] + 15.0f) * Mathf::Min(s, 2.0f);
        m_phys->SetDrive(m_joint[b], rel, Fin(k) ? k : 0.0f, tuning.dampingRatio, Fin(cap) ? Mathf::Max(cap, 1e-3f) : 0.0f);
    }
    m_haveDrive = true;
    // The pelvis has no parent joint: when a behaviour wants it turned, the
    // hip drives above carry that wish against the legs (the stance-hip torque).
}

float DynamicCharacter::PhysicsWeight() const {
    switch (m_state) {
        case CharState::Animated: return 0.0f;
        case CharState::Blending: {
            const float T = tuning.blendTime;
            return T > 1e-4f ? Mathf::Clamp01(1.0f - m_stateTime / T) * m_blendFrom : 0.0f;
        }
        default: return 1.0f;
    }
}

void DynamicCharacter::BonePose(int bone, Vec3& pos, Quat& rot) const {
    if (!m_built || bone < 0 || bone >= kBones) return;
    Vec3 x; Quat q;
    if (m_state == CharState::Animated) { x = m_animX[bone]; q = m_animQ[bone]; }
    else if (m_state == CharState::Blending) {
        const float w = PhysicsWeight();
        x = Vec3::Lerp(m_animX[bone], m_frozenX[bone], w);
        q = Quat::Slerp(m_animQ[bone], m_frozenQ[bone], w).Normalized();
    } else {
        const BodyState s = m_phys->State(bone);
        x = s.position; q = s.rotation;
    }
    rot = (q * m_off[bone].Inverse()).Normalized();
    pos = x - rot * m_posOff[bone];
}

BodyState DynamicCharacter::Body(int bone) const {
    if (m_state == CharState::Animated && m_built && bone >= 0 && bone < kBones)
        return {m_animX[bone], m_animQ[bone], m_animV[bone], m_animW[bone]};
    return m_phys->State(bone);
}

Vec3 DynamicCharacter::CentreOfMass() const {
    Vec3 c; float m = 0.0f;
    for (int b = 0; b < kBones; ++b) { const float w = m_phys->Mass(b); c += m_phys->State(b).position * w; m += w; }
    return m > 0.0f ? c * (1.0f / m) : c;
}

Vec3 DynamicCharacter::CentreOfMassVelocity() const {
    Vec3 c; float m = 0.0f;
    for (int b = 0; b < kBones; ++b) { const float w = m_phys->Mass(b); c += m_phys->State(b).velocity * w; m += w; }
    return m > 0.0f ? c * (1.0f / m) : c;
}

float DynamicCharacter::PelvisHeight() const {
    float ground = m_groundY;
    Vec3 pts[16];
    const int n = SupportPoints(pts, 16);
    if (n > 0) { float lo = 1e9f; for (int i = 0; i < n; ++i) lo = Mathf::Min(lo, pts[i].y); ground = lo - m_footR; }
    return m_phys->State(Hips).position.y - ground;
}

float DynamicCharacter::TorsoLean() const {
    const Vec3 up = m_phys->State(Torso).rotation * Vec3::Up;
    return std::acos(Mathf::Clamp(up.y, -1.0f, 1.0f)) * Mathf::Rad2Deg;
}

float DynamicCharacter::MaxSpeed() const {
    float m = 0.0f;
    for (int b = 0; b < kBones; ++b) m = Mathf::Max(m, m_phys->State(b).velocity.Magnitude());
    return m;
}

bool DynamicCharacter::TorsoTouching() const {
    return m_phys->BodyTouching(Torso) || m_phys->BodyTouching(Head);
}

Vec3 DynamicCharacter::AnimForward() const {
    const Quat yaw = (m_animQ[Hips] * m_bindQ[Hips].Inverse()).Normalized();
    return SafeNorm(Flat(yaw * m_fwd0), m_fwd0);
}

Vec3 DynamicCharacter::BodyForward() const {
    const Vec3 f = m_phys->State(Torso).rotation * m_chestFwdLocal;
    Vec3 flat = Flat(f);
    if (flat.Magnitude() < 0.2f) flat = Flat(m_phys->State(Torso).rotation * (m_bindQ[Torso].Inverse() * Vec3::Up)) * (f.y < 0 ? 1.0f : -1.0f);
    return SafeNorm(flat, AnimForward());
}

Vec3 DynamicCharacter::ChestFront() const { return m_phys->State(Torso).rotation * m_chestFwdLocal; }

Vec3 DynamicCharacter::Lateral(int side) const {
    const Quat yaw = YawBetween(m_fwd0, AnimForward());
    return yaw * m_lat0[side == 0 ? 0 : 1];
}

Vec3 DynamicCharacter::Anchor(int bone) const {
    if (bone <= 0 || bone >= kBones) return m_phys->State(Hips).position;
    const BodyState p = m_phys->State(kParent[bone]);
    return p.position + p.rotation * m_anchorLocal[bone];
}

Quat DynamicCharacter::HeadingPose(int bone, const Vec3& fwd) const {
    if (bone < 0 || bone >= kBones) return Quat::Identity;
    return (YawBetween(m_fwd0, fwd) * m_bindQ[bone]).Normalized();
}

Quat DynamicCharacter::Aim(int bone, const Vec3& dir, const Vec3& fwd) const {
    const Quat ref = HeadingPose(bone, fwd);
    if (dir.SqrMagnitude() < 1e-10f || !Fin(dir)) return ref;
    return (Quat::FromToRotation(ref * Vec3::Up, dir) * ref).Normalized();
}

Vec3 DynamicCharacter::FromHeading(const Vec3& v, const Vec3& fwd, int side) const {
    const Quat yaw = YawBetween(m_fwd0, fwd);
    const Vec3 f = SafeNorm(Flat(fwd), m_fwd0);
    const Vec3 lat = yaw * m_lat0[side == 0 ? 0 : 1];
    return SafeNorm(lat * v.x + Vec3::Up * v.y + f * v.z, Vec3::Down);
}

void DynamicCharacter::LegIK(Frame& f, int side, const Vec3& hip, const Vec3& ankle, const Vec3& fwd) const {
    const int th = side == 0 ? LThigh : RThigh, sh = th + 1;
    const float L1 = m_segLen[th], L2 = m_segLen[sh];
    if (L1 <= 0.0f || L2 <= 0.0f || !Fin(hip) || !Fin(ankle)) return;
    const Vec3 d = ankle - hip;
    const float dist = Mathf::Clamp(d.Magnitude(), 0.05f * (L1 + L2), (L1 + L2) * 0.999f);
    const Vec3 dn = SafeNorm(d, Vec3::Down);
    const float a = (L1 * L1 - L2 * L2 + dist * dist) / (2.0f * dist);
    const float hk = std::sqrt(Mathf::Max(0.0f, L1 * L1 - a * a));
    Vec3 pole = fwd - dn * Vec3::Dot(fwd, dn);
    pole = SafeNorm(pole, Lateral(side));
    const Vec3 knee = hip + dn * a + pole * hk;
    const Vec3 ank = hip + dn * dist;
    f.want[th] = Aim(th, knee - hip, fwd); f.has[th] = true;
    f.want[sh] = Aim(sh, ank - knee, fwd); f.has[sh] = true;
}

void DynamicCharacter::FlatFoot(Frame& f, int side) const {
    const int ft = side == 0 ? LFoot : RFoot;
    // Keep the foot's current heading, flat on the ground.
    const Quat q = m_phys->State(ft).rotation;
    const Vec3 toe = q * Vec3::Up;
    f.want[ft] = HeadingPose(ft, SafeNorm(Flat(toe), AnimForward()));
    // HeadingPose turns the build pose; the build foot points along m_fwd0.
    f.has[ft] = true;
}

bool DynamicCharacter::FootSupport(int side, Vec3& centre) const {
    const int ft = side == 0 ? LFoot : RFoot;
    Vec3 sum; int n = 0;
    for (int i = 0; i < m_phys->SphereCount(ft); ++i) {
        Vec3 c;
        if (m_phys->SphereTouching(ft, i, &c)) { sum += c; ++n; }
    }
    if (n == 0) return false;
    centre = sum * (1.0f / (float)n);
    return true;
}

int DynamicCharacter::SupportPoints(Vec3* pts, int maxPts) const {
    int n = 0;
    for (int ft : {LFoot, RFoot})
        for (int i = 0; i < m_phys->SphereCount(ft) && n < maxPts; ++i) {
            Vec3 c;
            if (m_phys->SphereTouching(ft, i, &c)) pts[n++] = c;
        }
    return n;
}

int DynamicCharacter::NearestBody(const Vec3& p) const {
    int best = Torso; float bd = 1e30f;
    for (int b = 0; b < kBones; ++b) {
        const BodyState s = Body(b);
        const Vec3 ax = s.rotation * Vec3::Up;
        const float t = Mathf::Clamp(Vec3::Dot(p - s.position, ax), -m_half[b], m_half[b]);
        const float d = Vec3::Distance(s.position + ax * t, p) - m_radius[b];
        if (d < bd) { bd = d; best = b; }
    }
    return best;
}

void DynamicCharacter::Push(const Vec3& point, const Vec3& impulse) {
    if (!m_built || !Fin(point) || !Fin(impulse)) return;
    StartPhysics();
    if (m_state == CharState::GettingUp && impulse.Magnitude() > 0.15f * m_mass) ChangeState(CharState::Down);
    const int b = NearestBody(point);
    // Spread a little to the neighbours so a shove moves the body, not one limb.
    m_phys->ApplyImpulse(b, impulse * 0.6f, point);
    if (kParent[b] >= 0) m_phys->ApplyImpulse(kParent[b], impulse * 0.25f, point);
    if (b != Hips && kParent[b] != Hips) m_phys->ApplyImpulse(Hips, impulse * 0.15f, point);
    else m_phys->ApplyImpulse(Torso, impulse * 0.15f, point);
}

void DynamicCharacter::Hit(const Vec3& point, const Vec3& impulse) {
    if (!m_built || !Fin(point) || !Fin(impulse)) return;
    const int b = NearestBody(point);
    m_relax[b] = 1.0f;
    if (kParent[b] >= 0) m_relax[kParent[b]] = Mathf::Max(m_relax[kParent[b]], 0.6f);
    for (int c = 0; c < kBones; ++c) if (kParent[c] == b) m_relax[c] = Mathf::Max(m_relax[c], 0.6f);
    Push(point, impulse);
}

void DynamicCharacter::Shot(const Vec3& point, const Vec3& direction, float power) {
    if (!m_built || !Fin(point) || !Fin(direction) || !Fin(power)) return;
    const Vec3 dir = SafeNorm(direction, Vec3::Forward);
    power = Mathf::Clamp(power, 0.0f, 1e4f);
    const int b = NearestBody(point);
    Hit(point, dir * power);
    // A leg hit buckles the leg.
    if (IsLeg(b)) {
        const int top = Side(b) == 0 ? LThigh : RThigh;
        for (int i = top; i < top + 3; ++i) m_relax[i] = 1.0f;
    }
}

void DynamicCharacter::Explosion(const Vec3& centre, float force, float radius) {
    if (!m_built || !Fin(centre) || !Fin(force) || !Fin(radius) || radius <= 0.0f) return;
    force = Mathf::Clamp(force, -1e5f, 1e5f);
    bool any = false;
    for (int b = 0; b < kBones; ++b) {
        const BodyState s = Body(b);
        const Vec3 d = s.position - centre;
        const float dist = d.Magnitude();
        if (dist >= radius) continue;
        any = true;
    }
    if (!any) return;
    StartPhysics();
    float total = 0.0f;
    for (int b = 0; b < kBones; ++b) {
        const BodyState s = m_phys->State(b);
        const Vec3 d = s.position - centre;
        const float dist = d.Magnitude();
        if (dist >= radius) continue;
        const float fall = 1.0f - dist / radius;
        Vec3 dir = SafeNorm(d, Vec3::Up) + Vec3::Up * 0.3f;
        dir = dir.Normalized();
        const Vec3 J = dir * (force * fall * kMassFrac[b]);
        m_phys->ApplyImpulse(b, J, s.position);
        m_relax[b] = 1.0f;
        total += J.Magnitude();
    }
    if (total > 0.08f * m_mass * 9.81f) ChangeState(CharState::Falling);
}

void DynamicCharacter::Knockdown() {
    if (!m_built) return;
    StartPhysics();
    if (m_state == CharState::Balancing || m_state == CharState::GettingUp) ChangeState(CharState::Falling);
}

void DynamicCharacter::GetUp() {
    if (!m_built || m_dead) return;
    auto* g = static_cast<GetUpModule*>(m_mod[5].get());
    if (m_state == CharState::Down) g->requested = true;
}

void DynamicCharacter::Die() {
    if (!m_built) return;
    m_dead = true;
    if (m_state == CharState::Animated || m_state == CharState::Blending) StartPhysics();
    for (int b = 0; b < kBones; ++b) m_relax[b] = 1.0f;
    static_cast<Grab*>(m_mod[2].get())->Release(*this);
    ChangeState(CharState::Dead);
}

void DynamicCharacter::Revive() {
    if (!m_built || !m_dead) return;
    m_dead = false;
    ChangeState(CharState::Down);
    static_cast<GetUpModule*>(m_mod[5].get())->requested = true;
}

void DynamicCharacter::SetMode(RagdollMode m) {
    mode = m;
    if (!m_built) return;
    if (m == RagdollMode::Powered && m_state == CharState::Animated) StartPhysics();
    if (m == RagdollMode::Limp) {
        if (m_state == CharState::Animated || m_state == CharState::Blending) StartPhysics();
        if (m_state != CharState::Dead) ChangeState(CharState::Down);
    }
}

int DynamicCharacter::Raycast(const Vec3& origin, const Vec3& dir, float maxDist, Vec3* hitPoint) const {
    if (!m_built || !Fin(origin) || !Fin(dir) || !Fin(maxDist)) return -1;
    const Vec3 d = SafeNorm(dir, Vec3::Forward);
    int best = -1; float bt = maxDist;
    auto sphere = [&](const Vec3& c, float r, float& t) {
        const Vec3 m = origin - c;
        const float b = Vec3::Dot(m, d), cc = Vec3::Dot(m, m) - r * r;
        if (cc > 0.0f && b > 0.0f) return false;
        const float disc = b * b - cc;
        if (disc < 0.0f) return false;
        t = Mathf::Max(0.0f, -b - std::sqrt(disc));
        return true;
    };
    for (int b = 0; b < kBones; ++b) {
        const BodyState s = Body(b);
        const Vec3 u = s.rotation * Vec3::Up;
        const Vec3 a = s.position - u * m_half[b];
        const float L = 2.0f * m_half[b], r = m_radius[b];
        float t;
        // Cylinder part.
        const Vec3 m = origin - a;
        const Vec3 mp = m - u * Vec3::Dot(m, u), dp = d - u * Vec3::Dot(d, u);
        const float A = Vec3::Dot(dp, dp), B = 2.0f * Vec3::Dot(mp, dp), C = Vec3::Dot(mp, mp) - r * r;
        if (A > 1e-9f) {
            const float disc = B * B - 4.0f * A * C;
            if (disc >= 0.0f) {
                t = (-B - std::sqrt(disc)) / (2.0f * A);
                if (t < 0.0f && C < 0.0f) t = 0.0f;
                const float along = Vec3::Dot(m + d * t, u);
                if (t >= 0.0f && t < bt && along >= 0.0f && along <= L) { bt = t; best = b; }
            }
        }
        if (sphere(a, r, t) && t < bt) { bt = t; best = b; }
        if (sphere(a + u * L, r, t) && t < bt) { bt = t; best = b; }
    }
    if (best >= 0 && hitPoint) *hitPoint = origin + d * bt;
    return best;
}

} // namespace dyn
} // namespace okay
