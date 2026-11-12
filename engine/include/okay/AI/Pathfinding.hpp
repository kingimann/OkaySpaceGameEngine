#pragma once
#include <vector>

namespace okay {

class Tilemap;

/// A* grid pathfinding — the backbone of enemy navigation, tower-defense
/// routing, and click-to-move. Works on any rectangular grid of walkable flags,
/// or directly on a Tilemap (empty cells are walkable, non-empty are walls).
class Pathfinding {
public:
    struct Cell { int x = 0; int y = 0; };

    /// The most cells (width * height) a search will take on: 4096 x 4096. A larger grid has no
    /// path (an empty result), so a bad size can never size an allocation or wrap an index.
    static constexpr long long kMaxCells = 1LL << 24;

    /// Find a path from (sx, sy) to (gx, gy) over a width*height grid where
    /// `walkable[y * width + x]` is true for passable cells. Returns the cells
    /// from start to goal inclusive, or an empty vector if no path exists.
    /// With `allowDiagonal`, movement may cut corners diagonally (cost ~1.414).
    static std::vector<Cell> AStar(const std::vector<bool>& walkable, int width, int height,
                                   int sx, int sy, int gx, int gy, bool allowDiagonal = false);

    /// Convenience: pathfind over a Tilemap, treating tile id 0 as walkable.
    static std::vector<Cell> OnTilemap(const Tilemap& map, int sx, int sy,
                                       int gx, int gy, bool allowDiagonal = false);
};

} // namespace okay
