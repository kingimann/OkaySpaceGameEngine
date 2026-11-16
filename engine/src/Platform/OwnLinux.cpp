// okay::platform backend "own" for Linux: X11/GLX windowing, ALSA audio and evdev
// controllers, all loaded at run time (see OwnLinux.hpp). No SDL.
#include "Internal.hpp"
#include "OwnLinux.hpp"
#include "EventQueue.hpp"
#include <cstdlib>
#include <ctime>
#include <deque>

namespace okay::platform {
namespace {

uint64_t MonoNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

class OwnBackend final : public Backend {
public:
    OwnBackend() : m_epochNs(MonoNs()) {}
    BackendKind Kind() const override { return BackendKind::Own; }

    bool Init(const InitFlags& f) override {
        m_flags = f;
        if (f.video && !m_x11) {
            std::string err;
            m_x11 = own::X11System::Open(err);
            if (!m_x11) { m_err = err; return false; }
        }
        return true;
    }
    void Shutdown() override { m_x11.reset(); m_pending.Clear(); }
    std::string LastError() const override { return m_err; }

    uint64_t TicksMs() override { return (MonoNs() - m_epochNs) / 1000000ull; }
    uint64_t PerfCounter() override { return MonoNs(); }
    uint64_t PerfFrequency() override { return 1000000000ull; }
    void DelayMs(uint32_t ms) override {
        timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
        while (nanosleep(&ts, &ts) != 0) {}   // EINTR: sleep the rest
    }

    std::unique_ptr<Window> MakeWindow(const WindowDesc& d) override {
        if (!m_x11 && !Init(InitFlags())) return nullptr;
        std::string err;
        auto w = m_x11->MakeWindow(d, err);
        if (!w) m_err = err;
        return w;
    }
    bool PollEvent(Event& out) override {
        if (m_x11 && m_x11->PollEvent(out)) return true;
        if (m_flags.gamepad) {
            if (m_pending.Empty()) own::EvdevPollHotplug(m_pendingVec), Drain();
            if (m_pending.Poll(out)) return true;
        }
        return false;
    }
    void* GLProcAddress(const char* n) override { return m_x11 ? m_x11->GLProcAddress(n) : nullptr; }
    void ReleaseCurrent() override { if (m_x11) m_x11->ReleaseCurrent(); }

    void StartTextInput() override { if (m_x11) m_x11->StartTextInput(); }
    void StopTextInput() override { if (m_x11) m_x11->StopTextInput(); }
    bool IsKeyDown(Key k) override { return m_x11 && m_x11->IsKeyDown(k); }
    uint32_t MouseState(int* x, int* y) override {
        if (!m_x11) { if (x) *x = 0; if (y) *y = 0; return 0; }
        return m_x11->MouseState(x, y);
    }
    bool SetRelativeMouse(bool on) override { return m_x11 && m_x11->SetRelativeMouse(on); }
    void ShowCursor(bool s) override { if (m_x11) m_x11->ShowCursor(s); }
    void WarpMouse(Window* w, int x, int y) override { if (m_x11) m_x11->WarpMouse(w, x, y); }
    bool IsCursorShown() override { return !m_x11 || m_x11->IsCursorShown(); }
    bool SetCursor(CursorShape s) override { return m_x11 && m_x11->SetCursor(s); }
    int DisplayCount() override { return m_x11 ? m_x11->DisplayCount() : 0; }
    bool GetDisplayInfo(int i, DisplayInfo& out) override { return m_x11 && m_x11->GetDisplayInfo(i, out); }
    bool ShowErrorBox(const std::string& title, const std::string& text, Window* parent) override {
        if (m_x11 && m_x11->MessageBox(title, text, parent)) return true;
        return Backend::ShowErrorBox(title, text, parent);
    }
    uint32_t RelativeMouseState(int* dx, int* dy) override {
        if (!m_x11) { if (dx) *dx = 0; if (dy) *dy = 0; return 0; }
        return m_x11->RelativeMouseState(dx, dy);
    }
    bool SupportsRelativeMouseState() override { return true; }
    Key KeyForChar(char c) override { return m_x11 ? m_x11->KeyForChar(c) : Backend::KeyForChar(c); }

    bool SetClipboardText(const std::string& t) override { return m_x11 && m_x11->SetClipboardText(t); }
    std::string GetClipboardText() override { return m_x11 ? m_x11->GetClipboardText() : std::string(); }
    bool ClipboardPut(const std::string& mime, const std::vector<uint8_t>& d) override { return m_x11 && m_x11->ClipboardPut(mime, d); }
    bool ClipboardFetch(const std::string& mime, std::vector<uint8_t>& out) override { return m_x11 && m_x11->ClipboardFetch(mime, out); }
    std::vector<std::string> ClipboardFormats() override { return m_x11 ? m_x11->ClipboardFormats() : std::vector<std::string>(); }

    std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec& want, bool capture, const char* name, AudioSpec* got) override {
        std::string err;
        auto d = own::OpenAlsa(want, capture, name, got, err);
        if (!d) m_err = err;
        return d;
    }
    int AudioDeviceCount(bool capture) override { return own::AlsaDeviceCount(capture); }
    std::string AudioDeviceName(int i, bool capture) override { return own::AlsaDeviceName(i, capture); }

    int GamepadDeviceCount() override { return own::EvdevGamepadCount(); }
    bool IsGamepad(int i) override { return i >= 0 && i < own::EvdevGamepadCount(); }
    std::unique_ptr<Gamepad> OpenGamepad(int i) override { return own::OpenEvdevGamepad(i); }

    own::X11System* X11() { return m_x11.get(); }

private:
    void Drain() { for (auto& e : m_pendingVec) m_pending.Push(e); m_pendingVec.clear(); }
    uint64_t m_epochNs;
    InitFlags m_flags;
    std::unique_ptr<own::X11System> m_x11;
    std::string m_err;
    EventQueue m_pending;
    std::vector<Event> m_pendingVec;
};

} // namespace

std::unique_ptr<Backend> CreateOwnBackend() { return std::unique_ptr<Backend>(new OwnBackend()); }

namespace own {
/// Test hook: the pixels of one of our windows (see OwnLinux.hpp).
bool GrabWindowPixels(Backend& b, Window& w, std::vector<uint8_t>& rgba, int& width, int& height) {
    if (b.Kind() != BackendKind::Own) return false;
    X11System* x = static_cast<OwnBackend&>(b).X11();
    return x && x->GrabWindow(w, rgba, width, height);
}
} // namespace own

} // namespace okay::platform
