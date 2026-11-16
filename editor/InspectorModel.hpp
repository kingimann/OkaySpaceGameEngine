#pragma once
// The logic behind the Hierarchy and Inspector windows, without any drawing, so it
// can be tested: the Hierarchy search filter, drag-and-drop reparenting, editing the
// same property on several objects ("mixed" values), copy / paste / reset of one
// component's values, and which prefab records an instance has changed. Also the actions behind the Inspector's
// right-click menus (duplicate / paste as new / move / reset one value ...; the menu rules themselves are in
// InspectorContextModel.hpp).
#include <Okay.hpp>
#include "InspectorClipboardRules.hpp"
#include "InspectorContextModel.hpp"
#include "okay/Scene/PrefabTools.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace oked {

// ---- Hierarchy search --------------------------------------------------------------
//
// Words are split at spaces and ALL must match. A plain word matches the name, the
// tag, or a kind word (camera, light, mesh, sprite, script, ui ...). Prefixes narrow it:
//   t:enemy        tag contains "enemy"            (tag: works too)
//   c:rigidbody    has a component whose name contains it
//   l:water        layer name contains it
//   is:off | locked | static | prefab | ui          the object's state
// A leading "-" turns a word around ("-t:enemy" = everything not tagged enemy).
// Text with a "/" in it is a path ("Parent/Child") and is kept whole.
struct HierFilter {
    enum class Kind { Word, Tag, Component, Layer, State, Path };
    struct Term { Kind kind = Kind::Word; std::string text; bool negate = false; };
    std::vector<Term> terms;

    static HierFilter Parse(const std::string& text);
    bool Empty() const { return terms.empty(); }
    /// `pathHits`: the objects Scene::FindAllByPath found for a Path term.
    bool Matches(const okay::GameObject& go, const std::unordered_set<const okay::GameObject*>* pathHits = nullptr) const;
    /// The raw text of the Path term, or "" (the caller asks the scene for its hits).
    std::string PathText() const;
};
/// Lower-case words describing what the object is ("camera", "mesh 3d", "script" ...).
std::string HierKindWords(const okay::GameObject& go);
/// Lower-case names of the object's components (registry names, record names, kind words).
std::string HierComponentWords(const okay::GameObject& go);

// ---- Drag and drop in the Hierarchy ---------------------------------------------------
enum class DropWhere { Before, Inside, After };
struct DropHooks {
    /// Called just before / after an object is moved (the editor keeps UI widgets where they were).
    std::function<void(okay::GameObject*)> before, after;
};
/// What dragging `dragged` onto `target` would move, in the order they should end up:
/// drops the target itself, anything that would become its own ancestor, and anything whose
/// parent is also dragged (it travels with its parent); sorted in hierarchy order. `target`
/// null means "the top level" (only meaningful for Inside).
std::vector<okay::GameObject*> PlanDrop(okay::Scene& scene, const std::vector<okay::GameObject*>& dragged,
                                        const okay::GameObject* target, DropWhere where);
/// Do it. Returns how many objects moved. Keeps the world pose. Inside appends as the last child.
int ApplyDrop(okay::Scene& scene, const std::vector<okay::GameObject*>& dragged,
              okay::GameObject* target, DropWhere where, const DropHooks& hooks = {});
/// Position of `go` in the hierarchy as drawn (depth first, roots in scene order).
std::vector<okay::GameObject*> HierarchyOrder(okay::Scene& scene);

// ---- Editing a property on several objects -------------------------------------------
/// One property read across `objs` (each addressed by component type name and the n-th component of it).
struct FieldState {
    int have = 0;          ///< objects that have the component
    bool mixed = false;    ///< they do not all hold the same value
    okay::PropertyValue value;   ///< the first object's value
    std::vector<bool> axisMixed; ///< per float component of a Vec / Color / Quat: that part differs
};
FieldState ReadField(const std::vector<okay::GameObject*>& objs, const std::string& typeName, int index,
                     const okay::PropertyDesc& desc);
/// Write `value` to every object that has the property. Each write is announced as usual.
/// Returns how many objects changed.
int WriteField(const std::vector<okay::GameObject*>& objs, const std::string& typeName, int index,
               const okay::PropertyDesc& desc, const okay::PropertyValue& value);
/// Like WriteField for a vector-like value, but only the float parts in `axisMask` (bit 0 = x ...) are
/// replaced; the other parts of each object keep their own value.
int WriteFieldAxes(const std::vector<okay::GameObject*>& objs, const std::string& typeName, int index,
                   const okay::PropertyDesc& desc, const okay::PropertyValue& value, unsigned axisMask);
/// "Light" or "Light#1" style path of the index-th component of a type.
std::string ComponentPath(const std::string& typeName, int index);
/// Which of several same-type components `c` is (0 for the first).
int ComponentIndexOf(const okay::GameObject& go, const okay::Component& c);

/// Editing a component with its own hand-written Inspector while several objects are selected: take the values
/// before the drawer runs, and after it ran hand every value it changed to the same component (same type, same
/// position among components of that type) of the other selected objects.
struct ComponentEdit {
    bool active = false;
    okay::PropertySnapshot before;
};
ComponentEdit BeginComponentEdit(const std::vector<okay::GameObject*>& selection, const okay::GameObject* primary,
                                 const okay::Component& comp);
/// Returns how many values were written to other objects.
int PropagateComponentEdit(const std::vector<okay::GameObject*>& selection, okay::GameObject* primary,
                           okay::Component& comp, const ComponentEdit& edit);

// ---- Copy, paste, reset a component's values ---------------------------------------------
struct ComponentClip {
    std::string type;   ///< registry type name; empty = nothing copied
    okay::PropertySnapshot values;
    /// A component made from these values saves as the same scene text as the one they were copied from (CheckCopyIsExact). False: the
    /// kind saves something that is not one of its registered values (a particle system's colour gradient), so Paste As New would
    /// make a different component. Paste Values (into a component that exists) is not affected: it only writes the registered values.
    bool exact = true;
    std::string inexactNote;   ///< when not exact: what a copy leaves out, in plain words
    bool Valid() const { return !type.empty(); }
};
ComponentClip CopyComponentValues(const okay::Component& c);
/// Paste into a component of the same type. Returns how many values were written (0 if the types differ).
int PasteComponentValues(const ComponentClip& clip, okay::Component& target);
/// Put every property back to what a freshly added component of that type has. Returns how many values changed.
int ResetComponentValues(okay::Component& c);
/// True when the component's type has registered properties (so Copy / Paste / Reset work on it).
bool HasRegisteredProperties(const okay::Component& c);

// ---- Right-click menu actions: components -----------------------------------------------------
// Written for the Inspector's context menus. The rules for which rows exist and when they are enabled are in
// InspectorContextModel.hpp; these are the operations the rows run (all ImGui-free, so they are tested headless).
/// Where `c` sits among the components after the Transform: index 0 = the first, `count` = how many there are.
/// index is -1 when `c` is not on `go` (or is its Transform).
struct ComponentSlot { int index = -1; int count = 0; };
ComponentSlot SlotOf(const okay::GameObject& go, const okay::Component& c);
/// Move `c` up, down, to the top (right after the Transform) or to the bottom. True when the order changed.
bool MoveComponentTo(okay::GameObject& go, okay::Component& c, okay::inspectorctx::MoveKind kind);
/// The kind of `c` as the Inspector shows it: the registry's name made readable ("Rigidbody 2D"), else its C++ type name.
std::string ComponentDisplayName(const okay::Component& c);
/// The kind of component a registry type name stands for, as shown ("Rigidbody2D" -> "Rigidbody 2D").
std::string TypeDisplayName(const std::string& registryTypeName);
/// A component on `go` that needs `c` (RequireComponent) and would be left without it, as shown; "" when removing `c` is fine.
std::string NeededByName(const okay::GameObject& go, const okay::Component& c);
/// True when another component of `c`'s kind may be added to its object: the registry can make one, and the kind is not "only one
/// per object". "Only one per object" is exactly what the Add Component browser enforces (it hides the row once the object has one:
/// editor/AddComponentRules.hpp, one table for both), plus any okay::ComponentRules::SetUnique rule. `why` gets the reason when not.
bool CanAddAnother(const okay::Component& c, std::string* why = nullptr);

/// What a copy of one component would lose, found by trying it: the object's saved text is loaded into scratch scenes and
///   (1) the original is taken out of one of them and a copy is put in its place (a new component of the kind with the registered
///       values and the on / off state, made the way Duplicate and Paste As New make one): what the object saves must come back the same,
///       record by record. A kind whose saved text holds more than its registered values (a particle system's colour gradient and
///       curves) comes out `exact` false, with the name of the saved record that is not copied in `note`. A record that is one per
///       object, not one per component (a UI object's layout), is there on both sides and does not count;
///   (2) a copy is put next to the original on another scratch object: the file must save it too and load it back as a second one
///       (`twoOnOne`). Some writers keep only one component of a kind per object. Only Duplicate needs this: Paste As New puts
///       the copy on an object that has none.
/// Nothing in the real scene is touched. Costs a few loads of one object's text: call it when a menu opens, not every frame.
struct CopyCheck {
    bool exact = true;      ///< a copy standing where the original stands saves as the same records (Duplicate and Paste As New)
    bool saved = true;      ///< false: the scene file keeps nothing of this kind (it is run-time only), so there was nothing to compare
    bool twoOnOne = true;   ///< a second one on the same object is saved and loads back as a second one (Duplicate)
    std::string note;       ///< not exact: what the copy leaves out, in plain words
    std::string twoNote;    ///< not twoOnOne: why, in plain words
};
CopyCheck CheckCopyIsExact(const okay::GameObject& go, const okay::Component& c);

/// True when `source` may be duplicated: CanAddAnother says yes and the copy would be exact and could be saved next to it
/// (CheckCopyIsExact: `exact` and `twoOnOne`). `why` gets the reason.
bool CanDuplicate(const okay::GameObject& go, const okay::Component& source, std::string* why = nullptr);
/// Add a second component of `source`'s kind to `go` with the same values (the ones Copy Values copies) and the same on / off
/// state, placed right after `source`. Null when it cannot be done (CanDuplicate says why).
okay::Component* DuplicateComponent(okay::GameObject& go, okay::Component& source);
/// True when the clipboard's kind can be added to `go`: the clipboard holds a component, its copy is exact (ComponentClip::exact), the
/// registry can make one, and `go` does not already have one when the kind is "only one per object". `why` gets the reason when not.
bool CanPasteAsNew(const okay::GameObject& go, const ComponentClip& clip, std::string* why = nullptr);
/// Add a new component of the clipboard's kind to `go` and paste the copied values into it. Null when it cannot be done.
okay::Component* PasteComponentAsNew(okay::GameObject& go, const ComponentClip& clip);
/// How many of `objs` Paste As New can really add the clipboard's kind to (the number a menu names). `firstWhy` gets the reason the
/// first object that cannot take it gives, so a menu with no taker can say why.
int PasteAsNewTargets(const std::vector<okay::GameObject*>& objs, const ComponentClip& clip, std::string* firstWhy = nullptr);

// ---- Right-click menu actions: a whole object --------------------------------------------------
/// The component values of one object, without its name, tag, layer or place in the Hierarchy (Transform.parent is left out).
struct ObjectClip {
    std::string from;                ///< the object's name when it was copied
    okay::PropertySnapshot values;
    bool Valid() const { return !values.Empty(); }
};
ObjectClip CopyObjectValues(const okay::GameObject& go);
/// Write the copied values into the components `target` has of the same kinds (matched by kind and position among that
/// kind); components it lacks are skipped, and so is a camera's scale (a camera cannot be scaled, see ScaleIsLocked).
/// Returns how many values were written.
int PasteObjectValues(const ObjectClip& clip, okay::GameObject& target);

// ---- Right-click menu actions: one value -------------------------------------------------------
/// What a new component holds for this property: the registered default, else read from a fresh component. Invalid when unknown.
okay::PropertyValue DefaultValueOf(const okay::ComponentTypeInfo& info, const okay::PropertyDesc& desc);
/// Write the default of the property to every object that has the component (like WriteField). Returns how many changed.
int ResetField(const std::vector<okay::GameObject*>& objs, const std::string& typeName, int index, const okay::PropertyDesc& desc);
/// The property of `info` that a field drawn with this label shows ("Spot Angle##light" -> spotAngle), or null.
const okay::PropertyDesc* FindPropertyForLabel(const okay::ComponentTypeInfo& info, const std::string& label);
/// A camera's scale is not editable (scaling warps the view): the Inspector greys its Scale row out, and every edit that reaches
/// several objects leaves cameras out of a scale.
bool ScaleIsLocked(const okay::GameObject& go);
/// The objects a write to this property reaches: all of `objs`, except the cameras for Transform's scale. `skipped` gets how many
/// were left out. Use it for every field-menu write (Reset to Default, Paste Value, Set to Zero ...), so a multi-selection with a
/// camera in it never scales the camera.
std::vector<okay::GameObject*> WriteTargets(const std::vector<okay::GameObject*>& objs, const std::string& typeName,
                                            const okay::PropertyDesc& desc, int* skipped = nullptr);

/// What Paste Value would do with the text the clipboard held when the menu opened, for one field.
struct PasteCheck {
    bool ok = false;               ///< the text is a value this field takes, and may be pasted
    okay::PropertyValue value;     ///< ok: what would be written, as the field stores it
    std::string preview;           ///< ok: that value as short text for the tooltip
    std::string reason;            ///< not ok: why, in plain words ("" never: there is always a reason)
    bool same = false;             ///< ok, but every selected object already holds it: pasting would change nothing
};
/// Decide Paste Value for `desc`. In order: the clipboard has text of a sensible size on one line (InspectorClipboardRules.hpp), the
/// text reads as this kind of value (PropertyDesc::ParseText), a file path stays inside the project (`projectDir`), and the registry
/// accepts the value (PropertyDesc::Normalize: not nan or infinite, in range, no control characters, a real object id). `current` is
/// the field as it stands on the selected objects, to tell "already holds it".
PasteCheck CheckPaste(const okay::PropertyDesc& desc, const okay::inspectorctx::ClipboardText& clip, const FieldState& current,
                      const std::string& projectDir);

// ---- Right-click menu actions: the Transform ---------------------------------------------------
/// Position, rotation and scale relative to the parent, as Copy Transform Values keeps them.
struct TransformClip {
    bool valid = false;
    okay::Vec3 position{0, 0, 0}, scale{1, 1, 1};
    okay::Quat rotation;
};
constexpr unsigned kTransformPosition = 1u, kTransformRotation = 2u, kTransformScale = 4u, kTransformAll = 7u;
TransformClip CopyTransform(const okay::Transform& t);
/// Write the parts named by `parts` (bit set of kTransform*) of the clip into `t`.
void PasteTransform(const TransformClip& clip, okay::Transform& t, unsigned parts);
/// Put the parts back to position 0, no rotation, scale 1. A camera keeps its scale (it is not editable).
void ResetTransform(okay::GameObject& go, unsigned parts);
/// Which parts of the transform already are at their default (bit set of kTransform*).
unsigned TransformAtDefault(const okay::Transform& t);

// ---- Field labels and asset kinds ---------------------------------------------------------
/// "spotAngle" -> "Spot Angle", "castShadows" -> "Cast Shadows", "uvScale" -> "UV Scale"
std::string PrettyName(const std::string& propertyName);
/// File extensions (lower case, with the dot) of an asset kind a property asks for: "image", "audio", "model",
/// "font", "prefab", "script", "material", "scene". Empty list for "" / "file" / unknown (anything goes).
std::vector<std::string> AssetKindExtensions(const std::string& kind);
/// True when `path` is something a field of that kind accepts. Unknown kinds accept anything.
bool AssetKindAccepts(const std::string& kind, const std::string& path);
/// Project files a field of that kind can use, for the picker: every match under `root`, shortest path first
/// (at most `limit`). Paths are the same strings a drag from the Project panel carries.
std::vector<std::string> ListAssetsOfKind(const std::string& root, const std::string& kind, std::size_t limit = 4000);

// ---- Tooltips for hand-written fields ----------------------------------------------------------
/// The tooltip of a property: the registry's own, else a plain-English default for the commonly used components
/// (Transform, Rigidbody, colliders, Light, Camera, Audio Source, Sprite / Mesh Renderer). "" when there is none.
std::string TooltipFor(const std::string& typeName, const okay::PropertyDesc& desc);
/// The tooltip for a field a hand-written Inspector drew with this label ("Use Temperature##light", "Sort Order"):
/// found by matching the label to a property of the component type. "" when nothing matches.
std::string FieldTooltip(const std::string& typeName, const std::string& label);
/// Names (as shown in the Inspector) of the properties whose value differs between the selected objects, for the
/// component of this type and position. Hidden and runtime-only properties are left out.
std::vector<std::string> MixedPropertyNames(const std::vector<okay::GameObject*>& objs, const std::string& typeName, int index);

// ---- Keeping a prefab root's place --------------------------------------------------------------------
/// Where a top-level object sits among the scene's top-level objects: the one before it (or, for the first, the
/// one after it), by guid. Taken before a revert rebuilds the instance.
struct RootSlot {
    bool valid = false;
    std::string neighbourGuid;
    bool neighbourIsBefore = true;
};
RootSlot CaptureRootSlot(okay::Scene& scene, const okay::GameObject& go);
/// Put `go` (the rebuilt instance) back at that place. No-op for a child (the engine keeps its slot) or when the
/// neighbour is gone.
void RestoreRootSlot(okay::Scene& scene, okay::GameObject* go, const RootSlot& slot);

// ---- Prefab overrides ----------------------------------------------------------------------
/// One changed record of an instance, tied to the live object and component it belongs to.
struct PrefabChange {
    okay::GameObject* object = nullptr;   ///< the live object (null if it cannot be found)
    std::string record;                   ///< "transform", "light" ... (scene-file record name)
    int occurrence = 0;
    int prefabObject = 0;                 ///< PrefabTools index to revert / apply with
    std::string description;              ///< "position.x: 0 -> 4"
    bool added = false, removed = false;
    std::string objectName;               ///< the prefab's name for the object (for lists)
    std::vector<okay::PrefabFieldDiff> fields;   ///< per-field differences (changed records only)
    bool wholeRecordOnly = false;         ///< the two lines differ in shape: only the whole record can be applied / reverted
};
/// All records the instance whose root is `root` has changed. Empty if it is not an instance or the file is missing.
std::vector<PrefabChange> PrefabChangesOf(okay::GameObject& root, std::string* error = nullptr);
/// The change that covers component `c` of `go`, or null.
const PrefabChange* FindChange(const std::vector<PrefabChange>& changes, const okay::GameObject& go, const okay::Component& c);
/// True when `c` (a component of `go`) is changed in the instance compared with its prefab.
bool IsChanged(const std::vector<PrefabChange>& changes, const okay::GameObject& go, const okay::Component& c);
/// The scene-file record name for a component ("light"), or "" when unknown.
std::string RecordNameOf(const okay::Component& c);

} // namespace oked
