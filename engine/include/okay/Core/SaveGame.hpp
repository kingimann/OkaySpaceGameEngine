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
};

/// Whole-game saves in named slots. A save holds the scene as it is right now
/// (every object, where it is, what it carries: the player, inventories, chests,
/// placed builds, objects spawned or destroyed), plus what the scene file
/// doesn't: rigidbody velocities, script and visual-script variables, and the
/// game's Save keys (the default save file). Loading puts all of it back.
///
/// Slots are files under Save::BaseDir()/saves/<slot>.okaygame (per user, per
/// game in the player). Slot names are letters, digits, '-' and '_' (up to 64, and not a
/// Windows device name such as "con" or "nul2"). Each file
/// carries a format version and ends with a checksum; the previous good save is
/// kept beside it as <slot>.okaygame.bak and used if the main file is damaged.
/// Loading a slot replaces the default Save file's keys with the slot's copy.
/// A slot saved by a newer build is listed (flagged `incompatible`), never loaded, and is
/// kept as `<slot>.okaygame.newer.bak` if an older build saves over it.
/// Use from the game's main thread (the play-time counter and migrations are not guarded).
class SaveGame {
public:
    /// The save format this build writes. Files from a newer one are refused.
    static constexpr int kFormatVersion = 2;
    /// Caps on one slot file, so a hostile or runaway file can't exhaust memory.
    static constexpr std::size_t kDefaultMaxFileBytes = 256u * 1024u * 1024u;
    static constexpr std::size_t kDefaultMaxLines = 8000000u;
    /// Change the caps (0 = back to the default). For tools and tests.
    static void SetLimits(std::size_t maxFileBytes, std::size_t maxLines);

    /// Upgrade hook: `fn` gets the text of a slot saved in format `fromVersion` and
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
    static std::string SlotPath(const std::string& slot);
    /// Seconds played this run, plus the play time of the save it was loaded from.
    static float PlayTime();
};

} // namespace okay
