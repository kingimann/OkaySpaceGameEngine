#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// No-code "chase" AI: moves toward a named target each frame (enemies seeking
/// the player, homing pickups, companions). Stops within `stopDistance`. Drives
/// a sibling Rigidbody2D's velocity when present, else moves the Transform.
class FollowTarget2D : public Behaviour {
public:
    std::string target;          // name of the object to follow
    float speed = 3.0f;
    float stopDistance = 0.0f;   // keep this far from the target (0 = touch)

    void Update(float dt) override {
        if (!transform || target.empty()) return;
        Scene* s = GetScene();
        GameObject* g = s ? s->Find(target) : nullptr;
        if (!g) return;
        Vec3 me = transform->Position(), to = g->transform->Position();
        float dx = to.x - me.x, dy = to.y - me.y;
        float dist = Mathf::Sqrt(dx * dx + dy * dy);
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr;
        if (dist <= stopDistance + 1e-4f) {
            if (rb) rb->velocity = {0, 0};
            return;
        }
        float nx = dx / dist, ny = dy / dist;
        if (rb) rb->velocity = {nx * speed, ny * speed};
        else transform->Translate({nx * speed * dt, ny * speed * dt, 0.0f});
    }
};

} // namespace okay
