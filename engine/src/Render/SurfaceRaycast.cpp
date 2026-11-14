#include "okay/Render/SurfaceRaycast.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Lightmap.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>
#include <limits>

namespace okay {

namespace {

// Does the ray hit the box (slab test)?
bool HitsBox(const Vec3& o, const Vec3& d, const Vec3& lo, const Vec3& hi, float maxD) {
    float t0 = 0.0f, t1 = maxD;
    const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    const float l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(dd[a]) < 1e-9f) { if (oo[a] < l[a] || oo[a] > h[a]) return false; continue; }
        float ta = (l[a] - oo[a]) / dd[a], tb = (h[a] - oo[a]) / dd[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::fmax(t0, ta); t1 = std::fmin(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

} // namespace

bool RaycastSurfaces(Scene& scene, const Vec3& origin, const Vec3& dir, float maxDistance, SurfaceHit& hit,
                     const std::function<bool(const GameObject&)>& skip) {
    float best = maxDistance;
    bool found = false;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || !go->transform) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !mr->enabled || mr->mesh.vertices.empty() || mr->mesh.triangles.size() < 3) continue;
        if (skip && skip(*go)) continue;
        const Mesh& mesh = mr->DisplayMesh();
        const Mat4 M = go->transform->LocalToWorldMatrix();
        Vec3 lo, hi;
        mesh.Bounds(lo, hi);
        Vec3 wlo{1e30f, 1e30f, 1e30f}, whi{-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < 8; ++i) {
            const Vec3 p = M.MultiplyPoint({(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z});
            wlo = {std::fmin(wlo.x, p.x), std::fmin(wlo.y, p.y), std::fmin(wlo.z, p.z)};
            whi = {std::fmax(whi.x, p.x), std::fmax(whi.y, p.y), std::fmax(whi.z, p.z)};
        }
        const Vec3 pad{0.01f, 0.01f, 0.01f};
        if (!HitsBox(origin, dir, wlo - pad, whi + pad, best)) continue;
        // Work in the mesh's local space: one ray transform instead of transforming every vertex.
        const Mat4 Mi = M.Inverse();
        const Vec3 lo_ = Mi.MultiplyPoint(origin);
        Vec3 ld = Mi.MultiplyVector(dir);
        const float scaleLen = ld.Magnitude();
        if (scaleLen < 1e-9f) continue;
        ld = ld * (1.0f / scaleLen);
        const std::size_t tn = mesh.triangles.size() / 3;
        for (std::size_t t = 0; t < tn; ++t) {
            const int a = mesh.triangles[t * 3], b = mesh.triangles[t * 3 + 1], c = mesh.triangles[t * 3 + 2];
            if (a < 0 || b < 0 || c < 0 || a >= (int)mesh.vertices.size() || b >= (int)mesh.vertices.size() || c >= (int)mesh.vertices.size()) continue;
            float tl;
            if (!RayTriangle(lo_, ld, mesh.vertices[a], mesh.vertices[b], mesh.vertices[c], std::numeric_limits<float>::max(), tl)) continue;
            const Vec3 lp = lo_ + ld * tl;
            const Vec3 wp = M.MultiplyPoint(lp);
            const float wd = Vec3::Dot(wp - origin, dir);
            if (wd <= 1e-4f || wd >= best) continue;
            Vec3 n = Vec3::Cross(mesh.vertices[b] - mesh.vertices[a], mesh.vertices[c] - mesh.vertices[a]);
            n = M.MultiplyVector(n);   // fine for the uniform scales scenes use; renormalised below
            if (n.Magnitude() < 1e-12f) continue;
            n = n.Normalized();
            if (Vec3::Dot(n, dir) > 0.0f) n = n * -1.0f;
            best = wd; found = true;
            hit.position = wp; hit.normal = n; hit.distance = wd; hit.object = go;
        }
    }
    return found;
}

} // namespace okay
