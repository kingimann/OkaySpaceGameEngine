#pragma once
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace okay {

class Scene;

/// What a save slot holds, for a load menu.
struct SaveSlotInfo {
    std::string slot;       ///< "1", "quick", "autosave"...
    std::string label;      ///< what the game called it ("Chapter 2: The Mine")
    std::string scene;      ///< the scene's name when it was saved
    long long   savedAt = 0;   ///< seconds since 1970 (UTC)
    float       playTime = 0.0f;   ///< seconds played up to the save
    bool        exists = false;
    int         version = 0;       ///< the save format the file was written in

    // ---- Added by the save framework (SaveFramework.hpp). SaveGame leaves them at
    // their defaults; SaveStore fills them in for its own slot files. ----
    long long   savedAtMs = 0;     ///< milliseconds since 1970 (UTC); savedAt is this / 1000
    std::string thumbnail;         ///< image the game stored for the slot (empty = none)
    std::string gameVersion;       ///< the game's own version string when it was saved
    std::string kind;              ///< "manual", "quick", "auto" or "checkpoint"
    std::map<std::string, std::string> fields;   ///< custom text fields the game attached
    long long   sizeBytes = 0;     ///< size of the slot file on disk
    bool        corrupt = false;   ///< unreadable or failed its checks (see `error`)
    bool        incompatible = false;   ///< written by a newer build: can't be loaded here
    bool        fromBackup = false;     ///< the main file is damaged; these values come from its .bak
    std::string error;             ///< why it is flagged (empty when it is fine)
    /// An older whole-game save (<slot>.okaygame, written before SaveGame joined the save framework). It loads,
    /// lists and counts for Continue like any slot; `version` is then its old format number (1 or 2). The next
    /// save to the slot writes the current .okayslot file and retires it.
    bool        legacyFile = false;
    /// A whole-scene save (SaveGame, save_game, AutoSave, or an older .okaygame file). The SaveGame names
    /// (has_save, save_slots, load_game, ...) see only these; GameSaves and Continue see every slot.
    bool        wholeScene = false;
};

/// What a game attaches to a slot beyond the scene, for a load menu to show.
struct SaveOptions {
    std::string label;         ///< what the game calls it ("Chapter 2: The Mine")
    /// An image for the slot. A bare file name ("slot1.png") is a file in the saves folder
    /// that `SaveGame::Delete` removes with the slot (`SaveGame::ThumbnailPath(slot)` is where
    /// a screenshot for it goes); a path with folders is yours and is never touched.
    std::string thumbnail;
    std::string gameVersion;   ///< the game's own version text when it was saved
    std::map<std::string, std::string> fields;   ///< custom text a menu shows ("difficulty" = "hard")
    std::string kind;          ///< SlotKind text ("manual" when empty; the AutoSave component uses "auto")
};

/// Whole-game saves in named slots: the save framework's "whole scene" mode (SaveFramework.hpp,
/// SaveCaptureOptions::wholeScene). A save holds the scene as it is right now (every object, where it is,
/// what it carries: the player, inventories, chests, placed builds, objects spawned or destroyed), plus what
/// the scene file doesn't: rigidbody velocities, script and visual-script variables, and the game's Save keys
/// (the default save file). Loading puts all of it back.
///
/// Slots live in the framework's store (GameSaves::Manager().Store(), by default Save::BaseDir()/saves):
/// one <slot>.okayslot file per slot, the same files `game_save`, Continue, the autosave ring and the Save
/// Slots screen use, so a slot saved here is seen there (Continue, game_* calls). The SaveGame names see only
/// whole-scene slots (SaveSlotInfo::wholeScene): a framework slot of Saveable objects (an autosave ring, a quick
/// save, a checkpoint) is "No save" to them, as before. A game's SetDataVersion / migrations never run on
/// whole-scene slots, and SetLimits changes only SaveGame's caps. Slot names are letters,
/// digits, '-' and '_' (up to 64, and not a Windows device name such as "con" or "nul2"). Each file carries a
/// version and checksums; the previous good save is kept as <slot>.okayslot.bak and used if the main file is
/// damaged. A slot saved by a newer build is listed (flagged `incompatible`), never loaded, and is kept as
/// `.newer.bak` if an older build saves over it.
///
/// Older saves: SaveGame used to write <slot>.okaygame files. They still load, list and count for Continue
/// (SaveSlotInfo::legacyFile); SetMigration and SetLimits apply to them. The next save to that slot writes the
/// .okayslot file and keeps the old save as <slot>.okayslot.bak. If a slot has both files (two different saves
/// from the two older systems, or a .okaygame written after going back to an older release), neither is hidden:
/// the one saved earlier moves to the free name <slot>_old_save (_old_save_2, ...) with its label and time.
/// Use from the game's main thread (the play-time counter and migrations are not guarded).
class SaveGame {
public:
    /// The newest .okaygame format this build reads (older saves; new saves are .okayslot files). Newer ones are refused.
    static constexpr int kFormatVersion = 2;
    /// Caps on one slot file, so a hostile or runaway file can't exhaust memory.
    static constexpr std::size_t kDefaultMaxFileBytes = 256u * 1024u * 1024u;
    static constexpr std::size_t kDefaultMaxLines = 8000000u;
    /// Change the caps (0 = back to the default). For tools and tests. The byte cap is also the store's
    /// (SaveStore::SetMaxBytes); the line cap counts the lines of the saved scene and state.
    static void SetLimits(std::size_t maxFileBytes, std::size_t maxLines);

    /// Upgrade hook for older .okaygame files: `fn` gets the text of a slot saved in format `fromVersion` and
    /// rewrites it into the next version's layout (false + message = can't). Steps
    /// run in order, so a version 1 file passes through every step above it. Pass
    /// an empty function to remove one. A step with no hook leaves the text as is.
    using Migration = std::function<bool(std::string& text, std::string* error)>;
    static void SetMigration(int fromVersion, Migration fn);

    /// True if the last Load/LoadNow read the slot's .bak because the main file was
    /// damaged (the `error` text then says so too, though the load succeeded).
    static bool LastLoadUsedBackup();

    /// Save the scene to `slot`. False (with a reason in `error`) on a bad slot
    /// name or a file that can't be written (a full disk, a read-only folder: the old
    /// slot is untouched, the reason says which).
    static bool Save(Scene& scene, const std::string& slot, const std::string& label = "",
                     std::string* error = nullptr);
    /// Save with a thumbnail, a game version and custom fields a load menu shows (they come
    /// back in `Info` and `List`).
    static bool Save(Scene& scene, const std::string& slot, const SaveOptions& options,
                     std::string* error = nullptr);
    /// Load `slot` into the scene at the end of this frame (safe from scripts
    /// and menus). False if the slot doesn't exist or can't be read.
    static bool Load(Scene& scene, const std::string& slot, std::string* error = nullptr);
    /// Load right away (outside Update: tools, tests, a title screen).
    static bool LoadNow(Scene& scene, const std::string& slot, std::string* error = nullptr);

    static bool Exists(const std::string& slot);
    /// Remove the slot with its backups (and a thumbnail kept in the saves folder).
    static bool Delete(const std::string& slot);
    /// What a load menu shows for the slot: label, scene, time, play time, thumbnail path,
    /// game version, custom fields, file size and the flags `incompatible` (saved by a
    /// newer build), `fromBackup` and `corrupt`. Reads only the header lines, so a file cut
    /// short after its header isn't noticed; `verify` reads the whole file and checks its
    /// checksum (use it for one slot, not a long list of big saves). A slot that doesn't
    /// exist has `exists` false.
    static SaveSlotInfo Info(const std::string& slot, bool verify = false);
    /// Every slot on disk, newest first (slots with no known time last), damaged and
    /// newer-version slots included and flagged. Reads only each file's header lines
    /// unless `verify`.
    static std::vector<SaveSlotInfo> List(bool verify = false);
    /// True when the slot would load: its file passes its checks, or its backup does. Reads
    /// the whole file but doesn't build the scene. `error` says what is wrong otherwise.
    static bool Check(const std::string& slot, std::string* error = nullptr);
    /// Where a screenshot for the slot goes: `saves/<slot>.png` in the save folder. Pass the
    /// bare name (`<slot>.png`) as `SaveOptions::thumbnail` so Delete removes it with the slot.
    static std::string ThumbnailPath(const std::string& slot);

    static bool ValidSlot(const std::string& slot);
    /// The slot's file in the store (<slot>.okayslot).
    static std::string SlotPath(const std::string& slot);
    /// Where an older save of the slot lives (<slot>.okaygame beside the store's files).
    static std::string LegacySlotPath(const std::string& slot);
    /// Seconds played this run, plus the play time of the whole-scene save it was loaded from. SaveGame's own
    /// clock: framework loads (checkpoints, game_load of a Saveable slot) use SaveSystem::PlayTime and leave it alone.
    static float PlayTime();
};

/// The whole-scene capture behind SaveGame, used by the save framework (SaveSystem::CaptureScene with
/// SaveCaptureOptions::wholeScene, SaveSystem::ApplyScene, SaveStore for older .okaygame files). Engine-internal.
namespace wholescene {

/// What a whole-scene save holds besides its header.
struct State {
    std::string sceneText;   ///< SceneSerializer text
    std::string stateText;   ///< tab-separated lines: body3, body2, script, graph, kv (the game's Save keys)
    float playTime = 0.0f;
};

/// The scene as text plus its state lines (velocities, script and graph variables, the game's Save keys).
State Capture(Scene& scene);
/// Check the state lines and the line cap before anything is replaced. False with a reason.
bool Check(const State& state, std::string* error);
/// Replace the scene with the saved one now (Deserialize, Start), then put the state back. False with a reason.
bool ApplyNow(Scene& scene, const State& state, bool restorePlayTime, std::string* error);
/// The same at the end of the frame (Scene::RequestLoadText, safe from scripts); `after` runs once it is back.
/// `done` runs exactly once: done(true) after the state is back, done(false) when the load was replaced by
/// another one in the same frame or the saved scene text did not load.
void ApplyDeferred(Scene& scene, State state, bool restorePlayTime, std::function<void(bool ok)> done);

enum class LegacyStatus { Ok, Missing, Damaged, Newer };
/// Read an older .okaygame file at `path` whole (format, checksum, SetMigration steps, SetLimits), falling back
/// to its .bak when it is damaged (`usedBackup`, and `error` then holds a note though it succeeded).
LegacyStatus ReadLegacy(const std::string& path, const std::string& slot, State& out, SaveSlotInfo& info,
                        bool* usedBackup, std::string* error);
/// The header of an older .okaygame file (cheap; `verify` reads it whole). `exists` false when there is none.
SaveSlotInfo LegacyInfo(const std::string& path, const std::string& slot, bool verify);
/// Remove an older .okaygame file and its .bak / .tmp / .newer.bak / .corrupt / .swap companions.
bool RemoveLegacy(const std::string& path);

} // namespace wholescene

} // namespace okay
