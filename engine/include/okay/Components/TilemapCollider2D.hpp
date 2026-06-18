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
class TilemapCollider2D : public Behaviour {
public:
    void Start() override { Build(); }

    /// (Re)generate the collider GameObjects from the current tilemap.
    void Build() {
        if (!gameObject || !gameObject->scene()) return;
        Tilemap* map = gameObject->GetComponent<Tilemap>();
        if (!map) return;
        Scene* scene = gameObject->scene();
        float ts = map->tileSize;

        for (int y = 0; y < map->Height(); ++y) {
            int x = 0;
            while (x < map->Width()) {
                if (map->GetTile(x, y) == 0) { ++x; continue; }
                int start = x;
                while (x < map->Width() && map->GetTile(x, y) != 0) ++x;
                int run = x - start; // number of solid tiles in this strip

                Vec3 origin = transform ? transform->Position() : Vec3::Zero;
                GameObject* col = scene->CreateGameObject("TileCollider");
                col->transform->localPosition = {
                    origin.x + (start + run * 0.5f) * ts,
                    origin.y + (y + 0.5f) * ts, 0.0f};
                auto* bc = col->AddComponent<BoxCollider2D>();
                bc->size = {run * ts, ts}; // no Rigidbody2D -> static solid
                ++m_count;
            }
        }
    }

    /// Number of collider boxes generated (after merging).
    int ColliderCount() const { return m_count; }

private:
    int m_count = 0;
};

} // namespace okay
