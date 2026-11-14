#pragma once
#include "okay/AI/Pathfinding.hpp"
#include "okay/Scene/Component.hpp"
#include <string>
#include <vector>

namespace okay {

class Tilemap;

/// Blocks cells on a 2D tilemap. An agent whose current path runs through those
/// cells plans a new route at once. Agents on some other route are left alone.
/// The blocked rectangle sits on this object's cell (or on the cell Place names).
class GridObstacle2D : public Behaviour {
public:
    std::string mapName;
    int cellsWide = 1;
    int cellsTall = 1;
    bool blocks = true;

    /// Block `cellsWide` by `cellsTall` cells starting at this cell, and repath
    /// any agent whose route used one of them.
    void Place(int cellX, int cellY);
    /// Stop blocking, without forcing a new plan on agents that still have a route.
    void Clear();
    /// Tell agents about the cells this obstacle covers right now.
    void Commit();

    void OnEnable() override;
    void OnDisable() override;
    void Update(float dt) override;
    int ExecutionOrder() const override { return -20; }

    void CoveredCells(const Tilemap& map, std::vector<Pathfinding::Cell>& out) const;
    bool UsesMap(const Tilemap& map) const;

private:
    bool m_hasAnchor = false;
    int m_anchorX = 0, m_anchorY = 0;
    int m_lastX = 0x7fffffff, m_lastY = 0x7fffffff;
    Tilemap* Map() const;
};
} // namespace okay
