// Navigation's grid paths: 2D grids and tilemaps. The search itself is the shared one in GridAStar.hpp.
#include "okay/AI/Navigation.hpp"
#include "okay/Components/Tilemap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

bool Navigation::GridLineClear(const std::vector<bool>& walkable, int width, int height, GridCell a, GridCell b) {
    if (width <= 0 || height <= 0) return false;
    if (static_cast<long long>(width) * height > kMaxGridCells) return false;
    if ((int)walkable.size() < width * height) return false;
    const std::vector<char> open = OpenGrid(walkable, width, height, nullptr);
    return LineClearOpen(open, width, height, a.x, a.y, b.x, b.y);
}

std::vector<GridCell> Navigation::SmoothGridPath(const std::vector<bool>& walkable, int width, int height,
                                                const std::vector<GridCell>& path) {
    if (path.size() < 3) return path;
    std::vector<GridCell> out;
    out.push_back(path.front());
    std::size_t i = 0;
    while (i + 1 < path.size()) {
        std::size_t farthest = i + 1;
        for (std::size_t j = i + 2; j < path.size(); ++j)
            if (GridLineClear(walkable, width, height, path[i], path[j])) farthest = j;
        out.push_back(path[farthest]);
        if (farthest == i) break;
        i = farthest;
    }
    return out;
}

std::vector<GridCell> Navigation::GridPath(
    const std::vector<bool>& walkable, int width, int height,
    int sx, int sy, int gx, int gy, bool allowDiagonal,
    const std::vector<float>* costs, bool smooth) {

    if (width <= 0 || height <= 0) return {};
    if (static_cast<long long>(width) * height > kMaxGridCells) return {};
    const int N = width * height;
    if ((int)walkable.size() < N) return {};
    const bool priced = costs && (int)costs->size() == N;
    const std::vector<char> open = OpenGrid(walkable, width, height, priced ? costs : nullptr);

    auto passable = [&](int x, int y) { return OpenAt(open, width, x, y) && y < height; };
    if (!passable(sx, sy) || !passable(gx, gy)) return {};

    float minCost = 1.0f;
    if (priced) {
        minCost = std::numeric_limits<float>::infinity();
        for (int i = 0; i < N; ++i) {
            if (!open[(std::size_t)i]) continue;
            minCost = std::min(minCost, (*costs)[(std::size_t)i]);
        }
        if (!(minCost > 0.0f) || !std::isfinite(minCost)) minCost = 1.0f;
    }

    gridastar::DenseStore store(width, height);
    gridastar::Options opt;
    opt.diagonal = allowDiagonal;
    std::vector<GridCell> path;
    gridastar::Search(
        store, GridCell{sx, sy}, GridCell{gx, gy}, opt,
        [&](int, int, int bx, int by) { return passable(bx, by); },
        [&](int bx, int by, bool diag) {
            const float price = priced ? (*costs)[(std::size_t)(by * width + bx)] : 1.0f;
            return (diag ? 1.41421356f : 1.0f) * price;
        },
        [&](int x, int y) {
            float dx = std::fabs(float(x - gx)), dy = std::fabs(float(y - gy));
            float h = dx + dy;
            if (allowDiagonal) h = (dx + dy) + (1.41421356f - 2.0f) * std::min(dx, dy);
            return h * minCost;
        },
        path);
    if (path.empty() || !smooth) return path;

    std::vector<bool> blocked(walkable.size(), false);
    for (int i = 0; i < N; ++i) blocked[(std::size_t)i] = open[(std::size_t)i] != 0;
    return SmoothGridPath(blocked, width, height, path);
}

std::vector<GridCell> Navigation::TilemapPath(
    const Tilemap& map, int sx, int sy, int gx, int gy, bool allowDiagonal,
    const std::vector<float>* tileCost, bool smooth) {
    int w = map.Width(), h = map.Height();
    if (w <= 0 || h <= 0) return {};
    if (static_cast<long long>(w) * h > kMaxGridCells) return {};
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
    return GridPath(walkable, w, h, sx, sy, gx, gy, allowDiagonal, priced ? &cellCost : nullptr, smooth);
}

} // namespace okay
