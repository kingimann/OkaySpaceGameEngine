#pragma once
// Fitting a person into a vehicle seat: pure maths (no scene, no physics), tested headless
// (tests/test_vehicle_seat_fit.cpp) and used by VehicleSeat.
//
// Written from scratch from plain geometry. A seat is an anchor in the vehicle's local space (floor point under the seat,
// turn about the up axis, cushion height, roof height). A person is described only by their standing height (from their
// collider or mesh bounds). Sitting puts the hips on the cushion; the head then needs a little room under the roof, and
// a person who is too tall is scaled down (down to a floor, never up) so they do not poke through. Getting out looks for a
// free standing spot beside the door. All yaw rotations are about +Y, in degrees, + = turning from +Z towards +X.
// Header only, free of <mutex>/<thread> (rule 21a).
#include <algorithm>
#include <cmath>
#include <vector>

namespace okay {
namespace seatfit {

struct V3 { float x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Len(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

constexpr float kDeg = 57.2957795f;
/// Standing hip joint height as a share of body height, and the same joint once seated (the sit clip puts the hip 0.57 m
/// above the feet root on a 1.8 m person, so 0.57 / 1.8). Both are properties of the pose, not of a vehicle.
constexpr float kHipStanding = 0.52f;
constexpr float kHipSeated = 0.57f / 1.8f;

/// Turn a local vector about +Y by `yawDeg`.
inline V3 RotY(V3 v, float yawDeg) {
    const float r = yawDeg / kDeg, c = std::cos(r), s = std::sin(r);
    return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

/// A seat's place in the vehicle.
struct SeatAnchor {
    V3 floorPoint{0.0f, 0.6f, 0.0f};   ///< local point on the floor under the seat (where the feet root normally goes)
    float yawDeg = 180.0f;             ///< which way the sitter faces, as a turn from the vehicle's +Z (180 = forward in a -Z-facing body)
    float cushionHeight = 0.45f;       ///< seat surface above floorPoint
    float roofHeight = 0.0f;           ///< roof (or door-frame top) above floorPoint; 0 or less = open top, no limit
    float headMargin = 0.05f;          ///< room to leave between the head and the roof
    bool  scaleToFit = true;           ///< scale a tall person down to fit under the roof
    float minScale = 0.8f;             ///< never scale below this (a bigger person then simply pokes up)
    bool  driver = true;
};

/// What a seated person needs: where the feet root goes (local), the scale to apply, the yaw, and whether the head clears.
struct SeatedFit { V3 feetLocal; float scale = 1.0f; float yawDeg = 0.0f; float headTop = 0.0f; bool headClears = true; };

/// Fit a person of standing height `height` (metres, >0) at the anchor. Seat surface and hips meet; the head top is
/// (1 - kHipStanding) * height * scale above the cushion. NaN or non-positive heights are treated as 1.8.
inline SeatedFit FitToSeat(const SeatAnchor& a, float height) {
    if (!(height > 0.01f) || !std::isfinite(height)) height = 1.8f;
    SeatedFit f;
    const float headAboveSeat = (1.0f - kHipStanding) * height;       // at scale 1
    float s = 1.0f;
    if (a.roofHeight > 0.0f && a.scaleToFit) {
        const float room = a.roofHeight - a.headMargin - a.cushionHeight;
        if (room > 0.0f && headAboveSeat > room) s = room / headAboveSeat;
        s = std::max(std::min(1.0f, a.minScale), std::min(1.0f, s));
    }
    f.scale = s;
    // Hips on the cushion: feet root is the seated hip height below the cushion surface (hip scales with the person).
    f.feetLocal = {a.floorPoint.x, a.floorPoint.y + a.cushionHeight - kHipSeated * height * s, a.floorPoint.z};
    f.yawDeg = a.yawDeg;
    f.headTop = a.cushionHeight + headAboveSeat * s;                // above floorPoint
    f.headClears = !(a.roofHeight > 0.0f) || f.headTop <= a.roofHeight - a.headMargin + 1e-4f;
    return f;
}

/// Anchor local point -> world, for a vehicle at `pos` turned `vehicleYawDeg`.
inline V3 ToWorld(const V3& vehiclePos, float vehicleYawDeg, const V3& local) { return vehiclePos + RotY(local, vehicleYawDeg); }
inline float WorldYaw(float vehicleYawDeg, float anchorYawDeg) {
    float y = std::fmod(vehicleYawDeg + anchorYawDeg, 360.0f);
    if (y > 180.0f) y -= 360.0f; else if (y <= -180.0f) y += 360.0f;
    return y;
}

/// The smooth walk from where the player stood to the seat. Smoothstep position, duration grows with the distance
/// (`speed` m/s) between `minTime` and `maxTime`.
struct EnterBlend {
    V3 from, to; float t = 0.0f, duration = 0.0f; bool active = false;
    void Begin(V3 start, V3 seat, float speed = 3.0f, float minTime = 0.25f, float maxTime = 0.8f) {
        from = start; to = seat; t = 0.0f;
        duration = std::min(maxTime, std::max(minTime, Len(seat - start) / std::max(0.1f, speed)));
        active = true;
    }
    /// Advance and return the position; once finished it stays at `to` and `active` goes false.
    V3 Step(float dt) {
        if (!active) return to;
        t += std::max(0.0f, dt);
        float k = std::min(1.0f, t / duration);
        if (k >= 1.0f) active = false;
        const float e = k * k * (3.0f - 2.0f * k);
        return from + (to - from) * e;
    }
};

/// Standing spots to try when getting out, in order, in vehicle-local space (the vehicle faces +Z, its left is -X):
/// beside the door on the preferred side at growing distances and a few steps along the car, then the other side, then
/// behind, then in front. Feet root points on the floor height `floorY`.
inline std::vector<V3> ExitCandidates(float halfWidth, float halfLength, float doorZ, bool preferLeft, float personRadius, float floorY) {
    std::vector<V3> out;
    const float clear = 0.1f;
    const float side[2] = {preferLeft ? -1.0f : 1.0f, preferLeft ? 1.0f : -1.0f};
    const float alongStep[3] = {0.0f, 0.8f, -0.8f};
    for (float sd : side)
        for (int k = 0; k < 4; ++k)
            for (float al : alongStep)
                out.push_back({sd * (halfWidth + personRadius + clear + 0.4f * (float)k), floorY, doorZ + al});
    out.push_back({0.0f, floorY, -(halfLength + personRadius + clear)});
    out.push_back({0.0f, floorY, halfLength + personRadius + clear});
    return out;
}
/// Index of the first candidate that `blocked(candidate)` says is free, or -1 when all are blocked (the caller then lifts the
/// person above the roof instead of leaving them inside geometry).
template <class F> inline int FirstFree(const std::vector<V3>& c, F blocked) {
    for (size_t i = 0; i < c.size(); ++i) if (!blocked(c[i])) return (int)i;
    return -1;
}

/// A part hidden while a person sits (for example a roof panel the head would poke through): true when the part's centre
/// (vehicle-local) is inside the clip box.
struct ClipBox { V3 centre, half; };
inline bool InsideClip(const ClipBox& b, const V3& p) {
    return std::fabs(p.x - b.centre.x) <= b.half.x + 1e-5f && std::fabs(p.y - b.centre.y) <= b.half.y + 1e-5f && std::fabs(p.z - b.centre.z) <= b.half.z + 1e-5f;
}

/// Camera near plane for a cockpit view: close to the body surface it must shrink so the dashboard is not cut away.
/// `distToSurface` = distance from the eye to the nearest car surface (metres).
inline float CockpitNearPlane(float distToSurface) {
    if (!(distToSurface == distToSurface)) return 0.1f;
    return std::min(0.1f, std::max(0.02f, 0.8f * distToSurface));
}
/// Keeps a first-person eye point (vehicle-local) inside the cabin box [lo, hi] by `margin`, so it never rests in the body or
/// outside the car.
inline V3 KeepEyeInCabin(V3 eye, V3 lo, V3 hi, float margin) {
    auto cl = [&](float v, float a, float b) { return std::min(b - margin, std::max(a + margin, v)); };
    return {cl(eye.x, lo.x, hi.x), cl(eye.y, lo.y, hi.y), cl(eye.z, lo.z, hi.z)};
}

} // namespace seatfit
} // namespace okay
