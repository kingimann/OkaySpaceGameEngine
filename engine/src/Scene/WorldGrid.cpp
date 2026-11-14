#include "okay/Scene/WorldGrid.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace okay::worldpart {

std::uint64_t PackCell(const CellCoord& c) {
    auto f = [](int v) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(v) + kMaxCellIndex) & 0x1FFFFFull; };
    return (f(c.x) << 42) | (f(c.y) << 21) | f(c.z);
}

bool WorldGrid::IsValidCellSize(float s) { return std::isfinite(s) && s > 0.0f; }

WorldGrid::WorldGrid(float cellSize, GridPlane plane) : m_plane(plane) {
    m_valid = IsValidCellSize(cellSize);
    m_size = m_valid ? cellSize : 0.0f;
}

bool WorldGrid::Active(int axis) const {
    if (axis == 0) return true;
    if (axis == 1) return m_plane != GridPlane::XZ;
    return m_plane != GridPlane::XY;
}

CellCoord WorldGrid::Normalize(const CellCoord& c) const {
    CellCoord n = c;
    if (!Active(1)) n.y = 0;
    if (!Active(2)) n.z = 0;
    return n;
}

bool WorldGrid::InRange(const CellCoord& c) const {
    auto ok = [](int v) { return v >= -kMaxCellIndex && v <= kMaxCellIndex; };
    if (!ok(c.x)) return false;
    if (Active(1) ? !ok(c.y) : c.y != 0) return false;
    if (Active(2) ? !ok(c.z) : c.z != 0) return false;
    return true;
}

bool WorldGrid::CellOf(const Vec3& p, CellCoord& out) const {
    if (!m_valid) return false;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    auto idx = [&](float v, int& r) {
        const double f = std::floor(static_cast<double>(v) / static_cast<double>(m_size));
        if (!(f >= -static_cast<double>(kMaxCellIndex) && f <= static_cast<double>(kMaxCellIndex))) return false;
        r = static_cast<int>(f);
        return true;
    };
    CellCoord c;
    if (!idx(p.x, c.x)) return false;
    if (Active(1) && !idx(p.y, c.y)) return false;
    if (Active(2) && !idx(p.z, c.z)) return false;
    out = c;
    return true;
}

bool WorldGrid::CellBox(const CellCoord& c, Vec3& lo, Vec3& hi) const {
    if (!m_valid || !InRange(c)) return false;
    const float inf = std::numeric_limits<float>::infinity();
    auto lohi = [&](int v, bool active, float& l, float& h) {
        if (!active) { l = -inf; h = inf; return; }
        l = static_cast<float>(static_cast<double>(v) * m_size);
        h = static_cast<float>((static_cast<double>(v) + 1.0) * m_size);
    };
    lohi(c.x, true, lo.x, hi.x);
    lohi(c.y, Active(1), lo.y, hi.y);
    lohi(c.z, Active(2), lo.z, hi.z);
    return true;
}

bool WorldGrid::CellCenter(const CellCoord& c, Vec3& out) const {
    if (!m_valid || !InRange(c)) return false;
    auto mid = [&](int v, bool active) { return active ? static_cast<float>((static_cast<double>(v) + 0.5) * m_size) : 0.0f; };
    out = Vec3{mid(c.x, true), mid(c.y, Active(1)), mid(c.z, Active(2))};
    return true;
}

float WorldGrid::DistanceToCell(const Vec3& p, const CellCoord& c) const {
    const float inf = std::numeric_limits<float>::infinity();
    if (!m_valid || !InRange(c) || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return inf;
    auto gap = [&](float v, int cell, bool active) {
        if (!active) return 0.0;
        const double lo = static_cast<double>(cell) * m_size, hi = lo + m_size, d = v;
        return d < lo ? lo - d : (d > hi ? d - hi : 0.0);
    };
    const double dx = gap(p.x, c.x, true), dy = gap(p.y, c.y, Active(1)), dz = gap(p.z, c.z, Active(2));
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
}

std::vector<CellCoord> WorldGrid::Neighbours(const CellCoord& cIn, int ring, bool includeSelf) const {
    std::vector<CellCoord> out;
    if (!m_valid) return out;
    const CellCoord c = Normalize(cIn);
    if (!InRange(c)) return out;
    ring = std::max(0, std::min(ring, 32));
    const int ry = Active(1) ? ring : 0, rz = Active(2) ? ring : 0;
    for (int x = c.x - ring; x <= c.x + ring; ++x)
        for (int y = c.y - ry; y <= c.y + ry; ++y)
            for (int z = c.z - rz; z <= c.z + rz; ++z) {
                const CellCoord n{x, y, z};
                if (!includeSelf && n == c) continue;
                if (InRange(n)) out.push_back(n);
            }
    return out;
}

bool WorldGrid::CellsInRadius(const Vec3& p, float radius, std::vector<CellCoord>& out, std::size_t maxCells) const {
    out.clear();
    if (!m_valid || !std::isfinite(radius) || radius < 0.0f) return false;
    CellCoord o;
    if (!CellOf(p, o)) return false;
    const double span = std::ceil(static_cast<double>(radius) / static_cast<double>(m_size)) + 1.0;
    if (span > 1000.0) return false;   // way beyond maxCells in any case; avoids a huge loop bound
    const int r = static_cast<int>(span);
    const double side = 2.0 * r + 1.0;
    const double cells = side * (Active(1) ? side : 1.0) * (Active(2) ? side : 1.0);
    if (cells > static_cast<double>(maxCells) * 4.0 + 64.0) return false;   // box far bigger than the answer
    const int ry = Active(1) ? r : 0, rz = Active(2) ? r : 0;
    std::vector<std::pair<float, CellCoord>> found;
    for (int x = o.x - r; x <= o.x + r; ++x)
        for (int y = o.y - ry; y <= o.y + ry; ++y)
            for (int z = o.z - rz; z <= o.z + rz; ++z) {
                const CellCoord c{x, y, z};
                if (!InRange(c)) continue;
                const float d = DistanceToCell(p, c);
                if (d <= radius) {
                    found.push_back({d, c});
                    if (found.size() > maxCells) return false;
                }
            }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });
    out.reserve(found.size());
    for (const auto& f : found) out.push_back(f.second);
    return true;
}

std::string WorldGrid::CellName(const CellCoord& c) {
    auto n = [](int v) { return v < 0 ? "n" + std::to_string(-static_cast<long long>(v)) : std::to_string(v); };
    return "cell_" + n(c.x) + "_" + n(c.y) + "_" + n(c.z);
}

bool WorldGrid::ParseCellName(const std::string& name, CellCoord& out) {
    if (name.compare(0, 5, "cell_") != 0) return false;
    int v[3];
    std::size_t pos = 5;
    for (int k = 0; k < 3; ++k) {
        bool neg = false;
        if (pos < name.size() && name[pos] == 'n') { neg = true; ++pos; }
        const std::size_t start = pos;
        long long val = 0;
        while (pos < name.size() && name[pos] >= '0' && name[pos] <= '9') {
            val = val * 10 + (name[pos] - '0');
            if (val > kMaxCellIndex) return false;
            ++pos;
        }
        if (pos == start) return false;
        v[k] = static_cast<int>(neg ? -val : val);
        if (k < 2) { if (pos >= name.size() || name[pos] != '_') return false; ++pos; }
    }
    if (pos != name.size()) return false;
    out = CellCoord{v[0], v[1], v[2]};
    return true;
}

} // namespace okay::worldpart
