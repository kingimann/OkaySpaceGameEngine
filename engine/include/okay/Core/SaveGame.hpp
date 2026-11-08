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

/// Whole-game saves in named slots. A save holds the scene as it is right now
/// (every object, where it is, what it carries: the player, inventories, chests,
/// placed builds, objects spawned or destroyed), plus what the scene file
/// doesn't: rigidbody velocities, script and visual-script variables, and the
/// game's Save keys (the default save file). Loading puts all of it back.
///
/// Slots are files under Save::BaseDir()/saves/<slot>.okaygame (per user, per
/// game in the player). Slot names are letters, digits, '-' and '_'. Each file
/// carries a format version and ends with a checksum; the previous good save is
/// kept beside it as <slot>.okaygame.bak and used if the main file is damaged.
/// Loading a slot replaces the default Save file's keys with the slot's copy.
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
    /// name or a file that can't be written.
    static bool Save(Scene& scene, const std::string& slot, const std::string& label = "",
                     std::string* error = nullptr);
    /// Load `slot` into the scene at the end of this frame (safe from scripts
    /// and menus). False if the slot doesn't exist or can't be read.
    static bool Load(Scene& scene, const std::string& slot, std::string* error = nullptr);
    /// Load right away (outside Update: tools, tests, a title screen).
    static bool LoadNow(Scene& scene, const std::string& slot, std::string* error = nullptr);

    static bool Exists(const std::string& slot);
    static bool Delete(const std::string& slot);
    static SaveSlotInfo Info(const std::string& slot);
    /// Every slot on disk, newest first. Reads only each file's header lines.
    static std::vector<SaveSlotInfo> List();

    static bool ValidSlot(const std::string& slot);
    static std::string SlotPath(const std::string& slot);
    /// Seconds played this run, plus the play time of the save it was loaded from.
    static float PlayTime();
};

} // namespace okay
