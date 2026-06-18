#include "okay/Scene/Transform.hpp"
#include <algorithm>

namespace okay {

Mat4 Transform::LocalToWorldMatrix() const {
    Mat4 local = Mat4::TRS(localPosition, localRotation, localScale);
    if (m_parent) return m_parent->LocalToWorldMatrix() * local;
    return local;
}

Vec3 Transform::Position() const {
    if (!m_parent) return localPosition;
    return m_parent->LocalToWorldMatrix().MultiplyPoint(localPosition);
}

Quat Transform::Rotation() const {
    if (!m_parent) return localRotation;
    return (m_parent->Rotation() * localRotation).Normalized();
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

void Transform::SetParent(Transform* parent) {
    if (parent == m_parent) return;
    // Preserve world position across the reparent.
    Vec3 world = Position();
    if (m_parent) {
        auto& sib = m_parent->m_children;
        sib.erase(std::remove(sib.begin(), sib.end(), this), sib.end());
    }
    m_parent = parent;
    if (m_parent) m_parent->m_children.push_back(this);
    SetPosition(world);
}

} // namespace okay
