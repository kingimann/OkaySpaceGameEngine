#pragma once
#include <vector>

namespace okay {

class Tilemap;

/// A* grid pathfinding for 2D games and tower-defense routing. Works on any
/// rectangular grid of walkable flags, or directly on a Tilemap.
///
/// Defaults match the original search, so an old call is unchanged: four
/// directions, every open cell costs 1, and the result is every cell from the
/// start to the goal. Diagonal steps, per-cell costs and line-of-sight
/// smoothing are opt-in.
class Pathfinding {
public:
    struct Cell { int x = 0; int y = 0; };

    /// The most cells (width * height) a search will take on: 4096 x 4096. A larger grid has no
    /// path (an empty result), so a bad size can never size an allocation or wrap an index.
    static constexpr long long kMaxCells = 1LL << 24;

    /// Find a path from (sx, sy) to (gx, gy) over a width*height grid where
    /// `walkable[y * width + x]` is true for passable cells. Returns the cells
    /// from start to goal inclusive, or an empty vector if no path exists.
    ///
    /// `allowDiagonal` adds the four diagonal steps. A diagonal never squeezes
    /// between two blocked cells (both orthogonal neighbours must be open).
    /// `costs`, when set, is one price per cell in the same order as `walkable`.
    /// Stepping into a cell costs the straight or diagonal length times that
    /// price. A price of 0 or less blocks the cell. A null pointer, or a vector
    /// of the wrong length, leaves every open cell at price 1.
    /// `smooth` pulls the path straight wherever the line of sight is clear
    /// (string pulling). It does not re-price the route: it only drops cells
    /// the line does not need, and it never crosses a blocked cell.
    static std::vector<Cell> AStar(const std::vector<bool>& walkable, int width, int height,
                                   int sx, int sy, int gx, int gy, bool allowDiagonal = false,
                                   const std::vector<float>* costs = nullptr, bool smooth = false);

    /// Pathfind over a Tilemap. With no cost table, tile id 0 is walkable and
    /// any other id is a wall. `tileCost`, when set and non-empty, is the price
    /// of each tile id (index = id). A price of 0 or less is a wall. An id past
    /// the end of the table is a wall, except id 0, which stays price 1.
    static std::vector<Cell> OnTilemap(const Tilemap& map, int sx, int sy,
                                       int gx, int gy, bool allowDiagonal = false,
                                       const std::vector<float>* tileCost = nullptr, bool smooth = false);

    /// True when the straight line between the two cell centres stays on open
    /// cells. A line that passes exactly through a grid corner is blocked when
    /// either cell beside that corner is a wall, the same rule as a diagonal step.
    static bool LineClear(const std::vector<bool>& walkable, int width, int height, Cell a, Cell b);

    /// String-pull `path`: keep a later cell whenever the line of sight from the
    /// current one reaches it. The result is a subsequence of `path`.
    static std::vector<Cell> Smooth(const std::vector<bool>& walkable, int width, int height,
                                    const std::vector<Cell>& path);
};

} // namespace okay
