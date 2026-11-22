#pragma once
// ---------------------------------------------------------------------------
// Narrow phase of our own physics core (okay::phys): distance and penetration between two convex shapes, and the
// contact manifold (up to four points) between them.
//
//  * GJK distance with the Voronoi-region sub-simplex search: E. Gilbert, D. Johnson, S. Keerthi, "A fast procedure for
//    computing the distance between complex objects in three-dimensional space" (IEEE J. Robotics and Automation, 1988);
//    G. van den Bergen, "A fast and robust GJK implementation for collision detection of convex objects" (1999) for the
//    termination test; closest points on segments, triangles and tetrahedra from C. Ericson, "Real-Time Collision
//    Detection" (2005), 5.1 and 9.5.
//  * Penetration depth by the expanding polytope algorithm: G. van den Bergen, "Proximity queries and penetration depth
//    computation on 3D game objects" (GDC 2001).
//  * Manifolds by clipping the incident feature against the reference face (Sutherland-Hodgman, Ericson 8.3.4) and
//    keeping at most four points that cover the largest area (the reduction described by D. Gregorius, "Robust contact
//    creation for physics simulations", GDC 2015 talk).
//  * Shapes are a core plus a radius (spheres, capsules), so round shapes are exact and cheap.
// No other engine's code was read or used.
// ---------------------------------------------------------------------------
#include "okay/Physics/Core/Shape.hpp"
#include <cstdint>

namespace okay {
namespace phys {

struct DistanceResult {
    bool overlap = false;   // the cores overlap (distance 0)
    float distance = 0.0f;  // between the cores (radius not included)
    V3 pA, pB;              // closest points on the cores (world)
    V3 normal;              // from A toward B (unit) when !overlap
    int iterations = 0;
};

/// Distance between the cores of two shapes (GJK).
DistanceResult GjkDistance(const Shape& a, const Xf& xa, const Shape& b, const Xf& xb);

struct PenetrationResult {
    bool ok = false;
    V3 normal;              // from A toward B: moving B by normal * depth separates the cores
    float depth = 0.0f;
    V3 pA, pB;              // deepest points on the cores (world)
};
/// Penetration of overlapping cores (EPA).
PenetrationResult EpaPenetration(const Shape& a, const Xf& xa, const Shape& b, const Xf& xb);

struct ContactPoint {
    V3 pA, pB;              // points on the surfaces of A and B (world)
    float separation = 0;   // along the manifold normal; < 0 = overlapping
};

struct Manifold {
    V3 normal;              // from A toward B
    int count = 0;
    ContactPoint pts[4];
};

/// Contact manifold between two shapes when they are closer than `speculative` (points with separation up to that
/// distance are kept, so the solver can stop a body before it reaches the other: speculative contacts). Returns false
/// when they are farther apart.
bool Collide(const Shape& a, const Xf& xa, const Shape& b, const Xf& xb, float speculative, Manifold& out);

/// Reduce a set of contact points to at most four: the deepest, then the ones that span the largest area.
int ReduceContacts(ContactPoint* pts, int count, const V3& normal);

} // namespace phys
} // namespace okay
