#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/NetInventory.hpp"
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Render/Mesh.hpp"
#include <string>

namespace okay {

/// An item lying in the world. Walking into it (its trigger collider), coming within
/// `autoRadius`, or pressing `interactKey` within `interactRange` puts it in the
/// collector's Inventory — as much as fits; whatever doesn't fit stays on the ground.
/// Optionally drifts toward the player inside `magnetRadius`. Items dropped from an
/// inventory spawn one of these (Spawn), so nothing dropped is ever lost.
class ItemPickup : public Behaviour {
public:
    std::string item;                 ///< ItemDatabase id
    int   count = 1;
    ItemInstanceData data;            ///< durability / uid / values carried by this item
    std::string collectorTag = "Player";   ///< who can pick it up (tag or name; empty = anyone with an Inventory)
    bool  pickupOnTouch = true;       ///< collect when the collector enters its trigger
    float autoRadius = 0.0f;          ///< collect when the player is this close (0 = off)
    char  interactKey = 0;            ///< press near it to collect (0 = off)
    float interactRange = 2.0f;
    float magnetRadius = 0.0f;        ///< drift toward the player inside this range (0 = off)
    float magnetSpeed = 6.0f;
    /// Pressing interactKey picks it up in the hands (ProceduralObjectHolding) instead of
    /// putting it in the bag, when the player can hold objects. While held it isn't collected.
    bool  holdInHands = false;

    /// The rest of this frame ignores interactKey. A drop on that same press (E again)
    /// would otherwise put the object straight back in the hands.
    static void BlockInteract() { InteractBlocked() = true; }
    static void ClearInteractBlock() { InteractBlocked() = false; }

    void OnTriggerEnter2D(Collider2D* o) override { if (pickupOnTouch && o) GiveTo(o->gameObject); }
    void OnTriggerEnter3D(Collider3D* o) override { if (pickupOnTouch && o) GiveTo(o->gameObject); }
    void OnCollisionEnter2D(const Collision2D& c) override { if (pickupOnTouch) GiveTo(c.gameObject); }
    void OnCollisionEnter3D(const Collision3D& c) override { if (pickupOnTouch) GiveTo(c.gameObject); }

    /// Put this item into `who`'s Inventory (on it or its root) if `who` is a collector
    /// (collectorTag). Returns how many moved; the pickup is destroyed once it's empty.
    int GiveTo(GameObject* who) {
        if (Held()) return 0;
        if (!who || (!collectorTag.empty() && who->tag != collectorTag && who->name.find(collectorTag) == std::string::npos)) return 0;
        return Give(who);
    }

    void Update(float dt) override {
        // A previous hold must not stay kinematic after the hands have moved on.
        if (auto* ho = gameObject ? gameObject->GetComponent<HoldableObject>() : nullptr) {
            if (GameObject* who = ho->Holder()) {
                auto* hands = who->GetComponent<ProceduralObjectHolding>();
                if (!hands || hands->Held() != gameObject) ho->ReleaseHold();
            }
        }
        if (m_gone || !gameObject || !gameObject->transform || Held()) return;
        GameObject* player = FindInventoryPlayer(gameObject->scene());
        if (!player || !player->transform) return;
        const Vec3 me = gameObject->transform->Position(), p = player->transform->Position();
        const Vec3 d = p - me;
        const float dist = d.Magnitude();
        if (autoRadius > 0.0f && dist <= autoRadius) { Give(player); return; }
        if (interactKey && dist <= interactRange && Input::GetKeyDown(interactKey) && !InteractBlocked()
            && NearestHold(player, dist)) {
            auto* hands = holdInHands ? player->GetComponent<ProceduralObjectHolding>() : nullptr;
            if (hands && hands->Pickup(gameObject)) {
                BlockInteract();   // this press is used; a second prop must not take it
                return;
            }
            Give(player);
            return;
        }
        if (magnetRadius > 0.0f && dist <= magnetRadius && dist > 0.05f) {
            const float step = magnetSpeed * dt;
            gameObject->transform->SetPosition(step >= dist ? p : me + d * (step / dist));
        }
    }

    void LateUpdate(float) override { ClearInteractBlock(); }

    /// Lower is a better pickup: closest to the camera aim, then nearer to the camera.
    static float AimScore(const Vec3& point, GameObject* player) {
        Vec3 origin = player && player->transform ? player->transform->Position() : Vec3::Zero;
        Vec3 dir = player && player->transform ? player->transform->Rotation() * Vec3{0.0f, 0.0f, -1.0f} : Vec3{0.0f, 0.0f, -1.0f};
        if (player && player->scene() && player->scene()->mainCamera && player->scene()->mainCamera->transform) {
            Transform* ct = player->scene()->mainCamera->transform;
            origin = ct->Position();
            dir = ct->Rotation() * Vec3{0.0f, 0.0f, -1.0f};
        }
        const float dl = dir.Magnitude();
        dir = dl > 1e-6f ? dir * (1.0f / dl) : Vec3{0.0f, 0.0f, -1.0f};
        const Vec3 w = point - origin;
        const float along = Vec3::Dot(w, dir);
        const float t = along > 0.0f ? along : 0.0f;
        const float offRay = (point - (origin + dir * t)).Magnitude();
        return offRay * 1000.0f + w.Magnitude();
    }

    /// Someone is holding it in their hands right now.
    bool Held() const {
        const auto* h = gameObject ? gameObject->GetComponent<HoldableObject>() : nullptr;
        return h && h->Holder();
    }

    /// The Inventory on `who`, else on its root.
    static Inventory* InventoryOf(GameObject* who) {
        if (!who) return nullptr;
        if (auto* in = who->GetComponent<Inventory>()) return in;
        Transform* t = who->transform;
        while (t && t->Parent()) t = t->Parent();
        return (t && t->gameObject) ? t->gameObject->GetComponent<Inventory>() : nullptr;
    }

    /// Drop `count` of `item` into the world at `pos`: the definition's world prefab
    /// (with an ItemPickup added if it has none), else a small cube showing its icon.
    static GameObject* Spawn(Scene& scene, const std::string& item, int count, const ItemInstanceData& data, const Vec3& pos) {
        if (item.empty() || count <= 0) return nullptr;
        const ItemDefinition* def = ItemDatabase::Get().Def(item);
        GameObject* go = nullptr;
        if (def && !def->worldPrefab.empty()) go = SceneSerializer::InstantiateFromFile(scene, def->worldPrefab);
        if (!go) {
            go = scene.CreateGameObject(def ? def->DisplayName() : item);
            auto* mr = go->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Cube();
            mr->texture = ItemIconPath("", item);
            go->transform->localScale = {0.4f, 0.4f, 0.4f};
            go->AddComponent<BoxCollider3D>()->isTrigger = true;
        }
        go->transform->SetPosition(pos);
        auto* pk = go->GetComponent<ItemPickup>();
        if (!pk) pk = go->AddComponent<ItemPickup>();
        pk->item = item; pk->count = count; pk->data = data;
        return go;
    }

private:
    int Give(GameObject* who) {
        if (m_gone || !who || who == gameObject || item.empty() || count <= 0) return 0;
        if (NetInventory::DeferPickup(this, who)) return 0;   // the host grants it; the first request wins
        Inventory* inv = InventoryOf(who);
        if (!inv) return 0;
        Inventory carried;   // move through InventoryOps: partial fit and one-off items handled once
        carried.slots.push_back(Inventory::Slot{item, count, data});
        const InvResult r = InventoryOps::Transfer(&carried, 0, inv);
        if (r.moved < count) inv->NotifyFull(item, count - r.moved);
        if (!r.Ok()) return 0;
        InventoryEvents::Fire(inv->gameObject, "on_item_picked_up", nullptr, {vs::VsValue(item), vs::VsValue((float)r.moved)});
        count -= r.moved;
        if (count <= 0) {
            m_gone = true;
            if (gameObject && gameObject->scene()) gameObject->scene()->Destroy(gameObject);
        }
        return r.moved;
    }

    bool m_gone = false;

    static bool& InteractBlocked() { static bool blocked = false; return blocked; }

    /// Hold-in-hands props share one key. The one closest to the aim ray takes it
    /// (distance breaks a tie), so a block sitting beside a crate does not steal
    /// the press when the crosshair is on the crate.
    bool NearestHold(GameObject* player, float dist) const {
        if (!holdInHands || !player || !player->transform || !gameObject || !gameObject->transform) return true;
        Scene* sc = gameObject->scene();
        if (!sc) return true;
        const float mine = AimScore(gameObject->transform->Position(), player);
        const Vec3 p = player->transform->Position();
        for (auto* other : sc->FindObjectsOfType<ItemPickup>()) {
            if (!other || other == this || !other->enabled || !other->holdInHands || !other->interactKey || other->Held()) continue;
            GameObject* go = other->gameObject;
            if (!go || !go->active || !go->transform) continue;
            const float od = (go->transform->Position() - p).Magnitude();
            if (od > other->interactRange) continue;
            if (AimScore(go->transform->Position(), player) + 1e-3f < mine) return false;
        }
        (void)dist;
        return true;
    }
};

} // namespace okay
