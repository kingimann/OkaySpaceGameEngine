// The engine mode of the Xbox app shell: runs an exported OkaySpace game from package\Data. EXPERIMENTAL, UNCOMPILED, UNVERIFIED:
// this file has never been through a Windows compiler (no C++/WinRT headers on the machine it was written on). It only holds the Windows
// glue (CoreWindow events -> UwpBackend, Windows.Gaming.Input -> PadDevice, WASAPI start, the loop); every decision it makes lives in
// pure headers that are tested on Linux: XboxLifecycle / XboxDisplay (suspend, resize, device loss), XboxInput (pads), XboxPaths,
// XboxAudio, XboxUwpBackend, XboxGameConfig. Built only with -DOKAY_XBOX_APP_ENGINE=ON (okay_engine_uwp linked).
// Written from Microsoft's documentation of: CoreApplication / IFrameworkView, CoreWindow events (SizeChanged, VisibilityChanged,
// Activated, KeyDown, KeyUp, PointerMoved / Pressed / Released, PointerWheelChanged, CharacterReceived, Closed),
// SystemNavigationManager.BackRequested, DisplayInformation.LogicalDpi, SuspendingOperation.GetDeferral, Windows.Gaming.Input.Gamepad
// (GetCurrentReading, Vibration), Windows.Media.Devices.MediaDevice.GetDefaultAudioRenderId.
#pragma once
#if defined(OKAY_XBOX_APP_ENGINE)
#include "XboxGameRuntime.hpp"
#include "XboxPresent.hpp"
#include "XboxUwpBackend.hpp"
#include "UwpPlatform.hpp"
#include "XboxWasapi.hpp"
#include "okay/Core/Paths.hpp"
#include "okay/Render/D3D11Renderer.hpp"

#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <chrono>

namespace okay_xbox_app {

using namespace winrt;
using namespace winrt::Windows::Foundation;   // IInspectable
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;
namespace gi = winrt::Windows::Gaming::Input;
namespace xb = okay::xbox;

// The documented GamepadButtons values must equal ours (XboxInput.hpp). A mismatch is a compile error, not a wrong button on a console.
static_assert(static_cast<uint32_t>(gi::GamepadButtons::Menu) == xb::kPadMenu && static_cast<uint32_t>(gi::GamepadButtons::View) == xb::kPadView &&
              static_cast<uint32_t>(gi::GamepadButtons::A) == xb::kPadA && static_cast<uint32_t>(gi::GamepadButtons::B) == xb::kPadB &&
              static_cast<uint32_t>(gi::GamepadButtons::X) == xb::kPadX && static_cast<uint32_t>(gi::GamepadButtons::Y) == xb::kPadY &&
              static_cast<uint32_t>(gi::GamepadButtons::DPadUp) == xb::kPadDpadUp && static_cast<uint32_t>(gi::GamepadButtons::DPadDown) == xb::kPadDpadDown &&
              static_cast<uint32_t>(gi::GamepadButtons::DPadLeft) == xb::kPadDpadLeft && static_cast<uint32_t>(gi::GamepadButtons::DPadRight) == xb::kPadDpadRight &&
              static_cast<uint32_t>(gi::GamepadButtons::LeftShoulder) == xb::kPadLeftShoulder && static_cast<uint32_t>(gi::GamepadButtons::RightShoulder) == xb::kPadRightShoulder &&
              static_cast<uint32_t>(gi::GamepadButtons::LeftThumbstick) == xb::kPadLeftThumb && static_cast<uint32_t>(gi::GamepadButtons::RightThumbstick) == xb::kPadRightThumb,
              "GamepadButtons values differ from XboxInput.hpp");

inline std::string Utf8(hstring const& h) { return winrt::to_string(h); }

// One Windows.Gaming.Input pad as a PadDevice.
struct WinRtPad : xb::PadDevice {
    gi::Gamepad pad{nullptr};
    bool Read(xb::PadReading& o) override {
        if (!pad) return false;
        const gi::GamepadReading r = pad.GetCurrentReading();
        o.buttons = static_cast<uint32_t>(r.Buttons);
        o.leftTrigger = r.LeftTrigger; o.rightTrigger = r.RightTrigger;
        o.leftX = r.LeftThumbstickX; o.leftY = r.LeftThumbstickY; o.rightX = r.RightThumbstickX; o.rightY = r.RightThumbstickY;
        return true;
    }
    void SendVibration(const xb::Vibration& v) override {
        if (!pad) return;
        gi::GamepadVibration g{v.leftMotor, v.rightMotor, v.leftTrigger, v.rightTrigger};
        pad.Vibration(g);
    }
};

struct Host : xb::UwpHost {
    CoreWindow window{nullptr};
    std::vector<std::unique_ptr<WinRtPad>> pads;
    std::unique_ptr<xb::WasapiHost> audio;
    void* CoreWindowHandle() override { return window ? reinterpret_cast<::IUnknown*>(get_abi(window)) : nullptr; }
    void PumpEvents() override { window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent); }
    void Refresh() {   // rebuild the pad list from the system (called each frame; cheap for a handful of pads)
        auto list = gi::Gamepad::Gamepads();
        pads.clear();
        for (uint32_t i = 0; i < list.Size(); ++i) { auto p = std::make_unique<WinRtPad>(); p->pad = list.GetAt(i); pads.push_back(std::move(p)); }
    }
    int PadCount() override { Refresh(); return (int)pads.size(); }
    xb::PadDevice* Pad(int i) override { return i >= 0 && i < (int)pads.size() ? pads[(size_t)i].get() : nullptr; }
    xb::AudioHost* Audio() override {
        if (!audio) {
            const hstring id = winrt::Windows::Media::Devices::MediaDevice::GetDefaultAudioRenderId(winrt::Windows::Media::Devices::AudioDeviceRole::Default);
            audio.reset(new xb::WasapiHost(std::wstring(id.c_str())));
        }
        return audio.get();
    }
    bool PresentPixels(const std::uint8_t*, int, int) override { return false; }   // software frames are not presented yet (docs/xbox_uwp.md)
};

inline std::wstring PackageDir() { return std::wstring(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str()); }
inline std::string Narrow(const std::wstring& w) { return winrt::to_string(winrt::hstring(w)); }

struct GameApp : implements<GameApp, IFrameworkViewSource, IFrameworkView> {
    IFrameworkView CreateView() { return *this; }
    void Initialize(CoreApplicationView const&) {
        m_susp = CoreApplication::Suspending({this, &GameApp::OnSuspending});
        m_res = CoreApplication::Resuming({this, &GameApp::OnResuming});
    }
    void Load(hstring const&) {}
    void Uninitialize() {}

    void SetWindow(CoreWindow const& w) {
        m_host.window = w;
        // Roots first: the Paths override must be in place before anything asks for a per-user folder.
        xb::PathRoots roots;
        roots.install = Narrow(PackageDir()) + "/";
        roots.local = Narrow(std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str()));
        roots.temp = Narrow(std::wstring(winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path().c_str()));
        m_roots = roots;
        okay::Paths::Env env;
        env.appData = xb::DesktopDirToLocal(roots, xb::DesktopDir::RoamingAppData);
        env.localAppData = xb::DesktopDirToLocal(roots, xb::DesktopDir::LocalAppData);
        env.home = xb::DesktopDirToLocal(roots, xb::DesktopDir::Documents);
        okay::Paths::SetEnvOverride(&env);
        m_backend.reset(new xb::UwpBackend(&m_host, roots));
        xb::InstallBackend(m_backend.get());
        m_backend->Init(okay::platform::InitFlags());
        auto& life = m_backend->Life();
        auto pixels = [this](float dips) { return dips; };
        (void)pixels;
        auto resize = [this]() {
            auto b = m_host.window.Bounds();
            const float dpi = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().LogicalDpi();
            m_backend->Life().Resized(b.Width, b.Height, dpi);
        };
        resize();
        w.SizeChanged([resize](auto&&, auto&&) { resize(); });
        w.VisibilityChanged([this](auto&&, VisibilityChangedEventArgs const& a) { m_backend->Life().VisibilityChanged(a.Visible()); });
        w.Activated([this](auto&&, WindowActivatedEventArgs const& a) { m_backend->Life().Activated(a.WindowActivationState() != CoreWindowActivationState::Deactivated); });
        w.Closed([this](auto&&, auto&&) { m_backend->Life().WindowClosed(); });
        w.KeyDown([this](auto&&, KeyEventArgs const& a) { m_backend->PushKey((int)a.VirtualKey(), true, a.KeyStatus().WasKeyDown); });
        w.KeyUp([this](auto&&, KeyEventArgs const& a) { m_backend->PushKey((int)a.VirtualKey(), false); });
        w.CharacterReceived([this](auto&&, CharacterReceivedEventArgs const& a) {
            const uint32_t c = a.KeyCode(); wchar_t wc[2] = {(wchar_t)c, 0};
            if (c >= 32 && c < 0xD800) m_backend->PushText(winrt::to_string(winrt::hstring(wc)));
        });
        auto scale = [](float v) { return (int)(v * winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().LogicalDpi() / 96.0f + 0.5f); };
        w.PointerMoved([this, scale](auto&&, PointerEventArgs const& a) { auto p = a.CurrentPoint().Position(); m_backend->PushPointerMove(scale(p.X), scale(p.Y)); });
        w.PointerPressed([this, scale](auto&&, PointerEventArgs const& a) { auto p = a.CurrentPoint().Position(); m_backend->PushPointerButton(okay::platform::MouseLeft, true, scale(p.X), scale(p.Y)); });
        w.PointerReleased([this, scale](auto&&, PointerEventArgs const& a) { auto p = a.CurrentPoint().Position(); m_backend->PushPointerButton(okay::platform::MouseLeft, false, scale(p.X), scale(p.Y)); });
        w.PointerWheelChanged([this](auto&&, PointerEventArgs const& a) { m_backend->PushWheel(a.CurrentPoint().Properties().MouseWheelDelta() / 120.0f); });
        // B on the pad: the system's back request. We handle it (so the system does not close us abruptly) and leave through the loop.
        winrt::Windows::UI::Core::SystemNavigationManager::GetForCurrentView().BackRequested([this](auto&&, BackRequestedEventArgs const& a) { a.Handled(true); m_backend->Life().BackRequested(); });
        (void)life;
    }

    void Run() {
        m_host.window.Activate();
        std::string err;
        okay::D3D11Renderer::SetShaderBundleDir((Narrow(PackageDir()) + "/Shaders").c_str());
        if (!m_renderer.Init()) { Fail("The Direct3D 11 renderer could not start (see the log). Shaders: package\\Shaders must hold shaders.manifest and the .cso files."); return; }
        const xb::SizePx first = xb::ChooseBackBuffer(m_backend->Life().Size());
        if (!m_presenter.Create(m_renderer.DeviceForPresent(), reinterpret_cast<::IUnknown*>(get_abi(m_host.window)), first)) { Fail("The swap chain could not be created."); return; }
        if (!m_runtime.Start(Narrow(PackageDir()) + "/Data", err)) { Fail(err); return; }
        m_backend->onSuspend = [this] { m_runtime.OnSuspend(); };
        m_backend->onResume = [this] { m_runtime.OnResume(); };
        auto last = std::chrono::steady_clock::now();
        bool quit = false;
        while (!quit) {
            okay::platform::Event e;
            while (m_backend->PollEvent(e)) {
                if (e.type == okay::platform::EventType::Quit) quit = true;
                else if (e.type == okay::platform::EventType::GamepadAdded) m_runtime.PadAdded(e.deviceId);
                else if (e.type == okay::platform::EventType::GamepadRemoved) m_runtime.PadRemoved(e.deviceId);
            }
            if (quit) break;
            auto& life = m_backend->Life();
            if (!life.ShouldRender()) { m_host.window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending); last = std::chrono::steady_clock::now(); continue; }
            auto& tr = m_presenter.Tracker();
            tr.WindowSized(life.Size());
            if (tr.NeedsResize() && !m_presenter.Resize(tr.Wanted())) { Fail("Resizing the swap chain failed."); return; }
            if (tr.DeviceLost()) { Fail("The graphics device was lost (no recovery in this version)."); return; }   // DeviceRebuilt() is the documented next step
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - last).count(); last = now;
            std::vector<char> keys = xb::KeysDown(*m_backend);
            if (!m_runtime.Frame(dt, keys)) break;
            okay::Mat4 vp; okay::Vec3 eye;
            const xb::SizePx sz = m_presenter.Size();
            if (m_runtime.View(sz.w, sz.h, vp, eye) && m_runtime.GetScene())
                m_presenter.RenderAndPresent(m_renderer, *m_runtime.GetScene(), vp, eye, 4, 0.10f, 0.10f, 0.12f, 1.0f);
        }
        CoreApplication::Exit();
    }

private:
    void Fail(const std::string& why) { m_backend->Log("Xbox app: " + why); CoreApplication::Exit(); }   // no UI to show it on yet: the log is the only report
    void OnSuspending(IInspectable const&, winrt::Windows::ApplicationModel::SuspendingEventArgs const& a) {
        auto deferral = a.SuspendingOperation().GetDeferral();
        auto& life = m_backend->Life();
        life.Suspending(m_backend->TicksMs(), 4500);
        m_backend->ServiceLifecycle();   // runs onSuspend (the save) and marks the save done
        if (void* dev = m_renderer.DeviceForPresent()) { winrt::com_ptr<IDXGIDevice3> d; if (SUCCEEDED(static_cast<::IUnknown*>(dev)->QueryInterface(IID_PPV_ARGS(d.put())))) d->Trim(); }
        deferral.Complete();   // Microsoft: complete within the deadline or the app is terminated (SuspendReady / SuspendOverdue in XboxLifecycle.hpp)
    }
    void OnResuming(IInspectable const&, IInspectable const&) { m_backend->Life().Resuming(); }

    Host m_host; xb::PathRoots m_roots; std::unique_ptr<xb::UwpBackend> m_backend;
    okay::D3D11Renderer m_renderer; xb::SwapChainPresenter m_presenter; xb::XboxGameRuntime m_runtime;
    event_token m_susp{}, m_res{};
};

} // namespace okay_xbox_app
#endif
