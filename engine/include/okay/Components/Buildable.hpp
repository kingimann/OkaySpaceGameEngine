#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <string>

namespace okay {

/// Marks an object that players may edit in build mode (see BuildMode) and gives
/// it a stable id. Build mode only moves, rotates, copies, deletes or wires up
/// objects that carry a Buildable, so players can't take the level apart: add one
/// to level props you *want* players to rearrange, and to every palette prefab.
/// Logic links, undo and saved creations refer to objects by `buildId`, which is
/// saved with the scene and with creations (0 = assigned the first time it's needed).
class Buildable : public Behaviour {
public:
    int  buildId  = 0;       ///< stable id (links, undo and creations use it; 0 = not assigned yet)
    bool editable = true;    ///< players may move, rotate, copy, delete and wire it in build mode
    bool placed   = false;   ///< a player placed it in build mode (saved with creations)
    std::string prefab;      ///< palette prefab it was placed from ("" for level objects)

    /// True once build mode removed it. The object lingers until the end-of-frame
    /// destroy (or forever, as an undo shell), so lookups skip it.
    bool Removed() const { return m_removed; }
    void MarkRemoved(bool removed = true) { m_removed = removed; }

    /// The live (not removed) Buildable with `id` in `scene`, or nullptr.
    static Buildable* Find(const Scene& scene, int id) {
        if (id == 0) return nullptr;
        for (const auto& up : scene.Objects()) {
            if (!up || up->IsDestroyed()) continue;
            auto* b = up->GetComponent<Buildable>();
            if (b && b->buildId == id && !b->m_removed) return b;
        }
        return nullptr;
    }
    /// The Buildable that owns `go`: on it, or on its nearest ancestor.
    static Buildable* Of(GameObject* go) {
        for (Transform* t = go ? go->transform : nullptr; t; t = t->Parent())
            if (t->gameObject)
                if (auto* b = t->gameObject->GetComponent<Buildable>()) return b;
        return nullptr;
    }
    /// The highest id in use in `scene` (0 if none).
    static int MaxId(const Scene& scene) {
        int m = 0;
        for (const auto& up : scene.Objects())
            if (up && !up->IsDestroyed())
                if (auto* b = up->GetComponent<Buildable>()) if (b->buildId > m) m = b->buildId;
        return m;
    }

private:
    bool m_removed = false;
};

} // namespace okay
