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
    "  float2 uTiling;    float2 _p2;\n"
    "};\n"
    "Texture2D uTex : register(t0);\n"
    "SamplerState uSamp : register(s0);\n"
    "struct VSIn { float3 pos:POSITION; float3 nrm:NORMAL; float2 uv:TEXCOORD; };\n"
    "struct PSIn { float4 pos:SV_POSITION; float3 nrm:NORMAL; float3 world:TEXCOORD1; float2 uv:TEXCOORD0; };\n"
    "PSIn VSMain(VSIn i){\n"
    "  PSIn o;\n"
    "  float4 cp = mul(uMVP, float4(i.pos,1.0));\n"
    "  cp.z = (cp.z + cp.w) * 0.5;\n"        // GL [-1,1] -> D3D [0,1]
    "  o.pos = cp;\n"
    "  o.nrm = mul((float3x3)uModel, i.nrm);\n"
    "  o.world = mul(uModel, float4(i.pos,1.0)).xyz;\n"
    "  o.uv = i.uv * uTiling;\n"
    "  return o;\n"
    "}\n"
    "float4 PSMain(PSIn i):SV_TARGET {\n"
    "  float3 base = uColor;\n"
    "  if (uUseTex > 0.5) base *= uTex.Sample(uSamp, i.uv).rgb;\n"
    "  if (uUnlit > 0.5) return float4(base + uEmissive, 1.0);\n"
    "  float3 N = normalize(i.nrm);\n"
    "  float ndl = max(dot(N, uLightDir), 0.0);\n"
    "  float3 diff = base * (uAmbient + uLightColor * ndl);\n"
    "  float spec = 0.0;\n"
    "  if (uSpecular > 0.0) {\n"
    "    float3 V = normalize(uEye - i.world);\n"
    "    float3 H = normalize(uLightDir + V);\n"
    "    spec = pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular;\n"
    "  }\n"
    "  return float4(diff + spec.xxx + uEmissive, 1.0);\n"
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
    float tiling[2];    float _p2[2];
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
    ID3D11SamplerState*     samp = nullptr;
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
    };
    p->dev->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &p->layout);
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

    D3D11_SAMPLER_DESC sd; std::memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    p->dev->CreateSamplerState(&sd, &p->samp);

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
    w = W; h = H; samples = S < 1 ? 1 : S;

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
        ID3D11ShaderResourceView* srv = p->TextureFor(mr->texture);

        p->verts.clear(); p->verts.reserve(T.size() * 8);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], cc = T[i + 2];
            Vec3 fn = Vec3::Cross(V[b] - V[a], V[cc] - V[a]);
            { float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
            const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
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
            }
        }
        const int vcount = (int)(p->verts.size() / 8);
        if (vcount == 0) continue;

        // Grow / upload the dynamic vertex buffer.
        if (vcount > p->vbCap) {
            if (p->vb) { p->vb->Release(); p->vb = nullptr; }
            const int cap = vcount + 1024;
            D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
            bd.ByteWidth = (UINT)(cap * 8 * sizeof(float)); bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(p->dev->CreateBuffer(&bd, nullptr, &p->vb))) continue;
            p->vbCap = cap;
        }
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
        cb.ambient[0] = 0.35f; cb.ambient[1] = 0.36f; cb.ambient[2] = 0.40f;
        cb.eye[0] = eye.x; cb.eye[1] = eye.y; cb.eye[2] = eye.z;
        cb.tiling[0] = srv ? mr->tiling.x : 1.0f; cb.tiling[1] = srv ? mr->tiling.y : 1.0f;
        if (SUCCEEDED(c->Map(p->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, &cb, sizeof(cb)); c->Unmap(p->cb, 0);
        }
        if (srv) c->PSSetShaderResources(0, 1, &srv);

        UINT stride = 8 * sizeof(float), offset = 0;
        c->IASetVertexBuffers(0, 1, &p->vb, &stride, &offset);
        c->Draw((UINT)vcount, 0);
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
