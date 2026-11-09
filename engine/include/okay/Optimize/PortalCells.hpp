#pragma once
// ---------------------------------------------------------------------------
// Portal / cell visibility (feature map 27.2). Pure geometry.
//
// The level is cut into convex cells (rooms, corridors; axis aligned boxes)
// joined by portals (doorways, windows: convex polygons that connect two
// cells). From the camera's cell a breadth-first walk crosses every open
// portal the camera can see through, narrowing the view frustum at each one
// (the planes through the eye and the portal's clipped outline), so a cell is
// reported visible only if some line of sight reaches it through a chain of
// portals. Objects then only need a test against the narrowed frusta of their
// own cell.
// ---------------------------------------------------------------------------
#include "okay/Math/Bounds.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>
#include <vector>

namespace okay::optimize {

/// A plane with an inward-pointing unit normal: inside means Distance() >= 0.
struct VisPlane {
    Vec3  n{0, 1, 0};
    float d = 0.0f;
    float Distance(const Vec3& p) const { return Vec3::Dot(n, p) + d; }
};

/// A convex region bounded by planes (a view frustum, or a frustum narrowed by portals).
struct ConvexRegion {
    std::vector<VisPlane> planes;
    /// Left, right, bottom, top, near, far planes of a view-projection matrix (clip = vp * p).
    static ConvexRegion FromViewProjection(const Mat4& vp);
    bool ContainsPoint(const Vec3& p) const;
    /// Conservative box test: false only when the box is fully outside one plane.
    bool IntersectsBounds(const Bounds& b) const;
    bool IntersectsSphere(const Vec3& c, float r) const;
};

/// Clip a convex polygon to a region. Returns the vertices that remain (empty when fully outside).
std::vector<Vec3> ClipPolygon(const std::vector<Vec3>& polygon, const ConvexRegion& region);

struct PortalCell {
    Bounds bounds;
    std::string name;
    std::vector<int> portals;     ///< ids of the portals touching this cell
};

struct Portal {
    int cellA = -1, cellB = -1;
    std::vector<Vec3> polygon;    ///< convex outline, at least 3 points
    bool open = true;             ///< a closed door blocks sight
    std::string name;
};

struct PortalVisibility {
    int  cameraCell = -1;
    bool cameraOutside = false;                   ///< the eye is in no cell: treat everything as visible
    std::vector<int> visibleCells;                ///< ascending, unique
    std::vector<std::vector<ConvexRegion>> regions;   ///< per cell id: the narrowed frusta it is seen through
    int portalsTested = 0;
    int portalsPassed = 0;
    int nodesVisited = 0;
    bool truncated = false;                       ///< hit the visit budget (result is still conservative-incomplete)
    bool CellVisible(int cell) const;
    /// True when `bounds`, which lives in `cell`, is inside one of that cell's narrowed frusta.
    bool BoundsVisible(int cell, const Bounds& bounds) const;
};

class PortalCells {
public:
    int AddCell(const Bounds& bounds, const std::string& name = std::string());
    /// A portal between two cells with the given convex outline.
    int AddPortal(int cellA, int cellB, const std::vector<Vec3>& polygon, bool open = true,
                  const std::string& name = std::string());
    /// A rectangular portal: `center` plus/minus `halfU` and `halfV` (two perpendicular vectors in its plane).
    int AddPortalRect(int cellA, int cellB, const Vec3& center, const Vec3& halfU, const Vec3& halfV,
                      bool open = true, const std::string& name = std::string());
    void SetPortalOpen(int portal, bool open);
    int  CellCount() const { return static_cast<int>(cells_.size()); }
    int  PortalCount() const { return static_cast<int>(portals_.size()); }
    const PortalCell& Cell(int id) const { return cells_[id]; }
    const Portal& GetPortal(int id) const { return portals_[id]; }

    /// The cell containing `p` (the smallest one when cells overlap, lowest id on a tie), or -1.
    int FindCell(const Vec3& p) const;

    /// Walk the portals from the eye's cell. `maxDepth` limits how many portals deep the walk goes,
    /// `maxVisits` caps the total work.
    PortalVisibility ComputeVisibility(const Vec3& eye, const Mat4& viewProj, int maxDepth = 32,
                                       int maxVisits = 4096) const;

private:
    std::vector<PortalCell> cells_;
    std::vector<Portal> portals_;
};

} // namespace okay::optimize
