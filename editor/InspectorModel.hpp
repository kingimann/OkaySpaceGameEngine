#pragma once
// The logic behind the Hierarchy and Inspector windows, without any drawing, so it
// can be tested: the Hierarchy search filter, drag-and-drop reparenting, editing the
// same property on several objects ("mixed" values), copy / paste / reset of one
// component's values, and which prefab records an instance has changed.
#include <Okay.hpp>
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

// ---- Copy, paste, reset a component's values ---------------------------------------------
struct ComponentClip {
    std::string type;   ///< registry type name; empty = nothing copied
    okay::PropertySnapshot values;
    bool Valid() const { return !type.empty(); }
};
ComponentClip CopyComponentValues(const okay::Component& c);
/// Paste into a component of the same type. Returns how many values were written (0 if the types differ).
int PasteComponentValues(const ComponentClip& clip, okay::Component& target);
/// Put every property back to what a freshly added component of that type has. Returns how many values changed.
int ResetComponentValues(okay::Component& c);
/// True when the component's type has registered properties (so Copy / Paste / Reset work on it).
bool HasRegisteredProperties(const okay::Component& c);

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

// ---- Prefab overrides ----------------------------------------------------------------------
/// One changed record of an instance, tied to the live object and component it belongs to.
struct PrefabChange {
    okay::GameObject* object = nullptr;   ///< the live object (null if it cannot be found)
    std::string record;                   ///< "transform", "light" ... (scene-file record name)
    int occurrence = 0;
    int prefabObject = 0;                 ///< PrefabTools index to revert / apply with
    std::string description;              ///< "position.x: 0 -> 4"
    bool added = false, removed = false;
};
/// All records the instance whose root is `root` has changed. Empty if it is not an instance or the file is missing.
std::vector<PrefabChange> PrefabChangesOf(okay::GameObject& root, std::string* error = nullptr);
/// True when `c` (a component of `go`) is changed in the instance compared with its prefab.
bool IsChanged(const std::vector<PrefabChange>& changes, const okay::GameObject& go, const okay::Component& c);
/// The scene-file record name for a component ("light"), or "" when unknown.
std::string RecordNameOf(const okay::Component& c);

} // namespace oked
