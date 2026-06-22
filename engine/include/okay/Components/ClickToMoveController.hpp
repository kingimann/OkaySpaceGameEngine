#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/UIAnchor.hpp"     // UICanvas::Width/Height (viewport)
#include "okay/Input/Input.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// RuneScape / Diablo-style point-and-click movement. Click the ground and the
/// character walks there: the click is unprojected through the scene's main
/// Camera onto a horizontal plane at the player's feet, and the body steers toward
/// that point each frame (turning to face the way it travels) until it arrives.
/// Drives a sibling Rigidbody3D's velocity when present (so it collides), else
/// moves the Transform. A Character component (here or on a child) is animated
/// from movement (idle / walk / run).
///
/// Setup: attach to the player; have a perspective main Camera (a high, angled
/// follow camera reads best). No colliders needed for the ground pick — it uses a
/// flat plane — so it works on any flat-ish floor out of the box.
class ClickToMoveController : public Behaviour {
public:
    float walkSpeed   = 4.0f;
    float runSpeed    = 7.0f;
    char  runKey      = 0;          // hold to run (0 = disabled)
    float stopDistance = 0.15f;     // how close counts as "arrived"
    float turnSpeed   = 12.0f;      // how fast the body turns toward travel
    int   mouseButton = 0;          // which button sets the destination (0=left)
    bool  holdToMove  = false;      // hold the button to keep retargeting (else single clicks)
    bool  driveAnimation = true;    // animate a sibling Character from movement
    float groundY     = 0.0f;       // ground plane height when usePlayerHeight is off
    bool  usePlayerHeight = true;   // pick on the plane at the player's current Y

    // ---- Follow camera (RuneScape-style: trails the player, looks at it) ----
    bool  followCamera   = true;    // position the main Camera each frame
    float cameraHeight   = 1.0f;    // look target height above the player origin
    float cameraDistance = 12.0f;   // distance from the player
    float minDistance    = 4.0f, maxDistance = 24.0f;
    float cameraYaw      = 0.0f;    // orbit angle (degrees); rotate with middle-drag / Q,E
    float cameraPitch    = 50.0f;   // downward tilt (degrees)
    float minPitch       = 15.0f, maxPitch = 85.0f;
    float rotateSpeed    = 0.3f;    // degrees per pixel of middle-drag
    float keyRotateSpeed = 90.0f;   // degrees/sec for the rotate keys
    char  rotateLeftKey  = 0;       // e.g. 'q' (0 = disabled)
    char  rotateRightKey = 0;       // e.g. 'e'
    float cameraDamping  = 0.0f;    // 0 = snap; higher = smoother follow

    bool HasDestination() const { return m_hasDest; }
    Vec3 Destination()    const { return m_dest; }
    void ClearDestination() { m_hasDest = false; }
    /// Send the player to a world point directly (for scripts / waypoints).
    void MoveTo(const Vec3& worldPoint) { m_dest = worldPoint; m_hasDest = true; }

    void Update(float dt) override {
        if (!transform || !gameObject || !gameObject->scene()) return;
        Scene& scene = *gameObject->scene();

        // ---- Set a destination from a click on the ground plane ----
        bool clicked = holdToMove ? Input::GetMouseButton(mouseButton)
                                  : Input::GetMouseButtonDown(mouseButton);
        if (clicked) {
            Vec3 hit;
            if (GroundPick(scene, hit)) { m_dest = hit; m_hasDest = true; }
        }
        if (!m_hasDest) { Animate(false, false); StopXZ(); return; }

        // ---- Steer toward the destination on the XZ plane ----
        Vec3 pos = transform->Position();
        Vec3 to{m_dest.x - pos.x, 0.0f, m_dest.z - pos.z};
        float dist = std::sqrt(to.x * to.x + to.z * to.z);
        if (dist <= stopDistance) {                  // arrived
            m_hasDest = false;
            Animate(false, false);
            StopXZ();
            return;
        }
        Vec3 dir{to.x / dist, 0.0f, to.z / dist};
        bool running = runKey && Input::GetKey(runKey);
        float speed = running ? runSpeed : walkSpeed;
        // Don't overshoot on the final step.
        float step = speed * dt;
        bool lastStep = step >= dist;

        auto* rb = gameObject->GetComponent<Rigidbody3D>();
        if (rb) {
            if (lastStep) { rb->velocity.x = 0.0f; rb->velocity.z = 0.0f; transform->SetPosition({m_dest.x, pos.y, m_dest.z}); m_hasDest = false; }
            else { rb->velocity.x = dir.x * speed; rb->velocity.z = dir.z * speed; }
        } else {
            transform->Translate(lastStep ? Vec3{to.x, 0.0f, to.z} : dir * step);
            if (lastStep) m_hasDest = false;
        }

        // Face travel direction (the character mesh faces -Z).
        Quat want = Quat::LookRotation({-dir.x, 0.0f, -dir.z});
        float t = 1.0f - std::exp(-turnSpeed * dt);
        transform->localRotation = Quat::Slerp(transform->localRotation, want, t);

        Animate(true, running);
    }

    // RuneScape-style follow camera: trail the player and keep looking at it.
    // Rotate with a middle-mouse drag (or the rotate keys), zoom with the wheel.
    void LateUpdate(float dt) override {
        if (!followCamera || !transform || !gameObject || !gameObject->scene()) return;
        Scene* sc = gameObject->scene();
        if (!sc->mainCamera || !sc->mainCamera->transform) return;
        Transform* cam = sc->mainCamera->transform;

        // Orbit input: middle-drag rotates yaw/pitch; keys rotate yaw; wheel zooms.
        Vec2 mp = Input::MousePosition();
        if (Input::GetMouseButton(2)) {
            if (m_haveDrag) {
                cameraYaw   += (mp.x - m_lastDrag.x) * rotateSpeed;
                cameraPitch -= (mp.y - m_lastDrag.y) * rotateSpeed;
            }
            m_lastDrag = mp; m_haveDrag = true;
        } else m_haveDrag = false;
        if (rotateLeftKey  && Input::GetKey(rotateLeftKey))  cameraYaw -= keyRotateSpeed * dt;
        if (rotateRightKey && Input::GetKey(rotateRightKey)) cameraYaw += keyRotateSpeed * dt;
        float wheel = Input::MouseWheel();
        if (wheel != 0.0f) cameraDistance = Mathf::Clamp(cameraDistance - wheel, minDistance, maxDistance);
        cameraPitch = Mathf::Clamp(cameraPitch, minPitch, maxPitch);

        Vec3 targetC = transform->Position(); targetC.y += cameraHeight;
        float pr = cameraPitch * Mathf::Deg2Rad;
        Vec3 behind = Quat::Euler(0, cameraYaw, 0) * Vec3{0, 0, 1};
        float cp = Mathf::Cos(pr);
        Vec3 offset{behind.x * cp, Mathf::Sin(pr), behind.z * cp};
        Vec3 desired = targetC + offset * cameraDistance;

        if (cameraDamping > 0.0f && m_haveCamPos) {
            float t = 1.0f - std::exp(-cameraDamping * dt);
            m_camPos = m_camPos + (desired - m_camPos) * t;
        } else m_camPos = desired;
        m_haveCamPos = true;

        // Never let the follow camera dip to or below the floor: from under (or
        // grazing) the ground plane you'd see it edge-on, or — since the floor is a
        // single-sided plane — not at all. Keep a small clearance above it.
        float floorY = usePlayerHeight ? 0.0f : groundY;
        if (m_camPos.y < floorY + 0.5f) m_camPos.y = floorY + 0.5f;

        cam->SetPosition(m_camPos);
        // Level rotation from yaw/pitch (z = 0, no roll) that looks at the player.
        cam->localRotation = Quat::Euler(-cameraPitch, cameraYaw, 0.0f);
    }

private:
    bool m_hasDest = false;
    Vec3 m_dest{0, 0, 0};
    Vec2 m_lastDrag{0, 0};
    bool m_haveDrag = false;
    Vec3 m_camPos{0, 0, 0};
    bool m_haveCamPos = false;

    void StopXZ() {
        if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr) {
            rb->velocity.x = 0.0f; rb->velocity.z = 0.0f;
        }
    }

    void Animate(bool moving, bool running) {
        if (!driveAnimation) return;
        if (Character* ch = FindCharacter())
            ch->anim = moving ? (running ? 3 : 2) : 1;   // run / walk / idle
    }

    Character* FindCharacter() const {
        if (gameObject) if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        if (transform)
            for (Transform* c : transform->Children())
                if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }

    // Unproject the cursor through the main camera onto the ground plane.
    bool GroundPick(Scene& scene, Vec3& out) const {
        Camera* cam = scene.mainCamera;
        if (!cam || !cam->gameObject || !cam->gameObject->transform) return false;
        float w = UICanvas::Width(), h = UICanvas::Height();
        if (w < 1.0f || h < 1.0f) return false;

        Vec2 mp = Input::MousePosition();
        float ndcx = mp.x / w * 2.0f - 1.0f;
        float ndcy = 1.0f - mp.y / h * 2.0f;      // screen Y is top-down

        Mat4 vp = cam->ProjectionMatrix(w / h) * cam->ViewMatrix();
        Mat4 inv = vp.Inverse();
        // Far point at this pixel (depth 1 maps to the far plane in both clip
        // conventions); the ray starts at the camera position, so the near
        // convention doesn't matter.
        Vec4 farH = inv * Vec4{ndcx, ndcy, 1.0f, 1.0f};
        if (Mathf::Abs(farH.w) < 1e-6f) return false;
        Vec3 farW{farH.x / farH.w, farH.y / farH.w, farH.z / farH.w};
        Vec3 ro = cam->gameObject->transform->Position();
        Vec3 rd = (farW - ro).Normalized();
        if (Mathf::Abs(rd.y) < 1e-5f) return false;   // ray parallel to the ground

        float planeY = usePlayerHeight && transform ? transform->Position().y : groundY;
        float t = (planeY - ro.y) / rd.y;
        if (t <= 0.0f) return false;                  // plane is behind the camera
        out = {ro.x + rd.x * t, planeY, ro.z + rd.z * t};
        return true;
    }
};

} // namespace okay
