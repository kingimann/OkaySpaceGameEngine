#include "okay/Components/Decal.hpp"
#include "okay/Components/Foliage.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace okay {

namespace {

struct V { Vec3 p; };

// Clip a convex polygon against the half-space dot(n, p) <= d (Sutherland-Hodgman).
void ClipPlane(std::vector<Vec3>& poly, const Vec3& n, float d) {
    std::vector<Vec3> out;
    const std::size_t c = poly.size();
    for (std::size_t i = 0; i < c; ++i) {
        const Vec3& a = poly[i];
        const Vec3& b = poly[(i + 1) % c];
        const float da = Vec3::Dot(n, a) - d, db = Vec3::Dot(n, b) - d;
        if (da <= 0.0f) out.push_back(a);
        if ((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f)) {
            const float t = da / (da - db);
            out.push_back(a + (b - a) * t);
        }
    }
    poly.swap(out);
}

} // namespace

int Decal::Project() {
    projected_ = true;
    triangles_ = 0;
    if (!gameObject || !gameObject->scene() || !transform) return 0;
    Scene* sc = gameObject->scene();
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    const Mat4 W = transform->LocalToWorldMatrix();
    const Mat4 Wi = W.Inverse();
    const Vec3 half{std::max(0.001f, size.x) * 0.5f, std::max(0.001f, size.y) * 0.5f, std::max(0.001f, size.z) * 0.5f};
    // The box is centred half-way along +Z so the decal reaches forward from the object.
    const float zc = half.z;
    const float cosMax = std::cos(std::min(89.0f, std::max(0.0f, maxAngle)) * 0.0174532925f);
    Mesh out;
    out.name = "Decal";
    for (const auto& up : sc->Objects()) {
        GameObject* g = up.get();
        if (!g || g == gameObject || !g->active || !g->transform) continue;
        auto* smr = g->GetComponent<MeshRenderer>();
        if (!smr || !smr->enabled || g->GetComponent<Decal>()) continue;
        if (const Transform* par = g->transform->Parent(); par && par->gameObject && par->gameObject->GetComponent<Foliage>()) continue;
        const Mesh& m = smr->DisplayMesh();
        if (m.triangles.size() < 3) continue;
        const Mat4 M = Wi * g->transform->LocalToWorldMatrix();     // that mesh -> decal space
        Vec3 lo, hi;
        m.Bounds(lo, hi);
        Vec3 bmin{1e30f, 1e30f, 1e30f}, bmax{-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < 8; ++i) {
            const Vec3 p = M.MultiplyPoint({(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z});
            bmin = {std::fmin(bmin.x, p.x), std::fmin(bmin.y, p.y), std::fmin(bmin.z, p.z)};
            bmax = {std::fmax(bmax.x, p.x), std::fmax(bmax.y, p.y), std::fmax(bmax.z, p.z)};
        }
        if (bmax.x < -half.x || bmin.x > half.x || bmax.y < -half.y || bmin.y > half.y || bmax.z < 0 || bmin.z > 2 * half.z) continue;
        const std::size_t tn = m.triangles.size() / 3;
        for (std::size_t t = 0; t < tn; ++t) {
            const int ia = m.triangles[t * 3], ib = m.triangles[t * 3 + 1], ic = m.triangles[t * 3 + 2];
            if (ia < 0 || ib < 0 || ic < 0 || ia >= (int)m.vertices.size() || ib >= (int)m.vertices.size() || ic >= (int)m.vertices.size()) continue;
            std::vector<Vec3> poly{M.MultiplyPoint(m.vertices[ia]), M.MultiplyPoint(m.vertices[ib]), M.MultiplyPoint(m.vertices[ic])};
            Vec3 n = Vec3::Cross(poly[1] - poly[0], poly[2] - poly[0]);
            if (n.Magnitude() < 1e-10f) continue;
            n = n.Normalized();
            // Only surfaces that face the projector (it looks along +Z, so they face -Z).
            const float facing = -n.z;
            if (facing < cosMax) continue;
            Vec3 c{poly[0].x, poly[0].y, poly[0].z};
            c = (poly[0] + poly[1] + poly[2]) * (1.0f / 3.0f);
            ClipPlane(poly, {1, 0, 0}, half.x);  ClipPlane(poly, {-1, 0, 0}, half.x);
            ClipPlane(poly, {0, 1, 0}, half.y);  ClipPlane(poly, {0, -1, 0}, half.y);
            ClipPlane(poly, {0, 0, 1}, 2 * half.z); ClipPlane(poly, {0, 0, -1}, 0.0f);
            if (poly.size() < 3) continue;
            (void)c; (void)zc;
            const int base = (int)out.vertices.size();
            for (const Vec3& p : poly) {
                out.vertices.push_back(p + n * lift);
                out.uvs.push_back({p.x / (2.0f * half.x) + 0.5f, p.y / (2.0f * half.y) + 0.5f});
                out.normals.push_back(n);
            }
            for (std::size_t k = 1; k + 1 < poly.size(); ++k) {
                out.triangles.push_back(base); out.triangles.push_back(base + (int)k); out.triangles.push_back(base + (int)k + 1);
            }
        }
    }
    triangles_ = out.TriangleCount();
    mr->mesh = std::move(out);
    mr->texture = texture;
    mr->color = tint;
    mr->doubleSided = true;
    mr->castShadows = false;
    return triangles_;
}

} // namespace okay
