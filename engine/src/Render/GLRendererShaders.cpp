#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/MeshHash.hpp"
#include "okay/Render/Visibility.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture: shares the texture cache
#include "okay/Render/ShadowSetup.hpp"
#include "okay/Render/Atmosphere.hpp"         // sky palette, fog modes, post settings
#include "okay/Render/PostQualityMath.hpp"   // shared bloom soft knee + AO power (UNVERIFIED on a GPU)
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SkinnedMesh.hpp"    // skinned meshes: VB cache keyed on SkinnedMesh::version
#include "okay/Components/Character.hpp"
#include <utility>
#include "okay/Core/Log.hpp"
#include "okay/Debug/MemoryTracker.hpp"   // "Textures (GPU)": bytes of uploaded textures
#include "okay/Core/Time.hpp"
#include "okay/Render/OkayShader.hpp"     // Custom (.okayshader) surfaces

#include "GLRendererInternal.hpp"
#include "okay/Render/RHI/BloomPass.hpp"   // the full-screen vertex shader and the gaussian blur live there once

namespace okay {
namespace gl_render {

GL g;   // resolved entry points

// GPU skinning, shared by every vertex shader that draws meshes: uBones holds
// each joint's skin matrix (meshNode^-1 * jointWorld * inverseBind) as three
// rows; a vertex blends its four joints' rows by weight and applies them once.
// SKIN_BONES is defined when the program is built (from the GPU's uniform limit).
const char* kSkinGlsl =
    "uniform float uSkin; uniform vec4 uBones[SKIN_BONES * 3];\n"
    "attribute vec4 aJoints; attribute vec4 aWeights;\n"
    "void skinRows(out vec4 r0, out vec4 r1, out vec4 r2){\n"
    "  int j0 = int(aJoints.x + 0.5) * 3, j1 = int(aJoints.y + 0.5) * 3;\n"
    "  int j2 = int(aJoints.z + 0.5) * 3, j3 = int(aJoints.w + 0.5) * 3;\n"
    "  r0 = uBones[j0] * aWeights.x + uBones[j1] * aWeights.y + uBones[j2] * aWeights.z + uBones[j3] * aWeights.w;\n"
    "  r1 = uBones[j0+1] * aWeights.x + uBones[j1+1] * aWeights.y + uBones[j2+1] * aWeights.z + uBones[j3+1] * aWeights.w;\n"
    "  r2 = uBones[j0+2] * aWeights.x + uBones[j1+2] * aWeights.y + uBones[j2+2] * aWeights.z + uBones[j3+2] * aWeights.w;\n"
    "}\n"
    "vec3 skinPos(vec3 p){\n"
    "  if (uSkin < 0.5) return p;\n"
    "  vec4 r0, r1, r2; skinRows(r0, r1, r2); vec4 h = vec4(p, 1.0);\n"
    "  return vec3(dot(r0, h), dot(r1, h), dot(r2, h));\n"
    "}\n";

const char* kVert =
    "#version 120\n"
    "SKIN\n"
    "uniform mat4 uMVP; uniform mat4 uModel; uniform vec2 uTiling; uniform vec2 uTexOffset;\n"
    "attribute vec3 aPos; attribute vec3 aNormal; attribute vec2 aUV; attribute vec3 aColor;\n"
    "attribute vec3 aTan;\n"
    "varying vec3 vN; varying vec3 vWorld; varying vec2 vUV; varying vec3 vCol; varying vec3 vTan;\n"
    "void main(){\n"
    "  vec3 P = aPos, Nn = aNormal, Tt = aTan;\n"
    "  if (uSkin > 0.5) {\n"
    "    vec4 r0, r1, r2; skinRows(r0, r1, r2); vec4 h = vec4(aPos, 1.0);\n"
    "    P = vec3(dot(r0, h), dot(r1, h), dot(r2, h));\n"
    "    Nn = vec3(dot(r0.xyz, aNormal), dot(r1.xyz, aNormal), dot(r2.xyz, aNormal));\n"
    "    Tt = vec3(dot(r0.xyz, aTan), dot(r1.xyz, aTan), dot(r2.xyz, aTan));\n"
    "  }\n"
    "  gl_Position = uMVP * vec4(P,1.0);\n"
    "  vN = mat3(uModel) * Nn;\n"
    "  vTan = mat3(uModel) * Tt;\n"
    "  vWorld = (uModel * vec4(P,1.0)).xyz;\n"
    "  vUV = aUV * uTiling + uTexOffset;\n"
    "  vCol = aColor;\n"
    "}\n";

const char* kFrag =
    "#version 120\n"
    "uniform vec3 uColor; uniform vec3 uLightDir; uniform vec3 uLightColor;\n"
    "uniform vec3 uAmbient; uniform vec3 uEmissive; uniform vec3 uEye;\n"
    "uniform float uSpecular; uniform float uShininess; uniform float uUnlit;\n"
    "uniform sampler2D uTex; uniform float uUseTex;\n"
    "uniform float uShaderMode; uniform float uToonBands; uniform float uRimStr;\n"
    "uniform vec3 uRimColor; uniform vec3 uGradTop; uniform vec3 uGradBot;\n"
    "uniform float uShadowAlpha;\n"   // >=0: draw a flat translucent black (ground contact shadow)
    "uniform float uAlpha;\n"         // material opacity (1 = opaque); blended for water/glass
    "uniform int uLightCount;\n"      // multi-light: directional + point + spot from the scene
    "uniform float uLType[16]; uniform vec3 uLPos[16]; uniform vec3 uLDir[16];\n"
    "uniform vec3 uLCol[16]; uniform float uLRange[16]; uniform float uLCosOut[16]; uniform float uLCosIn[16];\n"
    "uniform float uLFalloff[16];\n"   // 0 = linear (1-d/r)^2, 1 = physical inverse-square
    "uniform vec4 uLRect[16]; uniform vec3 uLRight[16]; uniform vec3 uLUp[16];\n"   // x=panel half width y=half height z=mask row (-1 none) w=mask scale; panel axes
    "uniform sampler2D uCookieTex; uniform float uCookieRows; uniform float uSunSoft; uniform float uFadeDist;\n"   // light masks (unit 8), sun Soft Edges, shadow fade distance
    "uniform float uFogOn; uniform vec3 uFogColor; uniform float uFogStart; uniform float uFogEnd;\n"  // distance fog
    "uniform float uFogMode; uniform float uFogDensity; uniform float uFogHeight; uniform float uFogFalloff;\n"  // linear / exponential / height (Atmosphere.hpp)
    "uniform float uTonemap;\n"                                                                        // filmic ACES tonemap toggle
    "uniform float uHdrOut;\n"                                                                         // 1 = leave the colour linear for the post chain
    "vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }\n"        // source: K. Narkowicz, "ACES Filmic Tone Mapping Curve" (blog, 2016), a fit of the ACES look
    "uniform sampler2D uNormalTex; uniform float uHasNormal; uniform float uNormalStrength;\n"  // bump/normal map
    "uniform sampler2D uSpecTex; uniform float uHasSpecMap;\n"                  // gloss map (per-texel specular)
    "uniform sampler2D uAoTex; uniform float uHasAo; uniform float uAoStrength;\n"  // ambient-occlusion map
    "uniform sampler2D uMrTex; uniform float uHasMR;\n"                        // glTF packed metallic-roughness (G rough, B metal)
    "uniform sampler2D uEmisTex; uniform float uHasEmis;\n"                    // glTF emissive texture
    "uniform float uMetallic; uniform float uReflectivity;\n"                  // PBR metalness + env reflection
    "uniform vec3 uSkyTop; uniform vec3 uSkyHor; uniform vec3 uSkyBot; uniform float uEnvOn;\n"  // sky for reflections
    // ---- Shadows ----
    // Sun: up to 4 ortho cascades side by side in one depth atlas (count*S x S).
    // Local: a 4x4 atlas of tiles for spot (1 tile) and point/area (6 cube-face
    // tiles) lights. Both are sampler2DShadow, so every tap is a hardware 2x2
    // bilinear depth compare; 12 rotated Poisson taps on top give soft edges.
    "uniform sampler2DShadow uShadowTex; uniform float uShadowOn;\n"
    "uniform mat4 uCasVP[4]; uniform vec4 uCasSphere[4]; uniform float uCasTexel[4];\n"
    "uniform float uCasCount; uniform float uCasSize; uniform float uShadowSoft;\n"
    "uniform float uSunIndex; uniform float uSunStrength; uniform float uSunBias;\n"
    "uniform sampler2DShadow uLocalTex; uniform mat4 uLocalVP[16]; uniform vec4 uLShadow[16];\n"   // x=first tile (-1 none), y=strength, z=bias, w=texel at 1m
    "uniform float uLocalSize; uniform float uReceive;\n"
    "uniform vec3 uAmbSky; uniform vec3 uAmbGround;\n"                                          // ambient mode (up / down)
    "varying vec3 vN; varying vec3 vWorld; varying vec2 vUV; varying vec3 vCol; varying vec3 vTan;\n"
    // ---- 2026-11 lighting upgrade: same maths as okay::Lighting / okay::ShadowMath (UNVERIFIED on a GPU) ----
    "float softClip(float x){ return x <= 1.0 ? max(x, 0.0) : 1.0 + 0.3 * (1.0 - exp(-(x - 1.0) / 0.3)); }\n"
    "float distFade(float d, float dist){\n"
    "  if (dist <= 0.0) return 1.0;\n"
    "  float st = dist * 0.8; if (d <= st) return 1.0; if (d >= dist) return 0.0;\n"
    "  float t = (d - st) / (dist - st); return 1.0 - t * t * (3.0 - 2.0 * t);\n"
    "}\n"
    "float lightAtten(float d, float rng, float fo){\n"
    "  if (fo > 0.5) { float dr = d / rng; float win = max(1.0 - dr*dr*dr*dr, 0.0); win *= win; return win / (1.0 + 8.0 * d * d / (rng * rng)); }\n"
    "  float a = max(1.0 - d / rng, 0.0); return a * a;\n"
    "}\n"
    "float rectTerm(int li, vec3 p, vec3 N){\n"
    "  vec3 axis = normalize(uLDir[li]); float sum = 0.0;\n"
    "  for (int j = 0; j < 3; j++) { for (int k = 0; k < 3; k++) {\n"
    "    float fu = (float(k) - 1.0) * 0.6666667; float fv = (float(j) - 1.0) * 0.6666667;\n"
    "    vec3 q = uLPos[li] + uLRight[li] * (fu * uLRect[li].x) + uLUp[li] * (fv * uLRect[li].y); vec3 v = q - p; float d = length(v);\n"
    "    if (d < 1e-4) { sum += 1.0; }\n"
    "    else { vec3 l = v / d; float recv = dot(N, l); float emit = dot(axis, -l);\n"
    "      if (recv > 0.0 && emit > 0.0) sum += recv * emit * lightAtten(d, max(uLRange[li], 1e-4), uLFalloff[li]); }\n"
    "  } }\n"
    "  return sum / 9.0;\n"
    "}\n"
    "float cookieSample(float row, vec2 uv, bool wrapIt){\n"
    "  if (!wrapIt && (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)) return 0.0;\n"
    "  if (wrapIt) uv = fract(uv);\n"
    "  uv = clamp(uv, vec2(0.0078125), vec2(0.9921875));\n"
    "  return texture2D(uCookieTex, vec2(uv.x, (row + uv.y) / max(uCookieRows, 1.0))).r;\n"
    "}\n"
    "float cookieAt(int li, vec3 p){\n"
    "  float row = uLRect[li].z; if (row < -0.5) return 1.0;\n"
    "  if (uLType[li] > 0.5 && uLType[li] < 1.5) return 1.0;\n"           // point lights have none
    "  if (uLType[li] > 2.5) return 1.0;\n"                               // nor do area lights
    "  if (uLType[li] < 0.5) { float sc = max(uLRect[li].w, 0.05);\n"       // sun: laid across the world, repeating
    "    return cookieSample(row, vec2(dot(p, uLRight[li]) / sc + 0.5, 0.5 - dot(p, uLUp[li]) / sc), true); }\n"
    "  vec3 dd = p - uLPos[li]; float z = dot(dd, normalize(uLDir[li])); if (z <= 1e-4) return 0.0;\n"   // spot: fits the cone
    "  float co = uLCosOut[li]; float th = sqrt(max(1.0 - co * co, 0.0)) / max(co, 1e-3);\n"
    "  float k = 0.5 / (z * max(th, 1e-3));\n"
    "  return cookieSample(row, vec2(0.5 + dot(dd, uLRight[li]) * k, 0.5 - dot(dd, uLUp[li]) * k), false);\n"
    "}\n"
    "mat2 gRot;\n"                                                                              // per-pixel Poisson rotation
    "float sunTap(vec2 uv, vec2 o, float z, float ci){\n"
    "  float e = 1.0 / uCasSize;\n"                                                             // stay a texel inside the cascade tile
    "  vec2 t = clamp(uv + gRot * o, vec2(e), vec2(1.0 - e));\n"
    "  return shadow2D(uShadowTex, vec3((ci + t.x) / uCasCount, t.y, z)).r;\n"
    "}\n"
    "float sunCascade(int i, vec3 wp, vec3 n){\n"
    "  float tx = uCasTexel[i];\n"
    // World-space bias: push the receiver off its surface along the normal (more
    // for a wider filter) and a little toward the sun. Same rule for every cascade.
    "  float sinT = sqrt(max(1.0 - clamp(dot(n, uLightDir), 0.0, 1.0) * clamp(dot(n, uLightDir), 0.0, 1.0), 0.0));\n"   // slope: shadowmath::NormalOffset
    "  vec3 p = wp + n * (tx * (0.6 + 1.6 * sinT + 0.6 * uShadowSoft) * uSunBias) + uLightDir * (tx * 0.75 * uSunBias);\n"
    "  vec3 q = (uCasVP[i] * vec4(p, 1.0)).xyz * 0.5 + 0.5;\n"
    "  if (q.z >= 1.0) return 1.0;\n"
    "  float r0 = uShadowSoft / uCasSize; float ci = float(i); float s = 0.0;\n"
    "  float sr = r0 + (1.5 + 4.0 * uSunSoft) / uCasSize; float bl = 0.0;\n"      // is anything in the way within a slightly wider ring?
    "  bl += 1.0 - sunTap(q.xy, vec2( 0.000, 1.000)*sr, q.z, ci); bl += 1.0 - sunTap(q.xy, vec2( 0.866, 0.500)*sr, q.z, ci);\n"
    "  bl += 1.0 - sunTap(q.xy, vec2( 0.866,-0.500)*sr, q.z, ci); bl += 1.0 - sunTap(q.xy, vec2( 0.000,-1.000)*sr, q.z, ci);\n"
    "  bl += 1.0 - sunTap(q.xy, vec2(-0.866,-0.500)*sr, q.z, ci); bl += 1.0 - sunTap(q.xy, vec2(-0.866, 0.500)*sr, q.z, ci);\n"
    "  if (bl < 0.05) return 1.0;\n"                                                // nothing in the way: no need to filter
    "  float r = r0 * (1.0 + 2.0 * uSunSoft * (bl / 6.0));\n"                      // wider where more of the ring is blocked (no raw depth read: an approximation of the gap)

    "  s += sunTap(q.xy, vec2(-0.326,-0.406)*r, q.z, ci); s += sunTap(q.xy, vec2(-0.840,-0.074)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2(-0.696, 0.457)*r, q.z, ci); s += sunTap(q.xy, vec2(-0.203, 0.621)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2( 0.962,-0.195)*r, q.z, ci); s += sunTap(q.xy, vec2( 0.473,-0.480)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2( 0.519, 0.767)*r, q.z, ci); s += sunTap(q.xy, vec2( 0.185,-0.893)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2( 0.507, 0.064)*r, q.z, ci); s += sunTap(q.xy, vec2( 0.896, 0.412)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2(-0.322,-0.933)*r, q.z, ci); s += sunTap(q.xy, vec2(-0.792,-0.598)*r, q.z, ci);\n"
    "  return s / 12.0;\n"
    "}\n"
    "float shadowFactor(vec3 wp, vec3 n){\n"                                                    // sun: 0 = shadowed .. 1 = lit
    "  int cnt = int(uCasCount + 0.5);\n"
    "  for (int i = 0; i < 4; i++) { if (i >= cnt) break;\n"
    "    float d = length(wp - uCasSphere[i].xyz); float R = uCasSphere[i].w;\n"
    "    if (d >= R) continue;\n"                                                               // tightest cascade holding the point
    "    float s = sunCascade(i, wp, n);\n"
    "    float edge = R * 0.82;\n"                                                              // blend into the next cascade (no seam)
    "    if (d > edge) { float f = (d - edge) / (R - edge);\n"
    "      s = mix(s, (i + 1 < cnt) ? sunCascade(i + 1, wp, n) : 1.0, f); }\n"                // last one fades out at the distance
    "    return 1.0 - uSunStrength * distFade(length(wp - uEye), uFadeDist) * (1.0 - s);\n"
    "  }\n"
    "  return 1.0;\n"
    "}\n"
    "float localTap(vec2 org, vec2 uv, vec2 o, float z){\n"
    "  float e = 1.0 / uLocalSize;\n"
    "  vec2 t = clamp(uv + gRot * o, vec2(e), vec2(1.0 - e));\n"
    "  return shadow2D(uLocalTex, vec3(org + t * 0.25, z)).r;\n"
    "}\n"
    "float localShadow(int li, vec3 wp, vec3 n, vec3 l, float dist){\n"                         // spot/point/area: 0 = shadowed .. 1 = lit
    "  vec4 info = uLShadow[li];\n"
    "  float tw = dist * info.w;\n"                                                             // world size of a shadow texel here
    "  vec3 p = wp + n * (tw * (1.5 + 0.6 * uShadowSoft) * info.z) + l * (tw * info.z);\n"
    "  float tile = info.x;\n"
    "  if (uLType[li] < 1.5 || uLType[li] > 2.5) {\n"                                          // point / area: pick the cube face
    "    vec3 v = p - uLPos[li]; vec3 a = abs(v);\n"
    "    if (a.x >= a.y && a.x >= a.z) tile += (v.x > 0.0) ? 0.0 : 1.0;\n"
    "    else if (a.y >= a.z) tile += (v.y > 0.0) ? 2.0 : 3.0;\n"
    "    else tile += (v.z > 0.0) ? 4.0 : 5.0;\n"
    "  }\n"
    "  vec4 sc = uLocalVP[int(tile + 0.5)] * vec4(p, 1.0);\n"
    "  if (sc.w <= 1e-5) return 1.0;\n"
    "  vec3 q = sc.xyz / sc.w * 0.5 + 0.5;\n"
    "  if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0 || q.z >= 1.0) return 1.0;\n"
    "  vec2 org = vec2(mod(tile, 4.0), floor(tile / 4.0 + 0.01)) * 0.25;\n"
    "  float r = uShadowSoft / uLocalSize; float s = 0.0;\n"
    "  s += localTap(org, q.xy, vec2(-0.326,-0.406)*r, q.z); s += localTap(org, q.xy, vec2(-0.840,-0.074)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2(-0.696, 0.457)*r, q.z); s += localTap(org, q.xy, vec2(-0.203, 0.621)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2( 0.962,-0.195)*r, q.z); s += localTap(org, q.xy, vec2( 0.473,-0.480)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2( 0.519, 0.767)*r, q.z); s += localTap(org, q.xy, vec2( 0.185,-0.893)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2( 0.507, 0.064)*r, q.z); s += localTap(org, q.xy, vec2( 0.896, 0.412)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2(-0.322,-0.933)*r, q.z); s += localTap(org, q.xy, vec2(-0.792,-0.598)*r, q.z);\n"
    "  return 1.0 - info.y * (1.0 - s / 12.0);\n"
    "}\n"
    "void main(){\n"
    "  if (uShadowAlpha >= 0.0) { gl_FragColor = vec4(0.0, 0.0, 0.0, uShadowAlpha); return; }\n"
    "  float gAlpha = uAlpha;\n"                                  // opacity (a Custom surface may change it)
    "  vec3 N = normalize(vN);\n"
    "  if (uHasNormal > 0.5) {\n"                                  // perturb N by the tangent-space normal map
    "    vec3 tn = texture2D(uNormalTex, vUV).rgb * 2.0 - 1.0;\n"
    "    tn.xy *= uNormalStrength;\n"
    "    vec3 Tn = vTan - N * dot(N, vTan);\n"                     // Gram-Schmidt orthonormalize the tangent
    "    if (length(Tn) > 1e-5) {\n"
    "      Tn = normalize(Tn); vec3 Bn = cross(N, Tn);\n"
    "      vec3 pn = Tn * tn.x + Bn * tn.y + N * tn.z;\n"
    "      if (length(pn) > 1e-5) N = normalize(pn);\n"
    "    }\n"
    "  }\n"
    "  vec3 base = uColor * vCol;\n"
    "  if (uUseTex > 0.5) base *= texture2D(uTex, vUV).rgb;\n"
    "  vec3 Vv = normalize(uEye - vWorld);\n"
    "  float fz = 1.0 - max(dot(N, Vv), 0.0);\n"                  // Fresnel term (grazing)
    "  if (uShaderMode > 2.5 && uShaderMode < 3.5)\n"             // Gradient
    "    base = mix(uGradBot, uGradTop, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));\n"
    "  bool fres = uShaderMode > 3.5 && uShaderMode < 4.5;\n"     // Fresnel
    "  if (fres) base *= 0.10;\n"
    "  if (uShaderMode > 4.5 && uShaderMode < 5.5) {\n"           // Iridescent (oil-slick)
    "    float ph = fz * 6.2831853;\n"
    "    base *= vec3(0.5+0.5*cos(ph), 0.5+0.5*cos(ph+2.0944), 0.5+0.5*cos(ph+4.1888));\n"
    "  }\n"
    "  bool holo = uShaderMode > 5.5 && uShaderMode < 6.5;\n"     // Hologram
    "  if (holo) base *= 0.18 * (0.55 + 0.45 * sin(vWorld.y * 40.0));\n"
    "  if (uShaderMode > 6.5) base = floor(base * 5.0) / 5.0;\n"  // Posterize (retro banding)
    // Custom (.okayshader) surface: the generated okaySurface() decides albedo, emission, alpha,
    // normal, metallic, specular and shininess; the lighting below is unchanged (OkayShader.hpp).
    "#ifdef OKAY_SURFACE\n"
    "  vec3 okE = uEmissive; if (uHasEmis > 0.5) okE *= texture2D(uEmisTex, vUV).rgb;\n"
    "  OkaySurface okS = okaySurface(vUV, vWorld, Vv, vCol, uOkTime, base, okE, uAlpha, N, uMetallic, uSpecular, uShininess);\n"
    "  base = okS.albedo; gAlpha = okS.alpha;\n"
    "  if (dot(okS.normal, okS.normal) > 1e-12) N = normalize(okS.normal);\n"
    "#endif\n"
    // Filmic mode = a gamma-correct pipeline: albedo/emissive are authored in
    // sRGB, so decode to LINEAR before lighting and re-encode at the end.
    // Lighting math done directly in sRGB space (the old path, kept when the
    // toggle is off) is what makes shading look flat and washed-out.
    "  bool lin = uTonemap > 0.5;\n"
    "  if (lin) base = pow(max(base, vec3(0.0)), vec3(2.2));\n"
    "  vec3 emis = uEmissive;\n"
    "  if (uHasEmis > 0.5) emis *= texture2D(uEmisTex, vUV).rgb;\n"
    "#ifdef OKAY_SURFACE\n"
    "  emis = okS.emission;\n"
    "#endif\n"
    "  if (lin) emis = pow(max(emis, vec3(0.0)), vec3(2.2));\n"
    "  float shiny = uShininess; float metalK = uMetallic; float specK = uSpecular;\n"
    "  if (uHasMR > 0.5) {\n"
    "    vec4 mr = texture2D(uMrTex, vUV);\n"
    "    metalK *= mr.b;\n"
    "    float rough0 = clamp(1.0 - (uShininess - 4.0) / 124.0, 0.0, 1.0);\n"
    "    float rough = clamp(rough0 * mr.g, 0.0, 1.0);\n"
    "    shiny = 4.0 + (1.0 - rough) * 124.0;\n"
    "    specK *= clamp(1.0 - rough, 0.04, 1.0);\n"
    "  }\n"
    "#ifdef OKAY_SURFACE\n"
    "  metalK = okS.metallic; specK = okS.specular; shiny = okS.shininess;\n"
    "#endif\n"
    // Unlit: decode+encode cancel, so unlit colors look identical either way.
    "  if (uUnlit > 0.5) { vec3 uc = base + emis;\n"
    "    if (lin && uHdrOut < 0.5) uc = pow(max(uc, vec3(0.0)), vec3(1.0/2.2));\n"
    "    gl_FragColor = vec4(uc, gAlpha); return; }\n"
    "  bool toon = uShaderMode > 1.5 && uShaderMode < 2.5 && uToonBands > 0.5;\n"
    "  vec3 amb = mix(uAmbGround, uAmbSky, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));\n"  // scene Ambient Mode (flat / hemisphere / sky)
    "  vec3 lit = amb; vec3 spec = vec3(0.0);\n"
    // Interleaved gradient noise; source: J. Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare" (SIGGRAPH 2014).
    "  { float a = 52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) * 6.2831853;\n"
    "    float ca = cos(a), sa = sin(a); gRot = mat2(ca, sa, -sa, ca); }\n"   // interleaved-gradient noise: rotate the Poisson disk per pixel
    "  vec3 Ns = normalize(vN);\n"                                // geometric normal for shadow bias (not the bumped one)
    "  bool recv = uReceive > 0.5;\n"                            // MeshRenderer::receiveShadows
    "  float sh = (uShadowOn > 0.5 && recv) ? shadowFactor(vWorld, Ns) : 1.0;\n"   // sun occlusion (real cast shadows)
    "  if (uLightCount < 1) {\n"                                  // fallback: the global directional
    "    float ndl = max(dot(N, uLightDir), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "    lit += uLightColor * ndl * sh;\n"
    "    if (specK > 0.0) { vec3 H = normalize(uLightDir + Vv);\n"
    "      spec = vec3(pow(max(dot(N,H),0.0), max(shiny,1.0)) * specK * sh); }\n"
    "  } else {\n"
    "    for (int i = 0; i < 16; i++) { if (i >= uLightCount) break;\n"   // directional + point + spot
    "      vec3 ld; float at = 1.0;\n"
    "      float wrap = 0.0; float d = 0.0;\n"
    "      if (uLType[i] < 0.5) { ld = normalize(-uLDir[i]); }\n"        // directional
    "      else { vec3 toL = uLPos[i] - vWorld; d = length(toL); ld = toL / max(d, 1e-4);\n"
    "        float rng = max(uLRange[i], 1e-4);\n"
    "        if (uLFalloff[i] > 0.5) { float dr = d/rng; float win = max(1.0 - dr*dr*dr*dr, 0.0); win*=win;\n"  // physical inverse-square, windowed
    "          at = win / (1.0 + 8.0*d*d/(rng*rng)); }\n"
    "        else { at = max(1.0 - d / rng, 0.0); at *= at; }\n"                                                // linear rolloff
    "        if (uLType[i] > 2.5) { wrap = 0.5; }\n"                                                            // area: soft wrap fill
    "        else if (uLType[i] > 1.5) { float cs = dot(normalize(uLDir[i]), -ld);\n"   // spot cone
    "          float dn = uLCosIn[i] - uLCosOut[i];\n"
    "          float sp = dn > 1e-4 ? clamp((cs - uLCosOut[i]) / dn, 0.0, 1.0) : (cs >= uLCosOut[i] ? 1.0 : 0.0);\n"
    "          at *= sp * sp * (3.0 - 2.0 * sp); } }\n"   // smoothstep cone (okay::SpotCone)
    "      bool rect = uLType[i] > 2.5 && uLRect[i].x > 0.0 && uLRect[i].y > 0.0;\n"    // Area light with a Panel size: a real rectangle
    "      float ndl = max((dot(N, ld) + wrap) / (1.0 + wrap), 0.0);\n"
    "      if (rect) { ndl = rectTerm(i, vWorld, N); at = 1.0; }\n"
    "      if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "      if (uLRect[i].z > -0.5) at *= cookieAt(i, vWorld);\n"                       // light mask
    "      if (ndl * at <= 0.0) continue;\n"                      // no light reaches here: skip the shadow lookup
    "      float lsh = 1.0;\n"
    "      if (uLType[i] < 0.5) { if (abs(float(i) - uSunIndex) < 0.5) lsh = sh; }\n"   // only the main sun has cascades
    "      else if (recv && uLShadow[i].x > -0.5) lsh = localShadow(i, vWorld, Ns, ld, d);\n"     // spot / point / area shadow tiles
    "      lit += uLCol[i] * ndl * at * lsh;\n"
    "      if (specK > 0.0) { vec3 H = normalize(ld + Vv);\n"   // highlights take the light's colour + intensity
    "        spec += uLCol[i] * (pow(max(dot(N,H),0.0), max(shiny,1.0)) * specK * at * lsh); }\n"
    "    }\n"
    "  }\n"
    "  if (uShaderMode > 7.5) lit += fz * fz * (lit) * 0.6;\n"     // Velvet: fuzzy grazing sheen
    "  float gloss = 1.0;\n"                                      // gloss/specular map (per-texel shininess)
    "  if (uHasSpecMap > 0.5) gloss = dot(texture2D(uSpecTex, vUV).rgb, vec3(0.2126, 0.7152, 0.0722));\n"
    "  if (uHasAo > 0.5) {\n"                                     // occlude ambient + diffuse in creases
    "    float ao = dot(texture2D(uAoTex, vUV).rgb, vec3(0.2126, 0.7152, 0.0722));\n"
    "    lit *= (1.0 - uAoStrength * (1.0 - ao));\n"
    "  }\n"
    "  float metal = clamp(metalK, 0.0, 1.0);\n"                 // metalness: kill diffuse, tint spec/reflection
    "  float diffK = 1.0 - 0.9 * metal;\n"
    "  vec3 f0 = mix(vec3(1.0), base, metal);\n"                  // dielectric = white, metal = albedo-tinted
    "  if (uTonemap < 0.5 && !toon) lit = vec3(softClip(lit.x), softClip(lit.y), softClip(lit.z));\n"   // roll-off above 1.0 (the filmic path has ACES)
    "  vec3 diff = base * lit * diffK;\n"
    "  vec3 rim = vec3(0.0);\n"                                   // rim glow (Fresnel/Hologram or rimStr>0)
    "  float rs = uRimStr; if ((fres || holo) && rs < 0.8) rs = 1.6;\n"
    "  if (rs > 0.0) rim = uRimColor * (fz*fz*fz * rs);\n"
    "  vec3 col = diff + (spec * gloss) * f0 + rim + emis;\n"
    "  float reflAmt = max(uReflectivity, metal);\n"              // env reflection of the sky gradient
    "  float reflK = reflAmt * gloss;\n"
    "  if (reflK > 0.0 && uEnvOn > 0.5) {\n"
    "    float ndv = max(dot(N, Vv), 0.0);\n"
    "    vec3 R = reflect(-Vv, N); float ry = clamp(R.y, -1.0, 1.0);\n"
    "    vec3 env = mix(uSkyHor, ry >= 0.0 ? uSkyTop : uSkyBot, abs(ry));\n"
    "    if (lin) env = pow(max(env, vec3(0.0)), vec3(2.2));\n"
    "    float fr2 = 1.0 - ndv; fr2 = fr2*fr2*fr2*fr2*fr2;\n"     // Schlick (1-n·v)^5
    "    float kk = reflK + (1.0 - reflK) * fr2;\n"
    "    col = col * (1.0 - kk) + env * f0 * kk;\n"
    "  }\n"
    "  if (uFogOn > 0.5) {\n"                                      // fog (FogFactor in Atmosphere.hpp, same maths)
    "    float fd = length(uEye - vWorld);\n"
    "    float ff;\n"
    "    if (uFogMode < 0.5) ff = clamp((fd - uFogStart) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);\n"   // linear
    "    else if (uFogMode < 1.5) ff = 1.0 - exp(-uFogDensity * fd);\n"                                   // exponential
    "    else { float te = exp(-max(uEye.y - uFogHeight, 0.0) * uFogFalloff);\n"                          // height: thins above fogHeight
    "           float tw = exp(-max(vWorld.y - uFogHeight, 0.0) * uFogFalloff);\n"
    "           ff = 1.0 - exp(-uFogDensity * fd * 0.5 * (te + tw)); }\n"
    "    vec3 fc = uFogColor;\n"
    "    if (lin) fc = pow(max(fc, vec3(0.0)), vec3(2.2));\n"      // fog is authored sRGB too
    "    col = mix(col, fc, ff);\n"
    "  }\n"
    // Filmic output: ACES roll-off in linear light, then encode back to sRGB.
    // The gamma encode is what was missing before — without it linear results
    // were displayed raw, crushing mid-tones. With the post chain running the
    // colour stays linear here: the chain exposes, tonemaps and encodes it.
    "  if (lin && uHdrOut < 0.5) { col = aces(col); col = pow(max(col, vec3(0.0)), vec3(1.0/2.2)); }\n"
    "  gl_FragColor = vec4(col, gAlpha);\n"
    "}\n";

// ---- Depth-only program (shadow-map pass) -------------------------------------
// Renders just clip-space position from the light's point of view; the depth
// buffer it fills IS the shadow map the main shader samples. No colour output.
const char* kDepthVert =
    "#version 120\n"
    "SKIN\n"
    "attribute vec3 aPos;\n"
    "uniform mat4 uDepthMVP;\n"
    "void main(){ gl_Position = uDepthMVP * vec4(skinPos(aPos), 1.0); }\n";
const char* kDepthFrag =
    "#version 120\n"
    "void main(){ }\n";   // depth is written automatically; no colour

// Joints the skinning shaders take (from the GPU's vertex uniform limit; set once).
int g_skinBones = 0;

GLuint compile(GLenum type, const char* src) {
    // A "SKIN" line expands to the skinning helpers sized for this GPU.
    std::string expanded;
    if (const char* at = std::strstr(src, "SKIN\n")) {
        if (g_skinBones == 0) {
            GLint comps = 1024;   // GL guarantees at least this many vertex uniform components
            if (g.GetIntegerv) g.GetIntegerv(0x8B4A /*GL_MAX_VERTEX_UNIFORM_COMPONENTS*/, &comps);
            int n = (comps - 160) / 12;   // leave room for the other uniforms; 12 floats per joint
            g_skinBones = n > 256 ? 256 : (n < 1 ? 1 : n);
        }
        expanded.assign(src, at);
        expanded += "#define SKIN_BONES " + std::to_string(g_skinBones) + "\n";
        expanded += kSkinGlsl;
        expanded += at + 5;
        src = expanded.c_str();
    }
    GLuint s = g.CreateShader(type);
    g.ShaderSource(s, 1, &src, nullptr);
    g.CompileShader(s);
    GLint ok = 0; g.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; g.GetShaderInfoLog(s, 512, nullptr, log); Log::Error("[gl] shader: ", log); g.DeleteShader(s); return 0; }
    return s;
}
const char* kQuadVert = rhi::kFullscreenVertGlsl;
const char* kBlurFrag = rhi::kGaussianBlurGlsl;
const char* kSsaoDepthVert =
    "#version 120\n"
    "SKIN\n"
    "attribute vec3 aPos; uniform mat4 uMVP; uniform mat4 uModel; varying vec3 vWorld;\n"
    "void main(){ vec3 P = skinPos(aPos); vWorld = (uModel * vec4(P, 1.0)).xyz; gl_Position = uMVP * vec4(P, 1.0); }\n";
const char* kSsaoDepthFrag =
    "#version 120\n"
    "uniform vec3 uEye; uniform float uFar; varying vec3 vWorld;\n"
    "void main(){\n"
    "  float d = clamp(length(uEye - vWorld) / uFar, 0.0, 1.0);\n"
    "  float hi = floor(d * 255.0) / 255.0;\n"                     // 16-bit pack in R+G
    "  gl_FragColor = vec4(hi, fract(d * 255.0), 0.0, 1.0);\n"
    "}\n";
const char* kSsaoFrag =
    "#version 120\n"
    "uniform sampler2D uDepth; uniform vec2 uTexel; uniform float uRadius; uniform float uFar;\n"
    "varying vec2 vUV;\n"
    "float depthAt(vec2 uv){ vec4 t = texture2D(uDepth, uv); return t.r + t.g / 255.0; }\n"
    "void main(){\n"
    "  float d = depthAt(vUV);\n"
    "  if (d >= 0.995) { gl_FragColor = vec4(1.0); return; }\n"    // background/sky
    "  float dist = max(d * uFar, 0.5);\n"
    "  float sr = clamp(uRadius / dist, 0.004, 0.12);\n"           // world radius -> screen UV (approx)
    "  float occ = 0.0;\n"
    "  float range = uRadius * 1.6;\n"
    "  for (int i = 0; i < 12; i++) {\n"
    "    float a = float(i) * 2.618034;\n"                          // golden-angle spiral
    "    float rr = sr * (0.25 + 0.75 * float(i) / 11.0);\n"
    "    vec2 o = vec2(cos(a), sin(a)) * rr;\n"
    "    o.x *= uTexel.x / uTexel.y;\n"                             // keep the disc round
    "    float ds = depthAt(vUV + o);\n"
    "    float diffW = (d - ds) * uFar;\n"                          // + = sample nearer (occluder)
    "    if (diffW > 0.03 && diffW < range) occ += 1.0 - diffW / range;\n"
    "  }\n"
    "  float ao = 1.0 - occ / 12.0;\n"
    "  gl_FragColor = vec4(ao, ao, ao, 1.0);\n"
    "}\n";
const char* kMulFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform float uStrength; uniform float uPower; varying vec2 vUV;\n"
    "void main(){\n"
    "  vec3 ao = texture2D(uSrc, vUV).rgb;\n"
    "  if (uPower != 1.0) ao = pow(max(ao, vec3(0.0)), vec3(uPower));\n"          // PostQualityMath.hpp: AoCurve
    "  gl_FragColor = vec4(mix(vec3(1.0), ao, uStrength), 1.0);\n"  // blended DST_COLOR*src
    "}\n";
const char* kSkyFrag =
    "#version 120\n"
    "uniform mat4 uInvVP; uniform vec3 uEye; uniform vec3 uSunDir;\n"
    "uniform vec3 uZenith; uniform vec3 uHorizon; uniform vec3 uGround; uniform vec3 uSunColor;\n"
    "uniform float uSunSize; uniform float uSunHalo; uniform float uSunDisc; uniform float uHaze; uniform float uNight;\n"
    "uniform float uStarDensity; uniform float uStarBright;\n"
    "uniform float uCloudCover; uniform float uCloudDensity; uniform float uCloudScale; uniform vec2 uCloudOffset;\n"
    "uniform float uTime; uniform float uLin;\n"
    // Colour mode (uMode 0): the screen-space gradient the host paints, drawn in-frame
    // while the post chain runs so grading / bloom / the vignette cover the sky too.
    "uniform float uMode; uniform vec3 uTop; uniform vec3 uHor; uniform vec3 uBot; uniform float uHorizonPos;\n"
    "uniform float uSunOn; uniform vec2 uSunPos; uniform float uSunRad; uniform float uStarsOn; uniform vec2 uViewSize;\n"
    "varying vec2 vUV;\n"
    "float hash21(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
    "float vnoise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0)), c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));\n"
    "  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y); }\n"
    "float fbm(vec2 p){ float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; } return s; }\n"
    "void main(){\n"
    "  if (uMode < 0.5) {\n"                                                       // Colour mode (see above)
    "    float t = 1.0 - vUV.y; float hp = clamp(uHorizonPos, 0.05, 0.95);\n"
    "    vec3 col = t < hp ? mix(uTop, uHor, t / hp) : mix(uHor, uBot, (t - hp) / (1.0 - hp));\n"
    "    float hz = 1.0 - abs(t - hp) / 0.10;\n"
    "    if (hz > 0.0) col = mix(col, min(uHor * vec3(1.15, 1.15, 1.10) + vec3(0.12, 0.12, 0.10), vec3(1.0)), hz * 0.5);\n"
    "    if (uStarsOn > 0.5 && t < hp) {\n"
    "      vec2 g = vUV * uViewSize / 22.0; vec2 cell = floor(g), cf = fract(g);\n"
    "      float h = hash21(cell); vec2 sp = vec2(hash21(cell + 3.1), hash21(cell + 9.7)) * 0.8 + 0.1;\n"
    "      col += vec3((h > 1.0 - 0.5 * uStarDensity ? 1.0 : 0.0) * smoothstep(0.12, 0.0, length(cf - sp)) * uStarBright);\n"
    "    }\n"
    "    if (uSunOn > 0.5) {\n"
    "      float dp = length((vec2(vUV.x, t) - uSunPos) * uViewSize);\n"
    "      float r = max(uSunRad, 0.005) * uViewSize.y; float glowR = 0.45 * uViewSize.y;\n"
    "      for (int i = 12; i >= 1; --i) { float fr = float(i) / 12.0; if (dp < glowR * fr) col = mix(col, uSunColor, (34.0 / 255.0) * (1.0 - fr) * (1.0 - fr)); }\n"
    "      for (int gi = 4; gi >= 1; --gi) { if (dp < r * (1.0 + float(gi) * 0.7)) col = mix(col, uSunColor, (40.0 - float(gi) * 6.0) / 255.0); }\n"
    "      if (dp < r) col = uSunColor;\n"
    "    }\n"
    "    if (uLin > 0.5) col = pow(max(col, vec3(0.0)), vec3(2.2));\n"
    "    gl_FragColor = vec4(col, 1.0); return;\n"
    "  }\n"
    "  vec2 ndc = vUV * 2.0 - 1.0;\n"
    "  vec4 p0 = uInvVP * vec4(ndc, -1.0, 1.0); vec4 p1 = uInvVP * vec4(ndc, 1.0, 1.0);\n"
    "  vec3 dir = normalize(p1.xyz / p1.w - p0.xyz / p0.w);\n"
    "  float y = dir.y;\n"
    "  vec3 col = (y >= 0.0) ? mix(uHorizon, uZenith, pow(y, 0.55)) : mix(uHorizon, uGround, smoothstep(0.0, 0.25, -y));\n"
    "  vec3 hazeCol = uHorizon * 1.12 + 0.06;\n"                                   // aerial haze brightens the horizon
    "  col = mix(col, hazeCol, uHaze * 0.6 * exp(-abs(y) * 5.0));\n"
    "  vec3 flatD = normalize(vec3(dir.x, 0.0, dir.z) + vec3(1e-5)); vec3 flatS = normalize(vec3(uSunDir.x, 0.0, uSunDir.z) + vec3(1e-5));\n"
    "  col += uSunColor * (0.14 * uHaze * uSunDisc * exp(-abs(y) * 4.0) * pow(max(dot(flatD, flatS), 0.0), 3.0));\n"   // the horizon warms toward the sun
    "  float ang = acos(clamp(dot(dir, uSunDir), -1.0, 1.0));\n"
    "  float disc = (1.0 - smoothstep(uSunSize * 0.85, uSunSize * 1.15, ang)) * uSunDisc;\n"
    "  float halo = uSunHalo * uSunDisc * (0.6 * exp(-ang * 4.0) + 0.3 * exp(-ang * 1.2));\n"
    "  col += uSunColor * (disc * 1.2 + halo);\n"
    "  if (uNight > 0.0 && y > 0.0) {\n"                                           // stars: a hashed cell grid over the dome
    "    float az = atan(dir.z, dir.x); float el = asin(clamp(y, -1.0, 1.0));\n"
    "    vec2 sc = vec2(az, el) * (64.0 * (0.5 + uStarDensity));\n"
    "    vec2 ci = floor(sc), cf = fract(sc);\n"
    "    float h = hash21(ci);\n"
    "    vec2 sp = vec2(hash21(ci + 7.3), hash21(ci + 13.7)) * 0.8 + 0.1;\n"
    "    float d = length(cf - sp);\n"
    "    float star = (h > 1.0 - 0.35 * uStarDensity ? 1.0 : 0.0) * smoothstep(0.09, 0.0, d);\n"
    "    float tw = 0.7 + 0.3 * sin(uTime * 3.0 + h * 40.0);\n"
    "    col += vec3(star * uStarBright * uNight * tw * smoothstep(0.0, 0.15, y));\n"
    "  }\n"
    "  if (uCloudCover > 0.0 && y > 0.01) {\n"                                     // a cloud layer on a plane above the camera
    "    vec2 uv = dir.xz / (y + 0.12) * (0.6 / max(uCloudScale, 0.05)) + uCloudOffset;\n"
    "    float n = fbm(uv);\n"
    "    float cov = smoothstep(1.0 - uCloudCover, 1.0 - uCloudCover * 0.6 + 0.05, n);\n"
    "    float lit = clamp(0.5 + (fbm(uv + flatS.xz * 0.06) - n) * 7.0, 0.0, 1.0);\n"  // thinner toward the sun = lit edge
    "    vec3 bright = mix(vec3(1.0), uSunColor, 0.35) * (0.95 - 0.8 * uNight);\n"
    "    vec3 shade = mix(uHorizon, uZenith, 0.5) * (0.8 - 0.3 * uNight);\n"
    "    vec3 cc = mix(shade, bright, lit);\n"
    "    float a = cov * uCloudDensity * smoothstep(0.0, 0.2, y);\n"
    "    col = mix(col, cc, a);\n"
    "  }\n"
    "  if (uLin > 0.5) col = pow(max(col, vec3(0.0)), vec3(2.2));\n"              // linear for the HDR chain
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

// Bright-pass + 2x2 box downsample (uThresh 0 = plain downsample).
const char* kDownFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform vec2 uTexel; uniform float uThresh; uniform float uRadius; uniform float uSoft; varying vec2 vUV;\n"
    OKAY_BLOOM_KNEE_SRC                                                          // soft knee (PostQualityMath.hpp); uSoft 0 = the old hard cut
    "vec3 tap(vec2 uv){ vec3 c = texture2D(uSrc, uv).rgb; float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "  float k = bloomK(l, uThresh, uSoft); return c * k; }\n"
    "void main(){ vec2 o = uTexel * uRadius;\n"
    "  vec3 s = tap(vUV + vec2(-o.x, -o.y)) + tap(vUV + vec2(o.x, -o.y)) + tap(vUV + vec2(-o.x, o.y)) + tap(vUV + vec2(o.x, o.y));\n"
    "  gl_FragColor = vec4(s * 0.25, 1.0); }\n";

// Exposure, bloom composite, tonemap, colour grading and vignette in one pass.
// The alpha trick: the host composites this image over its own sky, so a sky
// pixel (alpha 0) that bloom or the vignette touch is written as "colour at
// coverage" (black at alpha v darkens whatever is behind by 1 - v).
const char* kUberFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform sampler2D uBloomA; uniform sampler2D uBloomC;\n"
    "uniform float uExposure; uniform float uLinearIn; uniform float uTonemap;\n"
    "uniform float uBloomOn; uniform float uBloomIntensity;\n"
    "uniform float uGradeOn; uniform float uTemperature; uniform float uTint; uniform float uSaturation; uniform float uContrast;\n"
    "uniform vec3 uLift; uniform vec3 uGamma; uniform vec3 uGain;\n"
    "uniform float uVignette; uniform float uVignetteSmooth; uniform float uAspect;\n"
    "varying vec2 vUV;\n"
    // Tone curves. Source: aces(): K. Narkowicz, "ACES Filmic Tone Mapping Curve" (blog, 2016); hable() A-F and the white
    // point 11.2: J. Hable, "Uncharted 2: HDR Lighting" (GDC 2010). Published formulas, used as published.
    "vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }\n"
    "vec3 hable(vec3 v){ float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;\n"
    "  return ((v*(A*v+C*B)+D*E)/(v*(A*v+B)+D*F))-E/F; }\n"
    "vec3 filmic(vec3 x){ return clamp(hable(x * 2.0) / hable(vec3(11.2)), 0.0, 1.0); }\n"
    "void main(){\n"
    "  vec4 s = texture2D(uSrc, vUV);\n"
    "  vec3 c = s.rgb * uExposure;\n"
    "  float blLum = 0.0;\n"
    "  if (uBloomOn > 0.5) {\n"
    "    vec3 bl = (texture2D(uBloomA, vUV).rgb + texture2D(uBloomC, vUV).rgb) * (0.5 * uBloomIntensity * uExposure);\n"
    "    c += bl; blLum = dot(bl, vec3(0.2126, 0.7152, 0.0722));\n"
    "  }\n"
    "  if (uLinearIn > 0.5) {\n"
    "    if (uTonemap > 1.5) c = filmic(c); else if (uTonemap > 0.5) c = aces(c); else c = clamp(c, 0.0, 1.0);\n"
    "    c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));\n"
    "  } else c = clamp(c, 0.0, 1.0);\n"
    "  if (uGradeOn > 0.5) {\n"
    "    c *= vec3(1.0 + 0.25 * uTemperature, 1.0 + 0.2 * uTint, 1.0 - 0.25 * uTemperature);\n"   // white balance
    "    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "    c = l + (c - l) * uSaturation;\n"
    "    c = (c - 0.5) * uContrast + 0.5;\n"
    "    c = c * (1.0 + uGain) + uLift * (1.0 - c);\n"                                           // lift the blacks, scale the whites
    "    c = pow(max(c, vec3(0.0)), vec3(1.0) / max(vec3(1.0) + uGamma * 0.75, vec3(0.1)));\n"   // mid-tone gamma
    "  }\n"
    "  float cov = clamp(s.a + blLum * 2.0, 0.0, 1.0);\n"
    "  float v = 0.0;\n"
    "  if (uVignette > 0.0) {\n"
    "    vec2 d = (vUV - 0.5) * vec2(uAspect, 1.0) * 2.0;\n"
    "    float r = length(d) / sqrt(uAspect * uAspect + 1.0);\n"
    "    v = 0.8 * uVignette * smoothstep(max(1.0 - uVignetteSmooth * 1.3, 0.0), 1.05, r);\n"
    "  }\n"
    "  vec3 pc = c * cov * (1.0 - v);\n"
    "  float oa = 1.0 - (1.0 - v) * (1.0 - cov);\n"
    "  gl_FragColor = vec4(oa > 1e-4 ? pc / oa : vec3(0.0), oa);\n"
    "}\n";

// Edge smoothing (the scene's "fxaa" switch; provenance audit A3). Our own pass, written from the general idea of
// image-space anti-aliasing only (find a brightness edge, blend along it), no published shader followed:
//   - brightness with the Rec. 709 luma weights (ITU-R BT.709) at the pixel and its four side neighbours;
//   - a pixel is an edge when the spread of those five is above 0.02 + 0.06 x the brightest;
//   - the edge runs across the brightness slope (right - left, down - up); taps at 1 and 2.5 texels along it on
//     both sides, weights pixel 1 : near pair 2 : far pair 1, mixed in premultiplied alpha so a silhouette against
//     an empty background keeps its colour, and blended in by 8 x the spread (all of it from a spread of 0.125).
// The constants were chosen with tests/test_gl_post_alpha.cpp on Mesa (a turned cube: more part-covered silhouette
// pixels, less total variation, no value outside its 5 x 5 neighbourhood, flat faces and empty background unchanged;
// taps further than 2.5 texels started to overshoot that neighbourhood).
const char* kEdgeSmoothFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform vec2 uTexel; varying vec2 vUV;\n"
    "float brightness(vec3 c){ return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
    "vec4 premul(vec2 at){ vec4 t = texture2D(uSrc, at); return vec4(t.rgb * t.a, t.a); }\n"
    "void main(){\n"
    "  vec4 c = texture2D(uSrc, vUV);\n"
    "  float m = brightness(c.rgb);\n"
    "  float up = brightness(texture2D(uSrc, vUV - vec2(0.0, uTexel.y)).rgb);\n"
    "  float down = brightness(texture2D(uSrc, vUV + vec2(0.0, uTexel.y)).rgb);\n"
    "  float left = brightness(texture2D(uSrc, vUV - vec2(uTexel.x, 0.0)).rgb);\n"
    "  float right = brightness(texture2D(uSrc, vUV + vec2(uTexel.x, 0.0)).rgb);\n"
    "  float top = max(m, max(max(up, down), max(left, right)));\n"
    "  float spread = top - min(m, min(min(up, down), min(left, right)));\n"
    "  vec2 slope = vec2(right - left, down - up);\n"
    "  float steep = length(slope);\n"
    "  if (spread < 0.02 + 0.06 * top || steep < 1e-4) { gl_FragColor = c; return; }\n"
    "  vec2 along = vec2(-slope.y, slope.x) * (uTexel / steep);\n"
    "  vec4 own = vec4(c.rgb * c.a, c.a);\n"
    "  vec4 sum = own + (premul(vUV + along) + premul(vUV - along)) * 2.0\n"
    "           + premul(vUV + along * 2.5) + premul(vUV - along * 2.5);\n"
    "  vec4 o = mix(own, sum * (1.0 / 7.0), clamp(spread * 8.0, 0.0, 1.0));\n"
    "  gl_FragColor = o.a > 1e-4 ? vec4(o.rgb / o.a, o.a) : c;\n"
    "}\n";

// Depth of field: blend toward the blurred frame by how far the pixel's linear
// depth (the SSAO depth pass) sits outside the focus range. The blurred copy is
// premultiplied (colour times coverage, kDofCopyFrag + kDofBlurFrag) and the mix
// happens premultiplied too, so an empty pixel (alpha 0, where the host paints its
// sky) stays empty and a far object fades into the sky without a dark fringe.
// Before, the copy and blur wrote alpha 1, which turned the sky an opaque dark blur.
const char* kDofFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform sampler2D uBlur; uniform sampler2D uDepth;\n"
    "uniform float uFocus; uniform float uRange; uniform float uFar; varying vec2 vUV;\n"
    "void main(){\n"
    "  vec4 t = texture2D(uDepth, vUV); float dn = t.r + t.g / 255.0;\n"
    "  float d = dn >= 0.995 ? uFar * 4.0 : dn * uFar;\n"                           // background: far away
    "  float coc = clamp((abs(d - uFocus) - uRange * 0.5) / max(uRange, 1e-3), 0.0, 1.0);\n"
    "  vec4 s = texture2D(uSrc, vUV);\n"
    "  vec4 p = mix(vec4(s.rgb * s.a, s.a), texture2D(uBlur, vUV), coc);\n"         // premultiplied mix
    "  gl_FragColor = vec4(p.a > 1e-4 ? p.rgb / p.a : vec3(0.0), p.a);\n"
    "}\n";
// The depth of field's blurred copy: the display image premultiplied by its coverage ...
const char* kDofCopyFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; varying vec2 vUV;\n"
    "void main(){ vec4 c = texture2D(uSrc, vUV); gl_FragColor = vec4(c.rgb * c.a, c.a); }\n";
// ... blurred with the same 9-tap gaussian as kBlurFrag, alpha included.
const char* kDofBlurFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform vec2 uDir; varying vec2 vUV;\n"
    "void main(){\n"
    // 9-tap gaussian in 5 linear taps: weights and offsets derived in SsaoMath.hpp (OKAY_SSAO_BLUR_*), checked by tests/test_post_quality_math.cpp.
    "  vec4 s = texture2D(uSrc, vUV) * 0.227027;\n"
    "  s += (texture2D(uSrc, vUV + uDir * 1.384615) + texture2D(uSrc, vUV - uDir * 1.384615)) * 0.316216;\n"
    "  s += (texture2D(uSrc, vUV + uDir * 3.230769) + texture2D(uSrc, vUV - uDir * 3.230769)) * 0.070270;\n"
    "  gl_FragColor = s;\n"
    "}\n";

GLuint buildQuadProgram(const char* fragSrc) {
    GLuint vs = compile(GL_VERTEX_SHADER, kQuadVert), fs = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) { if (vs) g.DeleteShader(vs); if (fs) g.DeleteShader(fs); return 0; }
    GLuint prog = g.CreateProgram();
    g.AttachShader(prog, vs); g.AttachShader(prog, fs);
    g.BindAttribLocation(prog, 0, "aPos");
    g.LinkProgram(prog);
    GLint ok = 0; g.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    g.DeleteShader(vs); g.DeleteShader(fs);
    if (!ok) { char log[512]; g.GetProgramInfoLog(prog, 512, nullptr, log); Log::Error("[gl] post link: ", log); g.DeleteProgram(prog); return 0; }
    return prog;
}

// A colour render target of the given format; false (and nothing left behind) on failure.
bool makeColorTarget(unsigned int& fbo, unsigned int& tex, int w, int h, bool hdr) {
    g.GenTextures(1, &tex);
    g.BindTexture(GL_TEXTURE_2D, tex);
    g.TexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g.GenFramebuffers(1, &fbo);
    g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    const bool ok = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    g.BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { g.DeleteTextures(1, &tex); tex = 0; g.DeleteFramebuffers(1, &fbo); fbo = 0; }
    return ok;
}

} // namespace gl_render
} // namespace okay
