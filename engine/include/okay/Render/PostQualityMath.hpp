#pragma once
// ---------------------------------------------------------------------------
// Two post-processing quality settings, the maths in one place.
//
//   1. Bloom Softness (post.bloomSoftness, 0..1). Today the bloom bright-pass is
//      a hard cut: a pixel glows by (luma - threshold) and not at all below the
//      threshold, so a pixel crossing the threshold makes the glow pop. A soft
//      knee blends that corner: around the threshold, over +-knee (knee =
//      threshold * softness), the glow follows a smooth curve instead.
//          s = clamp(l - t + knee, 0, 2 * knee);  s = s * s / (4 * knee + 1e-5)
//          gain = max(s, l - t, 0) / max(l, 1e-4)        (the pixel is scaled by gain)
//      With softness 0 the knee is 0, s is 0, and this is exactly today's
//      max(l - t, 0) / max(l, 1e-4). Default 0, so no existing scene changes.
//
//   2. Ambient Occlusion Power (post.ssaoPower, 0.25..4). The blurred occlusion
//      value `ao` (1 = open) is raised to this power before it is applied:
//      colour *= mix(1, ao ^ power, strength). Above 1 the dark creases get darker
//      and shallow occlusion fades faster (a tighter, crisper contact shadow);
//      below 1 the effect spreads wider and lighter. Power 1 is exactly today's
//      look. Default 1.
//
// The GLSL / HLSL function text (OKAY_BLOOM_KNEE_SRC) is one string: both
// languages accept it as written. It is pasted into the OpenGL bright-pass shader
// and the Direct3D 11 PSDown shader. test_post_quality_math.cpp checks the maths
// on the CPU and that both renderers paste the same text.
//
// UNVERIFIED on a real GPU: only the CPU maths is run here; the shaders are
// compile-checked (Direct3D 11, MinGW) or not compiled at all (GLSL).
// ---------------------------------------------------------------------------
#include <cmath>

namespace okay {
namespace postq {

/// Scale (0..1) applied to a pixel of luma `l` by the bloom bright-pass.
inline float BloomGain(float l, float threshold, float softness) {
    const float knee = threshold * softness;
    float s = l - threshold + knee;
    s = s < 0.0f ? 0.0f : (s > 2.0f * knee ? 2.0f * knee : s);
    s = s * s / (4.0f * knee + 1e-5f);
    float hard = l - threshold;
    float m = s > hard ? s : hard;
    m = m > 0.0f ? m : 0.0f;
    return m / (l > 1e-4f ? l : 1e-4f);
}

/// The occlusion actually applied: ao ^ power (power 1 returns ao unchanged).
inline float AoCurve(float ao, float power) {
    if (power == 1.0f) return ao;
    return std::pow(ao < 0.0f ? 0.0f : ao, power);
}

/// colour *= ApplyPowered(ao, strength, power); strength is capped at 1 like ssao::Apply.
inline float ApplyPowered(float ao, float strength, float power) {
    if (strength > 1.0f) strength = 1.0f;
    return 1.0f + (AoCurve(ao, power) - 1.0f) * strength;
}

} // namespace postq
} // namespace okay

#define OKAY_BLOOM_KNEE_SRC \
    "float bloomK(float l, float t, float soft){ float knee = t * soft;\n" \
    "  float s = clamp(l - t + knee, 0.0, 2.0 * knee); s = s * s / (4.0 * knee + 1e-5);\n" \
    "  return max(max(s, l - t), 0.0) / max(l, 1e-4); }\n"
