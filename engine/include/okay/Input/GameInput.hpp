#pragma once
#include "okay/Scene/Scene.hpp"

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

} // namespace okay
