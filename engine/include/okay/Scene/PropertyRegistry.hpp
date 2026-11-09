#pragma once
// Property reflection, part 2: the registry of component types and their
// properties, path access ("Light.intensity", "Transform.position.x"), the change
// hook, and snapshot / diff / copy utilities. Values and descriptors are in
// Properties.hpp; PropertyBuilder.hpp is the short way to describe a component.
// See docs/properties.md.
#include "okay/Scene/Properties.hpp"
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace okay {

class GameObject;
class Component;

/// One registered component type and its properties. `name` is the C++ type name
/// as ComponentSerializerRegistry::TypeKey gives it ("Light", "Rigidbody2D");
/// `recordName` is the scene-file record the serializer uses ("light",
/// "rigidbody2d"), kept so a tool can map between them. A path may use either.
struct ComponentTypeInfo {
    std::string name;
    std::string recordName;
    std::string category;                 ///< "Rendering", "Physics 2D", ... for an Add Component menu
    std::type_index type{typeid(void)};   ///< the exact dynamic type
    /// The pseudo component "GameObject": its properties are the object's own (name,
    /// active, tag, layer, ...) and are read and written through any component.
    bool objectLevel = false;
    std::vector<PropertyDesc> properties;
    /// Add a component of this type to an object (null when it cannot be added,
    /// like a Transform).
    std::function<Component*(GameObject&)> add;

    /// Exact name first, then any case.
    const PropertyDesc* Find(const std::string& propertyName) const;
};

/// What the change hook is told. `oldValue` and `newValue` are the WHOLE property's
/// values (a Vec3, not the one float a path like "Transform.position.x" changed),
/// so a subscriber can undo by setting `oldValue` back.
struct PropertyChange {
    GameObject* object = nullptr;
    Component* component = nullptr;
    std::shared_ptr<const ComponentTypeInfo> componentInfo;
    const PropertyDesc* property = nullptr;   ///< points into componentInfo
    std::string path;                         ///< "Light.intensity", "Camera#1.depth"
    int componentIndex = 0;                   ///< which of several same-type components
    PropertyValue oldValue;
    PropertyValue newValue;
};

/// A resolved property path.
struct PropertyRef {
    Component* component = nullptr;
    std::shared_ptr<const ComponentTypeInfo> info;
    const PropertyDesc* desc = nullptr;       ///< points into info
    int componentIndex = 0;
    std::string subField;                     ///< "" or "x" / "r" ...
};

/// The table of component types and their properties. Every function is safe to
/// call from any thread; hooks run on the thread that made the change.
///
/// Paths: `Component.property`, optionally `Component#n.property` for the n-th
/// component of that type on the object (0 = the first, the default), and
/// optionally `.x` `.y` `.z` `.w` (or `.r` `.g` `.b` `.a` on a Color) after a
/// vector. The component part is a type name ("Light") or a scene-file record
/// name ("light"), any case. "GameObject.name" reaches the object's own fields.
class PropertyRegistry {
public:
    using ChangeHook = std::function<void(const PropertyChange&)>;

    /// Add a component type, replacing the one with the same name (or, failing that,
    /// the same exact type). Properties with no name, no type or no getter are
    /// dropped. Third-party components call this (see PropertyBuilder.hpp) from startup code.
    static void Register(ComponentTypeInfo info);
    /// By type name or record name (see above). Null when unknown.
    static std::shared_ptr<const ComponentTypeInfo> FindByName(const std::string& name);
    static std::shared_ptr<const ComponentTypeInfo> FindByType(std::type_index type);
    /// The entry for `c`'s exact dynamic type (a subclass is not matched).
    static std::shared_ptr<const ComponentTypeInfo> FindFor(const Component& c);
    /// Every entry, in registration order.
    static std::vector<std::shared_ptr<const ComponentTypeInfo>> All();
    /// Remove every entry and forget that the built-ins were registered, so the next
    /// use registers them again: a fresh process, for tests.
    static void Clear();

    // ---- Path access -------------------------------------------------------
    /// Find the property a path names on `go`. False, with `error` set, when the
    /// component or property is unknown, the object has no such component, or a
    /// sub-field is wrong.
    static bool Resolve(const GameObject& go, const std::string& path, PropertyRef& out, std::string* error = nullptr);
    static bool TryGetProperty(const GameObject& go, const std::string& path, PropertyValue& out, std::string* error = nullptr);
    /// The value, or an invalid PropertyValue when the path does not resolve.
    static PropertyValue GetProperty(const GameObject& go, const std::string& path);
    /// The value as text (an Enum as its option name); "" when it does not resolve.
    static std::string GetPropertyText(const GameObject& go, const std::string& path);
    /// Validate and store a value, then fire the change hook (once, and only if the
    /// stored value actually changed). A sub-field path takes a Float. Returns false
    /// with the component untouched when the path does not resolve, the property is
    /// read-only, or the value is refused (wrong type, out of range with
    /// rejectOutOfRange, not finite, ...). Out-of-range numbers are clamped when
    /// the descriptor says so.
    static bool SetProperty(GameObject& go, const std::string& path, const PropertyValue& value, std::string* error = nullptr);
    /// Parse `text` in the property's type (PropertyDesc::ParseText), then SetProperty.
    static bool SetPropertyFromText(GameObject& go, const std::string& path, const std::string& text, std::string* error = nullptr);
    /// Same, starting from a component and a property name instead of an object and a path.
    static bool SetComponentProperty(Component& c, const std::string& property, const PropertyValue& value, std::string* error = nullptr);
    static PropertyValue GetComponentProperty(const Component& c, const std::string& property);

    // ---- Change hook -------------------------------------------------------
    /// Subscribe. Every successful SetProperty / SetComponentProperty / ApplyPropertySnapshot /
    /// CopyProperties that changed a value calls the hook once per property. Writing a
    /// Derived property announces the stored properties it changed instead (writing
    /// Transform.eulerAngles announces Transform.rotation), so a subscriber sees each
    /// stored value once and can put it back exactly. Returns an id for RemoveChangeHook. A hook may add or remove hooks and may set
    /// properties (that fires the hooks again). Exceptions from a hook are swallowed.
    static int AddChangeHook(ChangeHook hook);
    static void RemoveChangeHook(int id);
    /// While one of these lives on a thread, changes made on that thread fire no
    /// hooks. Undo code wraps its own writes in one so applying a step does not
    /// record another step.
    class ScopedHookSuppression {
    public:
        ScopedHookSuppression();
        ~ScopedHookSuppression();
        ScopedHookSuppression(const ScopedHookSuppression&) = delete;
        ScopedHookSuppression& operator=(const ScopedHookSuppression&) = delete;
    };
};

/// Register every built-in component's properties (GameObject, Transform, Camera,
/// Light, SpriteRenderer, MeshRenderer, Rigidbody2D/3D, the 2D and 3D colliders,
/// AudioSource, ParticleSystem; see docs/properties.md). Idempotent and safe to call
/// from several threads: the work runs once. Lookups call it themselves the first
/// time, so calling it at startup is optional but makes the cost predictable.
void RegisterBuiltinProperties();

namespace detail {
/// Defined in BuiltinProperties.cpp: the table of built-in registrations.
void RegisterBuiltinPropertyTable();
}

// ---- Snapshot, diff, copy ---------------------------------------------------

/// What a snapshot, apply or copy includes. By default only authored data: no
/// read-only, runtime-only or derived properties.
struct PropertySnapshotOptions {
    bool includeReadOnly = false;
    bool includeRuntime = false;
    bool includeDerived = false;
};

/// Every property value of an object at one moment, in a stable order (the
/// GameObject fields, then each component in stored order, each in registration
/// order). Components with no registration are left out. A plain value: store it,
/// compare it, keep it for an undo step or a "revert after Play".
struct PropertySnapshot {
    struct Entry {
        std::string component;    ///< type name ("Light")
        int index = 0;            ///< 0 for the first component of that type, 1 for the second ...
        std::string property;     ///< "intensity"
        PropertyValue value;
        /// "Light.intensity" or "Light#1.intensity".
        std::string Path() const;
    };
    std::vector<Entry> entries;
    bool Empty() const { return entries.empty(); }
    const Entry* Find(const std::string& path) const;
};
PropertySnapshot SnapshotProperties(const GameObject& go, const PropertySnapshotOptions& options = {});
/// One component's properties. Entry.index is 0.
PropertySnapshot SnapshotComponent(const Component& c, const PropertySnapshotOptions& options = {});

enum class PropertyDiffKind { Changed, Added, Removed };

/// One difference between two snapshots. `Changed`: both have the property and the
/// values differ. `Added`: only the second has it (a component the first lacks).
/// `Removed`: only the first has it.
struct PropertyDiff {
    std::string path;
    std::string component;
    int index = 0;
    std::string property;
    PropertyDiffKind kind = PropertyDiffKind::Changed;
    PropertyValue before;   ///< invalid for Added
    PropertyValue after;    ///< invalid for Removed
};
/// What changed going from `a` to `b`: Changed and Added in b's order, then Removed.
std::vector<PropertyDiff> DiffProperties(const PropertySnapshot& a, const PropertySnapshot& b);

/// Write a snapshot's values back (each entry found on `go` by component name and
/// index). Fires the change hook for each value that changes. Returns how many
/// properties were written; entries whose component or property is gone, or whose
/// value is refused, are skipped.
int ApplyPropertySnapshot(GameObject& go, const PropertySnapshot& snapshot);
/// Apply the `Changed` entries of a diff: the `after` values going forward, the
/// `before` values going back (`forward` false). Added and Removed entries are
/// skipped (a diff never adds or removes components). Returns the number written.
int ApplyPropertyDiff(GameObject& go, const std::vector<PropertyDiff>& diff, bool forward = true);

struct PropertyCopyOptions {
    PropertySnapshotOptions what;
    /// Copy the Transform (position, rotation, scale).
    bool includeTransform = true;
    /// Copy GameObject.name / active / tag / layer / ... (off: only components).
    bool includeObjectFields = false;
    /// Copy properties flagged Structural (Transform.parent).
    bool includeStructural = false;
    /// Add a component to the target when the source has one it lacks (needs the
    /// type to be addable). Off: such components are skipped.
    bool addMissingComponents = false;
    /// When not empty, copy only these component type names.
    std::vector<std::string> onlyComponents;
};
/// Copy property values from one object to another, component by component
/// (matched by type and index). Fires the change hook for each value that changes.
/// Returns how many properties were written.
int CopyProperties(const GameObject& from, GameObject& to, const PropertyCopyOptions& options = {});
/// Copy every property of `from` to `to`, which must be the same exact type. Structural
/// properties (Transform.parent) are not copied. Fires the change hook like CopyProperties.
int CopyComponentProperties(const Component& from, Component& to, const PropertySnapshotOptions& options = {});

} // namespace okay
