#pragma once
// Every 2D collider as convex "pieces" (a rounded convex polygon: corner points plus a rounding radius), so the polygon,
// edge, capsule and composite colliders all collide, ray-cast and overlap as the solid shapes they are, never as their
// bounding boxes. Circle = 1 point + radius, Capsule = 2 points + radius, Box / convex Polygon = corners, Edge = one
// 2-point piece per segment. Written from general convex-geometry definitions (support points, ear clipping, slab clipping).
#include "okay/Physics/Collider2D.hpp"
#include <vector>

namespace okay {
namespace shape2d {

struct Piece {
    std::vector<Vec2> v;     ///< corners, counter-clockwise (1 = circle, 2 = segment / capsule, 3+ = polygon)
    float r = 0.0f;          ///< rounding radius added all round
    Vec2 mn{0, 0}, mx{0, 0}; ///< bounds, filled by Bounds()
    void Bounds();
};

/// Furthest point of a piece along `d`.
Vec2 Support(const Piece& p, const Vec2& d);
/// Split a closed outline into convex polygons (the outline itself when it is already convex; ear clipping otherwise).
std::vector<std::vector<Vec2>> ConvexPieces(const std::vector<Vec2>& outline);
/// The pieces of a collider in world space. `withSkin` adds the collider's contact offset to every piece's radius.
void PiecesOf(const Collider2D* c, std::vector<Piece>& out, bool withSkin = true);
/// Exact ray test against one piece. n is the outward normal at the hit.
bool RayPiece(const Piece& p, const Vec2& o, const Vec2& d, float maxT, float& t, Vec2& n);
/// True for the shapes handled through pieces: Polygon, Edge, Composite.
bool IsGeneric(const Collider2D* c);

}  // namespace shape2d

/// The Inspector rows and saved-scene record of the Composite collider.
namespace detail { void RegisterColliderShape2DSerializers(); }

} // namespace okay
