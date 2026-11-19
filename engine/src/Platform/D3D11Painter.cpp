// okay::platform::Painter on Direct3D 11 (Windows only). One device and one swap chain per window of the own Win32 backend. Written from the
// Direct3D 11 / DXGI / D3DCompile documentation and the Painter interface; the geometry (every drawing call as triangles) is D3D11Batch.hpp and is
// tested on any machine. THE DIRECT3D CALLS IN THIS FILE HAVE ONLY BEEN SYNTAX-CHECKED (MinGW), NEVER RUN: no Windows machine was available.
//
// How it draws: drawing calls append triangles (device pixels) to a batch; the batch is sent as one draw call whenever the texture, the blend
// mode, the scissor rectangle or the sampler changes, and at Present / ReadPixels / SetRenderTarget / Flush. d3d11.dll and d3dcompiler_47.dll are
// loaded at run time, so a machine without them (or with no usable device) just gets null from the factory and the caller falls back to the
// software painter.
#include "okay/Platform/Painter.hpp"

#if defined(_WIN32)

#include "D3D11Batch.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace okay::platform {
namespace {

namespace d3 = okay::platform::d3d;
using ui2d::Tex;

template <class T> void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

const char kHlsl[] =
    "cbuffer CB : register(b0) { float2 uInvSize; float2 uPad; };\n"   // uInvSize = 2 / target size in pixels
    "Texture2D tex0 : register(t0);\n"
    "SamplerState smp0 : register(s0);\n"
    "struct VSIn { float2 pos : POSITION; float4 col : COLOR; float2 uv : TEXCOORD; };\n"
    "struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR; float2 uv : TEXCOORD; };\n"
    "PSIn VSMain(VSIn i) {\n"
    "  PSIn o;\n"
    "  o.pos = float4(i.pos.x * uInvSize.x - 1.0, 1.0 - i.pos.y * uInvSize.y, 0.0, 1.0);\n"
    "  o.col = i.col; o.uv = i.uv;\n"
    "  return o;\n"
    "}\n"
    "float4 PSMain(PSIn i) : SV_TARGET { return i.col * tex0.Sample(smp0, i.uv); }\n";

struct DTex {
    int w = 0, h = 0;
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;   // only for target textures
    BlendMode blend = BlendMode::Blend;
    ScaleMode scale = ScaleMode::Nearest;
    bool target = false;
    std::uint8_t mr = 255, mg = 255, mb = 255, ma = 255;
};

class D3D11Painter final : public Painter {
public:
    explicit D3D11Painter(Window& w, bool vsync) : m_win(&w), m_vsync(vsync) {}
    ~D3D11Painter() override {
        ReleaseCachedTextures();
        for (DTex* t : m_texs) { Rel(t->rtv); Rel(t->srv); Rel(t->tex); delete t; }
        if (m_ctx) m_ctx->ClearState();
        Rel(m_vb); Rel(m_cb); Rel(m_blendOn); Rel(m_blendOff); Rel(m_raster); Rel(m_depth); Rel(m_sampLin); Rel(m_sampNear); Rel(m_layout); Rel(m_vs); Rel(m_ps);
        Rel(m_whiteSrv); Rel(m_whiteTex); Rel(m_backRtv); Rel(m_swap); Rel(m_ctx); Rel(m_dev);
        if (m_d3dDll) FreeLibrary(m_d3dDll);
        if (m_compDll) FreeLibrary(m_compDll);
    }

    bool Init(std::string* err) {
        m_hwnd = (HWND)m_win->Win32Handle();
        if (!m_hwnd) { Fail(err, "the window has no HWND"); return false; }
        m_d3dDll = LoadLibraryA("d3d11.dll");
        m_compDll = LoadLibraryA("d3dcompiler_47.dll");
        if (!m_d3dDll) { Fail(err, "d3d11.dll is not available"); return false; }
        if (!m_compDll) { Fail(err, "d3dcompiler_47.dll is not available"); return false; }
        auto create = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(m_d3dDll, "D3D11CreateDevice");
        m_compile = (pD3DCompile)GetProcAddress(m_compDll, "D3DCompile");
        if (!create || !m_compile) { Fail(err, "the Direct3D 11 entry points are missing"); return false; }
        const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_10_0;
        HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, want, 3, D3D11_SDK_VERSION, &m_dev, &got, &m_ctx);
        if (FAILED(hr)) { m_warp = true; hr = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, want, 3, D3D11_SDK_VERSION, &m_dev, &got, &m_ctx); }   // the CPU rasteriser of Windows
        if (FAILED(hr)) { Fail(err, "D3D11CreateDevice failed"); return false; }
        int w = 0, h = 0;
        m_win->GetDrawableSize(w, h);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        if (!MakeSwapChain(w, h, err)) return false;
        if (!MakePipeline(err)) return false;
        return true;
    }

    PainterKind Kind() const override { return PainterKind::D3D11; }
    void* NativeRenderer() override { return nullptr; }
    std::string DriverName() override { return m_warp ? "direct3d11 (WARP, software)" : "direct3d11"; }
    bool IsAccelerated() override { return !m_warp; }

    // ---- target -----------------------------------------------------------------------------
    void OutputSize(int* w, int* h) override { SyncSize(); if (w) *w = TargetW(); if (h) *h = TargetH(); }
    void Clear() override {
        SyncSize();
        Flush();
        const float c[4] = {m_r / 255.0f, m_g / 255.0f, m_b / 255.0f, m_a / 255.0f};
        if (ID3D11RenderTargetView* rtv = CurrentRtv()) m_ctx->ClearRenderTargetView(rtv, c);
    }
    void SetBlendMode(BlendMode m) override { m_blend = m; }
    BlendMode GetBlendMode() override { return m_blend; }
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override { m_r = r; m_g = g; m_b = b; m_a = a; }
    bool GetDrawColor(std::uint8_t* r, std::uint8_t* g, std::uint8_t* b, std::uint8_t* a) override { if (r) *r = m_r; if (g) *g = m_g; if (b) *b = m_b; if (a) *a = m_a; return true; }

    void SetClipRect(const ui2d::Rect* r) override { if (r) { m_clipOn = true; m_clip = *r; } else m_clipOn = false; }
    void GetClipRect(ui2d::Rect* r) override { if (r) *r = m_clipOn ? m_clip : ui2d::Rect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return m_clipOn; }
    void SetScale(float sx, float sy) override { if (sx > 0 && sy > 0 && std::isfinite(sx) && std::isfinite(sy)) { m_sx = sx; m_sy = sy; } }

    // ---- primitives -------------------------------------------------------------------------
    void FillRect(const ui2d::Rect* r) override {
        if (!r) { FillAll(); return; }
        Begin(nullptr, m_blend);
        d3::FillDev(m_batch, d3::ScaleRect(*r, m_sx, m_sy), m_r, m_g, m_b, m_a);
    }
    void FillRectF(const ui2d::FRect* r) override {
        if (!r) { FillAll(); return; }
        Begin(nullptr, m_blend);
        d3::FillDev(m_batch, d3::ScaleRectF(*r, m_sx, m_sy), m_r, m_g, m_b, m_a);
    }
    void DrawRect(const ui2d::Rect* r) override {
        Begin(nullptr, m_blend);
        const ui2d::Rect full{0, 0, (int)std::lround(TargetW() / m_sx), (int)std::lround(TargetH() / m_sy)};
        const ui2d::Rect rc = r ? *r : full;
        if (rc.w <= 0 || rc.h <= 0) return;
        d3::OutlineDev(m_batch, d3::ScaleRect(rc, m_sx, m_sy), m_r, m_g, m_b, m_a);
    }
    void DrawPoint(int x, int y) override {
        Begin(nullptr, m_blend);
        const int dx = d3::RoundI((x + 0.5) * m_sx - 0.5), dy = d3::RoundI((y + 0.5) * m_sy - 0.5);
        d3::FillDev(m_batch, d3::DevRect{dx, dy, dx + std::max(1, d3::RoundI(m_sx)), dy + std::max(1, d3::RoundI(m_sy))}, m_r, m_g, m_b, m_a);
    }
    void DrawLine(int x0, int y0, int x1, int y1) override {
        Begin(nullptr, m_blend);
        d3::LineQuad(m_batch, d3::RoundI(x0 * (double)m_sx) + 0.5, d3::RoundI(y0 * (double)m_sy) + 0.5, d3::RoundI(x1 * (double)m_sx) + 0.5, d3::RoundI(y1 * (double)m_sy) + 0.5, 1.0, m_r, m_g, m_b, m_a);
    }
    void DrawLineF(float x0, float y0, float x1, float y1) override {
        Begin(nullptr, m_blend);
        d3::LineQuad(m_batch, std::floor(x0 * (double)m_sx) + 0.5, std::floor(y0 * (double)m_sy) + 0.5, std::floor(x1 * (double)m_sx) + 0.5, std::floor(y1 * (double)m_sy) + 0.5, 1.0, m_r, m_g, m_b, m_a);
    }

    // ---- textures ---------------------------------------------------------------------------
    Tex CreateTexture(const TextureDesc& d, const std::uint8_t* rgba) override {
        if (d.width <= 0 || d.height <= 0 || d.width > 16384 || d.height > 16384) return nullptr;
        D3D11_TEXTURE2D_DESC td;
        std::memset(&td, 0, sizeof(td));
        td.Width = (UINT)d.width; td.Height = (UINT)d.height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (d.target ? D3D11_BIND_RENDER_TARGET : 0);
        D3D11_SUBRESOURCE_DATA sd;
        std::memset(&sd, 0, sizeof(sd));
        std::vector<std::uint8_t> zero;
        if (!rgba) { zero.assign((size_t)d.width * (size_t)d.height * 4, 0); }
        sd.pSysMem = rgba ? rgba : zero.data(); sd.SysMemPitch = (UINT)d.width * 4;
        DTex* t = new DTex();
        t->w = d.width; t->h = d.height; t->blend = d.blend; t->scale = d.scale; t->target = d.target;
        if (FAILED(m_dev->CreateTexture2D(&td, &sd, &t->tex)) || FAILED(m_dev->CreateShaderResourceView(t->tex, nullptr, &t->srv)) ||
            (d.target && FAILED(m_dev->CreateRenderTargetView(t->tex, nullptr, &t->rtv)))) {
            Rel(t->rtv); Rel(t->srv); Rel(t->tex); delete t; return nullptr;
        }
        m_texs.insert(t);
        return t;
    }
    bool UpdateTexture(Tex tex, const ui2d::Rect* r, const void* rgba, int pitch) override {
        DTex* t = TT(tex);
        if (!t || !rgba) return false;
        ui2d::Rect rc{0, 0, t->w, t->h};
        if (r) rc = *r;
        if (rc.x < 0 || rc.y < 0 || rc.w <= 0 || rc.h <= 0 || rc.x + (long long)rc.w > t->w || rc.y + (long long)rc.h > t->h || pitch < rc.w * 4) return false;
        Flush();   // draws that sample the old pixels go first
        D3D11_BOX box = {(UINT)rc.x, (UINT)rc.y, 0, (UINT)(rc.x + rc.w), (UINT)(rc.y + rc.h), 1};
        m_ctx->UpdateSubresource(t->tex, 0, &box, rgba, (UINT)pitch, 0);
        return true;
    }
    void DestroyTexture(Tex tex) override {
        DTex* t = TT(tex);
        if (!t) return;
        Flush();
        if (m_target == t) SetRenderTarget(nullptr);
        m_texs.erase(t);
        Rel(t->rtv); Rel(t->srv); Rel(t->tex);
        delete t;
    }
    void SetTextureBlendMode(Tex t, BlendMode m) override { if (DTex* s = TT(t)) s->blend = m; }
    void SetTextureScaleMode(Tex t, ScaleMode m) override { if (DTex* s = TT(t)) s->scale = m; }
    void QueryTexture(Tex t, int* w, int* h) override { DTex* s = TT(t); if (w) *w = s ? s->w : 0; if (h) *h = s ? s->h : 0; }
    void SetTextureColorMod(Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override { if (DTex* s = TT(t)) { s->mr = r; s->mg = g; s->mb = b; } }
    void SetTextureAlphaMod(Tex t, std::uint8_t a) override { if (DTex* s = TT(t)) s->ma = a; }

    void Copy(Tex t, const ui2d::Rect* s, const ui2d::Rect* d) override {
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, 0.0, nullptr, 0); else Blit(t, s, 0, 0, TargetW() / m_sx, TargetH() / m_sy, 0.0, nullptr, 0);
    }
    void CopyF(Tex t, const ui2d::Rect* s, const ui2d::FRect* d) override {
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, 0.0, nullptr, 0); else Blit(t, s, 0, 0, TargetW() / m_sx, TargetH() / m_sy, 0.0, nullptr, 0);
    }
    void CopyEx(Tex t, const ui2d::Rect* s, const ui2d::Rect* d, double angle, const ui2d::Point* c, int flip) override {
        ui2d::FPoint fc; if (c) fc = ui2d::FPoint{(float)c->x, (float)c->y};
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, angle, c ? &fc : nullptr, flip); else Blit(t, s, 0, 0, TargetW() / m_sx, TargetH() / m_sy, angle, c ? &fc : nullptr, flip);
    }
    void CopyExF(Tex t, const ui2d::Rect* s, const ui2d::FRect* d, double angle, const ui2d::FPoint* c, int flip) override {
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, angle, c, flip); else Blit(t, s, 0, 0, TargetW() / m_sx, TargetH() / m_sy, angle, c, flip);
    }
    void Geometry(Tex tex, const ui2d::Vertex* v, int nv, const int* idx, int ni) override {
        DTex* t = tex ? TT(tex) : nullptr;
        if (tex && !t) return;
        Begin(t, t ? t->blend : m_blend);
        d3::GeometryVerts(m_batch, v, nv, idx, ni, m_sx, m_sy, t ? t->mr : 255, t ? t->mg : 255, t ? t->mb : 255, t ? t->ma : 255);
    }

    // ---- render targets, flush ----------------------------------------------------------------
    bool SetRenderTarget(Tex tex) override {
        DTex* nt = nullptr;
        if (tex) { nt = TT(tex); if (!nt || !nt->target) return false; }
        if (nt == m_target) return true;
        Flush();
        SaveTargetState();
        m_target = nt;
        RestoreTargetState();
        return true;
    }
    Tex GetRenderTarget() override { return m_target; }
    void Flush() override { Submit(); }

    // ---- the frame --------------------------------------------------------------------------
    void Present() override {
        Submit();
        if (!m_swap) return;
        const HRESULT hr = m_swap->Present(m_vsync ? 1 : 0, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) m_lost = true;   // the owner sees Lost() and builds a new painter
    }
    bool SetVSync(bool on) override { m_vsync = on; return on; }
    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override {
        Submit();
        SyncSize();
        ID3D11Texture2D* src = m_target ? m_target->tex : m_back;
        w = TargetW(); h = TargetH();
        if (!src || w <= 0 || h <= 0) return false;
        D3D11_TEXTURE2D_DESC td;
        src->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
        ID3D11Texture2D* st = nullptr;
        if (FAILED(m_dev->CreateTexture2D(&td, nullptr, &st))) return false;
        m_ctx->CopyResource(st, src);
        D3D11_MAPPED_SUBRESOURCE ms;
        if (FAILED(m_ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms))) { Rel(st); return false; }
        rgba.assign((size_t)w * (size_t)h * 4, 0);
        for (int y = 0; y < h; ++y) std::memcpy(&rgba[(size_t)y * (size_t)w * 4], (const std::uint8_t*)ms.pData + (size_t)y * ms.RowPitch, (size_t)w * 4);
        m_ctx->Unmap(st, 0);
        Rel(st);
        return true;
    }
    /// True after Present found the device removed or reset (a driver update, a GPU reset): the owner should make a new painter.
    bool Lost() const { return m_lost; }

private:
    struct SavedTarget { ui2d::Rect clip{0, 0, 0, 0}; bool clipOn = false; float sx = 1, sy = 1; };
    static void Fail(std::string* err, const char* why) { if (err) *err = why; }
    DTex* TT(Tex t) const { DTex* s = (DTex*)t; return s && m_texs.count(s) ? s : nullptr; }
    int TargetW() const { return m_target ? m_target->w : m_w; }
    int TargetH() const { return m_target ? m_target->h : m_h; }
    ID3D11RenderTargetView* CurrentRtv() const { return m_target ? m_target->rtv : m_backRtv; }

    bool MakeSwapChain(int w, int h, std::string* err) {
        IDXGIDevice* dxd = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory* fac = nullptr;
        bool ok = SUCCEEDED(m_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxd)) && SUCCEEDED(dxd->GetAdapter(&ad)) && SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory), (void**)&fac));
        if (ok) {
            DXGI_SWAP_CHAIN_DESC sd;
            std::memset(&sd, 0, sizeof(sd));
            sd.BufferDesc.Width = (UINT)w; sd.BufferDesc.Height = (UINT)h; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 1; sd.OutputWindow = m_hwnd; sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            ok = SUCCEEDED(fac->CreateSwapChain(m_dev, &sd, &m_swap));
            if (ok) fac->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);   // the app does its own fullscreen toggling
        }
        Rel(fac); Rel(ad); Rel(dxd);
        if (!ok) { Fail(err, "could not create the swap chain"); return false; }
        m_w = w; m_h = h;
        return MakeBackView(err);
    }
    bool MakeBackView(std::string* err) {
        Rel(m_backRtv); Rel(m_back);
        if (FAILED(m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&m_back)) || FAILED(m_dev->CreateRenderTargetView(m_back, nullptr, &m_backRtv))) { Fail(err, "could not get the back buffer"); return false; }
        return true;
    }
    void SyncSize() {
        if (m_target || !m_swap) return;
        int w = 0, h = 0;
        m_win->GetDrawableSize(w, h);
        if (w < 1 || h < 1 || (w == m_w && h == m_h)) return;
        Submit();
        m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
        Rel(m_backRtv); Rel(m_back);
        if (FAILED(m_swap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0))) { m_lost = true; return; }
        m_w = w; m_h = h;
        MakeBackView(nullptr);
    }
    bool MakePipeline(std::string* err) {
        ID3DBlob *vsb = nullptr, *psb = nullptr, *eb = nullptr;
        const SIZE_T len = std::strlen(kHlsl);
        bool ok = SUCCEEDED(m_compile(kHlsl, len, "okay_painter", nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsb, &eb)) &&
                  SUCCEEDED(m_compile(kHlsl, len, "okay_painter", nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psb, &eb));
        if (ok) {
            ok = SUCCEEDED(m_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &m_vs)) &&
                 SUCCEEDED(m_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &m_ps));
            const D3D11_INPUT_ELEMENT_DESC el[3] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            static_assert(sizeof(d3::Vtx) == 20, "vertex layout");
            ok = ok && SUCCEEDED(m_dev->CreateInputLayout(el, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &m_layout));
        } else if (eb) { Fail(err, (const char*)eb->GetBufferPointer()); }
        Rel(vsb); Rel(psb); Rel(eb);
        if (!ok) { if (err && err->empty()) *err = "could not build the painter's shaders"; return false; }
        D3D11_BUFFER_DESC bd;
        std::memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = (UINT)(kMaxVerts * sizeof(d3::Vtx)); bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok = SUCCEEDED(m_dev->CreateBuffer(&bd, nullptr, &m_vb));
        bd.ByteWidth = 16; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ok = ok && SUCCEEDED(m_dev->CreateBuffer(&bd, nullptr, &m_cb));
        D3D11_BLEND_DESC bl;
        std::memset(&bl, 0, sizeof(bl));
        bl.RenderTarget[0].BlendEnable = TRUE;
        bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ok = ok && SUCCEEDED(m_dev->CreateBlendState(&bl, &m_blendOn));
        bl.RenderTarget[0].BlendEnable = FALSE;
        ok = ok && SUCCEEDED(m_dev->CreateBlendState(&bl, &m_blendOff));
        D3D11_RASTERIZER_DESC rd;
        std::memset(&rd, 0, sizeof(rd));
        rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.ScissorEnable = TRUE; rd.DepthClipEnable = TRUE;
        ok = ok && SUCCEEDED(m_dev->CreateRasterizerState(&rd, &m_raster));
        D3D11_DEPTH_STENCIL_DESC dd;
        std::memset(&dd, 0, sizeof(dd));
        ok = ok && SUCCEEDED(m_dev->CreateDepthStencilState(&dd, &m_depth));
        D3D11_SAMPLER_DESC sm;
        std::memset(&sm, 0, sizeof(sm));
        sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sm.ComparisonFunc = D3D11_COMPARISON_ALWAYS; sm.MaxLOD = D3D11_FLOAT32_MAX;
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;  ok = ok && SUCCEEDED(m_dev->CreateSamplerState(&sm, &m_sampNear));
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; ok = ok && SUCCEEDED(m_dev->CreateSamplerState(&sm, &m_sampLin));
        // a 1 x 1 white texture for the untextured drawing
        const std::uint8_t white[4] = {255, 255, 255, 255};
        D3D11_TEXTURE2D_DESC td;
        std::memset(&td, 0, sizeof(td));
        td.Width = td.Height = 1; td.MipLevels = td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {white, 4, 0};
        ok = ok && SUCCEEDED(m_dev->CreateTexture2D(&td, &sd, &m_whiteTex)) && SUCCEEDED(m_dev->CreateShaderResourceView(m_whiteTex, nullptr, &m_whiteSrv));
        if (!ok) { Fail(err, "could not create the painter's Direct3D objects"); return false; }
        return true;
    }

    // ---- batching ---------------------------------------------------------------------------
    // The pending triangles use one texture (null = the white texture), one blend mode and one scissor; anything different submits first.
    void Begin(DTex* tex, BlendMode blend) {
        SyncSize();
        const d3::DevRect sc = d3::Scissor(m_clipOn, m_clip, m_sx, m_sy, TargetW(), TargetH());
        const bool same = m_batch.empty() || (m_bTex == tex && m_bBlend == blend && m_bScissor.x0 == sc.x0 && m_bScissor.y0 == sc.y0 && m_bScissor.x1 == sc.x1 && m_bScissor.y1 == sc.y1 &&
                                              (!tex || m_bLinear == (tex->scale == ScaleMode::Linear)));
        if (!same) Submit();
        if (m_batch.empty()) { m_bTex = tex; m_bBlend = blend; m_bScissor = sc; m_bLinear = tex && tex->scale == ScaleMode::Linear; }
    }
    void FillAll() {   // FillRect(null): the whole target (clip not applied, like the other painters)
        Begin(nullptr, m_blend);
        d3::FillDev(m_batch, d3::DevRect{0, 0, TargetW(), TargetH()}, m_r, m_g, m_b, m_a);
    }
    void Blit(Tex tex, const ui2d::Rect* src, double dx, double dy, double dw, double dh, double angle, const ui2d::FPoint* centre, int flip) {
        DTex* t = TT(tex);
        if (!t || t == m_target) return;
        Begin(t, t->blend);
        if (!d3::TexQuad(m_batch, t->w, t->h, src, dx, dy, dw, dh, m_sx, m_sy, angle, centre, flip, t->mr, t->mg, t->mb, t->ma) ) { /* nothing was added */ }
    }
    void Submit() {
        if (m_batch.empty() || !m_ctx || !m_swap) { m_batch.clear(); return; }
        if (m_bScissor.Empty()) { m_batch.clear(); return; }
        ID3D11RenderTargetView* rtv = CurrentRtv();
        if (!rtv) { m_batch.clear(); return; }
        const int tw = TargetW(), th = TargetH();
        m_ctx->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp = {0, 0, (float)tw, (float)th, 0, 1};
        m_ctx->RSSetViewports(1, &vp);
        D3D11_RECT sr = {m_bScissor.x0, m_bScissor.y0, m_bScissor.x1, m_bScissor.y1};
        m_ctx->RSSetScissorRects(1, &sr);
        D3D11_MAPPED_SUBRESOURCE cb;
        if (SUCCEEDED(m_ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &cb))) { float* f = (float*)cb.pData; f[0] = 2.0f / (float)tw; f[1] = 2.0f / (float)th; f[2] = f[3] = 0; m_ctx->Unmap(m_cb, 0); }
        const FLOAT bf[4] = {0, 0, 0, 0};
        m_ctx->OMSetBlendState(m_bBlend == BlendMode::Blend ? m_blendOn : m_blendOff, bf, 0xffffffff);
        m_ctx->OMSetDepthStencilState(m_depth, 0);
        m_ctx->RSSetState(m_raster);
        m_ctx->IASetInputLayout(m_layout);
        m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_ctx->VSSetShader(m_vs, nullptr, 0);
        m_ctx->PSSetShader(m_ps, nullptr, 0);
        m_ctx->VSSetConstantBuffers(0, 1, &m_cb);
        ID3D11ShaderResourceView* srv = m_bTex ? m_bTex->srv : m_whiteSrv;
        m_ctx->PSSetShaderResources(0, 1, &srv);
        ID3D11SamplerState* smp = m_bLinear ? m_sampLin : m_sampNear;
        m_ctx->PSSetSamplers(0, 1, &smp);
        const UINT stride = sizeof(d3::Vtx), offset = 0;
        m_ctx->IASetVertexBuffers(0, 1, &m_vb, &stride, &offset);
        size_t done = 0;
        while (done < m_batch.size()) {   // the dynamic vertex buffer holds kMaxVerts: send the batch in pieces (whole triangles)
            const size_t n = std::min<size_t>(kMaxVerts - kMaxVerts % 3, m_batch.size() - done);
            D3D11_MAPPED_SUBRESOURCE vb;
            if (FAILED(m_ctx->Map(m_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &vb))) break;
            std::memcpy(vb.pData, &m_batch[done], n * sizeof(d3::Vtx));
            m_ctx->Unmap(m_vb, 0);
            m_ctx->Draw((UINT)n, 0);
            done += n;
        }
        ID3D11ShaderResourceView* none = nullptr;   // never leave a texture bound that may become the render target next
        m_ctx->PSSetShaderResources(0, 1, &none);
        m_batch.clear();
    }
    void SaveTargetState() { SavedTarget& s = m_target ? m_savedTex : m_savedWin; (void)s; m_stash = SavedTarget{m_clip, m_clipOn, m_sx, m_sy}; }
    void RestoreTargetState() {   // clip and scale are per target: the window keeps its own, a render target starts clean
        if (m_target) { m_savedWin = m_stash; m_clipOn = false; m_sx = m_sy = 1.0f; }
        else { m_clip = m_savedWin.clip; m_clipOn = m_savedWin.clipOn; m_sx = m_savedWin.sx; m_sy = m_savedWin.sy; }
    }

    static constexpr size_t kMaxVerts = 1 << 16;
    Window* m_win;
    HWND m_hwnd = nullptr;
    HMODULE m_d3dDll = nullptr, m_compDll = nullptr;
    pD3DCompile m_compile = nullptr;
    ID3D11Device* m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    IDXGISwapChain* m_swap = nullptr;
    ID3D11Texture2D* m_back = nullptr;
    ID3D11RenderTargetView* m_backRtv = nullptr;
    ID3D11Buffer *m_vb = nullptr, *m_cb = nullptr;
    ID3D11BlendState *m_blendOn = nullptr, *m_blendOff = nullptr;
    ID3D11RasterizerState* m_raster = nullptr;
    ID3D11DepthStencilState* m_depth = nullptr;
    ID3D11SamplerState *m_sampLin = nullptr, *m_sampNear = nullptr;
    ID3D11InputLayout* m_layout = nullptr;
    ID3D11VertexShader* m_vs = nullptr;
    ID3D11PixelShader* m_ps = nullptr;
    ID3D11Texture2D* m_whiteTex = nullptr;
    ID3D11ShaderResourceView* m_whiteSrv = nullptr;
    std::unordered_set<DTex*> m_texs;
    DTex* m_target = nullptr;
    std::vector<d3::Vtx> m_batch;
    DTex* m_bTex = nullptr; BlendMode m_bBlend = BlendMode::Blend; d3::DevRect m_bScissor; bool m_bLinear = false;
    std::uint8_t m_r = 0, m_g = 0, m_b = 0, m_a = 255;
    BlendMode m_blend = BlendMode::Blend;
    ui2d::Rect m_clip{0, 0, 0, 0}; bool m_clipOn = false;
    float m_sx = 1, m_sy = 1;
    SavedTarget m_savedWin, m_savedTex, m_stash;
    int m_w = 1, m_h = 1;
    bool m_vsync = true, m_warp = false, m_lost = false;
};

} // namespace

std::unique_ptr<Painter> CreateD3D11Painter(Window& w, const PainterDesc& d, std::string* err) {
    if (w.Kind() != BackendKind::Own || !w.Win32Handle()) {
        if (err) *err = "the Direct3D 11 painter needs a window of the own Win32 backend";
        return nullptr;
    }
    std::unique_ptr<D3D11Painter> p(new D3D11Painter(w, d.vsync));
    if (!p->Init(err)) return nullptr;
    return p;
}

} // namespace okay::platform

#else // not Windows

namespace okay::platform {
std::unique_ptr<Painter> CreateD3D11Painter(Window&, const PainterDesc&, std::string* err) {
    if (err) *err = "Direct3D 11 exists only on Windows";
    return nullptr;
}
}

#endif
