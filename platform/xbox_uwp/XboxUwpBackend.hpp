#pragma once
// The UWP platform backend: `okay::platform::Backend` for the Xbox UWP app, in platform/xbox_uwp (not in the desktop backends). It is
// pure C++ over a small `UwpHost` interface, so the whole thing is tested on Linux with a fake host; the real host (CoreWindow,
// CoreDispatcher, Windows.Gaming.Input, WASAPI) is the Windows-only glue in app/ and XboxWasapi.hpp, UNCOMPILED and NEVER RUN.
// It reports Kind() == Own (it is our own code, not SDL). Written from docs/platform/platform_layer.md and Platform.hpp, and Microsoft's
// documentation of CoreWindow events (SizeChanged, VisibilityChanged, Activated, KeyDown/KeyUp, PointerMoved/Pressed/Released,
// PointerWheelChanged, CharacterReceived, Closed). No <thread>, no <mutex>.
#include "XboxAudio.hpp"
#include "XboxInput.hpp"
#include "XboxLifecycle.hpp"
#include "XboxPadHub.hpp"
#include "XboxPaths.hpp"
#include "XboxTouch.hpp"
#include "okay/Platform/Platform.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>

namespace okay::xbox {

/// What the backend needs from the Windows side.
struct UwpHost {
    virtual ~UwpHost() = default;
    virtual void* CoreWindowHandle() = 0;               // the CoreWindow as an IUnknown*
    virtual void PumpEvents() = 0;                      // CoreDispatcher.ProcessEvents(ProcessAllIfPresent): runs the handlers that feed the backend
    virtual int PadCount() = 0;                         // connected pads right now
    virtual PadDevice* Pad(int index) = 0;
    virtual AudioHost* Audio() = 0;                     // null: no audio output
    virtual bool PresentPixels(const std::uint8_t*, int, int) { return false; }   // optional: show a software frame
    virtual void SetTitle(const std::string&) {}        // ApplicationView.Title
    virtual std::vector<std::string> Locales() { return {"en-US"}; }
    virtual void ShowMessage(const std::string&, const std::string&) {}
    virtual int TouchDevices() { return 0; }            // PointerDevice.GetPointerDevices(): touch digitizers present (an Xbox usually has none)
};

class UwpBackend;

class UwpWindow : public platform::Window {
public:
    explicit UwpWindow(UwpBackend* b) : b_(b) {}
    std::uint32_t Id() const override { return 1; }
    void* NativeHandle() const override;
    void GetSize(int& w, int& h) const override;
    void GetPosition(int& x, int& y) const override { x = y = 0; }
    void SetTitle(const std::string& t) override;
    void SetSize(int, int) override {}                      // a UWP window is sized by the system
    void SetPosition(int, int) override {}
    void SetMinimumSize(int, int) override {}
    void Show() override {} void Hide() override {} void Raise() override {} void Minimize() override {} void Restore() override {}
    void SetFullscreen(bool) override {}
    bool IsFocused() const override;
    bool IsFullscreen() const override { return true; }     // an Xbox UWP app is always full screen
    bool PresentPixels(const std::uint8_t* rgba, int w, int h) override;
    bool CreateGLContext(const platform::GLAttributes&) override { return false; }   // no OpenGL in UWP
    void DestroyGLContext() override {}
    bool MakeCurrent() override { return false; }
    void SwapBuffers() override {}
    bool SetSwapInterval(int) override { return false; }
    void GetDrawableSize(int& w, int& h) const override { GetSize(w, h); }
private:
    UwpBackend* b_;
};

class UwpBackend : public platform::Backend {
public:
    UwpBackend(UwpHost* host, const PathRoots& roots) : host_(host), roots_(roots), t0_(std::chrono::steady_clock::now()) {}

    // ---- pushed by the Windows glue from CoreWindow / CoreApplication handlers ----
    Lifecycle& Life() { return life_; }
    void PushKey(int virtualKey, bool down, bool repeat = false) {
        const platform::Key k = KeyFromVirtualKey(virtualKey); if (k == platform::Key::Unknown) return;
        const bool was = keys_.count((int)k) != 0;
        if (down) keys_.insert((int)k); else keys_.erase((int)k);
        if (down == was && !repeat) return;   // no change and not an auto-repeat
        platform::Event e = Base(down ? platform::EventType::KeyDown : platform::EventType::KeyUp); e.key = k; e.repeat = down && was; e.mods = ModState(); q_.push_back(e);
    }
    void PushText(const std::string& utf8) { if (!textInput_ || utf8.empty()) return; platform::Event e = Base(platform::EventType::TextInput); e.text = utf8; q_.push_back(e); }
    void PushPointerMove(int x, int y) { platform::Event e = Base(platform::EventType::MouseMove); e.dx = x - mx_; e.dy = y - my_; mx_ = x; my_ = y; e.x = x; e.y = y; relDx_ += e.dx; relDy_ += e.dy; q_.push_back(e); }
    void PushPointerButton(int button, bool down, int x, int y) {
        mx_ = x; my_ = y;
        if (down) mouse_ |= platform::MouseMask(button); else mouse_ &= ~platform::MouseMask(button);
        platform::Event e = Base(down ? platform::EventType::MouseButtonDown : platform::EventType::MouseButtonUp); e.button = button; e.x = x; e.y = y; e.clicks = 1; q_.push_back(e);
    }
    /// Touch / pen pointer from PointerPressed / Moved / Released / Canceled. Raw points become FingerDown / FingerMotion / FingerUp
    /// (fx, fy are 0..1 of the window). The primary finger also drives the mouse (left button), so mouse-driven UI works with a touch;
    /// recognised taps and drags are available through TakeGestures. UNVERIFIED with a real touch screen.
    void PushTouch(long long id, PointerPhase ph, int x, int y) { Route(touch_.Feed(id, ph, x, y, TicksMs())); }
    /// The window lost focus or the app suspends: every finger ends as a cancel (the mouse button is released too).
    void CancelTouches() { Route(touch_.CancelAll()); }
    std::vector<TouchEvent> TakeGestures() { std::vector<TouchEvent> v; v.swap(gestures_); return v; }
    TouchTracker& Touch() { return touch_; }
    /// Text composition (IME / on-screen keyboard): TextEditing while composing (text = composed string, x = caret in code points),
    /// TextEditing "" when it ends, TextInput on commit. Only while text input is on. UNVERIFIED with a real IME.
    void PushComposition(const std::string& utf8, int caret) {
        if (!textInput_ || !comp_.Update(utf8, caret)) return;
        platform::Event e = Base(platform::EventType::TextEditing); e.text = comp_.Active() ? comp_.Text() : std::string(); e.x = comp_.Caret(); q_.push_back(e);
    }
    void PushCompositionCommit(const std::string& finalText = std::string()) {
        const bool was = comp_.Active(); const std::string t = comp_.Commit(finalText);
        if (!textInput_) return;
        if (was) q_.push_back(Base(platform::EventType::TextEditing));
        PushText(t);
    }
    void PushWheel(float notchesY) { platform::Event e = Base(platform::EventType::MouseWheel); e.wheelY = notchesY; q_.push_back(e); }
    void PushPadAdded(int index) { platform::Event e = Base(platform::EventType::GamepadAdded); e.deviceId = index; q_.push_back(e); }
    void PushPadRemoved(int instanceId) { platform::Event e = Base(platform::EventType::GamepadRemoved); e.deviceId = instanceId; q_.push_back(e); }
    /// Hot-plug and player slots (XboxPadHub.hpp). The glue calls PadConnected from Gamepad.GamepadAdded (key = NonRoamableId) and PadDisconnected
    /// from GamepadRemoved. Once EnableHub or PadConnected ran, the slot number is the device index, GamepadAdded carries the slot and
    /// GamepadRemoved the pad's instance id (sent only after the grace time). A returning pad keeps its slot and instance id. UNVERIFIED with real pads.
    void EnableHub(int maxSlots = 8, std::uint64_t graceMs = 10000) { hub_.reset(new PadHub(maxSlots, graceMs)); proxies_.clear(); proxies_.resize((size_t)hub_->SlotCount()); }
    int PadConnected(const std::string& key, PadDevice* dev) {
        if (!hub_) EnableHub();
        const int slot = hub_->OnAdded(key, Now());
        if (slot >= 0) proxies_[(size_t)slot].target = dev;
        Translate(); return slot;
    }
    void PadDisconnected(const std::string& key) { if (!hub_) return; const int slot = hub_->SlotOf(key); hub_->OnRemoved(key, Now()); if (slot >= 0) proxies_[(size_t)slot].target = nullptr; Translate(); }
    PadHub* Hub() { return hub_.get(); }
    /// Replace the clock the hub uses (tests); empty = the backend's own TicksMs.
    std::function<std::uint64_t()> clockOverride;
    /// Called (once per suspend) from PollEvent when the app is suspending, BEFORE SaveDone: write the game's state here.
    std::function<void()> onSuspend;
    std::function<void()> onResume;

    // ---- Backend ----
    platform::BackendKind Kind() const override { return platform::BackendKind::Own; }
    bool Init(const platform::InitFlags&) override { inited_ = true; err_.clear(); return true; }
    void Shutdown() override { inited_ = false; }
    std::string LastError() const override { return err_; }
    std::uint64_t TicksMs() override { return (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_).count(); }
    std::uint64_t PerfCounter() override { return (std::uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0_).count(); }
    std::uint64_t PerfFrequency() override { return 1000000000ull; }
    void DelayMs(std::uint32_t) override {}   // a UWP game loop paces itself with Present(1); a sleep would stall the dispatcher
    std::unique_ptr<platform::Window> MakeWindow(const platform::WindowDesc&) override { return std::unique_ptr<platform::Window>(new UwpWindow(this)); }

    bool PollEvent(platform::Event& out) override {
        if (q_.empty()) { host_->PumpEvents(); Translate(); }
        if (q_.empty()) return false;
        out = q_.front(); q_.pop_front(); return true;
    }
    void* GLProcAddress(const char*) override { return nullptr; }
    void ReleaseCurrent() override {}
    void StartTextInput() override { textInput_ = true; }
    void StopTextInput() override { if (comp_.Cancel() && textInput_) q_.push_back(Base(platform::EventType::TextEditing)); textInput_ = false; }
    void SetTextInputRect(int x, int y, int w, int h) override { imeRect_[0] = x; imeRect_[1] = y; imeRect_[2] = w; imeRect_[3] = h; }   // where the app wants the on-screen keyboard / candidates
    const int* TextInputRect() const { return imeRect_; }
    int TouchDeviceCount() override { return host_->TouchDevices(); }
    bool IsKeyDown(platform::Key k) override { return keys_.count((int)k) != 0; }
    std::uint32_t MouseState(int* x, int* y) override { if (x) *x = mx_; if (y) *y = my_; return mouse_; }
    bool SetRelativeMouse(bool on) override { rel_ = on; return true; }
    void ShowCursor(bool s) override { cursor_ = s; }
    void WarpMouse(platform::Window*, int x, int y) override { mx_ = x; my_ = y; }
    bool IsCursorShown() override { return cursor_; }
    std::uint32_t RelativeMouseState(int* dx, int* dy) override { if (dx) *dx = relDx_; if (dy) *dy = relDy_; relDx_ = relDy_ = 0; return mouse_; }
    bool SupportsRelativeMouseState() override { return true; }
    platform::Key KeyForChar(char c) override {   // a US layout (CoreWindow gives virtual keys, not the layout): documented limit
        if (c >= 'a' && c <= 'z') return (platform::Key)((int)platform::Key::A + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return (platform::Key)((int)platform::Key::A + (c - 'A'));
        if (c >= '1' && c <= '9') return (platform::Key)((int)platform::Key::N1 + (c - '1'));
        if (c == '0') return platform::Key::N0;
        if (c == ' ') return platform::Key::Space;
        return platform::Key::Unknown;
    }
    bool ShowErrorBox(const std::string& title, const std::string& text, platform::Window*) override { Log(title + ": " + text); host_->ShowMessage(title, text); return true; }
    std::string BasePath() override { std::string p = TrimEndSlash(Slash(roots_.install)); return p.empty() ? p : p + "/"; }
    std::string PrefPath(const std::string&, const std::string& app) override {
        PathResult r = Resolve(roots_, PathArea::Saves, app.empty() ? std::string() : app);
        if (!r.ok) return std::string();
        std::error_code ec; std::filesystem::create_directories(r.path, ec);
        return r.path + "/";
    }
    std::vector<std::string> PreferredLocales() override { return host_->Locales(); }
    void Log(const std::string& line) override { log_.push_back(line); if (log_.size() > 200) log_.pop_front(); if (logSink) logSink(line); }
    /// Also receives every line (the app sets it to a FileLog, XboxLog.hpp, so the owner can read the log from the Device Portal).
    std::function<void(const std::string&)> logSink;
    const std::deque<std::string>& LogLines() const { return log_; }
    bool SetClipboardText(const std::string& t) override { clip_ = t; return true; }   // in-app only (no Windows clipboard yet)
    std::string GetClipboardText() override { return clip_; }

    std::unique_ptr<platform::AudioDevice> OpenAudio(const platform::AudioSpec& want, bool capture, const char*, platform::AudioSpec* got) override {
        if (capture) { err_ = "Microphone capture is not supported in the Xbox UWP build yet."; return nullptr; }
        AudioHost* ah = host_->Audio();
        AudioOutputFormat fmt;
        if (!ah || !ah->Format(fmt)) { err_ = "No audio output is available."; return nullptr; }
        std::unique_ptr<UwpAudioDevice> d(new UwpAudioDevice(want, fmt, ah));
        std::string e; if (!d->Begin(e)) { err_ = e; return nullptr; }
        if (got) *got = d->Spec();
        return std::unique_ptr<platform::AudioDevice>(d.release());
    }
    int AudioDeviceCount(bool capture) override { return capture ? 0 : (host_->Audio() ? 1 : 0); }
    std::string AudioDeviceName(int index, bool capture) override { return (!capture && index == 0 && host_->Audio()) ? "Default audio output" : ""; }

    int GamepadDeviceCount() override { return hub_ ? hub_->SlotCount() : host_->PadCount(); }
    bool IsGamepad(int i) override { return hub_ ? hub_->Present(i) : (i >= 0 && i < host_->PadCount()); }
    std::unique_ptr<platform::Gamepad> OpenGamepad(int i) override {
        if (hub_) {
            if (!hub_->Present(i)) return nullptr;
            std::unique_ptr<BackendPad> p(new BackendPad(this, hub_->InstanceId(i), &proxies_[(size_t)i]));
            pads_.push_back(p.get());
            return std::unique_ptr<platform::Gamepad>(p.release());
        }
        if (!IsGamepad(i)) return nullptr;
        PadDevice* d = host_->Pad(i); if (!d) return nullptr;
        std::unique_ptr<BackendPad> p(new BackendPad(this, nextPadId_++, d));
        pads_.push_back(p.get());
        return std::unique_ptr<platform::Gamepad>(p.release());
    }

    UwpHost* Host() { return host_; }
    const PathRoots& Roots() const { return roots_; }

private:
    struct BackendPad : UwpGamepad {
        BackendPad(UwpBackend* b, int id, PadDevice* d) : UwpGamepad(id, d), b_(b) {}
        ~BackendPad() override { auto& v = b_->pads_; v.erase(std::remove(v.begin(), v.end(), this), v.end()); }
        UwpBackend* b_;
    };
    // Forwards to whichever real pad is in the slot now; a slot with no pad reads as disconnected.
    struct SlotProxy : PadDevice {
        PadDevice* target = nullptr;
        bool Read(PadReading& o) override { return target && target->Read(o); }
        void SendVibration(const Vibration& v) override { if (target) target->SendVibration(v); }
        const char* Name() const override { return target ? target->Name() : "Xbox controller"; }
        bool HasTriggerMotors() const override { return target ? target->HasTriggerMotors() : true; }
    };
    void Route(const std::vector<TouchEvent>& evs) {
        const SizePx sz = life_.Size(); const float w = sz.w > 0 ? (float)sz.w : 1.0f, h = sz.h > 0 ? (float)sz.h : 1.0f;
        for (const TouchEvent& g : evs) {
            const bool down = g.kind == TouchEvent::RawDown, move = g.kind == TouchEvent::RawMove;
            if (!down && !move && g.kind != TouchEvent::RawUp && g.kind != TouchEvent::RawCancel) { gestures_.push_back(g); if (gestures_.size() > 256) gestures_.erase(gestures_.begin()); continue; }
            platform::Event e = Base(down ? platform::EventType::FingerDown : move ? platform::EventType::FingerMotion : platform::EventType::FingerUp);
            e.fingerId = g.id; e.fx = (float)g.x / w; e.fy = (float)g.y / h; e.x = g.x; e.y = g.y; q_.push_back(e);
            if (!g.primary) continue;
            if (move) PushPointerMove(g.x, g.y);
            else if (down) { PushPointerMove(g.x, g.y); PushPointerButton(platform::MouseLeft, true, g.x, g.y); }
            else PushPointerButton(platform::MouseLeft, false, g.x, g.y);
        }
    }
    std::uint64_t Now() { return clockOverride ? clockOverride() : TicksMs(); }
    platform::Event Base(platform::EventType t) { platform::Event e; e.type = t; e.timestampMs = (std::uint32_t)TicksMs(); e.windowId = 1; return e; }
public:
    /// Turn lifecycle changes into engine events (and run onSuspend / onResume) and poll the pads. PollEvent does this when its queue is
    /// empty; the Windows glue also calls it from the Suspending handler, so the state is saved before the system's deadline.
    void ServiceLifecycle() { Translate(); }
private:
    void Translate() {
        const std::uint64_t now = TicksMs();
        if (hub_) {
            hub_->Tick(Now());
            for (const PadHubEvent& e : hub_->TakeEvents()) {
                if (e.kind == PadHubEvent::Added || e.kind == PadHubEvent::Reconnected) { platform::Event ev = Base(platform::EventType::GamepadAdded); ev.deviceId = e.slot; q_.push_back(ev); }
                else if (e.kind == PadHubEvent::Removed) { platform::Event ev = Base(platform::EventType::GamepadRemoved); ev.deviceId = e.instanceId; q_.push_back(ev); proxies_[(size_t)e.slot].target = nullptr; }
            }
        }
        for (BackendPad* p : pads_) p->Poll(now);
        const LifecyclePending p = life_.Take();
        if (p.saveState) {
            q_.push_back(Base(platform::EventType::WindowFocusLost));
            if (onSuspend) onSuspend();
            life_.SaveDone();
        }
        if (p.resized) { platform::Event e = Base(platform::EventType::WindowResized); e.width = life_.Size().w; e.height = life_.Size().h; q_.push_back(e); }
        if (p.resumed) { q_.push_back(Base(platform::EventType::WindowFocusGained)); if (onResume) onResume(); }
        if (p.exitRequested && !quitSent_) { quitSent_ = true; q_.push_back(Base(platform::EventType::Quit)); }
    }
    friend class UwpWindow;
    UwpHost* host_; PathRoots roots_; std::chrono::steady_clock::time_point t0_;
    Lifecycle life_; std::deque<platform::Event> q_; std::set<int> keys_; std::vector<BackendPad*> pads_; int nextPadId_ = 1; std::unique_ptr<PadHub> hub_; std::vector<SlotProxy> proxies_;
    std::deque<std::string> log_; std::string clip_, err_;
    int mx_ = 0, my_ = 0, relDx_ = 0, relDy_ = 0; std::uint32_t mouse_ = 0;
    TouchTracker touch_; std::vector<TouchEvent> gestures_; TextComposition comp_; int imeRect_[4] = {0, 0, 0, 0};
    bool inited_ = false, textInput_ = false, rel_ = false, cursor_ = true, quitSent_ = false;
};

inline void* UwpWindow::NativeHandle() const { return b_->host_->CoreWindowHandle(); }
inline void UwpWindow::GetSize(int& w, int& h) const { const SizePx s = b_->life_.Size(); w = s.w; h = s.h; }
inline void UwpWindow::SetTitle(const std::string& t) { b_->host_->SetTitle(t); }
inline bool UwpWindow::IsFocused() const { return b_->life_.Focused() && b_->life_.Visible(); }
inline bool UwpWindow::PresentPixels(const std::uint8_t* rgba, int w, int h) { return b_->host_->PresentPixels(rgba, w, h); }

} // namespace okay::xbox
