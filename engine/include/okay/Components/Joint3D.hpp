#pragma once
// ---------------------------------------------------------------------------
// Joint3D — a positional constraint between this body and either a world anchor or
// another Rigidbody3D. Solved each physics step by Physics3D (impulse + position
// correction). Three modes (all linear — no angular/hinge yet):
//   * Distance — keep the two points a fixed distance apart (a rigid rod / taut rope).
//   * Spring   — a damped Hookean spring pulling toward the rest length (bouncy).
//   * Pin      — weld: lock this body to the anchor/other body at their start offset.
//
// Put it on the dynamic body. Leave `connectedBody` empty to anchor to a fixed world
// point (`anchor`); set it to another object's name to link two bodies.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <string>

namespace okay {

class Joint3D : public Component {
public:
    enum class Mode { Distance, Spring, Pin, Hinge };

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
    float motorSpeed = 0.0f;          ///< target angular speed about the axis (deg/s)
    float maxMotorTorque = 1000.0f;   ///< torque the motor can apply to reach it

    // ---- runtime (set by Physics3D on the first solve) ----
    bool  initialized = false;
    Vec3  pinOffset{0, 0, 0};         ///< A_pos - B_pos captured at init (Pin)
    float restLen = 1.0f;             ///< resolved rest length used by the solver
    Vec3  hingeLever{0, 0, 0};        ///< world lever COM_A -> pivot at init (Hinge)
    Quat  refRot{0, 0, 0, 1};         ///< A's orientation at init (Hinge)
};

} // namespace okay
