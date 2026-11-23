#pragma once
#include "okay/AI/Navigation.hpp"
#include <vector>

namespace okay {

class Tilemap;

/// The older name for Navigation's grid paths, kept so existing code and the
/// GridPathAgent2D / GridObstacle2D headers still compile. Every call goes
/// straight to Navigation (see Navigation::GridPath for what each argument does).
/// New code should call Navigation::GridPath, TilemapPath, GridLineClear and
/// SmoothGridPath.
class Pathfinding {
public:
    using Cell = GridCell;

    static constexpr long long kMaxCells = Navigation::kMaxGridCells;

    static std::vector<Cell> AStar(const std::vector<bool>& walkable, int width, int height,
                                   int sx, int sy, int gx, int gy, bool allowDiagonal = false,
                                   const std::vector<float>* costs = nullptr, bool smooth = false) {
        return Navigation::GridPath(walkable, width, height, sx, sy, gx, gy, allowDiagonal, costs, smooth);
    }

    static std::vector<Cell> OnTilemap(const Tilemap& map, int sx, int sy,
                                       int gx, int gy, bool allowDiagonal = false,
                                       const std::vector<float>* tileCost = nullptr, bool smooth = false) {
        return Navigation::TilemapPath(map, sx, sy, gx, gy, allowDiagonal, tileCost, smooth);
    }

    static bool LineClear(const std::vector<bool>& walkable, int width, int height, Cell a, Cell b) {
        return Navigation::GridLineClear(walkable, width, height, a, b);
    }

    static std::vector<Cell> Smooth(const std::vector<bool>& walkable, int width, int height,
                                    const std::vector<Cell>& path) {
        return Navigation::SmoothGridPath(walkable, width, height, path);
    }
};

} // namespace okay
