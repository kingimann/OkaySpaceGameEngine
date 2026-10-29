#pragma once
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
};

/// Whole-game saves in named slots. A save holds the scene as it is right now
/// (every object, where it is, what it carries: the player, inventories, chests,
/// placed builds, objects spawned or destroyed), plus what the scene file
/// doesn't: rigidbody velocities, script and visual-script variables, and the
/// game's Save keys (the default save file). Loading puts all of it back.
///
/// Slots are files under Save::BaseDir()/saves/<slot>.okaygame (per user, per
/// game in the player). Slot names are letters, digits, '-' and '_'.
class SaveGame {
public:
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
    /// Every slot on disk, newest first.
    static std::vector<SaveSlotInfo> List();

    static bool ValidSlot(const std::string& slot);
    static std::string SlotPath(const std::string& slot);
    /// Seconds played this run, plus the play time of the save it was loaded from.
    static float PlayTime();
};

} // namespace okay
