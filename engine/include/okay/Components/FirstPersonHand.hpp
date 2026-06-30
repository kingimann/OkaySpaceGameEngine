#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — shows your character's OWN real arm in first person, the way
// Minecraft/Unturned do it: the body is hidden from YOUR camera only (the camera
// ignores the player subtree, so other cameras / remote players still see your whole
// character), and the right arm is flagged as the first-person viewmodel so it keeps
// rendering for you. It's literally your character's customised arm — its shape, skin,
// sleeve and glove — raised into view. Nothing is spawned and nothing is duplicated,
// so there is never a "second arm" or a "second character".
//
// Needs a Character; if it isn't a separated part rig yet, the hand builds the rig
// (that's what carries the real arm geometry). Click swings the arm (the real
// Character::Punch, so others see the hit). Put this on the FPS camera (or the player).
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Character.hpp"
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
    // Raise the character's own real arm and hide the rest of the body from the OWNER's
    // camera. The body is hidden by the camera ignore (subtree-aware) + the baked mesh
    // staying disabled — never deactivated, never duplicated. The arm parts are flagged
    // firstPersonViewmodel so they render for the owner even inside the ignored subtree.
    void Apply() {
        Character* c = FindCharacter();
        if (!c) return;
        if (!c->separateParts) c->separateParts = true;   // the real arm lives on the part rig
        if (!c->PartsBuilt()) c->BuildParts();
        if (c->PartsBuilt()) {
            // The rig's 180° facing flip puts the body's screen-right arm on the L-named
            // bones (3-5). Flag that arm; everything else stays active (so remote views
            // are unaffected) but is culled from the owner's camera.
            int a0 = showLeftArm ? 6 : 3;
            for (int bi = 0; bi < 15; ++bi)
                if (GameObject* p = c->Part(bi)) {
                    p->active = true;
                    p->firstPersonViewmodel = (bi >= a0 && bi <= a0 + 2);
                }
            c->firstPersonArm = true; c->fpArmBase = a0;   // raise it + aim the punch here
            c->fpArmBob = bobbing;
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
            if (c->PartsBuilt())
                for (int bi = 0; bi < 15; ++bi)
                    if (GameObject* p = c->Part(bi)) { p->active = true; p->firstPersonViewmodel = false; }
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
