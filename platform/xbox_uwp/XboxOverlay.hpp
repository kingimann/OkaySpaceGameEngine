#pragma once
// The UI overlay on Direct3D 11: the software-painted 2D / UI layer (XboxSoftPainter, premultiplied RGBA) uploaded as a texture and drawn over the 3D
// frame with a full-screen triangle (VSOverlay / PSOverlay from the offline shader bundle, D3D11OverlayHlsl.hpp) and a One / InvSrcAlpha blend.
// Only built with OKAY_XBOX_UWP. UNCOMPILED on Windows (MinGW syntax check only) and NEVER RUN. Written from Microsoft's documentation of
// ID3D11Device::CreateTexture2D / CreateBlendState / CreateSamplerState, ID3D11DeviceContext::UpdateSubresource and Draw.
#if defined(_WIN32) && defined(OKAY_XBOX_UWP)
#include "XboxOverlayMath.hpp"
#include "okay/Render/D3D11Renderer.hpp"
#include <d3d11.h>

namespace okay::xbox {

class UiOverlay {
public:
    UiOverlay() = default;
    UiOverlay(const UiOverlay&) = delete; UiOverlay& operator=(const UiOverlay&) = delete;
    ~UiOverlay() { Release(); }

    /// `device` = ID3D11Device* (D3D11Renderer::DeviceForPresent). False when the overlay shaders are not in the bundle (then there is no UI layer).
    bool Create(void* device, SizePx size) {
        Release();
        dev_ = static_cast<ID3D11Device*>(device); if (!dev_) return false;
        dev_->AddRef(); dev_->GetImmediateContext(&ctx_);
        const void* vb = nullptr; const void* pb = nullptr; std::size_t vn = 0, pn = 0;
        if (!D3D11Renderer::BundleShader("VSOverlay", &vb, &vn) || !D3D11Renderer::BundleShader("PSOverlay", &pb, &pn)) { Release(); return false; }
        if (FAILED(dev_->CreateVertexShader(vb, vn, nullptr, &vs_)) || FAILED(dev_->CreatePixelShader(pb, pn, nullptr, &ps_))) { Release(); return false; }
        D3D11_BLEND_DESC bd = {}; bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;   // premultiplied alpha
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
        D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(dev_->CreateBlendState(&bd, &blend_)) || FAILED(dev_->CreateSamplerState(&sd, &samp_)) || FAILED(dev_->CreateRasterizerState(&rd, &rast_)) || FAILED(dev_->CreateDepthStencilState(&dd, &depth_))) { Release(); return false; }
        return Resize(size);
    }
    bool Valid() const { return tex_ != nullptr; }
    SizePx Size() const { return size_; }

    /// New texture of this size (the old content is gone: draw and upload the whole UI again).
    bool Resize(SizePx size) {
        if (!dev_) return false;
        if (srv_) { srv_->Release(); srv_ = nullptr; } if (tex_) { tex_->Release(); tex_ = nullptr; }
        D3D11_TEXTURE2D_DESC td = {}; td.Width = (UINT)size.w; td.Height = (UINT)size.h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev_->CreateTexture2D(&td, nullptr, &tex_)) || FAILED(dev_->CreateShaderResourceView(tex_, nullptr, &srv_))) { if (tex_) { tex_->Release(); tex_ = nullptr; } return false; }
        size_ = size; return true;
    }
    /// Copy the changed rows of the surface into the texture (UpdateSubresource with a box: only those bytes go to the GPU).
    void Upload(const SoftSurface& s, const IRect& dirty) {
        if (!tex_ || s.Width() != size_.w || s.Height() != size_.h) return;
        const IRect d = Intersect(dirty, IRect{0, 0, s.Width(), s.Height()}); if (d.Empty()) return;
        D3D11_BOX box = {(UINT)d.x, (UINT)d.y, 0, (UINT)(d.x + d.w), (UINT)(d.y + d.h), 1};
        const std::uint8_t* src = s.Data() + ((std::size_t)d.y * (std::size_t)s.Width() + (std::size_t)d.x) * 4;
        ctx_->UpdateSubresource(tex_, 0, &box, src, (UINT)s.Width() * 4, 0);
    }
    /// Blend the overlay over `backBuffer` (an ID3D11Texture2D*): a full-screen triangle, then the bindings are cleared.
    void Draw(void* backBuffer) {
        if (!tex_ || !backBuffer) return;
        ID3D11Texture2D* bb = static_cast<ID3D11Texture2D*>(backBuffer);
        D3D11_TEXTURE2D_DESC bdsc; bb->GetDesc(&bdsc);
        ID3D11RenderTargetView* rtv = nullptr;
        if (FAILED(dev_->CreateRenderTargetView(bb, nullptr, &rtv))) return;
        const float bf[4] = {0, 0, 0, 0}; D3D11_VIEWPORT vp = {0, 0, (FLOAT)bdsc.Width, (FLOAT)bdsc.Height, 0, 1};
        ctx_->OMSetRenderTargets(1, &rtv, nullptr); ctx_->RSSetViewports(1, &vp); ctx_->RSSetState(rast_); ctx_->OMSetDepthStencilState(depth_, 0); ctx_->OMSetBlendState(blend_, bf, 0xFFFFFFFFu);
        ctx_->IASetInputLayout(nullptr); ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->VSSetShader(vs_, nullptr, 0); ctx_->PSSetShader(ps_, nullptr, 0); ctx_->PSSetShaderResources(0, 1, &srv_); ctx_->PSSetSamplers(0, 1, &samp_);
        ctx_->Draw(3, 0);
        ID3D11ShaderResourceView* none = nullptr; ctx_->PSSetShaderResources(0, 1, &none);
        ID3D11RenderTargetView* nort = nullptr; ctx_->OMSetRenderTargets(1, &nort, nullptr); ctx_->OMSetBlendState(nullptr, bf, 0xFFFFFFFFu);
        rtv->Release();
    }
    void Release() {
        for (IUnknown** p : {(IUnknown**)&srv_, (IUnknown**)&tex_, (IUnknown**)&vs_, (IUnknown**)&ps_, (IUnknown**)&blend_, (IUnknown**)&samp_, (IUnknown**)&rast_, (IUnknown**)&depth_, (IUnknown**)&ctx_, (IUnknown**)&dev_})
            if (*p) { (*p)->Release(); *p = nullptr; }
        size_ = {};
    }
private:
    ID3D11Device* dev_ = nullptr; ID3D11DeviceContext* ctx_ = nullptr; ID3D11VertexShader* vs_ = nullptr; ID3D11PixelShader* ps_ = nullptr;
    ID3D11BlendState* blend_ = nullptr; ID3D11SamplerState* samp_ = nullptr; ID3D11RasterizerState* rast_ = nullptr; ID3D11DepthStencilState* depth_ = nullptr;
    ID3D11Texture2D* tex_ = nullptr; ID3D11ShaderResourceView* srv_ = nullptr; SizePx size_{};
};

} // namespace okay::xbox
#endif
