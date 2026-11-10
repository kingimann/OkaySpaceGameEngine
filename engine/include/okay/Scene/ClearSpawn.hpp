#pragma once
// Put a newly created object on open ground, and ease a body back out when it
// starts Play inside another collider or gets squeezed between two of them.
// This does not touch the physics step. It only moves the transform.
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <cmath>
#include <vector>

namespace okay {

struct SeparationMemory {
    Vec3 dir{0.0f, 0.0f, 0.0f};
    bool have = false;
};

namespace clearspawn_detail {

struct Extents {
    Vec3 centerOff{0.0f, 0.5f, 0.0f};   // collider center minus the transform position
    Vec3 half{0.5f, 0.5f, 0.5f};
    bool any = false;
};

inline Extents Measure(GameObject* go) {
    Extents e;
    if (!go) return e;
    Collider3D* col = nullptr;
    for (Collider3D* c : go->GetComponents<Collider3D>()) {
        if (c && !c->isTrigger) { col = c; break; }
    }
    if (!col) {
        // A character with no collider still needs a body-sized footprint so it
        // is not dropped inside the props already in the scene.
        e.half = {0.3f, 0.9f, 0.3f};
        e.centerOff = {0.0f, 0.9f, 0.0f};
        return e;
    }
    e.any = true;
    if (auto* box = dynamic_cast<BoxCollider3D*>(col)) e.half = box->HalfExtents();
    else if (auto* cap = dynamic_cast<CapsuleCollider3D*>(col)) {
        float r = cap->WorldRadius();
        float h = cap->height * 0.5f;
        e.half = {r, std::fmax(r, h), r};
    } else if (auto* sp = dynamic_cast<SphereCollider3D*>(col)) {
        float r = sp->WorldRadius();
        e.half = {r, r, r};
    } else {
        Vec3 mn, mx;
        col->WorldAABB(mn, mx);
        Vec3 mid = (mn + mx) * 0.5f;
        e.half = (mx - mn) * 0.5f;
        e.centerOff = mid - (go->transform ? go->transform->Position() : Vec3::Zero);
        if (e.half.x < 0.05f) e.half.x = 0.05f;
        if (e.half.y < 0.05f) e.half.y = 0.05f;
        if (e.half.z < 0.05f) e.half.z = 0.05f;
        return e;
    }
    e.centerOff = col->offset;
    if (e.half.x < 0.05f) e.half.x = 0.05f;
    if (e.half.y < 0.05f) e.half.y = 0.05f;
    if (e.half.z < 0.05f) e.half.z = 0.05f;
    return e;
}

inline bool IsGround(Collider3D* c, float feetY) {
    if (!c) return false;
    Vec3 mn, mx;
    c->WorldAABB(mn, mx);
    // A floor (or the ground plane) only meets the body from below.
    return mx.y <= feetY + 0.08f && (mx.x - mn.x) + (mx.z - mn.z) > 1.5f;
}

inline int SolidOverlaps(Scene& scene, GameObject* self, const Vec3& center, const Vec3& half,
                         const Quat& rot, float feetY, float padXZ = 0.0f) {
    Vec3 h = half;
    h.x += padXZ;
    h.z += padXZ;
    Physics3D::IgnoreHierarchy own(self);
    auto hits = scene.physics3D().OverlapBox(scene, center, h, rot,
                                              Physics3D::kAllLayers, /*queryTriggers=*/false, self);
    int n = 0;
    for (Collider3D* c : hits) {
        if (!c || c->isTrigger) continue;
        if (c->gameObject && self && c->gameObject->IsSelfOrDescendantOf(self)) continue;
        if (IsGround(c, feetY)) continue;
        ++n;
    }
    return n;
}

inline Vec3 TestHalf(const Vec3& half) {
    return {std::fmax(0.05f, half.x - 0.02f),
            std::fmax(0.05f, half.y - 0.04f),
            std::fmax(0.05f, half.z - 0.02f)};
}

inline float FloorY(Scene& scene, GameObject* self, float x, float z) {
    Physics3D::IgnoreHierarchy own(self);
    auto hits = scene.physics3D().RaycastAll(scene, {x, 80.0f, z}, {0.0f, -1.0f, 0.0f}, 160.0f,
                                              self, Physics3D::kAllLayers, false);
    float best = 1e30f;
    bool any = false;
    for (const RaycastHit3D& h : hits) {
        if (!h.hit || h.normal.y < 0.45f) continue;
        if (h.point.y < best) { best = h.point.y; any = true; }
    }
    return any ? best : 0.0f;
}

} // namespace clearspawn_detail

/// World position for `go`'s transform so its collider sits on the ground and
/// does not intersect the other solid colliders. Searches outward from `hint`
/// (the scene view focus, or the origin).
inline Vec3 ClearFeetPosition(Scene& scene, GameObject* go, const Vec3& hint) {
    using namespace clearspawn_detail;
    if (!go || !go->transform) return hint;
    const Extents ext = Measure(go);
    const Quat rot = go->transform->Rotation();
    const float bottom = ext.centerOff.y - ext.half.y;
    const Vec3 half = TestHalf(ext.half);
    auto pose = [&](float x, float z) {
        const float gy = FloorY(scene, go, x, z);
        const Vec3 pos{x, gy - bottom, z};
        const Vec3 center = pos + ext.centerOff + Vec3{0.0f, 0.03f, 0.0f};
        // A little air around the body, so a slot between two props is not "clear".
        const int n = SolidOverlaps(scene, go, center, half, rot, gy, 0.9f);
        return std::pair<Vec3, int>{pos, n};
    };
    const float hx = hint.x, hz = hint.z;
    auto best = pose(hx, hz);
    if (best.second == 0) return best.first;
    const int kDirs = 12;
    for (int ring = 1; ring <= 24; ++ring) {
        const float r = 0.45f * (float)ring;
        for (int k = 0; k < kDirs; ++k) {
            const float a = (float)k / (float)kDirs * 6.2831853f + (float)ring * 0.27f;
            auto cand = pose(hx + std::sin(a) * r, hz + std::cos(a) * r);
            if (cand.second == 0) return cand.first;
            if (cand.second < best.second) best = cand;
        }
    }
    return best.first;
}

/// Slide `go` out of overlapping solids a short step this frame. Two walls
/// that face each other cancel the physics push, so the body picks a free
/// horizontal direction and eases that way. A single light graze is left to
/// the contact solver. `wish` (zero if the player is idle) breaks ties toward
/// the way they are trying to walk.
inline void EaseOutOfSolids(GameObject* go, float dt, SeparationMemory& mem, const Vec3& wish) {
    using namespace clearspawn_detail;
    if (!go || !go->transform || !go->scene() || dt <= 0.0f) return;
    Scene& scene = *go->scene();
    const Extents ext = Measure(go);
    if (!ext.any) return;
    const Quat rot = go->transform->Rotation();
    const Vec3 pos = go->transform->Position();
    const Vec3 half = TestHalf(ext.half);
    const Vec3 center = pos + ext.centerOff + Vec3{0.0f, 0.03f, 0.0f};
    const float feet = pos.y + ext.centerOff.y - ext.half.y;
    const int here = SolidOverlaps(scene, go, center, half, rot, feet);
    auto clearance = [&](const Vec3& dir, float halfAlong) {
        Physics3D::IgnoreHierarchy own(go);
        RaycastHit3D hit = scene.physics3D().Raycast(scene, center, dir, 4.0f, go,
                                                     Physics3D::kAllLayers, false);
        if (!hit.hit || hit.normal.y > 0.6f) return 4.0f;
        return hit.distance - halfAlong;
    };
    const float cpx = clearance({1, 0, 0}, ext.half.x);
    const float cmx = clearance({-1, 0, 0}, ext.half.x);
    const float cpz = clearance({0, 0, 1}, ext.half.z);
    const float cmz = clearance({0, 0, -1}, ext.half.z);
    // A narrow slot: one side is touching and the other is barely clear.
    // A wide corridor (or standing against one wall) is not a slot.
    auto narrow = [](float a, float b) {
        const float lo = a < b ? a : b;
        const float hi = a > b ? a : b;
        return lo < 0.16f && hi < 0.40f;
    };
    const bool slotX = narrow(cpx, cmx) && (cpz > 0.5f || cmz > 0.5f);
    const bool slotZ = narrow(cpz, cmz) && (cpx > 0.5f || cmx > 0.5f);
    const int crowded = SolidOverlaps(scene, go, center, half, rot, feet, 0.35f);
    const Vec3 wishFlat{wish.x, 0.0f, wish.z};
    // Clear, and not finishing a slide that already found a way out: stop.
    // The slide only continues along a direction that reduced the overlap.
    // It does not start a new shove through one wall.
    if (here == 0 && crowded == 0) { mem.have = false; mem.dir = Vec3::Zero; return; }
    if (here == 0 && (slotX || slotZ || (mem.have && crowded > 0))) {
        Vec3 out = mem.have ? mem.dir
                 : slotX ? Vec3{0, 0, cpz >= cmz ? 1.0f : -1.0f}
                         : Vec3{cpx >= cmx ? 1.0f : -1.0f, 0, 0};
        if (out.SqrMagnitude() < 1e-6f) out = {0, 0, 1};
        if (wishFlat.SqrMagnitude() > 0.04f) {
            Vec3 w = wishFlat.Normalized();
            if (std::fabs(Vec3::Dot(w, out)) > 0.2f) out = Vec3::Dot(w, out) >= 0 ? out : out * -1.0f;
            else if (slotX && std::fabs(w.z) > std::fabs(w.x)) out = {0, 0, w.z >= 0 ? 1.0f : -1.0f};
            else if (slotZ && std::fabs(w.x) > std::fabs(w.z)) out = {w.x >= 0 ? 1.0f : -1.0f, 0, 0};
        }
        float step = 1.6f * dt;
        if (step < 0.004f) step = 0.004f;
        if (step > 0.05f) step = 0.05f;
        go->transform->SetPosition(pos + out * step);
        mem.dir = out;
        mem.have = true;
        return;
    }

    Vec3 dirs[10];
    int nd = 0;
    if (mem.have && mem.dir.SqrMagnitude() > 1e-6f) dirs[nd++] = mem.dir.Normalized();
    Vec3 flat{wish.x, 0.0f, wish.z};
    if (flat.SqrMagnitude() > 1e-6f) dirs[nd++] = flat.Normalized();
    const Vec3 compass[8] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1},
        {0.7071f, 0, 0.7071f}, {0.7071f, 0, -0.7071f},
        {-0.7071f, 0, 0.7071f}, {-0.7071f, 0, -0.7071f}
    };
    for (const Vec3& d : compass) dirs[nd++] = d;

    const float probe = 0.45f;
    int bestN = here;
    float bestDot = -2.0f;
    Vec3 best = Vec3::Zero;
    bool found = false;
    for (int i = 0; i < nd; ++i) {
        const Vec3 c = center + dirs[i] * probe;
        const int n = SolidOverlaps(scene, go, c, half, rot, feet);
        const float d = Vec3::Dot(dirs[i], flat.SqrMagnitude() > 1e-6f ? flat.Normalized() : dirs[i]);
        const bool better = !found || n < bestN || (n == bestN && d > bestDot + 1e-4f);
        if (n < here && better) { bestN = n; bestDot = d; best = dirs[i]; found = true; }
    }
    // No horizontal step drops the overlap. A wish that stays inside the solid
    // is a normal contact (walking into one wall). Shoving along it drives the
    // body through. Only take a step that actually reduces the overlap: along
    // the wish when that helps, otherwise straight up out of a floor.
    if (!found) {
        const Vec3 up{0.0f, 1.0f, 0.0f};
        const Vec3 wishDir = flat.SqrMagnitude() > 1e-6f ? flat.Normalized() : up;
        const int nWish = SolidOverlaps(scene, go, center + wishDir * probe, half, rot, feet);
        const int nUp = SolidOverlaps(scene, go, center + up * probe, half, rot, feet + probe);
        if (nWish < here) best = wishDir;
        else if (nUp < here) best = up;
        else return;
        found = true;
    }
    float step = 1.6f * dt;
    if (step < 0.004f) step = 0.004f;
    if (step > 0.05f) step = 0.05f;
    const Vec3 next = pos + best * step;
    const Vec3 nc = next + ext.centerOff + Vec3{0.0f, 0.03f, 0.0f};
    const int after = SolidOverlaps(scene, go, nc, half, rot, feet + best.y * step);
    if (after > here) return;
    go->transform->SetPosition(next);
    mem.dir = best;
    mem.have = true;
}

} // namespace okay
