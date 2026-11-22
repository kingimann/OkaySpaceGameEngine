// ---------------------------------------------------------------------------
// OkaySpace Xbox UWP app shell - EXPERIMENTAL, UNCOMPILED, UNVERIFIED.
//
// Written without a Windows PC: this file has never been through a Windows compiler. It is the smallest app that proves the package pipeline:
// CoreApplication / IFrameworkView life cycle (suspend and resume), a CoreWindow, a Direct3D 11 flip-model swap chain, one triangle,
// Windows.Gaming.Input gamepad reading (left stick moves the triangle, A makes it jump to the centre, B exits cleanly) and a clean exit.
// By default it does NOT load or run an OkaySpace game. Built with -DOKAY_XBOX_APP_ENGINE=ON (okay_engine_uwp linked) it runs the exported game
// in package\Data through engine_shell.hpp when that folder is there, and falls back to the triangle when it is not.
//
// Written from Microsoft's public documentation: the C++/WinRT "Core app" pattern (IFrameworkViewSource, IFrameworkView), the DXGI
// "CreateSwapChainForCoreWindow" page, "Windows.Gaming.Input.Gamepad", and the UWP rule that an app must call IDXGIDevice3::Trim on suspend.
//
// C++/WinRT RULES FOLLOWED HERE (each one fixed a real compile error in the first Windows builds):
//  * ALL #includes come first; there is NO using-directive at global scope. Names are qualified or come from namespace aliases (wf, wac, wam, wui,
//    wgi), so ::IUnknown / ::IInspectable (Windows SDK) never meet winrt::Windows::Foundation::IUnknown / IInspectable.
//  * One smart-pointer style for COM: winrt::com_ptr everywhere (no WRL, so <wrl/client.h> is never included).
//  * <unknwn.h> is included before any winrt header (enables classic COM interop in C++/WinRT).
//  * No WinRT call before CoreApplication::Run (the package folder comes from GetCurrentPackagePath).
// ---------------------------------------------------------------------------
#include <unknwn.h>
#include <windows.h>
#include <appmodel.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "package_dir.hpp"
#include "XboxLog.hpp"   // the log file in LocalState\logs (pure C++, platform/xbox_uwp)
#include "engine_shell.hpp"   // empty unless OKAY_XBOX_APP_ENGINE; plain-C++ boundary to the engine (engine_bridge.hpp), no global using-directive

namespace wf = winrt::Windows::Foundation;
namespace wam = winrt::Windows::ApplicationModel;
namespace wac = winrt::Windows::ApplicationModel::Core;
namespace wui = winrt::Windows::UI::Core;
namespace wgi = winrt::Windows::Gaming::Input;

namespace {

std::vector<char> ReadPackageFile(const wchar_t* name) {   // the install folder is readable by the app
    const std::wstring path = okay_xbox_app::PackageDirWide() + L"\\" + name;
    std::ifstream f(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct FrameConstants { float offset[2]; float aspect; float pad; };

struct App : winrt::implements<App, wac::IFrameworkViewSource, wac::IFrameworkView> {
    wac::IFrameworkView CreateView() { return *this; }

    void Initialize(wac::CoreApplicationView const& view) {
        view.Activated({this, &App::OnActivated});
        m_suspendToken = wac::CoreApplication::Suspending({this, &App::OnSuspending});
        m_resumeToken = wac::CoreApplication::Resuming({this, &App::OnResuming});
    }
    void Load(winrt::hstring const&) {}
    void Uninitialize() {}

    void SetWindow(wui::CoreWindow const& window) {
        m_window = window;
        window.Closed([this](auto&&, auto&&) { m_exit = true; });
        window.VisibilityChanged([this](auto&&, wui::VisibilityChangedEventArgs const& a) { m_visible = a.Visible(); });
        window.SizeChanged([this](auto&&, auto&&) { m_resize = true; });
    }

    void Run() {
        OpenLog();
        m_window.Activate();
        if (!CreateDevice()) { Log("The test app could not start Direct3D 11 / the swap chain / its two shader files (tri_vs.cso, tri_ps.cso in the package). Exiting."); wac::CoreApplication::Exit(); return; }
        Log("Started: the triangle test. Left stick moves it, A centres it, B exits.");
        while (!m_exit) {
            if (m_visible) {
                m_window.Dispatcher().ProcessEvents(wui::CoreProcessEventsOption::ProcessAllIfPresent);
                if (m_exit) break;
                ReadGamepad();
                if (m_resize) { m_resize = false; ResizeSwapChain(); }
                Render();
            } else {   // not shown (or suspended): sleep in the message pump instead of spinning
                m_window.Dispatcher().ProcessEvents(wui::CoreProcessEventsOption::ProcessOneAndAllPending);
            }
        }
    }

private:
    // LocalFolder\logs\okayspace.log: readable from the Device Portal (Apps > this app > file explorer) or on a PC under %LOCALAPPDATA%\Packages\<family>\LocalState\logs.
    void OpenLog() {
        try {
            const std::string dir = winrt::to_string(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()) + "/logs";
            m_log.Open(dir);
        } catch (...) {}
        Log("OkaySpace Xbox triangle test (EXPERIMENTAL, UNVERIFIED) starting. Package folder: " + winrt::to_string(winrt::hstring(okay_xbox_app::PackageDirWide())));
    }
    void Log(const std::string& line) { m_log.Line(line); }

    void OnActivated(wac::CoreApplicationView const&, wam::Activation::IActivatedEventArgs const&) { m_window.Activate(); }

    void OnSuspending(wf::IInspectable const&, wam::SuspendingEventArgs const&) {
        // Required by the UWP rules: release driver memory the app does not need while suspended.
        Log("Suspending.");
        if (m_device) { auto dxgi = m_device.try_as<IDXGIDevice3>(); if (dxgi) dxgi->Trim(); }
        m_visible = false;
    }
    void OnResuming(wf::IInspectable const&, wf::IInspectable const&) { m_visible = true; Log("Resumed."); }

    bool CreateDevice() {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, static_cast<UINT>(std::size(levels)),
                                       D3D11_SDK_VERSION, m_device.put(), nullptr, m_context.put());
        if (FAILED(hr)) return false;
        auto dxgiDevice = m_device.try_as<IDXGIDevice3>();
        if (!dxgiDevice) return false;
        winrt::com_ptr<IDXGIAdapter> adapter; winrt::com_ptr<IDXGIFactory2> factory;
        if (FAILED(dxgiDevice->GetAdapter(adapter.put())) || FAILED(adapter->GetParent(IID_PPV_ARGS(factory.put())))) return false;
        auto const b = m_window.Bounds();
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = (std::max)(1u, static_cast<UINT>(b.Width)); sd.Height = (std::max)(1u, static_cast<UINT>(b.Height));
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        if (FAILED(factory->CreateSwapChainForCoreWindow(m_device.get(), reinterpret_cast<::IUnknown*>(winrt::get_abi(m_window)), &sd, nullptr, m_swap.put()))) return false;
        if (!MakeTarget()) return false;
        const std::vector<char> vs = ReadPackageFile(L"tri_vs.cso"), ps = ReadPackageFile(L"tri_ps.cso");
        if (vs.empty() || ps.empty()) return false;   // the build script copies the two .cso files next to the exe
        if (FAILED(m_device->CreateVertexShader(vs.data(), vs.size(), nullptr, m_vs.put())) || FAILED(m_device->CreatePixelShader(ps.data(), ps.size(), nullptr, m_ps.put()))) return false;
        D3D11_BUFFER_DESC cb = {}; cb.ByteWidth = sizeof(FrameConstants); cb.Usage = D3D11_USAGE_DEFAULT; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(m_device->CreateBuffer(&cb, nullptr, m_cb.put()));
    }

    bool MakeTarget() {
        winrt::com_ptr<ID3D11Texture2D> back;
        if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(back.put())))) return false;
        D3D11_TEXTURE2D_DESC td; back->GetDesc(&td); m_w = td.Width; m_h = td.Height;
        return SUCCEEDED(m_device->CreateRenderTargetView(back.get(), nullptr, m_rtv.put()));
    }
    void ResizeSwapChain() {
        if (!m_swap) return;
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_rtv = nullptr;   // the buffers must have no references while they are resized
        auto const b = m_window.Bounds();
        if (FAILED(m_swap->ResizeBuffers(0, (std::max)(1u, static_cast<UINT>(b.Width)), (std::max)(1u, static_cast<UINT>(b.Height)), DXGI_FORMAT_UNKNOWN, 0)) || !MakeTarget()) m_exit = true;
    }

    void ReadGamepad() {
        auto pads = wgi::Gamepad::Gamepads();
        if (pads.Size() == 0) { if (m_havePad) Log("The gamepad was disconnected."); m_havePad = false; return; }
        if (!m_havePad) Log("A gamepad is connected (" + std::to_string(pads.Size()) + ").");
        m_havePad = true;
        const wgi::GamepadReading r = pads.GetAt(0).GetCurrentReading();   // the first connected pad
        m_x = static_cast<float>(r.LeftThumbstickX) * 0.5f; m_y = static_cast<float>(r.LeftThumbstickY) * 0.5f;
        if ((r.Buttons & wgi::GamepadButtons::A) == wgi::GamepadButtons::A) { m_x = 0; m_y = 0; }
        if ((r.Buttons & wgi::GamepadButtons::B) == wgi::GamepadButtons::B) { m_exit = true; wac::CoreApplication::Exit(); }
    }

    void Render() {
        if (!m_rtv) return;
        const float bg[4] = {0.04f, 0.04f, 0.08f, 1.0f};   // blue-ish while no pad is connected, green-ish once one is
        const float bgPad[4] = {0.04f, 0.08f, 0.04f, 1.0f};
        ID3D11RenderTargetView* targets[1] = {m_rtv.get()};
        m_context->OMSetRenderTargets(1, targets, nullptr);
        m_context->ClearRenderTargetView(m_rtv.get(), m_havePad ? bgPad : bg);
        D3D11_VIEWPORT vp = {0, 0, static_cast<float>(m_w), static_cast<float>(m_h), 0, 1};
        m_context->RSSetViewports(1, &vp);
        FrameConstants c = {{m_x, m_y}, m_h ? static_cast<float>(m_w) / static_cast<float>(m_h) : 1.0f, 0};
        m_context->UpdateSubresource(m_cb.get(), 0, nullptr, &c, 0, 0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vs.get(), nullptr, 0);
        ID3D11Buffer* buffers[1] = {m_cb.get()};
        m_context->VSSetConstantBuffers(0, 1, buffers);
        m_context->PSSetShader(m_ps.get(), nullptr, 0);
        m_context->Draw(3, 0);
        const HRESULT hr = m_swap->Present(1, 0);   // vsync
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) { Log("The graphics device was lost (the test app does not recover). Exiting."); m_exit = true; }   // no device-loss recovery in this shell
    }

    wui::CoreWindow m_window{nullptr};
    winrt::event_token m_suspendToken{}, m_resumeToken{};
    winrt::com_ptr<ID3D11Device> m_device; winrt::com_ptr<ID3D11DeviceContext> m_context; winrt::com_ptr<IDXGISwapChain1> m_swap;
    winrt::com_ptr<ID3D11RenderTargetView> m_rtv; winrt::com_ptr<ID3D11VertexShader> m_vs; winrt::com_ptr<ID3D11PixelShader> m_ps; winrt::com_ptr<ID3D11Buffer> m_cb;
    okay::xbox::FileLog m_log;
    UINT m_w = 0, m_h = 0;
    float m_x = 0, m_y = 0;
    bool m_exit = false, m_visible = true, m_resize = false, m_havePad = false;
};

} // namespace

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
#if defined(OKAY_XBOX_APP_ENGINE)
    {   // an exported game is in package\Data: run it. Otherwise (no Data, or the engine was not built in) the triangle test below.
        std::ifstream cfg(okay_xbox_app::PackageDirWide() + L"\\Data\\game.okayconfig", std::ios::binary);
        if (cfg.good()) { wac::CoreApplication::Run(winrt::make<okay_xbox_app::GameApp>()); return 0; }
    }
#endif
    wac::CoreApplication::Run(winrt::make<App>());
    return 0;
}
