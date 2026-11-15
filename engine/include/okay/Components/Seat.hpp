#pragma once
// A place to sit. The player snaps to sitPoint (local) facing `facing` degrees
// off this object's yaw, then the Animator plays SitDown → Sit. Move or jump
// stands them up at exitPoint. Occupied is runtime only.
//
// On a vehicle (this object or a parent has a VehicleController) the seat is a vehicle seat: the occupant rides
// along, moving does not stand them up (the move keys drive), the use key E gets in and out, and the occupant's
// colliders are switched off while seated so the body does not push the car. The first Seat in the vehicle's
// hierarchy (the vehicle object, then its children in order) is the driver's seat: the car takes input only while a
// player sits in it. Other seats on the vehicle are passenger seats.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>
#include <vector>

namespace okay {

class Collider3D;

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
    /// True when this seat is on a vehicle (this object or a parent has a VehicleController).
    bool OnVehicle() const;
    /// True when this is the vehicle's driver seat (the first Seat in the vehicle's hierarchy).
    bool IsDriverSeat() const;
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
    bool  m_claimedThisFrame = false;          // the use key that got in must not also get out
    std::vector<Collider3D*> m_offColliders;   // the occupant's colliders switched off on a vehicle
    void RestoreColliders();
};

} // namespace okay
