#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Physics/Collider2D.hpp"

namespace okay {

/// Generates static collision from a sibling Tilemap: every non-empty tile
/// becomes solid ground. On Start it builds box colliders (merging consecutive
/// solid tiles in each row into one box to keep the count low), so a physics
/// body lands on and is blocked by the tilemap — the basis of a platformer.
/// The boxes are runtime-only children of the tilemap (`transient`): they move
/// with it, are never written to a scene or save, and Build() replaces them
/// (#888 F54: they were saved root objects, one more set on every load).
class TilemapCollider2D : public Behaviour {
public:
    void Start() override { Build(); }

    /// (Re)generate the collider GameObjects from the current tilemap: the boxes of the last build go first.
    void Build() {
        if (!gameObject || !gameObject->scene() || !transform) return;
        Scene* scene = gameObject->scene();
        const std::vector<Transform*> kids = transform->Children();   // a copy: Destroy may change the list
        for (Transform* k : kids)
            if (k && k->gameObject && k->gameObject->transient && k->gameObject->name == kBoxName && !scene->IsDestroyPending(k->gameObject))
                scene->Destroy(k->gameObject);
        m_count = 0;
        Tilemap* map = gameObject->GetComponent<Tilemap>();
        if (!map) return;
        float ts = map->tileSize;

        for (int y = 0; y < map->Height(); ++y) {
            int x = 0;
            while (x < map->Width()) {
                if (map->GetTile(x, y) == 0) { ++x; continue; }
                int start = x;
                while (x < map->Width() && map->GetTile(x, y) != 0) ++x;
                int run = x - start; // number of solid tiles in this strip

                GameObject* col = scene->CreateGameObject(kBoxName);
                col->transient = true;
                col->transform->SetParent(transform, false);
                col->transform->localPosition = {(start + run * 0.5f) * ts, (y + 0.5f) * ts, 0.0f};
                auto* bc = col->AddComponent<BoxCollider2D>();
                bc->size = {run * ts, ts}; // no Rigidbody2D -> static solid
                ++m_count;
            }
        }
    }

    /// Number of collider boxes generated (after merging).
    int ColliderCount() const { return m_count; }

private:
    static constexpr const char* kBoxName = "TileCollider";
    int m_count = 0;
};

} // namespace okay
