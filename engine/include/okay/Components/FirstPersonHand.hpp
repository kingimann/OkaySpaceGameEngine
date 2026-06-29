#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — a Minecraft-style first-person hand that uses your CHARACTER'S
// OWN arm (no separate viewmodel, no extra arm). It freezes the body so walking
// never shows your torso/legs, and raises your real arm into the first-person
// camera. Empty-handed the arm rests low; equip a weapon/item (holdingItem /
// SetHolding(true)) to raise it. Click swings it via the real Character::Punch(),
// so third-person + networked players see the hit too.
//
// Put it on the player or its FPS camera; it finds the Character +
// FirstPersonController up the hierarchy. Tune where the hand sits with Hand Raise
// / Elbow Bend — no need to come back and forth.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Game.hpp"
#include <cmath>

namespace okay {

class FirstPersonHand : public Behaviour {
public:
    int   attackButton = 0;      ///< mouse button that swings (0 = left)
    bool  holdToSwing  = true;   ///< keep swinging while held (Minecraft mining)

    /// Holding a weapon/item? Empty-handed the arm rests low; true raises it up.
    bool  holdingItem  = false;
    void  SetHolding(bool h) { holdingItem = h; }

    // Tune where the hand sits (so you can dial it in without going back and forth).
    float handRaise = -96.0f;    ///< how far forward/up the arm reaches (more negative = higher)
    float elbowBend = 16.0f;     ///< forearm bend — controls the fist's height/closeness
    float raiseSpeed = 10.0f;    ///< how fast the hand eases between lowered and raised

    void Punch() { if (Character* c = FindCharacter()) c->Punch(); }

    void Start() override { Apply(); }

    void Update(float dt) override {
        if (Game::Paused()) return;
        Apply();
        float k = raiseSpeed > 0.0f ? (1.0f - std::exp(-raiseSpeed * dt)) : 1.0f;
        m_raise += ((holdingItem ? 1.0f : 0.0f) - m_raise) * k;
        if (Character* c = FindCharacter()) c->fpArmUp = m_raise;
        bool fire = holdToSwing ? Input::GetMouseButton(attackButton)
                                : Input::GetMouseButtonDown(attackButton);
        if (fire) if (Character* c = FindCharacter()) if (!c->Punching()) c->Punch();
    }

    void OnDestroy() override {
        if (Character* c = FindCharacter()) c->firstPersonArm = false;
    }

private:
    float m_raise = 0.0f;   ///< eased 0 (lowered) .. 1 (raised)

    // Show the body to the first-person camera (so its arm renders) and tell the
    // Character to run the first-person arm pose with our tuning.
    void Apply() {
        if (auto* fpc = FindController()) { fpc->showBody = true; fpc->ApplyBodyVisibility(); }
        if (Character* c = FindCharacter()) {
            c->firstPersonArm = true;
            c->fpRaise = handRaise;
            c->fpElbow = elbowBend;
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
