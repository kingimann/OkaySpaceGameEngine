#pragma once
// Where does the dig ray hit the voxel terrain? One function that works the same in first and third person.
// Written from scratch from the plain idea of a fixed-step ray march against the density field (no outside code).
//
//   First person: the camera sits at the character's eyes, so the ray is simply the camera's view line.
//   Third person: the camera is behind and above the character. The ray still goes through the screen centre (the
//   crosshair), but it starts where it passes the character (not at the lens, so ground between the lens and the
//   character is ignored) and the reach is measured from the character's eyes, not from the camera.
//   Both: a hit inside the character's own body, or one the character cannot see because ground is in the way
//   (aiming over a wall), is refused.
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

struct VoxelAimInput {
    Vec3  camPos{0, 0, 0};
    Vec3  camDir{0, 0, -1};          ///< where the camera looks (need not be unit length)
    float range = 80.0f;             ///< furthest the ray goes from where it starts
    bool  hasBody = false;           ///< a character to measure from (false: the camera is the origin, as before)
    Vec3  feet{0, 0, 0};             ///< character feet (world)
    float bodyHeight = 1.8f;
    float bodyRadius = 0.45f;
    float reach = 12.0f;             ///< furthest from the character's eyes a block can be dug or placed (world units)
    bool  place = false;             ///< true: return the last air point before the surface (where a block would go)
    float eyeFraction = 0.9f;        ///< eye height as a fraction of bodyHeight
    float thirdPersonMin = 0.8f;     ///< camera further than this from the eyes counts as third person
};

enum class VoxelAimMiss { None, NoTerrain, NoHit, TooFar, InsideBody, Blocked, HeadInGround };

struct VoxelAimHit {
    bool  hit = false;
    VoxelAimMiss miss = VoxelAimMiss::None;
    bool  thirdPerson = false;
    Vec3  local{0, 0, 0};            ///< the point in the terrain's own space (the surface point, or the air point when placing)
    Vec3  surfaceLocal{0, 0, 0};     ///< the first solid point along the ray
    Vec3  world{0, 0, 0};
    Vec3  normal{0, 1, 0};           ///< world-space face direction (points out of the ground)
    float fromBody = 0.0f;           ///< distance from the character's eyes (from the camera when there is no body)
    float fromCamera = 0.0f;
};

inline bool VoxelAimInsideBody(const VoxelAimInput& in, const Vec3& p) {
    if (!in.hasBody) return false;
    const float dx = p.x - in.feet.x, dz = p.z - in.feet.z;
    if (dx * dx + dz * dz > in.bodyRadius * in.bodyRadius) return false;
    return p.y > in.feet.y + 0.1f && p.y < in.feet.y + in.bodyHeight;
}

/// March the ray. `vt` is the terrain's transform (it may be moved, turned or scaled).
inline VoxelAimHit VoxelAimFind(const VoxelTerrain& vox, const Transform& vt, const VoxelAimInput& in) {
    VoxelAimHit r;
    Vec3 dir = in.camDir;
    const float dm = dir.Magnitude();
    if (dm < 1e-6f) { r.miss = VoxelAimMiss::NoHit; return r; }
    dir = dir * (1.0f / dm);

    const Vec3 eye = in.hasBody ? Vec3{in.feet.x, in.feet.y + in.bodyHeight * in.eyeFraction, in.feet.z} : in.camPos;
    r.thirdPerson = in.hasBody && Vec3::Distance(in.camPos, eye) > in.thirdPersonMin;

    // Where the ray starts. Third person: where the view line passes closest to the character, so ground between the lens
    // and the character cannot be hit. Never behind the lens.
    float t0 = 0.0f;
    if (r.thirdPerson) t0 = std::max(0.0f, Vec3::Dot(eye - in.camPos, dir));
    float t1 = t0 + in.range;
    // The reach is from the character: the ray can never usefully go further than the reach plus the camera's distance.
    if (in.hasBody) t1 = std::min(t1, t0 + in.reach + Vec3::Distance(in.camPos + dir * t0, eye));

    float step = vox.voxelSize * 0.4f; if (step < 0.05f) step = 0.05f;

    // A head pushed into the ground (a tight tunnel, a wall in the face) digs nothing.
    if (in.hasBody && vox.SampleDensity(vt.InverseTransformPoint(eye)) > vox.iso) { r.miss = VoxelAimMiss::HeadInGround; return r; }

    Vec3 prevLocal = vt.InverseTransformPoint(in.camPos + dir * t0);
    bool havePrev = false;
    bool sawBody = false;
    for (float t = t0; t <= t1; t += step) {
        const Vec3 p = in.camPos + dir * t;
        const Vec3 local = vt.InverseTransformPoint(p);
        if (VoxelAimInsideBody(in, p)) { sawBody = true; prevLocal = local; havePrev = true; continue; }
        if (vox.SampleDensity(local) > vox.iso) {
            r.surfaceLocal = local;
            r.local = (in.place && havePrev) ? prevLocal : local;
            r.world = vt.TransformPoint(r.local);
            r.fromCamera = t;
            r.fromBody = Vec3::Distance(vt.TransformPoint(local), eye);
            if (in.hasBody && r.fromBody > in.reach) { r.miss = VoxelAimMiss::TooFar; return r; }
            if (in.hasBody && VoxelAimInsideBody(in, r.world)) { r.miss = VoxelAimMiss::InsideBody; return r; }
            // Can the character see it? March from the eyes to the point; ground in the way means it is behind a wall.
            if (in.hasBody) {
                const Vec3 hitW = vt.TransformPoint(local);
                const Vec3 to = hitW - eye;
                const float len = to.Magnitude();
                const float margin = std::max(step * 2.5f, vox.voxelSize * 1.2f);
                if (len > margin) {
                    const Vec3 d = to * (1.0f / len);
                    for (float u = step; u < len - margin; u += step)
                        if (vox.SampleDensity(vt.InverseTransformPoint(eye + d * u)) > vox.iso) { r.miss = VoxelAimMiss::Blocked; return r; }
                }
            }
            Vec3 n = vt.TransformDirection(vox.SurfaceNormal(local));
            const float nm = n.Magnitude();
            r.normal = nm > 1e-6f ? n * (1.0f / nm) : Vec3{0, 1, 0};
            r.hit = true;
            return r;
        }
        prevLocal = local; havePrev = true;
    }
    r.miss = sawBody ? VoxelAimMiss::InsideBody : VoxelAimMiss::NoHit;
    return r;
}

} // namespace okay
