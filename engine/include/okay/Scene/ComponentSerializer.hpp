#pragma once
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>

namespace okay {

class GameObject;
class Component;

/// A table of component name -> how to write and read that component in the scene
/// text, so a component can be saved without growing SceneSerializer.cpp's long
/// if/else chain. The chain still handles every component that predates the
/// registry; a registered name is looked up only when the chain does not know a
/// field, and registered components are written after the chain's, so the text
/// format is unchanged. See docs/scene_serializer.md.
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
    };

    /// Add (or replace) an entry. Safe to call from any thread.
    static void Register(Entry entry);
    static std::shared_ptr<const Entry> Find(const std::string& name);
    /// Run every entry's writer, in registration order.
    static void WriteAll(const GameObject& go, std::ostream& out);

    /// A compiler-independent name for a component's C++ type ("SpriteRenderer"),
    /// used to save per-component flags (see the `compoff` record).
    static std::string TypeKey(const Component& c);
};

namespace detail {
/// Defined in SceneSerializer.cpp: registers the components that use the registry.
void RegisterBuiltinComponentSerializers();
}

} // namespace okay
