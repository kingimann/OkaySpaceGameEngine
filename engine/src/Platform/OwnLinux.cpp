// okay::platform backend "own" for Linux: X11/GLX windowing, ALSA audio and evdev
// controllers, all loaded at run time (see OwnLinux.hpp). No SDL.
#include "Internal.hpp"
#include "OwnLinux.hpp"
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace okay::platform {
namespace {

uint64_t MonoNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

class OwnBackend final : public Backend {
public:
    OwnBackend() : m_epochNs(MonoNs()) {
        // The wake pipe: WakeWait writes a byte, WaitEvents polls the read end next to the X connection (poll(2), pipe(2)).
        if (pipe(m_wakeFd) == 0) {
            for (int fd : m_wakeFd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); fcntl(fd, F_SETFD, FD_CLOEXEC); }
        } else {
            m_wakeFd[0] = m_wakeFd[1] = -1;
        }
    }
    ~OwnBackend() override { for (int fd : m_wakeFd) if (fd >= 0) close(fd); }
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
    void Shutdown() override { m_x11.reset(); m_pending.clear(); }
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
    // m_pending holds, in order: events PumpEvents already read, pushed events, and pad hot-plug events.
    bool PollEvent(Event& out) override {
        if (m_pending.empty() && m_x11 && m_x11->PollEvent(out)) return true;
        if (m_pending.empty() && m_flags.gamepad) own::EvdevPollHotplug(m_pendingVec), Drain();
        if (!m_pending.empty()) { out = std::move(m_pending.front()); m_pending.pop_front(); return true; }
        return false;
    }
    bool HasPendingEvents() override { return !m_pending.empty(); }   // PumpEvents moved what the X server had into m_pending
    void PumpEvents() override {
        Event e;
        while (m_x11 && m_x11->PollEvent(e)) m_pending.push_back(std::move(e));
    }
    // The idle wait: poll(2) on the X connection and the wake pipe. Xlib may already hold events it read from the socket,
    // which poll cannot see, so PumpEvents runs first and after every wake-up.
    bool WaitEvents(uint32_t timeoutMs) override {
        const int xfd = m_x11 ? m_x11->ConnectionFd() : -1;
        if (xfd < 0 || m_wakeFd[0] < 0) return Backend::WaitEvents(timeoutMs);
        PumpEvents();
        if (TakeWake() || !m_pending.empty()) return true;
        const uint64_t t0 = MonoNs(), limit = (uint64_t)timeoutMs * 1000000ull;
        for (;;) {
            const uint64_t spent = MonoNs() - t0;
            if (spent >= limit) return false;
            pollfd fds[2] = {{xfd, POLLIN, 0}, {m_wakeFd[0], POLLIN, 0}};
            const int left = (int)((limit - spent + 999999ull) / 1000000ull);
            const int r = poll(fds, 2, left);
            if (r < 0 && errno != EINTR) return false;
            char drain[64];
            while (read(m_wakeFd[0], drain, sizeof drain) > 0) {}
            PumpEvents();
            if (TakeWake() || !m_pending.empty()) return true;
            // The socket had data that made no event (a reply, a filtered key): wait out the rest.
        }
    }
    void WakeWait() override {
        NoteWake();
        if (m_wakeFd[1] >= 0) { const char b = 1; ssize_t n = write(m_wakeFd[1], &b, 1); (void)n; }   // a full pipe is already a wake
    }
    bool PushEvent(const Event& e) override {
        if (e.type == EventType::NoEvent || e.type == EventType::Other) return false;
        m_pending.push_back(e);
        return true;
    }
    void* GLProcAddress(const char* n) override { return m_x11 ? m_x11->GLProcAddress(n) : nullptr; }
    void ReleaseCurrent() override { if (m_x11) m_x11->ReleaseCurrent(); }
    GLCurrent CurrentGL() override { return m_x11 ? m_x11->CurrentGL() : GLCurrent(); }
    bool RestoreGL(const GLCurrent& s) override { return m_x11 && m_x11->RestoreGL(s); }

    void StartTextInput() override { if (m_x11) m_x11->StartTextInput(); }
    void StopTextInput() override { if (m_x11) m_x11->StopTextInput(); }
    bool IsKeyDown(Key k) override { return m_x11 && m_x11->IsKeyDown(k); }
    uint32_t KeyboardFocusWindowId() override { return m_x11 ? m_x11->KeyboardFocusWindowId() : 0; }
    uint32_t MouseFocusWindowId() override { return m_x11 ? m_x11->MouseFocusWindowId() : 0; }
    void* NativeHandleFromId(uint32_t id) override { return m_x11 ? m_x11->NativeHandleFromId(id) : nullptr; }
    uint32_t MouseState(int* x, int* y) override {
        if (!m_x11) { if (x) *x = 0; if (y) *y = 0; return 0; }
        return m_x11->MouseState(x, y);
    }
    uint32_t ModState() override { return m_x11 ? m_x11->ModState() : Backend::ModState(); }
    bool GlobalMouseState(int* x, int* y, uint32_t* buttons) override {
        if (!m_x11) return Backend::GlobalMouseState(x, y, buttons);
        return m_x11->GlobalMouseState(x, y, buttons);
    }
    bool IsRelativeMouse() override { return m_x11 && m_x11->IsRelativeMouse(); }
    bool SetRelativeMouse(bool on) override { return m_x11 && m_x11->SetRelativeMouse(on); }
    void ShowCursor(bool s) override { if (m_x11) m_x11->ShowCursor(s); }
    void WarpMouse(Window* w, int x, int y) override { if (m_x11) m_x11->WarpMouse(w, x, y); }
    bool IsCursorShown() override { return !m_x11 || m_x11->IsCursorShown(); }
    bool SetCursorShape(CursorShape shape) override { return m_x11 && m_x11->SetCursorShape(shape); }
    uint32_t RelativeMouseState(int* dx, int* dy) override {
        if (!m_x11) { if (dx) *dx = 0; if (dy) *dy = 0; return 0; }
        return m_x11->RelativeMouseState(dx, dy);
    }
    bool SupportsRelativeMouseState() override { return true; }
    Key KeyForChar(char c) override { return m_x11 ? m_x11->KeyForChar(c) : Backend::KeyForChar(c); }

    int DisplayCount() override { return m_x11 ? (int)m_x11->Displays().size() : 0; }
    bool DisplayAt(int i, DisplayInfo& o) override {
        if (!m_x11) return false;
        const std::vector<DisplayInfo> v = m_x11->Displays();
        if (i < 0 || i >= (int)v.size()) return false;
        o = v[(size_t)i];
        return true;
    }
    std::vector<DisplayMode> DisplayModes(int i) override { return m_x11 ? m_x11->DisplayModes(i) : std::vector<DisplayMode>(); }

    bool SetClipboardText(const std::string& t) override { return m_x11 && m_x11->SetClipboardText(t); }
    std::string GetClipboardText() override { return m_x11 ? m_x11->GetClipboardText() : std::string(); }

    std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec& want, bool capture, const char* name, AudioSpec* got) override {
        std::string err;
        auto d = NullAudioRequested() ? OpenNullAudio(want, capture, got, err) : own::OpenAlsa(want, capture, name, got, err);
        if (!d) m_err = err;
        return d;
    }
    int AudioDeviceCount(bool capture) override { return NullAudioRequested() ? 1 : own::AlsaDeviceCount(capture); }
    std::string AudioDeviceName(int i, bool capture) override {
        if (NullAudioRequested()) return i == 0 ? "null" : "";
        return own::AlsaDeviceName(i, capture);
    }

    int GamepadDeviceCount() override { return own::EvdevGamepadCount(); }
    bool IsGamepad(int i) override { return i >= 0 && i < own::EvdevGamepadCount(); }
    std::unique_ptr<Gamepad> OpenGamepad(int i) override { return own::OpenEvdevGamepad(i); }

    own::X11System* X11() { return m_x11.get(); }

private:
    void Drain() { for (auto& e : m_pendingVec) m_pending.push_back(std::move(e)); m_pendingVec.clear(); }
    uint64_t m_epochNs;
    int m_wakeFd[2] = {-1, -1};   // the wake pipe: [0] read end, [1] write end
    InitFlags m_flags;
    std::unique_ptr<own::X11System> m_x11;
    std::string m_err;
    std::deque<Event> m_pending;
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
