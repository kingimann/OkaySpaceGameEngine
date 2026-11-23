#pragma once
// The horizon of a set of triangles seen from a point: the edges of the visible triangles that are not shared by two
// visible triangles, as a loop of directed edges (each kept in its triangle's winding). Both the convex hull builder
// (Shape.cpp) and the expanding polytope (Collide.cpp, EPA) grow a convex polytope this way: remove what the new point
// sees, then join the horizon to the point. (de Berg et al., "Computational Geometry", ch. 11; van den Bergen 2001.)
#include <utility>
#include <vector>

namespace okay {
namespace phys {

/// `corners(faceIndex, k)` gives corner k (0..2) of a face, counter-clockwise from outside.
template <class Corners>
void HorizonEdges(const std::vector<int>& visible, Corners corners, std::vector<std::pair<int, int>>& out) {
    out.clear();
    for (int vf : visible) {
        for (int k = 0; k < 3; ++k) {
            const int a = corners(vf, k), b = corners(vf, (k + 1) % 3);
            bool shared = false;   // is the reverse edge (b, a) on another visible face?
            for (int of : visible) {
                if (of == vf) continue;
                for (int m = 0; m < 3 && !shared; ++m)
                    if (corners(of, m) == b && corners(of, (m + 1) % 3) == a) shared = true;
                if (shared) break;
            }
            if (!shared) out.emplace_back(a, b);
        }
    }
}

} // namespace phys
} // namespace okay
