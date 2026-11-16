#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"

namespace okay {

/// Physics debug view switches, shared by every physics world (the editor's Play
/// scene is a fresh copy, so the toggles can't live on one world). Off by default:
/// with Collect() off the physics step records nothing, so it costs nothing.
struct PhysicsDebug {
    /// Record this frame's contacts (collisions + triggers) for the overlay.
    static bool& Collect()    { static bool on = false; return on; }
    /// Also record rays / shape casts made by scripts, visual scripts and Actions.
    static bool& ScriptRays() { static bool on = true; return on; }
    /// Cap per frame so a script casting thousands of rays can't flood the overlay.
    static constexpr int kMaxRays = 512;
    static constexpr int kMaxContacts = 512;
    /// Cap per frame on the bodies and joints the overlay draws.
    static constexpr int kMaxBodies = 2000;
};

/// One query ray for the debug overlay. `radius` > 0 for sphere-style casts.
struct PhysicsDebugRay3D {
    Vec3 from{0, 0, 0}, to{0, 0, 0};
    bool hit = false;
    Vec3 point{0, 0, 0}, normal{0, 0, 0};
    float radius = 0.0f;
};
/// One contact recorded during the physics step.
struct PhysicsDebugContact3D {
    Vec3 point{0, 0, 0}, normal{0, 0, 0};
    bool trigger = false;
    float impulse = 0.0f;     // Improved quality: how hard the solver pushed at this point this step (0 = not measured)
};
/// One body for the debug overlay (a box around its collider; sleeping bodies are drawn in another colour).
struct PhysicsDebugBody3D {
    Vec3 center{0, 0, 0}, half{0, 0, 0};
    bool sleeping = false;
};
/// One joint for the debug overlay: the line between the two ends it holds together.
struct PhysicsDebugJoint3D {
    Vec3 a{0, 0, 0}, b{0, 0, 0};
    bool broken = false;
};
struct PhysicsDebugRay2D {
    Vec2 from{0, 0}, to{0, 0};
    bool hit = false;
    Vec2 point{0, 0}, normal{0, 0};
    float radius = 0.0f;
};
struct PhysicsDebugContact2D {
    Vec2 point{0, 0}, normal{0, 0};
    bool trigger = false;
    float impulse = 0.0f;     // Improved quality: how hard the solver pushed at this point this step (0 = not measured)
};
struct PhysicsDebugBody2D {
    Vec2 center{0, 0}, half{0, 0};
    bool sleeping = false;
};
struct PhysicsDebugJoint2D {
    Vec2 a{0, 0}, b{0, 0};
    bool broken = false;
};

} // namespace okay
