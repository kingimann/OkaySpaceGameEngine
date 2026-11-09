#pragma once
// Three-way merge of scene (or prefab) files, for version control: two people edited
// copies of one scene (`base`); `ours` and `theirs` are the results. Changes that do
// not overlap merge automatically (different objects, different records, different
// fields of one record, identical changes on both sides); true conflicts are listed
// and resolved by a policy. The merged text always loads: if the result would not
// validate (parents, unique ids, structure) the merge falls back to `ours` and says
// so. Works on text, objects are paired by guid (name path in files without ids).
// See docs/scene_diff_merge.md.
//
// Header note: keep this header free of <mutex>, <thread> and <condition_variable>.
#include <functional>
#include <string>
#include <vector>

namespace okay {

class SceneMerge {
public:
    enum class Policy {
        PreferOurs,     ///< every conflict is settled with our side
        PreferTheirs,   ///< every conflict is settled with their side
        Mark,           ///< like PreferOurs, and the caller is expected to show the report
    };

    struct Conflict {
        enum class Kind {
            FieldChanged,           ///< the same field of a record changed differently
            RecordChanged,          ///< the same record changed differently as a whole
            RecordDeletedAndEdited, ///< a record deleted on one side and edited on the other
            RecordAddedDifferently, ///< both sides added a record of the same kind, differently
            ObjectDeletedAndEdited, ///< an object deleted on one side and edited on the other
            RenamedDifferently,
            ParentChanged,          ///< both sides moved the object under different parents
            ParentDeleted,          ///< a side added or moved an object under one the other side deleted
            ParentCycle,            ///< the two reparentings together would make a loop
            OrderChanged,           ///< both sides moved an object to different places
            Invalid,                ///< the merged text failed its check; `ours` was returned
        };
        Kind kind = Kind::FieldChanged;
        std::string object;       ///< the object's identity ("g:<guid>" or "p:<path>"); "" for file-level settings
        std::string name, path;   ///< where it sits (a name path like "Level/Arena/Enemy")
        std::string record;       ///< "Light", "Script#1", ...
        std::string field;        ///< "intensity", "[3]" ...
        std::string base, ours, theirs;   ///< the values (text as in the files; "(deleted)" where one side removed it)
        std::string resolution;   ///< "ours", "theirs", "dropped", "kept under X"
        std::string KindName() const;
    };

    struct Options {
        Policy policy = Policy::Mark;
        /// An extra check on the merged text before it is accepted, for instance loading
        /// it into a scratch Scene with SceneSerializer::Deserialize. Return false (with a
        /// reason in *error) to make the merge fall back to `ours`.
        std::function<bool(const std::string& text, std::string* error)> validator;
        /// For tests: skip the exact shortcuts (identical sides, one side unchanged) and
        /// always run the full algorithm.
        bool skipShortcuts = false;
    };

    struct Result {
        /// Both sides (and the base) were readable scenes. False means `text` is `ours`
        /// as given, and `error` says why nothing was merged.
        bool ok = false;
        std::string error;
        /// The merged scene, always loadable when `ok`. Canonical layout. `ours` when `fellBack`.
        std::string text;
        bool fellBack = false;
        std::vector<Conflict> conflicts;
        std::vector<std::string> warnings;
        /// Merged without a single conflict.
        bool Clean() const { return ok && !fellBack && conflicts.empty(); }
        /// The conflicts, readable.
        std::string ReportText() const;
        /// The conflicts for a tool: {"ok":..,"fellBack":..,"policy":..,"conflicts":[{...}]}
        std::string ReportJson() const;
        Policy policy = Policy::Mark;
    };

    /// Merge. An empty `baseText` means "both sides created the file".
    static Result Merge(const std::string& baseText, const std::string& oursText, const std::string& theirsText);
    static Result Merge(const std::string& baseText, const std::string& oursText, const std::string& theirsText,
                        const Options& opts);
};

} // namespace okay
