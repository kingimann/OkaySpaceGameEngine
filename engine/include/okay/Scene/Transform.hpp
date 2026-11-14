#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Math.hpp"
#include <atomic>
#include <cstdint>
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

    /// Place the object at a world position: the local position becomes what puts it there under the
    /// whole parent chain (a parent that is stretched on one axis and a child of it that is turned
    /// included). A parent with a collapsed axis (a scale of 0) cannot be undone on that axis; the
    /// local value of that axis is then kept.
    void SetPosition(const Vec3& worldPos);
    void SetRotation(const Quat& worldRot);
    /// Both at once (world space).
    void SetPositionAndRotation(const Vec3& worldPos, const Quat& worldRot) { SetRotation(worldRot); SetPosition(worldPos); }
    /// Both at once in the parent's frame (the rotation is normalized).
    void SetLocalPositionAndRotation(const Vec3& pos, const Quat& rot) { localPosition = pos; localRotation = rot.Normalized(); }
    /// Set the world-space scale (per axis, as LossyScale() reads it): the local scale
    /// becomes the wanted scale divided by the parent's lossy scale. An axis whose parent
    /// scale is zero is left alone. SetScale is the same call.
    void SetLossyScale(const Vec3& worldScale);
    void SetScale(const Vec3& worldScale) { SetLossyScale(worldScale); }
    /// Turn so Forward() points at `worldTarget`, with `worldUp` as the up hint. Does
    /// nothing when the target is at this position.
    void LookAt(const Vec3& worldTarget, const Vec3& worldUp = Vec3::Up);
    /// LookAt the current position of another transform (nothing happens for a null one).
    void LookAt(const Transform* target, const Vec3& worldUp = Vec3::Up) { if (target) LookAt(target->Position(), worldUp); }
    /// Turn so Forward() points along `worldDirection` (a direction, not a point). A zero, NaN or infinite
    /// direction does nothing. A direction along the up hint has no roll of its own: the hint is swapped for
    /// a world axis that is not parallel to it, so the result is always a finite rotation.
    void LookDirection(const Vec3& worldDirection, const Vec3& worldUp = Vec3::Up);
    /// Turn about `worldUp` only: Forward() ends up level, pointing at the side of the target as seen along
    /// that axis (an enemy facing the player on level ground, a turret base). The part of the direction along
    /// `worldUp` is dropped. Nothing happens when the target is straight above or below, or `worldUp` is zero.
    void LookAtYaw(const Vec3& worldTarget, const Vec3& worldUp = Vec3::Up);
    /// Which of the object's own axes a 2D look turns toward the target.
    enum class Facing2D { Right, Up };
    /// 2D: turn about the world Z axis so `facing` (Right() or Up() of the object) points at the target in the
    /// X / Y plane; Z is ignored. Nothing happens when the target is at this position in that plane.
    void LookAt2D(const Vec3& worldTarget, Facing2D facing = Facing2D::Right);
    /// Turn toward facing `worldTarget` by at most `maxDegrees` (the frame's turn speed times the frame time).
    /// Returns true once it faces the target. A negative or NaN `maxDegrees` turns nothing. Use it every frame
    /// for a turn that is smooth and does not depend on the frame rate.
    bool RotateTowards(const Vec3& worldTarget, float maxDegrees, const Vec3& worldUp = Vec3::Up);
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
    /// The local coordinates of a world point. An object whose matrix cannot be inverted (a scale of 0
    /// on some axis, here or in a parent) has no exact answer: each collapsed axis reads 0 and the other
    /// axes are exact, which is the nearest point of the object to `world` (the pseudo-inverse).
    Vec3 InverseTransformPoint(const Vec3& world) const;
    Vec3 TransformDirection(const Vec3& dir) const { return Rotation() * dir; }
    Vec3 InverseTransformDirection(const Vec3& dir) const { return Rotation().Conjugate() * dir; }
    /// Like TransformPoint without the position: the vector is turned and scaled (a
    /// direction is only turned).
    Vec3 TransformVector(const Vec3& local) const { return LocalToWorldMatrix().MultiplyVector(local); }
    /// The inverse of TransformVector; a collapsed axis reads 0 (see InverseTransformPoint).
    Vec3 InverseTransformVector(const Vec3& world) const;

    // ---- Matrices ------------------------------------------------------
    Mat4 LocalToWorldMatrix() const;
    /// Changes whenever this transform's world matrix changes (its own local
    /// values or any parent's). Cheap "did it move?" check for caches.
    unsigned long long WorldVersion() const { WorldCache(); return m_wStamp; }
    /// Diagnostics for tests and profiling, counted per thread, never reset: how many transforms world reads
    /// (Position, Rotation, LossyScale, LocalToWorldMatrix, WorldVersion and what is built on them) had to
    /// look at, and how many world matrices were rebuilt. A read of an unchanged chain adds its depth to the
    /// first and nothing to the second.
    static std::uint64_t WorldWalkSteps();
    static std::uint64_t WorldRebuilds();
    /// Diagnostic, per thread: child-list slots visited or moved by list maintenance (taking a child out of
    /// a list, putting a list in order again, reordering). Taking children out of one parent one at a time
    /// adds a small constant each, however long the list is.
    static std::uint64_t ChildListWork();

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
    /// root) is always accepted. Taking this transform out of its old parent's child list costs
    /// the same however long the list is (a list of 64 or fewer is erased from; a longer one
    /// leaves a hole that is swept out in bulk, so a parent's 300000 children can leave one at a time),
    /// and the order of the others does not change.
    bool SetParent(Transform* parent, bool worldPositionStays = true);
    /// SetParent, then put this transform at sibling position `siblingIndex` (clamped, so 0 or less is
    /// first and anything past the end is last) among the new parent's children, or among the scene's
    /// root objects for nullptr. Nothing changes (false) when SetParent refuses.
    bool SetParent(Transform* parent, bool worldPositionStays, int siblingIndex);
    /// The children in sibling order. Taking a child out of a very long list is lazy (see SetParent), so
    /// this first settles the list; the vector it returns never holds a hole.
    const std::vector<Transform*>& Children() const { return Kids(); }
    int ChildCount() const { return static_cast<int>(m_children.size() - m_tombs.load(std::memory_order_relaxed)); }
    /// The child at `index` in sibling order, or nullptr when out of range.
    Transform* GetChild(int index) const {
        const auto& kids = Kids();
        return index >= 0 && index < static_cast<int>(kids.size()) ? kids[static_cast<std::size_t>(index)] : nullptr;
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
    void MoveChild(Transform* child, int dir);
    /// Move `child` to the first (front) or last (back) sibling position.
    void MoveChildToEdge(Transform* child, bool toFront);
    /// Position `child` immediately before/after `anchor` in the child list
    /// (drag-to-reorder). Both must already be children of this transform.
    void ReorderChild(Transform* child, Transform* anchor, bool after);

private:
    friend class GameObject;
    friend class Scene;
    bool PlaceNextTo(Transform* anchor, bool after);
    Vec3 WorldToParentLocal(const Vec3& worldPos) const;   // a world point in the parent's frame
    void NoteOrderChanged();   // sibling order changed: the scene's structure version moves
    Transform* m_parent = nullptr;

    // ---- Child list --------------------------------------------------------
    // m_children holds the children in order. A child of a long list (see kHoleMin) that leaves is not
    // erased: its slot becomes nullptr (a hole, counted in m_tombs) and the holes are swept out in bulk,
    // when more than half the slots are holes or when the list is read (Kids). Every child knows its slot
    // (m_slot), so taking it out needs no search. Readers only ever see a list without holes.
    static constexpr std::size_t kHoleMin = 64;
    const std::vector<Transform*>& Kids() const {
        if (m_tombs.load(std::memory_order_acquire) != 0) SettleChildren();
        return m_children;
    }
    void SettleChildren() const;                 // sweep the holes out, renumber the slots (locks while it does)
    void DropChild(Transform* child);            // child leaves this list; the order of the rest is kept
    void ReindexChildren(std::size_t from);      // slots of m_children[from..] after an insert / erase
    /// Remove every child for which `dying(child)` is true, in one pass (Scene::DetachDying).
    template <class Pred> void RemoveChildrenIf(Pred dying) {
        SettleChildren();
        m_children.erase(std::remove_if(m_children.begin(), m_children.end(), dying), m_children.end());
        ReindexChildren(0);
    }
    std::vector<Transform*> m_children;
    std::size_t m_slot = 0;                      // this transform's index in its parent's m_children
    mutable std::atomic<std::uint32_t> m_tombs{0};   // holes in m_children

    // ---- World-matrix cache (see WorldCache in Transform.cpp) ----------------
    // The local fields are public and are written directly all over the engine, so the cache keeps a copy
    // of the values it was built from and checks them on every read (no dirty flag could see a plain
    // `t->localPosition.x = 3`).
    const Mat4& WorldCache() const;
    const Mat4& RefreshWorld() const;
    bool ChainIsCurrent() const;                 // every transform from here to the root matches what its cache was built from
    void ReadWorldPose(Vec3& pos, Quat& rot, Vec3& scale) const;   // the world pose with one walk of the parent chain
    mutable Mat4 m_world;
    mutable Quat m_wRot = Quat::Identity;
    mutable Vec3 m_wScale = Vec3::One;           // LossyScale(): the parent's, times localScale, root first
    mutable Vec3 m_wP;                           // the local position, rotation and scale the cache was built from
    mutable Quat m_wR;
    mutable Vec3 m_wS;
    mutable const Transform* m_wParent = nullptr;
    mutable unsigned long long m_wParentStamp = 0, m_wStamp = 0;
    mutable bool m_wValid = false;
};

} // namespace okay
