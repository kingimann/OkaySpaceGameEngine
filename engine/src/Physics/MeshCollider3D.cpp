#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace okay {

namespace {
std::uint64_t g_meshEpoch = 0;

std::uint64_t HashBytes(const void* data, std::size_t bytes, std::uint64_t h) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    std::size_t i = 0;
    for (; i + 8 <= bytes; i += 8) {
        std::uint64_t w; std::memcpy(&w, p + i, 8);
        h = (h ^ w) * 0x100000001b3ull; h ^= h >> 29;
    }
    for (; i < bytes; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

const Mesh* SourceMesh(const MeshCollider3D* c) {
    if (!c->gameObject) return nullptr;
    auto* mr = c->gameObject->GetComponent<MeshRenderer>();
    return mr ? &mr->DisplayMesh() : nullptr;
}

Vec3 HullNormal(const Mesh& src) {
    Vec3 n{0, 0, 0};
    const int nv = (int)src.vertices.size();
    for (std::size_t t = 0; t + 2 < src.triangles.size(); t += 3) {
        int i0 = src.triangles[t], i1 = src.triangles[t + 1], i2 = src.triangles[t + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= nv || i1 >= nv || i2 >= nv) continue;
        n = n + Vec3::Cross(src.vertices[i1] - src.vertices[i0], src.vertices[i2] - src.vertices[i0]);
    }
    if (n.SqrMagnitude() < 1e-12f) return {0, 1, 0};
    return n.Normalized();
}

// The convex hull of the vertices. A flat (or collinear) cloud has no volume, so
// it is thickened slightly along its normal: a dynamic body still has a shape.
Mesh BuildConvexHull(const Mesh& src) {
    Mesh h = Mesh::ConvexHull(src.vertices);
    if (h.vertices.size() >= 4 && h.triangles.size() >= 4) return h;
    if (src.vertices.size() < 3) return h;
    Vec3 n = HullNormal(src);
    Vec3 mn = src.vertices[0], mx = src.vertices[0];
    for (const Vec3& v : src.vertices) {
        mn = {std::min(mn.x, v.x), std::min(mn.y, v.y), std::min(mn.z, v.z)};
        mx = {std::max(mx.x, v.x), std::max(mx.y, v.y), std::max(mx.z, v.z)};
    }
    float ext = (mx - mn).Magnitude();
    float eps = std::max(ext * 0.02f, 1e-3f);
    Vec3 t = std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Vec3 side = Vec3::Cross(n, t);
    if (side.SqrMagnitude() < 1e-12f) side = {1, 0, 0};
    else side = side.Normalized();
    std::vector<Vec3> pts = src.vertices;
    pts.reserve(src.vertices.size() * 3);
    for (const Vec3& v : src.vertices) {
        pts.push_back(v + n * eps);
        pts.push_back(v + side * eps);
    }
    return Mesh::ConvexHull(pts);
}
} // namespace

void MeshCollider3D::AdvanceEpoch() { ++g_meshEpoch; }

void MeshCollider3D::Refresh() const {
    const Mesh* m = SourceMesh(this);
    if (!m) { if (m_valid && !m_tris.Empty()) m_tris.Clear(); m_valid = true; m_vp = m_ip = nullptr; m_vn = m_in = 0; return; }
    const void* vp = m->vertices.data(); const void* ip = m->triangles.data();
    bool stale = !m_valid || vp != m_vp || ip != m_ip || m->vertices.size() != m_vn || m->triangles.size() != m_in;
    if (!stale && m_checkedEpoch != g_meshEpoch) {
        std::uint64_t h = 0xcbf29ce484222325ull;
        h = HashBytes(m->vertices.data(), m->vertices.size() * sizeof(Vec3), h);
        h = HashBytes(m->triangles.data(), m->triangles.size() * sizeof(int), h);
        stale = h != m_hash;
        m_hash = h;
    }
    m_checkedEpoch = g_meshEpoch;
    if (!stale) return;
    m_hash = HashBytes(m->vertices.data(), m->vertices.size() * sizeof(Vec3), 0xcbf29ce484222325ull);
    m_hash = HashBytes(m->triangles.data(), m->triangles.size() * sizeof(int), m_hash);
    m_vp = vp; m_ip = ip; m_vn = m->vertices.size(); m_in = m->triangles.size();
    m_tris.Build(*m);
    m_valid = true;
    ++m_builds;
}

bool MeshCollider3D::HasTriangles() const { Refresh(); return !m_tris.Empty(); }
const TriMesh& MeshCollider3D::Tris() const { Refresh(); return m_tris; }

Mat4 MeshCollider3D::WorldMatrix() const {
    return transform ? transform->LocalToWorldMatrix() : Mat4::Identity();
}

void MeshCollider3D::WorldAABB(Vec3& mn, Vec3& mx) const {
    Refresh();
    if (!m_tris.Empty()) {
        trimesh::WorldView v(m_tris, WorldMatrix());
        if (v.Valid()) { v.WorldBounds(mn, mx); return; }
    }
    BoxCollider3D::WorldAABB(mn, mx);
}

bool MeshCollider3D::IsDynamicBody() const {
    if (!gameObject) return false;
    auto* rb = gameObject->GetComponent<Rigidbody3D>();
    return rb && rb->enabled && rb->bodyType == Rigidbody3D::BodyType::Dynamic;
}

void MeshCollider3D::EnsureHull() const {
    if (!IsDynamicBody()) {
        if (m_hullReady || !m_hull.vertices.empty()) {
            m_hull = Mesh{};
            m_hullTris.Clear();
            m_hullReady = false;
        }
        return;
    }
    Refresh();
    if (m_hullReady && m_hullHash == m_hash) return;
    const Mesh* src = SourceMesh(this);
    m_hull = src ? BuildConvexHull(*src) : Mesh{};
    m_hullTris.Build(m_hull);
    m_hullHash = m_hash;
    m_hullReady = true;
}

bool MeshCollider3D::UsesConvexHull() const {
    if (!IsDynamicBody()) return false;
    EnsureHull();
    return m_hull.vertices.size() >= 4 && m_hull.triangles.size() >= 4;
}

const Mesh& MeshCollider3D::Hull() const {
    EnsureHull();
    return m_hull;
}

const TriMesh& MeshCollider3D::HullTris() const {
    EnsureHull();
    return m_hullTris;
}

Vec3 MeshCollider3D::SupportWorld(const Vec3& dir) const {
    EnsureHull();
    if (m_hull.vertices.empty()) return WorldCenter();
    Mat4 m = WorldMatrix();
    Vec3 best = m.MultiplyPoint(m_hull.vertices[0]);
    float bd = Vec3::Dot(best, dir);
    for (std::size_t i = 1; i < m_hull.vertices.size(); ++i) {
        Vec3 w = m.MultiplyPoint(m_hull.vertices[i]);
        float d = Vec3::Dot(w, dir);
        if (d > bd) { bd = d; best = w; }
    }
    return best;
}

bool MeshCollider3D::IsDynamicConflict() const {
    // A dynamic body with a hull is supported. Conflict is only the leftover case:
    // real triangles, but no convex shape could be built from them.
    return IsDynamicBody() && HasTriangles() && !UsesConvexHull();
}

std::string MeshCollider3D::StatusMessage() const {
    if (!SourceMesh(this))
        return "No MeshRenderer on this object: acting as the Size bounding box. Add a MeshRenderer for exact triangle collision.";
    if (!HasTriangles())
        return "The mesh has no usable triangles: acting as the Size bounding box.";
    if (IsDynamicConflict())
        return "No convex hull: this object has a dynamic Rigidbody3D and the mesh could not be wrapped in a convex shape, "
               "so this collider is ignored by physics contact. Use a Box, Sphere or Capsule collider, or make the body Kinematic / Static. "
               "Raycasts and overlaps still hit the triangles.";
    return std::string();
}

} // namespace okay
