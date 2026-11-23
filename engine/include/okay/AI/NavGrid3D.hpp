#pragma once
#include "okay/AI/GridAStar.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint>

namespace okay {

/// 3D obstacle-avoiding pathfinding for NPCs: A* over a lazily-sampled grid
/// on the XZ plane. A cell is walkable when an agent-sized sphere probe at
/// its centre (knee height above the walking surface, found by a down-ray —
/// which also hits heightmap Terrain) overlaps no solid collider. Returns a
/// smoothed list of world waypoints, or empty when no route exists within
/// the expansion budget (the caller falls back to straight-line steering).
/// The search is the shared grid A* (GridAStar.hpp), the same one Navigation's
/// 2D grid paths use.
class NavGrid3D {
public:
    static std::vector<Vec3> FindPath(Scene& sc, const Vec3& from, const Vec3& to,
                                      GameObject* self = nullptr,
                                      float cell = 0.75f, float agentRadius = 0.4f,
                                      int maxExpand = 4096) {
        std::vector<Vec3> out;
        float dx = to.x - from.x, dz = to.z - from.z;
        if (std::sqrt(dx * dx + dz * dz) < cell) return out;   // adjacent — no plan needed

        auto key = [](int ix, int iz) { return gridastar::SparseStore::Key(ix, iz); };
        auto cellCenter = [&](int ix, int iz) {
            return Vec3{from.x + ix * cell, from.y, from.z + iz * cell};
        };
        int gx = (int)std::lround((to.x - from.x) / cell);
        int gz = (int)std::lround((to.z - from.z) / cell);

        // Walkability probe, memoised per query — remembers the surface height
        // too, so edges can enforce a MAX STEP between cells (otherwise a path
        // happily climbs onto wall tops the down-ray finds). Cells near the
        // GOAL are always walkable — the target's own capsule shouldn't wall
        // off the last step.
        const float kMaxStep = 0.45f;
        struct CellInfo { bool ok; float surf; };
        std::unordered_map<std::int64_t, CellInfo> walkCache;
        auto probeCell = [&](int ix, int iz) -> CellInfo {
            auto it = walkCache.find(key(ix, iz));
            if (it != walkCache.end()) return it->second;
            CellInfo ci{true, from.y};
            Vec3 c = cellCenter(ix, iz);
            RaycastHit3D g = sc.physics3D().Raycast(sc, {c.x, c.y + 2.5f, c.z},
                                                    {0, -1, 0}, 8.0f, self);
            if (g.hit) ci.surf = g.point.y;
            if (std::abs(ix - gx) + std::abs(iz - gz) > 1) {
                // No ground within range = a hole/cliff: blocked, hug real floor.
                if (!g.hit || c.y - ci.surf > 3.0f) ci.ok = false;
                if (ci.ok) {
                    Vec3 probe{c.x, ci.surf + agentRadius + 0.15f, c.z};
                    for (Collider3D* col : sc.physics3D().OverlapSphere(sc, probe, agentRadius)) {
                        if (!col || col->isTrigger) continue;
                        GameObject* cg = col->gameObject;
                        if (self && cg && (cg == self || cg->IsSelfOrDescendantOf(self))) continue;
                        ci.ok = false; break;
                    }
                }
            }
            walkCache[key(ix, iz)] = ci;
            return ci;
        };
        // An edge is traversable when both cells are walkable AND the surface
        // rise between them is a real step, not a wall top or a cliff face.
        auto edgeOK = [&](int ax, int az, int bx, int bz) -> bool {
            CellInfo a = probeCell(ax, az), b = probeCell(bx, bz);
            return a.ok && b.ok && std::fabs(a.surf - b.surf) <= kMaxStep;
        };

        auto hcost = [&](int ix, int iz) {
            float hx = (float)(gx - ix), hz = (float)(gz - iz);
            return std::sqrt(hx * hx + hz * hz);
        };
        // Grid y is world z. No closed set (reopen) and a pop budget, as this search always had.
        gridastar::SparseStore store;
        gridastar::Options opt;
        opt.diagonal = true;
        opt.maxExpand = maxExpand < 0 ? 0 : maxExpand;   // a negative budget searched nothing before, too
        opt.reopen = true;
        std::vector<GridCell> cells;
        if (!gridastar::Search(store, GridCell{0, 0}, GridCell{gx, gz}, opt,
                               edgeOK,
                               [](int, int, bool diag) { return diag ? 1.41421f : 1.0f; },
                               hcost, cells))
            return out;
        out.reserve(cells.size());
        for (const GridCell& c : cells) out.push_back(cellCenter(c.x, c.y));
        out.back() = to;   // land exactly on the target

        // String-pull smoothing: drop waypoints whose straight segment stays on
        // walkable cells (checked at sub-cell steps), so paths hug corners
        // instead of staircasing across open ground.
        if (out.size() > 2) {
            std::vector<Vec3> sm;
            sm.push_back(out.front());
            std::size_t a = 0;
            while (a + 1 < out.size()) {
                std::size_t best = a + 1;
                for (std::size_t b = out.size() - 1; b > a + 1; --b) {
                    Vec3 pa = out[a], pb = out[b];
                    float len = std::sqrt((pb.x - pa.x) * (pb.x - pa.x) + (pb.z - pa.z) * (pb.z - pa.z));
                    int steps = (int)(len / (cell * 0.5f)) + 1;
                    bool clear = true;
                    float prevSurf = probeCell((int)std::lround((pa.x - from.x) / cell),
                                               (int)std::lround((pa.z - from.z) / cell)).surf;
                    for (int s = 1; s <= steps && clear; ++s) {
                        float t = s / (float)steps;
                        int ix = (int)std::lround((pa.x + (pb.x - pa.x) * t - from.x) / cell);
                        int iz = (int)std::lround((pa.z + (pb.z - pa.z) * t - from.z) / cell);
                        CellInfo ci = probeCell(ix, iz);
                        clear = ci.ok && std::fabs(ci.surf - prevSurf) <= kMaxStep;
                        prevSurf = ci.surf;
                    }
                    if (clear) { best = b; break; }
                }
                sm.push_back(out[best]);
                a = best;
            }
            out.swap(sm);
        }
        return out;
    }
};

} // namespace okay
