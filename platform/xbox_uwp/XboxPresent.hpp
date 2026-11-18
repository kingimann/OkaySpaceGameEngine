#pragma once
// Swap-chain presentation for the Xbox UWP app: a flip-model swap chain on the CoreWindow, fed by the EXISTING D3D11Renderer
// through its no-read-back path (RenderToTexture draws, resolves MSAA and copies straight into the back buffer). Only built when
// OKAY_XBOX_UWP is defined. UNCOMPILED on Windows: syntax-checked on Linux with MinGW's DXGI headers only. NEVER RUN.
// Written from Microsoft's documentation of IDXGIFactory2::CreateSwapChainForCoreWindow, DXGI_SWAP_CHAIN_DESC1 and
// IDXGISwapChain::Present / ResizeBuffers. Flip model: DXGI_SWAP_EFFECT_FLIP_DISCARD, 2 buffers, no MSAA on the swap chain
// (the renderer's own target is multisampled and is resolved into the back buffer).
#if defined(_WIN32) && defined(OKAY_XBOX_UWP)
#include "XboxDisplay.hpp"
#include "okay/Render/D3D11Renderer.hpp"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <functional>

namespace okay::xbox {

class SwapChainPresenter {
public:
    SwapChainPresenter() = default;
    SwapChainPresenter(const SwapChainPresenter&) = delete;
    SwapChainPresenter& operator=(const SwapChainPresenter&) = delete;
    ~SwapChainPresenter() { Release(); }

    /// `device` = ID3D11Device* (D3D11Renderer::DeviceForPresent), `coreWindow` = the CoreWindow as an IUnknown*.
    bool Create(void* device, void* coreWindow, SizePx size) {
        Release();
        ID3D11Device* dev = static_cast<ID3D11Device*>(device);
        IDXGIDevice* dxgiDev = nullptr; IDXGIAdapter* adapter = nullptr; IDXGIFactory2* fac = nullptr;
        if (!dev || !coreWindow || FAILED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev))) return false;
        bool ok = SUCCEEDED(dxgiDev->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&fac));
        if (ok) {
            DXGI_SWAP_CHAIN_DESC1 d = {};
            d.Width = (UINT)size.w; d.Height = (UINT)size.h;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;            // RenderToTexture accepts R8G8B8A8 or B8G8R8A8
            d.SampleDesc.Count = 1;
            d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            d.BufferCount = 2;
            d.Scaling = DXGI_SCALING_STRETCH;                 // back buffer may be smaller than the window (1080p cap)
            d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            ok = SUCCEEDED(fac->CreateSwapChainForCoreWindow(dev, static_cast<IUnknown*>(coreWindow), &d, nullptr, &sc_));
        }
        if (fac) fac->Release(); if (adapter) adapter->Release(); dxgiDev->Release();
        if (!ok) { Release(); return false; }
        tracker_.SetCap(size); tracker_.WindowSized(size); tracker_.Resized(size);
        size_ = size;
        return true;
    }
    bool Valid() const { return sc_ != nullptr; }
    SizePx Size() const { return size_; }
    PresentTracker& Tracker() { return tracker_; }

    /// Resize the buffers. The caller must hold no reference to the back buffer (RenderAndPresent never keeps one).
    bool Resize(SizePx size) {
        if (!sc_) return false;
        if (size == size_) return true;
        if (FAILED(sc_->ResizeBuffers(0, (UINT)size.w, (UINT)size.h, DXGI_FORMAT_UNKNOWN, 0))) return false;
        size_ = size; tracker_.Resized(size);
        return true;
    }

    /// Render `scene` into the back buffer with the existing renderer (no read-back) and present it. The arguments after
    /// `renderer` are exactly D3D11Renderer::RenderToTexture's. Returns false when nothing was presented.
    template <class Scene, class Mat, class Vec>
    bool RenderAndPresent(D3D11Renderer& renderer, const Scene& scene, const Mat& vp, const Vec& eye, int samples,
                          float r, float g, float b, float a, int syncInterval = 1) {
        if (!sc_ || !tracker_.ShouldDraw()) return false;
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(sc_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return false;
        const bool drew = renderer.RenderToTexture(scene, vp, eye, size_.w, size_.h, samples, r, g, b, a, bb);
        if (drew && afterRender_) afterRender_(bb);   // the UI overlay is blended over the finished 3D frame here
        bb->Release();
        if (!drew) return false;
        tracker_.Report((std::int32_t)sc_->Present((UINT)syncInterval, 0));
        return !tracker_.DeviceLost();
    }

    /// Called with the back buffer (an ID3D11Texture2D*) after the 3D frame is drawn and before Present: the 2D / UI overlay hooks in here.
    void SetAfterRender(std::function<void(void*)> f) { afterRender_ = std::move(f); }

    void Release() { if (sc_) { sc_->Release(); sc_ = nullptr; } size_ = {}; }

private:
    IDXGISwapChain1* sc_ = nullptr;
    SizePx size_{};
    PresentTracker tracker_;
    std::function<void(void*)> afterRender_;
};

} // namespace okay::xbox
#endif
