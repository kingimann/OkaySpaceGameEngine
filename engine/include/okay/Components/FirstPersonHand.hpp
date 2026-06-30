#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — Minecraft-style first-person hand using your CHARACTER'S OWN
// arm. When the Character is a separated-parts rig (Character.separateParts), this
// simply HIDES every part except the right arm, so you see only your real arm and
// never your body — no spawned objects, no baked viewmodel. (If the character isn't
// separated, it falls back to showing the whole body so nothing breaks.)
//
// Click swings the arm (the real Character::Punch, so others see the hit). Put it on
// the player or its FPS camera.
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
    bool  showLeftArm  = false;  ///< show the left arm instead of the right

    void Punch() { if (Character* c = FindCharacter()) c->Punch(); }

    void Start() override { ClearArm(); }

    void Update(float) override {
        if (Game::Paused()) return;
        // First-person arm view is DISABLED for now (parked while we focus elsewhere):
        // the component no longer hides the body or raises an arm — the whole character
        // shows normally. Click still triggers the punch, so attacks keep working.
        ClearArm();
        bool fire = holdToSwing ? Input::GetMouseButton(attackButton)
                                : Input::GetMouseButtonDown(attackButton);
        if (fire) if (Character* c = FindCharacter()) if (!c->Punching()) c->Punch();
    }

    void OnDestroy() override { ClearArm(); }

private:
    // Make sure the first-person arm pose is off and every body part is visible, so a
    // disabled hand never leaves the character half-hidden.
    void ClearArm() {
        if (Character* c = FindCharacter()) {
            c->firstPersonArm = false; c->fpArmBase = -1;
            if (c->PartsBuilt())
                for (int bi = 0; bi < 15; ++bi) if (GameObject* p = c->Part(bi)) p->active = true;
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
