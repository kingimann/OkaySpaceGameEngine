#pragma once
// ---------------------------------------------------------------------------
// NetworkAvatarMotion — makes a REMOTE player avatar look alive using only the
// position the network already replicates. A remote avatar's transform is moved by
// NetworkManager (position sync); this component watches that movement and:
//   * turns the avatar to FACE the direction it's travelling, and
//   * drives a sibling Character's animation (idle / walk / run) from its speed.
// So other players turn and walk correctly with no extra bandwidth and no protocol
// change. It runs on proxies on purpose (it is NOT input — it reads synced motion),
// so it is intentionally NOT gated by IsLocallyControlled.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Math/Quat.hpp"
#include <cmath>

namespace okay {

class NetworkAvatarMotion : public Behaviour {
public:
    float turnSpeed   = 540.0f;   ///< how fast it rotates to face travel (deg/sec; <=0 = snap)
    float moveEpsilon = 0.04f;    ///< speed (units/sec) below which it's "standing still"
    float runSpeed    = 3.2f;     ///< speed at/above which the walk anim becomes a run

    void Start() override { if (transform) m_last = transform->Position(); m_have = true; }

    void Update(float dt) override {
        if (!transform || dt <= 0.0f) return;
        Vec3 p = transform->Position();
        if (!m_have) { m_last = p; m_have = true; return; }
        Vec3 d{p.x - m_last.x, 0.0f, p.z - m_last.z};
        m_last = p;
        float dist = std::sqrt(d.x * d.x + d.z * d.z);
        float speed = dist / dt;
        bool moving = speed > moveEpsilon;

        if (moving) {
            Quat want = Quat::LookRotation(Vec3{d.x, 0.0f, d.z});
            if (turnSpeed <= 0.0f) transform->localRotation = want;
            else {
                float t = (turnSpeed * dt) / 180.0f; if (t > 1.0f) t = 1.0f;
                transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
            }
        }
        if (auto* ch = gameObject ? gameObject->GetComponent<Character>() : nullptr)
            ch->anim = moving ? (speed >= runSpeed ? 3 : 2) : 1;
    }

private:
    Vec3 m_last{0, 0, 0};
    bool m_have = false;
};

} // namespace okay
