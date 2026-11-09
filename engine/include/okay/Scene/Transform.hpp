#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Math.hpp"
#include <vector>
#include <utility>
#include <algorithm>
#include <limits>
#include <string>

namespace okay {

/// The frame a move or a turn is expressed in.
///  Local: the parent's frame (the object's localPosition / localRotation axes).
///  World: the scene's axes.
///  Self:  the object's own axes (what Forward() / Right() / Up() point along).
enum class Space { Local = 0, World = 1, Self = 2 };

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
    /// Set the world-space scale (per axis, as LossyScale() reads it): the local scale
    /// becomes the wanted scale divided by the parent's lossy scale. An axis whose parent
    /// scale is zero is left alone. SetScale is the same call.
    void SetLossyScale(const Vec3& worldScale);
    void SetScale(const Vec3& worldScale) { SetLossyScale(worldScale); }
    /// Turn so Forward() points at `worldTarget`, with `worldUp` as the up hint. Does
    /// nothing when the target is at this position.
    void LookAt(const Vec3& worldTarget, const Vec3& worldUp = Vec3::Up);
    /// Rotation as Euler angles in degrees (Z, X, Y order, as Quat::Euler builds it).
    Vec3 EulerAngles() const { return Rotation().ToEuler(); }
    void SetEulerAngles(const Vec3& degrees) { SetRotation(Quat::Euler(degrees)); }
    Vec3 LocalEulerAngles() const { return localRotation.ToEuler(); }
    void SetLocalEulerAngles(const Vec3& degrees) { localRotation = Quat::Euler(degrees).Normalized(); }
    /// Move by `delta` in the parent's frame (localPosition += delta).
    void Translate(const Vec3& delta) { localPosition += delta; }
    /// Move by `delta` in the given space. World and Self distances are world units (a
    /// scaled parent does not stretch them); Self moves along the object's own axes.
    void Translate(const Vec3& delta, Space space);
    /// Turn by Euler angles (degrees, Z X Y order) around the object's own axes.
    void Rotate(const Vec3& eulerDegrees) {
        localRotation = (localRotation * Quat::Euler(eulerDegrees)).Normalized();
    }
    /// Turn by Euler angles in the given space: Self around the object's own axes, Local
    /// around the parent's axes, World around the scene's axes.
    void Rotate(const Vec3& eulerDegrees, Space space);
    /// Turn `degrees` around `axis` (which is read in the given space).
    void Rotate(const Vec3& axis, float degrees, Space space = Space::Self);
    /// Orbit the world point `point`: the position swings around it and the rotation
    /// turns with it, around the world `axis` by `degrees`.
    void RotateAround(const Vec3& point, const Vec3& axis, float degrees);

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
    /// Like TransformPoint without the position: the vector is turned and scaled (a
    /// direction is only turned).
    Vec3 TransformVector(const Vec3& local) const { return LocalToWorldMatrix().MultiplyVector(local); }
    Vec3 InverseTransformVector(const Vec3& world) const {
        return LocalToWorldMatrix().Inverse().MultiplyVector(world);
    }

    // ---- Matrices ------------------------------------------------------
    Mat4 LocalToWorldMatrix() const;
    /// Changes whenever this transform's world matrix changes (its own local
    /// values or any parent's). Cheap "did it move?" check for caches.
    unsigned long long WorldVersion() const { WorldCache(); return m_wStamp; }

    // ---- Hierarchy -----------------------------------------------------
    Transform* Parent() const { return m_parent; }
    /// Reparent this transform. When `worldPositionStays` is true (the
    /// default) the world pose is preserved: position and rotation exactly, scale as
    /// LossyScale() (under a rotated parent with non-uniform scale a turned child
    /// can't keep its exact shape, as that would take a shear). When false the local
    /// pose is kept.
    /// Refuses (returns false, nothing changes) a parent that is this transform
    /// or one of its descendants (which would make the hierarchy a loop), a parent whose
    /// object was destroyed, and a parent that lives in another scene. nullptr (make this a
    /// root) is always accepted.
    bool SetParent(Transform* parent, bool worldPositionStays = true);
    const std::vector<Transform*>& Children() const { return m_children; }
    int ChildCount() const { return static_cast<int>(m_children.size()); }
    /// The child at `index` in sibling order, or nullptr when out of range.
    Transform* GetChild(int index) const {
        return index >= 0 && index < ChildCount() ? m_children[static_cast<std::size_t>(index)] : nullptr;
    }
    /// A child by path relative to this transform: "Arm" or "Arm/Hand/Finger" (the first
    /// child of that name at each level). Empty path or no match: nullptr.
    Transform* Find(const std::string& path) const;
    /// The first descendant called `name` (depth first, in sibling order); not this transform.
    Transform* FindDeep(const std::string& name) const;
    /// True when `ancestor` is this transform or any transform above it.
    bool IsChildOf(const Transform* ancestor) const;
    /// The topmost transform above this one (this one when it has no parent).
    Transform* Root();
    const Transform* Root() const;
    /// How many transforms are above this one (0 for a root).
    int Depth() const;
    /// Make every child a root, keeping each one's world pose. Grandchildren stay with their parent.
    void DetachChildren();
    /// Move to the front / back among the siblings.
    void SetAsFirstSibling() { SetSiblingIndex(0); }
    void SetAsLastSibling()  { SetSiblingIndex(std::numeric_limits<int>::max()); }
    /// Become a sibling of `anchor`, placed just before / after it. When `anchor` has a
    /// different parent this reparents, keeping the world pose. False (nothing changes) when
    /// `anchor` is null, this transform, one of its descendants, or in another scene.
    bool PlaceBefore(Transform* anchor) { return PlaceNextTo(anchor, false); }
    bool PlaceAfter(Transform* anchor)  { return PlaceNextTo(anchor, true); }
    /// Position among the siblings (the parent's child list, or among the scene's root
    /// objects for a root). -1 when this transform is not in a scene.
    int GetSiblingIndex() const;
    /// Move to sibling position `index` (clamped to the valid range), keeping the others' order.
    void SetSiblingIndex(int index);
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
    friend class Scene;
    bool PlaceNextTo(Transform* anchor, bool after);
    void NoteOrderChanged();   // sibling order changed: the scene's structure version moves
    Transform* m_parent = nullptr;
    std::vector<Transform*> m_children;

    // World-matrix cache (see WorldCache in Transform.cpp).
    const Mat4& WorldCache() const;
    const Mat4& RefreshWorld() const;
    mutable Mat4 m_world;
    mutable Quat m_wRot = Quat::Identity;
    mutable Vec3 m_wP, m_wS;
    mutable Quat m_wR;
    mutable const Transform* m_wParent = nullptr;
    mutable unsigned long long m_wParentStamp = 0, m_wStamp = 0;
    mutable bool m_wValid = false;
};

} // namespace okay
