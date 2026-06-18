#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Math.hpp"
#include <vector>

namespace okay {

/// Spatial state of a GameObject and its place in the hierarchy. Every
/// GameObject owns exactly one Transform, just like in Unity.
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
    void Translate(const Vec3& delta) { localPosition += delta; }
    void Rotate(const Vec3& eulerDegrees) {
        localRotation = (localRotation * Quat::Euler(eulerDegrees)).Normalized();
    }

    // ---- Direction vectors (world space) ------------------------------
    Vec3 Forward() const { return Rotation() * Vec3::Forward; }
    Vec3 Up() const      { return Rotation() * Vec3::Up; }
    Vec3 Right() const   { return Rotation() * Vec3::Right; }

    // ---- Matrices ------------------------------------------------------
    Mat4 LocalToWorldMatrix() const;

    // ---- Hierarchy -----------------------------------------------------
    Transform* Parent() const { return m_parent; }
    /// Reparent this transform. When `worldPositionStays` is true (Unity's
    /// default) the world pose is preserved; when false the local pose is kept.
    void SetParent(Transform* parent, bool worldPositionStays = true);
    const std::vector<Transform*>& Children() const { return m_children; }
    int ChildCount() const { return static_cast<int>(m_children.size()); }

private:
    friend class GameObject;
    Transform* m_parent = nullptr;
    std::vector<Transform*> m_children;
};

} // namespace okay
