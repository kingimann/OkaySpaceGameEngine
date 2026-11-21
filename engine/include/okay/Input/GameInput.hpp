#pragma once
#include "okay/Scene/Scene.hpp"
#include <string>

namespace okay {

/// Why a world action (dig, swing, hotbar, click-to-move) should ignore this
/// frame. Queried from the scene itself, so it does not depend on which
/// component's Update ran first or on the host resetting Input::UICaptured
/// before PauseMenu gets to set it again.
enum class GameInputMask : unsigned {
    None      = 0,
    Pause     = 1u << 0,   ///< Game::Paused (the pause menu, whatever update order)
    Bag       = 1u << 1,   ///< inventory or grid bag open
    Chest     = 1u << 2,
    Craft     = 1u << 3,
    Text      = 1u << 4,   ///< a text field has focus
    Modal     = 1u << 5,   ///< UIModal or the settings screen
    Dialogue  = 1u << 6,
    Pointer   = 1u << 7,   ///< mouse button down over a UI widget
    Sequence  = 1u << 8,   ///< a cutscene locks the player
    /// World clicks, swings, digging.
    World     = Pause | Bag | Chest | Craft | Text | Modal | Dialogue | Pointer | Sequence,
    /// Hotbar keys, the bag toggle, sort, drop, use. The bag's own open state is
    /// not a reason to swallow the key that closes it.
    Keys      = Pause | Text | Modal | Dialogue | Sequence,
};

inline unsigned operator|(GameInputMask a, GameInputMask b) {
    return static_cast<unsigned>(a) | static_cast<unsigned>(b);
}
inline unsigned operator|(unsigned a, GameInputMask b) { return a | static_cast<unsigned>(b); }

/// True when `mask` matches something that should swallow gameplay input.
bool GameInputBlocked(const Scene* scene, unsigned mask = static_cast<unsigned>(GameInputMask::World));

/// The first matching reason, for tests and logs. Empty when nothing matches.
const char* GameInputBlockReason(const Scene* scene, unsigned mask = static_cast<unsigned>(GameInputMask::World));

/// Pickup or drop owns `key` this frame: an item is in range, or one is already
/// in the hands and a HoldPrompt drops on this key. The bag toggle steps aside.
bool InteractClaimsKey(const Scene* scene, char key);

// ---- The shared Interact action -------------------------------------------------------------
// One "Interact" action (InputActions, default E and pad X, rebindable) is the use key of seats,
// dialogue, pickups, buttons and the visual-script interact events. A component that stores its own
// key char treats the Interact action's default key (E) as "the Interact action": rebinding Interact
// moves it too. Any other key it stores is its own and stays as it is.

/// The Interact action's default keyboard key. A component key equal to it follows the action.
constexpr char kInteractDefaultKey = 'e';
/// The Interact action was pressed this frame (any of its key, mouse or controller bindings).
/// Reads the devices directly, so it works whether or not the host ticks InputActions.
bool InteractPressed();
/// Held now / released this frame.
bool InteractHeld();
bool InteractReleased();
/// `key` is one of the Interact action's keyboard bindings.
bool IsInteractKey(char key);
/// A component's use key pressed this frame: E follows the Interact action, any other key is read
/// as it is, 0 is off.
bool UseKeyDown(char componentKey);
/// The component key `componentKey` answers to the physical key `key` (for "who owns this press").
bool UseKeyIs(char componentKey, char key);
/// What to show a player for a component's use key ("E", or the key Interact is rebound to).
std::string UseKeyName(char componentKey);

/// A lean key (Q by default; lean-right was E and moved off it) should be ignored this frame: a chest, bench or
/// pickup or seat in range wants it as its use key, or it is also a bag key and was just
/// pressed to open or close the bag. One press, one owner.
bool LeanKeyTaken(const Scene* scene, char key, const GameObject* who = nullptr);

} // namespace okay
