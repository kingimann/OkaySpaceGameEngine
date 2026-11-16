// okay::platform backend "own" for Linux: X11/GLX windowing, ALSA audio and evdev
// controllers, all loaded at run time (see OwnLinux.hpp). No SDL.
#include "Internal.hpp"
#include "OwnLinux.hpp"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <unistd.h>   // gethostname (PathsFromUriList)

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
        // A second Init adds subsystems and never turns one off (the launcher asks for audio alone after video is up).
        if (!m_initDone) m_flags = f;
        else { m_flags.video |= f.video; m_flags.audio |= f.audio; m_flags.gamepad |= f.gamepad; }
        m_initDone = true;
        if (f.video && !m_x11) {
            std::string err;
            m_x11 = own::X11System::Open(err);
            if (!m_x11) { m_err = err; return false; }
        }
        return true;
    }
    void Shutdown() override { m_x11.reset(); m_pending.clear(); m_initDone = false; }
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
            if (m_pending.empty()) own::EvdevPollHotplug(m_pendingVec), Drain();
            if (!m_pending.empty()) { out = std::move(m_pending.front()); m_pending.pop_front(); return true; }
        }
        return false;
    }
    void* GLProcAddress(const char* n) override { return m_x11 ? m_x11->GLProcAddress(n) : nullptr; }
    void ReleaseCurrent() override { if (m_x11) m_x11->ReleaseCurrent(); }

    void StartTextInput() override { if (m_x11) m_x11->StartTextInput(); }
    void StopTextInput() override { if (m_x11) m_x11->StopTextInput(); }
    bool IsKeyDown(Key k) override { return m_x11 && m_x11->IsKeyDown(k); }
    uint32_t KeyboardFocusWindowId() override { return m_x11 ? m_x11->KeyboardFocusWindowId() : 0; }
    uint32_t MouseFocusWindowId() override { return m_x11 ? m_x11->MouseFocusWindowId() : 0; }
    bool IsRelativeMouse() override { return m_x11 && m_x11->IsRelativeMouse(); }
    uint32_t GlobalMouseState(int* x, int* y) override {
        if (!m_x11) { if (x) *x = 0; if (y) *y = 0; return 0; }
        return m_x11->GlobalMouseState(x, y);
    }
    void PumpEvents() override { if (m_x11) m_x11->PumpEvents(); }
    uint32_t MouseState(int* x, int* y) override {
        if (!m_x11) { if (x) *x = 0; if (y) *y = 0; return 0; }
        return m_x11->MouseState(x, y);
    }
    bool SetRelativeMouse(bool on) override { return m_x11 && m_x11->SetRelativeMouse(on); }
    void ShowCursor(bool s) override { if (m_x11) m_x11->ShowCursor(s); }
    void WarpMouse(Window* w, int x, int y) override { if (m_x11) m_x11->WarpMouse(w, x, y); }
    bool IsCursorShown() override { return !m_x11 || m_x11->IsCursorShown(); }
    bool SetSystemCursor(SystemCursor c) override { return m_x11 && m_x11->SetSystemCursor(c); }
    uint32_t RelativeMouseState(int* dx, int* dy) override {
        if (!m_x11) { if (dx) *dx = 0; if (dy) *dy = 0; return 0; }
        return m_x11->RelativeMouseState(dx, dy);
    }
    bool SupportsRelativeMouseState() override { return true; }
    Key KeyForChar(char c) override { return m_x11 ? m_x11->KeyForChar(c) : Backend::KeyForChar(c); }

    bool SetClipboardText(const std::string& t) override { return m_x11 && m_x11->SetClipboardText(t); }
    std::string GetClipboardText() override { return m_x11 ? m_x11->GetClipboardText() : std::string(); }

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

    std::vector<DisplayInfo> Displays() override { return m_x11 ? m_x11->Displays() : std::vector<DisplayInfo>(); }
    std::vector<VideoMode> DisplayModes(int display) override { return m_x11 ? m_x11->DisplayModes(display) : std::vector<VideoMode>(); }
    bool CurrentDisplayMode(int display, VideoMode& out) override { return m_x11 && m_x11->CurrentDisplayMode(display, out); }
    bool ShowErrorBox(const std::string& title, const std::string& text, Window* parent) override {
        std::string err;
        if (own::X11MessageBox(title, text, err)) return true;
        return Backend::ShowErrorBox(title, text, parent);   // no X: the text goes to stderr
    }

    own::X11System* X11() { return m_x11.get(); }

private:
    void Drain() { for (auto& e : m_pendingVec) m_pending.push_back(std::move(e)); m_pendingVec.clear(); }
    uint64_t m_epochNs;
    InitFlags m_flags;
    bool m_initDone = false;
    std::unique_ptr<own::X11System> m_x11;
    std::string m_err;
    std::deque<Event> m_pending;
    std::vector<Event> m_pendingVec;
};

} // namespace

std::unique_ptr<Backend> CreateOwnBackend() { return std::unique_ptr<Backend>(new OwnBackend()); }

namespace own {
float ScaleFromXResources(const std::string& res) {
    // Lines "name:\tvalue"; the name may carry a leading '*' or '.' and spaces around the colon (Xlib resource syntax).
    size_t pos = 0;
    while (pos < res.size()) {
        size_t end = res.find('\n', pos);
        if (end == std::string::npos) end = res.size();
        std::string line = res.substr(pos, end - pos);
        pos = end + 1;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        size_t k0 = 0;
        while (k0 < key.size() && (key[k0] == ' ' || key[k0] == '\t' || key[k0] == '*' || key[k0] == '.')) ++k0;
        if (key.compare(k0, std::string::npos, "Xft.dpi") != 0) continue;
        const double dpi = std::strtod(line.c_str() + colon + 1, nullptr);
        if (dpi >= 24.0 && dpi <= 960.0) return (float)(dpi / 96.0);
        return 1.0f;
    }
    return 1.0f;
}

std::vector<std::string> PathsFromUriList(const std::string& list) {
    std::vector<std::string> out;
    char host[256] = {0};
    if (gethostname(host, sizeof(host) - 1) != 0) host[0] = 0;
    auto hexv = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    size_t pos = 0;
    while (pos < list.size()) {
        size_t end = list.find('\n', pos);
        if (end == std::string::npos) end = list.size();
        std::string line = list.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t' || line.back() == '\0')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.size() < 5 || (line.compare(0, 5, "file:") != 0 && line.compare(0, 5, "FILE:") != 0)) continue;
        std::string rest = line.substr(5);
        if (rest.compare(0, 2, "//") == 0) {   // file://host/path: the host must be this machine
            const size_t slash = rest.find('/', 2);
            if (slash == std::string::npos) continue;
            const std::string h = rest.substr(2, slash - 2);
            if (!h.empty() && h != "localhost" && (!host[0] || h != host)) continue;
            rest = rest.substr(slash);
        }
        if (rest.empty() || rest[0] != '/') continue;   // only absolute local paths
        std::string path;
        for (size_t i = 0; i < rest.size(); ++i) {
            if (rest[i] == '%' && i + 2 < rest.size() && hexv(rest[i + 1]) >= 0 && hexv(rest[i + 2]) >= 0) {
                path += (char)(hexv(rest[i + 1]) * 16 + hexv(rest[i + 2]));
                i += 2;
            } else path += rest[i];
        }
        out.push_back(path);
    }
    return out;
}

DisplayRect ClipToWorkArea(const DisplayRect& b, const DisplayRect& w) {
    if (w.w <= 0 || w.h <= 0) return b;
    const int x0 = std::max(b.x, w.x), y0 = std::max(b.y, w.y);
    const int x1 = std::min(b.x + b.w, w.x + w.w), y1 = std::min(b.y + b.h, w.y + w.h);
    if (x1 <= x0 || y1 <= y0) return b;
    return DisplayRect{x0, y0, x1 - x0, y1 - y0};
}

/// Test hook: the pixels of one of our windows (see OwnLinux.hpp).
bool GrabWindowPixels(Backend& b, Window& w, std::vector<uint8_t>& rgba, int& width, int& height) {
    if (b.Kind() != BackendKind::Own) return false;
    X11System* x = static_cast<OwnBackend&>(b).X11();
    return x && x->GrabWindow(w, rgba, width, height);
}
} // namespace own

} // namespace okay::platform
