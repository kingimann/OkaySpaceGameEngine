#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Mesh.hpp"
#include <cmath>
#include <vector>
#include <map>
#include <array>
#include <string>
#include <cstdint>

namespace okay {

/// Runtime-only "destroy me after N seconds" tag. Used for fracture debris so the
/// shards clean themselves up instead of piling up forever. Not serialized — it
/// only ever exists on objects spawned during Play.
class TimedDestroy : public Behaviour {
public:
    float life = 4.0f;
    void Update(float dt) override {
        life -= dt;
        if (life <= 0.0f) { if (Scene* s = GetScene()) s->Destroy(gameObject); }
    }
};

/// Chaos-style destruction for voxel structures — instead of blocks just
/// vanishing, hit them and they shatter into physics debris, and anything left
/// floating with no path back to the ground collapses under its own weight.
///
/// Works on the same grid of cube GameObjects that BlockBuilder makes (anything
/// tagged `blockTag` with a box collider). Two pieces:
///   • Fracture — BreakBlock/BreakAt replace a solid cube with a cluster of
///     smaller cubes, each a real Rigidbody3D that flies out from the impact and
///     despawns after `debrisLifetime`.
///   • Structural collapse — after a break, CollapseUnsupported() flood-fills
///     from the ground/anchors through the still-solid blocks; any block that's
///     no longer connected becomes dynamic and falls. Knock out a wall's base and
///     the top sags and tumbles instead of hovering.
///
/// Drop it anywhere in the scene. For an instant "destruction gun", set
/// `breakButton` and it raycasts from the main camera each frame.
class Destructible : public Behaviour {
public:
    std::string blockTag    = "Block";  ///< only objects with this tag fracture/collapse
    float voxelSize         = 1.0f;     ///< grid cell size (matches BlockBuilder.blockSize)
    int   fractureChunks    = 2;        ///< debris cubes per axis (2 => 8 shards, 3 => 27)
    float debrisLifetime    = 4.0f;     ///< seconds before a shard despawns
    float explosionForce    = 6.0f;     ///< outward speed imparted to shards
    float upwardBias        = 0.35f;    ///< extra "pop" up so debris arcs, not just sprays out
    float debrisGravityScale= 1.0f;     ///< gravity multiplier on shards and collapsing blocks
    float debrisDrag        = 0.1f;     ///< air drag on shards (settles them)
    bool  collapseEnabled   = true;     ///< run structural-support collapse after each break
    float anchorY           = 0.75f;    ///< blocks centred at/below this Y are ground-anchored
    int   maxDebris         = 256;      ///< safety cap on live shards (skip fracturing past it)

    // Optional built-in "destruction gun" (camera ray). breakButton < 0 disables it.
    int   breakButton = -1;             ///< mouse button to fire (0=left,1=right,2=middle)
    float breakRadius = 1.2f;           ///< blocks within this radius of the hit shatter
    float reach       = 8.0f;           ///< max ray distance for the gun

    void Update(float) override {
        if (breakButton < 0) return;
        if (!Input::GetMouseButtonDown(breakButton)) return;
        Scene* s = GetScene();
        if (!s || !s->mainCamera || !s->mainCamera->gameObject || !s->mainCamera->gameObject->transform) return;
        Transform* cam = s->mainCamera->gameObject->transform;
        const Vec3 origin = cam->Position(), dir = cam->Forward() * -1.0f;
        RaycastHit3D hit = s->physics3D().Raycast(*s, origin, dir, reach, nullptr);
        if (!hit.hit || !hit.gameObject || hit.gameObject->tag != blockTag) return;
        BreakAt(hit.gameObject->transform->Position(), breakRadius, dir * explosionForce);
    }

    /// Shatter every block whose centre is within `radius` of `center`, throwing
    /// debris outward (plus the supplied `impulse`). Returns blocks broken. Runs a
    /// structural collapse pass afterwards when enabled.
    int BreakAt(const Vec3& center, float radius, const Vec3& impulse = Vec3::Zero) {
        Scene* s = GetScene();
        if (!s) return 0;
        float r2 = radius * radius;
        std::vector<GameObject*> victims;
        for (const auto& up : s->Objects()) {
            GameObject* g = up.get();
            if (!g || g->tag != blockTag || !g->transform) continue;
            if (g->GetComponent<Rigidbody3D>()) continue;   // already debris/falling
            Vec3 p = g->transform->Position();
            Vec3 d = p - center;
            if (d.x * d.x + d.y * d.y + d.z * d.z <= r2) victims.push_back(g);
        }
        int n = 0;
        for (GameObject* g : victims) if (BreakBlock(g, impulse)) ++n;
        if (collapseEnabled) CollapseUnsupported();
        return n;
    }

    /// Replace one block with a cluster of physics shards. Returns false if the
    /// object isn't a valid block (or the debris cap is hit, in which case it's
    /// simply removed without shards so the world still changes).
    bool BreakBlock(GameObject* block, const Vec3& impulse = Vec3::Zero) {
        Scene* s = GetScene();
        if (!s || !block || !block->transform || block->tag != blockTag) return false;

        const Vec3 center = block->transform->Position();
        const Vec3 scale  = block->transform->localScale;
        Color  col = Color::FromBytes(170, 172, 182);
        std::string tex;
        bool unlit = false;
        if (auto* mr = block->GetComponent<MeshRenderer>()) { col = mr->color; tex = mr->texture; unlit = mr->unlit; }

        // Scene::Destroy is deferred to end-of-frame, so retag the cube NOW —
        // otherwise a collapse pass run in the same frame still sees it as solid
        // structure and nothing above it ever loses support.
        block->tag = "Debris";
        block->active = false;
        s->Destroy(block);   // the solid cube is gone either way

        int per = fractureChunks < 1 ? 1 : (fractureChunks > 4 ? 4 : fractureChunks);
        float step = 1.0f / per;
        int spawned = 0;
        for (int iz = 0; iz < per; ++iz)
        for (int iy = 0; iy < per; ++iy)
        for (int ix = 0; ix < per; ++ix) {
            if ((int)LiveDebris() >= maxDebris) return spawned > 0;   // respect the cap
            // Local offset of this shard's centre inside the unit cube [-0.5,0.5].
            Vec3 local{(ix + 0.5f) * step - 0.5f, (iy + 0.5f) * step - 0.5f, (iz + 0.5f) * step - 0.5f};
            Vec3 pos{center.x + local.x * scale.x, center.y + local.y * scale.y, center.z + local.z * scale.z};

            GameObject* sh = s->CreateGameObject("Debris");
            if (!sh) continue;
            sh->tag = "Debris";   // NOT blockTag -> shards never re-fracture or count as structure
            sh->transform->localPosition = pos;
            sh->transform->localScale = {scale.x * step, scale.y * step, scale.z * step};
            auto* mr = sh->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Cube();
            mr->color = col;
            if (!tex.empty()) mr->texture = tex;
            mr->unlit = unlit;

            auto* bc = sh->AddComponent<BoxCollider3D>();
            bc->size = {1.0f, 1.0f, 1.0f};

            auto* rb = sh->AddComponent<Rigidbody3D>();
            rb->bodyType = Rigidbody3D::BodyType::Dynamic;
            rb->gravityScale = debrisGravityScale;
            rb->drag = debrisDrag;
            rb->mass = 0.5f;

            // Outward burst from the block centre + an upward pop + the hit impulse,
            // with a deterministic per-shard jitter (no RNG -> tests stay stable).
            Vec3 out = local;
            float len = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
            if (len > 1e-4f) out = out * (1.0f / len); else out = Vec3{0, 1, 0};
            float j = Jitter(ix, iy, iz);
            Vec3 vel{
                out.x * explosionForce * (0.6f + 0.8f * j) + impulse.x,
                out.y * explosionForce * (0.6f + 0.8f * j) + explosionForce * upwardBias + impulse.y,
                out.z * explosionForce * (0.6f + 0.8f * j) + impulse.z};
            rb->velocity = vel;

            sh->AddComponent<TimedDestroy>()->life = debrisLifetime;
            ++spawned;
        }
        return true;
    }

    /// Flood-fill from the ground/anchors through the still-solid blocks; convert
    /// any block left unconnected into a falling Rigidbody3D. Returns how many
    /// blocks started to collapse. Call repeatedly as supports are removed.
    int CollapseUnsupported() {
        Scene* s = GetScene();
        if (!s || voxelSize <= 1e-4f) return 0;

        // Index the solid (non-falling) blocks by integer grid cell.
        std::map<Cell, GameObject*> cells;
        for (const auto& up : s->Objects()) {
            GameObject* g = up.get();
            if (!g || g->tag != blockTag || !g->transform) continue;
            if (g->GetComponent<Rigidbody3D>()) continue;   // already dynamic = already falling
            cells[CellOf(g->transform->Position())] = g;
        }
        if (cells.empty()) return 0;

        // Anchors: blocks resting on the ground (low world Y) or marked static.
        std::vector<Cell> stack;
        std::map<Cell, bool> visited;
        for (auto& kv : cells) {
            bool anchored = kv.second->transform->Position().y <= anchorY || kv.second->isStatic;
            if (anchored) { visited[kv.first] = true; stack.push_back(kv.first); }
        }

        // Flood through 6-connected neighbours that are solid.
        static const std::array<Cell, 6> N{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
        while (!stack.empty()) {
            Cell c = stack.back(); stack.pop_back();
            for (const Cell& d : N) {
                Cell nc{c.x + d.x, c.y + d.y, c.z + d.z};
                if (cells.find(nc) == cells.end()) continue;
                if (visited[nc]) continue;
                visited[nc] = true;
                stack.push_back(nc);
            }
        }

        // Unvisited solid cells are floating -> make them fall.
        int collapsed = 0;
        for (auto& kv : cells) {
            if (visited[kv.first]) continue;
            GameObject* g = kv.second;
            auto* rb = g->AddComponent<Rigidbody3D>();
            rb->bodyType = Rigidbody3D::BodyType::Dynamic;
            rb->gravityScale = debrisGravityScale;
            rb->drag = debrisDrag;
            ++collapsed;
        }
        return collapsed;
    }

    /// Count of live debris shards (tag "Debris") in the scene.
    std::size_t LiveDebris() const {
        Scene* s = GetScene();
        if (!s) return 0;
        std::size_t n = 0;
        for (const auto& up : s->Objects()) if (up && up->tag == "Debris") ++n;
        return n;
    }

private:
    struct Cell {
        int x, y, z;
        bool operator<(const Cell& o) const {
            if (x != o.x) return x < o.x;
            if (y != o.y) return y < o.y;
            return z < o.z;
        }
    };
    Cell CellOf(const Vec3& p) const {
        float g = voxelSize > 1e-4f ? voxelSize : 1.0f;
        return Cell{(int)std::lround(p.x / g), (int)std::lround(p.y / g), (int)std::lround(p.z / g)};
    }
    // Deterministic 0..1 jitter from shard indices (splitmix-ish), so debris looks
    // scattered but tests get the same result every run.
    static float Jitter(int a, int b, int c) {
        std::uint32_t h = (std::uint32_t)(a * 73856093) ^ (std::uint32_t)(b * 19349663) ^ (std::uint32_t)(c * 83492791);
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return (h & 0xFFFF) / 65535.0f;
    }
};

} // namespace okay
