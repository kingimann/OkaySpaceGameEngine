#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include <cmath>

namespace okay {

/// A simple melee weapon for the player: on attack input it damages every NPC within
/// `range` and inside a `arc`-degree cone in front, on a `cooldown`. Put it on the
/// player; NPCs (NPCController) take the hit and die at 0 HP. Broadcasts
/// `player_attack` on each swing.
class MeleeAttacker : public Behaviour {
public:
    float damage   = 15.0f;
    float range    = 2.5f;
    float arc      = 120.0f;     // attack cone, degrees (full angle)
    float cooldown = 0.5f;
    char  attackKey = 'f';       // 0 = no key
    bool  useMouse = true;       // also swing on left mouse button

    bool Ready() const { return m_timer <= 0.0f; }

    void Update(float dt) override {
        if (m_timer > 0.0f) m_timer -= dt;
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy: don't read local input
        bool pressed = (attackKey && Input::GetKeyDown(attackKey)) ||
                       (useMouse && Input::GetMouseButtonDown(0));
        if (!pressed || m_timer > 0.0f || !transform) return;
        m_timer = cooldown;
        Swing();
    }

    /// Swing now (exposed for buttons/scripts/testing).
    void Swing() {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s || !transform) return;
        Vec3 pos = transform->Position();
        Vec3 fwd = Planar(transform->Forward());
        float cosHalf = std::cos(arc * 0.5f * 0.01745329f);
        for (NPCController* npc : s->FindObjectsOfType<NPCController>()) {
            if (!npc->gameObject || !npc->gameObject->transform || npc->IsDead()) continue;
            Vec3 d = npc->gameObject->transform->Position();
            d.x -= pos.x; d.y = 0.0f; d.z -= pos.z;
            float dist = std::sqrt(d.x * d.x + d.z * d.z);
            if (dist > range || dist < 1e-4f) continue;
            float dot = (d.x / dist) * fwd.x + (d.z / dist) * fwd.z;   // cos(angle to target)
            if (dot >= cosHalf) npc->Damage(damage);
        }
        for (ActionList* al : s->FindObjectsOfType<ActionList>()) al->ReceiveMessage("player_attack");
    }

private:
    static Vec3 Planar(const Vec3& v) {
        Vec3 p{v.x, 0.0f, v.z};
        float m = std::sqrt(p.x * p.x + p.z * p.z);
        return m > 1e-5f ? Vec3{p.x / m, 0.0f, p.z / m} : Vec3{0, 0, 1};
    }
    float m_timer = 0.0f;
};

} // namespace okay
