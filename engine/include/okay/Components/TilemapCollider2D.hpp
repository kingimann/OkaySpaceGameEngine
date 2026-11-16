#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Physics/Collider2D.hpp"
#include <vector>

namespace okay {

/// Generates static collision from a sibling Tilemap: every non-empty tile
/// becomes solid ground. On Start it builds box colliders (merging consecutive
/// solid tiles in each row into one box to keep the count low), so a physics
/// body lands on and is blocked by the tilemap — the basis of a platformer.
///
/// The boxes are "TileCollider" child objects of the tilemap, so they move with it,
/// and they are runtime-only (`transient`): a save never writes them, so saving and
/// loading cannot stack up copies. Build() removes the boxes of the last build first.
class TilemapCollider2D : public Behaviour {
public:
    void Start() override { Build(); }

    /// (Re)generate the collider GameObjects from the current tilemap. The boxes of an
    /// earlier build are removed first, and so are the boxes an older version of the engine
    /// wrote into scenes and saves (root "TileCollider" objects inside this tilemap's area).
    void Build() {
        if (!gameObject || !gameObject->scene()) return;
        Tilemap* map = gameObject->GetComponent<Tilemap>();
        if (!map) return;
        Scene* scene = gameObject->scene();
        RemoveOld(*scene, *map);
        m_count = 0;
        float ts = map->tileSize;

        for (int y = 0; y < map->Height(); ++y) {
            int x = 0;
            while (x < map->Width()) {
                if (map->GetTile(x, y) == 0) { ++x; continue; }
                int start = x;
                while (x < map->Width() && map->GetTile(x, y) != 0) ++x;
                int run = x - start; // number of solid tiles in this strip

                Vec3 origin = transform ? transform->Position() : Vec3::Zero;
                const Vec3 world{origin.x + (start + run * 0.5f) * ts,
                                 origin.y + (y + 0.5f) * ts, 0.0f};
                GameObject* col = scene->CreateGameObject("TileCollider");
                col->transient = true;    // made from the tilemap again on every Start: never saved
                if (transform) {
                    // A child, so it follows the tilemap when it moves. The Tilemap itself places
                    // tiles by its position only, so the box is put at the same world spot, unturned
                    // and unscaled, as before: the collision does not change where it is built.
                    col->transform->SetParent(transform, false);
                    col->transform->SetRotation(Quat::Identity);
                    col->transform->SetLossyScale(Vec3::One);
                    col->transform->SetPosition(world);
                } else {
                    col->transform->localPosition = world;
                }
                auto* bc = col->AddComponent<BoxCollider2D>();
                bc->size = {run * ts, ts}; // no Rigidbody2D -> static solid
                ++m_count;
            }
        }
    }

    /// Number of collider boxes generated (after merging).
    int ColliderCount() const { return m_count; }

private:
    /// A box this component made: a "TileCollider" with only a Transform and a BoxCollider2D.
    static bool IsTileBox(const GameObject* g) {
        if (!g || g->IsDestroyed() || g->name != "TileCollider") return false;
        const auto& cs = g->Components();
        return cs.size() == 2 && g->GetComponent<BoxCollider2D>() != nullptr;
    }

    void RemoveOld(Scene& scene, const Tilemap& map) {
        std::vector<GameObject*> old;
        if (transform)
            for (Transform* t : transform->Children())
                if (t && IsTileBox(t->gameObject)) old.push_back(t->gameObject);
        // Saves from before the boxes were children and transient hold them as saved root objects
        // (one more set per load). Remove those that sit inside this tilemap's area.
        const Vec3 o = transform ? transform->Position() : Vec3::Zero;
        const float ts = map.tileSize;
        const float x0 = o.x, x1 = o.x + map.Width() * ts, y0 = o.y, y1 = o.y + map.Height() * ts;
        const float lo_x = x0 < x1 ? x0 : x1, hi_x = x0 < x1 ? x1 : x0;
        const float lo_y = y0 < y1 ? y0 : y1, hi_y = y0 < y1 ? y1 : y0;
        for (const auto& up : scene.Objects()) {
            GameObject* g = up.get();
            if (!g || g->transient || g == gameObject || !IsTileBox(g)) continue;
            if (!g->transform || g->transform->Parent()) continue;
            const Vec3 p = g->transform->Position();
            if (p.x >= lo_x && p.x <= hi_x && p.y >= lo_y && p.y <= hi_y) old.push_back(g);
        }
        for (GameObject* g : old) scene.Destroy(g);
    }

    int m_count = 0;
};

} // namespace okay
