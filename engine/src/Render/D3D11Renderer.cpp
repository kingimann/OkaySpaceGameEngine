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
    "  float3 uAmbient;   float _p0;\n"
    "  float3 uEye;       float _p1;\n"
    "  float2 uTiling;    float uShadowAlpha; float _p2;\n"
    "  float3 uRimColor;  float uShaderMode;\n"
    "  float3 uGradTop;   float uToonBands;\n"
    "  float3 uGradBot;   float uRimStr;\n"
    "  float4 uLightCount;\n"                 // .x = number of scene lights (0 = use uLightDir)
    "  float4 uLights[32];\n"                 // 8 lights x 4: [dir,type][pos,range][col,cosOut][cosIn,..]
    "};\n"
    "Texture2D uTex : register(t0);\n"
    "SamplerState uSamp : register(s0);\n"
    "struct VSIn { float3 pos:POSITION; float3 nrm:NORMAL; float2 uv:TEXCOORD; float3 col:COLOR; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 nrm:NORMAL; float3 world:TEXCOORD1; float2 uv:TEXCOORD0; float3 col:COLOR; };\n"
    "PSIn VSMain(VSIn i){\n"
    "  PSIn o;\n"
    "  float4 cp = mul(uMVP, float4(i.pos,1.0));\n"
    "  cp.z = (cp.z + cp.w) * 0.5;\n"        // GL [-1,1] -> D3D [0,1]
    "  o.pos = cp;\n"
    "  o.nrm = mul((float3x3)uModel, i.nrm);\n"
    "  o.world = mul(uModel, float4(i.pos,1.0)).xyz;\n"
    "  o.uv = i.uv * uTiling;\n"
    "  o.col = i.col;\n"
    "  return o;\n"
    "}\n"
    "float4 PSMain(PSIn i):SV_TARGET {\n"
    "  if (uShadowAlpha >= 0.0) return float4(0,0,0,uShadowAlpha);\n"   // flat ground contact shadow
    "  float3 N = normalize(i.nrm);\n"
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
    "  if (uUnlit > 0.5) return float4(base + uEmissive, 1.0);\n"
    "  bool toon = uShaderMode > 1.5 && uShaderMode < 2.5 && uToonBands > 0.5;\n"
    "  float3 amb = uAmbient * lerp(0.55, 1.15, saturate(N.y * 0.5 + 0.5));\n"  // hemisphere ambient
    "  float3 lit = amb; float spec = 0.0;\n"
    "  int lc = (int)uLightCount.x;\n"
    "  if (lc < 1) {\n"                                          // fallback: global directional
    "    float ndl = max(dot(N, uLightDir), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "    lit += uLightColor * ndl;\n"
    "    if (uSpecular > 0.0) { float3 H = normalize(uLightDir + Vv);\n"
    "      spec = pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular; }\n"
    "  } else {\n"
    "    for (int li = 0; li < 8; li++) { if (li >= lc) break;\n"      // directional + point + spot
    "      float4 d0 = uLights[li*4+0]; float4 d1 = uLights[li*4+1];\n"
    "      float4 d2 = uLights[li*4+2]; float4 d3 = uLights[li*4+3];\n"
    "      float3 ld; float at = 1.0;\n"
    "      if (d0.w < 0.5) { ld = normalize(-d0.xyz); }\n"             // directional
    "      else { float3 toL = d1.xyz - i.world; float dist = length(toL); ld = toL / max(dist, 1e-4);\n"
    "        at = max(1.0 - dist / max(d1.w, 1e-4), 0.0); at *= at;\n"
    "        if (d0.w > 1.5) { float cs = dot(normalize(d0.xyz), -ld);\n"   // spot cone
    "          float dn = d3.x - d2.w; float sp = dn > 1e-4 ? saturate((cs - d2.w) / dn) : (cs >= d2.w ? 1.0 : 0.0);\n"
    "          at *= sp * sp; } }\n"
    "      float ndl = max(dot(N, ld), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "      lit += d2.xyz * ndl * at;\n"
    "      if (uSpecular > 0.0) { float3 H = normalize(ld + Vv);\n"
    "        spec += pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular * at; }\n"
    "    }\n"
    "  }\n"
    "  if (uShaderMode > 7.5) lit += fz * fz * lit * 0.6;\n"      // Velvet: fuzzy grazing sheen
    "  float3 diff = base * lit;\n"
    "  float3 rim = float3(0,0,0);\n"
    "  float rs = uRimStr; if ((fres || holo) && rs < 0.8) rs = 1.6;\n"
    "  if (rs > 0.0) rim = uRimColor * (fz*fz*fz * rs);\n"
    "  return float4(diff + spec.xxx + rim + uEmissive, 1.0);\n"
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
    float ambient[3];   float _p0;
    float eye[3];       float _p1;
    float tiling[2];    float shadowAlpha; float _p2;
    float rimColor[3];  float shaderMode;
    float gradTop[3];   float toonBands;
    float gradBot[3];   float rimStr;
    float lightCount[4];       // .x = number of scene lights
    float lights[128];         // 32 float4: per light [dir,type][pos,range][col,cosOut][cosIn,..]
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
    };
    p->dev->CreateInputLayout(il, 4, vsb->GetBufferPointer(), vsb->GetBufferSize(), &p->layout);
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
    float frameLights[128]; std::memset(frameLights, 0, sizeof(frameLights));
    int frameLightCount = 0;
    {
        const auto& LS = SceneLights::List();
        int n = (int)LS.size(); if (n > 8) n = 8; frameLightCount = n;
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
        p->verts.clear(); p->verts.reserve(T.size() * 11);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], cc = T[i + 2];
            if (a < 0 || b < 0 || cc < 0 || a >= nv || b >= nv || cc >= nv) continue; // skip bad tris
            Vec3 fn = Vec3::Cross(V[b] - V[a], V[cc] - V[a]);
            { float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
            const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
            float cr = 1.0f, cg = 1.0f, cb = 1.0f;  // white = no-op when there are no face colors
            if (faceCols) { const Color& fc = mesh.triColors[i / 3]; cr = fc.r; cg = fc.g; cb = fc.b; }
            const int idx[3] = {a, b, cc};
            for (int k = 0; k < 3; ++k) {
                const Vec3& pos = V[idx[k]];
                Vec3 nrm = hasN ? mesh.normals[idx[k]] : fn;
                float u, v;
                if (hasUV) { u = mesh.uvs[idx[k]].x; v = mesh.uvs[idx[k]].y; }
                else if (ax >= ay && ax >= az) { u = pos.z + 0.5f; v = pos.y + 0.5f; }
                else if (ay >= ax && ay >= az) { u = pos.x + 0.5f; v = pos.z + 0.5f; }
                else                           { u = pos.x + 0.5f; v = pos.y + 0.5f; }
                p->verts.push_back(pos.x); p->verts.push_back(pos.y); p->verts.push_back(pos.z);
                p->verts.push_back(nrm.x); p->verts.push_back(nrm.y); p->verts.push_back(nrm.z);
                p->verts.push_back(u);     p->verts.push_back(v);
                p->verts.push_back(cr);    p->verts.push_back(cg);    p->verts.push_back(cb);
            }
        }
        const int vcount = (int)(p->verts.size() / 11);
        if (vcount == 0) continue;

        // Grow / upload the dynamic vertex buffer.
        if (vcount > p->vbCap) {
            if (p->vb) { p->vb->Release(); p->vb = nullptr; p->vbCap = 0; }
            const int cap = vcount + 1024;
            D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
            bd.ByteWidth = (UINT)(cap * 11 * sizeof(float)); bd.Usage = D3D11_USAGE_DYNAMIC;
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
        cb.tiling[0] = srv ? mr->tiling.x : 1.0f; cb.tiling[1] = srv ? mr->tiling.y : 1.0f;
        cb.shaderMode = (float)(int)mr->shader;
        cb.toonBands = (float)mr->toonBands;
        cb.rimStr = mr->rimStrength;
        cb.rimColor[0] = mr->rimColor.r; cb.rimColor[1] = mr->rimColor.g; cb.rimColor[2] = mr->rimColor.b;
        cb.gradTop[0] = mr->gradientTop.r; cb.gradTop[1] = mr->gradientTop.g; cb.gradTop[2] = mr->gradientTop.b;
        cb.gradBot[0] = mr->gradientBottom.r; cb.gradBot[1] = mr->gradientBottom.g; cb.gradBot[2] = mr->gradientBottom.b;
        cb.shadowAlpha = -1.0f;   // lit pass (>=0 would draw the flat shadow — must be off here)
        cb.lightCount[0] = (float)frameLightCount;
        std::memcpy(cb.lights, frameLights, sizeof(frameLights));
        if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, &cb, sizeof(cb)); c->Unmap(p->cb, 0);
        }
        if (srv) c->PSSetShaderResources(0, 1, &srv);
        // Per-material texture filter (Pixel = nearest, Smooth = aniso/linear).
        ID3D11SamplerState* useSamp = (mr->texFilter == MeshRenderer::TexFilter::Pixel && p->sampPoint) ? p->sampPoint : p->samp;
        c->PSSetSamplers(0, 1, &useSamp);

        UINT stride = 11 * sizeof(float), offset = 0;
        c->IASetVertexBuffers(0, 1, &p->vb, &stride, &offset);
        c->Draw((UINT)vcount, 0);

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
    return p->pixels.data();
}

void D3D11Renderer::Destroy() {
    if (!m_impl) return;
    Impl* p = m_impl;
    p->DestroyTargets();
    for (auto& kv : p->texCache) if (kv.second) kv.second->Release();
    p->texCache.clear();
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
