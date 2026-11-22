#pragma once
// Interactable: put it on anything the player may use (a lever, a switch, a sign). InteractionSensor: put it on the
// player. The sensor finds the nearest thing that can be used, through the interaction contract (okay/Modules/Interact.hpp),
// so it also shows the prompt of a pickup, a chest, a talker or a seat, and uses an Interactable when the key is pressed
// (or held for `holdSeconds`). Written for this repository.
#include "okay/Modules/Interact.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include <string>

namespace okay {

class Interactable : public Behaviour {
public:
    float range = 2.0f;            ///< metres from the user
    std::string prompt = "Use";    ///< shown as "E to <prompt>"
    float holdSeconds = 0.0f;      ///< 0 = a tap; more = hold the key this long
    bool  oneShot = false;         ///< can be used once
    char  key = 'e';               ///< 'e' = the Interact input action (rebindable); 0 = not usable
    int   useCount = 0;            ///< runtime: how many times it was used

    /// Use it now (what the sensor calls when the key press or hold completes). Sends the script event `on_interact`
    /// to this object. False when it is switched off or already used up.
    bool Use(GameObject* actor);
    bool Available() const { return enabled && key != 0 && !(oneShot && useCount > 0); }
};

class InteractionSensor : public Behaviour {
public:
    float maxRange = 0.0f;          ///< 0 = each thing's own range
    GameObject* Target() const { return m_target; }
    const std::string& Prompt() const { return m_prompt; }
    /// 0..1 while the key is held for a hold interactable.
    float HoldProgress() const { return m_progress; }
    void Update(float dt) override;
    void LateUpdate(float) override;   // the press of this frame is spent
private:
    GameObject* m_target = nullptr;
    std::string m_prompt;
    float m_progress = 0.0f;
    float m_held = 0.0f;
};

} // namespace okay
