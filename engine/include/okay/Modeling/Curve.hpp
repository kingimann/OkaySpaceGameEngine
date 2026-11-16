#pragma once
// Bezier curves that turn into meshes: a pipe, a ribbon, or any 2D profile swept along the curve.
// Written from the standard cubic Bezier form and a rotation-minimising (parallel transport) frame.
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Mesh.hpp"
#include <vector>

namespace okay::modeling {

/// One control point. The handles are offsets from the point (the curve leaves along `outH` and arrives along `inH`).
struct CurvePoint {
    Vec3 p{0, 0, 0};
    Vec3 inH{0, 0, 0}, outH{0, 0, 0};
};
struct BezierCurve {
    std::vector<CurvePoint> pts;
    bool closed = false;
    int Spans() const { return pts.size() < 2 ? 0 : (int)pts.size() - (closed ? 0 : 1); }
};

/// A smooth curve through `points` (handles set from the neighbours, Catmull-Rom style; `tension` 0.5 is the usual look,
/// 0 makes straight segments).
BezierCurve CurveThrough(const std::vector<Vec3>& points, bool closed = false, float tension = 0.5f);
/// Recompute every handle of the curve the same way (after the points moved).
void SmoothHandles(BezierCurve& c, float tension = 0.5f);

/// Points along the curve: `perSpan` samples per span (clamped 1..256), the end points included; a closed curve does not
/// repeat its first point. Fewer than two control points give nothing.
std::vector<Vec3> SampleCurve(const BezierCurve& c, int perSpan);
/// Length of the sampled curve.
float CurveLength(const BezierCurve& c, int perSpan = 16);

/// A round pipe along the curve (`sides` 3..64). Open curves get end caps when `caps` is set. Empty on bad input.
Mesh CurvePipe(const BezierCurve& c, float radius, int sides = 12, int perSpan = 8, bool caps = true);
/// Any 2D `profile` (x across, y up from the curve) swept along it; `closedProfile` joins the last profile point to the first.
/// `scaleEnd` scales the profile linearly along the curve (1 = no taper). Open curves with a closed profile get caps.
Mesh CurveSweep(const BezierCurve& c, const std::vector<Vec2>& profile, bool closedProfile, int perSpan = 8, float scaleEnd = 1.0f, bool caps = true);
/// A flat strip `width` wide following the curve (it faces up where the curve runs along X).
/// `bothSides` adds the same strip facing the other way, so it shows from above and below (a flat sheet has no inside).
Mesh CurveRibbon(const BezierCurve& c, float width, int perSpan = 8, bool bothSides = false);

} // namespace okay::modeling
