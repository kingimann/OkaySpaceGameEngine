#pragma once
// ---------------------------------------------------------------------------
// Screen-space ambient occlusion (SSAO), the maths in one place.
//
// This is the algorithm the OpenGL renderer runs (GLRenderer.cpp: kSsaoFrag,
// kBlurFrag, kMulFrag), written once so it can be unit tested on the CPU and so
// the Direct3D 11 renderer shares the SAME numbers:
//
//   1. Depth: opaque geometry as depth / far (0..1) at half resolution.
//   2. Estimate: for each pixel, 12 taps on a golden-angle spiral around it. A tap
//      whose depth is nearer than the pixel's by more than 0.03 world units, and
//      by less than 1.6 x radius, counts as an occluder, fading out with the gap.
//   3. Blur: a 9-tap gaussian (5 linear taps), horizontal then vertical.
//   4. Apply: colour *= mix(1, ao, strength).
//
// The constants are macros so the C++ below and the HLSL string (OKAY_SSAO_HLSL,
// pasted into the Direct3D 11 shader source) are built from the very same
// literals. test_ssao_math.cpp checks both. The HLSL expects the host shader to
// declare uTex (t0), uNormalTex (t1), uSampClamp, uTexelDir and uP7 (x = radius,
// y = far, z = strength, w = AO power) and the PSQuad struct, as D3D11Renderer.cpp does.
//
// UNVERIFIED on a real GPU: the CPU functions are unit tested here, the HLSL and
// the Direct3D 11 pass that uses it have only been compile-checked (MinGW).
// ---------------------------------------------------------------------------
#include <cmath>
#include <cstddef>
#include <vector>

#define OKAY_SSAO_STR2(x) #x
#define OKAY_SSAO_STR(x) OKAY_SSAO_STR2(x)

#define OKAY_SSAO_FAR 150.0              // world units that depth 1.0 stands for
#define OKAY_SSAO_TAPS 12                // taps on the spiral
#define OKAY_SSAO_TAPS_M1 11             // OKAY_SSAO_TAPS - 1, for the HLSL ring formula
#define OKAY_SSAO_GOLDEN 2.618034        // radians between taps (golden angle)
#define OKAY_SSAO_BACKGROUND 0.995       // depth at or beyond this is sky: no occlusion
#define OKAY_SSAO_MIN_DIST 0.5           // pixels nearer than this (world) use this for the radius
#define OKAY_SSAO_SR_MIN 0.004           // smallest screen radius (uv)
#define OKAY_SSAO_SR_MAX 0.12            // largest screen radius (uv)
#define OKAY_SSAO_RING_MIN 0.25          // inner ring: radius fraction of the first tap
#define OKAY_SSAO_RING_SPAN 0.75         // the taps cover 0.25 .. 1.0 of the radius
#define OKAY_SSAO_RANGE_SCALE 1.6        // occluders further than radius * this are ignored
#define OKAY_SSAO_MIN_DIFF 0.03          // occluder must be nearer by more than this (world)
#define OKAY_SSAO_BLUR_W0 0.227027       // gaussian centre weight
#define OKAY_SSAO_BLUR_W1 0.316216       // weight of the tap pair at +-OFF1
#define OKAY_SSAO_BLUR_W2 0.070270       // weight of the tap pair at +-OFF2
#define OKAY_SSAO_BLUR_OFF1 1.384615     // texels (linear-sampled)
#define OKAY_SSAO_BLUR_OFF2 3.230769

namespace okay {
namespace ssao {

static_assert(OKAY_SSAO_TAPS - 1 == OKAY_SSAO_TAPS_M1, "keep the HLSL ring formula in step");

constexpr float kFar = (float)OKAY_SSAO_FAR;
constexpr int   kTaps = OKAY_SSAO_TAPS;
constexpr float kGolden = (float)OKAY_SSAO_GOLDEN;
constexpr float kBackground = (float)OKAY_SSAO_BACKGROUND;
constexpr float kMinDist = (float)OKAY_SSAO_MIN_DIST;
constexpr float kScreenRadiusMin = (float)OKAY_SSAO_SR_MIN;
constexpr float kScreenRadiusMax = (float)OKAY_SSAO_SR_MAX;
constexpr float kRingMin = (float)OKAY_SSAO_RING_MIN;
constexpr float kRingSpan = (float)OKAY_SSAO_RING_SPAN;
constexpr float kRangeScale = (float)OKAY_SSAO_RANGE_SCALE;
constexpr float kMinDiff = (float)OKAY_SSAO_MIN_DIFF;
constexpr float kBlurW0 = (float)OKAY_SSAO_BLUR_W0;
constexpr float kBlurW1 = (float)OKAY_SSAO_BLUR_W1;
constexpr float kBlurW2 = (float)OKAY_SSAO_BLUR_W2;
constexpr float kBlurOff1 = (float)OKAY_SSAO_BLUR_OFF1;
constexpr float kBlurOff2 = (float)OKAY_SSAO_BLUR_OFF2;

/// The tap radius on screen, in uv units, for a pixel at normalised depth `d`
/// (world distance = d * kFar). World radius shrinks on screen with distance.
inline float ScreenRadius(float radius, float d) {
    float dist = d * kFar; if (dist < kMinDist) dist = kMinDist;
    float sr = radius / dist;
    return sr < kScreenRadiusMin ? kScreenRadiusMin : (sr > kScreenRadiusMax ? kScreenRadiusMax : sr);
}

/// Offset (uv) of tap `i` for screen radius `sr`. `aspect` = texelX / texelY, so
/// the spiral stays round on a non-square target.
inline void TapOffset(int i, float sr, float aspect, float& ox, float& oy) {
    const float a = (float)i * kGolden;
    const float rr = sr * (kRingMin + kRingSpan * (float)i / (float)(kTaps - 1));
    ox = std::cos(a) * rr * aspect;
    oy = std::sin(a) * rr;
}

/// Occlusion (0..1) one tap adds: `d` is the pixel's depth, `ds` the tap's, both
/// normalised. A tap nearer than the pixel by gap `diffW` (world units) occludes
/// when 0.03 < gap < 1.6 x radius, fading to 0 at the far end.
inline float TapOcclusion(float d, float ds, float radius) {
    const float diffW = (d - ds) * kFar;
    const float range = radius * kRangeScale;
    if (diffW > kMinDiff && diffW < range) return 1.0f - diffW / range;
    return 0.0f;
}

/// Ambient light left (1 = open, lower = occluded) at uv (u, v). `depthAt(u, v)`
/// returns the normalised depth there. `texelX/Y` are the target's texel size.
template <class DepthFn>
inline float AoAt(DepthFn depthAt, float u, float v, float texelX, float texelY, float radius) {
    const float d = depthAt(u, v);
    if (d >= kBackground) return 1.0f;
    const float sr = ScreenRadius(radius, d);
    const float aspect = texelX / texelY;
    float occ = 0.0f;
    for (int i = 0; i < kTaps; ++i) {
        float ox, oy; TapOffset(i, sr, aspect, ox, oy);
        occ += TapOcclusion(d, depthAt(u + ox, v + oy), radius);
    }
    return 1.0f - occ / (float)kTaps;
}

/// The final multiply: colour *= Apply(ao, strength). Strength is capped at 1.
inline float Apply(float ao, float strength) {
    if (strength > 1.0f) strength = 1.0f;
    return 1.0f + (ao - 1.0f) * strength;
}

/// A w*h float image with clamp-to-edge bilinear sampling at texel centres, the
/// CPU twin of a linear-filtered texture fetch. Used by the reference passes.
struct Grid {
    int w = 0, h = 0;
    std::vector<float> px;
    Grid() = default;
    Grid(int W, int H, float fill = 0.0f) : w(W), h(H), px((std::size_t)W * H, fill) {}
    float& at(int x, int y) { return px[(std::size_t)y * w + x]; }
    float at(int x, int y) const { return px[(std::size_t)y * w + x]; }
    float texel(int x, int y) const {
        x = x < 0 ? 0 : (x >= w ? w - 1 : x); y = y < 0 ? 0 : (y >= h ? h - 1 : y);
        return px[(std::size_t)y * w + x];
    }
    /// Nearest texel for uv (what a point / Load fetch of the depth image does).
    float point(float u, float v) const {
        return texel((int)std::floor(u * (float)w), (int)std::floor(v * (float)h));
    }
    float bilinear(float u, float v) const {
        const float fx = u * (float)w - 0.5f, fy = v * (float)h - 0.5f;
        const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        const float ax = fx - (float)x0, ay = fy - (float)y0;
        const float a = texel(x0, y0) + (texel(x0 + 1, y0) - texel(x0, y0)) * ax;
        const float b = texel(x0, y0 + 1) + (texel(x0 + 1, y0 + 1) - texel(x0, y0 + 1)) * ax;
        return a + (b - a) * ay;
    }
};

/// Step 2 on the CPU: AO for every texel of a depth image (normalised depth).
inline Grid Estimate(const Grid& depth, float radius) {
    Grid out(depth.w, depth.h, 1.0f);
    const float tx = 1.0f / (float)depth.w, ty = 1.0f / (float)depth.h;
    auto fetch = [&](float u, float v) { return depth.point(u, v); };
    for (int y = 0; y < depth.h; ++y)
        for (int x = 0; x < depth.w; ++x)
            out.at(x, y) = AoAt(fetch, ((float)x + 0.5f) * tx, ((float)y + 0.5f) * ty, tx, ty, radius);
    return out;
}

/// Step 3 on the CPU: one gaussian pass along (dx, dy) = (1, 0) or (0, 1).
inline Grid Blur(const Grid& in, int dx, int dy) {
    Grid out(in.w, in.h, 1.0f);
    const float tx = (float)dx / (float)in.w, ty = (float)dy / (float)in.h;
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x) {
            const float u = ((float)x + 0.5f) / (float)in.w, v = ((float)y + 0.5f) / (float)in.h;
            float s = in.bilinear(u, v) * kBlurW0;
            s += (in.bilinear(u + tx * kBlurOff1, v + ty * kBlurOff1) + in.bilinear(u - tx * kBlurOff1, v - ty * kBlurOff1)) * kBlurW1;
            s += (in.bilinear(u + tx * kBlurOff2, v + ty * kBlurOff2) + in.bilinear(u - tx * kBlurOff2, v - ty * kBlurOff2)) * kBlurW2;
            out.at(x, y) = s;
        }
    return out;
}

} // namespace ssao
} // namespace okay

// ---- HLSL (Direct3D 11) -------------------------------------------------------
// Pasted into the Direct3D 11 shader source after the post-chain declarations.
// PSAoDepth writes view depth / far (SV_Position.w is 1 / clip w); PSAo is the
// estimate; the existing PSBlur is the blur; PSAoMul applies the result.
#define OKAY_SSAO_HLSL \
    "float4 PSAoDepth(float4 pos : SV_POSITION) : SV_TARGET {\n" \
    "  return float4(saturate((1.0 / pos.w) / uP7.y), 0.0, 0.0, 1.0);\n" \
    "}\n" \
    "float ssaoDepthAt(float2 uv) {\n"                       /* point fetch: depths must not be blended */ \
    "  float2 sz = 1.0 / uTexelDir.xy;\n" \
    "  int2 q = int2(clamp(floor(uv * sz), float2(0.0, 0.0), sz - 1.0));\n" \
    "  return uTex.Load(int3(q, 0)).r;\n" \
    "}\n" \
    "float4 PSAo(PSQuad i) : SV_TARGET {\n" \
    "  float d = ssaoDepthAt(i.uv);\n" \
    "  if (d >= " OKAY_SSAO_STR(OKAY_SSAO_BACKGROUND) ") return float4(1.0, 1.0, 1.0, 1.0);\n" \
    "  float dist = max(d * " OKAY_SSAO_STR(OKAY_SSAO_FAR) ", " OKAY_SSAO_STR(OKAY_SSAO_MIN_DIST) ");\n" \
    "  float sr = clamp(uP7.x / dist, " OKAY_SSAO_STR(OKAY_SSAO_SR_MIN) ", " OKAY_SSAO_STR(OKAY_SSAO_SR_MAX) ");\n" \
    "  float occ = 0.0;\n" \
    "  float range = uP7.x * " OKAY_SSAO_STR(OKAY_SSAO_RANGE_SCALE) ";\n" \
    "  [unroll] for (int k = 0; k < " OKAY_SSAO_STR(OKAY_SSAO_TAPS) "; k++) {\n" \
    "    float a = float(k) * " OKAY_SSAO_STR(OKAY_SSAO_GOLDEN) ";\n" \
    "    float rr = sr * (" OKAY_SSAO_STR(OKAY_SSAO_RING_MIN) " + " OKAY_SSAO_STR(OKAY_SSAO_RING_SPAN) " * float(k) / " OKAY_SSAO_STR(OKAY_SSAO_TAPS_M1) ");\n" \
    "    float2 o = float2(cos(a), sin(a)) * rr;\n" \
    "    o.x *= uTexelDir.x / uTexelDir.y;\n" \
    "    float ds = ssaoDepthAt(i.uv + o);\n" \
    "    float diffW = (d - ds) * " OKAY_SSAO_STR(OKAY_SSAO_FAR) ";\n" \
    "    if (diffW > " OKAY_SSAO_STR(OKAY_SSAO_MIN_DIFF) " && diffW < range) occ += 1.0 - diffW / range;\n" \
    "  }\n" \
    "  float ao = 1.0 - occ / " OKAY_SSAO_STR(OKAY_SSAO_TAPS) ".0;\n" \
    "  return float4(ao, ao, ao, 1.0);\n" \
    "}\n" \
    "float4 PSAoMul(PSQuad i) : SV_TARGET {\n"                /* colour in t0, blurred AO in t1 */ \
    "  float4 c = uTex.Sample(uSampClamp, i.uv);\n" \
    "  float a = uNormalTex.Sample(uSampClamp, i.uv).r;\n" \
    "  if (uP7.w != 1.0) a = pow(max(a, 0.0), uP7.w);\n"        /* PostQualityMath.hpp: AoCurve; w = power, 1 = unchanged */ \
    "  return float4(c.rgb * lerp(1.0, a, saturate(uP7.z)), c.a);\n" \
    "}\n"
