// The engine mode of the Xbox app shell, WinRT side (C++20): CoreWindow events and Windows.Gaming.Input forwarded to the engine through the plain
// boundary in engine_bridge.hpp. No engine, no Direct3D and no engine header here. EXPERIMENTAL, UNCOMPILED, UNVERIFIED (never through MSVC).
// Rules: included by main.cpp AFTER every other #include; no global using-directive; ::IUnknown for COM, wf::IInspectable for WinRT.
// Written from Microsoft's documentation of: CoreApplication / IFrameworkView, CoreWindow events (SizeChanged, VisibilityChanged, Activated, KeyDown,
// KeyUp, PointerMoved / Pressed / Released, PointerWheelChanged, CharacterReceived, Closed), SystemNavigationManager.BackRequested,
// DisplayInformation.LogicalDpi, SuspendingOperation.GetDeferral, Windows.Gaming.Input.Gamepad, MediaDevice.GetDefaultAudioRenderId.
#pragma once
#if defined(OKAY_XBOX_APP_ENGINE)
#include "engine_bridge.hpp"
#include "package_dir.hpp"

#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <cstring>
#include <exception>

namespace okay_xbox_app {

namespace wf = winrt::Windows::Foundation;
namespace wac = winrt::Windows::ApplicationModel::Core;
namespace wui = winrt::Windows::UI::Core;
namespace wgi = winrt::Windows::Gaming::Input;
namespace wgd = winrt::Windows::Graphics::Display;
namespace bridge = okay_xbox_bridge;

inline std::string Narrow(const std::wstring& w) { return winrt::to_string(winrt::hstring(w)); }

struct GameApp : winrt::implements<GameApp, wac::IFrameworkViewSource, wac::IFrameworkView> {
    wac::IFrameworkView CreateView() { return *this; }
    void Initialize(wac::CoreApplicationView const&) {
        m_susp = wac::CoreApplication::Suspending({this, &GameApp::OnSuspending});
        m_res = wac::CoreApplication::Resuming({this, &GameApp::OnResuming});
    }
    void Load(winrt::hstring const&) {}
    void Uninitialize() { if (m_engine) { bridge::Destroy(m_engine); m_engine = nullptr; } }

    void SetWindow(wui::CoreWindow const& w) {
        m_window = w;
        bridge::ShellCallbacks cb;
        cb.user = this;
        cb.coreWindow = [](void* u) -> void* { return reinterpret_cast<::IUnknown*>(winrt::get_abi(static_cast<GameApp*>(u)->m_window)); };
        cb.pump = [](void* u, bool wait) {
            static_cast<GameApp*>(u)->m_window.Dispatcher().ProcessEvents(wait ? wui::CoreProcessEventsOption::ProcessOneAndAllPending : wui::CoreProcessEventsOption::ProcessAllIfPresent);
        };
        cb.padCount = [](void*) -> int { return static_cast<int>(wgi::Gamepad::Gamepads().Size()); };
        cb.padKey = [](void*, int i, wchar_t* buf, int cap) -> int {
            auto pads = wgi::Gamepad::Gamepads();
            if (i < 0 || static_cast<uint32_t>(i) >= pads.Size()) return 0;
            const winrt::hstring id = pads.GetAt(static_cast<uint32_t>(i)).NonRoamableId();
            const int n = static_cast<int>(id.size());
            if (n <= 0 || n >= cap) return 0;
            std::memcpy(buf, id.c_str(), static_cast<std::size_t>(n) * sizeof(wchar_t)); buf[n] = L'\0';
            return n;
        };
        cb.padRead = [](void*, int i, bridge::PadReadingPod* o) -> bool {
            auto pads = wgi::Gamepad::Gamepads();
            if (i < 0 || static_cast<uint32_t>(i) >= pads.Size()) return false;
            const wgi::GamepadReading r = pads.GetAt(static_cast<uint32_t>(i)).GetCurrentReading();
            o->buttons = static_cast<std::uint32_t>(r.Buttons);
            o->leftTrigger = r.LeftTrigger; o->rightTrigger = r.RightTrigger;
            o->leftX = r.LeftThumbstickX; o->leftY = r.LeftThumbstickY; o->rightX = r.RightThumbstickX; o->rightY = r.RightThumbstickY;
            return true;
        };
        cb.padVibrate = [](void*, int i, double l, double r, double lt, double rt) {
            auto pads = wgi::Gamepad::Gamepads();
            if (i < 0 || static_cast<uint32_t>(i) >= pads.Size()) return;
            wgi::GamepadVibration v{l, r, lt, rt};
            pads.GetAt(static_cast<uint32_t>(i)).Vibration(v);
        };
        cb.audioDeviceId = [](void*, wchar_t* buf, int cap) -> int {
            const winrt::hstring id = winrt::Windows::Media::Devices::MediaDevice::GetDefaultAudioRenderId(winrt::Windows::Media::Devices::AudioDeviceRole::Default);
            const int n = static_cast<int>(id.size());
            if (n <= 0 || n >= cap) return 0;
            std::memcpy(buf, id.c_str(), static_cast<std::size_t>(n) * sizeof(wchar_t)); buf[n] = L'\0';
            return n;
        };
        auto local = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
        auto temp = winrt::Windows::Storage::ApplicationData::Current().TemporaryFolder().Path();
        m_engine = bridge::Create(cb, Narrow(PackageDirWide()), winrt::to_string(local), winrt::to_string(temp));

        auto resize = [this]() {
            auto b = m_window.Bounds();
            bridge::Resized(m_engine, b.Width, b.Height, wgd::DisplayInformation::GetForCurrentView().LogicalDpi());
        };
        resize();
        w.SizeChanged([resize](auto&&, auto&&) { resize(); });
        w.VisibilityChanged([this](auto&&, wui::VisibilityChangedEventArgs const& a) { bridge::Visibility(m_engine, a.Visible()); });
        w.Activated([this](auto&&, wui::WindowActivatedEventArgs const& a) { bridge::Activated(m_engine, a.WindowActivationState() != wui::CoreWindowActivationState::Deactivated); });
        w.Closed([this](auto&&, auto&&) { bridge::Closed(m_engine); });
        w.KeyDown([this](auto&&, wui::KeyEventArgs const& a) { bridge::Key(m_engine, static_cast<int>(a.VirtualKey()), true, a.KeyStatus().WasKeyDown); });
        w.KeyUp([this](auto&&, wui::KeyEventArgs const& a) { bridge::Key(m_engine, static_cast<int>(a.VirtualKey()), false, false); });
        w.CharacterReceived([this](auto&&, wui::CharacterReceivedEventArgs const& a) { bridge::Character(m_engine, a.KeyCode()); });
        // Pointer events: mouse, touch and pen all go through bridge::Pointer; XboxAppGlue.hpp decides what each one means.
        auto pointer = [this](wui::PointerEventArgs const& a, int action) {
            auto pt = a.CurrentPoint(); auto pos = pt.Position(); auto props = pt.Properties();
            bridge::PointerPod p;
            p.id = static_cast<long long>(pt.PointerId());
            const auto dev = pt.PointerDevice().PointerDeviceType();
            p.kind = dev == winrt::Windows::Devices::Input::PointerDeviceType::Touch ? 1 : dev == winrt::Windows::Devices::Input::PointerDeviceType::Pen ? 2 : 0;
            p.action = action; p.dipX = pos.X; p.dipY = pos.Y;
            p.left = props.IsLeftButtonPressed(); p.middle = props.IsMiddleButtonPressed(); p.right = props.IsRightButtonPressed();
            p.wheelDelta = action == 4 ? props.MouseWheelDelta() : 0;
            bridge::Pointer(m_engine, p);
        };
        w.PointerMoved([this, pointer](auto&&, wui::PointerEventArgs const& a) { pointer(a, 1); });
        w.PointerPressed([this, pointer](auto&&, wui::PointerEventArgs const& a) { pointer(a, 0); });
        w.PointerReleased([this, pointer](auto&&, wui::PointerEventArgs const& a) { pointer(a, 2); });
        w.PointerCaptureLost([this, pointer](auto&&, wui::PointerEventArgs const& a) { pointer(a, 3); });
        w.PointerWheelChanged([this, pointer](auto&&, wui::PointerEventArgs const& a) { pointer(a, 4); });
        // B on the pad: the system's back request. Handled here (so the system does not close the app abruptly); the loop leaves cleanly.
        wui::SystemNavigationManager::GetForCurrentView().BackRequested([this](auto&&, wui::BackRequestedEventArgs const& a) { a.Handled(true); bridge::BackRequested(m_engine); });
    }

    void Run() {
        m_window.Activate();
        std::string err;
        bool ok = false;
        try { ok = bridge::Run(m_engine, err); }   // on failure the engine side has logged `err` into the log file
        catch (const std::exception& ex) { bridge::Log(m_engine, std::string("Xbox app: the game stopped with an exception: ") + ex.what()); }
        catch (...) { bridge::Log(m_engine, "Xbox app: the game stopped with an unknown exception."); }
        if (!ok) bridge::Log(m_engine, "Xbox app: exiting after a failure" + (err.empty() ? std::string() : ": " + err));
        wac::CoreApplication::Exit();
    }

private:
    void OnSuspending(wf::IInspectable const&, winrt::Windows::ApplicationModel::SuspendingEventArgs const& a) {
        auto deferral = a.SuspendingOperation().GetDeferral();
        if (m_engine) bridge::Log(m_engine, "Xbox app: suspending (the system gives about 5 seconds).");
        if (m_engine) bridge::Suspending(m_engine, 4500);   // saves and trims synchronously
        deferral.Complete();   // Microsoft: complete within the deadline or the app is terminated
    }
    void OnResuming(wf::IInspectable const&, wf::IInspectable const&) { if (m_engine) { bridge::Log(m_engine, "Xbox app: resumed."); bridge::Resuming(m_engine); } }

    wui::CoreWindow m_window{nullptr};
    bridge::Engine* m_engine = nullptr;
    winrt::event_token m_susp{}, m_res{};
};

} // namespace okay_xbox_app
#endif
