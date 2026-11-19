#pragma once
// ---------------------------------------------------------------------------
// Shadow maths shared by every renderer (software first; the GPU backends call the same functions).
// Pure functions, no scene or window needed, so the unit tests (tests/test_lighting_upgrade.cpp) check them directly.
//
// Written from general shadow-mapping knowledge (texel-grid snapping, receiver-plane / slope bias, a blocker search
// that widens the filter with the gap between caster and receiver, a distance fade) in our own words and structure.
// No code, shader text or tests were taken from another project (CLAUDE.md rule 19a). Constants are our own and are
// pinned by tests.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mat4.hpp"
#include <algorithm>
#include <cmath>

namespace okay {
namespace shadowmath {

/// Stable cascades. A cascade's shadow map is an orthographic box that follows the camera. If the box slides by a
/// fraction of a texel, every shadow edge is re-sampled on a new grid and the edges crawl ("shimmer"). The fix is to
/// make the texel grid belong to the WORLD: this returns the sideways shift (in light-view units, to be applied after
/// the view matrix as a translation) that moves the box so that world texel boundaries stay on whole-texel steps.
/// `view` is the light's view matrix for this cascade; `texel` the world size of one shadow texel.
/// The earlier code rounded the centre of the box in the light's own view, which is always (0, 0) there, so it did
/// nothing; the grid is anchored to the world origin here instead.
inline Vec3 CascadeSnapShift(const Mat4& view, float texel) {
    if (!(texel > 0.0f)) return Vec3{0, 0, 0};
    const Vec4 o = view * Vec4{0.0f, 0.0f, 0.0f, 1.0f};   // world origin as seen by the light
    const float cx = -o.x, cy = -o.y;                       // the box centre, measured from the world origin
    return Vec3{cx - std::floor(cx / texel) * texel, cy - std::floor(cy / texel) * texel, 0.0f};
}

/// Tangent of the angle between the surface normal and the light, from n.l (1 = facing the light, 0 = edge on).
/// Clamped so that an almost edge-on surface does not ask for an enormous bias.
inline float SlopeTan(float ndl) {
    ndl = ndl < 0.0f ? 0.0f : (ndl > 1.0f ? 1.0f : ndl);
    const float s = std::sqrt(std::fmax(0.0f, 1.0f - ndl * ndl));
    const float t = s / std::fmax(ndl, 0.12f);
    return t > 6.0f ? 6.0f : t;
}

/// How far (world units) to push the receiver sideways along its normal before the lookup. Grows with the slope:
/// a surface that faces the light needs almost none, a grazing one needs more. `mul` is the light's Bias setting.
inline float NormalOffset(float ndl, float texelWorld, float mul) {
    ndl = ndl < 0.0f ? 0.0f : (ndl > 1.0f ? 1.0f : ndl);
    const float sinT = std::sqrt(std::fmax(0.0f, 1.0f - ndl * ndl));
    return texelWorld * (0.6f + 1.8f * sinT) * mul;
}

/// Receiver-plane depth shift: how much farther from the light (along its direction `lightDir`, unit) the SURFACE is at a filter tap
/// that lies (`ox`, `oy`) shadow-map texels away from the shaded point, where `axisX` / `axisY` are the world directions of the
/// shadow map's x and y and `ny` flips y (texel rows run downwards). The surface through the point has normal `n`; moving along
/// it by d changes its depth along the light by -(n.d)/(n.L). Comparing each tap with this depth, instead of one depth for all taps,
/// is what stops stripes on sloped ground WITHOUT a big bias: a big bias lets thin or low objects lose their shadow, which is worst
/// in the far cascades where one texel is large. Clamped to +-8 texels for surfaces almost edge-on to the light.
inline float ReceiverPlaneShift(const Vec3& n, const Vec3& lightDir, const Vec3& axisX, const Vec3& axisY,
                                float ox, float oy, float texelWorld) {
    float nl = Vec3::Dot(n, lightDir);                       // negative for a surface facing the light
    const float mag = std::fabs(nl) < 0.1f ? 0.1f : std::fabs(nl);
    const float nd = Vec3::Dot(n, axisX) * ox * texelWorld - Vec3::Dot(n, axisY) * oy * texelWorld;
    float s = -nd / (nl < 0.0f ? -mag : mag);
    const float lim = 8.0f * texelWorld;
    return s > lim ? lim : (s < -lim ? -lim : s);
}

/// Depth bias in WORLD units (along the light direction). A filter that reads `filterTexels` away from the centre
/// sees a sloped surface that much nearer or farther, so the slope term is multiplied by the filter's reach: this is
/// what stops stripes ("acne") on sloped ground without detaching shadows on flat ground. Capped, because an unlimited bias
/// lets the sun through a thin wall (tests/test_lighting_upgrade.cpp "closed room").
inline float DepthBias(float ndl, float texelWorld, float filterTexels, float mul) {
    float slope = (filterTexels + 0.5f) * SlopeTan(ndl);
    if (slope > 6.0f) slope = 6.0f;      // never more than ~7 texels, so thin walls and floors do not let light through
    return texelWorld * (0.7f + slope) * mul;
}

/// 1 close to the camera, falling smoothly to 0 at `distance`; the last `fadeFraction` of the range is the fade.
/// `distance` <= 0 means "no limit" (always 1).
inline float DistanceFade(float d, float distance, float fadeFraction = 0.2f) {
    if (!(distance > 0.0f)) return 1.0f;
    const float start = distance * (1.0f - fadeFraction);
    if (d <= start) return 1.0f;
    if (d >= distance) return 0.0f;
    const float t = (d - start) / (distance - start);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

/// Soft-shadow filter radius in texels. `gapWorld` is the average distance between the blocker and the receiver
/// (0 when they touch), `softness` 0..1 is the light's "Soft Edges" setting, `baseTexels` the global minimum.
/// A shadow is sharp where the object touches the ground and gets softer the farther the shadow falls from it.
inline float PenumbraTexels(float gapWorld, float texelWorld, float softness, float baseTexels, float maxTexels = 14.0f) {
    if (!(texelWorld > 0.0f)) return baseTexels;
    softness = softness < 0.0f ? 0.0f : (softness > 1.0f ? 1.0f : softness);
    const float width = 0.35f * softness * (gapWorld > 0.0f ? gapWorld : 0.0f) / texelWorld;
    const float r = baseTexels + width;
    return r > maxTexels ? maxTexels : r;
}

/// The k-th of n filter taps on a disc of radius 1 (golden-angle spiral: even coverage for any n, no random numbers,
/// so renders are repeatable). Sets x, y.
inline void DiscTap(int k, int n, float& x, float& y) {
    const float r = std::sqrt(((float)k + 0.5f) / (float)(n > 0 ? n : 1));
    const float a = (float)k * 2.39996323f;
    x = r * std::cos(a); y = r * std::sin(a);
}

} // namespace shadowmath
} // namespace okay
