#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Math.hpp"
#include <vector>
#include <utility>
#include <algorithm>

namespace okay {

/// Spatial state of a GameObject and its place in the hierarchy. Every
/// GameObject owns exactly one Transform.
class Transform : public Component {
public:
    // ---- Local space ---------------------------------------------------
    Vec3 localPosition = Vec3::Zero;
    Quat localRotation = Quat::Identity;
    Vec3 localScale    = Vec3::One;

    // ---- World space accessors ----------------------------------------
    Vec3 Position() const;
    Quat Rotation() const;
    Vec3 LossyScale() const;

    void SetPosition(const Vec3& worldPos);
    void SetRotation(const Quat& worldRot);
    void Translate(const Vec3& delta) { localPosition += delta; }
    void Rotate(const Vec3& eulerDegrees) {
        localRotation = (localRotation * Quat::Euler(eulerDegrees)).Normalized();
    }

    // ---- Direction vectors (world space) ------------------------------
    Vec3 Forward() const { return Rotation() * Vec3::Forward; }
    Vec3 Up() const      { return Rotation() * Vec3::Up; }
    Vec3 Right() const   { return Rotation() * Vec3::Right; }

    // ---- Local <-> world conversions ----------------------------------
    Vec3 TransformPoint(const Vec3& local) const { return LocalToWorldMatrix().MultiplyPoint(local); }
    Vec3 InverseTransformPoint(const Vec3& world) const {
        return LocalToWorldMatrix().Inverse().MultiplyPoint(world);
    }
    Vec3 TransformDirection(const Vec3& dir) const { return Rotation() * dir; }
    Vec3 InverseTransformDirection(const Vec3& dir) const { return Rotation().Conjugate() * dir; }

    // ---- Matrices ------------------------------------------------------
    Mat4 LocalToWorldMatrix() const;
    /// Changes whenever this transform's world matrix changes (its own local
    /// values or any parent's). Cheap "did it move?" check for caches.
    unsigned long long WorldVersion() const { WorldCache(); return m_wStamp; }

    // ---- Hierarchy -----------------------------------------------------
    Transform* Parent() const { return m_parent; }
    /// Reparent this transform. When `worldPositionStays` is true (the
    /// default) the world pose is preserved; when false the local pose is kept.
    /// Refuses (returns false, nothing changes) a parent that is this transform
    /// or one of its descendants, which would make the hierarchy a loop.
    bool SetParent(Transform* parent, bool worldPositionStays = true);
    const std::vector<Transform*>& Children() const { return m_children; }
    int ChildCount() const { return static_cast<int>(m_children.size()); }
    /// Reorder a child among its siblings by `dir` (-1 = earlier/up, +1 = later/
    /// down). No-op if it's already at the end being moved.
    void MoveChild(Transform* child, int dir) {
        for (std::size_t i = 0; i < m_children.size(); ++i) {
            if (m_children[i] != child) continue;
            std::size_t j = (dir < 0) ? (i == 0 ? i : i - 1)
                                      : (i + 1 >= m_children.size() ? i : i + 1);
            if (i != j) { std::swap(m_children[i], m_children[j]); NoteOrderChanged(); }
            return;
        }
    }
    /// Move `child` to the first (front) or last (back) sibling position.
    void MoveChildToEdge(Transform* child, bool toFront) {
        auto it = std::find(m_children.begin(), m_children.end(), child);
        if (it == m_children.end()) return;
        m_children.erase(it);
        if (toFront) m_children.insert(m_children.begin(), child);
        else         m_children.push_back(child);
        NoteOrderChanged();
    }
    /// Position `child` immediately before/after `anchor` in the child list
    /// (drag-to-reorder). Both must already be children of this transform.
    void ReorderChild(Transform* child, Transform* anchor, bool after) {
        if (child == anchor) return;
        auto rm = std::find(m_children.begin(), m_children.end(), child);
        if (rm == m_children.end()) return;
        m_children.erase(rm);
        auto at = std::find(m_children.begin(), m_children.end(), anchor);
        if (at == m_children.end()) { m_children.push_back(child); NoteOrderChanged(); return; }
        if (after) ++at;
        m_children.insert(at, child);
        NoteOrderChanged();
    }

private:
    friend class GameObject;
    void NoteOrderChanged();   // sibling order changed: the scene's structure version moves
    Transform* m_parent = nullptr;
    std::vector<Transform*> m_children;

    // World-matrix cache (see WorldCache in Transform.cpp).
    const Mat4& WorldCache() const;
    mutable Mat4 m_world;
    mutable Quat m_wRot = Quat::Identity;
    mutable Vec3 m_wP, m_wS;
    mutable Quat m_wR;
    mutable const Transform* m_wParent = nullptr;
    mutable unsigned long long m_wParentStamp = 0, m_wStamp = 0;
    mutable bool m_wValid = false;
};

} // namespace okay
