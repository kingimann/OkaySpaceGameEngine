#pragma once
// Box, circle and lasso select for mesh edit mode, as plain math on screen positions, so it can be tested
// without a window. The editor projects the vertices to the screen, builds a shape from the mouse, and
// asks which vertices, edges or faces fall inside. With X-ray off, only what faces the camera can be picked
// (a vertex needs one front-facing triangle; a face must itself face the camera); with X-ray on, everything
// inside the shape is picked, even through the mesh.
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Mesh.hpp"
#include <utility>
#include <vector>

namespace okay::modeling {

struct ScreenPoint { float x = 0, y = 0; bool ok = false; };   ///< a projected vertex (ok = false when behind the camera)

struct SelectShape {
    enum class Kind { Box, Circle, Lasso } kind = Kind::Box;
    Vec2 a{0, 0}, b{0, 0};            ///< Box: two opposite corners
    Vec2 centre{0, 0}; float radius = 0;   ///< Circle
    std::vector<Vec2> poly;           ///< Lasso: the outline the mouse traced (closed automatically)
    static SelectShape Box(Vec2 p, Vec2 q);
    static SelectShape Circle(Vec2 c, float r);
    static SelectShape Lasso(std::vector<Vec2> outline);
    bool Contains(float x, float y) const;
    bool Valid() const;               ///< a lasso needs 3 points, a circle a radius above 0, a box some size
};

/// Which vertices face the camera: the ones some triangle with its front toward `eye` uses.
std::vector<char> FrontFacingVertices(const Mesh& m, const Vec3& eye);

/// Vertices whose projected position is inside the shape. `proj` is one entry per vertex.
std::vector<int> SelectVerticesIn(const Mesh& m, const std::vector<ScreenPoint>& proj, const SelectShape& s, bool xray, const Vec3& eye);
/// Triangles whose polygon (a quad counts as one) has its middle inside the shape; whole polygons are returned.
std::vector<int> SelectFacesIn(const Mesh& m, const std::vector<ScreenPoint>& proj, const SelectShape& s, bool xray, const Vec3& eye);
/// The given edges that have both ends inside the shape.
std::vector<std::pair<int, int>> SelectEdgesIn(const Mesh& m, const std::vector<std::pair<int, int>>& edges, const std::vector<ScreenPoint>& proj,
                                               const SelectShape& s, bool xray, const Vec3& eye);

} // namespace okay::modeling
