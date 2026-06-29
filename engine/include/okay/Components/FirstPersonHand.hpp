#pragma once
// ---------------------------------------------------------------------------
// FirstPersonHand — a Minecraft-style first-person hand using your CHARACTER'S OWN
// arm. It does NOT spawn anything: while it's active the Character renders only its
// arm in its own existing mesh, so you see just your arm (real geometry + colours)
// and never your body — even looking straight down — with no extra objects in the
// scene.
//
// Empty-handed the arm rests low; equip a weapon/item (holdingItem / SetHolding) to
// raise it. Click swings it (the real Character::Punch, so others see the hit). Put
// it on the player or its FPS camera; tune Hand Raise / Elbow Bend live in Play.
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

    bool  holdingItem  = false;  ///< empty = arm rests low; true raises it
    void  SetHolding(bool h) { holdingItem = h; }

    float handRaise = -96.0f;    ///< how far forward/up the arm reaches (more negative = higher)
    float elbowBend = 16.0f;     ///< forearm bend — the fist's height/closeness
    float raiseSpeed = 10.0f;    ///< ease speed between lowered and raised

    void Punch() { if (Character* c = FindCharacter()) c->Punch(); }

    void Start() override { Apply(); }

    void Update(float dt) override {
        if (Game::Paused()) return;
        Apply();
        float k = raiseSpeed > 0.0f ? (1.0f - std::exp(-raiseSpeed * dt)) : 1.0f;
        m_raise += ((holdingItem ? 1.0f : 0.0f) - m_raise) * k;
        Character* c = FindCharacter();
        if (c) c->fpArmUp = m_raise;
        bool fire = holdToSwing ? Input::GetMouseButton(attackButton)
                                : Input::GetMouseButtonDown(attackButton);
        if (fire && c && !c->Punching()) c->Punch();
    }

    void OnDestroy() override {
        if (Character* c = FindCharacter()) c->firstPersonArm = false;
    }

private:
    float m_raise = 0.0f;

    void Apply() {
        // Show the player to its own camera (so the arm-only mesh renders) and tell
        // the Character to draw just the first-person arm with our tuning.
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
