#pragma once
// The editor side of prefabs, without any drawing (so it is tested headlessly): what state an
// instance is in (plain, variant, nested), which of its fields differ from the prefab, and the
// actions the Inspector, the Hierarchy and the Project panel call: Revert / Apply of a field, a
// component, an added or lost child object, or everything; Create Variant; Unpack; Update All.
//
// Nothing here is a second prefab system. The reading and writing of prefab files, the overrides,
// nesting and variants are okay::PrefabTools (engine/src/Scene/PrefabTools.cpp); this file adds what
// the editor needs around it:
//  - one undo step per action. The scene half goes through EditorState::PushUndo, the prefab files
//    an Apply writes go through EditorState::AttachUndoFiles (a file command joined to the step by PushUndo(name, command)), so Undo / Redo move scene and files
//    together;
//  - Apply updates the other instances in the scene: their own overrides are captured against the
//    OLD file first, then they are rebuilt from the NEW file, so an instance that overrode a
//    field keeps its value and the others take the new one;
//  - the names the Inspector shows (which diff fields belong to which labelled property).
#include "EditorState.hpp"
#include "InspectorModel.hpp"
#include <string>
#include <vector>

namespace oked {

enum class PrefabKind {
    Plain,     ///< an ordinary prefab file
    Variant,   ///< the file is `variant "<base>"` plus differences
    Nested     ///< the file holds `nestedprefab` objects (other prefabs inside it)
};

/// What the Inspector and Hierarchy show for an object that is, or sits inside, a prefab instance.
struct PrefabInstanceInfo {
    okay::GameObject* root = nullptr;     ///< the innermost instance root at or above the object
    okay::GameObject* outer = nullptr;    ///< the outermost one (== root unless the instance is nested in another)
    std::string source;                   ///< root->prefabSource
    std::string name;                     ///< file name of `source`
    bool exists = false;                  ///< the prefab file can be read
    PrefabKind kind = PrefabKind::Plain;
    std::string base;                     ///< Variant: the base file as written in the variant
    std::string baseName;                 ///< file name of `base`
    bool isNestedInstance = false;        ///< root != outer: this instance is a part of a bigger prefab instance
    std::string outerName;                ///< file name of outer->prefabSource, when nested
    int nestedCount = 0;                  ///< instances directly inside `root`
    bool isRoot = false;                  ///< the object itself is the instance root
};
/// False when `go` is not in a prefab instance.
bool DescribePrefabInstance(const okay::GameObject& go, PrefabInstanceInfo& out);
/// "Prefab Variant of Enemy.okayprefab", "Nested prefab (inside Car.okayprefab)", "Prefab" ...
std::string PrefabKindLabel(const PrefabInstanceInfo& info);
/// The kind of a prefab file ("variant" / "nestedprefab" lines), and for a variant its base as written.
PrefabKind PrefabKindOfFile(const std::string& path, std::string* base = nullptr);
/// The variant's base file as a path that exists ("" when it cannot be found): the path as written, or next to the variant.
std::string ResolveVariantBase(const PrefabInstanceInfo& info);
/// Save `root` and its descendants as a prefab file. Prefab instances inside it are written as nested prefabs
/// (PrefabTools::SavePrefab) so their links survive; a tree with none is written as before.
bool SaveAsPrefabFile(const okay::GameObject& root, const std::string& path, std::string* error = nullptr);
/// Forget cached file kinds and change lists (after a prefab file was written).
void ClearPrefabInfoCache();

// ---- What differs -----------------------------------------------------------------------------

/// The names of the diff fields of `ch` that belong to the Inspector property drawn with `label` on `comp`
/// ("Intensity", "Use Temperature##light", "Position"). Empty when the label matches no changed field.
/// A Vector property matches all its parts (position.x, position.y, position.z).
std::vector<std::string> ChangedFieldsForLabel(const PrefabChange& ch, const okay::Component& comp, const std::string& label);

/// An object the instance gained (a child added under it) or lost (a child of the prefab that is gone).
struct PrefabObjectChange {
    bool added = true;
    okay::GameObject* live = nullptr;  ///< added: the object
    int prefabObject = -1;             ///< lost: its index in the prefab (depth first)
    int parentObject = -1;             ///< added: the prefab index of the parent it sits under
    std::string name;
    std::string path;                  ///< "Root/Arm/Hand"
    std::string description;           ///< "Added object Bulb" / "Removed object Shade"
};
std::vector<PrefabObjectChange> PrefabObjectChangesOf(okay::GameObject& root);

/// How many things differ on the instance, for a Hierarchy marker. Cached for a while; cheap to call every frame.
int PrefabOverrideCount(okay::GameObject& root);

// ---- Actions ----------------------------------------------------------------------------------

/// One record, or one field of it, as PrefabTools addresses it.
struct PrefabTarget {
    int object = 0;
    std::string record;
    std::string field;       ///< "" = the whole record
    int occurrence = 0;
};
/// The target for a whole change, or one field of it ("" = whole change).
PrefabTarget TargetOf(const PrefabChange& ch, const std::string& field = "");

struct PrefabOpResult {
    bool ok = false;
    std::string message;                ///< what to tell the user (also the error text)
    okay::GameObject* root = nullptr;   ///< the instance root afterwards (it is rebuilt by a revert)
    int updatedInstances = 0;           ///< other instances rebuilt because the prefab file changed
};

/// Put records / fields back to the prefab's values. One undo step. `root` is the instance root.
PrefabOpResult PrefabRevert(okay::editor::EditorState& ed, okay::GameObject& root, const std::vector<PrefabTarget>& targets);
/// Every override gone, added objects dropped, lost objects back. One undo step.
PrefabOpResult PrefabRevertAll(okay::editor::EditorState& ed, okay::GameObject& root);
/// Write records / fields into the prefab file, then update the other instances. One undo step (scene and file).
PrefabOpResult PrefabApply(okay::editor::EditorState& ed, okay::GameObject& root, const std::vector<PrefabTarget>& targets);
/// Apply every changed record and, for a plain prefab, every added or lost child object.
PrefabOpResult PrefabApplyAll(okay::editor::EditorState& ed, okay::GameObject& root);
/// A child added to the instance goes (revert); a child the instance lost comes back from the prefab.
PrefabOpResult PrefabRevertObject(okay::editor::EditorState& ed, okay::GameObject& root, const PrefabObjectChange& ch);
/// The added child becomes part of the prefab, or the lost child is removed from it (plain prefab files only).
PrefabOpResult PrefabApplyObject(okay::editor::EditorState& ed, okay::GameObject& root, const PrefabObjectChange& ch);
/// Why an object change cannot be applied (variant / nested files), "" when it can.
std::string ApplyObjectBlockedReason(const PrefabInstanceInfo& info);

/// A free "<Name> Variant.okayprefab" next to `source`.
std::string DefaultVariantPath(const std::string& source);
/// Save the instance as a variant of its prefab at `variantPath` (what the instance changed becomes the variant) and
/// link the instance to the variant. One undo step (the new file goes away on Undo).
PrefabOpResult PrefabCreateVariant(okay::editor::EditorState& ed, okay::GameObject& root, const std::string& variantPath);
/// A new variant file of the prefab `basePath` with no differences yet (Project panel). Undo deletes it.
PrefabOpResult PrefabNewVariantFile(okay::editor::EditorState& ed, const std::string& basePath, const std::string& variantPath);
/// Cut the link: this instance becomes plain objects (`completely` also unlinks the instances inside it). One undo step.
PrefabOpResult PrefabUnpack(okay::editor::EditorState& ed, okay::GameObject& go, bool completely);
/// Rebuild every instance of `sourcePath` (and of files that use it) from the file, keeping their overrides. One undo step.
PrefabOpResult PrefabUpdateInstances(okay::editor::EditorState& ed, const std::string& sourcePath);

} // namespace oked
