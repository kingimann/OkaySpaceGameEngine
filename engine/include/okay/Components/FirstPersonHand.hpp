#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — a Minecraft-style first-person "view model": the player's own
// arm/fist drawn in the bottom corner of the screen, with a quick punch/swing arc
// when you attack (left mouse by default). Purely cosmetic — it doesn't dig or hit
// anything itself; pair it with a digger / interaction script for the actual effect.
//
// Drop it on the first-person CAMERA (or on the player — it finds the child camera)
// and it spawns a "Hand" mesh parented to the camera, so it rides the view. The arm
// is a simple skin-coloured box; tune the rest pose, size and swing in the inspector.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>

namespace okay {

class FirstPersonHand : public Behaviour {
public:
    Color skinColor   = Color::FromBytes(234, 188, 150);   ///< Steve-ish skin tone
    Color sleeveColor = Color::FromBytes(80, 150, 120);    ///< cuff/sleeve accent at the elbow
    bool  leftHanded  = false;                             ///< mirror the arm to the left side

    // Rest pose (camera-local; camera looks down -Z, so forward is negative Z).
    Vec3  restPosition{0.42f, -0.40f, -0.70f};
    Vec3  restEuler{-18.0f, 12.0f, 8.0f};                  ///< degrees
    Vec3  armSize{0.17f, 0.17f, 0.52f};                    ///< forearm box (long along Z)

    // Punch / swing.
    int   attackButton  = 0;            ///< mouse button that swings (0 = left)
    bool  holdToSwing   = true;         ///< keep swinging while held (Minecraft mining)
    float swingDuration = 0.26f;        ///< seconds for one out-and-back arc
    float punchPitch    = 70.0f;        ///< how far the fist pitches forward (deg)
    float punchYaw      = 20.0f;        ///< inward yaw during the swing (deg)
    float lungeForward  = 0.14f;        ///< how far the hand lunges forward (units)
    float idleBob       = 0.012f;       ///< gentle breathing bob amount (0 = off)

    /// Trigger a swing from script / other components (e.g. when a tool is used).
    void Punch() { if (m_t >= 1.0f) { m_t = 0.0f; m_swinging = true; } }
    bool Swinging() const { return m_swinging; }

    void Start() override { Build(); }

    void OnDestroy() override {
        if (m_hand) if (Scene* s = GetScene()) s->Destroy(m_hand);
        m_hand = nullptr;
    }

    void Update(float) override {
        if (!m_hand) { Build(); if (!m_hand) return; }
        float dt = Time::DeltaTime();
        if (Game::Paused()) { dt = 0.0f; }

        // Start / repeat the swing on the attack button.
        if (!Game::Paused()) {
            bool fire = holdToSwing ? Input::GetMouseButton(attackButton)
                                    : Input::GetMouseButtonDown(attackButton);
            if (fire && m_t >= 1.0f) { m_t = 0.0f; m_swinging = true; }
        }

        // Advance the swing timer (0 -> 1) and shape it into an out-and-back arc.
        if (m_swinging) {
            m_t += (swingDuration > 1e-3f ? dt / swingDuration : 1.0f);
            if (m_t >= 1.0f) { m_t = 1.0f; m_swinging = false; }
        }
        float arc = m_swinging ? std::sin(m_t * 3.14159265f) : 0.0f;   // 0..1..0

        m_idle += dt;
        float bob = idleBob * std::sin(m_idle * 1.8f);

        float side = leftHanded ? -1.0f : 1.0f;
        Transform* t = m_hand->transform;
        // Position: rest + a forward/down lunge during the punch + idle bob.
        Vec3 pos = restPosition;
        pos.x *= side;
        pos.y += bob;
        pos.z -= lungeForward * arc;
        pos.y -= 0.06f * arc;
        t->localPosition = pos;
        // Rotation: rest + a strong pitch/yaw swing (mirrored for the left hand).
        Vec3 e = restEuler;
        e.y *= side; e.z *= side;
        e.x -= punchPitch * arc;
        e.y += punchYaw * arc * side;
        t->localRotation = Quat::Euler(e);
    }

private:
    GameObject* m_hand   = nullptr;
    float       m_t      = 1.0f;     // swing progress (1 = idle/done)
    bool        m_swinging = false;
    float       m_idle   = 0.0f;

    Transform* CameraTransform() const {
        if (!gameObject) return nullptr;
        if (gameObject->GetComponent<Camera>()) return gameObject->transform;   // we're on the camera
        if (transform)
            for (Transform* c : transform->Children())
                if (c && c->gameObject && c->gameObject->GetComponent<Camera>()) return c;
        return nullptr;
    }

    void Build() {
        if (m_hand) return;
        Scene* s = GetScene();
        Transform* cam = CameraTransform();
        if (!s || !cam) return;

        // Pivot that rides the camera and carries the swing animation.
        m_hand = s->CreateGameObject("Hand");
        m_hand->transform->SetParent(cam, false);

        // Forearm: a long skin box. Drawn unlit-ish bright so it reads clearly in
        // dim caves, but still the scene's colour so it doesn't glow.
        GameObject* arm = s->CreateGameObject("Forearm");
        arm->transform->SetParent(m_hand->transform, false);
        auto* amr = arm->AddComponent<MeshRenderer>();
        amr->mesh = Mesh::Cube(1.0f);
        amr->color = skinColor;
        arm->transform->localScale = armSize;
        arm->transform->localPosition = {0.0f, 0.0f, 0.0f};

        // Fist: a chunkier box at the far (forward) end of the forearm.
        GameObject* fist = s->CreateGameObject("Fist");
        fist->transform->SetParent(m_hand->transform, false);
        auto* fmr = fist->AddComponent<MeshRenderer>();
        fmr->mesh = Mesh::Cube(1.0f);
        fmr->color = skinColor;
        fist->transform->localScale = {armSize.x * 1.35f, armSize.y * 1.35f, armSize.x * 1.35f};
        fist->transform->localPosition = {0.0f, 0.0f, -armSize.z * 0.5f - armSize.x * 0.6f};

        // Sleeve cuff: a thin band at the near (elbow) end.
        GameObject* cuff = s->CreateGameObject("Sleeve");
        cuff->transform->SetParent(m_hand->transform, false);
        auto* cmr = cuff->AddComponent<MeshRenderer>();
        cmr->mesh = Mesh::Cube(1.0f);
        cmr->color = sleeveColor;
        cuff->transform->localScale = {armSize.x * 1.15f, armSize.y * 1.15f, armSize.z * 0.28f};
        cuff->transform->localPosition = {0.0f, 0.0f, armSize.z * 0.42f};
    }
};

} // namespace okay
