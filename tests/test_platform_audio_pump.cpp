// Pure tests for the parts of the own Windows backend that need no Windows: the audio helpers
// (engine/src/Platform/AudioPump.hpp) and the scan code table (Win32Keys.hpp). Expected values are
// worked out by hand from the Microsoft / USB HID documentation, not by calling the code under test.
#include "AudioPump.hpp"
#include "Win32Keys.hpp"
#include <cstdio>
#include <cstdlib>

using namespace okay::platform;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static int g_cbCalls = 0, g_cbLen = 0;
static void FillCb(void*, uint8_t* s, int len) { ++g_cbCalls; g_cbLen = len; for (int i = 0; i < len; ++i) s[i] = (uint8_t)(0x40 + i); }
static void SinkCb(void* u, uint8_t* s, int len) { ++g_cbCalls; g_cbLen = len; *static_cast<int*>(u) += s[0]; }

static void Timing() {
    CHECK(pump::HnsFromFrames(48000, 48000) == 10000000);          // one second
    CHECK(pump::HnsFromFrames(480, 48000) == 100000);              // 10 ms
    CHECK(pump::HnsFromFrames(1024, 48000) == 213333);             // 213333.33 rounds down
    CHECK(pump::HnsFromFrames(1, 44100) == 227);                   // 226.757 rounds up
    CHECK(pump::HnsFromFrames(5, 0) == 0 && pump::HnsFromFrames(-1, 48000) == 0);
    CHECK(pump::FramesFromHns(100000, 48000) == 480);
    CHECK(pump::FramesFromHns(10000000, 44100) == 44100);
    CHECK(pump::FramesFromHns(227, 44100) == 1);
    CHECK(pump::FramesFromHns(1, 0) == 0);
    // Round trip for every period a game would ask for.
    for (int f = 64; f <= 16384; f += 64) CHECK(std::llabs(pump::FramesFromHns(pump::HnsFromFrames(f, 48000), 48000) - f) <= 1);
    // A huge frame count must not overflow (1e12 frames * 1e7 = 1e19 would): documented limit is 9.2e11 frames.
    CHECK(pump::HnsFromFrames(900000000000ll, 48000) == 187500000000000ll);
    CHECK(pump::ClampPeriodFrames(0) == 64 && pump::ClampPeriodFrames(1024) == 1024 && pump::ClampPeriodFrames(1 << 20) == 16384);
    CHECK(pump::BufferDurationHns(1024, 48000) == 640000);         // 3 * 213333.33 = 640000
    CHECK(pump::BufferDurationHns(64, 48000) == 200000);           // 3 * 64 frames = 4 ms, raised to 20 ms
    CHECK(pump::WaitMs(480, 48000) == 20);                         // two 10 ms periods
    CHECK(pump::WaitMs(64, 48000) == 5);                           // 2.7 ms, raised to 5
    CHECK(pump::WaitMs(16384, 8000) == 200);                       // 4 s, capped
    CHECK(pump::FreeFrames(960, 0) == 960 && pump::FreeFrames(960, 300) == 660 && pump::FreeFrames(960, 960) == 0 &&
          pump::FreeFrames(960, 5000) == 0);
}

static void Formats() {
    AudioSpec s;
    pump::WaveFormat f;
    s.freq = 48000; s.channels = 2; s.format = SampleFormat::S16;
    CHECK(pump::MakeWaveFormat(s, &f));
    CHECK(f.tag == 1 && f.channels == 2 && f.rate == 48000 && f.bits == 16 && f.blockAlign == 4 && f.avgBytesPerSec == 192000 && !f.IsFloat());
    s.format = SampleFormat::F32; s.channels = 1; s.freq = 44100;
    CHECK(pump::MakeWaveFormat(s, &f));
    CHECK(f.tag == 3 && f.bits == 32 && f.blockAlign == 4 && f.avgBytesPerSec == 176400 && f.IsFloat());
    s.channels = 6; s.freq = 48000;
    CHECK(pump::MakeWaveFormat(s, &f));
    CHECK(f.tag == 0xFFFE && f.subFloat && f.IsFloat() && f.blockAlign == 24 && f.avgBytesPerSec == 1152000 && f.channelMask == 0x3F);
    s.format = SampleFormat::S16; s.channels = 8;
    CHECK(pump::MakeWaveFormat(s, &f) && f.tag == 0xFFFE && !f.subFloat && f.blockAlign == 16 && f.channelMask == 0x63F);
    s.channels = 0; CHECK(!pump::MakeWaveFormat(s, &f));
    s.channels = 9; CHECK(!pump::MakeWaveFormat(s, &f));
    s.channels = 2; s.freq = 100; CHECK(!pump::MakeWaveFormat(s, &f));
    s.freq = 1000000; CHECK(!pump::MakeWaveFormat(s, &f));
    // The standard layouts, as numbers from the speaker position table (FL 1, FR 2, FC 4, LFE 8, BL 0x10, BR 0x20, BC 0x100, SL 0x200, SR 0x400).
    CHECK(pump::DefaultChannelMask(1) == 0x4 && pump::DefaultChannelMask(2) == 0x3 && pump::DefaultChannelMask(3) == 0x7);
    CHECK(pump::DefaultChannelMask(4) == 0x33 && pump::DefaultChannelMask(5) == 0x37 && pump::DefaultChannelMask(6) == 0x3F);
    CHECK(pump::DefaultChannelMask(7) == 0x70F && pump::DefaultChannelMask(8) == 0x63F);
    CHECK(pump::DefaultChannelMask(0) == 0 && pump::DefaultChannelMask(9) == 0);
    // The mask has exactly one bit per channel.
    for (int c = 1; c <= 8; ++c) CHECK(__builtin_popcount(pump::DefaultChannelMask(c)) == c);

    // Mix-format fallback.
    AudioSpec want; want.freq = 22050; want.channels = 1; want.format = SampleFormat::S16; want.frames = 512; want.allowChange = true;
    AudioSpec got;
    pump::WaveFormat mix; mix.tag = 0xFFFE; mix.subFloat = true; mix.channels = 2; mix.rate = 48000; mix.bits = 32;
    CHECK(pump::SpecFromMixFormat(mix, want, &got) && got.freq == 48000 && got.channels == 2 && got.format == SampleFormat::F32 && got.frames == 512 && got.allowChange);
    mix.tag = 1; mix.subFloat = false; mix.bits = 16;
    CHECK(pump::SpecFromMixFormat(mix, want, &got) && got.format == SampleFormat::S16);
    mix.bits = 24; CHECK(!pump::SpecFromMixFormat(mix, want, &got));          // 24-bit PCM: not offered
    mix.bits = 32; CHECK(!pump::SpecFromMixFormat(mix, want, &got));          // 32-bit integer PCM: not offered
    mix.bits = 16; mix.channels = 0; CHECK(!pump::SpecFromMixFormat(mix, want, &got));
    mix.channels = 2; mix.rate = 1; CHECK(!pump::SpecFromMixFormat(mix, want, &got));
}

static void Ring() {
    pump::ByteRing r;
    uint8_t in[10000], out[10000];
    for (int i = 0; i < 10000; ++i) in[i] = (uint8_t)(i * 7 + 3);
    CHECK(r.Size() == 0 && r.Pop(out, 10) == 0);
    r.Push(in, 5);
    CHECK(r.Size() == 5);
    CHECK(r.Pop(out, 3) == 3 && std::memcmp(out, in, 3) == 0 && r.Size() == 2);
    CHECK(r.Pop(out, 99) == 2 && std::memcmp(out, in + 3, 2) == 0 && r.Size() == 0);
    r.Push(in, 0);
    CHECK(r.Size() == 0);
    // Growth past the first 4096 bytes keeps the order, including after the head has moved (wrap-around).
    r.Push(in, 3000);
    CHECK(r.Pop(out, 2500) == 2500);
    r.Push(in + 3000, 6000);                       // wraps and grows
    CHECK(r.Size() == 6500);
    CHECK(r.Pop(out, 500) == 500 && std::memcmp(out, in + 2500, 500) == 0);
    CHECK(r.Pop(out, 6000) == 6000 && std::memcmp(out, in + 3000, 6000) == 0 && r.Size() == 0);
    // Many small pushes and odd pops compared with a plain model.
    std::vector<uint8_t> model;
    unsigned rng = 12345;
    bool same = true;
    for (int step = 0; step < 4000; ++step) {
        rng = rng * 1103515245u + 12345u;
        const uint32_t n = (rng >> 16) % 700;
        if (step % 3 != 2) {
            r.Push(in, n);
            model.insert(model.end(), in, in + n);
        } else {
            const uint32_t got = r.Pop(out, n);
            const uint32_t want = (uint32_t)std::min<size_t>(n, model.size());
            if (got != want || std::memcmp(out, model.data(), got) != 0) same = false;
            model.erase(model.begin(), model.begin() + got);
        }
        if (r.Size() != model.size()) same = false;
    }
    CHECK(same);
    r.Clear();
    CHECK(r.Size() == 0 && r.Pop(out, 10) == 0);
    // Cap: the oldest bytes go.
    pump::ByteRing c;
    c.Push(in, 100, 150);
    c.Push(in + 100, 100, 150);                    // total 200 > 150: drop 50 oldest
    CHECK(c.Size() == 150 && c.Pop(out, 150) == 150 && std::memcmp(out, in + 50, 150) == 0);
    c.Push(in, 400, 150);                          // bigger than the cap by itself: keep the newest 150
    CHECK(c.Size() == 150 && c.Pop(out, 150) == 150 && std::memcmp(out, in + 250, 150) == 0);
}

static void Fill() {
    AudioSpec s; s.channels = 2; s.format = SampleFormat::S16;
    pump::ByteRing q;
    uint8_t buf[16];
    std::memset(buf, 0xEE, sizeof buf);
    const uint8_t src[6] = {1, 2, 3, 4, 5, 6};
    q.Push(src, 6);
    CHECK(pump::FillPlayback(s, q, buf, 16) == 6);                      // underrun: 6 data bytes, then silence
    CHECK(std::memcmp(buf, src, 6) == 0);
    bool zeros = true;
    for (int i = 6; i < 16; ++i) zeros = zeros && buf[i] == 0;
    CHECK(zeros && q.Size() == 0);
    // Callback device: the callback gets a silent buffer to start from and the full length; the queue is untouched.
    s.callback = FillCb;
    q.Push(src, 6);
    g_cbCalls = 0;
    CHECK(pump::FillPlayback(s, q, buf, 16) == 0 && g_cbCalls == 1 && g_cbLen == 16 && buf[0] == 0x40 && buf[15] == 0x4F && q.Size() == 6);

    // Capture.
    AudioSpec c; c.channels = 1; c.format = SampleFormat::S16;
    pump::ByteRing cq;
    std::vector<uint8_t> scratch;
    pump::DeliverCapture(c, cq, src, 6, false, scratch);
    CHECK(cq.Size() == 6);
    pump::DeliverCapture(c, cq, src, 6, true, scratch);                 // silent packet: zeros of the same length
    uint8_t out[12];
    CHECK(cq.Pop(out, 12) == 12 && std::memcmp(out, src, 6) == 0 && out[6] == 0 && out[11] == 0);
    pump::DeliverCapture(c, cq, nullptr, 4, false, scratch);            // no pointer: treated as silence, never read
    CHECK(cq.Size() == 4);
    pump::DeliverCapture(c, cq, src, 6, false, scratch, 8);             // capped queue keeps the newest 8
    CHECK(cq.Size() == 8);
    int sum = 0;
    c.callback = SinkCb; c.user = &sum; g_cbCalls = 0;
    pump::ByteRing none;
    pump::DeliverCapture(c, none, src, 6, false, scratch);
    CHECK(g_cbCalls == 1 && g_cbLen == 6 && sum == 1 && none.Size() == 0);
}

static void Keys() {
    using win32keys::KeyFromScan;
    // Values from the scan code table (set 1) and the HID keyboard page.
    CHECK(KeyFromScan(0x01, false) == Key::Escape && KeyFromScan(0x02, false) == Key::N1 && KeyFromScan(0x0B, false) == Key::N0);
    CHECK(KeyFromScan(0x0E, false) == Key::Backspace && KeyFromScan(0x0F, false) == Key::Tab && KeyFromScan(0x1C, false) == Key::Return);
    CHECK(KeyFromScan(0x10, false) == Key::Q && KeyFromScan(0x1E, false) == Key::A && KeyFromScan(0x2C, false) == Key::Z);
    CHECK(KeyFromScan(0x11, false) == Key::W && KeyFromScan(0x1F, false) == Key::S && KeyFromScan(0x20, false) == Key::D);
    CHECK(KeyFromScan(0x1D, false) == Key::LCtrl && KeyFromScan(0x2A, false) == Key::LShift && KeyFromScan(0x36, false) == Key::RShift);
    CHECK(KeyFromScan(0x38, false) == Key::LAlt && KeyFromScan(0x39, false) == Key::Space && KeyFromScan(0x3A, false) == Key::CapsLock);
    CHECK(KeyFromScan(0x29, false) == Key::Grave && KeyFromScan(0x2B, false) == Key::Backslash && KeyFromScan(0x33, false) == Key::Comma);
    CHECK(KeyFromScan(0x3B, false) == Key::F1 && KeyFromScan(0x44, false) == Key::F10 && KeyFromScan(0x57, false) == Key::F11 && KeyFromScan(0x58, false) == Key::F12);
    CHECK(KeyFromScan(0x46, false) == Key::ScrollLock && KeyFromScan(0x45, false) == Key::Pause);
    CHECK(KeyFromScan(0x47, false) == Key::Kp7 && KeyFromScan(0x4F, false) == Key::Kp1 && KeyFromScan(0x52, false) == Key::Kp0 &&
          KeyFromScan(0x53, false) == Key::KpPeriod && KeyFromScan(0x37, false) == Key::KpMultiply && KeyFromScan(0x4A, false) == Key::KpMinus &&
          KeyFromScan(0x4E, false) == Key::KpPlus);
    CHECK(KeyFromScan(0x56, false) == Key::NonUsBackslash);
    // Extended keys: the same scan code means a different key.
    CHECK(KeyFromScan(0x47, true) == Key::Home && KeyFromScan(0x48, true) == Key::Up && KeyFromScan(0x4B, true) == Key::Left &&
          KeyFromScan(0x4D, true) == Key::Right && KeyFromScan(0x50, true) == Key::Down && KeyFromScan(0x4F, true) == Key::End);
    CHECK(KeyFromScan(0x49, true) == Key::PageUp && KeyFromScan(0x51, true) == Key::PageDown && KeyFromScan(0x52, true) == Key::Insert && KeyFromScan(0x53, true) == Key::Delete);
    CHECK(KeyFromScan(0x1D, true) == Key::RCtrl && KeyFromScan(0x38, true) == Key::RAlt && KeyFromScan(0x1C, true) == Key::KpEnter && KeyFromScan(0x35, true) == Key::KpDivide);
    CHECK(KeyFromScan(0x5B, true) == Key::LGui && KeyFromScan(0x5C, true) == Key::RGui && KeyFromScan(0x5D, true) == Key::Application);
    CHECK(KeyFromScan(0x45, true) == Key::NumLock && KeyFromScan(0x37, true) == Key::PrintScreen);
    CHECK(KeyFromScan(0x7F, true) == Key::Unknown && KeyFromScan(0x7F, false) == Key::Unknown && KeyFromScan(-1, false) == Key::Unknown && KeyFromScan(0, false) == Key::Unknown);
    // The numbers are the HID usage ids.
    CHECK((int)Key::A == 4 && (int)Key::Escape == 41 && (int)Key::NonUsBackslash == 100 && (int)Key::Application == 101);
    // No two non-extended scan codes map to the same key, except the deliberate unknowns.
    int seen[512] = {0};
    bool unique = true;
    for (int sc = 1; sc < 0x59; ++sc) { const Key k = KeyFromScan(sc, false); if (k != Key::Unknown && ++seen[(int)k] > 1) unique = false; }
    CHECK(unique);
}

int main() {
    Timing(); Formats(); Ring(); Fill(); Keys();
    std::printf("platform_audio_pump: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
