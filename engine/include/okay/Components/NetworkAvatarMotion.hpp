#pragma once
// ---------------------------------------------------------------------------
// NetworkAvatarMotion — makes a REMOTE player avatar look alive using only the
// position the network already replicates. A remote avatar's transform is moved by
// NetworkManager (position sync); this component watches that movement and:
//   * turns the avatar to FACE the direction it's travelling, and
//   * drives a sibling Character's animation (idle / walk / run) from its speed.
// So other players turn and walk correctly with no extra bandwidth and no protocol
// change. It also reports the speed and direction it sees to the Character
// (moveSpeed / moveAngle), so the stride covers the ground it really moves over
// instead of skating at a nominal cadence. If a remote NetworkSync on the same
// object already replicates rotation and animation, that one owns `anim` and the
// rotation and this only reports the gait. It runs on proxies on purpose (it is
// NOT input — it reads synced motion), so it is intentionally NOT gated by
// IsLocallyControlled.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Math/Quat.hpp"
#include <algorithm>
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
        // Speed from the eased position, smoothed (~80 ms): raw per-frame steps jitter.
        const float k = std::min(1.0f, dt * 12.0f);
        m_vx += (d.x / dt - m_vx) * k; m_vz += (d.z / dt - m_vz) * k;
        const float speed = std::sqrt(m_vx * m_vx + m_vz * m_vz);
        const bool moving = speed > moveEpsilon;

        // A remote NetworkSync here already brings the owner's rotation and anim:
        // let it own them (two writers made the walk / run flicker).
        auto* ns = gameObject ? gameObject->GetComponent<NetworkSync>() : nullptr;
        const bool syncOwnsPose = ns && ns->enabled && !ns->IsLocallyOwned();

        if (moving && !syncOwnsPose) {
            Quat want = Quat::LookRotation(Vec3{m_vx, 0.0f, m_vz});
            if (turnSpeed <= 0.0f) transform->localRotation = want;
            else {
                float t = (turnSpeed * dt) / 180.0f; if (t > 1.0f) t = 1.0f;
                transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
            }
        }
        if (auto* ch = gameObject ? gameObject->GetComponent<Character>() : nullptr) {
            if (syncOwnsPose && ns->syncAnimation) return;   // NetworkSync drives this Character's gait too
            if (!syncOwnsPose) ch->anim = moving ? (speed >= runSpeed ? 3 : 2) : 1;
            ch->moveSpeed = moving ? speed : 0.0f;
            ch->moveAngle = moving ? MoveAngleFor(*transform, m_vx, m_vz) : 0.0f;
        }
    }

private:
    Vec3 m_last{0, 0, 0};
    bool m_have = false;
    float m_vx = 0.0f, m_vz = 0.0f;   // smoothed ground velocity
};

} // namespace okay
