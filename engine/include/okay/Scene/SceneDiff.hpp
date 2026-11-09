#pragma once
// Structured diff of two scene (or prefab) files, for version control. Works on the
// text: objects are paired by their persistent id (guid), or by name path in files
// that have none, and the report says what was added, removed, renamed, reparented
// or reordered and which record and field of each object changed
// ("Light.intensity 1 -> 2"). Output is deterministic. See docs/scene_diff_merge.md.
//
// Header note: keep this header free of <mutex>, <thread> and <condition_variable>.
#include <map>
#include <string>
#include <vector>

namespace okay {

class SceneDiff {
public:
    struct FieldChange {
        std::string name;                 ///< "intensity", "[3]" (position) or "" for a one-value record
        std::string before, after;        ///< as written in the file (strings keep their quotes)
        bool hadBefore = true, hasAfter = true;
    };
    struct RecordChange {
        enum class Kind { Added, Removed, Modified };
        Kind kind = Kind::Modified;
        std::string label;                ///< "Light", "BoxCollider2D#1", "script"
        std::string before, after;        ///< whole record text (before: removed / modified, after: added / modified)
        std::vector<FieldChange> fields;  ///< per field, for a modified record
    };
    struct ObjectChange {
        enum class Kind { Added, Removed, Modified };
        Kind kind = Kind::Modified;
        std::string key;                  ///< "g:<guid>" or "p:<name path>": the object's identity
        std::string guid;                 ///< the object's id ("" in a file that has none)
        std::string name;                 ///< its name (the new one when renamed)
        std::string path;                 ///< "Level/Arena/Enemy" (in the new file; the old one when removed)
        bool renamed = false;
        std::string oldName;
        bool reparented = false;
        std::string oldParentPath, newParentPath;   ///< "" is the scene root
        bool reordered = false;           ///< moved among its siblings
        int oldIndex = -1, newIndex = -1; ///< position among its siblings, before and after
        bool movedInFile = false;         ///< moved in the file's object order (rare)
        bool recordsReordered = false;    ///< same records, different order
        bool guidChanged = false;         ///< got (or lost, or swapped) its id
        std::vector<RecordChange> records;

        // What Apply needs to rebuild the object (not shown in the readable forms).
        std::string parentKey, newGuid, newHeaderTail;
        bool headerTailChanged = false;
        struct Slot { int baseIndex = -1; std::string text; };   ///< keep base record `baseIndex`, or write `text`
        std::vector<Slot> layout;
    };
    struct Result {
        bool ok = false;                  ///< both files parsed
        std::string error;
        std::vector<std::string> warnings;
        int baseVersion = 0, otherVersion = 0;
        std::vector<RecordChange> globals;           ///< file-level records (name, gravity, fog ...)
        std::vector<ObjectChange> objects;           ///< objects of the other file in file order, then removed ones
        int added = 0, removed = 0, modified = 0, renamed = 0, reparented = 0, reordered = 0;

        /// Same text (apart from layout) on both sides.
        bool Empty() const { return ok && objects.empty() && globals.empty() && baseVersion == otherVersion; }
        /// A readable summary, one line per change. Long values are shortened to `maxValueChars`.
        std::string ToText(std::size_t maxValueChars = 60) const;
        /// The same as JSON (full values, keys in a fixed order).
        std::string ToJson() const;

        // Replay data for Apply: the other file's object order and sibling orders by key.
        std::vector<std::string> order;
        std::map<std::string, std::vector<std::string>> childOrder;
        std::vector<ObjectChange::Slot> globalLayout;
    };
    struct Options {
        /// Report an object of a file without ids as renamed when it kept its content and
        /// parent but got a new name (otherwise it shows up as removed plus added).
        bool detectRenames = true;
    };

    /// Compare two files. `ok` is false (with `error`) when either does not parse.
    static Result Compute(const std::string& baseText, const std::string& otherText);
    static Result Compute(const std::string& baseText, const std::string& otherText, const Options& opts);

    /// Replay a diff on the file it was computed from. The result is the canonical text
    /// of the other side. False, with `error`, when the diff does not fit `baseText`.
    static bool Apply(const std::string& baseText, const Result& diff, std::string* out, std::string* error = nullptr);
};

} // namespace okay
