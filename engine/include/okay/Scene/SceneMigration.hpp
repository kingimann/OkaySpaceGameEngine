#pragma once
// Upgrading old scene (and prefab) files: a registry of versioned steps, each taking
// `okayscene N` text to `okayscene N+1`, and MigrateToCurrent which runs the chain and
// returns the new text plus a log of what changed. A file from a newer build than this
// one is refused, never rewritten. See docs/scene_diff_merge.md.
//
// Two of today's three formats differ only in what the loader does when it opens the
// file (a Character body gets its Animator, an old first/third person player gets both
// controllers): that work needs the engine's component classes, so the text steps for
// those versions only bump the number when the file has nothing for them to do, and
// otherwise stop and report `needsEngine`; MigrateThroughEngine then lets the loader do
// the work and writes the scene back out.
//
// Header note: keep this header free of <mutex>, <thread> and <condition_variable>.
#include "okay/Scene/SceneText.hpp"

#include <functional>
#include <string>
#include <vector>

namespace okay {

class SceneMigration {
public:
    /// One upgrade, `from` -> `from + 1`. At most one step per `from`.
    struct Step {
        int from = 0;
        std::string name;                 ///< short id, e.g. "character-animator"
        std::string description;          ///< what changed in the format, and in which release
        /// The text rewrite. Edit `doc` (its version is set by the framework afterwards),
        /// add lines to `log` saying what changed, return false with `error` to refuse.
        /// Null: nothing to rewrite, the version number is just raised.
        std::function<bool(SceneText::Document& doc, std::vector<std::string>& log, std::string& error)> apply;
        /// Does the step have anything to do for this scene? Null: always.
        std::function<bool(const SceneText::Document& doc)> affects;
        /// The real upgrade is done by the engine's loader and cannot be written as text:
        /// when `affects` says it matters for a scene, MigrateTo stops here (`needsEngine`).
        bool needsEngine = false;
    };

    struct Result {
        /// False when nothing was done: the text is no scene, it is newer than the target, or a
        /// step is missing or refused (see `error`); `text` is then the input, untouched.
        bool ok = false;
        std::string error;
        std::string text;                 ///< the upgraded text (as far as it got)
        int fromVersion = 0, toVersion = 0;
        bool changed = false;
        /// `toVersion` is the version that was asked for.
        bool complete = false;
        /// Stopped at a step that only the engine's loader can apply (`toVersion` is its `from`).
        bool needsEngine = false;
        std::vector<std::string> log;
    };

    /// The format version this build writes.
    static int CurrentVersion();

    /// Bring `text` to the version this build writes. Output is in canonical layout.
    static Result MigrateToCurrent(const std::string& text);
    /// Bring `text` to `targetVersion` (for tools and tests; a target above CurrentVersion()
    /// needs steps registered for it).
    static Result MigrateTo(const std::string& text, int targetVersion);
    /// MigrateToCurrent, and where a step needs the engine, load the text into a scratch
    /// Scene (which applies those upgrades) and write it back out. Records this build does
    /// not know are not kept by that round trip, and objects without an id get new ones.
    static Result MigrateThroughEngine(const std::string& text);

    // ---- registry (register from one thread, at startup) ----

    /// Add a step. False when a step for `step.from` exists or `step.from` is below 1.
    static bool Register(const Step& step);
    /// Remove the step that upgrades from `from`. False when there is none.
    static bool Unregister(int from);
    /// The registered steps in order of `from`.
    static std::vector<Step> Steps();
    /// "okayscene 1 -> 2  character-animator: ..." one line per step.
    static std::string Describe();
};

} // namespace okay
