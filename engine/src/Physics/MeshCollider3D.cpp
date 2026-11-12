#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"

#include <cstring>
#include <string>

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

bool MeshCollider3D::IsDynamicConflict() const {
    if (!gameObject) return false;
    auto* rb = gameObject->GetComponent<Rigidbody3D>();
    return rb && rb->enabled && rb->bodyType == Rigidbody3D::BodyType::Dynamic;
}

std::string MeshCollider3D::StatusMessage() const {
    if (!SourceMesh(this))
        return "No MeshRenderer on this object: acting as the Size bounding box. Add a MeshRenderer for exact triangle collision.";
    if (!HasTriangles())
        return "The mesh has no usable triangles: acting as the Size bounding box.";
    if (IsDynamicConflict())
        return "Static only: this object also has a dynamic Rigidbody3D, so this collider is ignored by physics contact. "
               "Use a Box, Sphere or Capsule collider on moving objects (or make the body Kinematic / Static). "
               "Raycasts and overlaps still hit the triangles.";
    return std::string();
}

} // namespace okay
