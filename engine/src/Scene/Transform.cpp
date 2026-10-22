#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace okay {

namespace {
std::uint64_t g_worldStamp = 0;   // every recompute gets a new stamp (children compare it)
}

// World matrices are cached per transform. The local fields are public and get
// written directly all over the engine, so instead of dirty flags the cache keeps
// a copy of the inputs it was built from: same local TRS, same parent, and the
// parent's own cache unchanged (its stamp) -> the cached matrix is still right.
// Checking is a few compares per level; rebuilding was a TRS + matrix multiply
// per level, for every Position()/Rotation()/LocalToWorldMatrix() call.
const Mat4& Transform::WorldCache() const {
    std::uint64_t ps = 0;
    const Mat4* pw = nullptr;
    if (m_parent) { pw = &m_parent->WorldCache(); ps = m_parent->m_wStamp; }
    if (m_wValid && m_wParent == m_parent && m_wParentStamp == ps &&
        std::memcmp(&m_wP, &localPosition, sizeof(Vec3)) == 0 &&
        std::memcmp(&m_wR, &localRotation, sizeof(Quat)) == 0 &&
        std::memcmp(&m_wS, &localScale, sizeof(Vec3)) == 0)
        return m_world;
    Mat4 local = Mat4::TRS(localPosition, localRotation, localScale);
    m_world = pw ? (*pw) * local : local;
    m_wRot = m_parent ? (m_parent->m_wRot * localRotation).Normalized() : localRotation;
    m_wP = localPosition; m_wR = localRotation; m_wS = localScale;
    m_wParent = m_parent; m_wParentStamp = ps;
    m_wStamp = ++g_worldStamp;
    m_wValid = true;
    return m_world;
}

Mat4 Transform::LocalToWorldMatrix() const {
    return WorldCache();
}

Vec3 Transform::Position() const {
    if (!m_parent) return localPosition;
    return m_parent->WorldCache().MultiplyPoint(localPosition);
}

Quat Transform::Rotation() const {
    if (!m_parent) return localRotation;
    WorldCache();
    return m_wRot;
}

Vec3 Transform::LossyScale() const {
    if (!m_parent) return localScale;
    Vec3 p = m_parent->LossyScale();
    return {p.x * localScale.x, p.y * localScale.y, p.z * localScale.z};
}

void Transform::SetPosition(const Vec3& worldPos) {
    if (!m_parent) { localPosition = worldPos; return; }
    // Convert world position into the parent's local space.
    Vec3 parentPos   = m_parent->Position();
    Quat invRotation = m_parent->Rotation().Conjugate();
    Vec3 parentScale = m_parent->LossyScale();
    Vec3 rel = invRotation * (worldPos - parentPos);
    localPosition = {
        parentScale.x != 0 ? rel.x / parentScale.x : rel.x,
        parentScale.y != 0 ? rel.y / parentScale.y : rel.y,
        parentScale.z != 0 ? rel.z / parentScale.z : rel.z};
}

void Transform::SetRotation(const Quat& worldRot) {
    if (!m_parent) { localRotation = worldRot.Normalized(); return; }
    // Convert world rotation into the parent's local space.
    localRotation = (m_parent->Rotation().Conjugate() * worldRot).Normalized();
}

void Transform::SetParent(Transform* parent, bool worldPositionStays) {
    if (parent == m_parent) return;
    Vec3 world = Position();
    if (m_parent) {
        auto& sib = m_parent->m_children;
        sib.erase(std::remove(sib.begin(), sib.end(), this), sib.end());
    }
    m_parent = parent;
    if (m_parent) m_parent->m_children.push_back(this);
    // Preserve the world position only when asked (Unity's worldPositionStays).
    if (worldPositionStays) SetPosition(world);
}

} // namespace okay
