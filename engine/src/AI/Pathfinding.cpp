#include "okay/AI/Pathfinding.hpp"
#include "okay/Components/Tilemap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace okay {
namespace {

bool InBounds(int x, int y, int width, int height) {
    return x >= 0 && y >= 0 && x < width && y < height;
}

bool OpenAt(const std::vector<char>& open, int width, int x, int y) {
    if (!InBounds(x, y, width, (int)(open.size() / (std::size_t)std::max(width, 1)))) return false;
    return open[(std::size_t)y * (std::size_t)width + (std::size_t)x] != 0;
}

// Every cell the line between two centres touches. A corner crossing needs both
// side cells open, so a diagonal cannot slip between two walls.
bool LineClearOpen(const std::vector<char>& open, int width, int height, int x0, int y0, int x1, int y1) {
    auto pass = [&](int x, int y) { return OpenAt(open, width, x, y) && y < height; };
    if (!pass(x0, y0) || !pass(x1, y1)) return false;
    const int stepX = x0 < x1 ? 1 : (x0 > x1 ? -1 : 0);
    const int stepY = y0 < y1 ? 1 : (y0 > y1 ? -1 : 0);
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    int x = x0, y = y0;
    if (dx == 0 && dy == 0) return true;
    if (dx == 0) {
        for (int i = 0; i < dy; ++i) { y += stepY; if (!pass(x, y)) return false; }
        return true;
    }
    if (dy == 0) {
        for (int i = 0; i < dx; ++i) { x += stepX; if (!pass(x, y)) return false; }
        return true;
    }
    long long ix = 0, iy = 0;
    while (ix < dx || iy < dy) {
        const long long vx = (2 * ix + 1) * dy;
        const long long hy = (2 * iy + 1) * dx;
        if (vx < hy) {
            x += stepX; ++ix;
            if (!pass(x, y)) return false;
        } else if (hy < vx) {
            y += stepY; ++iy;
            if (!pass(x, y)) return false;
        } else {
            const int nx = x + stepX, ny = y + stepY;
            if (!pass(nx, y) || !pass(x, ny) || !pass(nx, ny)) return false;
            x = nx; y = ny; ++ix; ++iy;
        }
    }
    return x == x1 && y == y1;
}

std::vector<char> OpenGrid(const std::vector<bool>& walkable, int width, int height,
                           const std::vector<float>* costs) {
    const int N = width * height;
    std::vector<char> open((std::size_t)N, 0);
    const bool priced = costs && (int)costs->size() == N;
    for (int i = 0; i < N; ++i) {
        if (!walkable[(std::size_t)i]) continue;
        if (priced) {
            const float c = (*costs)[(std::size_t)i];
            if (!(c > 0.0f) || !std::isfinite(c)) continue;
        }
        open[(std::size_t)i] = 1;
    }
    return open;
}

} // namespace

bool Pathfinding::LineClear(const std::vector<bool>& walkable, int width, int height, Cell a, Cell b) {
    if (width <= 0 || height <= 0) return false;
    if (static_cast<long long>(width) * height > kMaxCells) return false;
    if ((int)walkable.size() < width * height) return false;
    const std::vector<char> open = OpenGrid(walkable, width, height, nullptr);
    return LineClearOpen(open, width, height, a.x, a.y, b.x, b.y);
}

std::vector<Pathfinding::Cell> Pathfinding::Smooth(const std::vector<bool>& walkable, int width, int height,
                                                   const std::vector<Cell>& path) {
    if (path.size() < 3) return path;
    std::vector<Cell> out;
    out.push_back(path.front());
    std::size_t i = 0;
    while (i + 1 < path.size()) {
        std::size_t far = i + 1;
        for (std::size_t j = i + 2; j < path.size(); ++j)
            if (LineClear(walkable, width, height, path[i], path[j])) far = j;
        out.push_back(path[far]);
        if (far == i) break;
        i = far;
    }
    return out;
}

std::vector<Pathfinding::Cell> Pathfinding::AStar(
    const std::vector<bool>& walkable, int width, int height,
    int sx, int sy, int gx, int gy, bool allowDiagonal,
    const std::vector<float>* costs, bool smooth) {

    if (width <= 0 || height <= 0) return {};
    if (static_cast<long long>(width) * height > kMaxCells) return {};
    const int N = width * height;
    if ((int)walkable.size() < N) return {};
    const bool priced = costs && (int)costs->size() == N;
    const std::vector<char> open = OpenGrid(walkable, width, height, priced ? costs : nullptr);

    auto passable = [&](int x, int y) { return OpenAt(open, width, x, y) && y < height; };
    if (!passable(sx, sy) || !passable(gx, gy)) return {};

    auto idx = [&](int x, int y) { return y * width + x; };
    auto price = [&](int i) {
        if (!priced) return 1.0f;
        return (*costs)[(std::size_t)i];
    };

    float minCost = 1.0f;
    if (priced) {
        minCost = std::numeric_limits<float>::infinity();
        for (int i = 0; i < N; ++i) {
            if (!open[(std::size_t)i]) continue;
            minCost = std::min(minCost, (*costs)[(std::size_t)i]);
        }
        if (!(minCost > 0.0f) || !std::isfinite(minCost)) minCost = 1.0f;
    }

    std::vector<float> g(N, std::numeric_limits<float>::infinity());
    std::vector<int> came(N, -1);
    std::vector<char> closed(N, 0);

    auto heur = [&](int x, int y) {
        float dx = std::fabs(float(x - gx)), dy = std::fabs(float(y - gy));
        float h = dx + dy;
        if (allowDiagonal) h = (dx + dy) + (1.41421356f - 2.0f) * std::min(dx, dy);
        return h * minCost;
    };

    using PQItem = std::pair<float, int>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;

    g[idx(sx, sy)] = 0.0f;
    pq.push({heur(sx, sy), idx(sx, sy)});

    const int goal = idx(gx, gy);

    static const int dx4[] = {1, -1, 0, 0};
    static const int dy4[] = {0, 0, 1, -1};
    static const int dx8[] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy8[] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int* dxs = allowDiagonal ? dx8 : dx4;
    const int* dys = allowDiagonal ? dy8 : dy4;
    const int dirs = allowDiagonal ? 8 : 4;

    while (!pq.empty()) {
        int cur = pq.top().second;
        pq.pop();
        if (cur == goal) break;
        if (closed[cur]) continue;
        closed[cur] = 1;

        int cx = cur % width, cy = cur / width;
        for (int d = 0; d < dirs; ++d) {
            int nx = cx + dxs[d], ny = cy + dys[d];
            if (!passable(nx, ny)) continue;
            if (allowDiagonal && dxs[d] != 0 && dys[d] != 0)
                if (!passable(cx + dxs[d], cy) || !passable(cx, cy + dys[d])) continue;

            int ni = idx(nx, ny);
            if (closed[ni]) continue;
            float step = ((dxs[d] != 0 && dys[d] != 0) ? 1.41421356f : 1.0f) * price(ni);
            float tentative = g[cur] + step;
            if (tentative < g[ni]) {
                g[ni] = tentative;
                came[ni] = cur;
                pq.push({tentative + heur(nx, ny), ni});
            }
        }
    }

    if (came[goal] == -1 && goal != idx(sx, sy)) return {};

    std::vector<Cell> path;
    for (int c = goal; c != -1; c = came[c]) {
        path.push_back(Cell{c % width, c / width});
        if (c == idx(sx, sy)) break;
    }
    std::reverse(path.begin(), path.end());
    if (!smooth) return path;

    std::vector<bool> blocked(walkable.size(), false);
    for (int i = 0; i < N; ++i) blocked[(std::size_t)i] = open[(std::size_t)i] != 0;
    return Smooth(blocked, width, height, path);
}

std::vector<Pathfinding::Cell> Pathfinding::OnTilemap(
    const Tilemap& map, int sx, int sy, int gx, int gy, bool allowDiagonal,
    const std::vector<float>* tileCost, bool smooth) {
    int w = map.Width(), h = map.Height();
    if (w <= 0 || h <= 0) return {};
    if (static_cast<long long>(w) * h > kMaxCells) return {};
    const bool priced = tileCost && !tileCost->empty();
    std::vector<bool> walkable(static_cast<std::size_t>(w) * h, false);
    std::vector<float> cellCost;
    if (priced) cellCost.assign(walkable.size(), 1.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int id = map.GetTile(x, y);
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            if (!priced) {
                walkable[i] = (id == 0);
                continue;
            }
            float c = 0.0f;
            if (id >= 0 && static_cast<std::size_t>(id) < tileCost->size()) c = (*tileCost)[(std::size_t)id];
            else if (id == 0) c = 1.0f;
            if (c > 0.0f && std::isfinite(c)) {
                walkable[i] = true;
                cellCost[i] = c;
            }
        }
    }
    return AStar(walkable, w, h, sx, sy, gx, gy, allowDiagonal, priced ? &cellCost : nullptr, smooth);
}

} // namespace okay
