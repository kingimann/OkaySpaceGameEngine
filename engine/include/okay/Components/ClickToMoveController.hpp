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

private:
    bool m_hasDest = false;
    Vec3 m_dest{0, 0, 0};

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
