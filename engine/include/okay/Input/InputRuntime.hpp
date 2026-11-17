#pragma once
// The process-wide input runtime scripts talk to: one PlayerInputManager, the asset it starts players from, and the per-frame
// update (called from InputActions::Tick, so every host that already ticks input drives it). With no asset set the default asset
// is used; with no player joined the first query joins a single "any device" player, so a script can just read actions.
#include "okay/Input/PlayerInput.hpp"
#include <string>

namespace okay {

class InputRuntime {
public:
    static PlayerInputManager& Players();
    /// Use the asset in `path` for players that join from now on (and re-create the ones already in).
    static bool LoadAsset(const std::string& path, std::string* error = nullptr);
    static void UseDefault();
    static const std::string& AssetPath();            ///< "" = the default asset
    /// The ActionSet of `player`, creating the default asset and a first player if nothing was set up yet. nullptr for a missing player.
    static ActionSet* Set(int player = 0);
    /// Next phase event of this frame for `player` ("Jump:performed"), false when none are left.
    static bool PopEvent(int player, std::string& out);
    static void Update(float dt);
    static void Reset();                              ///< forget players and asset (tests)
    /// Join from a word: "keyboard", "pad0".."pad3", "any". Returns the player index or -1.
    static int JoinNamed(const std::string& device);
};

} // namespace okay
