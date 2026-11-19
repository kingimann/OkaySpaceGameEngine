#pragma once
// A fake okay::platform::Backend for the window-mode tests: scripted events, a clock the test sets, counters for what the loop asked of it.
#include "okay/Platform/Platform.hpp"

#include <deque>
#include <string>

namespace fakeplat {
namespace plat = okay::platform;
class FakeBackend : public plat::Backend {
public:
    std::deque<plat::Event> queue;
    uint64_t now = 0;
    int textStarts = 0, textStops = 0, rectCalls = 0; int rx = 0, ry = 0, rw = 0, rh = 0;
    plat::BackendKind Kind() const override { return plat::BackendKind::Own; }
    bool Init(const plat::InitFlags&) override { return true; }
    void Shutdown() override {}
    std::string LastError() const override { return ""; }
    uint64_t TicksMs() override { return now; }
    uint64_t PerfCounter() override { return now * 1000; }
    uint64_t PerfFrequency() override { return 1000000; }
    void DelayMs(uint32_t) override {}
    std::unique_ptr<plat::Window> MakeWindow(const plat::WindowDesc&) override { return nullptr; }
    bool PollEvent(plat::Event& out) override { if (queue.empty()) return false; out = queue.front(); queue.pop_front(); return true; }
    void* GLProcAddress(const char*) override { return nullptr; }
    void ReleaseCurrent() override {}
    void StartTextInput() override { ++textStarts; }
    void StopTextInput() override { ++textStops; }
    bool IsKeyDown(plat::Key) override { return false; }
    uint32_t MouseState(int* x, int* y) override { if (x) *x = 0; if (y) *y = 0; return 0; }
    bool SetRelativeMouse(bool) override { return false; }
    void ShowCursor(bool) override {}
    void WarpMouse(plat::Window*, int, int) override {}
    void SetTextInputRect(int x, int y, int w, int h) override { ++rectCalls; rx = x; ry = y; rw = w; rh = h; }
    bool SetClipboardText(const std::string&) override { return false; }
    std::string GetClipboardText() override { return ""; }
    std::unique_ptr<plat::AudioDevice> OpenAudio(const plat::AudioSpec&, bool, const char*, plat::AudioSpec*) override { return nullptr; }
    int AudioDeviceCount(bool) override { return 0; }
    std::string AudioDeviceName(int, bool) override { return ""; }
    int GamepadDeviceCount() override { return 0; }
    bool IsGamepad(int) override { return false; }
    std::unique_ptr<plat::Gamepad> OpenGamepad(int) override { return nullptr; }
};

inline plat::Event Ev(plat::EventType t) { plat::Event e; e.type = t; return e; }
inline plat::Event Move(int x, int y) { plat::Event e = Ev(plat::EventType::MouseMove); e.x = x; e.y = y; return e; }
inline plat::Event Btn(bool down, int x, int y) { plat::Event e = Ev(down ? plat::EventType::MouseButtonDown : plat::EventType::MouseButtonUp); e.x = x; e.y = y; e.button = plat::MouseLeft; return e; }
inline plat::Event Text(const char* t) { plat::Event e = Ev(plat::EventType::TextInput); e.text = t; return e; }


} // namespace fakeplat
