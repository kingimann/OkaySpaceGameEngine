#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Inventory/InventoryEvents.hpp"
#include <string>
#include <vector>

namespace okay {

/// Who "the player" is for the inventory components (chests, crafting stations): one
/// lookup they all share, in this order —
///   1. the object named `preferred` (a component's playerName field), when set;
///   2. the object tagged "Player" — the local one ("LocalPlayer") in multiplayer;
///   3. the main camera's subject: its CameraFollow target, the object it hides in
///      first person (ignoreObject), or the root it's parented under;
///   4. null — callers then fall back to what they did before.
/// A minimap or cutscene camera is never "the player": only the main camera counts.
inline GameObject* FindInventoryPlayer(const Scene* s, const std::string& preferred = "") {
    if (!s) return nullptr;
    if (!preferred.empty())
        if (GameObject* g = s->Find(preferred)) return g;
    GameObject* tagged = nullptr;
    for (const auto& up : s->Objects()) {
        GameObject* g = up.get();
        if (!g || g->tag != "Player") continue;
        if (g->name == "LocalPlayer") return g;   // NetworkPlayerSpawner's own player
        if (!tagged) tagged = g;
    }
    if (tagged) return tagged;
    Camera* cam = s->mainCamera;
    GameObject* cg = cam ? cam->gameObject : nullptr;
    if (!cg) return nullptr;
    if (auto* f = cg->GetComponent<CameraFollow>())
        if (!f->targetName.empty())
            if (GameObject* g = s->Find(f->targetName)) return g;
    if (cam->ignoreObject) return cam->ignoreObject;
    if (cg->transform && cg->transform->Parent()) {
        Transform* t = cg->transform;
        while (t->Parent()) t = t->Parent();
        if (t->gameObject) return t->gameObject;
    }
    return nullptr;
}

/// Where to measure "in range" from: the player's position, else the main camera's.
/// False when neither exists.
inline bool InventoryPlayerPosition(const Scene* s, const std::string& preferred, Vec3& out) {
    if (GameObject* p = FindInventoryPlayer(s, preferred))
        if (p->transform) { out = p->transform->Position(); return true; }
    if (s && s->mainCamera && s->mainCamera->gameObject && s->mainCamera->gameObject->transform) {
        out = s->mainCamera->gameObject->transform->Position();
        return true;
    }
    return false;
}

/// Something the player opens with a key when standing near it: a chest, a crafting
/// bench. Pressing the key toggles only the NEAREST in-range one that uses that key and
/// closes the rest, so two chests side by side never open together; walking out of
/// range closes it. range 0 = no distance limit (still only the nearest opens).
class InventoryInteractable : public Behaviour {
public:
    bool  open  = false;
    float range = 2.5f;          ///< how close the player must be (0 = any distance)
    std::string playerName;      ///< who counts as the player (empty = FindInventoryPlayer's default)

    virtual char InteractKey() const = 0;

    /// Squared distance to the player, or -1 if there's no player/camera to measure from.
    float PlayerDistanceSq() const {
        Vec3 p;
        if (!gameObject || !gameObject->transform || !InventoryPlayerPosition(gameObject->scene(), playerName, p)) return -1.0f;
        const Vec3 a = gameObject->transform->Position();
        const float dx = a.x - p.x, dy = a.y - p.y, dz = a.z - p.z;
        return dx * dx + dy * dy + dz * dz;
    }
    /// Is the player close enough? True with no range limit, or nothing to measure from.
    bool PlayerInRange() const {
        if (range <= 0.0f) return true;
        const float d = PlayerDistanceSq();
        return d < 0.0f || d <= range * range;
    }

    /// The in-range interactable nearest the player among those using `key` (null if none).
    static InventoryInteractable* Nearest(const Scene* s, char key) {
        if (!s || !key) return nullptr;
        InventoryInteractable* best = nullptr;
        float bestD = 0.0f;
        for (const auto& up : s->Objects()) {
            if (!up || !up->active) continue;
            for (InventoryInteractable* c : up->GetComponents<InventoryInteractable>()) {
                if (!c->enabled || c->InteractKey() != key || !c->PlayerInRange()) continue;
                const float d = c->PlayerDistanceSq();
                if (!best || d < bestD) { best = c; bestD = d; }
            }
        }
        return best;
    }

    void Update(float) override {
        const char key = InteractKey();
        if (key && Input::GetKeyDown(key)) {
            // Every instance makes the same choice, whatever order they update in.
            open = (Nearest(gameObject ? gameObject->scene() : nullptr, key) == this) ? !open : false;
        }
        if (open && !PlayerInRange()) open = false;   // auto-close when you walk away
        if (open != m_wasOpen) {   // on_chest_opened / on_chest_closed(name), to the chest and the player
            m_wasOpen = open;
            const char* ev = open ? "on_chest_opened" : "on_chest_closed";
            const std::vector<vs::VsValue> args{vs::VsValue(gameObject ? gameObject->name : std::string())};
            InventoryEvents::Fire(gameObject, ev, nullptr, args);
            GameObject* player = FindInventoryPlayer(gameObject ? gameObject->scene() : nullptr, playerName);
            if (player && player != gameObject) InventoryEvents::Fire(player, ev, nullptr, args);
        }
    }

private:
    bool m_wasOpen = false;
};

} // namespace okay
