#pragma once
#include "okay/Render/PostQualityMath.hpp"   // OKAY_BLOOM_KNEE_SRC (shared bloom soft knee)
// Post-processing pixel shaders shared by the Direct3D renderers (HLSL text).
//
// These are the full-screen passes of the post chain: bright-pass + downsample (PSDown), the 5-tap
// linear-sampled gaussian blur (PSBlur), the "uber" pass (exposure, bloom composite, tonemap, colour
// grading, vignette: PSUber) and FXAA (PSFxaa). They were the text inside D3D11Renderer.cpp; they are
// here so Direct3D 12 can compile the very same source instead of a second copy.
//
// The host shader source must declare, before this text:
//   - struct PSQuad { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
//   - uTex (t0), uNormalTex (t1, bloom A), uSpecTex (t2, bloom C), uSampClamp (a clamp sampler);
//   - the constants uTexelDir, uGainRadius, uLiftAspect, uGammaThresh, uP0, uP1, uP2, uP7 (the D3D11 cbuffer; uP7.w is the bloom softness);
//   - aces(float3), as the mesh shader defines it.
// In Direct3D 11 these come from `kHLSL`. Direct3D 12 needs a root signature that supplies the same names.
//
// UNVERIFIED on a GPU: only text-checked (tests/test_d3d_post_hlsl.cpp) and compile-checked under MinGW.
#define OKAY_POST_HLSL \
    OKAY_BLOOM_KNEE_SRC /* soft knee (PostQualityMath.hpp); uP7.w = softness, 0 = the old hard cut */ \
    "float3 brightTap(float2 uv){ float3 c = uTex.Sample(uSampClamp, uv).rgb; float l = dot(c, float3(0.2126, 0.7152, 0.0722));\n" \
    "  float k = bloomK(l, uGammaThresh.w, uP7.w); return c * k; }\n" \
    "float4 PSDown(PSQuad i) : SV_TARGET {\n" \
    "  float2 o = uTexelDir.xy * uGainRadius.w;\n" \
    "  float3 s = brightTap(i.uv + float2(-o.x, -o.y)) + brightTap(i.uv + float2(o.x, -o.y)) + brightTap(i.uv + float2(-o.x, o.y)) + brightTap(i.uv + float2(o.x, o.y));\n" \
    "  return float4(s * 0.25, 1.0);\n" \
    "}\n" \
    "float4 PSBlur(PSQuad i) : SV_TARGET {\n" \
    "  float2 d = uTexelDir.zw;\n" \
    "  float3 s = uTex.Sample(uSampClamp, i.uv).rgb * 0.227027;\n" \
    "  s += (uTex.Sample(uSampClamp, i.uv + d * 1.384615).rgb + uTex.Sample(uSampClamp, i.uv - d * 1.384615).rgb) * 0.316216;\n" \
    "  s += (uTex.Sample(uSampClamp, i.uv + d * 3.230769).rgb + uTex.Sample(uSampClamp, i.uv - d * 3.230769).rgb) * 0.070270;\n" \
    "  return float4(s, 1.0);\n" \
    "}\n" \
    "float3 hable(float3 v){ float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;\n" \
    "  return ((v*(A*v+C*B)+D*E)/(v*(A*v+B)+D*F))-E/F; }\n" \
    "float3 filmic(float3 x){ return saturate(hable(x * 2.0) / hable(float3(11.2, 11.2, 11.2))); }\n" \
    "float4 PSUber(PSQuad i) : SV_TARGET {\n" \
    "  float4 s = uTex.Sample(uSampClamp, i.uv);\n" \
    "  float3 c = s.rgb * uP0.x;\n" \
    "  float blLum = 0.0;\n" \
    "  if (uP0.w > 0.5) {\n" \
    "    float3 bl = (uNormalTex.Sample(uSampClamp, i.uv).rgb + uSpecTex.Sample(uSampClamp, i.uv).rgb) * (0.5 * uP1.x * uP0.x);\n" \
    "    c += bl; blLum = dot(bl, float3(0.2126, 0.7152, 0.0722));\n" \
    "  }\n" \
    "  if (uP0.y > 0.5) {\n" \
    "    if (uP0.z > 1.5) c = filmic(c); else if (uP0.z > 0.5) c = aces(c); else c = saturate(c);\n" \
    "    c = pow(max(c, 0.0), 1.0 / 2.2);\n" \
    "  } else c = saturate(c);\n" \
    "  if (uP1.y > 0.5) {\n" \
    "    c *= float3(1.0 + 0.25 * uP1.z, 1.0 + 0.2 * uP1.w, 1.0 - 0.25 * uP1.z);\n" \
    "    float l = dot(c, float3(0.2126, 0.7152, 0.0722));\n" \
    "    c = l + (c - l) * uP2.x;\n" \
    "    c = (c - 0.5) * uP2.y + 0.5;\n" \
    "    c = c * (1.0 + uGainRadius.xyz) + uLiftAspect.xyz * (1.0 - c);\n" \
    "    c = pow(max(c, 0.0), 1.0 / max(1.0 + uGammaThresh.xyz * 0.75, 0.1));\n" \
    "  }\n" \
    "  float cov = saturate(s.a + blLum * 2.0);\n" \
    "  float v = 0.0;\n" \
    "  if (uP2.z > 0.0) {\n" \
    "    float2 d = (i.uv - 0.5) * float2(uLiftAspect.w, 1.0) * 2.0;\n" \
    "    float r = length(d) / sqrt(uLiftAspect.w * uLiftAspect.w + 1.0);\n" \
    "    v = 0.8 * uP2.z * smoothstep(max(1.0 - uP2.w * 1.3, 0.0), 1.05, r);\n" \
    "  }\n" \
    "  float3 pc = c * cov * (1.0 - v);\n" \
    "  float oa = 1.0 - (1.0 - v) * (1.0 - cov);\n" \
    "  return float4(oa > 1e-4 ? pc / oa : float3(0, 0, 0), oa);\n" \
    "}\n" \
    "float lumaF(float3 c){ return dot(c, float3(0.299, 0.587, 0.114)); }\n" \
    "float4 PSFxaa(PSQuad i) : SV_TARGET {\n" \
    "  float2 px = uTexelDir.xy;\n" \
    "  float4 c = uTex.Sample(uSampClamp, i.uv);\n" \
    "  float lM = lumaF(c.rgb);\n" \
    "  float lNW = lumaF(uTex.Sample(uSampClamp, i.uv + float2(-1.0, -1.0) * px).rgb);\n" \
    "  float lNE = lumaF(uTex.Sample(uSampClamp, i.uv + float2( 1.0, -1.0) * px).rgb);\n" \
    "  float lSW = lumaF(uTex.Sample(uSampClamp, i.uv + float2(-1.0,  1.0) * px).rgb);\n" \
    "  float lSE = lumaF(uTex.Sample(uSampClamp, i.uv + float2( 1.0,  1.0) * px).rgb);\n" \
    "  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n" \
    "  float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n" \
    "  if (lMax - lMin < max(0.0312, lMax * 0.125)) return c;\n" \
    "  float2 dir = float2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));\n" \
    "  float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * 0.125), 1.0 / 128.0);\n" \
    "  float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);\n" \
    "  dir = clamp(dir * rcpDirMin, -8.0, 8.0) * px;\n" \
    "  float4 rgbA = 0.5 * (uTex.Sample(uSampClamp, i.uv + dir * (1.0 / 3.0 - 0.5)) + uTex.Sample(uSampClamp, i.uv + dir * (2.0 / 3.0 - 0.5)));\n" \
    "  float4 rgbB = rgbA * 0.5 + 0.25 * (uTex.Sample(uSampClamp, i.uv + dir * -0.5) + uTex.Sample(uSampClamp, i.uv + dir * 0.5));\n" \
    "  float lB = lumaF(rgbB.rgb);\n" \
    "  return (lB < lMin || lB > lMax) ? rgbA : rgbB;\n" \
    "}\n"
