// RHI Direct3D 11 backend. It runs on the D3D11 renderer's own ID3D11Device and immediate
// context (CreateD3D11Device takes the device; there is no second device and no loader:
// D3DCompile comes from d3dcompiler, which the engine already links on Windows).
//
// Mapping:
//   Buffer -> ID3D11Buffer (default usage, UpdateSubresource for updates);
//   Texture -> ID3D11Texture2D with the views its use needs (SRV, RTV, DSV; a sampled depth
//   texture is typeless R24G8 so it can be both); Sampler -> ID3D11SamplerState;
//   Shader -> vertex + pixel shader compiled from ShaderDesc::hlsl*, plus one dynamic constant
//   buffer for the uniform block (bound at b0 of both stages); Pipeline -> input layout
//   (location i = semantic TEXCOORDi) + blend, depth-stencil and rasterizer state objects
//   (Direct3D 11 hands back the same object for an identical description, so no cache here);
//   render pass -> OMSetRenderTargets + clears.
//
// Conventions. Texture rows are stored as given: RHI row 0 is Direct3D row 0. Direct3D puts
// NDC y = +1 at row 0 and wants depth z/w in 0..1, so rhiClip() (kHlslPrelude) flips y and
// maps z to (z + w) / 2. With the rows the same, texture coordinates, viewports
// (TopLeftY = the RHI's y) and SV_Position (= the RHI fragCoord) need no change, and the RHI's
// counter-clockwise front faces (y up) are Direct3D's clockwise ones (its y goes down), the
// default FrontCounterClockwise = FALSE.
//
// Submit saves the context state it touches and puts it back at the end, so the renderer
// around it sees no change.
//
// Written by us from the Direct3D 11 documentation (resources, views, pipeline states,
// ID3D11DeviceContext) and the RHI design (docs/rendering/rhi.md). UNVERIFIED: compile-checked
// with MinGW only, not run on Windows yet.
#include "okay/Render/RHI/RHI.hpp"

namespace okay {
namespace rhi {

const char* const kHlslPrelude =
    "#define OKAY_RHI_D3D 1\n"
    "float4 rhiClip(float4 p) { return float4(p.x, -p.y, (p.z + p.w) * 0.5, p.w); }\n";

} // namespace rhi
} // namespace okay

#if !defined(_WIN32)

namespace okay {
namespace rhi {
std::unique_ptr<Device> CreateD3D11Device(void*) { return nullptr; }
TextureHandle WrapD3D11Texture(Device&, void*, const TextureDesc&) { return {}; }
} // namespace rhi
} // namespace okay

#else

#include "okay/Core/Log.hpp"
#include "BackendTables.hpp"
#include "okay/Texture/Convert.hpp"   // FloatToHalf / HalfToFloat (RGBA16F upload and read-back)

#include <d3d11.h>
#if !defined(OKAY_XBOX_UWP)
#include <d3dcompiler.h>
#endif

#include <cstring>
#include <unordered_map>
#include <utility>

namespace okay {
namespace rhi {
namespace {

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

struct DBuf { ID3D11Buffer* buf = nullptr; std::size_t size = 0; };
struct DTex {
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    int w = 0, h = 0; Format fmt = Format::RGBA8;
    bool rt = false, sampled = true;
    std::uint32_t swapChain = 0;   // back buffer of this swap chain (goes with it)
    void Release() { SafeRelease(srv); SafeRelease(rtv); SafeRelease(dsv); SafeRelease(tex); }
};
struct DShader {
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* cb = nullptr; std::uint32_t cbBytes = 0;
    std::vector<unsigned char> vsCode;   // for input layouts
    void Release() { SafeRelease(vs); SafeRelease(ps); SafeRelease(cb); }
};
struct DPipe {
    PipelineDesc desc;
    ID3D11InputLayout* layout = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    void Release() { SafeRelease(layout); SafeRelease(blend); SafeRelease(depth); SafeRelease(raster); }
};

DXGI_FORMAT TexFormat(Format f, bool sampledDepth) {
    switch (f) {
        case Format::RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case Format::RGBA32F: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::Depth: return sampledDepth ? DXGI_FORMAT_R24G8_TYPELESS : DXGI_FORMAT_D24_UNORM_S8_UINT;
    }
    return DXGI_FORMAT_R8G8B8A8_UNORM;
}
int GpuBytesPerPixel(Format f) { return f == Format::RGBA8 || f == Format::Depth ? 4 : (f == Format::RGBA16F ? 8 : 16); }

D3D11_BLEND BlendD3D(BlendFactor f, bool alpha) {
    // The alpha factors may not name a colour (Direct3D refuses *_COLOR there); for the alpha
    // channel OpenGL's SRC_COLOR reads the source alpha anyway, so the alpha twin is the same maths.
    switch (f) {
        case BlendFactor::Zero: return D3D11_BLEND_ZERO;
        case BlendFactor::One: return D3D11_BLEND_ONE;
        case BlendFactor::SrcColor: return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
        case BlendFactor::DstColor: return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
        case BlendFactor::OneMinusDstColor: return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
        case BlendFactor::SrcAlpha: return D3D11_BLEND_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstAlpha: return D3D11_BLEND_DEST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
    }
    return D3D11_BLEND_ONE;
}

D3D11_COMPARISON_FUNC CompareD3D(CompareOp c) {
    switch (c) {
        case CompareOp::Never: return D3D11_COMPARISON_NEVER;
        case CompareOp::Less: return D3D11_COMPARISON_LESS;
        case CompareOp::Equal: return D3D11_COMPARISON_EQUAL;
        case CompareOp::LessEqual: return D3D11_COMPARISON_LESS_EQUAL;
        case CompareOp::Greater: return D3D11_COMPARISON_GREATER;
        case CompareOp::NotEqual: return D3D11_COMPARISON_NOT_EQUAL;
        case CompareOp::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
        case CompareOp::Always: return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_LESS_EQUAL;
}

DXGI_FORMAT VertexFormatD3D(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float1: return DXGI_FORMAT_R32_FLOAT;
        case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
        case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case VertexFormat::UNorm8x4: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return DXGI_FORMAT_R32G32B32_FLOAT;
}

// CPU pixels (BytesPerPixel layout) -> GPU rows for UpdateSubresource / initial data.
void ToGpu(Format f, const void* src, int w, int h, std::vector<unsigned char>& out) {
    const std::size_t n = (std::size_t)w * h * 4;
    if (f == Format::RGBA16F) {
        out.resize(n * 2);
        const float* s = static_cast<const float*>(src);
        std::uint16_t* d = reinterpret_cast<std::uint16_t*>(out.data());
        for (std::size_t i = 0; i < n; ++i) d[i] = tex::FloatToHalf(s[i]);
    } else {
        const std::size_t bytes = n * (f == Format::RGBA32F ? 4 : 1);
        out.assign(static_cast<const unsigned char*>(src), static_cast<const unsigned char*>(src) + bytes);
    }
}

// The context state Submit changes, saved and put back (every Get* adds a reference).
struct SavedState {
    ID3D11InputLayout* layout = nullptr; D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* vb = nullptr; UINT vbStride = 0, vbOffset = 0;
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOffset = 0;
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* vsCb = nullptr; ID3D11Buffer* psCb = nullptr;
    ID3D11ShaderResourceView* srv[kMaxTextureSlots] = {};
    ID3D11SamplerState* smp[kMaxTextureSlots] = {};
    ID3D11RenderTargetView* rtv = nullptr; ID3D11DepthStencilView* dsv = nullptr;
    ID3D11BlendState* blend = nullptr; FLOAT blendFactor[4] = {0, 0, 0, 0}; UINT sampleMask = 0xffffffff;
    ID3D11DepthStencilState* depth = nullptr; UINT stencilRef = 0;
    ID3D11RasterizerState* raster = nullptr;
    D3D11_VIEWPORT vp[1]; UINT vpCount = 1;

    void Save(ID3D11DeviceContext* c) {
        c->IAGetInputLayout(&layout); c->IAGetPrimitiveTopology(&topo);
        c->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
        c->IAGetIndexBuffer(&ib, &ibFmt, &ibOffset);
        c->VSGetShader(&vs, nullptr, nullptr); c->PSGetShader(&ps, nullptr, nullptr);
        c->VSGetConstantBuffers(0, 1, &vsCb); c->PSGetConstantBuffers(0, 1, &psCb);
        c->PSGetShaderResources(0, kMaxTextureSlots, srv); c->PSGetSamplers(0, kMaxTextureSlots, smp);
        c->OMGetRenderTargets(1, &rtv, &dsv);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&depth, &stencilRef);
        c->RSGetState(&raster);
        std::memset(vp, 0, sizeof(vp)); vpCount = 1; c->RSGetViewports(&vpCount, vp);
    }
    void Restore(ID3D11DeviceContext* c) {
        ID3D11ShaderResourceView* none[kMaxTextureSlots] = {};
        c->PSSetShaderResources(0, kMaxTextureSlots, none);   // never an SRV that aliases the restored target
        c->OMSetRenderTargets(1, &rtv, dsv);
        c->IASetInputLayout(layout); c->IASetPrimitiveTopology(topo);
        c->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
        c->IASetIndexBuffer(ib, ibFmt, ibOffset);
        c->VSSetShader(vs, nullptr, 0); c->PSSetShader(ps, nullptr, 0);
        c->VSSetConstantBuffers(0, 1, &vsCb); c->PSSetConstantBuffers(0, 1, &psCb);
        c->PSSetShaderResources(0, kMaxTextureSlots, srv); c->PSSetSamplers(0, kMaxTextureSlots, smp);
        c->OMSetBlendState(blend, blendFactor, sampleMask);
        c->OMSetDepthStencilState(depth, stencilRef);
        c->RSSetState(raster);
        if (vpCount) c->RSSetViewports(vpCount, vp);
        SafeRelease(layout); SafeRelease(vb); SafeRelease(ib); SafeRelease(vs); SafeRelease(ps);
        SafeRelease(vsCb); SafeRelease(psCb);
        for (int i = 0; i < kMaxTextureSlots; ++i) { SafeRelease(srv[i]); SafeRelease(smp[i]); }
        SafeRelease(rtv); SafeRelease(dsv); SafeRelease(blend); SafeRelease(depth); SafeRelease(raster);
    }
};

class D3D11Device final : public Device {
public:
    explicit D3D11Device(ID3D11Device* dev) : m_dev(dev) {
        m_dev->AddRef();
        m_dev->GetImmediateContext(&m_ctx);   // adds a reference
        const D3D_FEATURE_LEVEL fl = m_dev->GetFeatureLevel();
        m_caps.backend = Backend::D3D11;
        m_caps.deviceName = fl >= D3D_FEATURE_LEVEL_11_0 ? "Direct3D 11 (feature level 11)" : "Direct3D 11 (feature level 10)";
        m_caps.maxTextureSize = fl >= D3D_FEATURE_LEVEL_11_0 ? 16384 : 8192;
        m_caps.floatRenderTargets = true;   // RGBA16F / RGBA32F targets are required from feature level 10
        m_caps.depthTextures = true;
        m_caps.indexU32 = true;
        m_caps.drawBaseVertex = true;
        m_caps.needsBarriers = false;       // the driver tracks hazards (it unbinds an SRV bound as a target)
        m_caps.framesInFlight = 1;          // releasing a resource still in use is safe: the driver keeps it
    }
    ~D3D11Device() override {
        ReleaseAllDeferred();
        for (auto& kv : m_pipes) kv.second.Release();
        for (auto& kv : m_shaders) kv.second.Release();
        for (auto& kv : m_buffers) SafeRelease(kv.second.buf);
        for (auto& kv : m_textures) kv.second.Release();
        for (auto& kv : m_samplers) SafeRelease(kv.second);
        SafeRelease(m_ctx);
        SafeRelease(m_dev);
    }

    const Caps& GetCaps() const override { return m_caps; }

    BufferHandle CreateBuffer(const BufferDesc& d) override {
        if (d.size == 0 || d.size > 0x7fffffffu) { SetError("CreateBuffer: bad size"); return {}; }
        D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = (UINT)d.size; bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = d.usage == BufferUsage::Index ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd; std::memset(&sd, 0, sizeof(sd)); sd.pSysMem = d.initialData;
        DBuf b; b.size = d.size;
        if (FAILED(m_dev->CreateBuffer(&bd, d.initialData ? &sd : nullptr, &b.buf)) || !b.buf) { SetError("CreateBuffer: ID3D11Device::CreateBuffer failed"); return {}; }
        BufferHandle h{NextId()}; m_buffers[h.id] = b; return h;
    }

    TextureHandle CreateTexture(const TextureDesc& d) override {
        if (d.width <= 0 || d.height <= 0 || d.width > m_caps.maxTextureSize || d.height > m_caps.maxTextureSize) {
            SetError("CreateTexture: bad size"); return {};
        }
        const bool depth = IsDepth(d.format);
        if (depth && d.initialData) { SetError("CreateTexture: a depth texture takes no initial data"); return {}; }
        D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof(td));
        td.Width = (UINT)d.width; td.Height = (UINT)d.height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = TexFormat(d.format, depth && d.sampled);
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        if (d.sampled) td.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        if (depth) td.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
        else if (d.renderTarget) td.BindFlags |= D3D11_BIND_RENDER_TARGET;
        std::vector<unsigned char> px;
        D3D11_SUBRESOURCE_DATA sd; std::memset(&sd, 0, sizeof(sd));
        if (d.initialData) {
            ToGpu(d.format, d.initialData, d.width, d.height, px);
            sd.pSysMem = px.data(); sd.SysMemPitch = (UINT)(d.width * GpuBytesPerPixel(d.format));
        }
        DTex t; t.w = d.width; t.h = d.height; t.fmt = d.format; t.rt = d.renderTarget || depth; t.sampled = d.sampled;
        if (FAILED(m_dev->CreateTexture2D(&td, d.initialData ? &sd : nullptr, &t.tex)) || !t.tex) {
            SetError("CreateTexture: ID3D11Device::CreateTexture2D failed"); return {};
        }
        if (!MakeViews(t)) { t.Release(); return {}; }
        TextureHandle h{NextId()}; m_textures[h.id] = t; return h;
    }

    TextureHandle Wrap(ID3D11Texture2D* tex, const TextureDesc& d) {
        if (!tex || d.width <= 0 || d.height <= 0 || IsDepth(d.format)) { SetError("WrapD3D11Texture: bad texture"); return {}; }
        DTex t; t.tex = tex; tex->AddRef();
        t.w = d.width; t.h = d.height; t.fmt = d.format; t.rt = d.renderTarget; t.sampled = d.sampled;
        if (!MakeViews(t)) { t.Release(); return {}; }
        TextureHandle h{NextId()}; m_textures[h.id] = t; return h;
    }

    SamplerHandle CreateSampler(const SamplerDesc& d) override {
        D3D11_SAMPLER_DESC sd; std::memset(&sd, 0, sizeof(sd));
        const bool minL = d.minFilter == Filter::Linear, magL = d.magFilter == Filter::Linear;
        sd.Filter = minL ? (magL ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT)
                         : (magL ? D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_POINT);
        sd.AddressU = d.wrapU == Wrap::Repeat ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = d.wrapV == Wrap::Repeat ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxAnisotropy = 1; sd.MinLOD = 0.0f; sd.MaxLOD = D3D11_FLOAT32_MAX;
        ID3D11SamplerState* s = nullptr;
        if (FAILED(m_dev->CreateSamplerState(&sd, &s)) || !s) { SetError("CreateSampler: CreateSamplerState failed"); return {}; }
        SamplerHandle h{NextId()}; m_samplers[h.id] = s; return h;
    }

    ShaderHandle CreateShader(const ShaderDesc& d) override;
    PipelineHandle CreatePipeline(const PipelineDesc& d) override;

    SwapChainHandle CreateSwapChain(int width, int height) override {
        // Off-screen for now (like Software): the host copies or reads the back buffer.
        TextureDesc td; td.width = width; td.height = height; td.renderTarget = true; td.sampled = true; td.debugName = "back buffer";
        const TextureHandle t = CreateTexture(td);
        if (!t) return {};
        SwapChainHandle s{NextId()};
        m_textures[t.id].swapChain = s.id;
        m_swap[s.id] = t.id;
        return s;
    }

    bool UpdateBuffer(BufferHandle h, std::size_t offset, const void* data, std::size_t bytes) override {
        auto it = m_buffers.find(h.id);
        if (it == m_buffers.end() || !data || offset + bytes > it->second.size) { SetError("UpdateBuffer: bad buffer or range"); return false; }
        if (!bytes) return true;
        D3D11_BOX box; box.left = (UINT)offset; box.right = (UINT)(offset + bytes); box.top = 0; box.bottom = 1; box.front = 0; box.back = 1;
        m_ctx->UpdateSubresource(it->second.buf, 0, &box, data, 0, 0);
        return true;
    }
    bool UpdateTexture(TextureHandle h, const void* pixels) override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end() || !pixels || IsDepth(it->second.fmt)) { SetError("UpdateTexture: bad texture"); return false; }
        const DTex& t = it->second;
        std::vector<unsigned char> px; ToGpu(t.fmt, pixels, t.w, t.h, px);
        m_ctx->UpdateSubresource(t.tex, 0, nullptr, px.data(), (UINT)(t.w * GpuBytesPerPixel(t.fmt)), 0);
        return true;
    }
    bool ReadTexture(TextureHandle h, std::vector<std::uint8_t>& out) override {
        auto it = m_textures.find(h.id);
        if (it == m_textures.end() || IsDepth(it->second.fmt)) { SetError("ReadTexture: bad texture (depth cannot be read back on Direct3D 11 yet)"); return false; }
        const DTex& t = it->second;
        D3D11_TEXTURE2D_DESC sd; t.tex->GetDesc(&sd);
        sd.MipLevels = 1; sd.ArraySize = 1; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
        sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
        ID3D11Texture2D* st = nullptr;
        if (FAILED(m_dev->CreateTexture2D(&sd, nullptr, &st)) || !st) { SetError("ReadTexture: staging texture failed"); return false; }
        m_ctx->CopySubresourceRegion(st, 0, 0, 0, 0, t.tex, 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE ms;
        if (FAILED(m_ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms))) { st->Release(); SetError("ReadTexture: Map failed"); return false; }
        const int cpuBpp = BytesPerPixel(t.fmt);
        out.assign((std::size_t)t.w * t.h * cpuBpp, 0);
        for (int y = 0; y < t.h; ++y) {
            const unsigned char* src = static_cast<const unsigned char*>(ms.pData) + (std::size_t)y * ms.RowPitch;
            unsigned char* dst = out.data() + (std::size_t)y * t.w * cpuBpp;
            if (t.fmt == Format::RGBA16F) {
                for (int i = 0; i < t.w * 4; ++i) {
                    std::uint16_t hv; std::memcpy(&hv, src + i * 2, 2);
                    const float f = tex::HalfToFloat(hv);
                    std::memcpy(dst + i * 4, &f, 4);
                }
            } else {
                std::memcpy(dst, src, (std::size_t)t.w * cpuBpp);
            }
        }
        m_ctx->Unmap(st, 0);
        st->Release();
        return true;
    }
    bool TextureSize(TextureHandle h, int& w, int& hh) const override { return table::TextureSize(m_textures, h.id, w, hh); }
    TextureHandle BackBuffer(SwapChainHandle s) override { return table::BackBuffer(m_swap, s.id); }
    bool ResizeSwapChain(SwapChainHandle s, int width, int height) override {
        auto it = m_swap.find(s.id);
        if (it == m_swap.end() || width <= 0 || height <= 0) return false;
        DTex& old = m_textures[it->second];
        if (old.w == width && old.h == height) return true;
        TextureDesc td; td.width = width; td.height = height; td.renderTarget = true; td.sampled = true;
        const TextureHandle t = CreateTexture(td);
        if (!t) return false;
        DTex fresh = m_textures[t.id];
        m_textures.erase(t.id);
        old.Release();
        fresh.swapChain = s.id;
        old = fresh;   // the back buffer keeps its handle
        return true;
    }
    bool Present(SwapChainHandle s) override { return m_swap.count(s.id) != 0; }
    std::size_t LiveResources() const override {
        return table::Count(m_buffers, m_textures, m_samplers, m_shaders, m_pipes, m_swap);
    }
    bool Submit(const CommandList& list) override;

protected:
    void Release(Kind k, std::uint32_t id) override {
        switch (k) {
            case Kind::Buffer: table::Erase(m_buffers, id, [](DBuf& b) { SafeRelease(b.buf); }); break;
            case Kind::Texture: {
                auto it = m_textures.find(id);
                if (it == m_textures.end() || it->second.swapChain) break;   // a back buffer goes with its swap chain
                it->second.Release(); m_textures.erase(it); break;
            }
            case Kind::Sampler: table::Erase(m_samplers, id, [](ID3D11SamplerState*& s) { SafeRelease(s); }); break;
            case Kind::Shader: table::Erase(m_shaders, id, [](DShader& s) { s.Release(); }); break;
            case Kind::Pipeline: table::Erase(m_pipes, id, [](DPipe& p) { p.Release(); }); break;
            case Kind::SwapChain: {
                auto it = m_swap.find(id);
                if (it != m_swap.end()) {
                    auto t = m_textures.find(it->second);
                    if (t != m_textures.end()) { t->second.Release(); m_textures.erase(t); }
                    m_swap.erase(it);
                }
                break;
            }
        }
    }

private:
    bool MakeViews(DTex& t) {
        if (IsDepth(t.fmt)) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dv; std::memset(&dv, 0, sizeof(dv));
            dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            if (FAILED(m_dev->CreateDepthStencilView(t.tex, &dv, &t.dsv))) { SetError("CreateTexture: depth view failed"); return false; }
            if (t.sampled) {
                D3D11_SHADER_RESOURCE_VIEW_DESC sv; std::memset(&sv, 0, sizeof(sv));
                sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                sv.Texture2D.MipLevels = 1;
                if (FAILED(m_dev->CreateShaderResourceView(t.tex, &sv, &t.srv))) { SetError("CreateTexture: depth shader view failed"); return false; }
            }
            return true;
        }
        if (t.sampled && FAILED(m_dev->CreateShaderResourceView(t.tex, nullptr, &t.srv))) { SetError("texture: shader view failed (not made to be sampled?)"); return false; }
        if (t.rt && FAILED(m_dev->CreateRenderTargetView(t.tex, nullptr, &t.rtv))) { SetError("texture: render target view failed (not made to be a target?)"); return false; }
        return true;
    }

    ID3D11Device* m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    Caps m_caps;
    std::unordered_map<std::uint32_t, DBuf> m_buffers;
    std::unordered_map<std::uint32_t, DTex> m_textures;
    std::unordered_map<std::uint32_t, ID3D11SamplerState*> m_samplers;
    std::unordered_map<std::uint32_t, DShader> m_shaders;
    std::unordered_map<std::uint32_t, DPipe> m_pipes;
    std::unordered_map<std::uint32_t, std::uint32_t> m_swap;   // swap chain -> its back buffer texture
};

#if !defined(OKAY_XBOX_UWP)
bool CompileStage(const std::string& body, const char* entry, const char* profile, const char* name,
                  std::vector<unsigned char>& out, std::string& error) {
    const std::string src = std::string(kHlslPrelude) + body;
    ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(src.c_str(), (SIZE_T)src.size(), name, nullptr, nullptr, entry, profile, 0, 0, &blob, &err)) || !blob) {
        error = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : std::string("unknown error");
        if (err) err->Release();
        if (blob) blob->Release();
        return false;
    }
    if (err) err->Release();
    const unsigned char* p = static_cast<const unsigned char*>(blob->GetBufferPointer());
    out.assign(p, p + blob->GetBufferSize());
    blob->Release();
    return true;
}
#endif

ShaderHandle D3D11Device::CreateShader(const ShaderDesc& d) {
    const std::string err = ValidateShaderDesc(d);
    if (!err.empty()) { SetError("CreateShader: " + err); return {}; }
    if (d.hlslVertex.empty() || d.hlslFragment.empty()) { SetError(std::string("CreateShader '") + d.debugName + "': no HLSL for the Direct3D 11 backend"); return {}; }
    const std::string herr = ValidateHlslDesc(d);
    if (!herr.empty()) { SetError(std::string("CreateShader '") + d.debugName + "': " + herr); return {}; }
#if defined(OKAY_XBOX_UWP)
    SetError(std::string("CreateShader '") + d.debugName + "': a UWP app has no run-time HLSL compiler (RHI shaders are not in the shader bundle yet)");
    return {};
#else
    std::vector<unsigned char> vsCode, psCode;
    std::string e;
    if (!CompileStage(d.hlslVertex, "VSMain", "vs_4_0", d.debugName, vsCode, e) ||
        !CompileStage(d.hlslFragment, "PSMain", "ps_4_0", d.debugName, psCode, e)) {
        Log::Error("[rhi d3d11] HLSL '", d.debugName, "': ", e);
        SetError(std::string("CreateShader '") + d.debugName + "': HLSL did not compile (see the log)");
        return {};
    }
    DShader s;
    s.vsCode = std::move(vsCode);
    if (FAILED(m_dev->CreateVertexShader(s.vsCode.data(), s.vsCode.size(), nullptr, &s.vs)) ||
        FAILED(m_dev->CreatePixelShader(psCode.data(), psCode.size(), nullptr, &s.ps))) {
        s.Release(); SetError(std::string("CreateShader '") + d.debugName + "': the device refused the bytecode"); return {};
    }
    if (d.uniformBytes) {
        s.cbBytes = (d.uniformBytes + 15u) & ~15u;   // constant buffers are whole 16-byte registers
        D3D11_BUFFER_DESC bd; std::memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = s.cbBytes; bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &s.cb))) { s.Release(); SetError("CreateShader: constant buffer failed"); return {}; }
    }
    ShaderHandle h{NextId()}; m_shaders[h.id] = std::move(s); return h;
#endif
}

PipelineHandle D3D11Device::CreatePipeline(const PipelineDesc& d) {
    auto sh = m_shaders.find(d.shader.id);
    if (!CheckPipeline(d, sh != m_shaders.end())) return {};
    DPipe p; p.desc = d;
    if (!d.layout.attributes.empty()) {
        static const char* const kSemantic = "TEXCOORD";
        std::vector<D3D11_INPUT_ELEMENT_DESC> el;
        for (const VertexAttribute& a : d.layout.attributes) {
            D3D11_INPUT_ELEMENT_DESC e; std::memset(&e, 0, sizeof(e));
            e.SemanticName = kSemantic; e.SemanticIndex = (UINT)a.location; e.Format = VertexFormatD3D(a.format);
            e.InputSlot = 0; e.AlignedByteOffset = a.offset; e.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            el.push_back(e);
        }
        const std::vector<unsigned char>& code = sh->second.vsCode;
        if (FAILED(m_dev->CreateInputLayout(el.data(), (UINT)el.size(), code.data(), code.size(), &p.layout))) {
            SetError(std::string("CreatePipeline '") + d.debugName + "': input layout refused (the vertex shader's TEXCOORDn inputs must match the layout)");
            return {};
        }
    }
    D3D11_BLEND_DESC bd; std::memset(&bd, 0, sizeof(bd));
    D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
    rt.BlendEnable = d.blend.enable ? TRUE : FALSE;
    rt.SrcBlend = BlendD3D(d.blend.srcColor, false); rt.DestBlend = BlendD3D(d.blend.dstColor, false); rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = BlendD3D(d.blend.srcAlpha, true); rt.DestBlendAlpha = BlendD3D(d.blend.dstAlpha, true); rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = (UINT8)(d.blend.writeMask & 0xF);   // same bit order: r 1, g 2, b 4, a 8
    D3D11_DEPTH_STENCIL_DESC dd; std::memset(&dd, 0, sizeof(dd));
    dd.DepthEnable = d.depth.test ? TRUE : FALSE;
    dd.DepthWriteMask = d.depth.test && d.depth.write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = CompareD3D(d.depth.compare);
    D3D11_RASTERIZER_DESC rd; std::memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = d.cull == CullMode::None ? D3D11_CULL_NONE : (d.cull == CullMode::Back ? D3D11_CULL_BACK : D3D11_CULL_FRONT);
    rd.FrontCounterClockwise = FALSE;   // see the file comment: the RHI's CCW (y up) is Direct3D's CW
    rd.DepthClipEnable = TRUE;
    if (FAILED(m_dev->CreateBlendState(&bd, &p.blend)) || FAILED(m_dev->CreateDepthStencilState(&dd, &p.depth)) ||
        FAILED(m_dev->CreateRasterizerState(&rd, &p.raster))) {
        p.Release(); SetError(std::string("CreatePipeline '") + d.debugName + "': a state object was refused"); return {};
    }
    PipelineHandle h{NextId()}; m_pipes[h.id] = p; return h;
}

bool D3D11Device::Submit(const CommandList& list) {
    if (!list.Error().empty()) { SetError("Submit: the list has an error: " + list.Error()); return false; }
    ++m_stats.submits;
    ID3D11DeviceContext* c = m_ctx;
    SavedState saved; saved.Save(c);
    ID3D11ShaderResourceView* noSrv[kMaxTextureSlots] = {};
    const DPipe* pipe = nullptr; const DShader* shader = nullptr;
    const DBuf* vb = nullptr; std::uint32_t vbOffset = 0;
    const DBuf* ib = nullptr; IndexType ibType = IndexType::U16; std::uint32_t ibOffset = 0;
    const std::uint8_t* uniforms = nullptr; std::uint32_t uniformBytes = 0;
    bool vbDirty = true, uniformsDirty = true;
    bool ok = true;
    auto fail = [&](const std::string& e) { SetError("Submit: " + e); ok = false; };

    for (const CommandList::Command& cmd : list.Commands()) {
        if (!ok) break;
        switch (cmd.op) {
            case CommandList::Op::Transition: ++m_stats.transitions; break;
            case CommandList::Op::BeginPass: {
                const RenderPassDesc& p = list.Pass(cmd.a);
                auto ct = p.color ? m_textures.find(p.color.id) : m_textures.end();
                auto dt = p.depth ? m_textures.find(p.depth.id) : m_textures.end();
                if ((p.color && (ct == m_textures.end() || !ct->second.rtv)) ||
                    (p.depth && (dt == m_textures.end() || !dt->second.dsv))) { fail("bad render pass attachment"); break; }
                c->PSSetShaderResources(0, kMaxTextureSlots, noSrv);   // nothing bound in the last pass survives it
                ID3D11RenderTargetView* rtv = p.color ? ct->second.rtv : nullptr;
                ID3D11DepthStencilView* dsv = p.depth ? dt->second.dsv : nullptr;
                c->OMSetRenderTargets(rtv ? 1u : 0u, rtv ? &rtv : nullptr, dsv);
                const DTex& any = p.color ? ct->second : dt->second;
                D3D11_VIEWPORT v; std::memset(&v, 0, sizeof(v));
                v.Width = (FLOAT)any.w; v.Height = (FLOAT)any.h; v.MaxDepth = 1.0f;
                c->RSSetViewports(1, &v);
                if (rtv && p.colorLoad == LoadOp::Clear) c->ClearRenderTargetView(rtv, p.clearColor);
                if (dsv && p.depthLoad == LoadOp::Clear) c->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, p.clearDepth, 0);
                pipe = nullptr; shader = nullptr; vb = nullptr; ib = nullptr; uniforms = nullptr; uniformBytes = 0;
                ++m_stats.passes;
                break;
            }
            case CommandList::Op::EndPass: break;
            case CommandList::Op::Viewport: {
                // Rows are stored as given, so the RHI's bottom-left y is Direct3D's top-left y.
                D3D11_VIEWPORT v; std::memset(&v, 0, sizeof(v));
                v.TopLeftX = (FLOAT)cmd.a; v.TopLeftY = (FLOAT)cmd.b; v.Width = (FLOAT)cmd.c; v.Height = (FLOAT)cmd.d; v.MaxDepth = 1.0f;
                c->RSSetViewports(1, &v);
                break;
            }
            case CommandList::Op::Pipeline: {
                if (const char* e = table::FindPipeline(m_pipes, m_shaders, cmd.a, pipe, shader)) { fail(e); break; }
                c->IASetInputLayout(pipe->layout);
                c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                c->VSSetShader(shader->vs, nullptr, 0);
                c->PSSetShader(shader->ps, nullptr, 0);
                ID3D11Buffer* cb = shader->cb;
                c->VSSetConstantBuffers(0, 1, &cb);
                c->PSSetConstantBuffers(0, 1, &cb);
                const FLOAT bf[4] = {0, 0, 0, 0};
                c->OMSetBlendState(pipe->blend, bf, 0xffffffff);
                c->OMSetDepthStencilState(pipe->depth, 0);
                c->RSSetState(pipe->raster);
                vbDirty = uniformsDirty = true;
                ++m_stats.pipelineBinds;
                break;
            }
            case CommandList::Op::VertexBuffer: {
                auto it = m_buffers.find(cmd.a);
                if (it == m_buffers.end()) { fail("unknown vertex buffer"); break; }
                vb = &it->second; vbOffset = cmd.b; vbDirty = true; break;
            }
            case CommandList::Op::IndexBuffer: {
                auto it = m_buffers.find(cmd.a);
                if (it == m_buffers.end()) { fail("unknown index buffer"); break; }
                ib = &it->second; ibType = (IndexType)cmd.b; ibOffset = cmd.c;
                c->IASetIndexBuffer(ib->buf, ibType == IndexType::U16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, ibOffset);
                break;
            }
            case CommandList::Op::Texture: {
                const UINT slot = cmd.a;
                if (cmd.b == 0) { ID3D11ShaderResourceView* none = nullptr; c->PSSetShaderResources(slot, 1, &none); break; }
                auto it = m_textures.find(cmd.b);
                auto sm = m_samplers.find(cmd.c);
                if (it == m_textures.end() || sm == m_samplers.end()) { fail("unknown texture or sampler"); break; }
                if (!it->second.srv) { fail("texture was not made to be sampled"); break; }
                ID3D11ShaderResourceView* srv = it->second.srv;
                ID3D11SamplerState* s = sm->second;
                c->PSSetShaderResources(slot, 1, &srv);
                c->PSSetSamplers(slot, 1, &s);
                break;
            }
            case CommandList::Op::Uniforms:
                uniforms = cmd.b ? list.UniformData(cmd.a) : nullptr; uniformBytes = cmd.b; uniformsDirty = true; break;
            case CommandList::Op::Draw:
            case CommandList::Op::DrawIndexed: {
                if (!pipe) { fail("draw without a pipeline"); break; }
                const bool indexed = cmd.op == CommandList::Op::DrawIndexed;
                if (indexed && !ib) { fail("DrawIndexed without an index buffer"); break; }
                const std::uint32_t count = cmd.a - cmd.a % 3;
                const VertexLayout& L = pipe->desc.layout;
                if (vbDirty) {
                    if (!L.attributes.empty()) {
                        if (!vb) { fail("the pipeline has vertex attributes but no vertex buffer is set"); break; }
                        ID3D11Buffer* b = vb->buf; UINT stride = L.stride, off = vbOffset;
                        c->IASetVertexBuffers(0, 1, &b, &stride, &off);
                    }
                    vbDirty = false;
                }
                if (!indexed && !L.attributes.empty() && count > 0) {
                    std::size_t lastEnd = 0;   // bytes the last vertex reads past its start
                    for (const VertexAttribute& a : L.attributes) {
                        const std::size_t sz = a.format == VertexFormat::UNorm8x4 ? 4 : 4 * ((std::size_t)a.format + 1);
                        if (a.offset + sz > lastEnd) lastEnd = a.offset + sz;
                    }
                    if ((std::size_t)vbOffset + ((std::size_t)cmd.b + count - 1) * L.stride + lastEnd > vb->size) {
                        fail("draw reads past the end of the vertex buffer"); break;
                    }
                }
                if (indexed && (std::size_t)ibOffset + ((std::size_t)cmd.b + count) * (ibType == IndexType::U16 ? 2u : 4u) > ib->size) {
                    fail("draw reads past the end of the index buffer"); break;
                }
                if (uniformsDirty && shader->cb && uniforms) {
                    D3D11_MAPPED_SUBRESOURCE ms;
                    if (FAILED(c->Map(shader->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { fail("constant buffer Map failed"); break; }
                    std::memset(ms.pData, 0, shader->cbBytes);
                    std::memcpy(ms.pData, uniforms, uniformBytes < shader->cbBytes ? uniformBytes : shader->cbBytes);
                    c->Unmap(shader->cb, 0);
                    uniformsDirty = false;
                }
                if (indexed) c->DrawIndexed(count, cmd.b, (INT)(std::int32_t)cmd.c);
                else c->Draw(count, cmd.b);
                ++m_stats.draws;
                m_stats.trianglesIn += count / 3;
                break;
            }
        }
    }
    saved.Restore(c);
    return ok;
}

} // namespace

std::unique_ptr<Device> CreateD3D11Device(void* d3d11Device) {
    if (!d3d11Device) return nullptr;
    return std::unique_ptr<Device>(new D3D11Device(static_cast<ID3D11Device*>(d3d11Device)));
}

TextureHandle WrapD3D11Texture(Device& dev, void* d3d11Texture2D, const TextureDesc& d) {
    if (dev.GetBackend() != Backend::D3D11) return {};
    return static_cast<D3D11Device&>(dev).Wrap(static_cast<ID3D11Texture2D*>(d3d11Texture2D), d);
}

} // namespace rhi
} // namespace okay

#endif // _WIN32
