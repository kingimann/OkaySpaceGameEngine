#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — makes a punch use the player's OWN character arm (no separate
// floating viewmodel). It reveals the Character's body to the first-person camera
// (the head sits inside the near clip plane, so you see your arms/torso, not your
// own head) and, on click, swings the character's real right arm via Character::Punch().
//
// Put it on the player or its first-person camera; it finds the Character +
// FirstPersonController up the hierarchy. Pair with a digger / interaction script
// for the actual hit — this drives the animation, your character does the rest.
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
    int   attackButton = 0;      ///< mouse button that punches (0 = left)
    bool  holdToSwing  = true;   ///< keep swinging while held (Minecraft mining)
    bool  showBody     = true;   ///< reveal your character so you see your own arm

    /// Trigger a punch from script / other components.
    void Punch() { if (Character* c = FindCharacter()) c->Punch(); }

    void Start() override { Apply(); }

    void Update(float) override {
        if (Game::Paused()) return;
        Apply();
        bool fire = holdToSwing ? Input::GetMouseButton(attackButton)
                                : Input::GetMouseButtonDown(attackButton);
        if (fire) if (Character* c = FindCharacter()) if (!c->Punching()) c->Punch();
    }

private:
    // Show the body so the camera renders your character's arm in first person.
    void Apply() {
        if (auto* fpc = FindController()) { fpc->showBody = showBody; fpc->ApplyBodyVisibility(); }
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
