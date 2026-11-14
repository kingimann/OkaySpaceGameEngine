#pragma once
// ---------------------------------------------------------------------------
// Spot and point light shadow views, shared by every renderer.
//
// One place decides how a local light looks at the scene for its shadow map:
//   * a spot light uses one perspective view down its cone,
//   * a point or area light uses six cube-face views, a little wider than 90
//     degrees so a soft filter never reads past a face edge.
// The GPU planner (ShadowSetup.hpp) and the software renderer
// (SoftwareRenderer.hpp, LocalShadowFactor) both call BuildLocalLightView, so the
// CPU picture and the GPU picture line up. This header needs only the math types
// and LightSample, so SoftwareRenderer.hpp can include it without a cycle.
// ---------------------------------------------------------------------------
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Lighting.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

/// tan(half angle) of one point-light cube face view (a little over 45 degrees).
inline constexpr float kLocalCubeTanHalf = 1.12f;

/// Cube-face basis for point-light shadows: face f looks along CubeFaceDir(f).
/// The shaders pick the face with the same dominant-axis rule as CubeFace().
inline Vec3 CubeFaceDir(int f) {
    static const Vec3 d[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    return d[f < 0 ? 0 : (f > 5 ? 5 : f)];
}
inline int CubeFace(const Vec3& v) {
    float ax = std::fabs(v.x), ay = std::fabs(v.y), az = std::fabs(v.z);
    if (ax >= ay && ax >= az) return v.x > 0.0f ? 0 : 1;
    if (ay >= az) return v.y > 0.0f ? 2 : 3;
    return v.z > 0.0f ? 4 : 5;
}

/// The shadow views of one local light.
struct LocalLightView {
    int   tiles = 0;            // 1 (spot) or 6 (point / area cube faces)
    Mat4  vp[6];                // view-projection per tile
    float texelAt1 = 0.0f;      // world size of one texel at distance 1 from the light
    float nearZ = 0.05f, farZ = 10.0f;
};

/// Build the shadow views for spot / point / area light `s` with square tiles of
/// `tileSize` texels. Same numbers the GPU planner always used.
inline LocalLightView BuildLocalLightView(const LightSample& s, int tileSize) {
    LocalLightView v;
    v.tiles = s.type == 2 ? 1 : 6;
    v.farZ = s.range;
    v.nearZ = std::clamp(s.range * 0.01f, 0.02f, 0.2f);
    if (s.type == 2) {
        // Cone half-angle from cosOuter, plus a margin for the soft filter.
        float half = std::acos(std::clamp(s.cosOuter, -1.0f, 1.0f));
        float halfDeg = std::fmin(half * 57.2957795f + 4.0f, 85.0f);
        float tanHalf = std::tan(halfDeg * 0.0174532925f);
        Vec3 f = s.dir.Normalized();
        Vec3 up = std::fabs(f.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        v.vp[0] = Mat4::Perspective(halfDeg * 2.0f, 1.0f, v.nearZ, v.farZ) *
                  Mat4::LookAt(s.pos, s.pos + f, up);
        v.texelAt1 = 2.0f * tanHalf / (float)tileSize;
    } else {
        const float fov = 2.0f * std::atan(kLocalCubeTanHalf) * 57.2957795f;
        Mat4 proj = Mat4::Perspective(fov, 1.0f, v.nearZ, v.farZ);
        for (int f = 0; f < 6; ++f) {
            Vec3 d = CubeFaceDir(f);
            Vec3 up = (f == 2 || f == 3) ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
            v.vp[f] = proj * Mat4::LookAt(s.pos, s.pos + d, up);
        }
        v.texelAt1 = 2.0f * kLocalCubeTanHalf / (float)tileSize;
    }
    return v;
}

} // namespace okay
