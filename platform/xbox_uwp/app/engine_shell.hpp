// The engine mode of the Xbox app shell, WinRT side (C++20): CoreWindow events and Windows.Gaming.Input forwarded to the engine through the plain
// boundary in engine_bridge.hpp. No engine, no Direct3D and no engine header here. EXPERIMENTAL, UNCOMPILED, UNVERIFIED (never through MSVC).
// Rules: included by main.cpp AFTER every other #include; no global using-directive; ::IUnknown for COM, wf::IInspectable for WinRT.
// Written from Microsoft's documentation of: CoreApplication / IFrameworkView, CoreWindow events (SizeChanged, VisibilityChanged, Activated, KeyDown,
// KeyUp, PointerMoved / Pressed / Released, PointerWheelChanged, CharacterReceived, Closed), SystemNavigationManager.BackRequested,
// DisplayInformation.LogicalDpi, SuspendingOperation.GetDeferral / Deadline, Windows.Gaming.Input.Gamepad (and its
// IGameControllerBatteryInfo.TryGetBatteryReport), MediaDevice.GetDefaultAudioRenderId, Windows.UI.Text.Core (CoreTextEditContext) and
// InputPane (the on-screen keyboard).
#pragma once
#if defined(OKAY_XBOX_APP_ENGINE)
#include "engine_bridge.hpp"
#include "package_dir.hpp"
#include "XboxTextEntry.hpp"   // pure C++ (no engine header): the UTF-16 mirror of the edited text

#include <winrt/Windows.Devices.Power.h>
#include <winrt/Windows.System.Power.h>
#include <winrt/Windows.UI.Popups.h>
#include <winrt/Windows.UI.Text.Core.h>

#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <cstring>
#include <string>
#include <utility>
#include <vector>
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
        // Pads by a stable key: RawGameController.NonRoamableId (the same pad keeps its key across a reconnect). The list is
        // re-read once a frame on this (UI) thread from Gamepad::Gamepads(); the engine keeps player slots by key (XboxPadSync.hpp).
        cb.padKeys = [](void* u, std::vector<std::string>* out) {
            auto* self = static_cast<GameApp*>(u);
            self->m_padCache.clear();
            for (wgi::Gamepad const& g : wgi::Gamepad::Gamepads()) {
                std::string key;
                try { if (auto raw = wgi::RawGameController::FromGameController(g)) key = winrt::to_string(raw.NonRoamableId()); } catch (...) {}
                if (key.empty()) continue;   // no stable id: skipped rather than guessed
                self->m_padCache.emplace_back(key, g);
                out->push_back(key);
            }
        };
        cb.padReadKey = [](void* u, const std::string& key, bridge::PadReadingPod* o) -> bool {
            for (auto const& kv : static_cast<GameApp*>(u)->m_padCache) {
                if (kv.first != key) continue;
                const wgi::GamepadReading r = kv.second.GetCurrentReading();
                o->buttons = static_cast<std::uint32_t>(r.Buttons);
                o->leftTrigger = r.LeftTrigger; o->rightTrigger = r.RightTrigger;
                o->leftX = r.LeftThumbstickX; o->leftY = r.LeftThumbstickY; o->rightX = r.RightThumbstickX; o->rightY = r.RightThumbstickY;
                return true;
            }
            return false;
        };
        cb.padVibrateKey = [](void* u, const std::string& key, double l, double r, double lt, double rt) {
            for (auto const& kv : static_cast<GameApp*>(u)->m_padCache)
                if (kv.first == key) { wgi::GamepadVibration v{l, r, lt, rt}; kv.second.Vibration(v); return; }
        };
        cb.padBatteryKey = [](void* u, const std::string& key, bridge::BatteryPod* o) -> bool {
            for (auto const& kv : static_cast<GameApp*>(u)->m_padCache) {
                if (kv.first != key) continue;
                try {
                    auto rep = kv.second.TryGetBatteryReport();
                    if (!rep) return false;
                    o->status = static_cast<int>(rep.Status());
                    if (auto r = rep.RemainingCapacityInMilliwattHours()) o->remainingMwh = r.Value();
                    if (auto f = rep.FullChargeCapacityInMilliwattHours()) o->fullMwh = f.Value();
                    return true;
                } catch (...) { return false; }
            }
            return false;
        };
        cb.textEntry = [](void* u, bool show, int scope, const std::string& utf8) { static_cast<GameApp*>(u)->TextEntry(show, scope, utf8); };
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
        // A game draws to the whole TV, not only the system's TV-safe area; the UI then keeps itself inside the title-safe area
        // (XboxTitleSafe.hpp through bridge::ScreenBounds). SetDesiredBoundsMode returns false where it is refused.
        {
            bool full = false;
            try { full = winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView().SetDesiredBoundsMode(winrt::Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow); }
            catch (...) { full = false; }
            std::string family;
            try { family = winrt::to_string(winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily()); } catch (...) {}
            bridge::ScreenBounds(m_engine, full, family);
        }
        resize();
        w.SizeChanged([resize](auto&&, auto&&) { resize(); });
        w.VisibilityChanged([this](auto&&, wui::VisibilityChangedEventArgs const& a) { bridge::Visibility(m_engine, a.Visible()); });
        w.Activated([this](auto&&, wui::WindowActivatedEventArgs const& a) { bridge::Activated(m_engine, a.WindowActivationState() != wui::CoreWindowActivationState::Deactivated); });
        w.Closed([this](auto&&, auto&&) { bridge::Closed(m_engine); });
        w.KeyDown([this](auto&&, wui::KeyEventArgs const& a) { bridge::Key(m_engine, static_cast<int>(a.VirtualKey()), true, a.KeyStatus().WasKeyDown); });
        w.KeyUp([this](auto&&, wui::KeyEventArgs const& a) { bridge::Key(m_engine, static_cast<int>(a.VirtualKey()), false, false); });
        w.CharacterReceived([this](auto&&, wui::CharacterReceivedEventArgs const& a) {
            const std::uint32_t c = a.KeyCode(); wchar_t wc[2] = {static_cast<wchar_t>(c), 0};
            if (c >= 32 && c < 0xD800) bridge::Text(m_engine, winrt::to_string(winrt::hstring(wc)));
        });
        auto scale = [](float v) { return static_cast<int>(v * wgd::DisplayInformation::GetForCurrentView().LogicalDpi() / 96.0f + 0.5f); };
        w.PointerMoved([this, scale](auto&&, wui::PointerEventArgs const& a) { auto p = a.CurrentPoint().Position(); bridge::PointerMove(m_engine, scale(p.X), scale(p.Y)); });
        w.PointerPressed([this, scale](auto&&, wui::PointerEventArgs const& a) { auto p = a.CurrentPoint().Position(); bridge::PointerButton(m_engine, 1, true, scale(p.X), scale(p.Y)); });
        w.PointerReleased([this, scale](auto&&, wui::PointerEventArgs const& a) { auto p = a.CurrentPoint().Position(); bridge::PointerButton(m_engine, 1, false, scale(p.X), scale(p.Y)); });
        w.PointerWheelChanged([this](auto&&, wui::PointerEventArgs const& a) { bridge::Wheel(m_engine, a.CurrentPoint().Properties().MouseWheelDelta() / 120.0f); });
        // B on the pad: the system's back request. Marked handled (so the system does not close the app); the game gets a back press
        // (closes a pause menu) and keeps running. Leaving is the game's Quit or closing the window.
        SetUpTextEntry();
        wui::SystemNavigationManager::GetForCurrentView().BackRequested([this](auto&&, wui::BackRequestedEventArgs const& a) { a.Handled(true); bridge::BackRequested(m_engine); });
    }

    void Run() {
        m_window.Activate();
        std::string err;
        bool ok = false;
        try { ok = bridge::Run(m_engine, err); }   // on failure the engine side has logged `err` into the log file
        catch (const std::exception& ex) { bridge::Log(m_engine, std::string("Xbox app: the game stopped with an exception: ") + ex.what()); }
        catch (...) { bridge::Log(m_engine, "Xbox app: the game stopped with an unknown exception."); }
        if (!ok) {
            bridge::Log(m_engine, "Xbox app: exiting after a failure" + (err.empty() ? std::string() : ": " + err));
            ShowFailure(err);   // before 2026-11-22 the app just closed; the reason was only in the log file
        }
        wac::CoreApplication::Exit();
    }

private:
    void OnSuspending(wf::IInspectable const&, winrt::Windows::ApplicationModel::SuspendingEventArgs const& a) {
        auto deferral = a.SuspendingOperation().GetDeferral();
        if (m_engine) bridge::Log(m_engine, "Xbox app: suspending (the system gives about 5 seconds).");
        // The real deadline (DateTime, 100 ns ticks) instead of a fixed guess; the engine keeps 1 s back for the trim (XboxSuspend.hpp).
        const long long deadline = a.SuspendingOperation().Deadline().time_since_epoch().count();
        const long long now = winrt::clock::now().time_since_epoch().count();
        if (m_engine) bridge::Suspending(m_engine, deadline, now);   // stops rumble, saves settings + game state, trims, synchronously
        deferral.Complete();   // Microsoft: complete within the deadline or the app is terminated
    }
    void OnResuming(wf::IInspectable const&, wf::IInspectable const&) { if (m_engine) { bridge::Log(m_engine, "Xbox app: resumed."); bridge::Resuming(m_engine); } }

    // A message box with the reason and where the log is, then wait (keep pumping events) until the player closes it. A failure of
    // the dialog itself is ignored: the reason is in the log anyway.
    void ShowFailure(const std::string& reason) {
        try {
            winrt::Windows::UI::Popups::MessageDialog dlg(winrt::to_hstring(bridge::FailureMessage(m_engine, reason)), L"OkaySpace");
            auto op = dlg.ShowAsync();
            while (op.Status() == wf::AsyncStatus::Started) m_window.Dispatcher().ProcessEvents(wui::CoreProcessEventsOption::ProcessOneAndAllPending);
        } catch (...) {}
    }

    // ---- the on-screen keyboard: a CoreTextEditContext we drive by hand (no XAML text box in a CoreWindow game) ----
    void SetUpTextEntry() {
        namespace wtc = winrt::Windows::UI::Text::Core;
        try {
            m_textCtx = wtc::CoreTextServicesManager::GetForCurrentView().CreateEditContext();
            m_textCtx.InputPaneDisplayPolicy(wtc::CoreTextInputPaneDisplayPolicy::Manual);   // we decide when it shows
            m_textCtx.TextRequested([this](auto&&, wtc::CoreTextTextRequestedEventArgs const& a) {
                auto req = a.Request(); const wtc::CoreTextRange r = req.Range();
                const std::u16string& t = m_mirror.Utf16();
                const std::int32_t b = r.StartCaretPosition < 0 ? 0 : (r.StartCaretPosition > (std::int32_t)t.size() ? (std::int32_t)t.size() : r.StartCaretPosition);
                std::int32_t e = r.EndCaretPosition > (std::int32_t)t.size() ? (std::int32_t)t.size() : r.EndCaretPosition;
                if (e < b) e = b;
                req.Text(winrt::hstring(reinterpret_cast<const wchar_t*>(t.data() + b), static_cast<uint32_t>(e - b)));
            });
            m_textCtx.SelectionRequested([this](auto&&, wtc::CoreTextSelectionRequestedEventArgs const& a) {
                a.Request().Selection(wtc::CoreTextRange{m_mirror.Caret16(), m_mirror.Caret16()});
            });
            m_textCtx.SelectionUpdating([](auto&&, wtc::CoreTextSelectionUpdatingEventArgs const& a) { a.Result(wtc::CoreTextSelectionUpdatingResult::Succeeded); });
            m_textCtx.TextUpdating([this](auto&&, wtc::CoreTextTextUpdatingEventArgs const& a) {
                const winrt::hstring txt = a.Text(); const wtc::CoreTextRange r = a.Range();
                const std::u16string add(reinterpret_cast<const char16_t*>(txt.c_str()), txt.size());
                if (add == u"\r" || add == u"\n") {   // Enter / Done on the keyboard: the field submits, the text is not changed
                    a.Result(wtc::CoreTextTextUpdatingResult::Failed);
                    if (m_engine) bridge::KeyboardSubmit(m_engine);
                    return;
                }
                if (!m_mirror.Replace(r.StartCaretPosition, r.EndCaretPosition, add)) { a.Result(wtc::CoreTextTextUpdatingResult::Failed); return; }
                a.Result(wtc::CoreTextTextUpdatingResult::Succeeded);
                const std::string kept = m_engine ? bridge::KeyboardText(m_engine, m_mirror.Utf8()) : m_mirror.Utf8();
                if (kept != m_mirror.Utf8()) {   // the field refused part of it (length, digits only): tell the keyboard, after this handler
                    const std::int32_t before = m_mirror.Utf16Size();
                    m_mirror.Reset(kept);
                    const std::int32_t after = m_mirror.Utf16Size();
                    m_window.Dispatcher().RunAsync(wui::CoreDispatcherPriority::Normal, [this, before, after]() {
                        if (m_textCtx) m_textCtx.NotifyTextChanged(wtc::CoreTextRange{0, before}, after, wtc::CoreTextRange{after, after});
                    });
                }
            });
            m_textCtx.FocusRemoved([this](auto&&, auto&&) { if (m_engine) bridge::KeyboardHidden(m_engine); });
            auto pane = winrt::Windows::UI::ViewManagement::InputPane::GetForCurrentView();
            pane.Showing([this](auto&&, auto&&) { if (m_engine) bridge::KeyboardShown(m_engine); });
            pane.Hiding([this](auto&&, auto&&) { if (m_engine) bridge::KeyboardHidden(m_engine); });
        } catch (...) {
            m_textCtx = nullptr;
            if (m_engine) bridge::Log(m_engine, "Xbox app: no on-screen keyboard (the text edit context could not be made); a USB keyboard still types.");
        }
    }
    void TextEntry(bool show, int scope, const std::string& utf8) {
        namespace wtc = winrt::Windows::UI::Text::Core;
        if (!m_textCtx) return;
        auto pane = winrt::Windows::UI::ViewManagement::InputPane::GetForCurrentView();
        try {
            if (show) {
                m_mirror.Reset(utf8);
                m_textCtx.InputScope(scope == 1 ? wtc::CoreTextInputScope::Number : scope == 2 ? wtc::CoreTextInputScope::Password : wtc::CoreTextInputScope::Default);
                m_textCtx.NotifyFocusEnter();
                if (!pane.TryShow() && m_engine) bridge::Log(m_engine, "Xbox app: the on-screen keyboard did not show (InputPane.TryShow returned false).");
            } else {
                pane.TryHide();
                m_textCtx.NotifyFocusLeave();
            }
        } catch (...) { if (m_engine) bridge::Log(m_engine, "Xbox app: the on-screen keyboard failed to open or close."); }
    }

    wui::CoreWindow m_window{nullptr};
    winrt::Windows::UI::Text::Core::CoreTextEditContext m_textCtx{nullptr};
    okay::xbox::TextMirror m_mirror;   // what the edit context sees (UTF-16)
    std::vector<std::pair<std::string, wgi::Gamepad>> m_padCache;   // refreshed by padKeys once a frame (UI thread only)
    bridge::Engine* m_engine = nullptr;
    winrt::event_token m_susp{}, m_res{};
};

} // namespace okay_xbox_app
#endif
