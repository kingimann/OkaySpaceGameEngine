// Engine side of the Xbox app, compiled as C++17 and linked with okay_engine_uwp (see engine_bridge.hpp for why it is separate from the shell).
// Holds the UwpBackend, the D3D11 renderer, the swap-chain presenter, WASAPI and the game runtime. EXPERIMENTAL, UNCOMPILED, UNVERIFIED:
// never through MSVC; the decisions it makes are in the pure headers tested on Linux (XboxLifecycle, XboxDisplay, XboxInput, XboxPaths, XboxAudio,
// XboxUwpBackend, XboxGameConfig). All #includes first, no using-directives.
#include "engine_bridge.hpp"

#include "UwpPlatform.hpp"
#include "XboxGameRuntime.hpp"
#include "XboxLog.hpp"
#include "XboxOverlay.hpp"
#include "XboxRecovery.hpp"
#include "XboxSuspend.hpp"
#include "XboxTextEntry.hpp"
#include "XboxSoftPainter.hpp"
#include "XboxPresent.hpp"
#include "XboxPadSync.hpp"
#include "XboxUwpBackend.hpp"
#include "XboxWasapi.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Paths.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Render/D3D11Renderer.hpp"
#include "okay/UI/UIRenderer.hpp"

#include <dxgi1_3.h>   // IDXGIDevice3::Trim

#include <chrono>
#include <memory>
#include <vector>

namespace okay_xbox_bridge {
namespace xb = okay::xbox;

namespace {
struct BridgePad : xb::PadDevice {
    const ShellCallbacks* cb = nullptr; int index = 0;
    bool Read(xb::PadReading& o) override {
        PadReadingPod p;
        if (!cb || !cb->padRead || !cb->padRead(cb->user, index, &p)) return false;
        o.buttons = p.buttons; o.leftTrigger = p.leftTrigger; o.rightTrigger = p.rightTrigger;
        o.leftX = p.leftX; o.leftY = p.leftY; o.rightX = p.rightX; o.rightY = p.rightY;
        return true;
    }
    void SendVibration(const xb::Vibration& v) override { if (cb && cb->padVibrate) cb->padVibrate(cb->user, index, v.leftMotor, v.rightMotor, v.leftTrigger, v.rightTrigger); }
};

// One pad, found by its stable key (the shell's padReadKey / padVibrateKey). Owned by XboxPadSync, freed only after the backend let go.
struct KeyedPad : xb::PadDevice {
    const ShellCallbacks* cb = nullptr; std::string key;
    bool Read(xb::PadReading& o) override {
        PadReadingPod p;
        if (!cb || !cb->padReadKey || !cb->padReadKey(cb->user, key, &p)) return false;
        o.buttons = p.buttons; o.leftTrigger = p.leftTrigger; o.rightTrigger = p.rightTrigger;
        o.leftX = p.leftX; o.leftY = p.leftY; o.rightX = p.rightX; o.rightY = p.rightY;
        return true;
    }
    void SendVibration(const xb::Vibration& v) override { if (cb && cb->padVibrateKey) cb->padVibrateKey(cb->user, key, v.leftMotor, v.rightMotor, v.leftTrigger, v.rightTrigger); }
    bool ReadBattery(xb::BatteryInfo& o) override {
        BatteryPod b;
        if (!cb || !cb->padBatteryKey || !cb->padBatteryKey(cb->user, key, &b)) return false;
        o.status = b.status; o.remainingMwh = b.remainingMwh; o.fullMwh = b.fullMwh;
        return true;
    }
};

struct BridgeHost : xb::UwpHost {
    ShellCallbacks cb;
    std::vector<std::unique_ptr<BridgePad>> pads;
    std::unique_ptr<xb::WasapiHost> audio;
    void* CoreWindowHandle() override { return cb.coreWindow ? cb.coreWindow(cb.user) : nullptr; }
    void PumpEvents() override { if (cb.pump) cb.pump(cb.user, false); }
    int PadCount() override {
        const int n = cb.padCount ? cb.padCount(cb.user) : 0;
        // Grow only: an open pad keeps a pointer to its BridgePad, so none is ever freed while the engine runs (before, all were
        // deleted whenever the count changed: a use after free on the next poll). The keyed path (XboxPadSync) is the one to use.
        while ((int)pads.size() < n) { auto p = std::make_unique<BridgePad>(); p->cb = &cb; p->index = (int)pads.size(); pads.push_back(std::move(p)); }
        return n;
    }
    xb::PadDevice* Pad(int i) override { return i >= 0 && i < (int)pads.size() ? pads[(std::size_t)i].get() : nullptr; }
    xb::AudioHost* Audio() override {
        if (!audio && cb.audioDeviceId) {
            wchar_t id[512] = {0};
            const int n = cb.audioDeviceId(cb.user, id, 511);
            if (n > 0) audio.reset(new xb::WasapiHost(std::wstring(id, (std::size_t)n)));
        }
        return audio.get();
    }
};
} // namespace

struct Engine {
    BridgeHost host;
    xb::PathRoots roots;
    okay::Paths::Env env;
    std::unique_ptr<xb::UwpBackend> backend;
    okay::D3D11Renderer renderer;
    xb::SwapChainPresenter presenter;
    xb::XboxGameRuntime runtime;
    xb::XboxSoftPainter painter;      // the 2D / UI layer, software-painted (premultiplied RGBA)
    xb::UiOverlay overlay;            // ... uploaded and blended over the 3D frame
    xb::OverlayState overlayState;
    xb::DeviceRecovery recovery;      // GPU device loss: rebuild with backoff, give up after a few tries
    xb::FileLog log;                  // LocalState\\logs\\okayspace.log: what the owner reads from the Device Portal
    xb::PadSync padSync;              // pads by stable key -> hub slots (when the shell gives padKeys)
    std::uint64_t suspendBudgetMs = 4000;   // what the current suspend leaves for saving (XboxSuspend.hpp FlushBudgetMs)
};

namespace {
// The pads connected now, from the shell, into the hub (once a frame, on the UI thread). No-op without padKeys.
void SyncPads(Engine* e) {
    if (!e->host.cb.padKeys) return;
    std::vector<std::string> keys;
    e->host.cb.padKeys(e->host.cb.user, &keys);
    const ShellCallbacks* cb = &e->host.cb;
    const int joined = e->padSync.Sync(*e->backend, keys, [cb](const std::string& k) {
        std::unique_ptr<KeyedPad> p(new KeyedPad()); p->cb = cb; p->key = k; return std::unique_ptr<xb::PadDevice>(p.release());
    });
    if (joined > 0) e->backend->Log("Xbox app: " + std::to_string(joined) + " controller(s) joined; " + std::to_string(e->padSync.Count()) + " connected.");
}
} // namespace

Engine* Create(const ShellCallbacks& cb, const std::string& installDir, const std::string& localDir, const std::string& tempDir) {
    std::unique_ptr<Engine> e(new Engine());
    e->host.cb = cb;
    e->roots.install = installDir; if (!e->roots.install.empty() && e->roots.install.back() != '/' && e->roots.install.back() != '\\') e->roots.install += "/";
    e->roots.local = localDir; e->roots.temp = tempDir;
    // The Paths override must be in place before anything asks for a per-user folder (docs/platform/xbox_uwp.md, "Files").
    e->env.appData = xb::DesktopDirToLocal(e->roots, xb::DesktopDir::RoamingAppData);
    e->env.localAppData = xb::DesktopDirToLocal(e->roots, xb::DesktopDir::LocalAppData);
    e->env.home = xb::DesktopDirToLocal(e->roots, xb::DesktopDir::Documents);
    okay::Paths::SetEnvOverride(&e->env);
    e->backend.reset(new xb::UwpBackend(&e->host, e->roots));
    {   // the log file first, so even a failure in the lines below leaves something to read
        const xb::PathResult lp = xb::Resolve(e->roots, xb::PathArea::Logs, "");
        if (lp.ok) e->log.Open(lp.path);
        Engine* raw = e.get();
        e->backend->logSink = [raw](const std::string& line) { raw->log.Line(line); };
        // The engine's own log lines (okay::Log: scene loading, save framework, scripts, the suspend summary) go into the same file.
        // Before 2026-11-22 only the backend's lines did.
        okay::Log::sink = [raw](okay::Log::Level lv, const std::string& msg) {
            static const char* const kTag[] = {"trace", "info", "warning", "error"};
            raw->log.Line(std::string("engine ") + kTag[(int)lv & 3] + ": " + msg);
        };
        e->backend->Log("OkaySpace Xbox app (EXPERIMENTAL, UNVERIFIED) starting. Install folder: " + e->roots.install + "  Local folder: " + e->roots.local);
        e->backend->Log(e->log.IsOpen() ? "Log file: " + e->log.Path() : "No log file (" + (lp.ok ? e->log.Error() : lp.error) + ")");
    }
    xb::InstallBackend(e->backend.get());
    e->backend->Init(okay::platform::InitFlags());
    if (cb.padKeys) e->backend->EnableHub(8, 10000);   // player slots that survive a short disconnect (XboxPadHub.hpp)
    return e.release();
}

void Log(Engine* e, const std::string& line) { if (e) e->backend->Log(line); }

void Destroy(Engine* e) { if (!e) return; okay::Log::sink = nullptr; e->padSync.Clear(*e->backend); xb::InstallBackend(nullptr); okay::Paths::SetEnvOverride(nullptr); delete e; }

void Resized(Engine* e, float w, float h, float dpi) { if (e) e->backend->Life().Resized(w, h, dpi); }
void Visibility(Engine* e, bool v) { if (e) e->backend->Life().VisibilityChanged(v); }
void Activated(Engine* e, bool a) { if (e) e->backend->Life().Activated(a); }
void Closed(Engine* e) { if (e) e->backend->Life().WindowClosed(); }
void BackRequested(Engine* e) { if (e) e->backend->Life().BackRequested(); }
void ScreenBounds(Engine* e, bool fullScreen, const std::string& family) {
    if (!e) return;
    const xb::SafeInsets i = xb::TitleSafeInsets(fullScreen ? xb::BoundsMode::FullScreen : xb::BoundsMode::SystemSafe, xb::IsXboxDeviceFamily(family));
    xb::XboxGameRuntime::SetTitleSafe(i);
    // A console has no keyboard on the desk: UI input fields bring up the system's on-screen keyboard (XboxTextEntry.hpp).
    e->runtime.EnableOnScreenKeyboard(xb::IsXboxDeviceFamily(family) && e->host.cb.textEntry != nullptr);
    e->backend->Log("Xbox app: device family " + (family.empty() ? std::string("unknown") : family) + ", " + (fullScreen ? "full screen" : "system TV-safe layout") +
                    ", UI title-safe inset " + std::to_string((int)(i.left * 100.0f + 0.5f)) + "% per edge.");
}
void Key(Engine* e, int vk, bool down, bool repeat) { if (e) e->backend->PushKey(vk, down, repeat); }
void Text(Engine* e, const std::string& s) { if (e) e->backend->PushText(s); }
void PointerMove(Engine* e, int x, int y) { if (e) e->backend->PushPointerMove(x, y); }
void PointerButton(Engine* e, int b, bool down, int x, int y) { if (e) e->backend->PushPointerButton(b, down, x, y); }
void Wheel(Engine* e, float n) { if (e) e->backend->PushWheel(n); }

void Suspending(Engine* e, long long deadlineTicks, long long nowTicks) {
    if (!e) return;
    const std::uint64_t remaining = xb::SuspendRemainingMs(deadlineTicks, nowTicks);
    e->suspendBudgetMs = xb::FlushBudgetMs(remaining);
    e->backend->Log("Xbox app: suspending, " + std::to_string(remaining) + " ms until the deadline, " + std::to_string(e->suspendBudgetMs) + " ms for saving.");
    e->backend->Life().Suspending(e->backend->TicksMs(), remaining);
    e->backend->ServiceLifecycle();   // stops rumble, runs onSuspend (settings + game state, in the budget) and marks the save done
    e->presenter.Release();           // the swap chain goes with the trim; Resuming rebuilds it
    if (void* dev = e->renderer.DeviceForPresent()) {
        IDXGIDevice3* d = nullptr;
        if (SUCCEEDED(static_cast<::IUnknown*>(dev)->QueryInterface(__uuidof(IDXGIDevice3), reinterpret_cast<void**>(&d))) && d) { d->Trim(); d->Release(); }
    }
}
void Resuming(Engine* e) { if (e) e->backend->Life().Resuming(); }
void KeyboardShown(Engine* e) { if (e) e->runtime.KeyboardShown(); }
void KeyboardHidden(Engine* e) { if (e) e->runtime.KeyboardHidden(); }
std::string KeyboardText(Engine* e, const std::string& utf8) { return e ? e->runtime.KeyboardText(utf8) : std::string(); }
void KeyboardSubmit(Engine* e) { if (e) e->runtime.KeyboardSubmit(); }

namespace {
bool SceneHasUi(okay::Scene& sc) {
    for (const auto& up : sc.Objects()) if (up && okay::IsUIElement(up.get())) return true;
    return false;
}
std::uint64_t NowMs() { return (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

bool Run(Engine* e, std::string& err) {
    if (!e) { err = "no engine"; return false; }
    auto fail = [&](const std::string& why) { e->backend->Log("Xbox app: " + why); err = why; return false; };
    okay::D3D11Renderer::SetShaderBundleDir((e->roots.install + "Shaders").c_str());
    // Everything that depends on the GPU device, in one place: used at start and again after a device loss.
    auto buildGpu = [&]() {
        if (!e->renderer.Init()) return false;
        if (!e->presenter.Create(e->renderer.DeviceForPresent(), e->host.CoreWindowHandle(), xb::ChooseBackBuffer(e->backend->Life().Size()))) return false;
        e->presenter.SetAfterRender([e](void* bb) { if (e->overlayState.Shown()) e->overlay.Draw(bb); });
        // The UI layer is optional: without the overlay shaders in the bundle the game runs without 2D / UI (logged).
        if (!e->overlay.Create(e->renderer.DeviceForPresent(), xb::UiBufferSize(e->presenter.Size()))) e->backend->Log("Xbox app: no UI overlay (VSOverlay / PSOverlay are not in the shader bundle): 2D / UI is not drawn.");
        return true;
    };
    if (!buildGpu()) return fail("The Direct3D 11 renderer or the swap chain could not start (see the log). package\\Shaders must hold shaders.manifest and the .cso files.");
    e->runtime.SetLocalDir(e->roots.local);   // saves, settings (Prefs) and the suspend snapshot (XboxSuspend.hpp SaveLayoutFor)
    // Text from CharacterReceived (a USB keyboard) and from the on-screen keyboard reaches UI input fields only while text input is on;
    // the desktop player turns it on for the whole run. Before 2026-11-22 nothing here did, so every typed character was dropped.
    e->backend->StartTextInput();
    if (e->host.cb.textEntry) {
        const ShellCallbacks* cb = &e->host.cb;
        e->runtime.onKeyboard = [cb](bool show, xb::TextScope scope, const std::string& text) { cb->textEntry(cb->user, show, (int)scope, text); };
    }
    e->backend->Log("Xbox app: shaders from " + e->roots.install + "Shaders; saves in " + e->roots.local + "/saves; game data in " + e->roots.install + "Data; pads connected: " + std::to_string(e->host.PadCount()));
    SyncPads(e);   // pads already plugged in get their slots before the game opens them (PadSet::OpenAll)
    std::string rerr;
    if (!e->runtime.Start(e->roots.install + "Data", rerr)) return fail(rerr);
    e->backend->onSuspend = [e] { e->runtime.OnSuspend(e->suspendBudgetMs); e->backend->Log("Xbox app: suspend save: " + e->runtime.LastSuspend().Summary()); };
    e->backend->onResume = [e] { e->runtime.OnResume(); };
    auto last = std::chrono::steady_clock::now();
    bool quit = false, lostReported = false;
    while (!quit) {
        SyncPads(e);
        okay::platform::Event ev;
        while (e->backend->PollEvent(ev)) {
            if (ev.type == okay::platform::EventType::Quit) quit = true;
            else if (ev.type == okay::platform::EventType::GamepadAdded) e->runtime.PadAdded(ev.deviceId);
            else if (ev.type == okay::platform::EventType::GamepadRemoved) e->runtime.PadRemoved(ev.deviceId);
            else if (ev.type == okay::platform::EventType::KeyDown && !ev.repeat) e->runtime.KeyTapped(*e->backend, ev.key);   // a tap shorter than a frame still counts
        }
        for (int n = e->backend->TakeBackPresses(); n > 0; --n) e->runtime.BackPressed();   // B on a console pad: closes a pause menu, never quits
        if (quit) break;
        xb::Lifecycle& life = e->backend->Life();
        if (!life.ShouldRender()) { if (e->host.cb.pump) e->host.cb.pump(e->host.cb.user, true); last = std::chrono::steady_clock::now(); continue; }
        const std::uint64_t now = NowMs();
        e->recovery.Tick(now);
        // ---- device loss: rebuild with backoff, never loop forever ----
        if (e->presenter.Valid() && e->presenter.Tracker().DeviceLost() && !lostReported) { lostReported = true; e->recovery.OnLost(now); e->backend->Log("Xbox app: the graphics device was lost; rebuilding it."); }
        if (e->recovery.GaveUp()) return fail(std::string("Giving up: ") + e->recovery.Reason() + " (" + std::to_string(e->recovery.Attempts()) + " tries).");
        if (!e->recovery.CanDraw()) {
            if (e->recovery.ShouldRebuild(now)) {
                e->overlay.Release(); e->presenter.Release(); e->renderer.Destroy();   // everything that held the old device
                const bool ok = buildGpu();
                if (ok) { e->presenter.Tracker().DeviceRebuilt(); lostReported = false; }
                e->recovery.RebuildDone(ok, NowMs());
                e->backend->Log(ok ? "Xbox app: the graphics device was rebuilt." : "Xbox app: rebuilding the graphics device failed; will retry.");
            } else Sleep(16);   // waiting for the next attempt
            last = std::chrono::steady_clock::now();
            continue;
        }
        if (!e->presenter.Valid()) {   // released at suspend: only the swap chain is rebuilt (same device)
            if (!e->presenter.Create(e->renderer.DeviceForPresent(), e->host.CoreWindowHandle(), xb::ChooseBackBuffer(life.Size()))) return fail("The swap chain could not be rebuilt after resuming.");
            e->presenter.SetAfterRender([e](void* bb) { if (e->overlayState.Shown()) e->overlay.Draw(bb); });
        }
        xb::PresentTracker& tr = e->presenter.Tracker();
        tr.WindowSized(life.Size());
        if (tr.NeedsResize() && !e->presenter.Resize(tr.Wanted())) return fail("Resizing the swap chain failed.");
        // ---- UI layer sizes and the mouse, before the scene updates (hit tests use them) ----
        const xb::SizePx buf = xb::UiBufferSize(e->presenter.Size()), layout = xb::UiLayoutSize(buf);
        e->runtime.SetUiSize(layout.w, layout.h);
        { int mx = 0, my = 0; const unsigned m = e->backend->MouseState(&mx, &my); const xb::SizePx win = life.Size();
          if (win.w > 0 && win.h > 0) e->runtime.FeedMouse((float)mx * (float)layout.w / (float)win.w, (float)my * (float)layout.h / (float)win.h, m); }
        const auto tnow = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(tnow - last).count(); last = tnow;
        std::vector<char> keys = xb::KeysDown(*e->backend);
        if (!e->runtime.Frame(dt, keys)) break;
        okay::Scene* scene = e->runtime.GetScene();
        // ---- 2D / UI layer: software-paint into the buffer, upload, blended in the afterRender hook ----
        if (scene && e->overlay.Valid()) {
            const xb::OverlayAction act = e->overlayState.Next(SceneHasUi(*scene));
            if (act != xb::OverlayAction::Nothing) {
                if (e->painter.Surface().Width() != buf.w || e->painter.Surface().Height() != buf.h) { e->painter.Resize(buf.w, buf.h); if (e->overlay.Size().w != buf.w || e->overlay.Size().h != buf.h) e->overlay.Resize(buf); }
                e->painter.SetDrawColor(0, 0, 0, 0); e->painter.Clear();   // transparent
                if (act == xb::OverlayAction::DrawAndUpload) {
                    e->painter.SetScale((float)buf.w / (float)layout.w, (float)buf.h / (float)layout.h);
                    okay::ui2d::RenderSceneUI(*scene, e->painter, layout.w, layout.h);
                    e->painter.SetScale(1.0f, 1.0f);
                }
                e->overlay.Upload(e->painter.Surface(), e->painter.Surface().TakeDirty());
            }
        }
        okay::Mat4 vp; okay::Vec3 eye;
        const xb::SizePx sz = e->presenter.Size();
        if (scene && e->runtime.View(sz.w, sz.h, vp, eye))
            e->presenter.RenderAndPresent(e->renderer, *scene, vp, eye, 4, 0.10f, 0.10f, 0.12f, 1.0f);
    }
    e->backend->Log("Xbox app: the game ended normally.");
    e->runtime.OnExit();
    return true;
}

} // namespace okay_xbox_bridge
