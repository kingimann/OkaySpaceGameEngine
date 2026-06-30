#pragma once
// ---------------------------------------------------------------------------
// Joint2D — a positional constraint between this body and either a world anchor or
// another Rigidbody2D. Solved each physics step by Physics2D (impulse + position
// correction). Three modes (all linear — no angular/hinge yet):
//   * Distance — keep the two points a fixed distance apart (a rigid rod / taut rope).
//   * Spring   — a damped Hookean spring pulling toward the rest length (bouncy).
//   * Pin      — weld: lock this body to the anchor/other body at their start offset.
//
// Put it on the dynamic body. Leave `connectedBody` empty to anchor to a fixed world
// point (`anchor`); set it to another object's name to link two bodies. This is the
// 2D twin of Joint3D, kept deliberately field-for-field identical so the same editor
// inspector, serializer and mental model cover both.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec2.hpp"
#include <string>

namespace okay {

class Joint2D : public Component {
public:
    enum class Mode { Distance, Spring, Pin };

    int   mode = (int)Mode::Distance;
    std::string connectedBody;        ///< name of the other body (empty = world anchor)
    Vec2  anchor{0, 0};               ///< world anchor point when there's no connected body
    float distance = 1.0f;            ///< rest length (Distance / Spring)
    bool  autoConfigure = true;       ///< set `distance`/offset from the start separation
    float spring = 30.0f;             ///< Spring stiffness
    float damper = 2.0f;              ///< Spring damping
    bool  breakable = false;          ///< snap the joint if stretched past breakForce
    float breakForce = 50.0f;         ///< stretch (length error) at which it breaks
    bool  broken = false;             ///< runtime: set when broken (stops constraining)

    // ---- runtime (set by Physics2D on the first solve) ----
    bool  initialized = false;
    Vec2  pinOffset{0, 0};            ///< A_pos - B_pos captured at init (Pin)
    float restLen = 1.0f;             ///< resolved rest length used by the solver
};

} // namespace okay
