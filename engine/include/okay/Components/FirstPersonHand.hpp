#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — a Minecraft-style first-person hand that uses your CHARACTER'S
// OWN right arm. It reveals the character to the first-person camera and tells the
// Character to hold its real right arm up into view (Character::firstPersonArm), so
// you see your actual arm — NOT an extra/added arm. Click to swing it (the real
// Character::Punch(), which also shows to third-person and networked players).
//
// Put it on the player or its FPS camera; it finds the Character +
// FirstPersonController up the hierarchy.
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

    void OnDestroy() override {
        if (Character* c = FindCharacter()) c->firstPersonArm = false;
    }

private:
    // Show the body to the first-person camera and raise the character's own right
    // arm into view — so you see YOUR arm, not an added one.
    void Apply() {
        if (auto* fpc = FindController()) { fpc->showBody = true; fpc->ApplyBodyVisibility(); }
        if (Character* c = FindCharacter()) c->firstPersonArm = true;
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
