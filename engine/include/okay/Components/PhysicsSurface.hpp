#pragma once
// PhysicsSurface: the "Physics Material" slot of an object. Pick a material by name (a built-in preset such as Ice, Rubber or
// Mud, or a project `.okayphysmat` file) and every collider on the object (and, if asked, on its children) feels that way:
// its friction, bounciness and mixing rules are set from the material, and the solver also reads the extra things a
// collider has no field for (grip when still, rolling friction, damping, slope grip). Optionally the body's mass comes
// from the material's density times the volume of its colliders.
//
// Why a component: it sits beside the colliders, works for 2D and 3D, can be added to a whole group of objects in bulk, and
// can be changed while the game runs (SetMaterial, or the property "material"). An object with no PhysicsSurface keeps using
// its collider's own friction and bounciness exactly as before, so old scenes load and play unchanged.
//
// Written from this engine's PhysicsMaterialAsset.hpp and the collider classes; mass = density x volume is the textbook rule.
#include "okay/Core/Paths.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/PhysicsMaterialAsset.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace okay {

class PhysicsSurface : public Behaviour {
public:
    std::string material = "Default";     ///< preset or project material name
    bool includeChildren = false;         ///< also feel this way on every child object's colliders
    bool massFromDensity = false;         ///< body mass = density x volume of the colliders (set when the game starts and on SetMaterial)

    /// The material now in use (nullptr when the name is unknown: the colliders keep what they had).
    const PhysicsMaterialData* Data() const {
        if (m_name != material || (!m_data && !material.empty())) {
            m_name = material; m_data = PhysicsMaterials::Find(material);
            // An unknown name may be a project file: look through the project's asset folder once (editor and player alike).
            if (!m_data && !material.empty() && !ScannedOnce()) { ScannedOnce() = true; PhysicsMaterials::ScanFolder(Paths::AssetBaseDir()); m_data = PhysicsMaterials::Find(material); }
        }
        return m_data;
    }
    std::string GetMaterial() const { return material; }
    /// Change the material now (also while the game runs). Returns false when no material has that name (nothing changes).
    bool SetMaterial(const std::string& name) {
        if (!PhysicsMaterials::Find(name)) return false;
        material = name; Apply();
        return true;
    }
    /// What gameplay and audio ask for: "Ice", "Grass"...
    std::string SurfaceName() const { const PhysicsMaterialData* d = Data(); return d ? PhysicsMaterials::SurfaceName(*d) : std::string(); }

    /// Copy the material's numbers into the colliders, and the mass when massFromDensity is on.
    void Apply() {
        const PhysicsMaterialData* d = Data();
        m_applied = material;
        if (!d || !gameObject) return;
        for (Collider3D* c : Colliders3D()) { c->friction = d->dynamicFriction; c->bounciness = d->bounciness; c->frictionCombine = d->frictionCombine; c->bounceCombine = d->bounceCombine; }
        for (Collider2D* c : Colliders2D()) { c->friction = d->dynamicFriction; c->bounciness = d->bounciness; c->frictionCombine = d->frictionCombine; c->bounceCombine = d->bounceCombine; }
        if (massFromDensity) {
            if (auto* rb = gameObject->GetComponent<Rigidbody3D>()) {
                const float v = Volume();
                if (v > 1e-6f) rb->mass = std::max(0.001f, d->density * v);
            }
        }
    }

    /// Volume (cubic metres) of the colliders on this object: box, sphere, capsule and cylinder exactly, a mesh by its bounds.
    float Volume() const {
        float total = 0.0f;
        if (!gameObject) return 0.0f;
        for (Collider3D* c : gameObject->GetComponents<Collider3D>()) {
            if (c->isTrigger) continue;
            const float kPi = 3.14159265f;
            switch (c->shape()) {
            case Collider3D::Shape::Box: { Vec3 h = static_cast<BoxCollider3D*>(c)->HalfExtents(); total += 8.0f * h.x * h.y * h.z; break; }
            case Collider3D::Shape::Sphere: { float r = static_cast<SphereCollider3D*>(c)->WorldRadius(); total += 4.0f / 3.0f * kPi * r * r * r; break; }
            case Collider3D::Shape::Capsule: case Collider3D::Shape::Cylinder: {
                auto* cap = static_cast<CapsuleCollider3D*>(c);
                float r = cap->WorldRadius(); Vec3 a, b; cap->Segment(a, b); float L = (b - a).Magnitude();
                if (c->shape() == Collider3D::Shape::Cylinder) total += kPi * r * r * (L + 0.1f * r);
                else total += kPi * r * r * L + 4.0f / 3.0f * kPi * r * r * r;
                break; }
            default: { Vec3 mn, mx; c->WorldAABB(mn, mx); Vec3 s = mx - mn; total += std::max(0.0f, s.x * s.y * s.z); break; }
            }
        }
        return total;
    }

    void Awake() override { Apply(); }
    void Start() override { Apply(); }
    void Update(float) override { if (m_applied != material) Apply(); }   // the Inspector (or a script) changed the name

    /// Forget that the project folder was scanned (after the asset folder changes, and in tests).
    static void RescanNextTime() { ScannedOnce() = false; }
private:
    static bool& ScannedOnce() { static bool b = false; return b; }
    std::vector<Collider3D*> Colliders3D() const { return includeChildren ? gameObject->GetComponentsInChildren<Collider3D>() : gameObject->GetComponents<Collider3D>(); }
    std::vector<Collider2D*> Colliders2D() const { return includeChildren ? gameObject->GetComponentsInChildren<Collider2D>() : gameObject->GetComponents<Collider2D>(); }
    mutable std::string m_name;
    mutable const PhysicsMaterialData* m_data = nullptr;
    std::string m_applied;
};

/// The material an object's colliders use, or nullptr (reads the object's PhysicsSurface).
inline const PhysicsMaterialData* SurfaceOf(const GameObject* go) {
    if (!go) return nullptr;
    const auto* s = go->GetComponent<PhysicsSurface>();
    return s ? s->Data() : nullptr;
}

/// Give an object a material by name (adds a PhysicsSurface when it has none). False when the name is unknown.
inline bool AssignPhysicsMaterial(GameObject& go, const std::string& name, bool includeChildren = false) {
    if (!PhysicsMaterials::Find(name)) return false;
    auto* s = go.GetComponent<PhysicsSurface>();
    if (!s) s = go.AddComponent<PhysicsSurface>();
    s->includeChildren = includeChildren;
    return s->SetMaterial(name);
}

} // namespace okay
