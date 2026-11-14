#pragma once
// The one controller for a first- and third-person character. Its View setting
// says which views the player has: First Person only, Third Person only, or
// Both (a toggle key switches, the camera blends). A view that is off is not
// built at all, so its settings are neither shown nor saved.
//
// The movement and camera code lives in FirstPersonController,
// ThirdPersonController and PlayerView. Those are this component's parts: it
// adds and removes them as the View changes, and they are never added by hand
// or saved as records of their own. Scripts and other components keep finding
// them with GetComponent, exactly as before.
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include <vector>

namespace okay {

class PlayerController : public Component {
public:
    enum class ViewMode { First = 0, Third = 1, Both = 2 };

    explicit PlayerController(ViewMode mode = ViewMode::Both) : m_mode(mode) {}

    ViewMode viewMode() const { return m_mode; }
    /// Change which views the player has. Adds the parts the new mode needs and
    /// removes the ones it does not (their settings go with them).
    void SetViewMode(ViewMode mode);

    /// Add a Player Controller (and the parts `mode` needs) to `go`. In Both,
    /// `start` is the view a fresh play begins in; the toggle is on.
    static PlayerController* Add(GameObject* go, ViewMode mode = ViewMode::Both,
                                 PlayerView::View start = PlayerView::View::Third);
    /// The Player Controller on `go`, or nullptr.
    static PlayerController* Of(GameObject* go) { return go ? go->GetComponent<PlayerController>() : nullptr; }
    /// Wrap the bare controllers a legacy scene put on `go` (First Person,
    /// Third Person, Player View) in one Player Controller. Returns it, or
    /// nullptr when `go` has no first- or third-person controller.
    static PlayerController* Adopt(GameObject* go);

    FirstPersonController* first() const  { return gameObject ? gameObject->GetComponent<FirstPersonController>() : nullptr; }
    ThirdPersonController* third() const  { return gameObject ? gameObject->GetComponent<ThirdPersonController>() : nullptr; }
    /// The view toggle (Both only): key, start view, blend.
    PlayerView*            toggle() const { return gameObject ? gameObject->GetComponent<PlayerView>() : nullptr; }

    /// The view the player is in or heading to: 0 first person, 1 third person.
    int CurrentView() const;

    void Awake() override { EnsureParts(); }
    void CollectOwnedParts(std::vector<Component*>& out) const override;
    /// A part is switched on and off with the component that owns it.
    void SyncEnabled();

private:
    ViewMode m_mode = ViewMode::Both;
    void EnsureParts();
};

} // namespace okay
