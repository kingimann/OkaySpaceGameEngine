#pragma once
// ---------------------------------------------------------------------------
// Depth of field, the maths in one place.
//
// The algorithm the OpenGL renderer runs (GLRenderer.cpp: kDofFrag), written once
// so it can be unit tested on the CPU and so the Direct3D 11 renderer shares the
// SAME numbers:
//
//   1. Depth: the half-resolution depth pass the ambient occlusion already uses
//      (view depth / far, far = 150 world units; see SsaoMath.hpp).
//   2. Blur: a half-resolution copy of the finished frame, two rounds of the
//      9-tap gaussian.
//   3. Mix: out = mix(sharp, blurred, coc), where coc (0..1) says how far the
//      pixel's distance is outside the focus range:
//          coc = clamp((|d - focus| - range / 2) / range, 0, 1)
//      Pixels with depth >= 0.995 are sky: treated as 4 x far away.
//
// The settings are the scene's Post-processing "Depth of field" switch, "Focus
// distance" (post.dofFocus) and "Focus range" (post.dofRange). Nothing here
// changes an existing scene: depth of field stays off unless the switch is on.
//
// The constants are macros so the C++ and the HLSL string (OKAY_DOF_HLSL, pasted
// into the Direct3D 11 shader source) are built from the same literals.
// test_dof_math.cpp checks both and reads the GL and D3D11 sources as text.
// The HLSL expects uTex (t0 = sharp frame), uNormalTex (t1 = blurred frame),
// uSpecTex (t2 = R32F depth / far, half res), uSampClamp, uTexelDir (xy = 1 / depth
// size) and uP7 (x = focus, y = range, z = far), plus the PSQuad struct.
//
// UNVERIFIED on a real GPU: the CPU functions are unit tested here; the HLSL and
// the Direct3D 11 pass have only been compile-checked (MinGW) and never run.
// ---------------------------------------------------------------------------
#include <cmath>

#define OKAY_DOF_STR2(x) #x
#define OKAY_DOF_STR(x) OKAY_DOF_STR2(x)

#define OKAY_DOF_BACKGROUND 0.995   // depth at or beyond this is sky
#define OKAY_DOF_SKY_MUL 4.0        // sky counts as far * this away
#define OKAY_DOF_HALF 0.5           // the in-focus zone is range * this each side of focus
#define OKAY_DOF_MIN_RANGE 1e-3     // guards the divide

namespace okay {
namespace dof {

constexpr float kBackground = (float)OKAY_DOF_BACKGROUND;
constexpr float kSkyMul = (float)OKAY_DOF_SKY_MUL;
constexpr float kHalf = (float)OKAY_DOF_HALF;
constexpr float kMinRange = (float)OKAY_DOF_MIN_RANGE;

/// World distance for a normalised depth `dn` (sky counts as 4 x far).
inline float Distance(float dn, float farDist) {
    return dn >= kBackground ? farDist * kSkyMul : dn * farDist;
}

/// Circle of confusion, 0 (sharp) .. 1 (fully blurred), for world distance `d`.
inline float Coc(float d, float focus, float range) {
    const float r = range > kMinRange ? range : kMinRange;
    float c = (std::fabs(d - focus) - range * kHalf) / r;
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

/// The final mix for one channel.
inline float Mix(float sharp, float blurred, float coc) { return sharp + (blurred - sharp) * coc; }

} // namespace dof
} // namespace okay

// ---- HLSL (Direct3D 11) -------------------------------------------------------
#define OKAY_DOF_HLSL \
    "float4 PSDof(PSQuad i) : SV_TARGET {\n" \
    "  float2 sz = 1.0 / uTexelDir.xy;\n"                  /* point fetch of the half-res depth */ \
    "  int2 q = int2(clamp(floor(i.uv * sz), float2(0.0, 0.0), sz - 1.0));\n" \
    "  float dn = uSpecTex.Load(int3(q, 0)).r;\n" \
    "  float d = dn >= " OKAY_DOF_STR(OKAY_DOF_BACKGROUND) " ? uP7.z * " OKAY_DOF_STR(OKAY_DOF_SKY_MUL) " : dn * uP7.z;\n" \
    "  float coc = clamp((abs(d - uP7.x) - uP7.y * " OKAY_DOF_STR(OKAY_DOF_HALF) ") / max(uP7.y, " OKAY_DOF_STR(OKAY_DOF_MIN_RANGE) "), 0.0, 1.0);\n" \
    "  return lerp(uTex.Sample(uSampClamp, i.uv), uNormalTex.Sample(uSampClamp, i.uv), coc);\n" \
    "}\n"
