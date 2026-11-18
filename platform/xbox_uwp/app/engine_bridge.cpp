// Engine side of the Xbox app, compiled as C++17 and linked with okay_engine_uwp (see engine_bridge.hpp for why it is separate from the shell).
// Holds the UwpBackend, the D3D11 renderer, the swap-chain presenter, WASAPI and the game runtime. EXPERIMENTAL, UNCOMPILED, UNVERIFIED:
// never through MSVC; the decisions it makes are in the pure headers tested on Linux (XboxLifecycle, XboxDisplay, XboxInput, XboxPaths, XboxAudio,
// XboxUwpBackend, XboxGameConfig). All #includes first, no using-directives.
#include "engine_bridge.hpp"

#include "UwpPlatform.hpp"
#include "XboxGameRuntime.hpp"
#include "XboxPresent.hpp"
#include "XboxUwpBackend.hpp"
#include "XboxWasapi.hpp"
#include "okay/Core/Paths.hpp"
#include "okay/Render/D3D11Renderer.hpp"

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

struct BridgeHost : xb::UwpHost {
    ShellCallbacks cb;
    std::vector<std::unique_ptr<BridgePad>> pads;
    std::unique_ptr<xb::WasapiHost> audio;
    void* CoreWindowHandle() override { return cb.coreWindow ? cb.coreWindow(cb.user) : nullptr; }
    void PumpEvents() override { if (cb.pump) cb.pump(cb.user, false); }
    int PadCount() override {
        const int n = cb.padCount ? cb.padCount(cb.user) : 0;
        if ((int)pads.size() != n) { pads.clear(); for (int i = 0; i < n; ++i) { auto p = std::make_unique<BridgePad>(); p->cb = &cb; p->index = i; pads.push_back(std::move(p)); } }
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
};

Engine* Create(const ShellCallbacks& cb, const std::string& installDir, const std::string& localDir, const std::string& tempDir) {
    std::unique_ptr<Engine> e(new Engine());
    e->host.cb = cb;
    e->roots.install = installDir; if (!e->roots.install.empty() && e->roots.install.back() != '/' && e->roots.install.back() != '\\') e->roots.install += "/";
    e->roots.local = localDir; e->roots.temp = tempDir;
    // The Paths override must be in place before anything asks for a per-user folder (docs/xbox_uwp.md, "Files").
    e->env.appData = xb::DesktopDirToLocal(e->roots, xb::DesktopDir::RoamingAppData);
    e->env.localAppData = xb::DesktopDirToLocal(e->roots, xb::DesktopDir::LocalAppData);
    e->env.home = xb::DesktopDirToLocal(e->roots, xb::DesktopDir::Documents);
    okay::Paths::SetEnvOverride(&e->env);
    e->backend.reset(new xb::UwpBackend(&e->host, e->roots));
    xb::InstallBackend(e->backend.get());
    e->backend->Init(okay::platform::InitFlags());
    return e.release();
}

void Destroy(Engine* e) { if (!e) return; xb::InstallBackend(nullptr); okay::Paths::SetEnvOverride(nullptr); delete e; }

void Resized(Engine* e, float w, float h, float dpi) { if (e) e->backend->Life().Resized(w, h, dpi); }
void Visibility(Engine* e, bool v) { if (e) e->backend->Life().VisibilityChanged(v); }
void Activated(Engine* e, bool a) { if (e) e->backend->Life().Activated(a); }
void Closed(Engine* e) { if (e) e->backend->Life().WindowClosed(); }
void BackRequested(Engine* e) { if (e) e->backend->Life().BackRequested(); }
void Key(Engine* e, int vk, bool down, bool repeat) { if (e) e->backend->PushKey(vk, down, repeat); }
void Text(Engine* e, const std::string& s) { if (e) e->backend->PushText(s); }
void PointerMove(Engine* e, int x, int y) { if (e) e->backend->PushPointerMove(x, y); }
void PointerButton(Engine* e, int b, bool down, int x, int y) { if (e) e->backend->PushPointerButton(b, down, x, y); }
void Wheel(Engine* e, float n) { if (e) e->backend->PushWheel(n); }

void Suspending(Engine* e, std::uint64_t deadlineMs) {
    if (!e) return;
    e->backend->Life().Suspending(e->backend->TicksMs(), deadlineMs);
    e->backend->ServiceLifecycle();   // runs onSuspend (the save) and marks the save done
    e->presenter.Release();           // the swap chain goes with the trim; Resuming rebuilds it
    if (void* dev = e->renderer.DeviceForPresent()) {
        IDXGIDevice3* d = nullptr;
        if (SUCCEEDED(static_cast<::IUnknown*>(dev)->QueryInterface(__uuidof(IDXGIDevice3), reinterpret_cast<void**>(&d))) && d) { d->Trim(); d->Release(); }
    }
}
void Resuming(Engine* e) { if (e) e->backend->Life().Resuming(); }

bool Run(Engine* e, std::string& err) {
    if (!e) { err = "no engine"; return false; }
    auto fail = [&](const std::string& why) { e->backend->Log("Xbox app: " + why); err = why; return false; };
    okay::D3D11Renderer::SetShaderBundleDir((e->roots.install + "Shaders").c_str());
    if (!e->renderer.Init()) return fail("The Direct3D 11 renderer could not start (see the log). package\\Shaders must hold shaders.manifest and the .cso files.");
    auto createChain = [&]() { return e->presenter.Create(e->renderer.DeviceForPresent(), e->host.CoreWindowHandle(), xb::ChooseBackBuffer(e->backend->Life().Size())); };
    if (!createChain()) return fail("The swap chain could not be created.");
    std::string rerr;
    if (!e->runtime.Start(e->roots.install + "Data", rerr)) return fail(rerr);
    e->backend->onSuspend = [e] { e->runtime.OnSuspend(); };
    e->backend->onResume = [e] { e->runtime.OnResume(); };
    auto last = std::chrono::steady_clock::now();
    bool quit = false;
    while (!quit) {
        okay::platform::Event ev;
        while (e->backend->PollEvent(ev)) {
            if (ev.type == okay::platform::EventType::Quit) quit = true;
            else if (ev.type == okay::platform::EventType::GamepadAdded) e->runtime.PadAdded(ev.deviceId);
            else if (ev.type == okay::platform::EventType::GamepadRemoved) e->runtime.PadRemoved(ev.deviceId);
        }
        if (quit) break;
        xb::Lifecycle& life = e->backend->Life();
        if (!life.ShouldRender()) { if (e->host.cb.pump) e->host.cb.pump(e->host.cb.user, true); last = std::chrono::steady_clock::now(); continue; }
        if (!e->presenter.Valid() && !createChain()) return fail("The swap chain could not be rebuilt after resuming.");
        xb::PresentTracker& tr = e->presenter.Tracker();
        tr.WindowSized(life.Size());
        if (tr.NeedsResize() && !e->presenter.Resize(tr.Wanted())) return fail("Resizing the swap chain failed.");
        if (tr.DeviceLost()) return fail("The graphics device was lost (no recovery in this version).");
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count(); last = now;
        std::vector<char> keys = xb::KeysDown(*e->backend);
        if (!e->runtime.Frame(dt, keys)) break;
        okay::Mat4 vp; okay::Vec3 eye;
        const xb::SizePx sz = e->presenter.Size();
        if (e->runtime.View(sz.w, sz.h, vp, eye) && e->runtime.GetScene())
            e->presenter.RenderAndPresent(e->renderer, *e->runtime.GetScene(), vp, eye, 4, 0.10f, 0.10f, 0.12f, 1.0f);
    }
    return true;
}

} // namespace okay_xbox_bridge
