#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Mesh.hpp"
#include <cmath>
#include <string>

namespace okay {

/// Minecraft-style voxel building: look at a surface and click to place a cube on a
/// grid; right-click removes the block you're aiming at. Attach it to the player (or
/// any object) — it casts a ray from the scene's main camera (centre-screen, like a
/// crosshair), snaps to a grid, and spawns/destroys block GameObjects at runtime, so
/// you can build maps live in Play. Placed blocks are real objects (mesh + collider)
/// tagged `blockTag`, so they save with the scene and only those are removable.
class BlockBuilder : public Behaviour {
public:
    float       blockSize   = 1.0f;                       ///< grid cell / cube size (world units)
    float       reach       = 6.0f;                       ///< max place/remove distance
    Color       blockColor  = Color::FromBytes(170, 172, 182);
    std::string blockTexture;                             ///< optional texture for placed blocks
    std::string blockTag    = "Block";                   ///< tag placed blocks carry (only these are removable)
    int         placeButton  = 0;                         ///< mouse button to place (0 = left)
    int         removeButton = 1;                         ///< mouse button to remove (1 = right)

    void Update(float) override {
        if (!gameObject) return;
        Scene* s = GetScene();
        if (!s || !s->mainCamera || !s->mainCamera->gameObject || !s->mainCamera->gameObject->transform) return;
        Transform* cam = s->mainCamera->gameObject->transform;
        const bool place  = Input::GetMouseButtonDown(placeButton);
        const bool remove = Input::GetMouseButtonDown(removeButton);
        if (!place && !remove) return;

        Build(*s, cam->Position(), cam->Forward(), place, remove);
    }

    /// Place or remove a block along a ray (camera-independent, so it's unit-testable).
    /// Returns the block placed/removed, or nullptr. `place` wins over `remove`.
    GameObject* Build(Scene& s, const Vec3& origin, const Vec3& dir, bool place, bool remove) {
        RaycastHit3D hit = s.physics3D().Raycast(s, origin, dir, reach, gameObject);
        if (place) {
            // Place against the hit face (or at arm's length into empty space), snapped.
            Vec3 target = hit.hit ? hit.point + hit.normal * (blockSize * 0.5f)
                                  : origin + dir * reach;
            Vec3 cell = Snap(target);
            return Occupied(s, cell) ? nullptr : PlaceBlock(s, cell);
        }
        if (remove && hit.hit && hit.gameObject && hit.gameObject->tag == blockTag) {
            GameObject* g = hit.gameObject;
            s.Destroy(g);   // only blocks we placed, never the world
            return g;
        }
        return nullptr;
    }

    /// Grid-snap a world point to the nearest cell centre (exposed for tests).
    Vec3 Snap(const Vec3& p) const {
        float g = blockSize > 1e-4f ? blockSize : 1.0f;
        return Vec3{std::round(p.x / g) * g, std::round(p.y / g) * g, std::round(p.z / g) * g};
    }

private:
    bool Occupied(Scene& s, const Vec3& cell) const {
        float e = blockSize * 0.25f;
        for (const auto& up : s.Objects()) {
            GameObject* g = up.get();
            if (!g || g->tag != blockTag || !g->transform) continue;
            Vec3 p = g->transform->Position();
            if (std::fabs(p.x - cell.x) < e && std::fabs(p.y - cell.y) < e && std::fabs(p.z - cell.z) < e)
                return true;
        }
        return false;
    }
    GameObject* PlaceBlock(Scene& s, const Vec3& cell) {
        GameObject* b = s.CreateGameObject("Block");
        if (!b) return nullptr;
        b->tag = blockTag;
        b->transform->localPosition = cell;
        b->transform->localScale = {blockSize, blockSize, blockSize};
        auto* mr = b->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->color = blockColor;
        if (!blockTexture.empty()) mr->texture = blockTexture;
        b->AddComponent<BoxCollider3D>();
        return b;
    }
};

} // namespace okay
