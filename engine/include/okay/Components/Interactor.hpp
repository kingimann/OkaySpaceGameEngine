#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/InputArbiter.hpp"
#include "okay/Net/NetOwnership.hpp"
#include <string>

namespace okay {

/// Optional. Put it on the local player to choose the shared use key. Chests,
/// benches, pickups, and aimed logic buttons already answer that key; this
/// component is how a game rebinds it and reads the prompt ("E — Open Chest").
/// key 0 keeps the default E. padButton -2 keeps the default (X). -1 turns the
/// pad button off.
class Interactor : public Behaviour {
public:
    char key = 0;
    int  padButton = -2;
    std::string prompt;     ///< last prompt, for scripts and the HUD (not saved)

    char Key() const {
        return key ? key : InputActions::Default(GameAction::Interact).key;
    }
    int Pad() const {
        return padButton == -2 ? InputActions::Default(GameAction::Interact).pad : padButton;
    }

    void Update(float) override {
        if (!IsLocallyControlled(gameObject)) { prompt.clear(); return; }
        prompt = InputArbiter::BestInteract(gameObject ? gameObject->scene() : nullptr).prompt;
    }
};

} // namespace okay
