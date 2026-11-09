#include "okay/Scene/Transform.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
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

void Transform::SetLossyScale(const Vec3& worldScale) {
    const Vec3 ps = m_parent ? m_parent->LossyScale() : Vec3::One;
    if (ps.x != 0) localScale.x = worldScale.x / ps.x;
    if (ps.y != 0) localScale.y = worldScale.y / ps.y;
    if (ps.z != 0) localScale.z = worldScale.z / ps.z;
}

void Transform::LookAt(const Vec3& worldTarget, const Vec3& worldUp) {
    const Vec3 dir = worldTarget - Position();
    if (dir.SqrMagnitude() < 1e-12f) return;
    SetRotation(Quat::LookRotation(dir, worldUp));
}

int Transform::GetSiblingIndex() const {
    if (m_parent) {
        for (std::size_t i = 0; i < m_parent->m_children.size(); ++i)
            if (m_parent->m_children[i] == this) return static_cast<int>(i);
        return -1;
    }
    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    if (!sc) return -1;
    int n = 0;
    for (const auto& up : sc->Objects()) {
        if (up.get() == gameObject) return n;
        if (!up->transform || !up->transform->Parent()) ++n;
    }
    return -1;
}

void Transform::SetSiblingIndex(int index) {
    const int cur = GetSiblingIndex();
    if (cur < 0) return;
    if (m_parent) {
        auto& kids = m_parent->m_children;
        index = std::clamp(index, 0, static_cast<int>(kids.size()) - 1);
        if (index == cur) return;
        kids.erase(kids.begin() + cur);
        kids.insert(kids.begin() + index, this);
        NoteOrderChanged();
        return;
    }
    Scene* sc = gameObject->scene();
    std::vector<GameObject*> roots = sc->Roots();
    index = std::clamp(index, 0, static_cast<int>(roots.size()) - 1);
    if (index == cur) return;
    sc->ReorderSibling(gameObject, roots[index], /*after=*/index > cur);
}

void Transform::NoteOrderChanged() {
    if (gameObject)   // UI draws in hierarchy order, so caches keyed on it re-sort
        if (Scene* sc = gameObject->scene()) sc->MarkStructureChanged();
}

bool Transform::SetParent(Transform* parent, bool worldPositionStays) {
    if (parent == m_parent) return true;
    for (Transform* t = parent; t; t = t->m_parent)
        if (t == this) return false;   // self or a descendant: would form a cycle
    const Vec3 world = Position();
    const Quat worldRot = Rotation();
    const Vec3 worldScale = LossyScale();
    if (m_parent) {
        auto& sib = m_parent->m_children;
        sib.erase(std::remove(sib.begin(), sib.end(), this), sib.end());
    }
    m_parent = parent;
    if (m_parent) m_parent->m_children.push_back(this);
    if (gameObject)   // a reparent changes which bones / names a model's caches see
        if (Scene* sc = gameObject->scene()) sc->MarkStructureChanged();
    // Preserve the world pose (position, rotation and scale) only when asked.
    if (worldPositionStays) {
        SetPosition(world);
        SetRotation(worldRot);
        // Scale follows LossyScale(): the world scale divided by the new parent's. Under a
        // rotated parent with non-uniform scale a child turned relative to it would need a
        // shear, which a TRS transform can't hold: position and rotation stay exact and
        // the scale is the per-axis lossy one (an axis with zero parent scale is kept).
        const Vec3 ps = m_parent ? m_parent->LossyScale() : Vec3::One;
        if (ps.x != 0) localScale.x = worldScale.x / ps.x;
        if (ps.y != 0) localScale.y = worldScale.y / ps.y;
        if (ps.z != 0) localScale.z = worldScale.z / ps.z;
    }
    return true;
}

} // namespace okay
