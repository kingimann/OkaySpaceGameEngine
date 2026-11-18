// ---------------------------------------------------------------------------
// OkaySpace Xbox UWP app shell - EXPERIMENTAL, UNCOMPILED, UNVERIFIED.
//
// Written without a Windows PC: this file has never been through a Windows compiler. It is the smallest
// app that proves the package pipeline: CoreApplication / IFrameworkView life cycle (suspend and resume),
// a CoreWindow, a Direct3D 11 flip-model swap chain, one triangle, Windows.Gaming.Input gamepad reading
// (left stick moves the triangle, A makes it jump to the centre, B exits cleanly) and a clean exit.
// By default it does NOT load or run an OkaySpace game. Built with -DOKAY_XBOX_APP_ENGINE=ON (okay_engine_uwp linked) it runs the
// exported game in package\Data through engine_app.hpp when that folder is there, and falls back to the triangle when it is not.
//
// Written from Microsoft's public documentation: the C++/WinRT "Core app" pattern (IFrameworkViewSource,
// IFrameworkView), the DXGI "CreateSwapChainForCoreWindow" page, "Windows.Gaming.Input.Gamepad", and the UWP
// rule that an app must call IDXGIDevice3::Trim on suspend. Dependencies: only the Windows SDK's own
// headers (C++/WinRT is header-only in the SDK) and system libraries. No third-party code.
// ---------------------------------------------------------------------------
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.UI.Core.h>

#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include "engine_app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::Gaming::Input;

namespace {

std::vector<char> ReadPackageFile(const wchar_t* name) {   // the install folder is readable by the app
    const std::wstring path = std::wstring(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str()) + L"\\" + name;
    std::ifstream f(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct FrameConstants { float offset[2]; float aspect; float pad; };

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    IFrameworkView CreateView() { return *this; }

    void Initialize(CoreApplicationView const& view) {
        view.Activated({this, &App::OnActivated});
        m_suspendToken = CoreApplication::Suspending({this, &App::OnSuspending});
        m_resumeToken = CoreApplication::Resuming({this, &App::OnResuming});
    }
    void Load(hstring const&) {}
    void Uninitialize() {}

    void SetWindow(CoreWindow const& window) {
        m_window = window;
        window.Closed([this](auto&&, auto&&) { m_exit = true; });
        window.VisibilityChanged([this](auto&&, VisibilityChangedEventArgs const& a) { m_visible = a.Visible(); });
        window.SizeChanged([this](auto&&, auto&&) { m_resize = true; });
    }

    void Run() {
        m_window.Activate();
        if (!CreateDevice()) { CoreApplication::Exit(); return; }
        while (!m_exit) {
            if (m_visible) {
                m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
                if (m_exit) break;
                ReadGamepad();
                if (m_resize) { m_resize = false; ResizeSwapChain(); }
                Render();
            } else {   // not shown (or suspended): sleep in the message pump instead of spinning
                m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
            }
        }
    }

private:
    void OnActivated(CoreApplicationView const&, Windows::ApplicationModel::Activation::IActivatedEventArgs const&) { m_window.Activate(); }

    void OnSuspending(IInspectable const&, Windows::ApplicationModel::SuspendingEventArgs const&) {
        // Required by the UWP rules: release driver memory the app does not need while suspended.
        if (m_device) { ComPtr<IDXGIDevice3> dxgi; if (SUCCEEDED(m_device.As(&dxgi))) dxgi->Trim(); }
        m_visible = false;
    }
    void OnResuming(IInspectable const&, IInspectable const&) { m_visible = true; }

    bool CreateDevice() {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (FAILED(hr)) return false;
        m_device = dev; m_context = ctx;
        ComPtr<IDXGIDevice3> dxgiDevice; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
        if (FAILED(m_device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
        const Rect b = m_window.Bounds();
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = std::max(1u, static_cast<UINT>(b.Width)); sd.Height = std::max(1u, static_cast<UINT>(b.Height));
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        if (FAILED(factory->CreateSwapChainForCoreWindow(m_device.Get(), reinterpret_cast<IUnknown*>(get_abi(m_window)), &sd, nullptr, &m_swap))) return false;
        if (!MakeTarget()) return false;
        const std::vector<char> vs = ReadPackageFile(L"tri_vs.cso"), ps = ReadPackageFile(L"tri_ps.cso");
        if (vs.empty() || ps.empty()) return false;   // the build script copies the two .cso files next to the exe
        if (FAILED(m_device->CreateVertexShader(vs.data(), vs.size(), nullptr, &m_vs)) || FAILED(m_device->CreatePixelShader(ps.data(), ps.size(), nullptr, &m_ps))) return false;
        D3D11_BUFFER_DESC cb = {}; cb.ByteWidth = sizeof(FrameConstants); cb.Usage = D3D11_USAGE_DEFAULT; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(m_device->CreateBuffer(&cb, nullptr, &m_cb));
    }

    bool MakeTarget() {
        ComPtr<ID3D11Texture2D> back;
        if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
        D3D11_TEXTURE2D_DESC td; back->GetDesc(&td); m_w = td.Width; m_h = td.Height;
        return SUCCEEDED(m_device->CreateRenderTargetView(back.Get(), nullptr, &m_rtv));
    }
    void ResizeSwapChain() {
        if (!m_swap) return;
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_rtv.Reset();
        const Rect b = m_window.Bounds();
        if (FAILED(m_swap->ResizeBuffers(0, std::max(1u, static_cast<UINT>(b.Width)), std::max(1u, static_cast<UINT>(b.Height)), DXGI_FORMAT_UNKNOWN, 0)) || !MakeTarget()) m_exit = true;
    }

    void ReadGamepad() {
        auto pads = Gamepad::Gamepads();
        if (pads.Size() == 0) { m_havePad = false; return; }
        m_havePad = true;
        const GamepadReading r = pads.GetAt(0).GetCurrentReading();   // the first connected pad
        m_x = static_cast<float>(r.LeftThumbstickX) * 0.5f; m_y = static_cast<float>(r.LeftThumbstickY) * 0.5f;
        if ((r.Buttons & GamepadButtons::A) == GamepadButtons::A) { m_x = 0; m_y = 0; }
        if ((r.Buttons & GamepadButtons::B) == GamepadButtons::B) { m_exit = true; CoreApplication::Exit(); }
    }

    void Render() {
        if (!m_rtv) return;
        const float bg[4] = {0.04f, 0.04f, 0.08f, 1.0f};   // blue-ish while no pad is connected, darker grey once one is
        const float bgPad[4] = {0.04f, 0.08f, 0.04f, 1.0f};
        m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
        m_context->ClearRenderTargetView(m_rtv.Get(), m_havePad ? bgPad : bg);
        D3D11_VIEWPORT vp = {0, 0, static_cast<float>(m_w), static_cast<float>(m_h), 0, 1};
        m_context->RSSetViewports(1, &vp);
        FrameConstants c = {{m_x, m_y}, m_h ? static_cast<float>(m_w) / static_cast<float>(m_h) : 1.0f, 0};
        m_context->UpdateSubresource(m_cb.Get(), 0, nullptr, &c, 0, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vs.Get(), nullptr, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
        m_context->PSSetShader(m_ps.Get(), nullptr, 0);
        m_context->Draw(3, 0);
        const HRESULT hr = m_swap->Present(1, 0);   // vsync
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) m_exit = true;   // no device-loss recovery in this shell
    }

    CoreWindow m_window{nullptr};
    event_token m_suspendToken{}, m_resumeToken{};
    ComPtr<ID3D11Device> m_device; ComPtr<ID3D11DeviceContext> m_context; ComPtr<IDXGISwapChain1> m_swap;
    ComPtr<ID3D11RenderTargetView> m_rtv; ComPtr<ID3D11VertexShader> m_vs; ComPtr<ID3D11PixelShader> m_ps; ComPtr<ID3D11Buffer> m_cb;
    UINT m_w = 0, m_h = 0;
    float m_x = 0, m_y = 0;
    bool m_exit = false, m_visible = true, m_resize = false, m_havePad = false;
};

} // namespace

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
#if defined(OKAY_XBOX_APP_ENGINE)
    {   // an exported game is in package\Data: run it. Otherwise (no Data, or the engine was not built in) the triangle test below.
        std::ifstream cfg(std::wstring(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str()) + L"\\Data\\game.okayconfig", std::ios::binary);
        if (cfg.good()) { CoreApplication::Run(make<okay_xbox_app::GameApp>()); return 0; }
    }
#endif
    CoreApplication::Run(make<App>());
    return 0;
}
