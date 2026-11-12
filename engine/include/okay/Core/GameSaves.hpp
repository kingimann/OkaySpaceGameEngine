#pragma once
// The no-code route to the save framework (docs/save_framework.md).
//
//   Saveable (SaveFramework.hpp)  marks an object and lists what of it is saved
//   SaveConfig                  one per scene: autosave interval and autosave on scene start
//   GameSaves                     Save / Load / Delete / Continue / "slot exists" / "last saved
//                                 time" in one call each, with every problem written to the
//                                 Console. The script builtins (game_save ...), the visual-script
//                                 Fn.game_* nodes, the Action List game_* actions and the
//                                 Save Slots screen all call these.
#include "okay/Core/SaveFramework.hpp"
#include "okay/Scene/Component.hpp"
#include <string>

namespace okay {

class Scene;

/// What SaveConfig sets for the running game.
struct GameSaveConfig {
    float autosaveInterval = 0.0f;       ///< seconds of play between autosaves (0 = off)
    bool autosaveOnSceneStart = false;   ///< autosave when a scene that has a SaveConfig starts
    int autosaveSlots = 3;               ///< autosave_1 ... autosave_N, oldest replaced first
};

class GameSaves {
public:
    /// The manager every call uses: SaveManager::Default() unless UseManager says otherwise
    /// (tests point it at a temp folder).
    static SaveManager& Manager();
    static void UseManager(SaveManager* manager);
    static GameSaveConfig& Settings();

    /// Save the scene to a slot. `label` is what a load menu shows ("" = "Saved game"). False,
    /// with the reason in `error` and in the Console, when it could not be written.
    static bool Save(Scene& scene, const std::string& slot, const std::string& label = std::string(),
                     std::string* error = nullptr);
    /// Load a slot. It happens at the start of the scene's next frame, so it is safe from an
    /// Update or a script. When the save was made in a scene of the build list, that level is
    /// loaded first; the state is then put on it (objects found by save id, runtime-spawned
    /// objects recreated, destroyed ones removed). Checks the slot now: a missing, damaged
    /// or newer-version slot returns false at once and writes the reason to the Console.
    static bool Load(Scene& scene, const std::string& slot, std::string* error = nullptr);
    /// Load the newest slot that is valid (a damaged one or one from a newer version is skipped).
    static bool Continue(Scene& scene, std::string* error = nullptr);
    /// Save into the autosave ring (Settings().autosaveSlots places).
    static bool AutoSave(Scene& scene, std::string* error = nullptr);
    static bool Delete(const std::string& slot);
    static bool Exists(const std::string& slot);
    static bool CanContinue();
    /// The slot Continue would load ("" = none).
    static std::string ContinueSlot();
    /// When the slot was saved, in Unix seconds (0 = no such slot).
    static long long SavedAt(const std::string& slot);
    /// Seconds since the slot was saved (-1 = no such slot).
    static double SecondsSinceSave(const std::string& slot);
    /// "2026-11-08 14:03" (UTC) or "" when there is no such slot.
    static std::string SavedText(const std::string& slot);
    static std::string Label(const std::string& slot);
    /// A load is waiting for the next frame or still putting the state back.
    static bool Restoring();
    /// Autosave when the interval has run out or a scene started; what SaveConfig calls.
    static void RequestSceneStartAutosave(Scene& scene);
};

/// One per scene: when and how often the game saves by itself. Nothing else to set up.
/// Objects are marked with a Saveable component.
class SaveConfig : public Behaviour {
public:
    float autosaveInterval = 0.0f;       ///< seconds of play between autosaves (0 = off)
    bool autosaveOnSceneStart = false;   ///< save when this scene starts (not while a load is putting a save back)
    int autosaveSlots = 3;               ///< how many autosaves are kept (1 to 20)

    void Start() override;
    void Update(float dt) override;
    /// Seconds until the next interval autosave (0 when off).
    float SecondsUntilAutosave() const { return autosaveInterval > 0.0f ? autosaveInterval - m_timer : 0.0f; }

private:
    float m_timer = 0.0f;
};

} // namespace okay
