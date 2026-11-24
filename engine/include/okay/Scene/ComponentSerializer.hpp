#pragma once
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class GameObject;
class Component;
struct ComponentTypeInfo;

/// A table of component name -> how to write and read that component in the scene
/// text, so a component can be saved without growing SceneSerializer.cpp's long
/// if/else chain. The chain still handles every component that predates the
/// registry; a registered name is looked up only when the chain does not know a
/// field, and registered components are written after the chain's, so the text
/// format is unchanged. See docs/engine/scene_serializer.md.
class ComponentSerializerRegistry {
public:
    struct Entry {
        /// The record name, e.g. "trailrenderer". Must be a single token and must
        /// not be a name the built-in chain already uses.
        std::string name;
        /// Write this component of `go` as whole lines starting with two spaces and
        /// `name` ("  name ...\n"). Write nothing when `go` has no such component.
        std::function<void(const GameObject& go, std::ostream& out)> write;
        /// Read the record. `in` starts just after the name and holds the rest of
        /// that one line only; add the component to `go`.
        std::function<void(GameObject& go, std::istream& in)> read;
        /// Optional, used instead of `read` when set: the same, but it may add notes
        /// (an unknown key it skipped, a value it could not use) that the loader adds
        /// to its warnings. `in` holds the rest of the record, which may run over
        /// several lines while a quoted value is open.
        std::function<void(GameObject& go, std::istream& in, std::vector<std::string>& notes)> readWithNotes;
        /// The scene chain already writes this record at its old place and calls `write`
        /// itself. WriteAll skips it, so the line is not written twice and the file stays
        /// byte-identical.
        bool inChain = false;
    };

    /// Add (or replace) an entry. Safe to call from any thread. Needs a name, a
    /// writer and a reader (`read` or `readWithNotes`).
    static void Register(Entry entry);
    /// Take an entry out again (a module that goes away at run time; tests). Its record is then kept as
    /// unknown text by a load. Nothing happens when the name is not registered.
    static void Unregister(const std::string& name);
    static std::shared_ptr<const Entry> Find(const std::string& name);
    /// Run every entry's writer, in registration order.
    static void WriteAll(const GameObject& go, std::ostream& out);

    /// A compiler-independent name for a component's C++ type ("SpriteRenderer"),
    /// used to save per-component flags (see the `compoff` record).
    static std::string TypeKey(const Component& c);

    // ---- The generic path: a component saved from its PropertyRegistry entry ----------------
    /// Save and load the component type `typeName` (a PropertyRegistry name, e.g.
    /// "CheckpointTrigger") as one record of named fields, built from its registry entry:
    ///   `  <recordName> key=value key="text" ...`
    /// One record per component of that exact type, in the order they are stored. Every
    /// authored property is written (not read-only, runtime-only or derived; `enabled` is
    /// kept by the `compoff` record as for every component). Reading is reorder-safe:
    /// fields may come in any order, a key the entry does not know is skipped with a note,
    /// a missing key leaves the fresh component's default, and a value the property refuses
    /// keeps the default with a note. So a new component needs no branch in the serializer.
    /// Refused (false, with `error`) when the type is not registered, has no `add`, is the
    /// object-level entry, or has no usable record name: empty, not lower-case letters and
    /// digits, or one the built-in chain already reads (an existing record is never taken over).
    static bool RegisterFromProperties(const std::string& typeName, std::string* error = nullptr);
    /// The fields of one component as RegisterFromProperties writes them: " key=value ..."
    /// (each with a leading space, no record name, no line break).
    static void WritePropertyFields(const Component& c, const ComponentTypeInfo& info, std::ostream& out);
    /// Read such fields into `c` (see RegisterFromProperties for the rules). Returns how many
    /// properties were set; adds a note for every key skipped and every value refused.
    static int ReadPropertyFields(Component& c, const ComponentTypeInfo& info, std::istream& in, std::vector<std::string>* notes = nullptr);
};

namespace detail {
/// Defined in SceneSerializer.cpp: registers the components that use the registry.
void RegisterBuiltinComponentSerializers();
void RegisterColliderShape2DSerializers();   // the Composite 2D collider (src/Physics/ColliderShapes2D.cpp)
void RegisterColliderShapeSerializers();   // the Cone / Convex Hull / Plane / Compound / Heightfield colliders (src/Physics/ColliderShapes3D.cpp)
void RegisterPhysicsToySerializers();   // Physics Surface and the physics toys (src/Physics/PhysicsToys.cpp)
}

} // namespace okay
