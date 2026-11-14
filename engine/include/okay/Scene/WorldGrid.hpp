#pragma once
// WorldGrid: cell addressing for large-world streaming (docs/foundations/world_streaming.md).
//
// A world is cut into square (or cubic) cells of one size. A cell is named by whole-number
// coordinates; cell (0,0) covers [0,size) on each active axis, so negative positions work (floor, not
// truncation). The grid only does arithmetic: no scene, no files. It rejects what cannot be addressed
// (a size that is not a finite number above 0, a position that is NaN or infinite, a cell index
// beyond +-kMaxCellIndex) instead of wrapping or hitting an undefined cast.
//
// Reuses optimize::CellCoord and optimize::GridPlane so the engine has one cell type.
#include "okay/Math/Vec3.hpp"
#include "okay/Optimize/StreamingGrid.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::worldpart {

using optimize::CellCoord;
using optimize::GridPlane;

/// Largest cell index (absolute value) a grid accepts: 2^20 - 1, so three of them pack into 63 bits.
constexpr int kMaxCellIndex = (1 << 20) - 1;

/// A packed, unique 63-bit number for a cell (for hash maps). Only for in-range cells.
std::uint64_t PackCell(const CellCoord& c);

class WorldGrid {
public:
    /// A grid with this cell size. A size that is not a finite number above 0 makes an INVALID grid
    /// (Valid() is false and every query answers "no"); IsValidCellSize tells before you build one.
    explicit WorldGrid(float cellSize = 64.0f, GridPlane plane = GridPlane::XZ);
    static bool IsValidCellSize(float cellSize);

    bool  Valid() const { return m_valid; }
    float CellSize() const { return m_size; }
    GridPlane Plane() const { return m_plane; }

    /// A cell with the axes the plane ignores set to 0 (XZ: y, XY: z).
    CellCoord Normalize(const CellCoord& c) const;
    /// True when every active axis index is within +-kMaxCellIndex and the ignored axes are 0.
    bool InRange(const CellCoord& c) const;

    /// The cell containing `p`. False (and `out` untouched) for an invalid grid, a NaN or infinite
    /// position, or a position whose cell index is beyond +-kMaxCellIndex ("outside the world").
    bool CellOf(const Vec3& p, CellCoord& out) const;

    /// The cell's box. Axes the plane ignores get -infinity..+infinity. False for an invalid grid or an
    /// out-of-range cell.
    bool CellBox(const CellCoord& c, Vec3& lo, Vec3& hi) const;
    /// The box centre on the active axes (ignored axes 0).
    bool CellCenter(const CellCoord& c, Vec3& out) const;

    /// Distance from `p` to the cell's box on the active axes (0 inside). +infinity when the grid is
    /// invalid, the cell is out of range or `p` is not finite.
    float DistanceToCell(const Vec3& p, const CellCoord& c) const;

    /// The cells within `ring` steps of `c` on the active axes (Chebyshev), in x, y, z order, only
    /// in-range ones. `ring` is clamped to 0..32. `includeSelf` keeps `c`.
    std::vector<CellCoord> Neighbours(const CellCoord& c, int ring = 1, bool includeSelf = false) const;

    /// Every cell whose box is within `radius` of `p`, nearest first (ties by coordinate). False,
    /// and `out` empty, for an invalid grid, a bad position or radius (NaN, negative, infinite), or
    /// when more than `maxCells` cells would be listed.
    bool CellsInRadius(const Vec3& p, float radius, std::vector<CellCoord>& out, std::size_t maxCells = 100000) const;

    /// The name a cell's chunk is stored under: "cell_<x>_<y>_<z>" ("-" is written as "n": cell_n3_0_2).
    static std::string CellName(const CellCoord& c);
    /// Parse a CellName. False for anything else (including trailing text and out-of-range numbers).
    static bool ParseCellName(const std::string& name, CellCoord& out);

private:
    float m_size = 0.0f;
    GridPlane m_plane = GridPlane::XZ;
    bool m_valid = false;
    bool Active(int axis) const;   // 0 x, 1 y, 2 z
};

} // namespace okay::worldpart
