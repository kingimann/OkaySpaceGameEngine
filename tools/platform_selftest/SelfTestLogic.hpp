#pragma once
// Pure logic of okay-platform-selftest: the tone generator, the event tally and the plain-text
// summary. No operating system calls, so tests/test_platform_selftest_logic.cpp checks it on Linux.
// Written for this program from general knowledge (a sine oscillator with a phase accumulator,
// zero crossings as a pitch check); nothing is taken from another project.
#include "okay/Platform/Platform.hpp"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay::selftest {

// ---- Tone ----------------------------------------------------------------------------------
/// A sine wave that keeps its phase between calls, so queued chunks join without a click.
class ToneGen {
public:
    ToneGen(double freqHz, int rate, int channels, double amplitude)
        : m_step(rate > 0 ? 2.0 * 3.14159265358979323846 * freqHz / rate : 0.0), m_ch(channels < 1 ? 1 : channels),
          m_amp(amplitude < 0 ? 0 : (amplitude > 1 ? 1 : amplitude)) {}
    /// Interleaved 16-bit samples for `frames` frames, the same value on every channel.
    void FillS16(int16_t* out, int frames) {
        for (int i = 0; i < frames; ++i) {
            const int16_t v = (int16_t)std::lround(std::sin(m_phase) * m_amp * 32767.0);
            for (int c = 0; c < m_ch; ++c) *out++ = v;
            Advance();
        }
    }
    void FillF32(float* out, int frames) {
        for (int i = 0; i < frames; ++i) {
            const float v = (float)(std::sin(m_phase) * m_amp);
            for (int c = 0; c < m_ch; ++c) *out++ = v;
            Advance();
        }
    }
private:
    void Advance() { m_phase += m_step; if (m_phase > 6.283185307179586) m_phase -= 6.283185307179586; }
    double m_phase = 0, m_step;
    int m_ch;
    double m_amp;
};

/// Number of upward zero crossings in a mono-or-interleaved stream (first channel only): the pitch in Hz
/// when the stream is exactly one second long.
inline int UpwardCrossings(const int16_t* s, int frames, int channels) {
    int n = 0;
    for (int i = 1; i < frames; ++i) if (s[(size_t)(i - 1) * channels] < 0 && s[(size_t)i * channels] >= 0) ++n;
    return n;
}

// ---- Names ---------------------------------------------------------------------------------
inline std::string KeyName(platform::Key k) {
    using platform::Key;
    const int v = (int)k;
    if (v >= (int)Key::A && v <= (int)Key::Z) return std::string(1, (char)('A' + v - (int)Key::A));
    if (v >= (int)Key::N1 && v <= (int)Key::N9) return std::string(1, (char)('1' + v - (int)Key::N1));
    if (k == Key::N0) return "0";
    if (v >= (int)Key::F1 && v <= (int)Key::F12) return "F" + std::to_string(v - (int)Key::F1 + 1);
    switch (k) {
    case Key::Return: return "Enter";  case Key::Escape: return "Esc";   case Key::Backspace: return "Backspace";
    case Key::Tab: return "Tab";       case Key::Space: return "Space";  case Key::Left: return "Left";
    case Key::Right: return "Right";   case Key::Up: return "Up";        case Key::Down: return "Down";
    case Key::LShift: return "LShift"; case Key::RShift: return "RShift"; case Key::LCtrl: return "LCtrl";
    case Key::RCtrl: return "RCtrl";   case Key::LAlt: return "LAlt";    case Key::RAlt: return "RAlt";
    case Key::Delete: return "Delete"; case Key::Insert: return "Insert"; case Key::Home: return "Home";
    case Key::End: return "End";       case Key::PageUp: return "PageUp"; case Key::PageDown: return "PageDown";
    case Key::Unknown: return "?";
    default: return "key#" + std::to_string(v);
    }
}

/// One plain line for an event, "" for events not worth printing (mouse motion is summarised, not listed).
inline std::string DescribeEvent(const platform::Event& e) {
    using platform::EventType;
    switch (e.type) {
    case EventType::KeyDown: return std::string("key down ") + KeyName(e.key) + (e.repeat ? " (repeat)" : "");
    case EventType::KeyUp: return "key up " + KeyName(e.key);
    case EventType::TextInput: return "text \"" + e.text + "\"";
    case EventType::MouseButtonDown: return "mouse button " + std::to_string(e.button) + " down at " + std::to_string(e.x) + "," + std::to_string(e.y) + " clicks=" + std::to_string(e.clicks);
    case EventType::MouseButtonUp: return "mouse button " + std::to_string(e.button) + " up";
    case EventType::MouseWheel: {
        char b[64];
        std::snprintf(b, sizeof b, "wheel %.2f,%.2f", (double)e.wheelX, (double)e.wheelY);
        return b;
    }
    case EventType::WindowResized: return "window resized to " + std::to_string(e.width) + "x" + std::to_string(e.height);
    case EventType::WindowFocusGained: return "window focus gained";
    case EventType::WindowFocusLost: return "window focus lost";
    case EventType::WindowClose: return "window close requested";
    case EventType::WindowDpiChanged: return "window DPI changed to " + std::to_string(e.x);
    case EventType::TextEditing: return "composing \"" + e.text + "\" caret " + std::to_string(e.x);
    case EventType::DropFile: return "file dropped: " + e.text;
    case EventType::GamepadAdded: return "gamepad added (index " + std::to_string(e.deviceId) + ")";
    case EventType::GamepadRemoved: return "gamepad removed (id " + std::to_string(e.deviceId) + ")";
    default: return std::string();
    }
}

/// Counts what arrived, so the summary can say "keyboard: 12 key presses" or "not tested".
struct EventTally {
    int keyDown = 0, keyUp = 0, text = 0, mouseMove = 0, mouseDown = 0, wheel = 0, resized = 0, focus = 0, drop = 0, padAdded = 0, other = 0;
    bool sawClose = false, sawEscape = false;
    int distinctKeys = 0;
    void Add(const platform::Event& e) {
        using platform::EventType;
        switch (e.type) {
        case EventType::KeyDown:
            if (!e.repeat) { ++keyDown; const int k = (int)e.key; if (k >= 0 && k < 512 && !m_seen[k]) { m_seen[k] = true; ++distinctKeys; } }
            if (e.key == platform::Key::Escape) sawEscape = true;
            break;
        case EventType::KeyUp: ++keyUp; break;
        case EventType::TextInput: ++text; break;
        case EventType::MouseMove: ++mouseMove; break;
        case EventType::MouseButtonDown: ++mouseDown; break;
        case EventType::MouseWheel: ++wheel; break;
        case EventType::WindowResized: ++resized; break;
        case EventType::WindowFocusGained: case EventType::WindowFocusLost: ++focus; break;
        case EventType::DropFile: ++drop; break;
        case EventType::GamepadAdded: ++padAdded; break;
        case EventType::Quit: case EventType::WindowClose: sawClose = true; break;
        case EventType::NoEvent: break;
        default: ++other; break;
        }
    }
private:
    bool m_seen[512] = {};
};

// ---- Report --------------------------------------------------------------------------------
enum class Status { Pass, Fail, Skip, Info };

struct Step { std::string name; Status status; std::string detail; };

class Report {
public:
    void Add(const std::string& name, Status s, const std::string& detail = std::string()) { m_steps.push_back({name, s, detail}); }
    int Count(Status s) const { int n = 0; for (const Step& st : m_steps) if (st.status == s) ++n; return n; }
    const std::vector<Step>& Steps() const { return m_steps; }
    /// 0 when nothing failed; 1 otherwise. A step that was skipped (nothing to test it with) is not a failure.
    int ExitCode() const { return Count(Status::Fail) ? 1 : 0; }

    /// The plain text printed at the end and saved next to the program.
    std::string Format(const std::string& title, const std::string& backend, const std::string& when) const {
        std::string o = title + "\n" + std::string(title.size(), '=') + "\n";
        o += "backend: " + backend + "\n";
        if (!when.empty()) o += "when: " + when + "\n";
        o += "\n";
        for (const Step& s : m_steps) {
            o += std::string("[") + Label(s.status) + "] " + s.name;
            if (!s.detail.empty()) o += " - " + s.detail;
            o += "\n";
        }
        o += "\n";
        const int f = Count(Status::Fail);
        o += std::to_string(Count(Status::Pass)) + " passed, " + std::to_string(f) + " failed, " + std::to_string(Count(Status::Skip)) + " not tested.\n";
        o += f ? "RESULT: FAILED. Send this file to the developer.\n" : "RESULT: nothing failed. Steps marked NOT TESTED need you to press, click or plug something in.\n";
        return o;
    }
    static const char* Label(Status s) {
        switch (s) { case Status::Pass: return "PASS"; case Status::Fail: return "FAIL"; case Status::Skip: return "NOT TESTED"; default: return "info"; }
    }
private:
    std::vector<Step> m_steps;
};

// ---- CI mode -------------------------------------------------------------------------------
/// A step that needs hardware a build machine may not have (an OpenGL driver, a sound card). On a person's PC a
/// failure is a FAIL. With --ci (a cloud runner: no GPU driver, no audio device) it is NOT TESTED, so the run says
/// honestly that the machine could not show it, and never a PASS. A success is a PASS either way.
inline Status HardwareStatus(bool ok, bool ci) { return ok ? Status::Pass : (ci ? Status::Skip : Status::Fail); }
/// The detail text for such a step: the reason, plus why it was not counted as a failure under --ci.
inline std::string HardwareDetail(const std::string& why, bool ci) {
    return ci ? why + " (counted as not tested: --ci, this machine may have none)" : why;
}

/// The event pump check: `pushedBack` = an event given to Backend::PushEvent came back through PollEvent;
/// `sawResize` / `evW` x `evH` = the last WindowResized event that arrived after Window::SetSize(askW, askH) (that one
/// goes through the operating system's own message queue); `nowW` x `nowH` = the size the window reports afterwards;
/// `beforeW` x `beforeH` = its size before. Pass when the pushed event came back, a resize event arrived, it carries
/// the size the window reports, and that size changed. The exact size asked for is not required: DPI scaling, a
/// window manager or a small screen can give another one.
inline Status PumpVerdict(bool pushedBack, bool sawResize, int evW, int evH, int nowW, int nowH, int beforeW, int beforeH,
                          int askW, int askH, std::string& detail) {
    auto sz = [](int w, int h) { return std::to_string(w) + "x" + std::to_string(h); };
    detail = pushedBack ? "a pushed event came back" : "the pushed event did not come back";
    detail += "; asked for " + sz(askW, askH) + ", the window reports " + sz(nowW, nowH);
    const bool changed = nowW != beforeW || nowH != beforeH;
    if (!sawResize) detail += ": no resize event arrived";
    else detail += " and the resize event said " + sz(evW, evH);
    if (!changed) detail += "; the size did not change from " + sz(beforeW, beforeH);
    return pushedBack && sawResize && changed && evW == nowW && evH == nowH ? Status::Pass : Status::Fail;
}

} // namespace okay::selftest
