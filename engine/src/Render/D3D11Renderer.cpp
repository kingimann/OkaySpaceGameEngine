// Direct3D 11 scene renderer (Windows only). Guarded so the Linux build / headless
// tests never reference Direct3D. Mirrors GLRenderer: offscreen MSAA render, resolve,
// read back RGBA8. See D3D11Renderer.hpp.
#if defined(_WIN32)

#include "okay/Render/D3D11Renderer.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture: shared texture cache
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Log.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
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
    "  float bias=0.0015; float t=1.0/max(uShadow.z,1.0); float s=0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.5,-0.5)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.5,-0.5)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2(-0.5, 0.5)*t).r)?1.0:0.0;\n"
    "  s += (pz-bias <= uShadowTex.Sample(uShadowSamp, uv+float2( 0.5, 0.5)*t).r)?1.0:0.0;\n"
    "  return s*0.25;\n"
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
    "  if (uUnlit > 0.5) return float4(base + uEmissive, uAlpha);\n"
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
    "  float3 col = diff + (spec * gloss) * f0 + rim + uEmissive;\n"
    "  float reflAmt = max(uPbr.y, metal);\n"                     // env reflection of the sky gradient
    "  float reflK = reflAmt * gloss;\n"
    "  if (reflK > 0.0 && uPbr2.y > 0.5) {\n"
    "    float ndv = max(dot(N, Vv), 0.0);\n"
    "    float3 R = reflect(-Vv, N); float ry = clamp(R.y, -1.0, 1.0);\n"
    "    float3 env = lerp(uSky[1].xyz, ry >= 0.0 ? uSky[0].xyz : uSky[2].xyz, abs(ry));\n"
    "    float fr2 = 1.0 - ndv; fr2 = fr2*fr2*fr2*fr2*fr2;\n"     // Schlick
    "    float kk = reflK + (1.0 - reflK) * fr2;\n"
    "    col = col * (1.0 - kk) + env * f0 * kk;\n"
    "  }\n"
    "  if (uFog.x > 0.5) {\n"                                    // distance fog
    "    float fd = length(uEye - i.world);\n"
    "    float ff = saturate((fd - uFog.y) / max(uFog.z - uFog.y, 1e-3));\n"
    "    col = lerp(col, uFogCol.xyz, ff);\n"
    "  }\n"
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
    float fogCol[4];           // xyz = fog colour
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
    std::unordered_map<std::string, ID3D11ShaderResourceView*> texCache;
    std::vector<float> verts;
    std::vector<std::uint32_t> pixels;

    ID3D11ShaderResourceView* TextureFor(const std::string& name);
    bool EnsureTargets(int W, int H, int S);
    void DestroyTargets();
};

bool D3D11Renderer::Init() {
    if (m_impl) return true;
    m_impl = new Impl();
    Impl* p = m_impl;
    UINT flags = 0;
    D3D_FEATURE_LEVEL got;
    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, want, 3,
                                 D3D11_SDK_VERSION, &p->dev, &got, &p->ctx))) {
        Log::Error("[d3d11] device creation failed");
        Destroy(); return false;
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

    return p->dev && p->vs && p->ps && p->layout && p->cb && p->raster && p->depth && p->samp;
}

bool D3D11Renderer::Available() const { return m_impl && m_impl->dev && m_impl->vs; }

void D3D11Renderer::Impl::DestroyTargets() {
    if (rtv)        { rtv->Release(); rtv = nullptr; }
    if (dsv)        { dsv->Release(); dsv = nullptr; }
    if (colorTex)   { colorTex->Release(); colorTex = nullptr; }
    if (depthTex)   { depthTex->Release(); depthTex = nullptr; }
    if (resolveTex) { resolveTex->Release(); resolveTex = nullptr; }
    if (staging)    { staging->Release(); staging = nullptr; }
    w = h = samples = 0;
}

bool D3D11Renderer::Impl::EnsureTargets(int W, int H, int S) {
    if (colorTex && w == W && h == H && samples == S) return true;
    DestroyTargets();
    w = W; h = H;
    // Clamp the requested MSAA to what the device actually supports for BOTH the color
    // and depth formats. Creating a multisampled texture with an unsupported count is a
    // common hard-failure (and on some drivers a crash) on integrated GPUs / RDP / VMs,
    // so drop to the next lower supported count, down to 1 (no MSAA).
    int want = S < 1 ? 1 : S;
    samples = 1;
    for (int s = want; s >= 1; --s) {
        UINT qC = 0, qD = 0;
        if (SUCCEEDED(dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, (UINT)s, &qC)) && qC > 0 &&
            SUCCEEDED(dev->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, (UINT)s, &qD)) && qD > 0) {
            samples = s; break;
        }
    }

    D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = samples;
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

const std::uint32_t* D3D11Renderer::RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                                   int w, int h, int samples,
                                                   float clearR, float clearG, float clearB, float clearA,
                                                   const GameObject* ignore) {
    if (!Available() || w < 1 || h < 1) return nullptr;
    Impl* p = m_impl;
    if (!p->EnsureTargets(w, h, samples)) return nullptr;
    ID3D11DeviceContext* c = p->ctx;

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
            for (const auto& up : scene.Objects()) {
                GameObject* go = up.get();
                if (!go || !go->active || go == ignore) continue;
                auto* mr = go->GetComponent<MeshRenderer>();
                if (!mr || mr->wireframe || !mr->enabled) continue;
                if (mr->color.a < 0.999f) continue;     // transparent meshes don't cast
                const Mesh& mesh = mr->mesh;
                const auto& V = mesh.vertices; const auto& T = mesh.triangles;
                if (V.empty() || T.size() < 3) continue;
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
                D3D11_MAPPED_SUBRESOURCE dms;
                if (SUCCEEDED(c->Map(p->vbDepth, 0, D3D11_MAP_WRITE_DISCARD, 0, &dms))) {
                    std::memcpy(dms.pData, p->verts.data(), p->verts.size() * sizeof(float));
                    c->Unmap(p->vbDepth, 0);
                }
                Mat4 dmvp = lightVP * go->transform->LocalToWorldMatrix();
                CB dcb; std::memset(&dcb, 0, sizeof(dcb));
                std::memcpy(dcb.mvp, dmvp.m, sizeof(dcb.mvp));
                if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &dms))) {
                    std::memcpy(dms.pData, &dcb, sizeof(dcb)); c->Unmap(p->cb, 0);
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

    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || go == ignore) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled) continue;
        const Mesh& mesh = mr->mesh;
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        if (V.empty() || T.size() < 3) continue;
        const bool hasN = mesh.HasNormals();
        // Texture whenever the material has one; box-project UVs when the mesh has none
        // (matches the software renderer, so UV-less textured models still texture here).
        const bool hasUV = !mesh.uvs.empty() && mesh.uvs.size() == V.size();
        // Per-triangle face colors (skin/shirt/etc. on the Character, foliage tints, ...)
        // are how UV-less meshes are colored in the software renderer; honor them here so
        // those models don't render as one flat uColor under the GPU path.
        const bool faceCols = mesh.HasFaceColors();
        ID3D11ShaderResourceView* srv = p->TextureFor(mr->texture);

        const int nv = (int)V.size();
        p->verts.clear(); p->verts.reserve(T.size() * 14);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], cc = T[i + 2];
            if (a < 0 || b < 0 || cc < 0 || a >= nv || b >= nv || cc >= nv) continue; // skip bad tris
            Vec3 fn = Vec3::Cross(V[b] - V[a], V[cc] - V[a]);
            { float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
            const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
            float cr = 1.0f, cg = 1.0f, cb = 1.0f;  // white = no-op when there are no face colors
            if (faceCols) { const Color& fc = mesh.triColors[i / 3]; cr = fc.r; cg = fc.g; cb = fc.b; }
            const int idx[3] = {a, b, cc};
            // Per-vertex UVs first, so we can derive the face tangent for normal mapping.
            float uu[3], vv[3];
            for (int k = 0; k < 3; ++k) {
                const Vec3& pos = V[idx[k]];
                if (hasUV) { uu[k] = mesh.uvs[idx[k]].x; vv[k] = mesh.uvs[idx[k]].y; }
                else if (ax >= ay && ax >= az) { uu[k] = pos.z + 0.5f; vv[k] = pos.y + 0.5f; }
                else if (ay >= ax && ay >= az) { uu[k] = pos.x + 0.5f; vv[k] = pos.z + 0.5f; }
                else                           { uu[k] = pos.x + 0.5f; vv[k] = pos.y + 0.5f; }
            }
            // Local-space per-face tangent (edges + UV deltas), transformed to world in VSMain.
            Vec3 e1 = V[b] - V[a], e2 = V[cc] - V[a];
            float du1 = uu[1] - uu[0], dv1 = vv[1] - vv[0];
            float du2 = uu[2] - uu[0], dv2 = vv[2] - vv[0];
            float det = du1 * dv2 - du2 * dv1;
            Vec3 tan = std::fabs(det) > 1e-8f ? (e1 * dv2 - e2 * dv1) * (1.0f / det) : e1;
            { float m = tan.Magnitude(); tan = m > 1e-6f ? tan * (1.0f / m) : Vec3{1, 0, 0}; }
            for (int k = 0; k < 3; ++k) {
                const Vec3& pos = V[idx[k]];
                Vec3 nrm = hasN ? mesh.normals[idx[k]] : fn;
                p->verts.push_back(pos.x); p->verts.push_back(pos.y); p->verts.push_back(pos.z);
                p->verts.push_back(nrm.x); p->verts.push_back(nrm.y); p->verts.push_back(nrm.z);
                p->verts.push_back(uu[k]); p->verts.push_back(vv[k]);
                p->verts.push_back(cr);    p->verts.push_back(cg);    p->verts.push_back(cb);
                p->verts.push_back(tan.x); p->verts.push_back(tan.y); p->verts.push_back(tan.z);
            }
        }
        const int vcount = (int)(p->verts.size() / 14);
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
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(c->Map(p->vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, p->verts.data(), p->verts.size() * sizeof(float));
            c->Unmap(p->vb, 0);
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
            cb.fogCol[0] = rs.fogColor.r; cb.fogCol[1] = rs.fogColor.g; cb.fogCol[2] = rs.fogColor.b; cb.fogCol[3] = 1.0f;
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
        c->IASetVertexBuffers(0, 1, &p->vb, &stride, &offset);
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
    }

    // Resolve MSAA (or copy) -> staging -> read back. D3D textures are top-left, so
    // no row flip is needed (unlike GL).
    if (p->samples > 1) c->ResolveSubresource(p->resolveTex, 0, p->colorTex, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    else                c->CopyResource(p->resolveTex, p->colorTex);
    c->CopyResource(p->staging, p->resolveTex);

    p->pixels.assign((std::size_t)w * h, 0u);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(c->Map(p->staging, 0, D3D11_MAP_READ, 0, &ms))) return nullptr;
    const std::uint8_t* src = (const std::uint8_t*)ms.pData;
    for (int y = 0; y < h; ++y)
        std::memcpy(p->pixels.data() + (std::size_t)y * w, src + (std::size_t)y * ms.RowPitch, (std::size_t)w * 4);
    c->Unmap(p->staging, 0);
    // Release the shadow map from t4 so next frame's depth pass can bind it as a DSV
    // without a read/write hazard warning.
    if (shadowOn) { ID3D11ShaderResourceView* nullsrv = nullptr; c->PSSetShaderResources(4, 1, &nullsrv); }
    return p->pixels.data();
}

void D3D11Renderer::Destroy() {
    if (!m_impl) return;
    Impl* p = m_impl;
    p->DestroyTargets();
    for (auto& kv : p->texCache) if (kv.second) kv.second->Release();
    p->texCache.clear();
    if (p->shadowSrv) p->shadowSrv->Release();
    if (p->shadowDsv) p->shadowDsv->Release();
    if (p->shadowTex) p->shadowTex->Release();
    if (p->vbDepth)   p->vbDepth->Release();
    if (p->layoutDepth) p->layoutDepth->Release();
    if (p->vsDepth)   p->vsDepth->Release();
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
