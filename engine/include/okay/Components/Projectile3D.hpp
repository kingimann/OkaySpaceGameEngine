#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// Damage the nearest HealthStat on `target` or one of its parents. Returns true
/// when something took the hit.
inline bool DamageGameObject(GameObject* target, float amount) {
    for (GameObject* g = target; g;) {
        if (auto* hp = g->GetComponent<HealthStat>()) { hp->Damage(amount); return true; }
        Transform* p = g->transform ? g->transform->Parent() : nullptr;
        g = p ? p->gameObject : nullptr;
    }
    return false;
}

/// A bullet / rocket / arrow in 3D. It flies along `velocity`, sweeps a ray each
/// frame so fast shots never pass through thin walls, damages the HealthStat it
/// hits, pushes Rigidbody3Ds, and calls on_hit() on its own script. Spawned by
/// the TopDownShooterController (or any script); also fine on a prefab.
class Projectile3D : public Behaviour {
public:
    Vec3  velocity{0.0f, 0.0f, 20.0f};   ///< world units per second
    float damage = 10.0f;
    float lifetime = 3.0f;               ///< seconds before it disappears
    float gravityScale = 0.0f;           ///< 0 = straight line; 1 = drops like a grenade
    float impactForce = 4.0f;            ///< impulse given to a Rigidbody3D it hits
    int   pierce = 0;                    ///< extra targets it passes through
    bool  bounce = false;                ///< ricochet off walls instead of stopping
    int   maxBounces = 2;
    /// Who fired it (never hit). Transient: set by the shooter, not saved.
    GameObject* owner = nullptr;

    int Hits() const { return m_hits; }
    bool Alive() const { return !m_dead; }

    void Update(float dt) override {
        if (m_dead || !transform || !gameObject) return;
        Scene* sc = gameObject->scene();
        m_age += dt;
        if (m_age >= lifetime) { Kill(sc); return; }

        if (gravityScale != 0.0f && sc) velocity = velocity + sc->physics3D().gravity * (gravityScale * dt);
        Vec3 from = transform->Position();
        Vec3 step = velocity * dt;
        float len = step.Magnitude();
        if (len < 1e-6f) return;
        Vec3 dir = step * (1.0f / len);

        if (sc) {
            RaycastHit3D hit = sc->physics3D().Raycast(*sc, from, dir, len, owner);
            if (hit.hit && hit.gameObject != gameObject && hit.gameObject != m_lastHit) {
                OnHit(*sc, hit, dir);
                if (m_dead) return;
                if (bounce && m_bounces < maxBounces && !m_pierced) {
                    ++m_bounces;
                    velocity = Vec3::Reflect(velocity, hit.normal);
                    transform->SetPosition(hit.point + hit.normal * 0.01f);
                    return;
                }
            }
            m_pierced = false;
        }
        transform->SetPosition(from + step);
        // Point the mesh along the flight (meshes face -Z).
        transform->localRotation = Quat::LookRotation({-dir.x, -dir.y, -dir.z});
    }

private:
    float m_age = 0.0f;
    int   m_hits = 0, m_bounces = 0;
    bool  m_dead = false, m_pierced = false;
    GameObject* m_lastHit = nullptr;

    void OnHit(Scene& sc, const RaycastHit3D& hit, const Vec3& dir) {
        ++m_hits;
        m_lastHit = hit.gameObject;
        const bool damaged = DamageGameObject(hit.gameObject, damage);
        if (hit.gameObject)
            if (auto* rb = hit.gameObject->GetComponent<Rigidbody3D>())
                if (impactForce > 0.0f) rb->AddImpulse(dir * impactForce);
        if (auto* s = gameObject->GetComponent<ScriptComponent>())
            if (s->VM()) s->VM()->CallEvent("on_hit");
        // Pierce only through things that can be hurt; walls always stop it.
        if (damaged && m_hits <= pierce) { m_pierced = true; return; }
        if (bounce && !damaged && m_bounces < maxBounces) return;
        Kill(&sc);
    }
    void Kill(Scene* sc) {
        m_dead = true;
        if (sc && gameObject) sc->Destroy(gameObject);
    }
};

} // namespace okay
