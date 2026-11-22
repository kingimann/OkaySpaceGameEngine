#pragma once
// An undo step for values written through the property registry (ImGui-free, so it is tested headless).
//
// The Inspector writes most component values with PropertyRegistry (InspectorModel's WriteField). Such an edit is
// recorded as a PropertyEditCommand: for each changed property, the object's GUID, the property path
// ("Light.intensity", "Camera#1.depth"), and the whole value before and after. Undo and redo set those values back
// through the registry on the live objects. Nothing is rebuilt, so every GameObject* and Component* stays valid
// (the whole-scene TextDelta in SceneUndo.hpp rebuilds the scene and gives every object a new address).
//
// The object is found again by GUID on every Undo / Redo, so a step still works after an earlier undo deleted the
// object and a later one brought it back (same GUID, new pointer). If an object or property cannot be found, or a
// value is refused, nothing is left half done (the values already written are put back) and the command fails;
// the stack then drops it as stale.
//
// Written from the engine's own interfaces (okay::ICommand in Core/Undo.hpp, PropertyRegistry in
// Scene/PropertyRegistry.hpp); no outside code.
#include "okay/Core/Undo.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace okay::editor {

/// One property of one object, before and after an edit.
struct PropertyEditEntry {
    std::string guid;        ///< GameObject::Guid
    std::string path;        ///< the whole stored property: "Light.intensity", "Light#1.color"
    PropertyValue before;
    PropertyValue after;
};

/// How a property command reaches the live scene. Owned by the editor state and shared by its commands.
struct PropertyAccess {
    std::function<GameObject*(const std::string& guid)> find;   ///< the live object with that GUID, or null
    std::function<void()> changed;                              ///< after values were written (the editor resyncs its scene text)
};

class PropertyEditCommand : public ICommand {
public:
    PropertyEditCommand(std::string name, std::shared_ptr<PropertyAccess> access, std::vector<PropertyEditEntry> entries)
        : m_name(std::move(name)), m_access(std::move(access)), m_entries(std::move(entries)) {}

    bool Do() override { return Apply(true); }
    bool Undo() override { return Apply(false); }
    std::string Name() const override { return m_name; }
    bool IsNoOp() const override {
        for (const PropertyEditEntry& e : m_entries) if (e.before != e.after) return false;
        return true;
    }
    std::size_t MemoryBytes() const override {
        std::size_t n = sizeof(*this) + m_name.capacity();
        for (const PropertyEditEntry& e : m_entries)
            n += sizeof(e) + e.guid.capacity() + e.path.capacity() + e.before.AsString().size() + e.after.AsString().size();
        return n;
    }
    const std::vector<PropertyEditEntry>& Entries() const { return m_entries; }

    /// Write `forward ? after : before` for every entry onto the objects `find` gives. All or nothing; the registry's
    /// change hooks are not told (this is undo applying a step, not a new edit). False when anything was missing.
    static bool Write(const std::vector<PropertyEditEntry>& entries, const std::function<GameObject*(const std::string&)>& find, bool forward) {
        if (!find) return false;
        std::vector<GameObject*> objs;
        objs.reserve(entries.size());
        for (const PropertyEditEntry& e : entries) {
            GameObject* go = find(e.guid);
            if (!go) return false;
            objs.push_back(go);
        }
        PropertyRegistry::ScopedHookSuppression quiet;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const PropertyEditEntry& e = entries[i];
            if (PropertyRegistry::SetProperty(*objs[i], e.path, forward ? e.after : e.before)) continue;
            for (std::size_t k = i; k-- > 0;)   // put back what was already written
                PropertyRegistry::SetProperty(*objs[k], entries[k].path, forward ? entries[k].before : entries[k].after);
            return false;
        }
        return true;
    }

private:
    bool Apply(bool forward) {
        if (!m_access || !Write(m_entries, m_access->find, forward)) return false;
        if (m_access->changed) m_access->changed();
        return true;
    }
    std::string m_name;
    std::shared_ptr<PropertyAccess> m_access;
    std::vector<PropertyEditEntry> m_entries;
};

}  // namespace okay::editor
