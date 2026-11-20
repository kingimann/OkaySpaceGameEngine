#pragma once
// ---------------------------------------------------------------------------
// Camera maths: the small, pure pieces the camera rigs, the brain and the
// Camera component are built from. Nothing here touches a Scene, so every
// piece is tested with plain numbers over simulated frames
// (tests/test_camera_rig.cpp).
//
// Written from general algorithms, by us, in our own words:
//   * critically damped spring: the closed-form solution of x'' = -2w x' - w^2 x
//     (a standard second-order ODE), so the result does not depend on the
//     frame rate;
//   * gradient noise: lattice slopes blended with a quintic fade (the usual
//     textbook construction), hashed with our own integer mix;
//   * cubic Bezier easing: x(s) = t solved with Newton steps and a bisection
//     fallback (a Bezier curve is a cubic polynomial);
//   * bounding sphere of a few points: box centre and the furthest point;
//   * dead zone / soft zone: a per-axis rule defined in docs/gameplay/camera_upgrade.md.
// No code, shader text or tests were copied from any other project.
// ---------------------------------------------------------------------------
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace okay {

/// 1 - e^(-k dt): the share of the gap that closes in one frame at rate `k`
/// (frame-rate independent; k <= 0 = all of it at once).
inline float EaseFactor(float k, float dt) { return k > 0.0f ? 1.0f - std::exp(-k * dt) : 1.0f; }

namespace cam {

inline bool AllFinite(float v) { return std::isfinite(v); }
inline bool AllFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// ---- Critically damped spring --------------------------------------------------
/// Move `x` (with speed `v`) toward `target`. `omega` is the stiffness in 1/s:
/// higher is snappier (about 1/omega seconds to close most of the gap), with no
/// overshoot. With a steady target the answer at time T is the same at 30, 60
/// or 144 frames a second. omega <= 0 jumps straight there. A non-finite target
/// or step is ignored; a non-finite state is repaired.
inline void SpringStep(float& x, float& v, float target, float omega, float dt) {
    if (!AllFinite(target)) return;
    if (!AllFinite(x) || !AllFinite(v)) { x = target; v = 0.0f; return; }
    if (!(dt > 0.0f) || !AllFinite(dt)) return;
    if (!(omega > 0.0f)) { x = target; v = 0.0f; return; }
    const float d = x - target;
    const float e = std::exp(-omega * dt);
    const float t = v + omega * d;
    x = target + (d + t * dt) * e;
    v = (v - omega * t * dt) * e;
}

inline void SpringStep(Vec3& x, Vec3& v, const Vec3& target, float omega, float dt) {
    SpringStep(x.x, v.x, target.x, omega, dt);
    SpringStep(x.y, v.y, target.y, omega, dt);
    SpringStep(x.z, v.z, target.z, omega, dt);
}

// ---- Deterministic noise ---------------------------------------------------------
inline std::uint32_t HashU32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/// Smooth gradient noise in about [-1, 1]. The same (t, seed) always gives the
/// same number: no random-number generator, no clock.
inline float Noise1(float t, std::uint32_t seed) {
    const float fl = std::floor(t);
    const float fr = t - fl;
    const std::int32_t i = (std::int32_t)fl;
    auto slope = [&](std::int32_t cell) {
        const std::uint32_t h = HashU32((std::uint32_t)cell * 0x9E3779B1u ^ (seed * 0x85EBCA6Bu + 0x27d4eb2fu));
        return (float)(h & 0xFFFFu) / 32767.5f - 1.0f;   // [-1, 1]
    };
    const float u = fr * fr * fr * (fr * (fr * 6.0f - 15.0f) + 10.0f);   // quintic fade
    const float a = slope(i) * fr;
    const float b = slope(i + 1) * (fr - 1.0f);
    return 2.0f * (a + (b - a) * u);
}

// ---- Shake -------------------------------------------------------------------------
enum class ShakePreset { Impact = 0, Explosion = 1, Footstep = 2, Handheld = 3 };

struct ShakeParams {
    float posAmount = 0.3f;        ///< largest move at strength 1 (world units)
    float rotAmount = 2.0f;        ///< largest turn at strength 1 (degrees)
    float frequency = 18.0f;       ///< wobbles per second
    float duration = 0.5f;         ///< seconds until it has died away (<= 0 = keeps going)
    float falloffPower = 2.0f;     ///< 1 = straight fade, 2 = quick start and soft tail
    Vec3  posAxes{1.0f, 1.0f, 0.0f};   ///< which directions move
    Vec3  rotAxes{1.0f, 1.0f, 0.5f};   ///< which angles turn (pitch, yaw, roll)
};

inline const char* ShakePresetName(ShakePreset p) {
    switch (p) {
        case ShakePreset::Impact: return "Impact";
        case ShakePreset::Explosion: return "Explosion";
        case ShakePreset::Footstep: return "Footstep";
        case ShakePreset::Handheld: return "Handheld";
    }
    return "Impact";
}

inline ShakeParams ShakePresetParams(ShakePreset p) {
    ShakeParams s;
    switch (p) {
        case ShakePreset::Impact:      // a hit: short, sharp, fast wobble
            s.posAmount = 0.12f; s.rotAmount = 1.5f; s.frequency = 28.0f; s.duration = 0.25f; s.falloffPower = 2.0f;
            s.posAxes = {1.0f, 1.0f, 0.0f}; s.rotAxes = {1.0f, 1.0f, 0.5f}; break;
        case ShakePreset::Explosion:   // big and slow, rumbling on
            s.posAmount = 0.5f; s.rotAmount = 4.0f; s.frequency = 13.0f; s.duration = 1.4f; s.falloffPower = 1.5f;
            s.posAxes = {1.0f, 1.0f, 0.6f}; s.rotAxes = {1.0f, 1.0f, 1.0f}; break;
        case ShakePreset::Footstep:    // a small up-down bump
            s.posAmount = 0.025f; s.rotAmount = 0.2f; s.frequency = 9.0f; s.duration = 0.18f; s.falloffPower = 2.0f;
            s.posAxes = {0.0f, 1.0f, 0.0f}; s.rotAxes = {1.0f, 0.0f, 0.0f}; break;
        case ShakePreset::Handheld:    // someone holding the camera: slow, never stops
            s.posAmount = 0.02f; s.rotAmount = 0.4f; s.frequency = 1.6f; s.duration = 0.0f; s.falloffPower = 1.0f;
            s.posAxes = {1.0f, 1.0f, 0.3f}; s.rotAxes = {1.0f, 1.0f, 0.6f}; break;
    }
    return s;
}

/// Several shakes at once, each from its own noise. Step it every frame, then
/// read Pos() (world units, in the camera's own axes) and RotDeg().
class CameraShaker {
public:
    static constexpr int kMaxShakes = 16;

    /// Start a shake. `strength` is 0..1 (more is allowed and clamps at 1).
    void Add(const ShakeParams& p, float strength) {
        if (!AllFinite(strength) || strength <= 0.0f) return;
        Inst in; in.p = p; in.strength = std::min(1.0f, strength); in.age = 0.0f;
        in.seed = m_counter++ * 2654435761u + 12345u;
        if (p.duration <= 0.0f) {       // a sustained one replaces the previous sustained one
            for (Inst& o : m_inst) if (o.p.duration <= 0.0f) { o = in; return; }
        }
        if ((int)m_inst.size() >= kMaxShakes) m_inst.erase(m_inst.begin());
        m_inst.push_back(in);
    }
    void Add(ShakePreset p, float strength) { Add(ShakePresetParams(p), strength); }

    /// A steady handheld wobble that eases in and out. 0 = off.
    void SetHandheld(float amount, float fadeRate = 3.0f) {
        m_handheldTarget = AllFinite(amount) ? std::max(0.0f, std::min(1.0f, amount)) : 0.0f;
        m_handheldRate = fadeRate;
    }
    float Handheld() const { return m_handheld; }

    void Step(float dt) {
        if (!(dt > 0.0f) || !AllFinite(dt)) return;
        m_pos = {0, 0, 0}; m_rot = {0, 0, 0};
        for (std::size_t i = 0; i < m_inst.size();) {
            Inst& in = m_inst[i];
            in.age += dt;
            if (in.p.duration > 0.0f && in.age >= in.p.duration) { m_inst.erase(m_inst.begin() + (long)i); continue; }
            float env = 1.0f;
            if (in.p.duration > 0.0f) env = std::pow(std::max(0.0f, 1.0f - in.age / in.p.duration), std::max(0.1f, in.p.falloffPower));
            Accumulate(in.p, in.strength * env, in.age, in.seed);
            ++i;
        }
        m_handheld += (m_handheldTarget - m_handheld) * EaseFactor(m_handheldRate, dt);
        if (m_handheld < 1e-4f && m_handheldTarget <= 0.0f) m_handheld = 0.0f;
        if (m_handheld > 0.0f) Accumulate(ShakePresetParams(ShakePreset::Handheld), m_handheld, m_handheldClock += dt, 0xC0FFEEu);
    }
    const Vec3& Pos() const { return m_pos; }
    const Vec3& RotDeg() const { return m_rot; }
    bool Active() const { return !m_inst.empty() || m_handheld > 0.0f; }
    int Count() const { return (int)m_inst.size(); }
    void Clear() { m_inst.clear(); m_pos = {0, 0, 0}; m_rot = {0, 0, 0}; m_handheld = 0.0f; m_handheldTarget = 0.0f; }

private:
    struct Inst { ShakeParams p; float strength = 0.0f, age = 0.0f; std::uint32_t seed = 0; };
    void Accumulate(const ShakeParams& p, float k, float age, std::uint32_t seed) {
        const float t = age * p.frequency;
        m_pos += Vec3{Noise1(t, seed + 1u) * p.posAxes.x, Noise1(t, seed + 2u) * p.posAxes.y, Noise1(t, seed + 3u) * p.posAxes.z} * (p.posAmount * k);
        m_rot += Vec3{Noise1(t, seed + 4u) * p.rotAxes.x, Noise1(t, seed + 5u) * p.rotAxes.y, Noise1(t, seed + 6u) * p.rotAxes.z} * (p.rotAmount * k);
    }
    std::vector<Inst> m_inst;
    std::uint32_t m_counter = 1;
    Vec3 m_pos{0, 0, 0}, m_rot{0, 0, 0};
    float m_handheld = 0.0f, m_handheldTarget = 0.0f, m_handheldRate = 3.0f, m_handheldClock = 0.0f;
};

// ---- Blend curves -------------------------------------------------------------------
enum class BlendStyle { Cut = 0, Linear = 1, EaseInOut = 2, EaseIn = 3, EaseOut = 4, Custom = 5 };

inline const char* BlendStyleName(BlendStyle s) {
    switch (s) {
        case BlendStyle::Cut: return "Cut";
        case BlendStyle::Linear: return "Linear";
        case BlendStyle::EaseInOut: return "Ease In and Out";
        case BlendStyle::EaseIn: return "Ease In";
        case BlendStyle::EaseOut: return "Ease Out";
        case BlendStyle::Custom: return "Custom Curve";
    }
    return "Linear";
}

/// y at progress `x` on a cubic Bezier from (0,0) to (1,1) with control points
/// (x1,y1) and (x2,y2) (x's are kept in 0..1 so the curve never runs backward).
inline float BezierEase(float x, float x1, float y1, float x2, float y2) {
    if (!(x > 0.0f)) return 0.0f;
    if (!(x < 1.0f)) return 1.0f;
    x1 = std::max(0.0f, std::min(1.0f, x1)); x2 = std::max(0.0f, std::min(1.0f, x2));
    auto bx = [&](float s) { const float u = 1.0f - s; return 3.0f * u * u * s * x1 + 3.0f * u * s * s * x2 + s * s * s; };
    auto by = [&](float s) { const float u = 1.0f - s; return 3.0f * u * u * s * y1 + 3.0f * u * s * s * y2 + s * s * s; };
    auto dbx = [&](float s) { const float u = 1.0f - s; return 3.0f * u * u * x1 + 6.0f * u * s * (x2 - x1) + 3.0f * s * s * (1.0f - x2); };
    float s = x;
    for (int i = 0; i < 6; ++i) {            // Newton
        const float err = bx(s) - x;
        if (std::fabs(err) < 1e-6f) return by(s);
        const float d = dbx(s);
        if (std::fabs(d) < 1e-6f) break;
        s -= err / d;
        if (s < 0.0f || s > 1.0f) break;
    }
    float lo = 0.0f, hi = 1.0f; s = x;       // bisection fallback
    for (int i = 0; i < 40; ++i) {
        if (bx(s) < x) lo = s; else hi = s;
        s = 0.5f * (lo + hi);
    }
    return by(s);
}

/// Shape a blend's progress t (0..1) into the amount (0..1). `bez` holds the
/// four numbers of the Custom curve.
inline float BlendCurve(BlendStyle style, float t, const float bez[4] = nullptr) {
    if (!(t > 0.0f)) return 0.0f;
    if (!(t < 1.0f)) return 1.0f;
    switch (style) {
        case BlendStyle::Cut: return 1.0f;
        case BlendStyle::Linear: return t;
        case BlendStyle::EaseInOut: return t * t * (3.0f - 2.0f * t);
        case BlendStyle::EaseIn: return BezierEase(t, 0.42f, 0.0f, 1.0f, 1.0f);
        case BlendStyle::EaseOut: return BezierEase(t, 0.0f, 0.0f, 0.58f, 1.0f);
        case BlendStyle::Custom: return bez ? BezierEase(t, bez[0], bez[1], bez[2], bez[3]) : t;
    }
    return t;
}

/// A camera's pose and lens, the thing a blend mixes.
struct CamPose {
    Vec3 pos{0, 0, 0};
    Quat rot = Quat::Identity;
    float fov = 60.0f;
    float ortho = 5.0f;
    float dutch = 0.0f;
};

inline CamPose MixPoses(const CamPose& a, const CamPose& b, float t) {
    CamPose r;
    r.pos = Vec3::Lerp(a.pos, b.pos, t);
    r.rot = Quat::Slerp(a.rot, b.rot, t);
    r.fov = a.fov + (b.fov - a.fov) * t;
    r.ortho = a.ortho + (b.ortho - a.ortho) * t;
    r.dutch = a.dutch + (b.dutch - a.dutch) * t;
    return r;
}

// ---- Dead zone and soft zone ---------------------------------------------------------
/// How far to move toward the centre, on one axis, this frame.
///   off   where the target is, in view units (0 = the middle, 1 = the edge)
///   dead  half-size of the dead zone: inside it nothing moves
///   soft  half-size of the soft zone: past it the camera catches up at once
///   rate  how fast the camera eases the target back to the dead-zone edge (per second)
/// Returns a signed amount to shift the view by (same sign as `off`).
inline float ZoneCorrection(float off, float dead, float soft, float rate, float dt) {
    if (!AllFinite(off) || !AllFinite(dt)) return 0.0f;
    dead = std::max(0.0f, dead);
    soft = std::max(soft, dead);
    const float a = std::fabs(off);
    if (a <= dead) return 0.0f;
    float c = (a - dead) * EaseFactor(rate, dt);
    if (a - c > soft) c = a - soft;
    return off < 0.0f ? -c : c;
}

// ---- Look ahead -----------------------------------------------------------------------
/// Shifts the framing toward where the target is heading. The speed is smoothed
/// first (a target moved by a 50 Hz physics step looks jerky frame to frame at
/// 144 fps), then the shift eases with a spring so it never snaps.
struct LookaheadFilter {
    Vec3 offset{0, 0, 0};
    Vec3 Step(const Vec3& pos, float seconds, float maxDist, float omega, float dt, float teleportDist = 8.0f) {
        if (!AllFinite(pos) || !(dt > 0.0f) || !AllFinite(dt)) return offset;
        if (!m_have) { m_last = pos; m_have = true; return offset; }
        const Vec3 moved = pos - m_last;
        m_last = pos;
        if (moved.Magnitude() > teleportDist) { Reset(); m_last = pos; m_have = true; return offset; }
        const Vec3 raw = moved * (1.0f / dt);
        m_vel += (raw - m_vel) * EaseFactor(10.0f, dt);
        Vec3 want = m_vel * seconds;
        const float len = want.Magnitude();
        if (len > maxDist && len > 1e-6f) want = want * (maxDist / len);
        if (seconds <= 0.0f) want = {0, 0, 0};
        SpringStep(offset, m_springV, want, omega, dt);
        return offset;
    }
    void Reset() { offset = {0, 0, 0}; m_vel = {0, 0, 0}; m_springV = {0, 0, 0}; m_have = false; }
private:
    Vec3 m_last{0, 0, 0}, m_vel{0, 0, 0}, m_springV{0, 0, 0};
    bool m_have = false;
};

// ---- Groups ------------------------------------------------------------------------------
struct GroupFrame {
    bool valid = false;
    Vec3 center{0, 0, 0};
    float radius = 0.0f;
};

/// The box round `n` points: its middle and the distance to the furthest point.
inline GroupFrame FrameGroup(const Vec3* pts, int n, float padding = 0.0f) {
    GroupFrame g;
    int count = 0;
    Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    for (int i = 0; i < n; ++i) {
        if (!AllFinite(pts[i])) continue;
        if (count == 0) { lo = hi = pts[i]; }
        else {
            lo = {std::min(lo.x, pts[i].x), std::min(lo.y, pts[i].y), std::min(lo.z, pts[i].z)};
            hi = {std::max(hi.x, pts[i].x), std::max(hi.y, pts[i].y), std::max(hi.z, pts[i].z)};
        }
        ++count;
    }
    if (count == 0) return g;
    g.valid = true;
    g.center = (lo + hi) * 0.5f;
    float r = 0.0f;
    for (int i = 0; i < n; ++i) if (AllFinite(pts[i])) r = std::max(r, (pts[i] - g.center).Magnitude());
    g.radius = r + std::max(0.0f, padding);
    return g;
}

/// How far back a perspective camera must stand for a sphere of `radius` to fit
/// in view (the narrower of the vertical and horizontal angles decides).
inline float DistanceToFit(float radius, float vfovDeg, float aspect) {
    const float v = std::max(1.0f, std::min(170.0f, vfovDeg)) * 0.0174532925f * 0.5f;
    const float hh = std::atan(std::tan(v) * std::max(0.05f, aspect));
    const float half = std::min(v, hh);
    return radius / std::max(0.01f, std::sin(half));
}

/// Half-height an orthographic camera needs for a circle of `radius` to fit.
inline float OrthoSizeToFit(float radius, float aspect) {
    return std::max(radius, radius / std::max(0.05f, aspect));
}

// ---- 2D -----------------------------------------------------------------------------------
struct Box2 {
    float minX = -1e9f, minY = -1e9f, maxX = 1e9f, maxY = 1e9f;
    bool Contains(float x, float y) const { return x >= minX && x <= maxX && y >= minY && y <= maxY; }
    float Area() const { return (maxX - minX) * (maxY - minY); }
};

/// Keep a view (centre `c`, half size `half`) inside `box`. A box smaller than
/// the view on an axis holds the view in the middle of it, so nothing jitters.
inline Vec2 ConfineView(Vec2 c, Vec2 half, const Box2& box) {
    auto one = [](float v, float h, float lo, float hi) {
        if (hi - lo <= 2.0f * h) return 0.5f * (lo + hi);
        return std::max(lo + h, std::min(hi - h, v));
    };
    return {one(c.x, half.x, box.minX, box.maxX), one(c.y, half.y, box.minY, box.maxY)};
}

/// Which room the point is in. The current room is kept while the point is still
/// inside it (no flicker on a shared edge); otherwise the smallest room that
/// holds the point; with none, the current room stays (or -1).
inline int PickRoom(const Box2* rooms, int n, float x, float y, int current) {
    if (current >= 0 && current < n && rooms[current].Contains(x, y)) return current;
    int best = -1;
    for (int i = 0; i < n; ++i)
        if (rooms[i].Contains(x, y) && (best < 0 || rooms[i].Area() < rooms[best].Area())) best = i;
    return best >= 0 ? best : (current < n ? current : -1);
}

/// Round to the nearest 1/ppu, the pixel grid (pixels per world unit `ppu`).
inline float SnapToPixel(float v, float ppu) {
    return ppu > 0.0f ? std::round(v * ppu) / ppu : v;
}

/// The nearest orthographic half-height that makes every sprite pixel a whole
/// number of screen pixels: `screenH` pixels tall, `ppu` sprite pixels per world
/// unit. The zoom is a whole number (1x, 2x, 3x ...).
inline float PixelPerfectOrthoSize(float screenH, float ppu, float wantedSize) {
    if (!(screenH > 0.0f) || !(ppu > 0.0f) || !(wantedSize > 0.0f)) return wantedSize;
    const float scale = std::max(1.0f, std::round(screenH / (2.0f * wantedSize * ppu)));
    return screenH / (2.0f * ppu * scale);
}

// ---- Fading things that block the view -------------------------------------------------------
/// Tracks an opacity for each thing between the camera and its target: those
/// currently blocking fade down to `lowAlpha`, those that stopped fade back up.
/// `Step` returns the ids to restore (fully back, remove from the list).
class FadeTracker {
public:
    struct Entry { const void* id = nullptr; float original = 1.0f, alpha = 1.0f; bool blocking = false; };
    void Mark(const void* id, float originalAlpha) {
        for (Entry& e : m_e) if (e.id == id) { e.blocking = true; return; }
        m_e.push_back({id, originalAlpha, originalAlpha, true});
    }
    /// Move every alpha toward its goal. `done` receives the entries that are back to normal.
    void Step(float lowAlpha, float rate, float dt, std::vector<Entry>& done) {
        for (std::size_t i = 0; i < m_e.size();) {
            Entry& e = m_e[i];
            const float goal = e.blocking ? std::min(e.original, lowAlpha) : e.original;
            e.alpha += (goal - e.alpha) * EaseFactor(rate, dt);
            e.blocking = false;     // must be marked again next frame to stay faded
            if (std::fabs(e.alpha - e.original) < 0.004f && goal == e.original) { e.alpha = e.original; done.push_back(e); m_e.erase(m_e.begin() + (long)i); }
            else ++i;
        }
    }
    const std::vector<Entry>& Entries() const { return m_e; }
    /// Forget an id (its object was destroyed).
    void Forget(const void* id) { for (std::size_t i = 0; i < m_e.size(); ++i) if (m_e[i].id == id) { m_e.erase(m_e.begin() + (long)i); return; } }
    void Clear() { m_e.clear(); }
private:
    std::vector<Entry> m_e;
};

} // namespace cam
} // namespace okay
