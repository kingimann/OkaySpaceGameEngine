#pragma once
// A place to sit. The player snaps to sitPoint (local) facing `facing` degrees
// off this object's yaw, then the Animator plays SitDown → Sit. Move or jump
// stands them up at exitPoint. Occupied is runtime only.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>

namespace okay {

class Seat : public Behaviour {
public:
    float seatHeight = 0.45f;          ///< metres; the chair clips are baked for this
    Vec3  sitPoint{0.0f, 0.0f, 0.0f};  ///< local point the body moves to
    float facing = 0.0f;               ///< extra yaw, degrees (0 = this object's forward, -Z)
    int   type = 0;                    ///< 0 chair, 1 ground, 2 bench (bench uses the chair clips)
    Vec3  exitPoint{0.0f, 0.0f, 0.7f}; ///< local point they stand up to
    float useRadius = 1.2f;
    std::string prompt = "Sit";        ///< seat_prompt() while a player is in range
    bool  occupied = false;
    bool  playerInRange = false;

    GameObject* Occupant() const { return m_who; }
    Vec3 SitWorld() const { return transform ? transform->TransformPoint(sitPoint) : Vec3{}; }
    Vec3 ExitWorld() const { return transform ? transform->TransformPoint(exitPoint) : Vec3{}; }
    Quat FaceWorld() const {
        if (!transform) return Quat::Identity;
        return (transform->Rotation() * Quat::Euler(0.0f, facing, 0.0f)).Normalized();
    }

    void Claim(GameObject* who);
    void Release();
    void Update(float dt) override;
    void OnDestroy() override { Release(); }

private:
    GameObject* m_who = nullptr;
    Vec3 m_from{};
    Quat m_fromRot = Quat::Identity;
    float m_blend = 1.0f;
    float m_savedGravity = 1.0f;
    bool  m_hadGravity = false;
};

} // namespace okay
