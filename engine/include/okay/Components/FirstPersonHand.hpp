#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — first-person arm done the way Minecraft and Unturned do it.
//
// THE MODEL (default): your character's BODY is hidden from your OWN camera only
// (the camera ignores the player subtree — every other camera / remote player still
// sees your whole body), and a dedicated ARM VIEWMODEL is attached to the camera and
// shown only to you. The viewmodel is a separate object — NOT the character rig — so
// it never duplicates the body and works with or without a separated-parts rig. This
// is exactly Minecraft's "hand" and Unturned's viewmodel layer.
//
// A separate path (viewmodelArm = false) instead raises the character's OWN separated
// rig arm — kept for people who want their real customised arm in view.
//
// Click swings the viewmodel forward AND calls Character::Punch (so the real body
// throws a hit other players see). Put this on the FPS camera (or the player).
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Game.hpp"
#include <cmath>

namespace okay {

class FirstPersonHand : public Behaviour {
public:
    int   attackButton = 0;        ///< mouse button that swings (0 = left)
    bool  holdToSwing  = true;     ///< keep swinging while held (Minecraft mining)
    bool  showLeftArm  = false;    ///< false = right arm (default), true = left

    /// DEFAULT (Minecraft/Unturned): a dedicated arm VIEWMODEL attached to the camera,
    /// visible only to you; your body is hidden from your own camera but seen by others.
    /// No rig required, and it can never spawn a "second character". Turn OFF to instead
    /// raise the separated rig's real arm into view (advanced; needs separateParts).
    bool  viewmodelArm = true;

    // ---- Viewmodel arm look (debug / styling knobs) ----
    bool  matchSkin   = true;                            ///< colour the arm from the Character's skin
    Color armColor    = Color::FromBytes(220, 176, 150); ///< used when matchSkin is off
    float armWidth    = 0.13f;     ///< arm thickness (x & z)
    float armLength   = 0.55f;     ///< arm length (y)
    float armReach    = 0.62f;     ///< distance in front of the camera
    float armSpread   = 0.34f;     ///< distance to the side (mirrored per arm)
    float armDrop     = 0.34f;     ///< distance below the view centre
    float armPitch    = -22.0f;    ///< tilt up into frame (degrees)
    float armYaw      = 16.0f;     ///< angle inward (degrees, mirrored per arm)
    float swingAmount = 1.0f;      ///< punch swing scale (0 = arm stays still)

    void Punch() {
        if (m_swing < 0.0f || m_swing >= 1.0f) m_swing = 0.0f;   // start a viewmodel swing
        if (Character* c = FindCharacter()) c->Punch();          // real hit, so others see it
    }

    void Start() override { Apply(); }

    void Update(float dt) override {
        if (Game::Paused()) return;
        // Advance the viewmodel swing on its own clock (matches the character's punch).
        if (m_swing >= 0.0f && m_swing < 1.0f) {
            float dur = 0.28f;
            if (Character* c = FindCharacter()) if (c->punchDuration > 1e-3f) dur = c->punchDuration;
            m_swing += dt / dur;
            if (m_swing > 1.0f) m_swing = 1.0f;
        }
        Apply();
        bool fire = holdToSwing ? Input::GetMouseButton(attackButton)
                                : Input::GetMouseButtonDown(attackButton);
        if (fire && !(m_swing >= 0.0f && m_swing < 1.0f)) Punch();
    }

    void OnDestroy() override { Teardown(); }

private:
    GameObject* m_arm   = nullptr;   ///< the spawned viewmodel (child of the camera)
    float       m_swing = -1.0f;     ///< punch swing 0..1 (<0 = idle)

    void Apply() {
        Character* c = FindCharacter();
        FirstPersonController* fpc = FindController();

        if (!viewmodelArm && c && c->separateParts) {
            // Advanced path: raise the character's OWN separated-rig arm as the viewmodel.
            DestroyViewmodel();
            if (!c->PartsBuilt()) c->BuildParts();
            if (c->PartsBuilt()) {
                // The rig's 180° facing flip puts the body's screen-right arm on the
                // L-named bones (3-5). Flag that arm as the viewmodel; nothing is
                // deactivated, so remote views still see the full body.
                int a0 = showLeftArm ? 6 : 3;
                for (int bi = 0; bi < 15; ++bi)
                    if (GameObject* p = c->Part(bi)) {
                        p->active = true;
                        p->firstPersonViewmodel = (bi >= a0 && bi <= a0 + 2);
                    }
                c->firstPersonArm = true; c->fpArmBase = a0;
            }
            if (fpc) { fpc->showBody = false; fpc->ApplyBodyVisibility(); }  // hide body from owner only
            return;
        }

        // DEFAULT Minecraft/Unturned way: standalone viewmodel arm, body hidden from the
        // owner's own camera (never deactivated — others see the whole character).
        if (c) { c->firstPersonArm = false; c->fpArmBase = -1; }
        if (fpc) { fpc->showBody = false; fpc->ApplyBodyVisibility(); }
        EnsureViewmodel(c);
    }

    void EnsureViewmodel(Character* c) {
        Transform* camT = CameraTransform();
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!camT || !s) return;
        if (!m_arm) {
            for (Transform* ch : camT->Children())
                if (ch && ch->gameObject && ch->gameObject->name == "FP_Arm") { m_arm = ch->gameObject; break; }
            if (!m_arm) {
                m_arm = s->CreateGameObject("FP_Arm");
                m_arm->transform->SetParent(camT, false);
                auto* mr = m_arm->AddComponent<MeshRenderer>();
                mr->mesh = Mesh::Cube();       // a blocky limb, like Minecraft's arm box
                mr->doubleSided = true;
            }
        }
        m_arm->active = true;
        m_arm->firstPersonViewmodel = true;    // render for the owner even though the body is ignored

        Color col = (matchSkin && c) ? c->skin : armColor;
        if (auto* mr = m_arm->GetComponent<MeshRenderer>()) mr->color = col;

        float side = showLeftArm ? -1.0f : 1.0f;
        float arc  = (m_swing >= 0.0f && m_swing < 1.0f) ? std::sin(m_swing * 3.14159265f) : 0.0f;
        // Camera space: -Z forward, +X right, +Y up. Place the arm in the lower corner,
        // angled up into frame; a punch jabs it forward (-Z) and rotates it down.
        m_arm->transform->localPosition = {
            std::fabs(armSpread) * side,
            -std::fabs(armDrop) + arc * 0.10f * swingAmount,
            -std::fabs(armReach) - arc * 0.22f * swingAmount
        };
        m_arm->transform->localRotation = Quat::Euler(
            armPitch - arc * 55.0f * swingAmount,
            std::fabs(armYaw) * side,
            0.0f);
        m_arm->transform->localScale = { std::fabs(armWidth), std::fabs(armLength), std::fabs(armWidth) };
    }

    void DestroyViewmodel() {
        if (m_arm) {
            if (Scene* s = gameObject ? gameObject->scene() : nullptr) s->Destroy(m_arm);
            m_arm = nullptr;
        }
    }

    // Remove the viewmodel + clear any rig-arm flags (when the hand is removed).
    void Teardown() {
        DestroyViewmodel();
        if (Character* c = FindCharacter()) {
            c->firstPersonArm = false; c->fpArmBase = -1;
            if (c->PartsBuilt())
                for (int bi = 0; bi < 15; ++bi)
                    if (GameObject* p = c->Part(bi)) { p->active = true; p->firstPersonViewmodel = false; }
        }
    }

    // The Transform to hang the viewmodel from: this object's camera if it has one,
    // else the camera somewhere under the controller's player (template puts the hand
    // on the camera, so the common case is just `transform`).
    Transform* CameraTransform() const {
        if (gameObject && gameObject->GetComponent<Camera>()) return transform;
        if (FirstPersonController* fpc = FindController())
            if (fpc->gameObject && fpc->gameObject->transform)
                if (Transform* f = FindCameraIn(fpc->gameObject->transform)) return f;
        return transform;
    }
    static Transform* FindCameraIn(Transform* t) {
        if (!t) return nullptr;
        if (t->gameObject && t->gameObject->GetComponent<Camera>()) return t;
        for (Transform* ch : t->Children()) if (Transform* f = FindCameraIn(ch)) return f;
        return nullptr;
    }
    Character* FindCharacter() const {
        for (Transform* t = transform; t; t = t->Parent())
            if (t->gameObject) if (auto* c = t->gameObject->GetComponent<Character>()) return c;
        return nullptr;
    }
    FirstPersonController* FindController() const {
        for (Transform* t = transform; t; t = t->Parent())
            if (t->gameObject) if (auto* c = t->gameObject->GetComponent<FirstPersonController>()) return c;
        return nullptr;
    }
};

} // namespace okay
