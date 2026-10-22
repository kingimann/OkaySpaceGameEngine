// Direct3D 11 scene renderer (Windows only). Guarded so the Linux build / headless
// tests never reference Direct3D. Mirrors GLRenderer: offscreen MSAA render, resolve,
// read back RGBA8. See D3D11Renderer.hpp.
#if defined(_WIN32)

#include "okay/Render/D3D11Renderer.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture: shared texture cache
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SkinnedMesh.hpp"    // skinned meshes: VB cache keyed on SkinnedMesh::version
#include "okay/Components/Character.hpp"
#include "okay/Core/Log.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>

namespace okay {
namespace {

// HLSL. Matrix convention matches GLRenderer: the engine's column-major Mat4 is
// uploaded as-is and used as mul(M, v) (== M*v, same as GL's uMVP*v). The engine's
// projection yields GL-style clip z in [-1,1]; D3D wants [0,1], so we remap z in the
// vertex shader: z' = (z + w) / 2. No row flip is needed on read-back (D3D textures
// are top-left, like SDL).
const char* kHLSL =
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
    "  float4 uLights[64];\n"                 // 16 lights x 4: [dir,type][pos,range][col,cosOut][cosIn,..]
    "  float4 uFog;\n"                        // x=on y=start z=end (distance fog)
    "  float4 uFogCol;\n"                     // xyz = fog colour
    "  float4x4 uLightVP;\n"                  // directional shadow-map view-projection
    "  float4 uShadow;\n"                     // x=on y=texelWorld z=mapSize w=unused
    "};\n"
    "Texture2D uTex : register(t0);\n"
    "Texture2D uNormalTex : register(t1);\n"        // bump/normal map (tangent-space)
    "Texture2D uSpecTex : register(t2);\n"          // gloss/specular map
    "Texture2D uAoTex : register(t3);\n"            // ambient-occlusion map
    "Texture2D uShadowTex : register(t4);\n"        // directional shadow map (depth)
    "SamplerState uSamp : register(s0);\n"
    "SamplerState uShadowSamp : register(s1);\n"    // clamp sampler for the shadow map
    "float shadowFactor(float3 wpos, float3 n){\n"  // 0=shadowed .. 1=lit (4-tap PCF)
    "  if (uShadow.x < 0.5) return 1.0;\n"
    "  float3 wp = wpos + n * uShadow.y * 2.0;\n"                  // normal-offset bias
    "  float4 sc = mul(uLightVP, float4(wp,1.0));\n"
    "  if (sc.w <= 0.0) return 1.0;\n"
    "  float3 q = sc.xyz / sc.w;\n"
    "  float2 uv = float2(q.x*0.5+0.5, -q.y*0.5+0.5);\n"          // D3D texture v is top-down
    "  float pz = q.z*0.5+0.5;\n"                                  // GL clip z [-1,1] -> [0,1]
    "  if (uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0||pz>1.0) return 1.0;\n"
    "  float ndl = saturate(dot(n, uLightDir));\n"
    "  float bias = 0.0008 + 0.0022 * (1.0 - ndl);\n"              // slope-scaled: grazing faces need more
    "  float t = 1.8/max(uShadow.z,1.0); float s=0.0;\n"           // ~1.8-texel Poisson disk = soft edge
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.326,-0.406)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.840,-0.074)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.696, 0.457)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.203, 0.621)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.962,-0.195)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.473,-0.480)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.519, 0.767)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.185,-0.893)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.507, 0.064)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.896, 0.412)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.322,-0.933)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.792,-0.598)*t).r)?1.0:0.0;\n"
    "  return s/12.0;\n"
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
    "  if (lin) emis = pow(max(emis, 0.0), 2.2);\n"
    "  if (uUnlit > 0.5) { float3 uc = base + emis;\n"            // decode+encode cancel: unlit unchanged
    "    if (lin) uc = pow(max(uc, 0.0), 1.0/2.2);\n"
    "    return float4(uc, uAlpha); }\n"
    "  bool toon = uShaderMode > 1.5 && uShaderMode < 2.5 && uToonBands > 0.5;\n"
    "  float3 amb = uAmbient * lerp(0.55, 1.15, saturate(N.y * 0.5 + 0.5));\n"  // hemisphere ambient
    "  float3 lit = amb; float spec = 0.0;\n"
    "  float sh = shadowFactor(i.world, N);\n"                   // sun occlusion (real cast shadows)
    "  int lc = (int)uLightCount.x;\n"
    "  if (lc < 1) {\n"                                          // fallback: global directional
    "    float ndl = max(dot(N, uLightDir), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "    lit += uLightColor * ndl * sh;\n"
    "    if (uSpecular > 0.0) { float3 H = normalize(uLightDir + Vv);\n"
    "      spec = pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular * sh; }\n"
    "  } else {\n"
    "    for (int li = 0; li < 16; li++) { if (li >= lc) break;\n"      // directional + point + spot
    "      float4 d0 = uLights[li*4+0]; float4 d1 = uLights[li*4+1];\n"
    "      float4 d2 = uLights[li*4+2]; float4 d3 = uLights[li*4+3];\n"
    "      float3 ld; float at = 1.0;\n"
    "      if (d0.w < 0.5) { ld = normalize(-d0.xyz); }\n"             // directional
    "      else { float3 toL = d1.xyz - i.world; float dist = length(toL); ld = toL / max(dist, 1e-4);\n"
    "        at = max(1.0 - dist / max(d1.w, 1e-4), 0.0); at *= at;\n"
    "        if (d0.w > 1.5) { float cs = dot(normalize(d0.xyz), -ld);\n"   // spot cone
    "          float dn = d3.x - d2.w; float sp = dn > 1e-4 ? saturate((cs - d2.w) / dn) : (cs >= d2.w ? 1.0 : 0.0);\n"
    "          at *= sp * sp; } }\n"
    "      float lsh = (d0.w < 0.5) ? sh : 1.0;\n"               // cast shadows apply to the sun only
    "      float ndl = max(dot(N, ld), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "      lit += d2.xyz * ndl * at * lsh;\n"
    "      if (uSpecular > 0.0) { float3 H = normalize(ld + Vv);\n"
    "        spec += pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular * at * lsh; }\n"
    "    }\n"
    "  }\n"
    "  if (uShaderMode > 7.5) lit += fz * fz * lit * 0.6;\n"      // Velvet: fuzzy grazing sheen
    "  float gloss = 1.0;\n"                                      // gloss/specular map
    "  if (uPbr.z > 0.5) gloss = dot(uSpecTex.Sample(uSamp, i.uv).rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "  if (uPbr.w > 0.5) {\n"                                     // ambient-occlusion map
    "    float ao = dot(uAoTex.Sample(uSamp, i.uv).rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "    lit *= (1.0 - uPbr2.x * (1.0 - ao));\n"
    "  }\n"
    "  float metal = saturate(uPbr.x);\n"                         // metalness
    "  float diffK = 1.0 - 0.9 * metal;\n"
    "  float3 f0 = lerp(float3(1,1,1), base, metal);\n"
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
    "  if (uFog.x > 0.5) {\n"                                    // distance fog
    "    float fd = length(uEye - i.world);\n"
    "    float ff = saturate((fd - uFog.y) / max(uFog.z - uFog.y, 1e-3));\n"
    "    float3 fc = uFogCol.xyz;\n"
    "    if (lin) fc = pow(max(fc, 0.0), 2.2);\n"                 // fog is authored sRGB too
    "    col = lerp(col, fc, ff);\n"
    "  }\n"
    // Filmic output: ACES roll-off in linear light, then encode back to sRGB.
    "  if (lin) { col = aces(col); col = pow(max(col, 0.0), 1.0/2.2); }\n"
    "  return float4(col, uAlpha);\n"
    "}\n";

// Constant-buffer layout mirrored on the C++ side (must match the cbuffer, 16-byte
// rules: each float3 is followed by a float to fill its 16-byte row).
struct CB {
    float mvp[16];
    float model[16];
    float color[3];     float specular;
    float emissive[3];  float shininess;
    float lightDir[3];  float unlit;
    float lightColor[3]; float useTex;
    float ambient[3];   float hasNormal;
    float eye[3];       float normalStrength;
    float tiling[2];    float shadowAlpha; float alpha;
    float rimColor[3];  float shaderMode;
    float gradTop[3];   float toonBands;
    float gradBot[3];   float rimStr;
    float pbr[4];              // metallic, reflectivity, hasSpecMap, hasAo
    float pbr2[4];             // aoStrength, envOn, texOffset.x, texOffset.y
    float sky[12];             // 3 float4: top, horizon, bottom (xyz)
    float lightCount[4];       // .x = number of scene lights
    float lights[256];         // 64 float4: 16 lights x 4 rows
    float fog[4];              // x=on y=start z=end
    float fogCol[4];           // xyz = fog colour, w = filmic pipeline flag (linear + ACES + gamma)
    float lightVP[16];         // directional shadow-map view-projection
    float shadowP[4];          // x=on y=texelWorld z=mapSize w=unused
};

} // namespace

struct D3D11Renderer::Impl {
    ID3D11Device*           dev = nullptr;
    ID3D11DeviceContext*    ctx = nullptr;
    ID3D11VertexShader*     vs = nullptr;
    ID3D11PixelShader*      ps = nullptr;
    ID3D11InputLayout*      layout = nullptr;
    ID3D11Buffer*           cb = nullptr;
    ID3D11Buffer*           vb = nullptr; int vbCap = 0;
    ID3D11RasterizerState*  raster = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11DepthStencilState* depthNoWrite = nullptr;   // ground-shadow pass: test but don't write
    ID3D11BlendState*       blend = nullptr;           // alpha blend for the ground shadow
    ID3D11SamplerState*     samp = nullptr;
    ID3D11SamplerState*     sampPoint = nullptr;   // nearest filtering (crisp pixel-art)
    ID3D11SamplerState*     sampShadow = nullptr;  // clamp sampler for the shadow map
    // Directional shadow map (depth-only, single-sample):
    ID3D11VertexShader*     vsDepth = nullptr;     // position-only depth pass
    // GPU skinning (SkinnedMesh::UsesGpuSkinning): bind-pose VB + a per-vertex
    // joints/weights stream, deformed in the vertex shader by the bone palette.
    ID3D11VertexShader*     vsSkin = nullptr;      ID3D11InputLayout* layoutSkin = nullptr;
    ID3D11VertexShader*     vsDepthSkin = nullptr; ID3D11InputLayout* layoutDepthSkin = nullptr;
    ID3D11Buffer*           bonesCB = nullptr;     // 768 float4 (256 joints x 3 rows)
    struct SkinVB { ID3D11Buffer* buf = nullptr; int count = 0; const void* vp = nullptr; std::size_t vn = 0, tn = 0; const void* jp = nullptr; unsigned lastUse = 0; };
    std::unordered_map<const void*, SkinVB> skinVB;   // key: &SkinnedMesh::bind
    SkinVB* EnsureSkinVB(const SkinnedMesh& sm);
    bool UploadBones(const SkinnedMesh& sm);
    bool CanGpuSkin(const SkinnedMesh* sm) const {
        return sm && vsSkin && layoutSkin && bonesCB && sm->UsesGpuSkinning() && !sm->palette.empty() &&
               sm->jointIdx.size() == sm->bind.vertices.size() && sm->jointWt.size() == sm->bind.vertices.size();
    }
    ID3D11InputLayout*      layoutDepth = nullptr;
    ID3D11Texture2D*        shadowTex = nullptr;
    ID3D11DepthStencilView* shadowDsv = nullptr;
    ID3D11ShaderResourceView* shadowSrv = nullptr;
    ID3D11Buffer*           vbDepth = nullptr; int vbDepthCap = 0;
    int shadowSize = 0;
    bool EnsureShadow(int size);
    // Size-dependent targets:
    int w = 0, h = 0, samples = 0;
    ID3D11Texture2D*        colorTex = nullptr;  ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D*        depthTex = nullptr;  ID3D11DepthStencilView* dsv = nullptr;
    ID3D11Texture2D*        resolveTex = nullptr;
    ID3D11Texture2D*        staging = nullptr;
    int reqSamples = 0;   // the MSAA count asked for (`samples` is what the device gave)
    DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;      // colour format of the live set
    DXGI_FORMAT wantFmt = DXGI_FORMAT_R8G8B8A8_UNORM;  // what the next EnsureTargets should build
    // Target sets for the OTHER sizes in use. The editor renders the Scene view,
    // Game view, Camera Preview and Animation preview at different sizes every
    // frame; one shared set meant destroying + re-creating MSAA, depth, resolve and
    // staging textures several times per frame. Parked sets are swapped back in.
    struct TargetSet {
        int w = 0, h = 0, samples = 0, reqSamples = 0;
        DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        ID3D11Texture2D* colorTex = nullptr; ID3D11RenderTargetView* rtv = nullptr;
        ID3D11Texture2D* depthTex = nullptr; ID3D11DepthStencilView* dsv = nullptr;
        ID3D11Texture2D* resolveTex = nullptr; ID3D11Texture2D* staging = nullptr;
        unsigned lastUse = 0;
    };
    std::vector<TargetSet> parked;
    // Pipelined read-back (SetReadbackStream): two staging textures per view,
    // written on alternate frames; the one from last frame is read.
    struct Pipe {
        ID3D11Texture2D* st[2] = {};
        int w = 0, h = 0, idx = 0;
        bool filled[2] = {};
        std::chrono::steady_clock::time_point last{};
    };
    std::unordered_map<int, Pipe> pipes;
    int stream = -1;
    ID3D11Texture2D* directDst = nullptr;   // RenderToTexture: resolve here, skip read-back
    bool shared = false;                    // built on a host device (InitShared)
    bool stateSaved = false;                // host pipeline state is already backed up
    unsigned targetUse = 0;
    void ReleaseSet(TargetSet& t);
    std::unordered_map<std::string, ID3D11ShaderResourceView*> texCache;
    std::vector<float> verts;
    std::vector<std::uint32_t> pixels;

    // Per-mesh vertex-buffer cache (mirrors GLRenderer): static geometry (imported
    // models, primitives) is expanded + uploaded ONCE (immutable buffer) and drawn
    // from GPU memory, instead of being rebuilt on the CPU and re-uploaded every
    // frame — which made large OBJ imports crawl. Revalidated per frame via an
    // allocation fingerprint + sampled content hash, exactly about once a second.
    // Skinned meshes are cached too, keyed on SkinnedMesh::version (a DYNAMIC
    // buffer rewritten in place when the pose changes); Character meshes stream.
    struct MeshVB {
        ID3D11Buffer* vbuf = nullptr;
        int vertCount = 0;
        const void* vp = nullptr;  std::size_t vn = 0;
        const void* tp = nullptr;  std::size_t tn = 0;
        const void* np = nullptr;  std::size_t nn = 0;
        const void* uvp = nullptr; std::size_t uvn = 0;
        const void* fcp = nullptr; std::size_t fcn = 0;
        std::uint64_t sampleHash = 0, fullHash = 0;
        unsigned lastUse = 0, lastFullHash = 0;
        std::uint64_t version = 0;   // SkinnedMesh::version when skinned (0 = static)
        int cap = 0;                 // dynamic buffers: capacity in vertices
    };
    std::unordered_map<const void*, MeshVB> meshVB;
    unsigned frame = 0;
    MeshVB* EnsureMeshVB(const Mesh& mesh, std::uint64_t version = 0);   // expand + upload on miss
    void BuildExpandedVerts(const Mesh& mesh); // fill `verts` (14 floats/vertex)

    ID3D11ShaderResourceView* TextureFor(const std::string& name);
    bool EnsureTargets(int W, int H, int S);
    void DestroyTargets();
};

bool D3D11Renderer::InitShared(void* device) {
    if (m_impl) return m_impl->shared;
    if (!device) return false;
    m_impl = new Impl();
    m_impl->dev = static_cast<ID3D11Device*>(device);
    m_impl->dev->AddRef();
    m_impl->dev->GetImmediateContext(&m_impl->ctx);   // AddRefs
    m_impl->shared = true;
    return Init();
}
bool D3D11Renderer::Shared() const { return m_impl && m_impl->shared && m_impl->vs; }
bool D3D11Renderer::SupportsGpuSkinning() const {
    return m_impl && m_impl->vsSkin && m_impl->layoutSkin && m_impl->vsDepthSkin && m_impl->layoutDepthSkin && m_impl->bonesCB;
}

bool D3D11Renderer::Init() {
    if (m_impl && m_impl->vs) return true;
    if (!m_impl) m_impl = new Impl();
    Impl* p = m_impl;
    if (!p->dev) {
        UINT flags = 0;
        D3D_FEATURE_LEVEL got;
        const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, want, 3,
                                     D3D11_SDK_VERSION, &p->dev, &got, &p->ctx))) {
            Log::Error("[d3d11] device creation failed");
            Destroy(); return false;
        }
    }
    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    const SIZE_T n = (SIZE_T)std::strlen(kHLSL);
    if (FAILED(D3DCompile(kHLSL, n, "okay3d", nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsb, &err)) ||
        FAILED(D3DCompile(kHLSL, n, "okay3d", nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psb, &err))) {
        if (err) err->Release(); if (vsb) vsb->Release();
        Log::Error("[d3d11] shader compile failed"); Destroy(); return false;
    }
    p->dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &p->vs);
    p->dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &p->ps);
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    p->dev->CreateInputLayout(il, 5, vsb->GetBufferPointer(), vsb->GetBufferSize(), &p->layout);
    vsb->Release(); psb->Release();

    D3D11_BUFFER_DESC cbd; std::memset(&cbd, 0, sizeof(cbd));
    cbd.ByteWidth = sizeof(CB); cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    p->dev->CreateBuffer(&cbd, nullptr, &p->cb);

    D3D11_RASTERIZER_DESC rd; std::memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;   // match GLRenderer (no cull)
    rd.DepthClipEnable = TRUE; rd.MultisampleEnable = TRUE;
    p->dev->CreateRasterizerState(&rd, &p->raster);

    D3D11_DEPTH_STENCIL_DESC dd; std::memset(&dd, 0, sizeof(dd));
    dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    p->dev->CreateDepthStencilState(&dd, &p->depth);
    // Ground-shadow pass: depth-test but don't write (so it never occludes geometry).
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    p->dev->CreateDepthStencilState(&dd, &p->depthNoWrite);
    // Straight alpha blend for the translucent shadow.
    D3D11_BLEND_DESC bd2; std::memset(&bd2, 0, sizeof(bd2));
    bd2.RenderTarget[0].BlendEnable = TRUE;
    bd2.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd2.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd2.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd2.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd2.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd2.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd2.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    p->dev->CreateBlendState(&bd2, &p->blend);

    D3D11_SAMPLER_DESC sd; std::memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_ANISOTROPIC;   // sharp textures at grazing angles (floors/terrain)
    sd.MaxAnisotropy = 8;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    p->dev->CreateSamplerState(&sd, &p->samp);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;   // crisp pixel-art (no up-close blur)
    sd.MaxAnisotropy = 1;
    p->dev->CreateSamplerState(&sd, &p->sampPoint);
    // Shadow-map sampler: linear filter, clamp at the borders (outside = lit).
    D3D11_SAMPLER_DESC ss; std::memset(&ss, 0, sizeof(ss));
    ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ss.MaxLOD = D3D11_FLOAT32_MAX;
    p->dev->CreateSamplerState(&ss, &p->sampShadow);

    // Depth-only vertex shader + position-only input layout for the shadow pass.
    ID3DBlob* vdb = nullptr; ID3DBlob* derr = nullptr;
    if (SUCCEEDED(D3DCompile(kHLSL, n, "okay3d", nullptr, nullptr, "VSDepth", "vs_4_0", 0, 0, &vdb, &derr)) && vdb) {
        p->dev->CreateVertexShader(vdb->GetBufferPointer(), vdb->GetBufferSize(), nullptr, &p->vsDepth);
        const D3D11_INPUT_ELEMENT_DESC ild[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        p->dev->CreateInputLayout(ild, 1, vdb->GetBufferPointer(), vdb->GetBufferSize(), &p->layoutDepth);
        vdb->Release();
    }
    if (derr) derr->Release();

    // GPU skinning shaders + layouts (stream 0: the usual 14-float vertex,
    // stream 1: joints.xyzw + weights.xyzw). Optional: without them skinned
    // meshes keep deforming on the CPU.
    {
        ID3DBlob* sb = nullptr; ID3DBlob* se = nullptr;
        if (SUCCEEDED(D3DCompile(kHLSL, n, "okay3d", nullptr, nullptr, "VSSkin", "vs_4_0", 0, 0, &sb, &se)) && sb) {
            p->dev->CreateVertexShader(sb->GetBufferPointer(), sb->GetBufferSize(), nullptr, &p->vsSkin);
            const D3D11_INPUT_ELEMENT_DESC ils[] = {
                {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR",        0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            p->dev->CreateInputLayout(ils, 7, sb->GetBufferPointer(), sb->GetBufferSize(), &p->layoutSkin);
            sb->Release();
        } else if (se) Log::Warning(std::string("[d3d11] skinning shader: ") + (const char*)se->GetBufferPointer());
        if (se) { se->Release(); se = nullptr; }
        if (SUCCEEDED(D3DCompile(kHLSL, n, "okay3d", nullptr, nullptr, "VSDepthSkin", "vs_4_0", 0, 0, &sb, &se)) && sb) {
            p->dev->CreateVertexShader(sb->GetBufferPointer(), sb->GetBufferSize(), nullptr, &p->vsDepthSkin);
            const D3D11_INPUT_ELEMENT_DESC ilds[] = {
                {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            p->dev->CreateInputLayout(ilds, 3, sb->GetBufferPointer(), sb->GetBufferSize(), &p->layoutDepthSkin);
            sb->Release();
        }
        if (se) se->Release();
        D3D11_BUFFER_DESC bb; std::memset(&bb, 0, sizeof(bb));
        bb.ByteWidth = 768 * 16; bb.Usage = D3D11_USAGE_DYNAMIC;
        bb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        p->dev->CreateBuffer(&bb, nullptr, &p->bonesCB);
    }

    return p->dev && p->vs && p->ps && p->layout && p->cb && p->raster && p->depth && p->samp;
}

bool D3D11Renderer::Available() const { return m_impl && m_impl->dev && m_impl->vs; }

void D3D11Renderer::Impl::ReleaseSet(TargetSet& t) {
    if (t.rtv)        t.rtv->Release();
    if (t.dsv)        t.dsv->Release();
    if (t.colorTex)   t.colorTex->Release();
    if (t.depthTex)   t.depthTex->Release();
    if (t.resolveTex) t.resolveTex->Release();
    if (t.staging)    t.staging->Release();
    t = TargetSet{};
}

void D3D11Renderer::Impl::DestroyTargets() {
    if (rtv)        { rtv->Release(); rtv = nullptr; }
    if (dsv)        { dsv->Release(); dsv = nullptr; }
    if (colorTex)   { colorTex->Release(); colorTex = nullptr; }
    if (depthTex)   { depthTex->Release(); depthTex = nullptr; }
    if (resolveTex) { resolveTex->Release(); resolveTex = nullptr; }
    if (staging)    { staging->Release(); staging = nullptr; }
    w = h = samples = reqSamples = 0;
}

bool D3D11Renderer::Impl::EnsureTargets(int W, int H, int S) {
    // A fully-built target set is cached by size; a half-built one (a mid-sequence
    // CreateTexture2D/View failure) must NOT satisfy the cache — check the views we
    // actually bind, not just colorTex. Otherwise the next call returns "ready" with
    // a null rtv/dsv and the render binds a null render target (hard crash).
    ++targetUse;
    if (colorTex && rtv && dsv && resolveTex && staging &&
        w == W && h == H && reqSamples == S && fmt == wantFmt) return true;
    // Park the current (complete) set, then swap in a parked one of this size.
    if (colorTex && rtv && dsv && resolveTex && staging) {
        TargetSet t;
        t.w = w; t.h = h; t.samples = samples; t.reqSamples = reqSamples; t.fmt = fmt;
        t.colorTex = colorTex; t.rtv = rtv; t.depthTex = depthTex; t.dsv = dsv;
        t.resolveTex = resolveTex; t.staging = staging; t.lastUse = targetUse;
        colorTex = depthTex = resolveTex = staging = nullptr; rtv = nullptr; dsv = nullptr;
        parked.push_back(t);
    }
    for (std::size_t i = 0; i < parked.size(); ++i) {
        TargetSet& t = parked[i];
        if (t.w != W || t.h != H || t.reqSamples != S || t.fmt != wantFmt) continue;
        DestroyTargets();
        w = t.w; h = t.h; samples = t.samples; reqSamples = t.reqSamples; fmt = t.fmt;
        colorTex = t.colorTex; rtv = t.rtv; depthTex = t.depthTex; dsv = t.dsv;
        resolveTex = t.resolveTex; staging = t.staging;
        parked.erase(parked.begin() + (std::ptrdiff_t)i);
        return true;
    }
    // Keep a handful (a view being resized leaves a trail of stale sizes).
    while (parked.size() > 4) {
        std::size_t oldest = 0;
        for (std::size_t i = 1; i < parked.size(); ++i) if (parked[i].lastUse < parked[oldest].lastUse) oldest = i;
        ReleaseSet(parked[oldest]);
        parked.erase(parked.begin() + (std::ptrdiff_t)oldest);
    }
    DestroyTargets();
    // Any partial failure below returns false; reset the size cache so the next call
    // starts clean (DestroyTargets nulls the resources, but the size fields linger).
    w = 0; h = 0; samples = 0;
    struct FailReset { Impl* p; bool ok = false; ~FailReset(){ if(!ok){ p->DestroyTargets(); p->w=p->h=p->samples=0; } } } fr{this};
    w = W; h = H; reqSamples = S; fmt = wantFmt;
    // Clamp the requested MSAA to what the device actually supports for BOTH the color
    // and depth formats. Creating a multisampled texture with an unsupported count is a
    // common hard-failure (and on some drivers a crash) on integrated GPUs / RDP / VMs,
    // so drop to the next lower supported count, down to 1 (no MSAA).
    int want = S < 1 ? 1 : S;
    samples = 1;
    for (int s = want; s >= 1; --s) {
        UINT qC = 0, qD = 0;
        if (SUCCEEDED(dev->CheckMultisampleQualityLevels(fmt, (UINT)s, &qC)) && qC > 0 &&
            SUCCEEDED(dev->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, (UINT)s, &qD)) && qD > 0) {
            samples = s; break;
        }
    }

    D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt; td.SampleDesc.Count = samples;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &colorTex))) return false;
    if (FAILED(dev->CreateRenderTargetView(colorTex, nullptr, &rtv))) return false;

    D3D11_TEXTURE2D_DESC dz = td;
    dz.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dz.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(dev->CreateTexture2D(&dz, nullptr, &depthTex))) return false;
    if (FAILED(dev->CreateDepthStencilView(depthTex, nullptr, &dsv))) return false;

    D3D11_TEXTURE2D_DESC tr = td;
    tr.SampleDesc.Count = 1; tr.BindFlags = 0;
    if (FAILED(dev->CreateTexture2D(&tr, nullptr, &resolveTex))) return false;

    D3D11_TEXTURE2D_DESC ts = tr;
    ts.Usage = D3D11_USAGE_STAGING; ts.BindFlags = 0; ts.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateTexture2D(&ts, nullptr, &staging))) return false;
    fr.ok = true;   // fully built — keep the cache
    return true;
}

// (Re)create the directional shadow map: a single-sample typeless depth texture
// usable BOTH as a depth-stencil target (depth pass) and a shader resource (main
// pass PCF lookup). Returns false on failure (renderer still works, no shadows).
bool D3D11Renderer::Impl::EnsureShadow(int size) {
    if (shadowTex && shadowSize == size) return true;
    if (shadowSrv) { shadowSrv->Release(); shadowSrv = nullptr; }
    if (shadowDsv) { shadowDsv->Release(); shadowDsv = nullptr; }
    if (shadowTex) { shadowTex->Release(); shadowTex = nullptr; }
    shadowSize = 0;
    if (!vsDepth || !layoutDepth) return false;

    D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
    td.Width = size; td.Height = size; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R24G8_TYPELESS; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &shadowTex))) return false;

    D3D11_DEPTH_STENCIL_VIEW_DESC dvd; std::memset(&dvd, 0, sizeof(dvd));
    dvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(dev->CreateDepthStencilView(shadowTex, &dvd, &shadowDsv))) { shadowTex->Release(); shadowTex = nullptr; return false; }

    D3D11_SHADER_RESOURCE_VIEW_DESC svd; std::memset(&svd, 0, sizeof(svd));
    svd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    svd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(shadowTex, &svd, &shadowSrv))) {
        shadowDsv->Release(); shadowDsv = nullptr; shadowTex->Release(); shadowTex = nullptr; return false;
    }
    shadowSize = size;
    return true;
}

// ---- Per-mesh vertex-buffer cache (mirrors GLRenderer.cpp) -------------------
static std::uint64_t FnvBytes(const void* data, std::size_t nbytes,
                              std::uint64_t seed = 1469598103934665603ull) {
    const unsigned char* pB = (const unsigned char*)data;
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < nbytes; ++i) { h ^= pB[i]; h *= 1099511628211ull; }
    return h;
}
static std::uint64_t MeshSampleHash(const Mesh& m) {
    std::size_t sizes[5] = { m.vertices.size(), m.triangles.size(), m.normals.size(),
                             m.uvs.size(), m.triColors.size() };
    std::uint64_t h = FnvBytes(sizes, sizeof(sizes));
    const std::size_t vn = m.vertices.size(), step = vn / 2048 + 1;
    for (std::size_t i = 0; i < vn; i += step) h = FnvBytes(&m.vertices[i], sizeof(Vec3), h);
    const std::size_t tn = m.triangles.size(), tstep = tn / 2048 + 1;
    for (std::size_t i = 0; i < tn; i += tstep) h = FnvBytes(&m.triangles[i], sizeof(int), h);
    return h;
}
static std::uint64_t MeshFullHash(const Mesh& m) {
    std::uint64_t h = 1469598103934665603ull;
    if (!m.vertices.empty())  h = FnvBytes(m.vertices.data(),  m.vertices.size()  * sizeof(Vec3),  h);
    if (!m.normals.empty())   h = FnvBytes(m.normals.data(),   m.normals.size()   * sizeof(Vec3),  h);
    if (!m.uvs.empty())       h = FnvBytes(m.uvs.data(),       m.uvs.size()       * sizeof(Vec2),  h);
    if (!m.triangles.empty()) h = FnvBytes(m.triangles.data(), m.triangles.size() * sizeof(int),   h);
    if (!m.triColors.empty()) h = FnvBytes(m.triColors.data(), m.triColors.size() * sizeof(Color), h);
    return h;
}

// Expand indexed triangles into the interleaved non-indexed layout the shader
// consumes: pos(3)+normal(3)+uv(2)+color(3)+tangent(3) = 14 floats per vertex.
void D3D11Renderer::Impl::BuildExpandedVerts(const Mesh& mesh) {
    const auto& V = mesh.vertices; const auto& T = mesh.triangles;
    const bool hasN = mesh.HasNormals();
    const bool hasUV = !mesh.uvs.empty() && mesh.uvs.size() == V.size();
    const bool faceCols = mesh.HasFaceColors();
    const int nv = (int)V.size();
    verts.resize(T.size() / 3 * 3 * 14);   // written through a raw pointer, trimmed at the end
    float* out = verts.data();
    for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
        int a = T[i], b = T[i + 1], cc = T[i + 2];
        if (a < 0 || b < 0 || cc < 0 || a >= nv || b >= nv || cc >= nv) continue; // skip bad tris
        Vec3 fn = Vec3::Cross(V[b] - V[a], V[cc] - V[a]);
        { float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
        const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
        float cr = 1.0f, cg = 1.0f, cb2 = 1.0f;  // white = no-op when there are no face colors
        if (faceCols) { const Color& fc = mesh.triColors[i / 3]; cr = fc.r; cg = fc.g; cb2 = fc.b; }
        const int idx[3] = {a, b, cc};
        float uu[3], vv[3];
        for (int k = 0; k < 3; ++k) {
            const Vec3& pos = V[idx[k]];
            if (hasUV) { uu[k] = mesh.uvs[idx[k]].x; vv[k] = mesh.uvs[idx[k]].y; }
            else if (ax >= ay && ax >= az) { uu[k] = pos.z + 0.5f; vv[k] = pos.y + 0.5f; }
            else if (ay >= ax && ay >= az) { uu[k] = pos.x + 0.5f; vv[k] = pos.z + 0.5f; }
            else                           { uu[k] = pos.x + 0.5f; vv[k] = pos.y + 0.5f; }
        }
        Vec3 e1 = V[b] - V[a], e2 = V[cc] - V[a];
        float du1 = uu[1] - uu[0], dv1 = vv[1] - vv[0];
        float du2 = uu[2] - uu[0], dv2 = vv[2] - vv[0];
        float det = du1 * dv2 - du2 * dv1;
        Vec3 tan = std::fabs(det) > 1e-8f ? (e1 * dv2 - e2 * dv1) * (1.0f / det) : e1;
        { float m = tan.Magnitude(); tan = m > 1e-6f ? tan * (1.0f / m) : Vec3{1, 0, 0}; }
        for (int k = 0; k < 3; ++k) {
            const Vec3& pos = V[idx[k]];
            Vec3 nrm = hasN ? mesh.normals[idx[k]] : fn;
            out[0] = pos.x; out[1] = pos.y; out[2] = pos.z;
            out[3] = nrm.x; out[4] = nrm.y; out[5] = nrm.z;
            out[6] = uu[k]; out[7] = vv[k];
            out[8] = cr;    out[9] = cg;    out[10] = cb2;
            out[11] = tan.x; out[12] = tan.y; out[13] = tan.z;
            out += 14;
        }
    }
    verts.resize((std::size_t)(out - verts.data()));
}

// Return the up-to-date cached GPU buffer for a mesh, expanding + uploading only
// when the mesh actually changed. nullptr = mesh has no drawable triangles.
// Joint indices + weights per EXPANDED vertex (same triangle-corner order and
// bad-triangle skipping as BuildExpandedVerts), uploaded once per bind mesh.
D3D11Renderer::Impl::SkinVB* D3D11Renderer::Impl::EnsureSkinVB(const SkinnedMesh& sm) {
    const Mesh& m = sm.bind;
    SkinVB& e = skinVB[&m];
    e.lastUse = frame;
    if (e.buf && e.vp == (const void*)m.vertices.data() && e.vn == m.vertices.size() &&
        e.tn == m.triangles.size() && e.jp == (const void*)sm.jointIdx.data()) return &e;
    if (e.buf) { e.buf->Release(); e.buf = nullptr; }
    const auto& T = m.triangles; const int nv = (int)m.vertices.size();
    std::vector<float> d; d.reserve(T.size() * 8);
    for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
        const int idx[3] = {T[i], T[i + 1], T[i + 2]};
        if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0 || idx[0] >= nv || idx[1] >= nv || idx[2] >= nv) continue;
        for (int k = 0; k < 3; ++k) {
            const auto& ji = sm.jointIdx[idx[k]]; const auto& jw = sm.jointWt[idx[k]];
            const int maxJ = (int)sm.palette.size() - 1;
            for (int q = 0; q < 4; ++q) d.push_back((float)(ji[q] < 0 ? 0 : ji[q] > maxJ ? maxJ : ji[q]));
            for (int q = 0; q < 4; ++q) d.push_back(ji[q] < 0 || ji[q] > maxJ ? 0.0f : jw[q]);
        }
    }
    e.vp = m.vertices.data(); e.vn = m.vertices.size(); e.tn = m.triangles.size(); e.jp = sm.jointIdx.data();
    e.count = (int)(d.size() / 8);
    if (e.count == 0) return nullptr;
    D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = (UINT)(d.size() * sizeof(float)); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init; std::memset(&init, 0, sizeof(init)); init.pSysMem = d.data();
    if (FAILED(dev->CreateBuffer(&bd, &init, &e.buf))) { e.buf = nullptr; e.count = 0; return nullptr; }
    return &e;
}

bool D3D11Renderer::Impl::UploadBones(const SkinnedMesh& sm) {
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(bonesCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return false;
    float* o = static_cast<float*>(ms.pData);
    const std::size_t n = sm.palette.size() < (std::size_t)SkinnedMesh::kMaxGpuJoints ? sm.palette.size() : (std::size_t)SkinnedMesh::kMaxGpuJoints;
    for (std::size_t j = 0; j < n; ++j) {
        const Mat4& M = sm.palette[j];
        for (int r = 0; r < 3; ++r)   // row r: M(r,0..3)
            for (int c2 = 0; c2 < 4; ++c2) *o++ = M.at(c2, r);
    }
    ctx->Unmap(bonesCB, 0);
    return true;
}

D3D11Renderer::Impl::MeshVB* D3D11Renderer::Impl::EnsureMeshVB(const Mesh& mesh, std::uint64_t version) {
    MeshVB& e = meshVB[&mesh];
    e.lastUse = frame;
    bool same = e.vbuf != nullptr &&
        e.vp  == (const void*)mesh.vertices.data()  && e.vn  == mesh.vertices.size()  &&
        e.tp  == (const void*)mesh.triangles.data() && e.tn  == mesh.triangles.size() &&
        e.np  == (const void*)mesh.normals.data()   && e.nn  == mesh.normals.size()   &&
        e.uvp == (const void*)mesh.uvs.data()       && e.uvn == mesh.uvs.size()       &&
        e.fcp == (const void*)mesh.triColors.data() && e.fcn == mesh.triColors.size();
    std::uint64_t sh = 0;
    if (version) {   // skinned: the deformer says when it changed, no hashing needed
        if (same && e.version == version) return e.vertCount > 0 ? &e : nullptr;
        same = false;
    }
    if (same) { sh = MeshSampleHash(mesh); if (sh != e.sampleHash) same = false; }
    if (same && frame - e.lastFullHash >= 60) {     // exact check ~once a second
        std::uint64_t fh = MeshFullHash(mesh);
        e.lastFullHash = frame;
        if (fh != e.fullHash) same = false; else e.fullHash = fh;
    }
    if (same) return e.vertCount > 0 ? &e : nullptr;
    BuildExpandedVerts(mesh);
    e.vp  = mesh.vertices.data();  e.vn  = mesh.vertices.size();
    e.tp  = mesh.triangles.data(); e.tn  = mesh.triangles.size();
    e.np  = mesh.normals.data();   e.nn  = mesh.normals.size();
    e.uvp = mesh.uvs.data();       e.uvn = mesh.uvs.size();
    e.fcp = mesh.triColors.data(); e.fcn = mesh.triColors.size();
    e.vertCount = (int)(verts.size() / 14);
    if (version) {
        // Skinned: rewrite a DYNAMIC buffer in place (re-created only to grow, or
        // when it was a static one). One upload per pose, shared by every pass.
        const bool wasStatic = e.vbuf && e.version == 0;
        e.version = version;
        if (e.vertCount == 0) return nullptr;
        if (e.vbuf && (wasStatic || e.vertCount > e.cap)) { e.vbuf->Release(); e.vbuf = nullptr; e.cap = 0; }
        if (!e.vbuf) {
            const int cap = e.vertCount + e.vertCount / 8;
            D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
            bd.ByteWidth = (UINT)((std::size_t)cap * 14 * sizeof(float)); bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &e.vbuf))) { e.vbuf = nullptr; e.vertCount = 0; return nullptr; }
            e.cap = cap;
        }
        D3D11_MAPPED_SUBRESOURCE ms;
        if (FAILED(ctx->Map(e.vbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { e.version = 0; e.vertCount = 0; return nullptr; }
        std::memcpy(ms.pData, verts.data(), verts.size() * sizeof(float));
        ctx->Unmap(e.vbuf, 0);
        return &e;
    }
    e.version = 0; e.cap = 0;
    e.sampleHash = sh ? sh : MeshSampleHash(mesh);
    e.fullHash = MeshFullHash(mesh);
    e.lastFullHash = frame;
    if (e.vbuf) { e.vbuf->Release(); e.vbuf = nullptr; }
    if (e.vertCount == 0) return nullptr;
    D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = (UINT)(verts.size() * sizeof(float));
    bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init; std::memset(&init, 0, sizeof(init));
    init.pSysMem = verts.data();
    if (FAILED(dev->CreateBuffer(&bd, &init, &e.vbuf))) { e.vbuf = nullptr; e.vertCount = 0; return nullptr; }
    return &e;
}

ID3D11ShaderResourceView* D3D11Renderer::Impl::TextureFor(const std::string& name) {
    if (name.empty()) return nullptr;
    auto it = texCache.find(name);
    if (it != texCache.end()) return it->second;
    ID3D11ShaderResourceView* srv = nullptr;
    Image* img = GetCachedTexture(name);
    if (img && img->Width() > 0) {
        D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
        td.Width = img->Width(); td.Height = img->Height(); td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sr; std::memset(&sr, 0, sizeof(sr));
        sr.pSysMem = img->Data(); sr.SysMemPitch = (UINT)img->Width() * 4;
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(dev->CreateTexture2D(&td, &sr, &tex)) && tex) {
            dev->CreateShaderResourceView(tex, nullptr, &srv);
            tex->Release();
        }
    }
    texCache[name] = srv;   // cache failures (nullptr) too
    return srv;
}

namespace {
// Everything the host renderer (SDL's D3D11 backend caches its bindings) could
// notice us changing on the shared immediate context. Mirrors the state backup
// in Dear ImGui's DX11 backend, widened to the slots this renderer uses.
struct D3D11StateBackup {
    static constexpr int kSrv = 8, kSamp = 4, kCb = 2;
    UINT scissorN = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, viewportN = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT scissor[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_VIEWPORT viewport[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11RasterizerState* rs = nullptr;
    ID3D11BlendState* blend = nullptr; FLOAT blendFactor[4]; UINT sampleMask = 0;
    ID3D11DepthStencilState* ds = nullptr; UINT stencilRef = 0;
    ID3D11ShaderResourceView* psSrv[kSrv] = {};
    ID3D11SamplerState* psSamp[kSamp] = {};
    ID3D11PixelShader* ps = nullptr; ID3D11VertexShader* vs = nullptr; ID3D11GeometryShader* gs = nullptr;
    ID3D11ClassInstance* psInst[256]; ID3D11ClassInstance* vsInst[256]; ID3D11ClassInstance* gsInst[256];
    UINT psInstN = 256, vsInstN = 256, gsInstN = 256;
    ID3D11Buffer* vsCb[kCb] = {}; ID3D11Buffer* psCb[kCb] = {};
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0;
    ID3D11Buffer* vb = nullptr; UINT vbStride = 0, vbOff = 0;
    ID3D11InputLayout* layout = nullptr;
    ID3D11RenderTargetView* rtv = nullptr; ID3D11DepthStencilView* dsv = nullptr;

    void Save(ID3D11DeviceContext* c) {
        c->RSGetScissorRects(&scissorN, scissor);
        c->RSGetViewports(&viewportN, viewport);
        c->RSGetState(&rs);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&ds, &stencilRef);
        c->PSGetShaderResources(0, kSrv, psSrv);
        c->PSGetSamplers(0, kSamp, psSamp);
        c->PSGetShader(&ps, psInst, &psInstN);
        c->VSGetShader(&vs, vsInst, &vsInstN);
        c->GSGetShader(&gs, gsInst, &gsInstN);
        c->VSGetConstantBuffers(0, kCb, vsCb);
        c->PSGetConstantBuffers(0, kCb, psCb);
        c->IAGetPrimitiveTopology(&topo);
        c->IAGetIndexBuffer(&ib, &ibFmt, &ibOff);
        c->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOff);
        c->IAGetInputLayout(&layout);
        c->OMGetRenderTargets(1, &rtv, &dsv);
    }
    void Restore(ID3D11DeviceContext* c) {
        c->RSSetScissorRects(scissorN, scissor);
        c->RSSetViewports(viewportN, viewport);
        c->RSSetState(rs); if (rs) rs->Release();
        c->OMSetBlendState(blend, blendFactor, sampleMask); if (blend) blend->Release();
        c->OMSetDepthStencilState(ds, stencilRef); if (ds) ds->Release();
        c->PSSetShaderResources(0, kSrv, psSrv); for (auto* v : psSrv) if (v) v->Release();
        c->PSSetSamplers(0, kSamp, psSamp); for (auto* v : psSamp) if (v) v->Release();
        c->PSSetShader(ps, psInst, psInstN); if (ps) ps->Release();
        for (UINT i = 0; i < psInstN; ++i) if (psInst[i]) psInst[i]->Release();
        c->VSSetShader(vs, vsInst, vsInstN); if (vs) vs->Release();
        for (UINT i = 0; i < vsInstN; ++i) if (vsInst[i]) vsInst[i]->Release();
        c->GSSetShader(gs, gsInst, gsInstN); if (gs) gs->Release();
        for (UINT i = 0; i < gsInstN; ++i) if (gsInst[i]) gsInst[i]->Release();
        c->VSSetConstantBuffers(0, kCb, vsCb); for (auto* v : vsCb) if (v) v->Release();
        c->PSSetConstantBuffers(0, kCb, psCb); for (auto* v : psCb) if (v) v->Release();
        c->IASetPrimitiveTopology(topo);
        c->IASetIndexBuffer(ib, ibFmt, ibOff); if (ib) ib->Release();
        c->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOff); if (vb) vb->Release();
        c->IASetInputLayout(layout); if (layout) layout->Release();
        c->OMSetRenderTargets(1, &rtv, dsv); if (rtv) rtv->Release(); if (dsv) dsv->Release();
    }
};
} // namespace

const std::uint32_t* D3D11Renderer::RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                                   int w, int h, int samples,
                                                   float clearR, float clearG, float clearB, float clearA,
                                                   const GameObject* ignore) {
    if (!Available() || w < 1 || h < 1) return nullptr;
    Impl* p = m_impl;
    if (p->shared && !p->stateSaved) {   // on the host's device: leave its pipeline state as we found it
        D3D11StateBackup* backup = new D3D11StateBackup();
        backup->Save(p->ctx);
        p->stateSaved = true;
        const std::uint32_t* r = RenderToPixels(scene, vp, eye, w, h, samples, clearR, clearG, clearB, clearA, ignore);
        p->stateSaved = false;
        backup->Restore(p->ctx);
        delete backup;
        return r;
    }
    if (!p->EnsureTargets(w, h, samples)) return nullptr;
    if (!p->rtv || !p->dsv || !p->ctx) return nullptr;   // never bind a null target
    ID3D11DeviceContext* c = p->ctx;

    // Advance the mesh-VB cache clock and evict buffers unused for ~5s of frames
    // (deleted objects, replaced meshes), so GPU memory doesn't grow unbounded.
    ++p->frame;
    if ((p->frame % 300u) == 0u) {
        for (auto it = p->meshVB.begin(); it != p->meshVB.end();) {
            if (p->frame - it->second.lastUse > 300u) {
                if (it->second.vbuf) it->second.vbuf->Release();
                it = p->meshVB.erase(it);
            } else ++it;
        }
        for (auto it = p->skinVB.begin(); it != p->skinVB.end();) {
            if (p->frame - it->second.lastUse > 300u) {
                if (it->second.buf) it->second.buf->Release();
                it = p->skinVB.erase(it);
            } else ++it;
        }
    }

    // ---- Shadow-map depth pre-pass (directional cast shadows) -----------------
    // Render scene depth from the sun into a depth texture the main pass samples
    // (PCF) for real cast shadows. Opt-in via ShadowsEnabled().
    Mat4 lightVP; float shadowTexel = 0.0f; bool shadowOn = false;
    if (ShadowsEnabled() && p->vsDepth &&
        ComputeDirectionalShadowVP(scene, vp, eye, lightVP, shadowTexel)) {
        int ssize = ShadowMapResolution(); if (ssize < 256) ssize = 256; if (ssize > 4096) ssize = 4096;
        if (p->EnsureShadow(ssize)) {
            ID3D11ShaderResourceView* nullsrv = nullptr;
            c->PSSetShaderResources(4, 1, &nullsrv);   // never bound as SRV + DSV at once
            D3D11_VIEWPORT sv; std::memset(&sv, 0, sizeof(sv));
            sv.Width = (FLOAT)ssize; sv.Height = (FLOAT)ssize; sv.MaxDepth = 1.0f;
            c->RSSetViewports(1, &sv);
            c->OMSetRenderTargets(0, nullptr, p->shadowDsv);
            c->ClearDepthStencilView(p->shadowDsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
            c->IASetInputLayout(p->layoutDepth);
            c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            c->VSSetShader(p->vsDepth, nullptr, 0);
            c->PSSetShader(nullptr, nullptr, 0);        // depth only — no pixel shader
            c->VSSetConstantBuffers(0, 1, &p->cb);
            c->RSSetState(p->raster);
            c->OMSetDepthStencilState(p->depth, 0);
            const float bf0[4] = {0, 0, 0, 0};
            c->OMSetBlendState(nullptr, bf0, 0xffffffff);
            const Frustum lightFr = Frustum::FromVP(lightVP);   // only casters inside the shadow box
            for (const auto& up : scene.Objects()) {
                GameObject* go = up.get();
                if (!go || !go->active || (ignore && go->IsSelfOrDescendantOf(ignore) && !go->firstPersonViewmodel)) continue;
                auto* mr = go->GetComponent<MeshRenderer>();
                if (!mr || mr->wireframe || !mr->enabled) continue;
                if (mr->color.a < 0.999f) continue;     // transparent meshes don't cast
                const Mesh& mesh = mr->mesh;
                const auto& V = mesh.vertices; const auto& T = mesh.triangles;
                if (V.empty() || T.size() < 3) continue;
                if (CulledByFrustum(lightFr, go, mr)) continue;
                Mat4 dmvp = lightVP * go->transform->LocalToWorldMatrix();
                CB dcb; std::memset(&dcb, 0, sizeof(dcb));
                std::memcpy(dcb.mvp, dmvp.m, sizeof(dcb.mvp));
                D3D11_MAPPED_SUBRESOURCE dms;
                if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &dms))) {
                    std::memcpy(dms.pData, &dcb, sizeof(dcb)); c->Unmap(p->cb, 0);
                }
                // Static meshes: draw positions straight from the cached interleaved
                // buffer (stride 14). Per-frame-deformed meshes stream position-only.
                const bool deformed = go->GetComponent<Character>() != nullptr;
                const auto* skinD = go->GetComponent<SkinnedMesh>();
                if (!deformed && p->CanGpuSkin(skinD) && p->vsDepthSkin && p->layoutDepthSkin) {
                    Impl::MeshVB* mv = p->EnsureMeshVB(skinD->bind, 0);
                    Impl::SkinVB* sv = mv ? p->EnsureSkinVB(*skinD) : nullptr;
                    if (!mv || !sv || sv->count != mv->vertCount || !p->UploadBones(*skinD)) continue;
                    c->IASetInputLayout(p->layoutDepthSkin);
                    c->VSSetShader(p->vsDepthSkin, nullptr, 0);
                    c->VSSetConstantBuffers(1, 1, &p->bonesCB);
                    ID3D11Buffer* bufs[2] = {mv->vbuf, sv->buf};
                    UINT strides[2] = {14 * sizeof(float), 8 * sizeof(float)}, offs[2] = {0, 0};
                    c->IASetVertexBuffers(0, 2, bufs, strides, offs);
                    c->Draw((UINT)mv->vertCount, 0);
                    c->IASetInputLayout(p->layoutDepth);
                    c->VSSetShader(p->vsDepth, nullptr, 0);
                    continue;
                }
                if (!deformed) {
                    const auto* skin = skinD;
                    Impl::MeshVB* mv = p->EnsureMeshVB(mesh, skin ? skin->version : 0);
                    if (!mv) continue;
                    UINT ds = 14 * sizeof(float), doff = 0;
                    c->IASetVertexBuffers(0, 1, &mv->vbuf, &ds, &doff);
                    c->Draw((UINT)mv->vertCount, 0);
                    continue;
                }
                const int nv = (int)V.size();
                p->verts.clear(); p->verts.reserve(T.size() * 3);
                for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
                    int a = T[i], b = T[i + 1], cc = T[i + 2];
                    if (a < 0 || b < 0 || cc < 0 || a >= nv || b >= nv || cc >= nv) continue;
                    const Vec3& pa = V[a]; const Vec3& pb = V[b]; const Vec3& pc = V[cc];
                    p->verts.push_back(pa.x); p->verts.push_back(pa.y); p->verts.push_back(pa.z);
                    p->verts.push_back(pb.x); p->verts.push_back(pb.y); p->verts.push_back(pb.z);
                    p->verts.push_back(pc.x); p->verts.push_back(pc.y); p->verts.push_back(pc.z);
                }
                const int dvc = (int)(p->verts.size() / 3);
                if (dvc == 0) continue;
                if (dvc > p->vbDepthCap) {
                    if (p->vbDepth) { p->vbDepth->Release(); p->vbDepth = nullptr; p->vbDepthCap = 0; }
                    const int cap = dvc + 1024;
                    D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
                    bd.ByteWidth = (UINT)(cap * 3 * sizeof(float)); bd.Usage = D3D11_USAGE_DYNAMIC;
                    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    if (FAILED(p->dev->CreateBuffer(&bd, nullptr, &p->vbDepth))) continue;
                    p->vbDepthCap = cap;
                }
                if (!p->vbDepth) continue;
                if (SUCCEEDED(c->Map(p->vbDepth, 0, D3D11_MAP_WRITE_DISCARD, 0, &dms))) {
                    std::memcpy(dms.pData, p->verts.data(), p->verts.size() * sizeof(float));
                    c->Unmap(p->vbDepth, 0);
                }
                UINT ds = 3 * sizeof(float), doff = 0;
                c->IASetVertexBuffers(0, 1, &p->vbDepth, &ds, &doff);
                c->Draw((UINT)dvc, 0);
            }
            shadowOn = true;
        }
    }

    D3D11_VIEWPORT vpd; std::memset(&vpd, 0, sizeof(vpd));
    vpd.Width = (FLOAT)w; vpd.Height = (FLOAT)h; vpd.MaxDepth = 1.0f;
    c->RSSetViewports(1, &vpd);
    c->OMSetRenderTargets(1, &p->rtv, p->dsv);
    const float clear[4] = {clearR, clearG, clearB, clearA};
    c->ClearRenderTargetView(p->rtv, clear);
    c->ClearDepthStencilView(p->dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

    c->IASetInputLayout(p->layout);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(p->vs, nullptr, 0);
    c->PSSetShader(p->ps, nullptr, 0);
    c->VSSetConstantBuffers(0, 1, &p->cb);
    c->PSSetConstantBuffers(0, 1, &p->cb);
    c->PSSetSamplers(0, 1, &p->samp);
    c->RSSetState(p->raster);
    c->OMSetDepthStencilState(p->depth, 0);

    Vec3 toLight = (SceneLight::Direction() * -1.0f).Normalized();
    // Gather all scene lights once per frame (directional + point + spot) into the
    // packed cbuffer array so every light shades on the GPU, not just the sun.
    float frameLights[256]; std::memset(frameLights, 0, sizeof(frameLights));
    int frameLightCount = 0;
    {
        const auto& LS = SceneLights::List();
        int n = (int)LS.size(); if (n > 16) n = 16; frameLightCount = n;
        for (int i = 0; i < n; ++i) {
            const LightSample& s = LS[i]; Vec3 d = s.dir.Normalized(); int b = i * 16;
            frameLights[b+0] = d.x; frameLights[b+1] = d.y; frameLights[b+2] = d.z; frameLights[b+3] = (float)s.type;
            frameLights[b+4] = s.pos.x; frameLights[b+5] = s.pos.y; frameLights[b+6] = s.pos.z; frameLights[b+7] = s.range;
            frameLights[b+8] = s.color.x; frameLights[b+9] = s.color.y; frameLights[b+10] = s.color.z; frameLights[b+11] = s.cosOuter;
            frameLights[b+12] = s.cosInner;
        }
    }
    Vec3 d3amb = SceneLights::AmbientColor();
    if (d3amb.x + d3amb.y + d3amb.z < 1e-4f) d3amb = Vec3{0.35f, 0.36f, 0.40f};
    // Refresh the env sky (for reflective/metal materials) from the scene, since the
    // software RenderMeshes that normally does this may not run on the GPU path.
    {
        const auto& rs = scene.renderSettings;
        EnvSky().enabled = rs.skybox;
        EnvSky().top     = {rs.skyTop.r, rs.skyTop.g, rs.skyTop.b};
        EnvSky().horizon = {rs.skyHorizon.r, rs.skyHorizon.g, rs.skyHorizon.b};
        EnvSky().bottom  = {rs.skyBottom.r, rs.skyBottom.g, rs.skyBottom.b};
    }

    // Skip what the camera can't see.
    const Frustum viewFr = Frustum::FromVP(vp);
    int drawnN = 0, culledN = 0;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || (ignore && go->IsSelfOrDescendantOf(ignore) && !go->firstPersonViewmodel)) continue;
        if (!(RenderCullingMask() & (1 << (go->layer & 31)))) continue;   // camera layer cull
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled) continue;
        const Mesh& mesh = mr->mesh;
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        if (V.empty() || T.size() < 3) continue;
        if (CulledByFrustum(viewFr, go, mr)) { ++culledN; continue; }
        ++drawnN;
        ID3D11ShaderResourceView* srv = p->TextureFor(mr->texture);

        // Geometry source: static meshes draw from the per-mesh cached GPU buffer
        // (expanded + uploaded once — see Impl::EnsureMeshVB); meshes deformed on
        // the CPU every frame by a Character stream through `vb`. SkinnedMesh is
        // cached too, re-uploaded only when its version moves.
        const bool deformed = go->GetComponent<Character>() != nullptr;
        const auto* skin = go->GetComponent<SkinnedMesh>();
        Impl::MeshVB* mvb = nullptr;
        Impl::SkinVB* svb = nullptr;
        int vcount = 0;
        D3D11_MAPPED_SUBRESOURCE ms;
        if (!deformed && p->CanGpuSkin(skin)) {
            // GPU skinning: the bind pose is uploaded once; only the bones change.
            mvb = p->EnsureMeshVB(skin->bind, 0);
            svb = mvb ? p->EnsureSkinVB(*skin) : nullptr;
            if (!mvb) continue;
            if (!svb || svb->count != mvb->vertCount || !p->UploadBones(*skin)) svb = nullptr;
            vcount = mvb->vertCount;
            if (!svb) mvb = p->EnsureMeshVB(mesh, skin->version);   // fall back: draw whatever mesh holds
            if (!mvb) continue;
        } else if (!deformed) {
            mvb = p->EnsureMeshVB(mesh, skin ? skin->version : 0);
            if (!mvb) continue;                    // no drawable triangles
            vcount = mvb->vertCount;
        } else {
            p->BuildExpandedVerts(mesh);
            vcount = (int)(p->verts.size() / 14);
            if (vcount == 0) continue;
            // Grow / upload the dynamic vertex buffer.
            if (vcount > p->vbCap) {
                if (p->vb) { p->vb->Release(); p->vb = nullptr; p->vbCap = 0; }
                const int cap = vcount + 1024;
                D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
                bd.ByteWidth = (UINT)(cap * 14 * sizeof(float)); bd.Usage = D3D11_USAGE_DYNAMIC;
                bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(p->dev->CreateBuffer(&bd, nullptr, &p->vb))) continue;
                p->vbCap = cap;
            }
            if (!p->vb) continue;  // never Map a null buffer (a prior CreateBuffer may have failed)
            if (SUCCEEDED(c->Map(p->vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, p->verts.data(), p->verts.size() * sizeof(float));
                c->Unmap(p->vb, 0);
            }
        }

        Mat4 model = go->transform->LocalToWorldMatrix();
        Mat4 mvp = vp * model;
        bool unlit = mr->unlit || mr->shader == MeshRenderer::Shader::Unlit;
        CB cb; std::memset(&cb, 0, sizeof(cb));
        std::memcpy(cb.mvp, mvp.m, sizeof(cb.mvp));
        std::memcpy(cb.model, model.m, sizeof(cb.model));
        cb.color[0] = mr->color.r; cb.color[1] = mr->color.g; cb.color[2] = mr->color.b;
        cb.specular = unlit ? 0.0f : mr->specular;
        cb.emissive[0] = mr->emissive.r; cb.emissive[1] = mr->emissive.g; cb.emissive[2] = mr->emissive.b;
        cb.shininess = mr->shininess;
        cb.lightDir[0] = toLight.x; cb.lightDir[1] = toLight.y; cb.lightDir[2] = toLight.z;
        cb.unlit = unlit ? 1.0f : 0.0f;
        cb.lightColor[0] = cb.lightColor[1] = cb.lightColor[2] = 0.85f;
        cb.useTex = srv ? 1.0f : 0.0f;
        cb.ambient[0] = d3amb.x; cb.ambient[1] = d3amb.y; cb.ambient[2] = d3amb.z;
        cb.eye[0] = eye.x; cb.eye[1] = eye.y; cb.eye[2] = eye.z;
        ID3D11ShaderResourceView* nsrv = mr->normalMap.empty() ? nullptr : p->TextureFor(mr->normalMap);
        cb.hasNormal = nsrv ? 1.0f : 0.0f;
        cb.normalStrength = mr->normalStrength;
        ID3D11ShaderResourceView* ssrv = mr->specularMap.empty() ? nullptr : p->TextureFor(mr->specularMap);
        ID3D11ShaderResourceView* aosrv = mr->aoMap.empty() ? nullptr : p->TextureFor(mr->aoMap);
        bool d3unlit = mr->unlit || mr->shader == MeshRenderer::Shader::Unlit;
        cb.pbr[0] = d3unlit ? 0.0f : mr->metallic;       // metallic
        cb.pbr[1] = d3unlit ? 0.0f : mr->reflectivity;   // reflectivity
        cb.pbr[2] = ssrv ? 1.0f : 0.0f;                  // hasSpecMap
        cb.pbr[3] = aosrv ? 1.0f : 0.0f;                 // hasAo
        cb.pbr2[0] = mr->aoStrength;                      // aoStrength
        cb.pbr2[1] = EnvSky().enabled ? 1.0f : 0.0f;     // envOn
        cb.pbr2[2] = mr->texOffset.x; cb.pbr2[3] = mr->texOffset.y;
        { const EnvSkyData& sky = EnvSky();
          cb.sky[0] = sky.top.x;     cb.sky[1] = sky.top.y;     cb.sky[2] = sky.top.z;
          cb.sky[4] = sky.horizon.x; cb.sky[5] = sky.horizon.y; cb.sky[6] = sky.horizon.z;
          cb.sky[8] = sky.bottom.x;  cb.sky[9] = sky.bottom.y;  cb.sky[10] = sky.bottom.z; }
        cb.tiling[0] = srv ? mr->tiling.x : 1.0f; cb.tiling[1] = srv ? mr->tiling.y : 1.0f;
        cb.shaderMode = (float)(int)mr->shader;
        cb.toonBands = (float)mr->toonBands;
        cb.rimStr = mr->rimStrength;
        cb.rimColor[0] = mr->rimColor.r; cb.rimColor[1] = mr->rimColor.g; cb.rimColor[2] = mr->rimColor.b;
        cb.gradTop[0] = mr->gradientTop.r; cb.gradTop[1] = mr->gradientTop.g; cb.gradTop[2] = mr->gradientTop.b;
        cb.gradBot[0] = mr->gradientBottom.r; cb.gradBot[1] = mr->gradientBottom.g; cb.gradBot[2] = mr->gradientBottom.b;
        cb.shadowAlpha = -1.0f;   // lit pass (>=0 would draw the flat shadow — must be off here)
        cb.alpha = mr->color.a;   // material opacity (1 = opaque); < 1 alpha-blends (water/glass)
        cb.lightCount[0] = (float)frameLightCount;
        std::memcpy(cb.lights, frameLights, sizeof(frameLights));
        {
            const auto& rs = scene.renderSettings;
            bool fogOn = rs.fog && rs.fogEnd > rs.fogStart;
            cb.fog[0] = fogOn ? 1.0f : 0.0f; cb.fog[1] = rs.fogStart; cb.fog[2] = rs.fogEnd; cb.fog[3] = 0.0f;
            // fogCol.w carries the filmic (linear + ACES + gamma) pipeline flag.
            cb.fogCol[0] = rs.fogColor.r; cb.fogCol[1] = rs.fogColor.g; cb.fogCol[2] = rs.fogColor.b;
            cb.fogCol[3] = rs.tonemap ? 1.0f : 0.0f;
        }
        std::memcpy(cb.lightVP, lightVP.m, sizeof(cb.lightVP));
        cb.shadowP[0] = shadowOn ? 1.0f : 0.0f; cb.shadowP[1] = shadowTexel;
        cb.shadowP[2] = (float)p->shadowSize; cb.shadowP[3] = 0.0f;
        if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, &cb, sizeof(cb)); c->Unmap(p->cb, 0);
        }
        if (shadowOn && p->shadowSrv) {
            c->PSSetShaderResources(4, 1, &p->shadowSrv);   // shadow map on t4
            c->PSSetSamplers(1, 1, &p->sampShadow);         // clamp sampler on s1
        }
        if (srv) c->PSSetShaderResources(0, 1, &srv);
        if (nsrv) c->PSSetShaderResources(1, 1, &nsrv);   // normal/bump map on t1
        if (ssrv) c->PSSetShaderResources(2, 1, &ssrv);   // gloss/specular map on t2
        if (aosrv) c->PSSetShaderResources(3, 1, &aosrv); // ambient-occlusion map on t3
        // Per-material texture filter (Pixel = nearest, Smooth = aniso/linear).
        ID3D11SamplerState* useSamp = (mr->texFilter == MeshRenderer::TexFilter::Pixel && p->sampPoint) ? p->sampPoint : p->samp;
        c->PSSetSamplers(0, 1, &useSamp);

        UINT stride = 14 * sizeof(float), offset = 0;
        ID3D11Buffer* vbUse = mvb ? mvb->vbuf : p->vb;   // cached static buffer or the stream buffer
        c->IASetVertexBuffers(0, 1, &vbUse, &stride, &offset);
        if (svb) {   // skinned on the GPU: second stream + bones, skinning vertex shader
            UINT st1 = 8 * sizeof(float), of1 = 0;
            c->IASetVertexBuffers(1, 1, &svb->buf, &st1, &of1);
            c->VSSetConstantBuffers(1, 1, &p->bonesCB);
            c->IASetInputLayout(p->layoutSkin);
            c->VSSetShader(p->vsSkin, nullptr, 0);
        }
        // Transparency: a material alpha < 1 (water, glass) alpha-blends over the
        // scene and doesn't write depth, so geometry behind shows through. Reuses the
        // ground-shadow blend + no-depth-write states. Draw transparent meshes after
        // opaque ones (objects draw in scene order).
        bool transparent = mr->color.a < 0.999f && p->blend && p->depthNoWrite;
        const float blendF[4] = {0, 0, 0, 0};
        if (transparent) {
            c->OMSetBlendState(p->blend, blendF, 0xffffffff);
            c->OMSetDepthStencilState(p->depthNoWrite, 0);
        }
        c->Draw((UINT)vcount, 0);
        if (transparent) {
            c->OMSetBlendState(nullptr, blendF, 0xffffffff);
            c->OMSetDepthStencilState(p->depth, 0);
        }

        // Ground contact shadow: re-draw the SAME geometry flattened onto the ground
        // plane as flat translucent black, so objects sit on the ground (not floating).
        if (mr->groundShadow && mr->groundShadowStrength > 0.001f && p->blend && p->depthNoWrite) {
            Mat4 sModel = Mat4::PlanarShadow(mr->groundShadowY + 0.01f, SceneLight::Direction()) * model;
            Mat4 sMvp = vp * sModel;
            std::memcpy(cb.mvp, sMvp.m, sizeof(cb.mvp));
            cb.shadowAlpha = mr->groundShadowStrength > 1.0f ? 1.0f : mr->groundShadowStrength;
            if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, &cb, sizeof(cb)); c->Unmap(p->cb, 0);
            }
            const float bf[4] = {0, 0, 0, 0};
            c->OMSetBlendState(p->blend, bf, 0xffffffff);
            c->OMSetDepthStencilState(p->depthNoWrite, 0);
            c->Draw((UINT)vcount, 0);
            c->OMSetBlendState(nullptr, bf, 0xffffffff);
            c->OMSetDepthStencilState(p->depth, 0);
        }
        if (svb) { c->IASetInputLayout(p->layout); c->VSSetShader(p->vs, nullptr, 0); }
    }
    LastCullCounters().drawn = drawnN; LastCullCounters().culled = culledN;

    // RenderToTexture: resolve straight into the host's texture and stop here.
    if (p->directDst) {
        if (p->samples > 1) c->ResolveSubresource(p->directDst, 0, p->colorTex, 0, p->fmt);
        else                c->CopyResource(p->directDst, p->colorTex);
        if (shadowOn) { ID3D11ShaderResourceView* nullsrv = nullptr; c->PSSetShaderResources(4, 1, &nullsrv); }
        return reinterpret_cast<const std::uint32_t*>(p->directDst);   // non-null = success (no pixels)
    }
    // Resolve MSAA (or copy) -> staging -> read back. D3D textures are top-left, so
    // no row flip is needed (unlike GL).
    if (p->samples > 1) c->ResolveSubresource(p->resolveTex, 0, p->colorTex, 0, p->fmt);
    else                c->CopyResource(p->resolveTex, p->colorTex);
    ID3D11Texture2D* readFrom = p->staging;
    if (p->stream >= 0) {
        Impl::Pipe& pp = p->pipes[p->stream];
        const auto now = std::chrono::steady_clock::now();
        const bool fresh = now - pp.last < std::chrono::milliseconds(250);
        pp.last = now;
        if (pp.w != w || pp.h != h || !pp.st[0] || !pp.st[1]) {
            for (auto*& t : pp.st) if (t) { t->Release(); t = nullptr; }
            pp.w = pp.h = 0; pp.filled[0] = pp.filled[1] = false; pp.idx = 0;
            D3D11_TEXTURE2D_DESC ts; std::memset(&ts, 0, sizeof(ts));
            ts.Width = w; ts.Height = h; ts.MipLevels = 1; ts.ArraySize = 1;
            ts.Format = DXGI_FORMAT_R8G8B8A8_UNORM; ts.SampleDesc.Count = 1;
            ts.Usage = D3D11_USAGE_STAGING; ts.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (SUCCEEDED(p->dev->CreateTexture2D(&ts, nullptr, &pp.st[0])) &&
                SUCCEEDED(p->dev->CreateTexture2D(&ts, nullptr, &pp.st[1]))) { pp.w = w; pp.h = h; }
        }
        if (pp.w == w && pp.h == h) {
            const int cur = pp.idx, prev = pp.idx ^ 1;
            c->CopyResource(pp.st[cur], p->resolveTex);
            pp.filled[cur] = true;
            readFrom = (fresh && pp.filled[prev]) ? pp.st[prev] : pp.st[cur];
            pp.idx = prev;
        } else c->CopyResource(p->staging, p->resolveTex);   // couldn't make the ring: synchronous
    } else {
        c->CopyResource(p->staging, p->resolveTex);
    }

    p->pixels.assign((std::size_t)w * h, 0u);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(c->Map(readFrom, 0, D3D11_MAP_READ, 0, &ms))) return nullptr;
    const std::uint8_t* src = (const std::uint8_t*)ms.pData;
    for (int y = 0; y < h; ++y)
        std::memcpy(p->pixels.data() + (std::size_t)y * w, src + (std::size_t)y * ms.RowPitch, (std::size_t)w * 4);
    c->Unmap(readFrom, 0);
    // Release the shadow map from t4 so next frame's depth pass can bind it as a DSV
    // without a read/write hazard warning.
    if (shadowOn) { ID3D11ShaderResourceView* nullsrv = nullptr; c->PSSetShaderResources(4, 1, &nullsrv); }
    return p->pixels.data();
}

void D3D11Renderer::SetReadbackStream(int id) { if (m_impl) m_impl->stream = id; }

void* D3D11Renderer::BoundRenderTargetTexture() {
    if (!m_impl || !m_impl->ctx) return nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    m_impl->ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return nullptr;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    rtv->Release();
    if (!res) return nullptr;
    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    res->Release();
    return tex;
}
void D3D11Renderer::ReleaseCom(void* unknown) { if (unknown) static_cast<IUnknown*>(unknown)->Release(); }


bool D3D11Renderer::RenderToTexture(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                    int w, int h, int samples,
                                    float clearR, float clearG, float clearB, float clearA,
                                    void* dstTexture, const GameObject* ignore) {
    if (!Available() || !dstTexture) return false;
    Impl* p = m_impl;
    D3D11_TEXTURE2D_DESC dd; static_cast<ID3D11Texture2D*>(dstTexture)->GetDesc(&dd);
    if ((int)dd.Width != w || (int)dd.Height != h || dd.SampleDesc.Count != 1) return false;
    if (dd.Format != DXGI_FORMAT_R8G8B8A8_UNORM && dd.Format != DXGI_FORMAT_B8G8R8A8_UNORM) return false;
    auto* backup = new D3D11StateBackup();   // (large: keep it off the stack)
    backup->Save(p->ctx);
    p->stateSaved = true;
    p->directDst = static_cast<ID3D11Texture2D*>(dstTexture);
    p->wantFmt = dd.Format;
    const bool ok = RenderToPixels(scene, vp, eye, w, h, samples, clearR, clearG, clearB, clearA, ignore) != nullptr;
    p->directDst = nullptr;
    p->wantFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    p->stateSaved = false;
    backup->Restore(p->ctx);
    delete backup;
    return ok;
}

void D3D11Renderer::Destroy() {
    if (!m_impl) return;
    Impl* p = m_impl;
    p->DestroyTargets();
    for (auto& t : p->parked) p->ReleaseSet(t);
    p->parked.clear();
    for (auto& kv : p->pipes) for (auto* t : kv.second.st) if (t) t->Release();
    p->pipes.clear();
    for (auto& kv : p->texCache) if (kv.second) kv.second->Release();
    p->texCache.clear();
    for (auto& kv : p->meshVB) if (kv.second.vbuf) kv.second.vbuf->Release();
    p->meshVB.clear();
    if (p->shadowSrv) p->shadowSrv->Release();
    if (p->shadowDsv) p->shadowDsv->Release();
    if (p->shadowTex) p->shadowTex->Release();
    if (p->vbDepth)   p->vbDepth->Release();
    if (p->layoutDepth) p->layoutDepth->Release();
    if (p->vsDepth)   p->vsDepth->Release();
    for (auto& kv : p->skinVB) if (kv.second.buf) kv.second.buf->Release();
    p->skinVB.clear();
    if (p->vsSkin) p->vsSkin->Release();
    if (p->layoutSkin) p->layoutSkin->Release();
    if (p->vsDepthSkin) p->vsDepthSkin->Release();
    if (p->layoutDepthSkin) p->layoutDepthSkin->Release();
    if (p->bonesCB) p->bonesCB->Release();
    if (p->sampShadow) p->sampShadow->Release();
    if (p->vb)     p->vb->Release();
    if (p->samp)   p->samp->Release();
    if (p->sampPoint) p->sampPoint->Release();
    if (p->blend)  p->blend->Release();
    if (p->depthNoWrite) p->depthNoWrite->Release();
    if (p->depth)  p->depth->Release();
    if (p->raster) p->raster->Release();
    if (p->cb)     p->cb->Release();
    if (p->layout) p->layout->Release();
    if (p->ps)     p->ps->Release();
    if (p->vs)     p->vs->Release();
    if (p->ctx)    p->ctx->Release();
    if (p->dev)    p->dev->Release();
    delete p;
    m_impl = nullptr;
}

D3D11Renderer::~D3D11Renderer() { Destroy(); }

} // namespace okay

#endif // _WIN32
