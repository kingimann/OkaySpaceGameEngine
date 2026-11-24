#pragma once
// The process-wide input runtime: THE action table (Actions(): the asset in use plus the actions code defines through
// InputActions, every device at once, holding the player's one rebinding store), one PlayerInputManager whose players start
// from it, and the per-frame update (called from InputActions::Tick, so every host that already ticks input drives it). With
// no asset set the default asset is used; with no player joined the first typed query joins a single "any device" player, so
// a script can just read actions. Player 0's bindings are the table's (a rebind of either is the same rebind); players 1+ keep
// their own rebinding ("inputov.player<N>").
#include "okay/Input/PlayerInput.hpp"
#include <string>

namespace okay {

class InputRuntime {
public:
    static PlayerInputManager& Players();
    /// The one action table InputActions, the built-in controllers and action_* scripts read (built on first use, again after
    /// the asset changes). Its rebinding is player 0's.
    static ActionSet& Actions();
    /// The table when it is built and not being built, else nullptr (never builds).
    static ActionSet* ActionsIfBuilt();
    /// Build the table again on next use (InputActions::Restore); players and the asset stay.
    static void RebuildActions();
    /// Changes each time the table is rebuilt (readers that cache it compare it).
    static unsigned ActionsSerial();
    /// Apply `fn` to the table (when built) and to every joined player's set: contexts, a new code action, a response.
    static void ForEachSet(void (*fn)(ActionSet&, const void*), const void* arg);
    /// Copy the table's bindings into player 0 (done by Update and after every table rebind; call after changing the table).
    static void SyncPlayer0();
    /// Use the asset in `path` for players that join from now on (and re-create the ones already in).
    static bool LoadAsset(const std::string& path, std::string* error = nullptr);
    static void UseDefault();
    static const std::string& AssetPath();            ///< "" = the default asset
    /// The ActionSet of `player`, creating the default asset and a first player if nothing was set up yet. nullptr for a missing player.
    static ActionSet* Set(int player = 0);
    /// Next phase event of this frame for `player` ("Jump:performed"), false when none are left.
    static bool PopEvent(int player, std::string& out);
    /// Where a project keeps its input asset, relative to the project folder (and to a built game's data folder: the
    /// export ships the file under the same path).
    static constexpr const char* kProjectAssetRef = "Assets/input.okayinput";
    /// A new play session in a project: forget the players, then use <projectDir>/Assets/input.okayinput when it exists (the default asset
    /// otherwise). A Player Input component with its own asset path still replaces it. True when the project's file was loaded.
    /// The editor's Play calls it with the project folder, the player with the game's data folder (a trailing '/' is fine).
    static bool UseProjectAsset(const std::string& projectDir);
    static void Update(float dt);
    /// Rebind one part of one binding of `player` (ActionSet::Rebind). Player 0's bindings are the table's, so InputActions and
    /// the built-in controllers follow at once. Not saved: SaveOverrides.
    static bool Rebind(int player, const std::string& action, int binding, int part, const std::string& path);
    /// Put one binding back to the asset's (`binding` of `action`), or every binding of the player with action "".
    static bool ResetBinding(int player, const std::string& action = "", int binding = -1);
    /// Save `player`'s overrides (Prefs "inputov.player<N>"); player 0's are the table's (InputActions::Save).
    static void SaveOverrides(int player);
    static void Reset();                              ///< forget players and asset (tests)
    /// Join from a word: "keyboard", "pad0".."pad3", "any". Returns the player index or -1.
    static int JoinNamed(const std::string& device);
};

} // namespace okay
