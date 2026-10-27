#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — shows your character's OWN body in first person. The camera
// ignores the player subtree, so other cameras and shadows still see the whole
// character. The shirt and the legs are flagged as the viewmodel. The head and
// the neck stay active but unflagged, so only this camera skips them. The arm
// is a copy that only this camera draws, entering from the lower-right corner
// the way a blocky first-person arm does. The real arm bones stay on the
// animation, so another camera never sees that pose.
//
// Needs a Character; if it isn't a separated part rig yet, the hand builds the rig
// (that's what carries the real arm geometry). Click swings the arm (the real
// Character::Punch on the right arm, so others see the hit, and the copy swings
// too). Put this on the FPS camera (or the player).
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Game.hpp"

namespace okay {

class FirstPersonHand : public Behaviour {
public:
    int   attackButton = 0;      ///< mouse button that swings (0 = left)
    bool  holdToSwing  = true;   ///< keep swinging while held (Minecraft mining)
    bool  showLeftArm  = false;  ///< false = right arm (default), true = left
    bool  followPitch  = true;   ///< arm tilts up/down with the camera look (Minecraft-style)
    bool  bobbing      = true;   ///< let the walk/run cycle bob the arm (off = rock-steady)
    bool  steadyArm    = false;  ///< fully decouple from the body: ignore lean / crouch / prone
    float armRaise     = -96.0f; ///< manual: upper-arm angle (how high the hand sits)
    float armElbow     = 16.0f;  ///< manual: forearm bend (hand closeness/height)

    void Punch() { if (Character* c = FindCharacter()) c->Punch(); }

    void Start() override { Apply(); }

    void Update(float) override {
        if (Game::Paused()) return;
        Apply();
        bool fire = holdToSwing ? Input::GetMouseButton(attackButton)
                                : Input::GetMouseButtonDown(attackButton);
        if (fire) if (Character* c = FindCharacter()) if (!c->Punching()) c->Punch();
    }

    void OnDestroy() override { ClearArm(); }

private:
    // Show the body to the owner, except the head and the neck. Those stay
    // active so they cast shadows and other cameras still see them; this camera
    // skips them because they are not the viewmodel. A run also drops both arms.
    void Apply() {
        Character* c = FindCharacter();
        if (!c) return;
        if (!c->separateParts) c->separateParts = true;   // the real arm lives on the part rig
        if (!c->PartsBuilt()) c->BuildParts();
        if (c->PartsBuilt()) {
            // The rig's 180° facing flip puts the body's screen-right arm on the L-named
            // bones (3-5). That is the arm raised into the view. The rest of the body
            // (except head and neck) is drawn too, solid, for when you look down.
            int a0 = showLeftArm ? 6 : 3;
            c->ApplyOwnerVisibility(true);
            c->fpArmUp = c->HideFirstPersonHands() ? 0.0f : 1.0f;
            c->firstPersonArm = true; c->fpArmBase = a0;   // raise it + aim the punch here
            c->fpArmBob = bobbing;
            c->fpSteady = steadyArm;
            c->fpRaise  = armRaise;    // manual tuning
            c->fpElbow  = armElbow;
            // The chest is about 0.3 m deep. The default near plane slices it.
            if (Camera* cam = gameObject ? gameObject->GetComponent<Camera>() : nullptr)
                if (cam->nearClip > 0.06f) cam->nearClip = 0.04f;
        }
        if (auto* fpc = FindController()) {
            c->fpPitch = followPitch ? fpc->pitch : 0.0f;   // arm follows the camera up/down
            fpc->showBody = false; fpc->ApplyBodyVisibility();
        }
    }

    // Clear the first-person pose + viewmodel flags (when the hand is removed).
    void ClearArm() {
        if (Character* c = FindCharacter()) {
            c->firstPersonArm = false; c->fpArmBase = -1;
            if (c->PartsBuilt()) c->ApplyOwnerVisibility(false);
        }
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
