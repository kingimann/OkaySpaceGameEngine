#pragma once
// A reference to a scene object that survives the scene being rebuilt (ImGui-free, so it is tested headless).
//
// Undo, redo, opening a scene and prefab mode rebuild every object from the scene text: the old objects are freed
// and new ones are made, and a new object can land at the very address an old one had. So an editor window that
// keeps a raw GameObject* across frames, and checks "is this pointer still in the scene?", can end up showing or
// editing a different object (the Inspector lock showed the wrong object after Ctrl+Z, #inspector-lock-undo).
//
// ObjectRef keeps the object's GUID instead (GameObject::Guid, which the scene text stores, so undo brings an object
// back with the same one) and looks the object up again each time it is used. When no live object has that GUID,
// Get() returns null: the reference never points at a stand-in.
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <string>

namespace okay::editor {

class ObjectRef {
public:
    ObjectRef() = default;
    explicit ObjectRef(const GameObject* go) { Set(go); }

    /// Point at `go` (null clears). Objects with no GUID (none should exist) cannot be held and clear the reference.
    void Set(const GameObject* go) { m_guid = go ? go->Guid() : std::string(); }
    void Clear() { m_guid.clear(); }
    /// True when something was set (whether or not it still exists).
    bool IsSet() const { return !m_guid.empty(); }
    const std::string& Guid() const { return m_guid; }

    /// The live object in `scene` with the stored GUID, or null when it is gone (deleted, undone, another scene).
    /// Destroyed and transient (editor-only helper) objects never match.
    GameObject* Get(const Scene& scene) const {
        if (m_guid.empty()) return nullptr;
        GameObject* go = scene.FindByGuid(m_guid);
        if (!go || go->IsDestroyed() || go->transient) return nullptr;
        return go;
    }
    /// Set and is gone: the object it named does not exist in `scene` now.
    bool IsMissing(const Scene& scene) const { return IsSet() && !Get(scene); }
    /// True when this names `go` (by GUID).
    bool Is(const GameObject* go) const { return go && !m_guid.empty() && go->Guid() == m_guid; }

private:
    std::string m_guid;
};

/// The Inspector's "Lock Inspector" state. While locked, the Inspector shows the pinned object instead of the
/// selection. The pin is held by GUID (ObjectRef), so undo / redo / reloads find the same object again, and an
/// object that took the old one's memory is never shown in its place.
///
/// When the pinned object no longer exists the lock STAYS ON and the Inspector shows nothing, with a note saying the
/// locked object is gone (and an Unlock button). It does not fall back to the selection: a locked Inspector must never
/// show an object the user did not lock. If a later undo / redo brings the object back (same GUID) it shows again.
struct InspectorLock {
    bool locked = false;
    ObjectRef pinned;

    void Lock(const GameObject* go) { locked = go != nullptr; pinned.Set(go); }
    void Unlock() { locked = false; pinned.Clear(); }

    struct View {
        GameObject* object = nullptr;   ///< what the Inspector shows (null = nothing)
        bool lockedMissing = false;     ///< locked, but the locked object is gone: say so
    };
    /// What the Inspector shows this frame, given the current primary selection.
    View Resolve(const Scene& scene, GameObject* selected) const {
        View v;
        if (!locked) { v.object = selected; return v; }
        v.object = pinned.Get(scene);
        v.lockedMissing = v.object == nullptr;
        return v;
    }
};

} // namespace okay::editor
