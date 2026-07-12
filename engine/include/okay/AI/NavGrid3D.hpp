#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include <vector>
#include <queue>
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
class NavGrid3D {
public:
    static std::vector<Vec3> FindPath(Scene& sc, const Vec3& from, const Vec3& to,
                                      GameObject* self = nullptr,
                                      float cell = 0.75f, float agentRadius = 0.4f,
                                      int maxExpand = 4096) {
        std::vector<Vec3> out;
        float dx = to.x - from.x, dz = to.z - from.z;
        if (std::sqrt(dx * dx + dz * dz) < cell) return out;   // adjacent — no plan needed

        auto key = [](int ix, int iz) { return ((std::int64_t)ix << 32) ^ (std::uint32_t)iz; };
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
        auto walkable = [&](int ix, int iz) -> bool { return probeCell(ix, iz).ok; };
        // An edge is traversable when both cells are walkable AND the surface
        // rise between them is a real step, not a wall top or a cliff face.
        auto edgeOK = [&](int ax, int az, int bx, int bz) -> bool {
            CellInfo a = probeCell(ax, az), b = probeCell(bx, bz);
            return a.ok && b.ok && std::fabs(a.surf - b.surf) <= kMaxStep;
        };

        struct Node { int ix, iz; float g; int parent; };
        std::vector<Node> nodes;
        std::unordered_map<std::int64_t, int> seen;
        auto hcost = [&](int ix, int iz) {
            float hx = (float)(gx - ix), hz = (float)(gz - iz);
            return std::sqrt(hx * hx + hz * hz);
        };
        using QE = std::pair<float, int>;   // f, node index
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
        nodes.push_back({0, 0, 0.0f, -1});
        seen[key(0, 0)] = 0;
        open.push({hcost(0, 0), 0});
        int found = -1, expanded = 0;
        const int NX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        const int NZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        while (!open.empty() && expanded < maxExpand) {
            int ni = open.top().second; open.pop();
            Node n = nodes[ni];
            ++expanded;
            if (n.ix == gx && n.iz == gz) { found = ni; break; }
            for (int d = 0; d < 8; ++d) {
                int nx = n.ix + NX[d], nz = n.iz + NZ[d];
                bool diag = NX[d] != 0 && NZ[d] != 0;
                if (!edgeOK(n.ix, n.iz, nx, nz)) continue;
                // no corner cutting: a diagonal needs both orthogonal cells free
                if (diag && (!edgeOK(n.ix, n.iz, n.ix + NX[d], n.iz) ||
                             !edgeOK(n.ix, n.iz, n.ix, n.iz + NZ[d]))) continue;
                float ng = n.g + (diag ? 1.41421f : 1.0f);
                auto it = seen.find(key(nx, nz));
                if (it != seen.end() && nodes[it->second].g <= ng) continue;
                int idx;
                if (it == seen.end()) { idx = (int)nodes.size(); nodes.push_back({nx, nz, ng, ni}); seen[key(nx, nz)] = idx; }
                else { idx = it->second; nodes[idx].g = ng; nodes[idx].parent = ni; }
                open.push({ng + hcost(nx, nz), idx});
            }
        }
        if (found < 0) return out;
        for (int i = found; i >= 0; i = nodes[i].parent)
            out.push_back(cellCenter(nodes[i].ix, nodes[i].iz));
        std::reverse(out.begin(), out.end());
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
