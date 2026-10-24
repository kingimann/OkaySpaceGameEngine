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
};

} // namespace okay
