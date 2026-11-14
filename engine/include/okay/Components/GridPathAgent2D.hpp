#pragma once
#include "okay/AI/Pathfinding.hpp"
#include "okay/Scene/Component.hpp"
#include <string>
#include <vector>

namespace okay {

/// Walks this object along a 2D tilemap. Off by default for every option that
/// would change an old path: four directions, every open cell the same price,
/// and no smoothing. Empty tiles are open and any other tile is a wall until
/// "Pay for mud and roads" is on.
class GridPathAgent2D : public Behaviour {
public:
    std::string mapName;
    float speed = 3.0f;
    float stopDistance = 0.05f;
    bool allowDiagonal = false;
    bool smoothPath = false;
    bool useCosts = false;
    float repathInterval = 0.5f;

    /// Walk to the cell that contains this world point.
    void SetDestination(float x, float y);
    /// Walk to this cell.
    void SetCell(int x, int y);
    void Stop();
    void Repath();

    bool Arrived() const { return m_arrived; }
    bool HasPath() const { return !m_path.empty() && !m_arrived; }
    bool HasGoal() const { return m_hasGoal; }
    const std::vector<Pathfinding::Cell>& Path() const { return m_path; }
    /// Length of the current path, in world units (0 when there is none).
    float PathLength() const;

    bool PathUses(int x, int y) const;

    void Update(float dt) override;

private:
    bool m_hasGoal = false;
    bool m_goalIsCell = false;
    bool m_arrived = false;
    int m_goalX = 0, m_goalY = 0;
    float m_goalWorldX = 0.0f, m_goalWorldY = 0.0f;
    std::vector<Pathfinding::Cell> m_path;
    int m_index = 0;
    float m_repathLeft = 0.0f;

    class Tilemap* Map() const;
};
} // namespace okay
