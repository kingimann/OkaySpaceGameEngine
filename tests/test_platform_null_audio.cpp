// The null audio device (engine/src/Platform/NullAudio.cpp): OKAY_AUDIO_DEVICE=null makes the default backend open a
// device that needs no sound card, on every platform (CI runners have none). Checked through the public okay::platform
// API only, so the same program runs on Linux and, cross-compiled, on Windows (the Windows test job): the list shows one
// device, "null"; queue mode drains at the real rate and keeps its bytes while paused; capture produces silence at the
// real rate; callback mode is called one period at a time at the spec's rate and stops when paused; closing a running
// device returns at once. Timing limits are wide (CI machines are busy); each one still fails an instant drain or a
// device that never runs.
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"

#include <atomic>
#include <cstdlib>
#include <vector>

using namespace okay::platform;

namespace {

void SetEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}

std::atomic<int> g_calls{0};
std::atomic<int> g_len{0};
std::atomic<int> g_nonZero{0};
void PlayCb(void*, uint8_t* stream, int len) {
    for (int i = 0; i < len; ++i) if (stream[i]) ++g_nonZero;   // handed over silent
    for (int i = 0; i < len; ++i) stream[i] = 0x55;
    g_len = len;
    ++g_calls;
}
void CapCb(void*, uint8_t* stream, int len) {
    for (int i = 0; i < len; ++i) if (stream[i]) ++g_nonZero;   // the null source records silence
    g_len = len;
    ++g_calls;
}

template <class F> bool WaitMs(int ms, F done) {
    const uint64_t until = TicksMs() + (uint64_t)ms;
    while (TicksMs() < until) { if (done()) return true; DelayMs(2); }
    return done();
}

} // namespace

int main() {
    RUN_SUITE("platform_null_audio");
    SetEnv("OKAY_AUDIO_DEVICE", "null");
    Backend* b = Default();
    CHECK(b != nullptr);
    if (!b) TEST_MAIN_RESULT();
    b->Init(InitFlags{false, true, false});

    // ---- the list: one device, "null", for playback and capture ----
    CHECK(b->AudioDeviceCount(false) == 1 && b->AudioDeviceName(0, false) == "null");
    CHECK(b->AudioDeviceCount(true) == 1 && b->AudioDeviceName(0, true) == "null");

    AudioSpec want;
    want.freq = 8000; want.format = SampleFormat::S16; want.channels = 1; want.frames = 400;   // 50 ms periods
    const uint32_t bytesPerSec = 8000u * 2u;

    // ---- queue mode playback ----
    {
        AudioSpec got;
        auto dev = b->OpenAudio(want, false, nullptr, &got);
        CHECK(dev != nullptr);
        if (!dev) std::cout << "  open failed: " << b->LastError() << "\n";
        if (dev) {
            CHECK(got.freq == 8000 && got.channels == 1 && got.format == SampleFormat::S16 && got.frames == 400);
            CHECK(dev->Spec().freq == 8000);
            std::vector<int16_t> pcm(3200, 100);   // 0.4 s
            const uint32_t bytes = (uint32_t)(pcm.size() * sizeof(int16_t));
            CHECK(dev->Queue(pcm.data(), bytes));
            DelayMs(150);
            CHECK(dev->QueuedBytes() == bytes);    // devices start paused: nothing plays
            dev->ClearQueued();
            CHECK(dev->QueuedBytes() == 0);
            CHECK(dev->Queue(pcm.data(), bytes));
            const uint64_t t0 = TicksMs();
            dev->Pause(false);
            DelayMs(100);
            const uint32_t left = dev->QueuedBytes();
            CHECK(left > 0 && left < bytes);       // draining, but not all at once
            CHECK(WaitMs(3000, [&] { return dev->QueuedBytes() == 0; }));
            const uint64_t took = TicksMs() - t0;
            std::cout << "  0.4 s of sound drained in " << took << " ms\n";
            CHECK(took >= 300 && took < 2500);
            // Paused again: what is queued stays.
            dev->Pause(true);
            DelayMs(60);                           // a period in flight finishes
            CHECK(dev->Queue(pcm.data(), bytes / 4));
            DelayMs(150);
            CHECK(dev->QueuedBytes() == bytes / 4);
            CHECK(dev->Dequeue(pcm.data(), 16) == 0);   // playback has nothing to dequeue
        }
    }

    // ---- callback mode playback: one period per call, at the spec's rate ----
    {
        g_calls = 0; g_len = 0; g_nonZero = 0;
        AudioSpec cb = want; cb.callback = &PlayCb;
        auto dev = b->OpenAudio(cb, false, nullptr, nullptr);
        CHECK(dev != nullptr);
        if (dev) {
            DelayMs(120);
            CHECK(g_calls == 0);                   // paused: not called
            dev->Pause(false);
            const uint64_t t0 = TicksMs();
            CHECK(WaitMs(3000, [] { return g_calls >= 10; }));
            const uint64_t took = TicksMs() - t0;
            std::cout << "  10 callbacks of 50 ms took " << took << " ms\n";
            CHECK(took >= 400 && took < 2500);     // 10 x 50 ms, not as fast as the thread can go
            CHECK(g_len == 800);                   // 400 frames of mono S16
            CHECK(g_nonZero == 0);                 // each buffer starts silent
            dev->Pause(true);
            const int calls = g_calls;
            DelayMs(200);
            CHECK(g_calls <= calls + 1);           // paused: it stops asking (one call may be in flight)
            dev->Pause(false);
            CHECK(WaitMs(3000, [&] { return g_calls >= calls + 2; }));   // and starts again
            const uint64_t c0 = TicksMs();
            dev.reset();                           // closing a running device returns at once and calls no more
            CHECK(TicksMs() - c0 < 1000);
            const int after = g_calls;
            DelayMs(120);
            CHECK(g_calls == after);
        }
    }

    // ---- capture, queue mode: silence at the real rate ----
    {
        auto cap = b->OpenAudio(want, true, nullptr, nullptr);
        CHECK(cap != nullptr);
        if (cap) {
            DelayMs(100);
            CHECK(cap->QueuedBytes() == 0);        // paused: records nothing
            CHECK(!cap->Queue("x", 1));            // capture takes no playback
            cap->Pause(false);
            const uint64_t t0 = TicksMs();
            std::vector<uint8_t> got;
            uint8_t buf[512];
            CHECK(WaitMs(3000, [&] {
                const uint32_t n = cap->Dequeue(buf, sizeof buf);
                got.insert(got.end(), buf, buf + n);
                return got.size() >= bytesPerSec / 4;   // 0.25 s
            }));
            const uint64_t took = TicksMs() - t0;
            std::cout << "  0.25 s of capture arrived in " << took << " ms\n";
            CHECK(took >= 200 && took < 2500);
            bool silent = true;
            for (uint8_t v : got) silent = silent && v == 0;
            CHECK(silent);
            cap->Pause(true);
        }
    }

    // ---- capture, callback mode ----
    {
        g_calls = 0; g_len = 0; g_nonZero = 0;
        AudioSpec cc = want; cc.callback = &CapCb; cc.channels = 2; cc.format = SampleFormat::F32;
        AudioSpec got;
        auto cap = b->OpenAudio(cc, true, nullptr, &got);
        CHECK(cap != nullptr);
        if (cap) {
            CHECK(got.channels == 2 && got.format == SampleFormat::F32);
            cap->Pause(false);
            CHECK(WaitMs(3000, [] { return g_calls >= 3; }));
            CHECK(g_len == 400 * 2 * 4);
            CHECK(g_nonZero == 0);
        }
    }

    // ---- formats no device takes are refused; without the variable the real backend is used again ----
    {
        AudioSpec bad = want; bad.channels = 0;
        CHECK(b->OpenAudio(bad, false, nullptr, nullptr) == nullptr);
        CHECK(!b->LastError().empty());
        SetEnv("OKAY_AUDIO_DEVICE", "nul");        // only the exact word
        CHECK(b->AudioDeviceCount(false) != 1 || b->AudioDeviceName(0, false) != "null");
        SetEnv("OKAY_AUDIO_DEVICE", nullptr);
        CHECK(b->AudioDeviceCount(false) != 1 || b->AudioDeviceName(0, false) != "null");
    }
    b->Shutdown();
    TEST_MAIN_RESULT();
}
