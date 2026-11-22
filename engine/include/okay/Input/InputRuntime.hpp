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
    /// A new play session in a project: forget the players, then use <projectDir>/Assets/input.okayinput when it exists (the default asset
    /// otherwise). A Player Input component with its own asset path still replaces it. True when the project's file was loaded.
    static bool UseProjectAsset(const std::string& projectDir);
    static void Update(float dt);
    /// Rebind one part of one binding of `player` (ActionSet::Rebind). Player 0's set is the default ActionSet: its linked
    /// actions are also what InputActions and the built-in controllers read, so they follow at once. Not saved: SaveOverrides.
    static bool Rebind(int player, const std::string& action, int binding, int part, const std::string& path);
    /// Put one binding back to the asset's (`binding` of `action`), or every binding of the player with action "".
    static bool ResetBinding(int player, const std::string& action = "", int binding = -1);
    /// Save `player`'s overrides (Prefs "inputov.player<N>"); for player 0 InputActions is saved too, so both layouts agree.
    static void SaveOverrides(int player);
    static void Reset();                              ///< forget players and asset (tests)
    /// Join from a word: "keyboard", "pad0".."pad3", "any". Returns the player index or -1.
    static int JoinNamed(const std::string& device);
};

} // namespace okay
