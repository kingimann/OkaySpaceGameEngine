// okay::platform without a display: the registry, timers, audio devices and
// controllers on every backend that is compiled in (sdl2 and own side by side),
// plus the own backend's evdev mapping fed through a pipe. Windows, events,
// GL and the clipboard need an X server: see test_platform_window.cpp.
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#if defined(__linux__)
#include "Platform/OwnLinux.hpp"
#include <linux/input.h>
#include <unistd.h>
#endif
#include <string>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#if defined(__has_include)
#if __has_include(<SDL_scancode.h>)
#include <SDL_scancode.h>
#define OKAY_TEST_HAVE_SDL_HEADERS 1
#endif
#endif

using namespace okay::platform;

namespace {

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

std::atomic<int> g_cbCalls{0};
std::atomic<int> g_cbLen{0};
void PlayCb(void*, uint8_t* stream, int len) {
    for (int i = 0; i < len; ++i) stream[i] = (uint8_t)(i & 0x7f);
    g_cbLen = len;
    ++g_cbCalls;
}
std::atomic<int> g_capCalls{0};
void CapCb(void*, uint8_t*, int) { ++g_capCalls; }

bool WaitFor(const std::function<bool()>& f, int ms) {
    for (int t = 0; t < ms; t += 5) { if (f()) return true; SleepMs(5); }
    return f();
}

void TestTime(Backend& b) {
    const uint64_t t0 = b.TicksMs(), p0 = b.PerfCounter();
    b.DelayMs(60);
    const uint64_t t1 = b.TicksMs(), p1 = b.PerfCounter();
    CHECK(t1 >= t0 + 55);
    CHECK(t1 < t0 + 400);
    CHECK(b.PerfFrequency() > 0);
    const double sec = (double)(p1 - p0) / (double)b.PerfFrequency();
    CHECK(sec > 0.055 && sec < 0.4);
    uint64_t prev = b.TicksMs();
    for (int i = 0; i < 1000; ++i) { const uint64_t n = b.TicksMs(); CHECK(n >= prev); prev = n; }
}

void TestAudio(Backend& b) {
    if (!b.Init(InitFlags{false, true, false})) { std::cout << "  SKIP audio: Init failed: " << b.LastError() << "\n"; return; }
    AudioSpec want;
    want.freq = 22050; want.format = SampleFormat::S16; want.channels = 1; want.frames = 512;
    AudioSpec got;
    auto dev = b.OpenAudio(want, false, nullptr, &got);
    if (!dev) { std::cout << "  SKIP audio: no playback device here: " << b.LastError() << "\n"; return; }
    CHECK(got.channels >= 1);
    CHECK(dev->Spec().freq == got.freq);
    // Queue mode: a paused device keeps what is queued; Clear drops it.
    std::vector<int16_t> pcm(4410, 100);
    const uint32_t bytes = (uint32_t)(pcm.size() * sizeof(int16_t));
    if (got.channels != 1) { std::cout << "  SKIP queue checks: device changed channel count\n"; return; }
    CHECK(dev->QueuedBytes() == 0);
    CHECK(dev->Queue(pcm.data(), bytes));
    CHECK(dev->QueuedBytes() == bytes);
    dev->ClearQueued();
    CHECK(dev->QueuedBytes() == 0);
    // ... and an unpaused one plays it down to nothing, in about real time (0.2 s).
    CHECK(dev->Queue(pcm.data(), bytes));
    dev->Pause(false);
    CHECK(WaitFor([&] { return dev->QueuedBytes() == 0; }, 3000));
    dev->Pause(true);
    dev.reset();

    // Callback mode: the device calls us with a period-sized buffer.
    g_cbCalls = 0;
    AudioSpec cb = want; cb.callback = &PlayCb;
    auto dc = b.OpenAudio(cb, false, nullptr, &got);
    CHECK(dc != nullptr);
    if (dc) {
        dc->Pause(false);
        CHECK(WaitFor([] { return g_cbCalls >= 3; }, 3000));
        CHECK(g_cbLen > 0 && g_cbLen % 2 == 0);
        dc->Pause(true);
        const int calls = g_cbCalls;
        SleepMs(100);
        CHECK(g_cbCalls <= calls + 2);   // paused: it stops asking (a callback may be in flight)
    }
    dc.reset();

    // Capture (a microphone, or the "null" / dummy source): queue mode and callback mode.
    auto cap = b.OpenAudio(want, true, nullptr, &got);
    if (!cap) { std::cout << "  SKIP capture: no capture device here: " << b.LastError() << "\n"; return; }
    cap->Pause(false);
    uint8_t buf[2048];
    uint32_t total = 0;
    CHECK(WaitFor([&] { total += cap->Dequeue(buf, sizeof buf); return total > 0; }, 3000));
    cap.reset();
    g_capCalls = 0;
    AudioSpec cc = want; cc.callback = &CapCb;
    auto capCb = b.OpenAudio(cc, true, nullptr, &got);
    CHECK(capCb != nullptr);
    if (capCb) { capCb->Pause(false); CHECK(WaitFor([] { return g_capCalls >= 2; }, 3000)); }
    // The device list always offers something to choose (at least "default" on the own backend).
    CHECK(b.AudioDeviceCount(false) >= 0);
}

void TestGamepadsAbsent(Backend& b) {
    b.Init(InitFlags{false, false, true});
    const int n = b.GamepadDeviceCount();
    CHECK(n >= 0);
    for (int i = 0; i < n; ++i) { auto g = b.IsGamepad(i) ? b.OpenGamepad(i) : nullptr; if (g) { (void)g->Axis(GamepadAxis::LeftX); } }
    CHECK(b.OpenGamepad(n + 5) == nullptr);
    std::cout << "  gamepad devices visible: " << n << "\n";
}

#if defined(__linux__)
// Feed struct input_event records through a pipe, as the kernel would, and check the mapping.
void WriteEv(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event e;
    std::memset(&e, 0, sizeof(e));
    e.type = type; e.code = code; e.value = value;
    CHECK(write(fd, &e, sizeof(e)) == (ssize_t)sizeof(e));
}

void TestEvdevMapping() {
    using namespace okay::platform::own;
    int fds[2];
    CHECK(pipe(fds) == 0);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    EvdevCaps caps;
    for (int a : {ABS_X, ABS_Y, ABS_RX, ABS_RY}) { caps.abs[a].present = true; caps.abs[a].min = -32768; caps.abs[a].max = 32767; }
    for (int a : {ABS_Z, ABS_RZ}) { caps.abs[a].present = true; caps.abs[a].min = 0; caps.abs[a].max = 255; }
    caps.abs[ABS_HAT0X] = {-1, 1, true};
    caps.abs[ABS_HAT0Y] = {-1, 1, true};
    auto pad = OpenEvdevFromFd(fds[0], caps, "Test Pad");
    CHECK(std::string(pad->Name()) == "Test Pad");
    CHECK(pad->InstanceId() > 0);
    CHECK(pad->Axis(GamepadAxis::LeftX) == 0 || std::abs(pad->Axis(GamepadAxis::LeftX)) <= 1);   // centred at open
    CHECK(!pad->Button(GamepadButton::A));

    WriteEv(fds[1], EV_ABS, ABS_X, 32767);
    WriteEv(fds[1], EV_ABS, ABS_Y, -32768);
    WriteEv(fds[1], EV_ABS, ABS_RX, 0);
    WriteEv(fds[1], EV_ABS, ABS_Z, 255);
    WriteEv(fds[1], EV_ABS, ABS_RZ, 0);
    WriteEv(fds[1], EV_KEY, BTN_SOUTH, 1);
    WriteEv(fds[1], EV_KEY, BTN_NORTH, 1);
    WriteEv(fds[1], EV_KEY, BTN_START, 1);
    WriteEv(fds[1], EV_KEY, BTN_TL, 1);
    WriteEv(fds[1], EV_KEY, BTN_THUMBR, 1);
    WriteEv(fds[1], EV_ABS, ABS_HAT0X, -1);
    WriteEv(fds[1], EV_ABS, ABS_HAT0Y, 1);
    WriteEv(fds[1], EV_SYN, SYN_REPORT, 0);
    CHECK(pad->Axis(GamepadAxis::LeftX) == 32767);
    CHECK(pad->Axis(GamepadAxis::LeftY) == -32768);
    CHECK(std::abs(pad->Axis(GamepadAxis::RightX)) <= 1);
    CHECK(pad->Axis(GamepadAxis::TriggerLeft) == 32767);
    CHECK(pad->Axis(GamepadAxis::TriggerRight) == 0);
    CHECK(pad->Button(GamepadButton::A));
    CHECK(!pad->Button(GamepadButton::B));
    CHECK(pad->Button(GamepadButton::X));    // BTN_NORTH is the Xbox "X"
    CHECK(!pad->Button(GamepadButton::Y));
    CHECK(pad->Button(GamepadButton::Start));
    CHECK(pad->Button(GamepadButton::LeftShoulder));
    CHECK(!pad->Button(GamepadButton::RightShoulder));
    CHECK(pad->Button(GamepadButton::RightStick));
    CHECK(pad->Button(GamepadButton::DpadLeft));
    CHECK(pad->Button(GamepadButton::DpadDown));
    CHECK(!pad->Button(GamepadButton::DpadUp));
    CHECK(!pad->Rumble(1, 1, 10));           // a pipe has no force feedback
    // Release and half-way values.
    WriteEv(fds[1], EV_KEY, BTN_SOUTH, 0);
    WriteEv(fds[1], EV_ABS, ABS_RZ, 128);
    WriteEv(fds[1], EV_ABS, ABS_X, 0);
    CHECK(!pad->Button(GamepadButton::A));
    CHECK(std::abs(pad->Axis(GamepadAxis::TriggerRight) - 16448) <= 130);   // 128 of 255, scaled to 0..32767
    CHECK(std::abs(pad->Axis(GamepadAxis::LeftX)) <= 1);
    close(fds[1]);   // the "device" goes away: EOF
    std::vector<Event> ev;
    EvdevPollHotplug(ev);
    bool removed = false;
    for (const Event& e : ev) if (e.type == EventType::GamepadRemoved && e.deviceId == pad->InstanceId()) removed = true;
    CHECK(removed);
    ev.clear();
    EvdevPollHotplug(ev);   // reported once only
    for (const Event& e : ev) CHECK(!(e.type == EventType::GamepadRemoved && e.deviceId == pad->InstanceId()));
}
#endif

// The process-level helpers the apps used to take from SDL directly.
void TestHelpers(Backend& b) {
    // BasePath: the executable's directory with a trailing separator.
    const std::string base = b.BasePath();
    CHECK(base.size() > 1);
    CHECK(base.back() == '/' || base.back() == '\\');
#if defined(__linux__)
    {
        char buf[4096];
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        CHECK(n > 0);
        if (n > 0) {
            std::string exe(buf, (size_t)n);
            CHECK(exe.compare(0, base.size(), base) == 0);
            CHECK(exe.find('/', base.size()) == std::string::npos);   // the exe sits directly in it
        }
    }
#endif
    // PrefPath: created, writable, under XDG_DATA_HOME, trailing separator.
    char tmpl[] = "/tmp/okay_prefXXXXXX";
    if (mkdtemp(tmpl)) {
        setenv("XDG_DATA_HOME", tmpl, 1);
        const std::string pref = b.PrefPath("OkayOrg", "Okay App");
        std::cout << "  PrefPath -> " << pref << "\n";
        CHECK(!pref.empty() && pref.back() == '/');
        CHECK(pref.compare(0, std::strlen(tmpl), tmpl) == 0);
        CHECK(access(pref.c_str(), W_OK) == 0);
        if (b.Kind() == BackendKind::Own) CHECK(b.PrefPath("", "x").empty());
        unsetenv("XDG_DATA_HOME");
        std::string rm = std::string("rm -rf ") + tmpl;
        CHECK(std::system(rm.c_str()) == 0);
    }
    // PreferredLocales: well formed whichever backend answers; our own parsing is exact.
    for (const std::string& l : b.PreferredLocales()) CHECK(!l.empty() && l.find('_') == std::string::npos);
    if (b.Kind() == BackendKind::Own) {
        setenv("LANGUAGE", "de_DE.UTF-8:fr:C", 1);
        const std::vector<std::string> l = b.PreferredLocales();
        CHECK(l.size() == 2 && l[0] == "de-DE" && l[1] == "fr");
        unsetenv("LANGUAGE");
        setenv("LC_ALL", "pt_BR.UTF-8@x", 1);
        const std::vector<std::string> l2 = b.PreferredLocales();
        CHECK(l2.size() == 1 && l2[0] == "pt-BR");
        unsetenv("LC_ALL");
        // The base class (own backend) cannot show a message box: it says so instead of pretending.
        CHECK(!b.ShowErrorBox("t", "this error box is not available on the own Linux backend (expected)"));
        // Layout-aware key lookup needs a display; without one it is the US table.
        CHECK(b.KeyForChar('a') == Key::A && b.KeyForChar('z') == Key::Z && b.KeyForChar('5') == Key::N5);
        CHECK(b.KeyForChar('0') == Key::N0 && b.KeyForChar(' ') == Key::Space && b.KeyForChar('?') == Key::Unknown);
        CHECK(!b.SetPerMonitorDpiAware());
    } else {
        CHECK(b.SupportsRelativeMouseState());
    }
    b.Log("platform test log line (expected on stderr)");
    if (b.Kind() == BackendKind::Own) CHECK(b.IsCursorShown());   // no display: nothing hid it (sdl2 needs video to answer)
    CHECK(b.ModState() == 0);   // no keys held
}

void TestKeyNumbering() {
    // The Key enum is USB HID usage ids; spot-check the ones that matter.
    CHECK((int)Key::A == 4 && (int)Key::Z == 29 && (int)Key::N1 == 30 && (int)Key::N0 == 39);
    CHECK((int)Key::Return == 40 && (int)Key::Escape == 41 && (int)Key::Space == 44);
    CHECK((int)Key::F1 == 58 && (int)Key::F12 == 69 && (int)Key::Right == 79 && (int)Key::Up == 82);
    CHECK((int)Key::LCtrl == 224 && (int)Key::RGui == 231);
#ifdef OKAY_TEST_HAVE_SDL_HEADERS
    CHECK((int)Key::A == SDL_SCANCODE_A && (int)Key::Z == SDL_SCANCODE_Z);
    CHECK((int)Key::N0 == SDL_SCANCODE_0 && (int)Key::N1 == SDL_SCANCODE_1);
    CHECK((int)Key::Return == SDL_SCANCODE_RETURN && (int)Key::Escape == SDL_SCANCODE_ESCAPE);
    CHECK((int)Key::Backspace == SDL_SCANCODE_BACKSPACE && (int)Key::Tab == SDL_SCANCODE_TAB);
    CHECK((int)Key::F1 == SDL_SCANCODE_F1 && (int)Key::F12 == SDL_SCANCODE_F12);
    CHECK((int)Key::Left == SDL_SCANCODE_LEFT && (int)Key::Down == SDL_SCANCODE_DOWN);
    CHECK((int)Key::KpEnter == SDL_SCANCODE_KP_ENTER && (int)Key::Delete == SDL_SCANCODE_DELETE);
    CHECK((int)Key::LShift == SDL_SCANCODE_LSHIFT && (int)Key::RAlt == SDL_SCANCODE_RALT);
    CHECK((int)Key::PageUp == SDL_SCANCODE_PAGEUP && (int)Key::Home == SDL_SCANCODE_HOME);
#endif
}

} // namespace

int main(int argc, char** argv) {
    // `test_platform --print-default`: which backend Default() picks (the OKAY_PLATFORM_BACKEND
    // environment variable overrides the build's choice); the ctest entries below check it.
    if (argc > 1 && std::strcmp(argv[1], "--print-default") == 0) {
        std::cout << "default=" << BackendName(DefaultKind()) << "\n";
        return 0;
    }
    RUN_SUITE("platform");
    BackendKind k{};
    CHECK(ParseBackendName("sdl2", k) && k == BackendKind::Sdl2);
    CHECK(ParseBackendName("own", k) && k == BackendKind::Own);
    CHECK(!ParseBackendName("x11", k) && !ParseBackendName(nullptr, k));
    CHECK(std::strcmp(BackendName(BackendKind::Own), "own") == 0);
    TestKeyNumbering();

    // Free timers work whatever the default backend is.
    const uint64_t t0 = TicksMs();
    DelayMs(20);
    CHECK(TicksMs() >= t0 + 15);
    CHECK(TicksMs32() >= (uint32_t)t0);
    CHECK(PerfFrequency() > 0);

    // Headless: SDL's dummy audio driver, ALSA's null device.
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("OKAY_ALSA_DEVICE", "null", 1);

    int ran = 0;
    for (BackendKind kind : {BackendKind::Sdl2, BackendKind::Own}) {
        std::cout << "[backend] " << BackendName(kind) << (IsCompiledIn(kind) ? "" : " (not compiled in)") << "\n";
        Backend* b = Get(kind);
        CHECK((b != nullptr) == IsCompiledIn(kind));
        if (!b) continue;
        ++ran;
        CHECK(b->Kind() == kind);
        TestTime(*b);
        TestAudio(*b);
        TestGamepadsAbsent(*b);
        TestHelpers(*b);
        b->Shutdown();
    }
    CHECK(ran >= 1);
    CHECK(Default() != nullptr);
    if (Default()) std::cout << "[default backend] " << Default()->Name() << " (OKAY_PLATFORM_BACKEND)\n";
    CHECK(Default() == Get(DefaultKind()));
#if defined(__linux__)
    if (IsCompiledIn(BackendKind::Own)) TestEvdevMapping();
#endif

    // A window size from a config file: zero, negative and absurd values are not sizes.
    CHECK(ValidWindowExtent(960, 1) == 960);
    CHECK(ValidWindowExtent(1, 960) == 1);
    CHECK(ValidWindowExtent(kMaxWindowExtent, 960) == kMaxWindowExtent);
    CHECK(ValidWindowExtent(0, 960) == 960);
    CHECK(ValidWindowExtent(-5, 600) == 600);
    CHECK(ValidWindowExtent(kMaxWindowExtent + 1, 600) == 600);
    CHECK(ValidWindowExtent(2147483647, 600) == 600);
    TEST_MAIN_RESULT();
}
