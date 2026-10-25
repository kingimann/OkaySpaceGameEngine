#pragma once
// ---------------------------------------------------------------------------
// Frustum culling shared by the renderers: skip whatever the camera (or the
// shadow-casting light) can't see before any draw call, buffer bind or vertex
// work. Each object is tested as a world-space bounding sphere:
//   - static meshes: the mesh's local bounds (cached per mesh, recomputed only
//     when its vertex array changes) through the object's world matrix;
//   - skinned meshes: around their bones' current positions (the mesh moves
//     with the skeleton, so its bind-pose bounds would pop when animated).
// ---------------------------------------------------------------------------
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace okay {

/// Turn frustum culling off (debugging a missing object); on by default.
inline bool& FrustumCullingEnabled() { static bool on = true; return on; }

/// Last main-pass counts from whichever renderer drew most recently (Stats panel).
struct CullCounters { int drawn = 0, culled = 0; };
inline CullCounters& LastCullCounters() { static CullCounters c; return c; }

/// Six planes from a view-projection matrix (column-major, clip = vp * p).
/// A point is inside when dot(plane.xyz, p) + plane.w >= 0 for every plane.
struct Frustum {
    float pl[6][4];
    static Frustum FromVP(const Mat4& m) {
        Frustum f;
        auto row = [&](int r, float o[4]) { o[0] = m.m[r]; o[1] = m.m[4 + r]; o[2] = m.m[8 + r]; o[3] = m.m[12 + r]; };
        float r0[4], r1[4], r2[4], r3[4];
        row(0, r0); row(1, r1); row(2, r2); row(3, r3);
        for (int k = 0; k < 4; ++k) {
            f.pl[0][k] = r3[k] + r0[k];   // left
            f.pl[1][k] = r3[k] - r0[k];   // right
            f.pl[2][k] = r3[k] + r1[k];   // bottom
            f.pl[3][k] = r3[k] - r1[k];   // top
            f.pl[4][k] = r3[k] + r2[k];   // near (-w..w clip; looser than 0..w, so safe either way)
            f.pl[5][k] = r3[k] - r2[k];   // far
        }
        for (auto& p : f.pl) {
            float l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (l > 1e-12f) for (float& x : p) x /= l;
        }
        return f;
    }
    bool SphereVisible(const Vec3& c, float r) const {
        for (const auto& p : pl)
            if (p[0] * c.x + p[1] * c.y + p[2] * c.z + p[3] < -r) return false;
        return true;
    }
};

namespace cull_detail {
struct LocalBounds {
    const void* data = nullptr; std::size_t count = 0;
    Vec3 probe[3];                 // a few vertices: catches edits made in place
    Vec3 center{0, 0, 0}; float radius = 0.0f;
};
inline std::unordered_map<const Mesh*, LocalBounds>& Cache() {
    static std::unordered_map<const Mesh*, LocalBounds> c;
    return c;
}
} // namespace cull_detail

/// Local-space bounding sphere of a mesh (box centre, farthest vertex), cached.
inline void MeshLocalSphere(const Mesh& mesh, Vec3& center, float& radius) {
    auto& cache = cull_detail::Cache();
    if (cache.size() > 8192) cache.clear();   // meshes come and go; rebuilding is cheap
    auto& e = cache[&mesh];
    const auto& V = mesh.vertices;
    const std::size_t n = V.size();
    auto probeAt = [&](int k) -> const Vec3& { return V[k == 0 ? 0 : (k == 1 ? n / 2 : n - 1)]; };
    bool same = e.data == (const void*)V.data() && e.count == n;
    if (same && n)
        for (int k = 0; k < 3 && same; ++k) same = std::memcmp(&e.probe[k], &probeAt(k), sizeof(Vec3)) == 0;
    if (!same) {
        e.data = V.data(); e.count = n;
        if (n) {
            Vec3 lo = V[0], hi = V[0];
            for (const Vec3& v : V) {
                lo.x = v.x < lo.x ? v.x : lo.x; lo.y = v.y < lo.y ? v.y : lo.y; lo.z = v.z < lo.z ? v.z : lo.z;
                hi.x = v.x > hi.x ? v.x : hi.x; hi.y = v.y > hi.y ? v.y : hi.y; hi.z = v.z > hi.z ? v.z : hi.z;
            }
            e.center = (lo + hi) * 0.5f;
            float r2 = 0.0f;
            for (const Vec3& v : V) { Vec3 d = v - e.center; float q = d.x * d.x + d.y * d.y + d.z * d.z; if (q > r2) r2 = q; }
            e.radius = std::sqrt(r2);
            for (int k = 0; k < 3; ++k) e.probe[k] = probeAt(k);
        } else { e.center = {0, 0, 0}; e.radius = 0.0f; }
    }
    center = e.center; radius = e.radius;
}

/// World-space bounding sphere of what a renderer draws for `go`. False when it
/// can't tell (then draw it).
inline bool ObjectWorldSphere(const GameObject* go, const MeshRenderer* mr, Vec3& c, float& r) {
    if (!go || !go->transform || !mr) return false;
    const Mat4 M = go->transform->LocalToWorldMatrix();
    auto maxScale = [&]() {
        auto len = [](float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); };
        float sx = len(M.m[0], M.m[1], M.m[2]), sy = len(M.m[4], M.m[5], M.m[6]), sz = len(M.m[8], M.m[9], M.m[10]);
        return sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz);
    };
    if (const auto* sm = go->GetComponent<SkinnedMesh>()) {
        if (sm->joints.empty()) return false;
        // Around the bones, padded by half the bind mesh's size (flesh, hair, cloth).
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        int n = 0;
        for (const Transform* j : sm->joints) {
            if (!j) continue;
            Vec3 p = j->Position();
            lo.x = p.x < lo.x ? p.x : lo.x; lo.y = p.y < lo.y ? p.y : lo.y; lo.z = p.z < lo.z ? p.z : lo.z;
            hi.x = p.x > hi.x ? p.x : hi.x; hi.y = p.y > hi.y ? p.y : hi.y; hi.z = p.z > hi.z ? p.z : hi.z;
            ++n;
        }
        if (n == 0) return false;
        Vec3 bc; float br;
        MeshLocalSphere(sm->bind.vertices.empty() ? mr->DisplayMesh() : sm->bind, bc, br);
        c = (lo + hi) * 0.5f;
        Vec3 h = (hi - lo) * 0.5f;
        r = std::sqrt(h.x * h.x + h.y * h.y + h.z * h.z) + br * maxScale() * 0.5f;
        return true;
    }
    Vec3 lc; float lr;
    MeshLocalSphere(mr->DisplayMesh(), lc, lr);
    c = M.MultiplyPoint(lc);
    r = lr * maxScale();
    return true;
}

/// True when `go` should be skipped for a camera / light frustum.
inline bool CulledByFrustum(const Frustum& f, const GameObject* go, const MeshRenderer* mr) {
    if (!FrustumCullingEnabled()) return false;
    Vec3 c; float r;
    if (!ObjectWorldSphere(go, mr, c, r)) return false;
    return !f.SphereVisible(c, r * 1.01f + 1e-3f);
}

} // namespace okay
