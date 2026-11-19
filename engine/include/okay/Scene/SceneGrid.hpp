#pragma once
// ---------------------------------------------------------------------------
// SceneGrid: "which objects are within R of this point" without testing every object in the scene.
//
// Build(scene, cellSize) sorts the objects into a uniform grid of cubes by their world position (one pass). Query(center,
// radius) then looks only at the cubes the circle touches: cost grows with the objects NEAR the point, not with the scene.
//   - The grid is a snapshot. Moving an object does not update it; call Build again (cheap: one pass) when things moved a
//     lot, or use Scene-wide brute force (FindObjectsInRadius below) for a single question. IsStale(scene) tells you an object
//     was created, destroyed or reparented since Build; it cannot see plain movement.
//   - Results are sorted by distance, then by InstanceId, so the answer is the same on every run.
//   - An object counts by its Transform position (its centre), not its size. Only objects that are active in the hierarchy
//     are put in unless `includeInactive`.
// Header only, own algorithm (hash of integer cell coordinates), no <mutex> / <thread>. Main thread.
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace okay {

/// Every object whose position is within `radius` of `center` (<= radius), nearest first, ties by InstanceId. Looks at every
/// object in the scene; use a SceneGrid when asking many times. `includeInactive` false skips objects that are not active in the hierarchy.
inline std::vector<GameObject*> FindObjectsInRadius(const Scene& scene, const Vec3& center, float radius, bool includeInactive = false) {
    std::vector<std::pair<float, GameObject*>> hit;
    const float r2 = radius * radius;
    if (radius >= 0.0f)
        for (const auto& up : scene.Objects()) {
            GameObject* g = up.get();
            if (g->IsDestroyed() || (!includeInactive && !g->ActiveInHierarchy())) continue;
            const float d2 = (g->transform->Position() - center).SqrMagnitude();
            if (d2 <= r2) hit.push_back({d2, g});
        }
    std::sort(hit.begin(), hit.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : a.second->InstanceId() < b.second->InstanceId();
    });
    std::vector<GameObject*> out;
    out.reserve(hit.size());
    for (const auto& h : hit) out.push_back(h.second);
    return out;
}

class SceneGrid {
public:
    /// Rebuild from the scene's objects now. `cellSize` should be about the radius you will ask for (values <= 0 become 1).
    void Build(const Scene& scene, float cellSize, bool includeInactive = false) {
        m_cell = cellSize > 0.0f ? cellSize : 1.0f;
        m_cells.clear();
        m_count = 0;
        for (const auto& up : scene.Objects()) {
            GameObject* g = up.get();
            if (g->IsDestroyed() || (!includeInactive && !g->ActiveInHierarchy())) continue;
            const Vec3 p = g->transform->Position();
            m_cells[Key(Cell(p.x), Cell(p.y), Cell(p.z))].push_back({p, g});
            ++m_count;
        }
        m_scene = scene.Uid();
        m_structure = scene.StructureVersion();
    }
    /// True when the scene is another one, or an object was created, destroyed or reparented since Build.
    bool IsStale(const Scene& scene) const { return m_scene != scene.Uid() || m_structure != scene.StructureVersion(); }
    std::size_t Count() const { return m_count; }
    std::size_t CellCount() const { return m_cells.size(); }
    /// The objects within `radius` of `center`, nearest first. Objects destroyed since Build are left out.
    std::vector<GameObject*> Query(const Vec3& center, float radius) const {
        std::vector<std::pair<float, GameObject*>> hit;
        if (radius >= 0.0f && m_count > 0) {
            const long x0 = Cell(center.x - radius), x1 = Cell(center.x + radius);
            const long y0 = Cell(center.y - radius), y1 = Cell(center.y + radius);
            const long z0 = Cell(center.z - radius), z1 = Cell(center.z + radius);
            const float r2 = radius * radius;
            // A huge radius would walk more empty cubes than there are objects: fall back to every cube that has any.
            const double span = double(x1 - x0 + 1) * double(y1 - y0 + 1) * double(z1 - z0 + 1);
            auto test = [&](const Entry& e) {
                if (e.go->IsDestroyed()) return;
                const float d2 = (e.pos - center).SqrMagnitude();
                if (d2 <= r2) hit.push_back({d2, e.go});
            };
            if (span > double(m_cells.size()) * 4.0) {
                for (const auto& kv : m_cells) for (const Entry& e : kv.second) test(e);
            } else {
                for (long z = z0; z <= z1; ++z)
                    for (long y = y0; y <= y1; ++y)
                        for (long x = x0; x <= x1; ++x) {
                            auto it = m_cells.find(Key(x, y, z));
                            if (it != m_cells.end()) for (const Entry& e : it->second) test(e);
                        }
            }
        }
        std::sort(hit.begin(), hit.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first < b.first : a.second->InstanceId() < b.second->InstanceId();
        });
        std::vector<GameObject*> out;
        out.reserve(hit.size());
        for (const auto& h : hit) out.push_back(h.second);
        return out;
    }
private:
    struct Entry { Vec3 pos; GameObject* go; };
    long Cell(float v) const { return static_cast<long>(std::floor(v / m_cell)); }
    static std::uint64_t Key(long x, long y, long z) {
        // 21 bits per axis (about +-1M cells); a collision only mixes two far cubes in one bucket, and every entry is distance-tested.
        return (std::uint64_t(x) & 0x1FFFFF) | ((std::uint64_t(y) & 0x1FFFFF) << 21) | ((std::uint64_t(z) & 0x1FFFFF) << 42);
    }
    float m_cell = 1.0f;
    std::size_t m_count = 0;
    std::uint64_t m_scene = 0, m_structure = 0;
    std::unordered_map<std::uint64_t, std::vector<Entry>> m_cells;
};

} // namespace okay
