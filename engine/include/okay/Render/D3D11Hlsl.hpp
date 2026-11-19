#pragma once
// The Direct3D 11 mesh / post HLSL source as one string. Moved out of D3D11Renderer.cpp unchanged (same text, same
// macros) so tools that are not Windows programs can read it: scripts/xbox-compile-shaders.ps1 asks `okay-shaders dump`
// for this text and compiles each entry point with the SDK's fxc.exe at BUILD time (D3DCompile does not exist in a UWP
// app). The desktop renderer still compiles the very same string at run time. Pure text, no Direct3D include.
// UNVERIFIED on a GPU (see docs/xbox_uwp.md).
#include "okay/Render/PostQualityMath.hpp"
#include "okay/Render/D3DPostHLSL.hpp"
#include "okay/Render/DofMath.hpp"
#include "okay/Render/SsaoMath.hpp"

namespace okay {

inline const char* D3D11HlslSource() {
  static const char* const kText =
    "cbuffer CB : register(b0) {\n"
    "  float4x4 uMVP; float4x4 uModel;\n"
    "  float3 uColor;     float uSpecular;\n"
    "  float3 uEmissive;  float uShininess;\n"
    "  float3 uLightDir;  float uUnlit;\n"
    "  float3 uLightColor; float uUseTex;\n"
    "  float3 uAmbient;   float uHasNormal;\n"      // _p0 repurposed: 1 = sample normal map
    "  float3 uEye;       float uNormalStrength;\n" // _p1 repurposed: bump intensity
    "  float2 uTiling;    float uShadowAlpha; float uAlpha;\n"   // _p2 repurposed: material opacity
    "  float3 uRimColor;  float uShaderMode;\n"
    "  float3 uGradTop;   float uToonBands;\n"
    "  float3 uGradBot;   float uRimStr;\n"
    "  float4 uPbr;\n"                        // x=metallic y=reflectivity z=hasSpecMap w=hasAo
    "  float4 uPbr2;\n"                       // x=aoStrength y=envOn z=texOffset.x w=texOffset.y
    "  float4 uSky[3];\n"                     // [0]=top [1]=horizon [2]=bottom (xyz) for reflections
    "  float4 uLightCount;\n"                 // .x = number of scene lights (0 = use uLightDir)
    "  float4 uLights[96];\n"                 // 16 lights x 6: [dir,type][pos,range][col,cosOut][cosIn,falloff,halfW,halfH][right,cookie row][up,cookie scale]
    "  float4 uFog;\n"                        // x=on y=start z=end (distance fog) w=receive shadows
    "  float4 uFogCol;\n"                     // xyz = fog colour, w = filmic pipeline flag
    "  float4 uFog2;\n"                       // x=mode (0 linear 1 exponential 2 height) y=density z=height w=falloff
    "  float4 uPost;\n"                       // x=1: leave the colour linear for the post chain
    "};\n"
    // Shadows + ambient: once per frame (b2). Sun = cascades side by side in t4;
    // spot/point lights = a 4x4 tile atlas in t5 (see ShadowSetup.hpp).
    "cbuffer SH : register(b2) {\n"
    "  float4x4 uCasVP[4];\n"
    "  float4 uCasSphere[4];\n"               // xyz = centre, w = radius
    "  float4 uCasInfo[4];\n"                 // x = world texel size
    "  float4 uShadow;\n"                     // x=on y=cascade count z=cascade size w=softness (texels)
    "  float4 uShadow2;\n"                    // x=sun light index y=sun strength z=sun bias w=local tile size
    "  float4 uAmbSky;\n"                     // ambient for up-facing surfaces (scene Ambient Mode)
    "  float4 uAmbGround;\n"                  // ... and down-facing ones
    "  float4x4 uLocalVP[16];\n"
    "  float4 uLShadow[16];\n"                // per light: x=first tile (-1 none) y=strength z=bias w=texel at 1m
    "  float4 uSunExtra;\n"                  // x=sun Soft Edges (0..1) y=sun shadow fade distance (0 = none) z=cookie atlas rows
    "};\n"
    "Texture2D uTex : register(t0);\n"
    "Texture2D uNormalTex : register(t1);\n"        // bump/normal map (tangent-space)
    "Texture2D uSpecTex : register(t2);\n"          // gloss/specular map
    "Texture2D uAoTex : register(t3);\n"            // ambient-occlusion map
    "Texture2D uMrTex : register(t6);\n"            // glTF metallic-roughness (G rough, B metal)
    "Texture2D uEmisTex : register(t7);\n"          // glTF emissive
    "Texture2D uShadowTex : register(t4);\n"        // sun cascade atlas (depth)
    "Texture2D uLocalTex : register(t5);\n"         // spot/point light tile atlas (depth)
    "Texture2D uCookieTex : register(t8);\n"        // light masks: 64 px wide, one 64 px row per mask (R)
    "SamplerState uSamp : register(s0);\n"
    "SamplerComparisonState uShadowCmp : register(s1);\n"   // filtered depth compare (hardware PCF)
    // ---- 2026-11 lighting upgrade: the same maths as okay::ShadowMath / okay::Lighting (UNVERIFIED on a GPU) ----
    "float softClip(float x){ return x <= 1.0 ? max(x, 0.0) : 1.0 + 0.3 * (1.0 - exp(-(x - 1.0) / 0.3)); }\n"
    "float3 softClip3(float3 v){ return float3(softClip(v.x), softClip(v.y), softClip(v.z)); }\n"
    "float distFade(float d, float dist){\n"
    "  if (dist <= 0.0) return 1.0;\n"
    "  float st = dist * 0.8; if (d <= st) return 1.0; if (d >= dist) return 0.0;\n"
    "  float t = (d - st) / (dist - st); return 1.0 - t * t * (3.0 - 2.0 * t);\n"
    "}\n"
    "float lightAtten(float d, float rng, float fo){\n"
    "  if (fo > 0.5) { float dr = d / rng; float win = max(1.0 - dr*dr*dr*dr, 0.0); win *= win; return win / (1.0 + 8.0 * d * d / (rng * rng)); }\n"
    "  float a = max(1.0 - d / rng, 0.0); return a * a;\n"
    "}\n"
    "float rectTerm(float3 axis, float3 pos, float3 rt, float3 up, float hw, float hh, float rng, float fo, float3 p, float3 N){\n"
    "  float sum = 0.0;\n"
    "  [unroll] for (int j = 0; j < 3; j++) {\n"
    "    [unroll] for (int i = 0; i < 3; i++) {\n"
    "      float fu = ((float)i - 1.0) * 0.6666667; float fv = ((float)j - 1.0) * 0.6666667;\n"
    "      float3 q = pos + rt * (fu * hw) + up * (fv * hh); float3 v = q - p; float d = length(v);\n"
    "      if (d < 1e-4) { sum += 1.0; }\n"
    "      else { float3 l = v / d; float recv = dot(N, l); float emit = dot(axis, -l);\n"
    "        if (recv > 0.0 && emit > 0.0) sum += recv * emit * lightAtten(d, rng, fo); }\n"
    "    }\n"
    "  }\n"
    "  return sum / 9.0;\n"
    "}\n"
    "float cookieSample(float row, float2 uv, bool wrapIt){\n"
    "  if (!wrapIt && (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)) return 0.0;\n"
    "  if (wrapIt) uv = frac(uv);\n"
    "  uv = clamp(uv, 0.0078125, 0.9921875);\n"                         // half a texel inside the 64 px tile
    "  return uCookieTex.SampleLevel(uSamp, float2(uv.x, (row + uv.y) / max(uSunExtra.z, 1.0)), 0).r;\n"
    "}\n"
    "float cookieAt(float4 d0, float4 d1, float4 d2, float4 d4, float4 d5, float3 p){\n"
    "  if (d4.w < -0.5) return 1.0;\n"                                  // no mask
    "  if (d0.w > 0.5 && d0.w < 1.5) return 1.0;\n"                      // point lights have none
    "  if (d0.w > 2.5) return 1.0;\n"                                    // nor do area lights
    "  if (d0.w < 0.5) { float sc = max(d5.w, 0.05);\n"                  // sun: laid across the world, repeating
    "    return cookieSample(d4.w, float2(dot(p, d4.xyz) / sc + 0.5, 0.5 - dot(p, d5.xyz) / sc), true); }\n"
    "  float3 dd = p - d1.xyz; float z = dot(dd, normalize(d0.xyz)); if (z <= 1e-4) return 0.0;\n"   // spot: fits the cone
    "  float co = d2.w; float th = sqrt(max(1.0 - co * co, 0.0)) / max(co, 1e-3);\n"
    "  float k = 0.5 / (z * max(th, 1e-3));\n"
    "  return cookieSample(d4.w, float2(0.5 + dot(dd, d4.xyz) * k, 0.5 - dot(dd, d5.xyz) * k), false);\n"
    "}\n"
    "static float2x2 gRot;\n"                       // per-pixel Poisson rotation
    "float sunTap(float2 uv, float2 o, float z, float ci){\n"
    "  float e = 1.0 / uShadow.z;\n"                                  // stay a texel inside the cascade tile
    "  float2 t = clamp(uv + mul(gRot, o), e, 1.0 - e);\n"
    "  return uShadowTex.SampleCmpLevelZero(uShadowCmp, float2((ci + t.x) / uShadow.y, t.y), z);\n"
    "}\n"
    "float sunCascade(int ci, float3 wp, float3 n){\n"
    "  float tx = uCasInfo[ci].x;\n"
    // World-space bias (same as GL): along the normal, more for a wider filter,
    // plus a little toward the sun.
    "  float ndlS = saturate(dot(n, uLightDir)); float sinT = sqrt(max(1.0 - ndlS * ndlS, 0.0));\n"   // slope: shadowmath::NormalOffset
    "  float3 p = wp + n * (tx * (0.6 + 1.6 * sinT + 0.6 * uShadow.w) * uShadow2.z) + uLightDir * (tx * 0.75 * uShadow2.z);\n"
    "  float4 sc = mul(uCasVP[ci], float4(p, 1.0));\n"
    "  float2 uv = float2(sc.x * 0.5 + 0.5, -sc.y * 0.5 + 0.5);\n"        // D3D texture v is top-down
    "  float z = sc.z * 0.5 + 0.5;\n"                                   // GL clip z [-1,1] -> [0,1]
    "  if (z >= 1.0) return 1.0;\n"
    "  float c = (float)ci; float s = 0.0;\n"
    "  float r0 = uShadow.w / uShadow.z; float r = r0;\n"                  // minimum filter radius (tile uv units)
    "  float dr = uCasInfo[ci].y;\n"                                       // world length of this cascade's depth range
    "  if (dr > 0.0) {\n"                                                  // blocker search -> penumbra (okay::shadowmath::PenumbraTexels)
    "    float sr = r0 + (1.5 + 4.0 * uSunExtra.x) / uShadow.z;\n"
    "    float zb = z - 3.0 * tx / dr; float bl = 0.0; float gs = 0.0; float e1 = 1.0 / uShadow.z;\n"
    "    [unroll] for (int k = 0; k < 6; k++) {\n"
    "      float ang = (float)k * 2.39996323; float rr = sqrt(((float)k + 0.5) / 6.0);\n"
    "      float2 o = mul(gRot, float2(cos(ang), sin(ang)) * rr) * sr;\n"
    "      float2 t = clamp(uv + o, e1, 1.0 - e1);\n"
    "      float2 ap = float2((c + t.x) / uShadow.y, t.y) * float2(uShadow.y * uShadow.z, uShadow.z);\n"
    "      float dd = uShadowTex.Load(int3((int2)ap, 0)).r;\n"
    "      if (zb > dd) { bl += 1.0; gs += (z - dd) * dr; }\n"
    "    }\n"
    "    if (bl < 0.5) return 1.0;\n"                                      // nothing in the way
    "    float pen = r0 * uShadow.z + 0.35 * saturate(uSunExtra.x) * (gs / bl) / max(tx, 1e-5);\n"
    "    r = min(pen, 14.0) / uShadow.z;\n"
    "  }\n"
    "  s += sunTap(uv, float2(-0.326,-0.406)*r, z, c); s += sunTap(uv, float2(-0.840,-0.074)*r, z, c);\n"
    "  s += sunTap(uv, float2(-0.696, 0.457)*r, z, c); s += sunTap(uv, float2(-0.203, 0.621)*r, z, c);\n"
    "  s += sunTap(uv, float2( 0.962,-0.195)*r, z, c); s += sunTap(uv, float2( 0.473,-0.480)*r, z, c);\n"
    "  s += sunTap(uv, float2( 0.519, 0.767)*r, z, c); s += sunTap(uv, float2( 0.185,-0.893)*r, z, c);\n"
    "  s += sunTap(uv, float2( 0.507, 0.064)*r, z, c); s += sunTap(uv, float2( 0.896, 0.412)*r, z, c);\n"
    "  s += sunTap(uv, float2(-0.322,-0.933)*r, z, c); s += sunTap(uv, float2(-0.792,-0.598)*r, z, c);\n"
    "  return s / 12.0;\n"
    "}\n"
    "float shadowFactor(float3 wp, float3 n){\n"     // sun: 0 = shadowed .. 1 = lit
    "  if (uShadow.x < 0.5) return 1.0;\n"
    "  int cnt = (int)(uShadow.y + 0.5);\n"
    "  [loop] for (int ci = 0; ci < 4; ci++) { if (ci >= cnt) break;\n"
    "    float d = length(wp - uCasSphere[ci].xyz); float R = uCasSphere[ci].w;\n"
    "    if (d >= R) continue;\n"                                         // tightest cascade holding the point
    "    float s = sunCascade(ci, wp, n);\n"
    "    float edge = R * 0.82;\n"                                        // blend into the next cascade (no seam)
    "    if (d > edge) { float f = (d - edge) / (R - edge);\n"
    "      s = lerp(s, (ci + 1 < cnt) ? sunCascade(ci + 1, wp, n) : 1.0, f); }\n"
    "    return 1.0 - uShadow2.y * distFade(length(wp - uEye), uSunExtra.y) * (1.0 - s);\n"
    "  }\n"
    "  return 1.0;\n"
    "}\n"
    "float localTap(float2 org, float2 uv, float2 o, float z){\n"
    "  float e = 1.0 / uShadow2.w;\n"
    "  float2 t = clamp(uv + mul(gRot, o), e, 1.0 - e);\n"
    "  return uLocalTex.SampleCmpLevelZero(uShadowCmp, org + t * 0.25, z);\n"
    "}\n"
    "float localShadow(int li, float3 lpos, float ltype, float3 wp, float3 n, float3 l, float dist){\n"
    "  float4 info = uLShadow[li];\n"
    "  float tw = dist * info.w;\n"                                      // world size of a shadow texel here
    "  float3 p = wp + n * (tw * (1.5 + 0.6 * uShadow.w) * info.z) + l * (tw * info.z);\n"
    "  float tile = info.x;\n"
    "  if (ltype < 1.5 || ltype > 2.5) {\n"                              // point / area: pick the cube face
    "    float3 v = p - lpos; float3 a = abs(v);\n"
    "    if (a.x >= a.y && a.x >= a.z) tile += (v.x > 0.0) ? 0.0 : 1.0;\n"
    "    else if (a.y >= a.z) tile += (v.y > 0.0) ? 2.0 : 3.0;\n"
    "    else tile += (v.z > 0.0) ? 4.0 : 5.0;\n"
    "  }\n"
    "  float4 sc = mul(uLocalVP[(int)(tile + 0.5)], float4(p, 1.0));\n"
    "  if (sc.w <= 1e-5) return 1.0;\n"
    "  float3 q = sc.xyz / sc.w;\n"
    "  float2 uv = float2(q.x * 0.5 + 0.5, -q.y * 0.5 + 0.5); float z = q.z * 0.5 + 0.5;\n"
    "  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || z >= 1.0) return 1.0;\n"
    "  float2 org = float2(fmod(tile, 4.0), floor(tile / 4.0 + 0.01)) * 0.25;\n"
    "  float r = uShadow.w / uShadow2.w; float s = 0.0;\n"
    "  s += localTap(org, uv, float2(-0.326,-0.406)*r, z); s += localTap(org, uv, float2(-0.840,-0.074)*r, z);\n"
    "  s += localTap(org, uv, float2(-0.696, 0.457)*r, z); s += localTap(org, uv, float2(-0.203, 0.621)*r, z);\n"
    "  s += localTap(org, uv, float2( 0.962,-0.195)*r, z); s += localTap(org, uv, float2( 0.473,-0.480)*r, z);\n"
    "  s += localTap(org, uv, float2( 0.519, 0.767)*r, z); s += localTap(org, uv, float2( 0.185,-0.893)*r, z);\n"
    "  s += localTap(org, uv, float2( 0.507, 0.064)*r, z); s += localTap(org, uv, float2( 0.896, 0.412)*r, z);\n"
    "  s += localTap(org, uv, float2(-0.322,-0.933)*r, z); s += localTap(org, uv, float2(-0.792,-0.598)*r, z);\n"
    "  return 1.0 - info.y * (1.0 - s / 12.0);\n"
    "}\n"
    // ACES filmic curve (matches the GL renderer) for the linear/filmic pipeline.
    "float3 aces(float3 x){\n"
    "  return saturate((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14));\n"
    "}\n"
    "float4 VSDepth(float3 pos:POSITION):SV_POSITION {\n"          // depth-only shadow pass
    "  float4 cp = mul(uMVP, float4(pos,1.0)); cp.z = (cp.z + cp.w) * 0.5; return cp;\n"
    "}\n"
    "struct VSIn { float3 pos:POSITION; float3 nrm:NORMAL; float2 uv:TEXCOORD; float3 col:COLOR; float3 tan:TANGENT; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 nrm:NORMAL; float3 world:TEXCOORD1; float2 uv:TEXCOORD0; float3 col:COLOR; float3 tan:TEXCOORD2; };\n"
    "PSIn VSMain(VSIn i){\n"
    "  PSIn o;\n"
    "  float4 cp = mul(uMVP, float4(i.pos,1.0));\n"
    "  cp.z = (cp.z + cp.w) * 0.5;\n"        // GL [-1,1] -> D3D [0,1]
    "  o.pos = cp;\n"
    "  o.nrm = mul((float3x3)uModel, i.nrm);\n"
    "  o.tan = mul((float3x3)uModel, i.tan);\n"
    "  o.world = mul(uModel, float4(i.pos,1.0)).xyz;\n"
    "  o.uv = i.uv * uTiling + uPbr2.zw;\n"
    "  o.col = i.col;\n"
    "  return o;\n"
    "}\n"
    // ---- GPU skinning: up to 256 joints as 3x4 affine rows (b1) ----
    "cbuffer Bones : register(b1) { float4 uBones[768]; };\n"
    "float3x4 skinMat(float4 j, float4 w){\n"
    "  float sw = w.x + w.y + w.z + w.w;\n"
    "  if (sw < 1e-6) return float3x4(1,0,0,0, 0,1,0,0, 0,0,1,0);\n"   // unweighted: bind pose
    "  int4 b = (int4)(j + 0.5) * 3;\n"
    "  float3x4 m = w.x * float3x4(uBones[b.x], uBones[b.x+1], uBones[b.x+2]);\n"
    "  m += w.y * float3x4(uBones[b.y], uBones[b.y+1], uBones[b.y+2]);\n"
    "  m += w.z * float3x4(uBones[b.z], uBones[b.z+1], uBones[b.z+2]);\n"
    "  m += w.w * float3x4(uBones[b.w], uBones[b.w+1], uBones[b.w+2]);\n"
    "  return m / sw;\n"
    "}\n"
    "struct VSInSkin { float3 pos:POSITION; float3 nrm:NORMAL; float2 uv:TEXCOORD; float3 col:COLOR; float3 tan:TANGENT;\n"
    "                  float4 bj:BLENDINDICES; float4 bw:BLENDWEIGHT; };\n"
    "PSIn VSSkin(VSInSkin i){\n"
    "  float3x4 m = skinMat(i.bj, i.bw);\n"
    "  VSIn v; v.pos = mul(m, float4(i.pos,1.0)); v.nrm = normalize(mul((float3x3)m, i.nrm));\n"
    "  v.tan = mul((float3x3)m, i.tan); v.uv = i.uv; v.col = i.col;\n"
    "  return VSMain(v);\n"
    "}\n"
    "float4 VSDepthSkin(float3 pos:POSITION, float4 bj:BLENDINDICES, float4 bw:BLENDWEIGHT):SV_POSITION {\n"
    "  return VSDepth(mul(skinMat(bj, bw), float4(pos,1.0)));\n"
    "}\n"
    "float4 PSMain(PSIn i):SV_TARGET {\n"
    "  if (uShadowAlpha >= 0.0) return float4(0,0,0,uShadowAlpha);\n"   // flat ground contact shadow
    "  float3 N = normalize(i.nrm);\n"
    "  if (uHasNormal > 0.5) {\n"                                       // perturb N by the tangent-space normal map
    "    float3 tn = uNormalTex.Sample(uSamp, i.uv).rgb * 2.0 - 1.0;\n"
    "    tn.xy *= uNormalStrength;\n"
    "    float3 Tn = i.tan - N * dot(N, i.tan);\n"                      // Gram-Schmidt orthonormalize
    "    if (length(Tn) > 1e-5) {\n"
    "      Tn = normalize(Tn); float3 Bn = cross(N, Tn);\n"
    "      float3 pn = Tn * tn.x + Bn * tn.y + N * tn.z;\n"
    "      if (length(pn) > 1e-5) N = normalize(pn);\n"
    "    }\n"
    "  }\n"
    "  float3 base = uColor * i.col;\n"      // per-vertex/face color (white when unused)
    "  if (uUseTex > 0.5) base *= uTex.Sample(uSamp, i.uv).rgb;\n"
    "  float3 Vv = normalize(uEye - i.world);\n"
    "  float fz = 1.0 - max(dot(N, Vv), 0.0);\n"               // Fresnel term (grazing)
    "  if (uShaderMode > 2.5 && uShaderMode < 3.5)\n"           // Gradient
    "    base = lerp(uGradBot, uGradTop, saturate(N.y * 0.5 + 0.5));\n"
    "  bool fres = uShaderMode > 3.5 && uShaderMode < 4.5;\n"   // Fresnel
    "  if (fres) base *= 0.10;\n"
    "  if (uShaderMode > 4.5 && uShaderMode < 5.5) {\n"         // Iridescent (oil-slick)
    "    float ph = fz * 6.2831853;\n"
    "    base *= float3(0.5+0.5*cos(ph), 0.5+0.5*cos(ph+2.0944), 0.5+0.5*cos(ph+4.1888));\n"
    "  }\n"
    "  bool holo = uShaderMode > 5.5 && uShaderMode < 6.5;\n"   // Hologram
    "  if (holo) base *= 0.18 * (0.55 + 0.45 * sin(i.world.y * 40.0));\n"
    "  if (uShaderMode > 6.5) base = floor(base * 5.0) / 5.0;\n"  // Posterize (retro banding)
    // Filmic mode (uFogCol.w): gamma-correct pipeline — decode sRGB albedo/emissive
    // to linear, light in linear, ACES + sRGB-encode at the end (matches GL).
    "  bool lin = uFogCol.w > 0.5;\n"
    "  if (lin) base = pow(max(base, 0.0), 2.2);\n"
    "  float3 emis = uEmissive;\n"
    "  if (uLightCount.z > 0.5) emis *= uEmisTex.Sample(uSamp, i.uv).rgb;\n"
    "  if (lin) emis = pow(max(emis, 0.0), 2.2);\n"
    "  float shiny = uShininess; float metalK = uPbr.x; float specK = uSpecular;\n"
    "  if (uLightCount.y > 0.5) {\n"
    "    float4 mr = uMrTex.Sample(uSamp, i.uv);\n"
    "    metalK *= mr.b;\n"
    "    float rough0 = saturate(1.0 - (uShininess - 4.0) / 124.0);\n"
    "    float rough = saturate(rough0 * mr.g);\n"
    "    shiny = 4.0 + (1.0 - rough) * 124.0;\n"
    "    specK *= clamp(1.0 - rough, 0.04, 1.0);\n"
    "  }\n"
    "  if (uUnlit > 0.5) { float3 uc = base + emis;\n"            // decode+encode cancel: unlit unchanged
    "    if (lin) uc = pow(max(uc, 0.0), 1.0/2.2);\n"
    "    return float4(uc, uAlpha); }\n"
    "  bool toon = uShaderMode > 1.5 && uShaderMode < 2.5 && uToonBands > 0.5;\n"
    "  float3 amb = lerp(uAmbGround.xyz, uAmbSky.xyz, saturate(N.y * 0.5 + 0.5));\n"  // scene Ambient Mode
    "  float3 lit = amb; float3 spec = float3(0,0,0);\n"
    "  { float a = 52.9829189 * frac(dot(i.pos.xy, float2(0.06711056, 0.00583715))) * 6.2831853;\n"
    "    float ca = cos(a), sa = sin(a); gRot = float2x2(ca, -sa, sa, ca); }\n"   // interleaved-gradient noise rotation
    "  float3 Ns = normalize(i.nrm);\n"                         // geometric normal for shadow bias
    "  bool recv = uFog.w > 0.5;\n"                             // MeshRenderer::receiveShadows
    "  float sh = recv ? shadowFactor(i.world, Ns) : 1.0;\n"    // sun occlusion (real cast shadows)
    "  int lc = (int)uLightCount.x;\n"
    "  if (lc < 1) {\n"                                          // fallback: global directional
    "    float ndl = max(dot(N, uLightDir), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "    lit += uLightColor * ndl * sh;\n"
    "    if (uSpecular > 0.0) { float3 H = normalize(uLightDir + Vv);\n"
    "      spec = pow(max(dot(N,H),0.0), max(shiny,1.0)) * specK * sh; }\n"
    "  } else {\n"
    "    [loop] for (int li = 0; li < 16; li++) { if (li >= lc) break;\n"   // directional + point + spot + area
    "      float4 d0 = uLights[li*6+0]; float4 d1 = uLights[li*6+1];\n"
    "      float4 d2 = uLights[li*6+2]; float4 d3 = uLights[li*6+3];\n"
    "      float4 d4 = uLights[li*6+4]; float4 d5 = uLights[li*6+5];\n"
    "      float3 ld; float at = 1.0; float wrap = 0.0; float dist = 0.0;\n"
    "      if (d0.w < 0.5) { ld = normalize(-d0.xyz); }\n"             // directional
    "      else { float3 toL = d1.xyz - i.world; dist = length(toL); ld = toL / max(dist, 1e-4);\n"
    "        float rng = max(d1.w, 1e-4);\n"
    "        if (d3.y > 0.5) { float dr = dist / rng; float win = max(1.0 - dr*dr*dr*dr, 0.0); win *= win;\n"   // physical inverse-square, windowed
    "          at = win / (1.0 + 8.0 * dist * dist / (rng * rng)); }\n"
    "        else { at = max(1.0 - dist / rng, 0.0); at *= at; }\n"           // linear rolloff
    "        if (d0.w > 2.5) { wrap = 0.5; }\n"                               // area: soft wrap fill
    "        else if (d0.w > 1.5) { float cs = dot(normalize(d0.xyz), -ld);\n"   // spot cone
    "          float dn = d3.x - d2.w; float sp = dn > 1e-4 ? saturate((cs - d2.w) / dn) : (cs >= d2.w ? 1.0 : 0.0);\n"
    "          at *= sp * sp * (3.0 - 2.0 * sp); } }\n"   // smoothstep cone (okay::SpotCone)
    "      bool rect = d0.w > 2.5 && d3.z > 0.0 && d3.w > 0.0;\n"             // Area light with a Panel size: a real rectangle
    "      float ndl = max((dot(N, ld) + wrap) / (1.0 + wrap), 0.0);\n"
    "      if (rect) { ndl = rectTerm(normalize(d0.xyz), d1.xyz, d4.xyz, d5.xyz, d3.z, d3.w, max(d1.w, 1e-4), d3.y, i.world, N); at = 1.0; }\n"
    "      if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "      if (d4.w > -0.5) at *= cookieAt(d0, d1, d2, d4, d5, i.world);\n"   // light mask
    "      if (ndl * at <= 0.0) continue;\n"                     // no light reaches here: skip the shadow lookup
    "      float lsh = 1.0;\n"
    "      if (d0.w < 0.5) { if (abs((float)li - uShadow2.x) < 0.5) lsh = sh; }\n"   // only the main sun has cascades
    "      else if (recv && uLShadow[li].x > -0.5) lsh = localShadow(li, d1.xyz, d0.w, i.world, Ns, ld, dist);\n"
    "      lit += d2.xyz * ndl * at * lsh;\n"
    "      if (uSpecular > 0.0) { float3 H = normalize(ld + Vv);\n"   // highlights take the light's colour + intensity
    "        spec += d2.xyz * (pow(max(dot(N,H),0.0), max(shiny,1.0)) * specK * at * lsh); }\n"
    "    }\n"
    "  }\n"
    "  if (uShaderMode > 7.5) lit += fz * fz * lit * 0.6;\n"      // Velvet: fuzzy grazing sheen
    "  float gloss = 1.0;\n"                                      // gloss/specular map
    "  if (uPbr.z > 0.5) gloss = dot(uSpecTex.Sample(uSamp, i.uv).rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "  if (uPbr.w > 0.5) {\n"                                     // ambient-occlusion map
    "    float ao = dot(uAoTex.Sample(uSamp, i.uv).rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "    lit *= (1.0 - uPbr2.x * (1.0 - ao));\n"
    "  }\n"
    "  float metal = saturate(metalK);\n"                       // metalness
    "  float diffK = 1.0 - 0.9 * metal;\n"
    "  float3 f0 = lerp(float3(1,1,1), base, metal);\n"
    "  if (!lin && !toon) lit = softClip3(lit);\n"                // roll-off above 1.0 (the filmic path has ACES instead)
    "  float3 diff = base * lit * diffK;\n"
    "  float3 rim = float3(0,0,0);\n"
    "  float rs = uRimStr; if ((fres || holo) && rs < 0.8) rs = 1.6;\n"
    "  if (rs > 0.0) rim = uRimColor * (fz*fz*fz * rs);\n"
    "  float3 col = diff + (spec * gloss) * f0 + rim + emis;\n"
    "  float reflAmt = max(uPbr.y, metal);\n"                     // env reflection of the sky gradient
    "  float reflK = reflAmt * gloss;\n"
    "  if (reflK > 0.0 && uPbr2.y > 0.5) {\n"
    "    float ndv = max(dot(N, Vv), 0.0);\n"
    "    float3 R = reflect(-Vv, N); float ry = clamp(R.y, -1.0, 1.0);\n"
    "    float3 env = lerp(uSky[1].xyz, ry >= 0.0 ? uSky[0].xyz : uSky[2].xyz, abs(ry));\n"
    "    if (lin) env = pow(max(env, 0.0), 2.2);\n"
    "    float fr2 = 1.0 - ndv; fr2 = fr2*fr2*fr2*fr2*fr2;\n"     // Schlick
    "    float kk = reflK + (1.0 - reflK) * fr2;\n"
    "    col = col * (1.0 - kk) + env * f0 * kk;\n"
    "  }\n"
    "  if (uFog.x > 0.5) {\n"                                    // fog (Atmosphere.hpp FogFactor, same maths)
    "    float fd = length(uEye - i.world);\n"
    "    float ff;\n"
    "    if (uFog2.x < 0.5) ff = saturate((fd - uFog.y) / max(uFog.z - uFog.y, 1e-3));\n"   // linear
    "    else if (uFog2.x < 1.5) ff = 1.0 - exp(-uFog2.y * fd);\n"                            // exponential
    "    else { float te = exp(-max(uEye.y - uFog2.z, 0.0) * uFog2.w);\n"                      // height: thins above fogHeight
    "           float tw = exp(-max(i.world.y - uFog2.z, 0.0) * uFog2.w);\n"
    "           ff = 1.0 - exp(-uFog2.y * fd * 0.5 * (te + tw)); }\n"
    "    float3 fc = uFogCol.xyz;\n"
    "    if (lin) fc = pow(max(fc, 0.0), 2.2);\n"                 // fog is authored sRGB too
    "    col = lerp(col, fc, ff);\n"
    "  }\n"
    // Filmic output: ACES roll-off in linear light, then encode back to sRGB. With
    // the post chain running the colour stays linear: the chain exposes, tonemaps
    // and encodes it.
    "  if (lin && uPost.x < 0.5) { col = aces(col); col = pow(max(col, 0.0), 1.0/2.2); }\n"
    "  return float4(col, uAlpha);\n"
    "}\n"
    // ---- Procedural sky + the post chain (mirrors the GLSL in GLRenderer.cpp) ----
    "cbuffer PC : register(b3) {\n"
    "  float4x4 uInvVP;\n"
    "  float4 uEyeSz;\n"                      // xyz eye, w sun disc radius (radians)
    "  float4 uSunDirHalo;\n"                 // xyz toward the sun, w halo
    "  float4 uZenithDisc;\n"                 // xyz zenith colour, w disc visibility
    "  float4 uHorizonHaze;\n"                // xyz horizon colour, w haze
    "  float4 uGroundNight;\n"                // xyz ground colour, w night (0..1)
    "  float4 uSunColorStar;\n"               // xyz sun colour, w star density
    "  float4 uCloud;\n"                      // cover, density, scale, star brightness
    "  float4 uCloudOffTL;\n"                 // xy cloud offset, z time, w linear output
    "  float4 uP0;\n"                         // exposure, linearIn, tonemap, bloomOn
    "  float4 uP1;\n"                         // bloomIntensity, gradeOn, temperature, tint
    "  float4 uP2;\n"                         // saturation, contrast, vignette, vignetteSmooth
    "  float4 uLiftAspect;\n"                 // xyz lift, w aspect
    "  float4 uGammaThresh;\n"                // xyz gamma, w bloom threshold
    "  float4 uGainRadius;\n"                 // xyz gain, w blur / downsample radius
    "  float4 uTexelDir;\n"                   // xy source texel, zw blur direction (uv)
    "  float4 uP7;\n"                         // SSAO: x radius, y far, z strength, w AO power; DoF: x focus, y range, z far; bloom bright-pass: w softness
    "  float4 uTopMode;\n"                    // Colour sky: xyz top, w mode (0 colour, 1 procedural)
    "  float4 uHorHp;\n"                      // xyz horizon, w horizon position
    "  float4 uBotStars;\n"                   // xyz bottom, w stars on
    "  float4 uSunPosRad;\n"                  // xy sun position (uv), z radius (of height), w sun on
    "  float4 uViewSize;\n"                   // xy pixels
    "};\n"
    "SamplerState uSampClamp : register(s2);\n"
    "struct PSQuad { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "PSQuad VSQuad(uint id : SV_VertexID) {\n"                   // one fullscreen triangle, no vertex buffer
    "  PSQuad o; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0); o.uv = uv; return o;\n"
    "}\n"
    "float hash21(float2 p){ p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }\n"
    "float vnoise(float2 p){ float2 i = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  float a = hash21(i), b = hash21(i + float2(1.0, 0.0)), c = hash21(i + float2(0.0, 1.0)), d = hash21(i + float2(1.0, 1.0));\n"
    "  return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y); }\n"
    "float fbm(float2 p){ float s = 0.0, a = 0.5; [unroll] for (int k = 0; k < 5; k++) { s += a * vnoise(p); p = p * 2.03 + float2(17.1, 9.7); a *= 0.5; } return s; }\n"
    "float4 PSSky(PSQuad i) : SV_TARGET {\n"
    "  if (uTopMode.w < 0.5) {\n"                                     // Colour mode: the host's screen-space gradient, in-frame
    "    float t = i.uv.y; float hp = clamp(uHorHp.w, 0.05, 0.95);\n"
    "    float3 col = t < hp ? lerp(uTopMode.xyz, uHorHp.xyz, t / hp) : lerp(uHorHp.xyz, uBotStars.xyz, (t - hp) / (1.0 - hp));\n"
    "    float hz = 1.0 - abs(t - hp) / 0.10;\n"
    "    if (hz > 0.0) col = lerp(col, min(uHorHp.xyz * float3(1.15, 1.15, 1.10) + float3(0.12, 0.12, 0.10), 1.0), hz * 0.5);\n"
    "    if (uBotStars.w > 0.5 && t < hp) {\n"
    "      float2 g = i.uv * uViewSize.xy / 22.0; float2 cell = floor(g), cf = frac(g);\n"
    "      float h = hash21(cell); float2 sp = float2(hash21(cell + 3.1), hash21(cell + 9.7)) * 0.8 + 0.1;\n"
    "      col += (h > 1.0 - 0.5 * uSunColorStar.w ? 1.0 : 0.0) * smoothstep(0.12, 0.0, length(cf - sp)) * uCloud.w;\n"
    "    }\n"
    "    if (uSunPosRad.w > 0.5) {\n"
    "      float dp = length((i.uv - uSunPosRad.xy) * uViewSize.xy);\n"
    "      float r = max(uSunPosRad.z, 0.005) * uViewSize.y; float glowR = 0.45 * uViewSize.y;\n"
    "      [unroll] for (int k = 12; k >= 1; --k) { float fr = (float)k / 12.0; if (dp < glowR * fr) col = lerp(col, uSunColorStar.xyz, (34.0 / 255.0) * (1.0 - fr) * (1.0 - fr)); }\n"
    "      [unroll] for (int gi = 4; gi >= 1; --gi) { if (dp < r * (1.0 + (float)gi * 0.7)) col = lerp(col, uSunColorStar.xyz, (40.0 - (float)gi * 6.0) / 255.0); }\n"
    "      if (dp < r) col = uSunColorStar.xyz;\n"
    "    }\n"
    "    if (uCloudOffTL.w > 0.5) col = pow(max(col, 0.0), 2.2);\n"
    "    return float4(col, 1.0);\n"
    "  }\n"
    "  float2 ndc = float2(i.uv.x * 2.0 - 1.0, 1.0 - i.uv.y * 2.0);\n"
    "  float4 p0 = mul(uInvVP, float4(ndc, -1.0, 1.0)); float4 p1 = mul(uInvVP, float4(ndc, 1.0, 1.0));\n"
    "  float3 dir = normalize(p1.xyz / p1.w - p0.xyz / p0.w);\n"
    "  float y = dir.y;\n"
    "  float3 zen = uZenithDisc.xyz, hor = uHorizonHaze.xyz, grd = uGroundNight.xyz, sunC = uSunColorStar.xyz, sunD = uSunDirHalo.xyz;\n"
    "  float haze = uHorizonHaze.w, night = uGroundNight.w, disc = uZenithDisc.w;\n"
    "  float3 col = (y >= 0.0) ? lerp(hor, zen, pow(max(y, 0.0), 0.55)) : lerp(hor, grd, smoothstep(0.0, 0.25, -y));\n"
    "  float3 hazeCol = hor * 1.12 + 0.06;\n"
    "  col = lerp(col, hazeCol, haze * 0.6 * exp(-abs(y) * 5.0));\n"
    "  float3 flatD = normalize(float3(dir.x, 0.0, dir.z) + 1e-5); float3 flatS = normalize(float3(sunD.x, 0.0, sunD.z) + 1e-5);\n"
    "  col += sunC * (0.14 * haze * disc * exp(-abs(y) * 4.0) * pow(max(dot(flatD, flatS), 0.0), 3.0));\n"
    "  float ang = acos(clamp(dot(dir, sunD), -1.0, 1.0));\n"
    "  float dsc = (1.0 - smoothstep(uEyeSz.w * 0.85, uEyeSz.w * 1.15, ang)) * disc;\n"
    "  float halo = uSunDirHalo.w * disc * (0.6 * exp(-ang * 4.0) + 0.3 * exp(-ang * 1.2));\n"
    "  col += sunC * (dsc * 1.2 + halo);\n"
    "  if (night > 0.0 && y > 0.0) {\n"
    "    float az = atan2(dir.z, dir.x); float el = asin(clamp(y, -1.0, 1.0));\n"
    "    float2 sc = float2(az, el) * (64.0 * (0.5 + uSunColorStar.w));\n"
    "    float2 ci = floor(sc), cf = frac(sc);\n"
    "    float h = hash21(ci);\n"
    "    float2 sp = float2(hash21(ci + 7.3), hash21(ci + 13.7)) * 0.8 + 0.1;\n"
    "    float d = length(cf - sp);\n"
    "    float star = (h > 1.0 - 0.35 * uSunColorStar.w ? 1.0 : 0.0) * smoothstep(0.09, 0.0, d);\n"
    "    float tw = 0.7 + 0.3 * sin(uCloudOffTL.z * 3.0 + h * 40.0);\n"
    "    col += star * uCloud.w * night * tw * smoothstep(0.0, 0.15, y);\n"
    "  }\n"
    "  if (uCloud.x > 0.0 && y > 0.01) {\n"
    "    float2 uv = dir.xz / (y + 0.12) * (0.6 / max(uCloud.z, 0.05)) + uCloudOffTL.xy;\n"
    "    float n = fbm(uv);\n"
    "    float cov = smoothstep(1.0 - uCloud.x, 1.0 - uCloud.x * 0.6 + 0.05, n);\n"
    "    float lit = saturate(0.5 + (fbm(uv + flatS.xz * 0.06) - n) * 7.0);\n"
    "    float3 bright = lerp(float3(1, 1, 1), sunC, 0.35) * (0.95 - 0.8 * night);\n"
    "    float3 shade = lerp(hor, zen, 0.5) * (0.8 - 0.3 * night);\n"
    "    float3 cc = lerp(shade, bright, lit);\n"
    "    float a = cov * uCloud.y * smoothstep(0.0, 0.2, y);\n"
    "    col = lerp(col, cc, a);\n"
    "  }\n"
    "  if (uCloudOffTL.w > 0.5) col = pow(max(col, 0.0), 2.2);\n"
    "  return float4(col, 1.0);\n"
    "}\n"
    // Post-chain pixel shaders (bright-pass, blur, uber, FXAA): shared text, D3DPostHLSL.hpp.
    OKAY_POST_HLSL
    // Ambient occlusion (SsaoMath.hpp): depth, estimate and multiply pixel shaders.
    OKAY_SSAO_HLSL
    // Depth of field (DofMath.hpp): mixes the sharp and blurred frame by distance from focus.
    OKAY_DOF_HLSL;
  return kText;
}

} // namespace okay
