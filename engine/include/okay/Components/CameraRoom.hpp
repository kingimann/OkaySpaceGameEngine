#pragma once
// A room for a 2D camera: a box the camera is held inside while the target is in
// it. Put one on an empty object per room (position = the middle, Size = how big),
// turn "Use Rooms" on in the Virtual Camera, and the view slides from room to room
// as the player crosses over. Written from the box-containment rule in
// CameraMath.hpp (PickRoom); see docs/gameplay/camera_upgrade.md.
#include "okay/Components/CameraMath.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

class CameraRoom : public Behaviour {
public:
    /// Width and height of the room in world units.
    Vec2 size{20.0f, 12.0f};

    /// The room's box on the XY plane.
    cam::Box2 Bounds() const {
        const Vec3 p = transform ? transform->Position() : Vec3{0, 0, 0};
        cam::Box2 b;
        const float hx = std::max(0.01f, size.x * 0.5f), hy = std::max(0.01f, size.y * 0.5f);
        b.minX = p.x - hx; b.maxX = p.x + hx; b.minY = p.y - hy; b.maxY = p.y + hy;
        return b;
    }
};

} // namespace okay
