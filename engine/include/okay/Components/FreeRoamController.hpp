#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/Cursor.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// A free-roam "fly" camera controller — drop it on a Camera (or any object) to fly
/// around the scene: WASD moves along the view direction, the up/down keys (Space /
/// Ctrl, or E / Q) raise and lower, the mouse looks, and holding the sprint key
/// boosts the speed. Great for spectating, debugging or a no-clip creative mode.
///
/// Looking: by default you hold the right mouse button to look (so the cursor is
/// free the rest of the time, like a scene-view fly-cam). Set lockCursor = true to
/// capture the cursor and always look (an FPS spectator).
class FreeRoamController : public Behaviour {
public:
    float moveSpeed = 8.0f;             // units/sec
    float boostMultiplier = 3.0f;       // hold sprint to fly faster
    char  sprintKey = Input::KeyShift;
    char  upKey   = ' ';                // rise (Space)
    char  downKey = 'c';                // descend ('c'; Ctrl also descends)
    float mouseSensitivity = 0.15f;     // degrees per pixel
    bool  invertY = false;
    bool  lockCursor = false;           // capture the cursor and always look
    bool  lookRequiresRightMouse = true;// otherwise hold RMB to look
    float minPitch = -89.0f, maxPitch = 89.0f;
    float acceleration = 12.0f;         // smoothing toward the target velocity (0 = instant)

    float yaw = 0.0f, pitch = 0.0f;     // look angles (degrees)

    void Start() override {
        // Seed the angles from the object's current rotation so it doesn't snap.
        if (transform) {
            Vec3 f = transform->localRotation * Vec3{0, 0, -1};
            yaw   = Mathf::Atan2(f.x, -f.z) * Mathf::Rad2Deg;
            pitch = Mathf::Atan2(f.y, std::sqrt(f.x * f.x + f.z * f.z)) * Mathf::Rad2Deg;
        }
        if (lockCursor) Cursor::Capture(true);
    }

    void Update(float dt) override {
        if (!transform) return;

        // ---- Look ----
        bool looking = lockCursor || !lookRequiresRightMouse || Input::GetMouseButton(1);
        Vec2 mp = Input::MousePosition();
        if (looking && m_haveMouse) {
            yaw   += (mp.x - m_lastMouse.x) * mouseSensitivity;
            pitch += (invertY ? 1.0f : -1.0f) * (mp.y - m_lastMouse.y) * mouseSensitivity;
            pitch  = Mathf::Clamp(pitch, minPitch, maxPitch);
        }
        m_lastMouse = mp; m_haveMouse = true;
        transform->localRotation = Quat::Euler(pitch, yaw, 0.0f);

        // ---- Move (full 3D, relative to where we look) ----
        Quat rot = transform->localRotation;
        Vec3 fwd = rot * Vec3{0, 0, -1};
        Vec3 right = rot * Vec3::Right;
        Vec2 axis = Input::AxisWASD();
        Vec2 pad  = Input::GamepadAxis();
        if (Mathf::Abs(pad.x) + Mathf::Abs(pad.y) > 0.15f) axis = pad;

        float upDown = 0.0f;
        if (upKey   && Input::GetKey(upKey))   upDown += 1.0f;
        if (downKey && Input::GetKey(downKey)) upDown -= 1.0f;
        if (Input::GetKey(Input::KeyCtrl))     upDown -= 1.0f;   // Ctrl always descends

        Vec3 dir = fwd * axis.y + right * axis.x + Vec3{0, 1, 0} * upDown;
        float len = dir.Magnitude();
        if (len > 1e-4f) dir = dir * (1.0f / len);

        float speed = moveSpeed;
        if (sprintKey && Input::GetKey(sprintKey)) speed *= boostMultiplier;

        // Ease toward the target velocity for a smooth, weighty glide.
        Vec3 target = dir * speed;
        if (acceleration > 0.0f) {
            float t = 1.0f - std::exp(-acceleration * dt);
            m_vel = m_vel + (target - m_vel) * t;
        } else {
            m_vel = target;
        }
        transform->Translate(m_vel * dt);
    }

private:
    Vec2 m_lastMouse{0, 0};
    bool m_haveMouse = false;
    Vec3 m_vel{0, 0, 0};
};

} // namespace okay
