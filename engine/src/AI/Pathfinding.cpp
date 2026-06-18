#include "okay/AI/Pathfinding.hpp"
#include "okay/Components/Tilemap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace okay {

std::vector<Pathfinding::Cell> Pathfinding::AStar(
    const std::vector<bool>& walkable, int width, int height,
    int sx, int sy, int gx, int gy, bool allowDiagonal) {

    auto inBounds = [&](int x, int y) { return x >= 0 && y >= 0 && x < width && y < height; };
    auto passable = [&](int x, int y) {
        return inBounds(x, y) &&
               static_cast<std::size_t>(y) * width + x < walkable.size() &&
               walkable[static_cast<std::size_t>(y) * width + x];
    };

    if (width <= 0 || height <= 0) return {};
    if (!passable(sx, sy) || !passable(gx, gy)) return {};

    const int N = width * height;
    auto idx = [&](int x, int y) { return y * width + x; };

    std::vector<float> g(N, std::numeric_limits<float>::infinity());
    std::vector<int> came(N, -1);
    std::vector<bool> closed(N, false);

    auto heur = [&](int x, int y) {
        float dx = std::fabs(float(x - gx)), dy = std::fabs(float(y - gy));
        if (allowDiagonal) // octile distance
            return (dx + dy) + (1.41421356f - 2.0f) * std::min(dx, dy);
        return dx + dy; // Manhattan
    };

    // Priority queue of (f-score, cell index); lowest f first.
    using PQItem = std::pair<float, int>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> open;

    g[idx(sx, sy)] = 0.0f;
    open.push({heur(sx, sy), idx(sx, sy)});

    const int goal = idx(gx, gy);

    static const int dx4[] = {1, -1, 0, 0};
    static const int dy4[] = {0, 0, 1, -1};
    static const int dx8[] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy8[] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int* dxs = allowDiagonal ? dx8 : dx4;
    const int* dys = allowDiagonal ? dy8 : dy4;
    const int dirs = allowDiagonal ? 8 : 4;

    while (!open.empty()) {
        int cur = open.top().second;
        open.pop();
        if (cur == goal) break;
        if (closed[cur]) continue;
        closed[cur] = true;

        int cx = cur % width, cy = cur / width;
        for (int d = 0; d < dirs; ++d) {
            int nx = cx + dxs[d], ny = cy + dys[d];
            if (!passable(nx, ny)) continue;
            // Don't squeeze diagonally between two walls.
            if (allowDiagonal && dxs[d] != 0 && dys[d] != 0)
                if (!passable(cx + dxs[d], cy) || !passable(cx, cy + dys[d])) continue;

            int ni = idx(nx, ny);
            if (closed[ni]) continue;
            float step = (dxs[d] != 0 && dys[d] != 0) ? 1.41421356f : 1.0f;
            float tentative = g[cur] + step;
            if (tentative < g[ni]) {
                g[ni] = tentative;
                came[ni] = cur;
                open.push({tentative + heur(nx, ny), ni});
            }
        }
    }

    if (came[goal] == -1 && goal != idx(sx, sy)) return {}; // unreachable

    // Reconstruct path from goal back to start.
    std::vector<Cell> path;
    for (int c = goal; c != -1; c = came[c]) {
        path.push_back(Cell{c % width, c / width});
        if (c == idx(sx, sy)) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<Pathfinding::Cell> Pathfinding::OnTilemap(
    const Tilemap& map, int sx, int sy, int gx, int gy, bool allowDiagonal) {
    int w = map.Width(), h = map.Height();
    std::vector<bool> walkable(static_cast<std::size_t>(w) * h, false);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            walkable[static_cast<std::size_t>(y) * w + x] = (map.GetTile(x, y) == 0);
    return AStar(walkable, w, h, sx, sy, gx, gy, allowDiagonal);
}

} // namespace okay
