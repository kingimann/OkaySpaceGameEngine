#pragma once
// ---------------------------------------------------------------------------
// Joint3D — a constraint between this body and either a world anchor or another
// Rigidbody3D. Solved each physics step by Physics3D (impulse + position correction):
//   * Distance — keep the two points a fixed distance apart (a rigid rod / taut rope).
//   * Spring   — a damped Hookean spring pulling toward the rest length (bouncy).
//   * Pin      — weld: lock this body to the anchor/other body at their start offset.
//   * Hinge    — pin a point and spin about `axis`. Optional motor. Optional angle
//                limits (degrees from the pose at the start) clamped with
//                Math/JointLimit.hpp, so a door or lever stops instead of spinning
//                a full circle. Limits are off until `useLimits` is set.
//   * Ball     — pin a point, free to turn every way (a shoulder, a chain link). Optional drive.
//   * ConeTwist — a ball joint whose swing stays inside a cone (swingLimit1 / swingLimit2 degrees about the
//                two axes across `axis`) and whose twist about `axis` stays in [minAngle, maxAngle] when
//                `useLimits` is set. Optional drive. Made for ragdolls (hips, shoulders, neck).
//   Drive (Ball and ConeTwist): `useDrive` pulls the body toward `driveTarget` (degrees, the turn from its
//   start pose relative to the other body) like a damped spring, never with more than driveMaxTorque: a
//   powered ragdoll joint (active ragdolls set driveTarget every frame from an animation pose).
//   Ball and ConeTwist pivot about `anchor` (a world point at the start), with or without a connected body.
//
// Put it on the dynamic body. Leave `connectedBody` empty to anchor to a fixed world
// point (`anchor`); set it to another object's name to link two bodies.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <cstdint>
#include <string>

namespace okay {

class Joint3D : public Component {
public:
    enum class Mode { Distance, Spring, Pin, Hinge, Slider, Ball, ConeTwist };

    int   mode = (int)Mode::Distance;
    std::string connectedBody;        ///< name of the other body (empty = world anchor)
    Vec3  anchor{0, 0, 0};            ///< world anchor point when there's no connected body
    float distance = 1.0f;            ///< rest length (Distance / Spring)
    bool  autoConfigure = true;       ///< set `distance`/offset from the start separation
    float spring = 30.0f;             ///< Spring stiffness
    float damper = 2.0f;              ///< Spring damping
    bool  breakable = false;          ///< snap the joint if stretched past breakForce
    float breakForce = 50.0f;         ///< stretch (length error) at which it breaks
    bool  broken = false;             ///< runtime: set when broken (stops constraining)

    // ---- Hinge (revolute): pins a point and locks rotation to `axis` ----
    Vec3  axis{0, 0, 1};              ///< world hinge axis the body may spin about
    bool  useMotor = false;           ///< drive the spin about the axis toward motorSpeed
    float motorSpeed = 0.0f;          ///< target angular speed about the axis (deg/s); Slider: metres per second along the axis
    float maxMotorTorque = 1000.0f;   ///< torque the motor can apply to reach it (Slider: the most force)
    bool  useLimits = false;          ///< clamp the hinge angle to [minAngle, maxAngle]
    float minAngle = -45.0f;          ///< lower limit (deg, relative to the start angle); Slider: metres of travel from the start position
    float maxAngle = 45.0f;           ///< upper limit (deg, relative to the start angle); Slider: metres of travel from the start position

    // ---- Ball / ConeTwist ----
    float swingLimit1 = 45.0f;        ///< ConeTwist: how far the body may swing about the first axis across `axis` (deg)
    float swingLimit2 = 45.0f;        ///< ConeTwist: ... about the second axis across `axis` (deg)
    bool  useDrive = false;           ///< pull toward driveTarget (a powered joint)
    Vec3  driveTarget{0, 0, 0};       ///< target turn from the start pose, Euler degrees (x = twist about `axis`)
    float driveStiffness = 0.0f;      ///< N*m per radian (0 with a frequency set: use the frequency)
    float driveDamping = 0.0f;        ///< N*m per radian per second
    float driveFrequency = 4.0f;      ///< Hz; > 0 = stiffness and damping from frequency and damping ratio (scales with the limb)
    float driveDampingRatio = 1.0f;   ///< 1 = critically damped
    float driveMaxTorque = 1000.0f;   ///< the most torque the drive may use (N*m)

    // ---- runtime: the partner found by name, remembered until the scene's objects change (not saved) ----
    struct PartnerCache {               ///< (a copy of a joint starts with nothing remembered)
        GameObject*   object = nullptr;
        std::uint64_t version = 0;      ///< Scene::StructureVersion() when `object` was looked up (0 = never)
        PartnerCache() = default;
        PartnerCache(const PartnerCache&) {}
        PartnerCache& operator=(const PartnerCache&) { object = nullptr; version = 0; return *this; }
    } partner;

    // ---- runtime (set by Physics3D on the first solve) ----
    bool  initialized = false;
    Vec3  pinOffset{0, 0, 0};         ///< A_pos - B_pos captured at init (Pin)
    float restLen = 1.0f;             ///< resolved rest length used by the solver
    Vec3  hingeLever{0, 0, 0};        ///< world lever COM_A -> pivot at init (Hinge)
    Quat  refRot{0, 0, 0, 1};         ///< A's orientation at init (Hinge)
};

} // namespace okay
