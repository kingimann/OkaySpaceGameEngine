#pragma once
// The one grid A* search in the engine. Navigation's 2D grid paths (Navigation::GridPath, the tilemap agent)
// and the 3D grid fallback (NavGrid3D, used when a scene has no baked NavMesh) both run this loop; they differ
// only in how cells are stored and which callbacks they pass.
//
// Written from the textbook A* algorithm (Hart, Nilsson and Raphael, 1968): a priority queue ordered by
// f = g + h, ties broken by the smaller node id. The two old searches it replaces broke ties the same way, so
// keeping their node ids (cell index for a dense grid, creation order for a sparse one) keeps their paths.
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay {

/// One square of a grid: column x, row y.
struct GridCell {
    int x = 0;
    int y = 0;
};

namespace gridastar {

/// The eight steps, four straight ones first. Every grid search walks them in this order.
inline constexpr int kStepX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
inline constexpr int kStepY[8] = {0, 0, 1, -1, 1, -1, 1, -1};

struct Options {
    /// Also take the four diagonal steps. A diagonal needs both straight steps beside it open.
    bool diagonal = false;
    /// Stop after this many queue pops (below 0: no limit).
    int maxExpand = -1;
    /// false: a cell is finished once it leaves the queue (closed set).
    /// true: no closed set; a cell can be expanded again when a cheaper route reaches it later, and every
    /// pop counts toward maxExpand. NavGrid3D always searched this way, so it keeps it.
    bool reopen = false;
};

/// Cells of a bounded width x height grid, kept in flat arrays. A cell's id is y * width + x.
class DenseStore {
public:
    DenseStore(int width, int height)
        : m_width(width),
          m_g((std::size_t)width * (std::size_t)height, std::numeric_limits<float>::infinity()),
          m_parent((std::size_t)width * (std::size_t)height, -1),
          m_closed((std::size_t)width * (std::size_t)height, 0) {}
    // The edge callback has already kept the cell inside the grid.
    int Find(int x, int y) const { return y * m_width + x; }
    int Add(int x, int y) { return Find(x, y); }
    float& G(int id) { return m_g[(std::size_t)id]; }
    int& Parent(int id) { return m_parent[(std::size_t)id]; }
    bool Closed(int id) const { return m_closed[(std::size_t)id] != 0; }
    void Close(int id) { m_closed[(std::size_t)id] = 1; }
    GridCell CellOf(int id) const { return GridCell{id % m_width, id / m_width}; }

private:
    int m_width;
    std::vector<float> m_g;
    std::vector<int> m_parent;
    std::vector<char> m_closed;
};

/// Cells of an unbounded grid, made only when the search first reaches them. A cell's id is the order it was
/// reached in (the start is 0).
class SparseStore {
public:
    int Find(int x, int y) const {
        auto it = m_ids.find(Key(x, y));
        return it == m_ids.end() ? -1 : it->second;
    }
    int Add(int x, int y) {
        const int id = (int)m_nodes.size();
        m_nodes.push_back(Node{GridCell{x, y}, std::numeric_limits<float>::infinity(), -1, false});
        m_ids[Key(x, y)] = id;
        return id;
    }
    float& G(int id) { return m_nodes[(std::size_t)id].g; }
    int& Parent(int id) { return m_nodes[(std::size_t)id].parent; }
    bool Closed(int id) const { return m_nodes[(std::size_t)id].closed; }
    void Close(int id) { m_nodes[(std::size_t)id].closed = true; }
    GridCell CellOf(int id) const { return m_nodes[(std::size_t)id].cell; }

    /// The packed key a cell is filed under (also handy for a caller's own per-cell cache).
    static std::int64_t Key(int x, int y) {
        return (std::int64_t)(((std::uint64_t)(std::uint32_t)x << 32) | (std::uint32_t)y);
    }

private:
    struct Node { GridCell cell; float g; int parent; bool closed; };
    std::vector<Node> m_nodes;
    std::unordered_map<std::int64_t, int> m_ids;
};

/// Search from `start` to `goal`. Callbacks:
///   edge(ax, ay, bx, by) -> bool   may the search step from cell a to the next cell b
///   stepCost(bx, by, diagonal) -> float   the price of that step
///   heuristic(x, y) -> float   an estimate of the price left from this cell to the goal
/// Fills `out` with the cells from start to goal (both included) and returns true, or returns false with
/// `out` empty when the goal was not reached (no route, or maxExpand ran out).
template <class Store, class Edge, class StepCost, class Heuristic>
bool Search(Store& store, GridCell start, GridCell goal, const Options& opt, Edge&& edge,
            StepCost&& stepCost, Heuristic&& heuristic, std::vector<GridCell>& out) {
    out.clear();
    using Entry = std::pair<float, int>;   // f, node id
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

    const int startId = store.Add(start.x, start.y);
    store.G(startId) = 0.0f;
    open.push({heuristic(start.x, start.y), startId});

    const int dirs = opt.diagonal ? 8 : 4;
    int found = -1, expanded = 0;
    while (!open.empty()) {
        if (opt.maxExpand >= 0 && expanded >= opt.maxExpand) break;
        const int cur = open.top().second;
        open.pop();
        const GridCell c = store.CellOf(cur);
        const bool atGoal = c.x == goal.x && c.y == goal.y;
        if (opt.reopen) {
            ++expanded;
            if (atGoal) { found = cur; break; }
        } else {
            if (atGoal) { found = cur; break; }
            if (store.Closed(cur)) continue;
            store.Close(cur);
            ++expanded;
        }
        for (int d = 0; d < dirs; ++d) {
            const int nx = c.x + kStepX[d], ny = c.y + kStepY[d];
            const bool diag = kStepX[d] != 0 && kStepY[d] != 0;
            if (!edge(c.x, c.y, nx, ny)) continue;
            if (diag && (!edge(c.x, c.y, nx, c.y) || !edge(c.x, c.y, c.x, ny))) continue;
            int ni = store.Find(nx, ny);
            if (!opt.reopen && ni >= 0 && store.Closed(ni)) continue;
            const float ng = store.G(cur) + stepCost(nx, ny, diag);
            if (ni >= 0 && !(ng < store.G(ni))) continue;
            if (ni < 0) ni = store.Add(nx, ny);
            store.G(ni) = ng;
            store.Parent(ni) = cur;
            open.push({ng + heuristic(nx, ny), ni});
        }
    }
    if (found < 0) return false;
    for (int id = found; id >= 0; id = store.Parent(id)) {
        out.push_back(store.CellOf(id));
        if (id == startId) break;
    }
    std::vector<GridCell> reversed(out.rbegin(), out.rend());
    out.swap(reversed);
    return true;
}

} // namespace gridastar
} // namespace okay
