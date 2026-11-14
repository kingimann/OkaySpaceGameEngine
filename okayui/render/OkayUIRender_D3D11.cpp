// OkayUI render backend: Direct3D 11. See OkayUIRenderBackends.hpp.
//
// Replaces the old okayui/src/OkayUI_D3D11.cpp path for new hosts: it draws one scissor rectangle per span (the old one
// drew the whole index buffer in one call and ignored clipping and textures), samples textures (glyph atlas, fallback fonts,
// host images), scales by the DPI and restores the pipeline state it changed. The shader source and the factory are
// compiled on every platform (the factory returns null off Windows); the Direct3D calls are Windows only.
#include "okay/UI/OkayUIRenderBackends.hpp"

#include <cstring>
#include <unordered_map>

namespace OkayUI {
namespace render {

static const char* const kHlsl =
    "cbuffer CB : register(b0) { float2 uScale; float2 uPad; };\n"       // uScale = 2 * dpi / framebuffer size
    "Texture2D    tex0 : register(t0);\n"
    "SamplerState smp0 : register(s0);\n"
    "struct VSIn { float2 pos : POSITION; float4 col : COLOR; float2 uv : TEXCOORD; };\n"
    "struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR; float2 uv : TEXCOORD; };\n"
    "PSIn VSMain(VSIn i) {\n"
    "  PSIn o;\n"
    "  o.pos = float4(i.pos.x * uScale.x - 1.0, 1.0 - i.pos.y * uScale.y, 0.0, 1.0);\n"
    "  o.col = i.col; o.uv = i.uv;\n"
    "  return o;\n"
    "}\n"
    "float4 PSMain(PSIn i) : SV_TARGET { return i.col * tex0.Sample(smp0, i.uv); }\n";

const char* D3D11ShaderSource() { return kHlsl; }

} // namespace render
} // namespace OkayUI

#if defined(_WIN32)

#include <d3d11.h>
#include <d3dcompiler.h>

namespace OkayUI {
namespace render {
namespace {

template <class T> void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

class D3D11Backend final : public Backend {
public:
    D3D11Backend(ID3D11Device* d, ID3D11DeviceContext* c) : m_dev(d), m_ctx(c) { m_dev->AddRef(); m_ctx->AddRef(); }
    ~D3D11Backend() override {
        for (auto& kv : m_tex) { Rel(kv.second.srv); Rel(kv.second.tex); }
        Rel(m_whiteSrv); Rel(m_whiteTex);
        Rel(m_vb); Rel(m_ib); Rel(m_cb); Rel(m_blend); Rel(m_raster); Rel(m_depth); Rel(m_sampLin); Rel(m_sampNear);
        Rel(m_layout); Rel(m_vs); Rel(m_ps); Rel(m_ctx); Rel(m_dev);
    }

    bool Init(std::string* err) {
        ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* eb = nullptr;
        const SIZE_T len = std::strlen(kHlsl);
        if (FAILED(D3DCompile(kHlsl, len, "okayui", nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsb, &eb))) { Fail(eb, "vertex shader", err); return false; }
        if (FAILED(D3DCompile(kHlsl, len, "okayui", nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psb, &eb))) { Fail(eb, "pixel shader", err); Rel(vsb); return false; }
        bool ok = SUCCEEDED(m_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &m_vs))
               && SUCCEEDED(m_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &m_ps));
        const D3D11_INPUT_ELEMENT_DESC il[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        ok = ok && SUCCEEDED(m_dev->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &m_layout));
        Rel(vsb); Rel(psb);

        D3D11_BUFFER_DESC cbd; std::memset(&cbd, 0, sizeof cbd);
        cbd.ByteWidth = 16; cbd.Usage = D3D11_USAGE_DYNAMIC; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok = ok && SUCCEEDED(m_dev->CreateBuffer(&cbd, nullptr, &m_cb));

        D3D11_BLEND_DESC bd; std::memset(&bd, 0, sizeof bd);       // straight alpha, source-over
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ok = ok && SUCCEEDED(m_dev->CreateBlendState(&bd, &m_blend));

        D3D11_RASTERIZER_DESC rd; std::memset(&rd, 0, sizeof rd);  // no culling; the scissor rectangle is the clip of each span
        rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.ScissorEnable = TRUE; rd.DepthClipEnable = TRUE;
        ok = ok && SUCCEEDED(m_dev->CreateRasterizerState(&rd, &m_raster));

        D3D11_DEPTH_STENCIL_DESC dd; std::memset(&dd, 0, sizeof dd);   // depth and stencil off whatever the host left on
        ok = ok && SUCCEEDED(m_dev->CreateDepthStencilState(&dd, &m_depth));

        D3D11_SAMPLER_DESC sd; std::memset(&sd, 0, sizeof sd);
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;  ok = ok && SUCCEEDED(m_dev->CreateSamplerState(&sd, &m_sampLin));
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;   ok = ok && SUCCEEDED(m_dev->CreateSamplerState(&sd, &m_sampNear));

        const std::uint8_t white[4] = { 255, 255, 255, 255 };
        Tex w; ok = ok && Make(1, 1, white, w);
        if (ok) { m_whiteTex = w.tex; m_whiteSrv = w.srv; }
        if (!ok && err && err->empty()) *err = "could not create the Direct3D 11 objects";
        return ok;
    }

    const char* Name() const override { return "direct3d11"; }
    Caps Capabilities() const override { Caps c; c.maxTexture = 8192; return c; }

    TexId CreateTexture(int w, int h, bool linear, const std::uint8_t* rgba) override {
        Tex t; t.linear = linear;
        if (!Make(w, h, rgba, t)) return 0;
        const TexId id = ++m_next;
        m_tex[id] = t;
        return id;
    }
    bool UpdateTexture(TexId id, int w, int h, const std::uint8_t* rgba) override {
        auto it = m_tex.find(id);
        if (it == m_tex.end() || !rgba) return false;
        Tex& t = it->second;
        if (t.w == w && t.h == h) {
            m_ctx->UpdateSubresource(t.tex, 0, nullptr, rgba, (UINT)w * 4, 0);
            return true;
        }
        Tex n; n.linear = t.linear;
        if (!Make(w, h, rgba, n)) return false;
        Rel(t.srv); Rel(t.tex);
        t = n;
        return true;
    }
    void DestroyTexture(TexId id) override {
        auto it = m_tex.find(id);
        if (it == m_tex.end()) return;
        Rel(it->second.srv); Rel(it->second.tex);
        m_tex.erase(it);
    }

    bool BeginFrame(int fbW, int fbH, float dpi) override {
        m_w = fbW; m_h = fbH;
        // ---- save what we change (the same set Dear ImGui's backend saves, minus what we do not touch) ----
        m_sNumRects = m_sNumViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        m_ctx->RSGetScissorRects(&m_sNumRects, m_sRects);
        m_ctx->RSGetViewports(&m_sNumViewports, m_sViewports);
        m_ctx->RSGetState(&m_sRaster);
        m_ctx->OMGetBlendState(&m_sBlend, m_sBlendFactor, &m_sSampleMask);
        m_ctx->OMGetDepthStencilState(&m_sDepth, &m_sStencilRef);
        m_ctx->PSGetShaderResources(0, 1, &m_sSrv);
        m_ctx->PSGetSamplers(0, 1, &m_sSamp);
        m_sPsCount = m_sVsCount = 256;
        m_ctx->PSGetShader(&m_sPs, m_sPsInst, &m_sPsCount);
        m_ctx->VSGetShader(&m_sVs, m_sVsInst, &m_sVsCount);
        m_ctx->VSGetConstantBuffers(0, 1, &m_sCb);
        m_ctx->IAGetPrimitiveTopology(&m_sTopo);
        m_ctx->IAGetIndexBuffer(&m_sIb, &m_sIbFormat, &m_sIbOffset);
        m_ctx->IAGetVertexBuffers(0, 1, &m_sVb, &m_sVbStride, &m_sVbOffset);
        m_ctx->IAGetInputLayout(&m_sLayout);
        // ---- ours ----
        D3D11_VIEWPORT vp; std::memset(&vp, 0, sizeof vp);
        vp.Width = (float)fbW; vp.Height = (float)fbH; vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        m_ctx->RSSetViewports(1, &vp);
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(m_ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            const float cb[4] = { 2.0f * dpi / (float)fbW, 2.0f * dpi / (float)fbH, 0.0f, 0.0f };
            std::memcpy(ms.pData, cb, sizeof cb);
            m_ctx->Unmap(m_cb, 0);
        } else return false;
        m_ctx->IASetInputLayout(m_layout);
        m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_ctx->VSSetShader(m_vs, nullptr, 0);
        m_ctx->VSSetConstantBuffers(0, 1, &m_cb);
        m_ctx->PSSetShader(m_ps, nullptr, 0);
        const float bf[4] = { 0, 0, 0, 0 };
        m_ctx->OMSetBlendState(m_blend, bf, 0xffffffffu);
        m_ctx->OMSetDepthStencilState(m_depth, 0);
        m_ctx->RSSetState(m_raster);
        return true;
    }

    void Draw(const Vertex* vtx, int nv, const std::int32_t* idx, int ni, const Span* spans, int ns) override {
        if (!EnsureBuffer(m_vb, m_vbCap, nv, (int)sizeof(Vertex), D3D11_BIND_VERTEX_BUFFER)) return;
        if (!EnsureBuffer(m_ib, m_ibCap, ni, (int)sizeof(std::int32_t), D3D11_BIND_INDEX_BUFFER)) return;
        D3D11_MAPPED_SUBRESOURCE ms;
        if (FAILED(m_ctx->Map(m_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
        std::memcpy(ms.pData, vtx, (size_t)nv * sizeof(Vertex)); m_ctx->Unmap(m_vb, 0);
        if (FAILED(m_ctx->Map(m_ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
        std::memcpy(ms.pData, idx, (size_t)ni * sizeof(std::int32_t)); m_ctx->Unmap(m_ib, 0);
        const UINT stride = (UINT)sizeof(Vertex), offset = 0;
        m_ctx->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
        m_ctx->IASetIndexBuffer(m_ib, DXGI_FORMAT_R32_UINT, 0);
        ID3D11ShaderResourceView* boundSrv = nullptr; ID3D11SamplerState* boundSamp = nullptr;
        for (int s = 0; s < ns; ++s) {
            const Span& sp = spans[s];
            ID3D11ShaderResourceView* srv = m_whiteSrv; ID3D11SamplerState* smp = m_sampNear;
            if (sp.tex) {
                auto it = m_tex.find(sp.tex);
                if (it == m_tex.end()) continue;
                srv = it->second.srv; smp = it->second.linear ? m_sampLin : m_sampNear;
            }
            if (srv != boundSrv) { m_ctx->PSSetShaderResources(0, 1, &srv); boundSrv = srv; }
            if (smp != boundSamp) { m_ctx->PSSetSamplers(0, 1, &smp); boundSamp = smp; }
            D3D11_RECT r;
            r.left = sp.clipOn ? sp.x0 : 0; r.top = sp.clipOn ? sp.y0 : 0; r.right = sp.clipOn ? sp.x1 : m_w; r.bottom = sp.clipOn ? sp.y1 : m_h;
            m_ctx->RSSetScissorRects(1, &r);                                   // one scissor rectangle per span
            m_ctx->DrawIndexed((UINT)sp.idxCount, (UINT)sp.idxOffset, 0);
        }
    }

    void EndFrame() override {
        m_ctx->RSSetScissorRects(m_sNumRects, m_sRects);
        m_ctx->RSSetViewports(m_sNumViewports, m_sViewports);
        m_ctx->RSSetState(m_sRaster); Rel(m_sRaster);
        m_ctx->OMSetBlendState(m_sBlend, m_sBlendFactor, m_sSampleMask); Rel(m_sBlend);
        m_ctx->OMSetDepthStencilState(m_sDepth, m_sStencilRef); Rel(m_sDepth);
        m_ctx->PSSetShaderResources(0, 1, &m_sSrv); Rel(m_sSrv);
        m_ctx->PSSetSamplers(0, 1, &m_sSamp); Rel(m_sSamp);
        m_ctx->PSSetShader(m_sPs, m_sPsInst, m_sPsCount); Rel(m_sPs);
        for (UINT i = 0; i < m_sPsCount; ++i) Rel(m_sPsInst[i]);
        m_ctx->VSSetShader(m_sVs, m_sVsInst, m_sVsCount); Rel(m_sVs);
        for (UINT i = 0; i < m_sVsCount; ++i) Rel(m_sVsInst[i]);
        m_ctx->VSSetConstantBuffers(0, 1, &m_sCb); Rel(m_sCb);
        m_ctx->IASetPrimitiveTopology(m_sTopo);
        m_ctx->IASetIndexBuffer(m_sIb, m_sIbFormat, m_sIbOffset); Rel(m_sIb);
        m_ctx->IASetVertexBuffers(0, 1, &m_sVb, &m_sVbStride, &m_sVbOffset); Rel(m_sVb);
        m_ctx->IASetInputLayout(m_sLayout); Rel(m_sLayout);
    }

    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override {
        // The bound render target, copied to a staging texture. Only RGBA8 / BGRA8 targets are understood.
        ID3D11RenderTargetView* rtv = nullptr;
        m_ctx->OMGetRenderTargets(1, &rtv, nullptr);
        if (!rtv) return false;
        ID3D11Resource* res = nullptr; rtv->GetResource(&res); Rel(rtv);
        ID3D11Texture2D* src = nullptr;
        if (!res || FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&src))) { Rel(res); return false; }
        Rel(res);
        D3D11_TEXTURE2D_DESC td; src->GetDesc(&td);
        const bool bgra = td.Format == DXGI_FORMAT_B8G8R8A8_UNORM || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const bool rgbaFmt = td.Format == DXGI_FORMAT_R8G8B8A8_UNORM || td.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if ((!bgra && !rgbaFmt) || td.SampleDesc.Count != 1) { Rel(src); return false; }
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
        ID3D11Texture2D* stg = nullptr;
        if (FAILED(m_dev->CreateTexture2D(&td, nullptr, &stg))) { Rel(src); return false; }
        m_ctx->CopyResource(stg, src);
        D3D11_MAPPED_SUBRESOURCE ms;
        bool ok = SUCCEEDED(m_ctx->Map(stg, 0, D3D11_MAP_READ, 0, &ms));
        if (ok) {
            w = (int)td.Width; h = (int)td.Height;
            rgba.resize((size_t)w * (size_t)h * 4);
            for (int y = 0; y < h; ++y) {
                const std::uint8_t* s = (const std::uint8_t*)ms.pData + (size_t)y * ms.RowPitch;
                std::uint8_t* d = &rgba[(size_t)y * w * 4];
                for (int x = 0; x < w; ++x) { d[x * 4] = s[x * 4 + (bgra ? 2 : 0)]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4 + (bgra ? 0 : 2)]; d[x * 4 + 3] = s[x * 4 + 3]; }
            }
            m_ctx->Unmap(stg, 0);
        }
        Rel(stg); Rel(src);
        return ok;
    }

private:
    struct Tex { ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr; int w = 0, h = 0; bool linear = false; };
    static void Fail(ID3DBlob* eb, const char* what, std::string* err) {
        if (err) *err = std::string(what) + ": " + (eb ? (const char*)eb->GetBufferPointer() : "compile failed");
        Rel(eb);
    }
    bool Make(int w, int h, const std::uint8_t* rgba, Tex& out) {
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
        D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof td);
        td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<std::uint8_t> zero;
        if (!rgba) { zero.assign((size_t)w * (size_t)h * 4, 0); rgba = zero.data(); }
        D3D11_SUBRESOURCE_DATA sd; sd.pSysMem = rgba; sd.SysMemPitch = (UINT)w * 4; sd.SysMemSlicePitch = 0;
        if (FAILED(m_dev->CreateTexture2D(&td, &sd, &out.tex))) return false;
        if (FAILED(m_dev->CreateShaderResourceView(out.tex, nullptr, &out.srv))) { Rel(out.tex); return false; }
        out.w = w; out.h = h;
        return true;
    }
    bool EnsureBuffer(ID3D11Buffer*& b, int& cap, int need, int elem, UINT bind) {
        if (b && cap >= need) return true;
        Rel(b);
        cap = need + need / 2 + 256;
        D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof bd);
        bd.ByteWidth = (UINT)((size_t)cap * (size_t)elem); bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = bind; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &b))) { cap = 0; return false; }
        return true;
    }

    ID3D11Device* m_dev; ID3D11DeviceContext* m_ctx;
    ID3D11VertexShader* m_vs = nullptr; ID3D11PixelShader* m_ps = nullptr; ID3D11InputLayout* m_layout = nullptr;
    ID3D11Buffer *m_vb = nullptr, *m_ib = nullptr, *m_cb = nullptr; int m_vbCap = 0, m_ibCap = 0;
    ID3D11BlendState* m_blend = nullptr; ID3D11RasterizerState* m_raster = nullptr; ID3D11DepthStencilState* m_depth = nullptr;
    ID3D11SamplerState *m_sampLin = nullptr, *m_sampNear = nullptr;
    ID3D11Texture2D* m_whiteTex = nullptr; ID3D11ShaderResourceView* m_whiteSrv = nullptr;
    std::unordered_map<TexId, Tex> m_tex; TexId m_next = 0;
    int m_w = 0, m_h = 0;
    // saved pipeline state
    UINT m_sNumRects = 0, m_sNumViewports = 0;
    D3D11_RECT m_sRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_VIEWPORT m_sViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11RasterizerState* m_sRaster = nullptr; ID3D11BlendState* m_sBlend = nullptr; float m_sBlendFactor[4] = {0}; UINT m_sSampleMask = 0;
    ID3D11DepthStencilState* m_sDepth = nullptr; UINT m_sStencilRef = 0;
    ID3D11ShaderResourceView* m_sSrv = nullptr; ID3D11SamplerState* m_sSamp = nullptr;
    ID3D11PixelShader* m_sPs = nullptr; ID3D11VertexShader* m_sVs = nullptr; ID3D11ClassInstance *m_sPsInst[256] = {nullptr}, *m_sVsInst[256] = {nullptr};
    UINT m_sPsCount = 0, m_sVsCount = 0;
    ID3D11Buffer* m_sCb = nullptr; D3D11_PRIMITIVE_TOPOLOGY m_sTopo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* m_sIb = nullptr; DXGI_FORMAT m_sIbFormat = DXGI_FORMAT_UNKNOWN; UINT m_sIbOffset = 0;
    ID3D11Buffer* m_sVb = nullptr; UINT m_sVbStride = 0, m_sVbOffset = 0;
    ID3D11InputLayout* m_sLayout = nullptr;
};

} // namespace

std::unique_ptr<Backend> CreateD3D11Backend(void* device, void* context, std::string* err) {
    if (!device || !context) { if (err) *err = "null device or context"; return nullptr; }
    std::unique_ptr<D3D11Backend> b(new D3D11Backend((ID3D11Device*)device, (ID3D11DeviceContext*)context));
    if (!b->Init(err)) return nullptr;
    return std::unique_ptr<Backend>(b.release());
}

} // namespace render
} // namespace OkayUI

#else   // not Windows

namespace OkayUI {
namespace render {
std::unique_ptr<Backend> CreateD3D11Backend(void*, void*, std::string* err) {
    if (err) *err = "Direct3D 11 exists on Windows only";
    return nullptr;
}
} // namespace render
} // namespace OkayUI

#endif
