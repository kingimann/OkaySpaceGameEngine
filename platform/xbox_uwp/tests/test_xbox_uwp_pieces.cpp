// Linux test for the pure Xbox UWP pieces added in round 3: XboxPendingSave.hpp, XboxSplash.hpp, XboxKeyLayout.hpp, XboxClipboard.hpp.
// Fake clock, fake storage, fake clipboard host. Expected values are worked out by hand. UNVERIFIED on Windows and on a console.
#include "XboxPendingSave.hpp"
#include "XboxSplash.hpp"
#include "XboxKeyLayout.hpp"
#include "XboxClipboard.hpp"
#include "XboxCapture.hpp"
#include "tests/FakeCaptureHost.hpp"
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
using namespace okay::xbox;

static void PendingTests() {
    std::uint64_t t = 0;
    auto clock = [&] { return t; };
    { // nothing pending
        PendingSaves p; PendingReport r = p.FlushForSuspend(clock, 4000);
        CHECK(r.results.empty() && r.AllSaved() && r.Summary() == "nothing pending" && !p.HasPending());
    }
    { // everything pending is written in order, once; asking twice for one name writes the newest data once
        PendingSaves p; std::vector<std::string> disk;
        p.Request("settings", [&](std::string&) { t += 3; disk.push_back("settings"); return true; });
        p.Request("slot1", [&](std::string&) { t += 40; disk.push_back("slot1 old"); return true; });
        p.Request("slot1", [&](std::string&) { t += 40; disk.push_back("slot1 new"); return true; });
        CHECK(p.PendingCount() == 2);
        t = 0; PendingReport r = p.FlushForSuspend(clock, 4000);
        CHECK(r.AllSaved() && r.results.size() == 2 && disk.size() == 2 && disk[0] == "settings" && disk[1] == "slot1 new");
        CHECK(r.results[0].ms == 3 && r.results[1].ms == 40 && !p.HasPending() && p.LastWriteMs("slot1") == 40);
        CHECK(r.Summary() == "settings saved (3 ms), slot1 saved (40 ms)");
    }
    { // a failed write stays pending and is reported; the next one still runs; a throwing writer is caught
        PendingSaves p;
        p.Request("a", [&](std::string& e) { e = "disk full"; return false; });
        p.Request("b", [&](std::string&) { throw 1; return true; });
        p.Request("c", [&](std::string&) { return true; });
        PendingReport r = p.FlushForSuspend(clock, 4000);
        CHECK(r.Count(PendingOutcome::Failed) == 2 && r.Count(PendingOutcome::Saved) == 1 && !r.AllSaved());
        CHECK(r.results[0].error == "disk full" && r.results[1].error == "threw an unknown exception");
        CHECK(p.PendingCount() == 2 && p.IsPending("a") && p.IsPending("b") && !p.IsPending("c"));
        CHECK(r.Summary().find("a FAILED: disk full") == 0);
    }
    { // deadline: a write whose last time is longer than the time left is not started, the file is not half written
        PendingSaves p; int writes = 0;
        // first round teaches the model that "big" takes 900 ms
        p.Request("big", [&](std::string&) { t += 900; ++writes; return true; });
        t = 0; p.RunFrame(clock);
        CHECK(writes == 1 && p.LastWriteMs("big") == 900);
        p.Request("small", [&](std::string&) { t += 10; return true; });
        p.Request("big", [&](std::string&) { t += 900; ++writes; return true; });
        t = 0; PendingReport r = p.FlushForSuspend(clock, 500);
        CHECK(r.results.size() == 2 && r.results[0].name == "small" && r.results[0].outcome == PendingOutcome::Saved);
        CHECK(r.results[1].outcome == PendingOutcome::WouldMissDeadline && writes == 1 && p.IsPending("big"));
        CHECK(r.results[1].error == "last write 900 ms, 490 left");
        CHECK(r.Summary() == "small saved (10 ms), big would miss the deadline (last write 900 ms, 490 left)");
    }
    { // out of time: once the budget is used up the rest are skipped and stay pending
        PendingSaves p;
        p.Request("one", [&](std::string&) { t += 600; return true; });
        p.Request("two", [&](std::string&) { return true; });
        t = 0; PendingReport r = p.FlushForSuspend(clock, 500);
        CHECK(r.results[0].outcome == PendingOutcome::Saved && r.results[1].outcome == PendingOutcome::OutOfTime && p.IsPending("two") && !p.IsPending("one"));
    }
    { // normal frame: failed write retried next frame; a save requested while writing is kept for the next frame
        PendingSaves p; int tries = 0;
        p.Request("x", [&](std::string&) { return ++tries >= 2; });
        CHECK(p.RunFrame().Count(PendingOutcome::Failed) == 1 && p.IsPending("x"));
        CHECK(p.RunFrame().AllSaved() && !p.HasPending());
        p.Request("first", [&](std::string&) { p.Request("again", [](std::string&) { return true; }); return true; });
        p.RunFrame(); CHECK(p.IsPending("again") && !p.IsPending("first"));
    }
}

static void SplashTests() {
    { // stays until a frame is presented AND the minimum time passed, then fades over 300 ms
        SplashConfig c; c.minShowMs = 1000; c.fadeMs = 400; SplashModel s(c);
        CHECK(s.State() == SplashState::NotStarted && !s.Visible() && s.Alpha() == 0.f);
        s.Start(100); s.Tick(100); CHECK(s.State() == SplashState::Showing && s.Alpha() == 1.f);
        s.Tick(5000); CHECK(s.State() == SplashState::Showing);            // time passed, no frame yet
        s.OnFramePresented(); s.Tick(5001); CHECK(s.State() == SplashState::Fading && s.Alpha() == 1.f);
        s.Tick(5201); CHECK(s.Alpha() == 0.5f && s.Visible());
        s.Tick(5401); CHECK(s.Done() && !s.Visible() && s.Alpha() == 0.f);
    }
    { // frame first, then the minimum time decides
        SplashConfig c; c.minShowMs = 1000; SplashModel s(c); s.Start(0); s.OnFramePresented();
        s.Tick(999); CHECK(s.State() == SplashState::Showing); s.Tick(1000); CHECK(s.State() == SplashState::Fading);
    }
    { // stall report, backwards clock, zero fade, Start twice
        SplashConfig c; c.stallMs = 2000; c.fadeMs = 0; c.minShowMs = 0; SplashModel s(c); s.Start(1000); s.Start(9999);
        s.Tick(2999); CHECK(!s.Stalled()); s.Tick(3000); CHECK(s.Stalled());
        s.Tick(10); CHECK(s.Stalled() && s.State() == SplashState::Showing);   // backwards: no change
        s.OnFramePresented(); s.Tick(3001); CHECK(!s.Stalled() && s.Done() && s.Alpha() == 0.f);   // zero fade: straight to Done
    }
}

static void LayoutTests() {
    const std::string kEmpty;
    // US
    CHECK(DecodeKey(KeyLayout::US, 4, false) == 'a' && DecodeKey(KeyLayout::US, 4, true) == 'A' && DecodeKey(KeyLayout::US, 30, true) == '!');
    CHECK(DecodeKey(KeyLayout::US, 39, true) == ')' && DecodeKey(KeyLayout::US, 44, false) == ' ' && DecodeKey(KeyLayout::US, 200, false) == 0);
    // French AZERTY: A and Q swap, Z and W swap, M sits right of L, digits need Shift
    CHECK(DecodeKey(KeyLayout::French, 4, false) == 'q' && DecodeKey(KeyLayout::French, 4, true) == 'Q' && DecodeKey(KeyLayout::French, 20, false) == 'a');
    CHECK(DecodeKey(KeyLayout::French, 26, false) == 'z' && DecodeKey(KeyLayout::French, 29, false) == 'w');
    CHECK(DecodeKey(KeyLayout::French, 16, false) == ',' && DecodeKey(KeyLayout::French, 51, false) == 'm' && DecodeKey(KeyLayout::French, 51, true) == 'M');
    CHECK(DecodeKey(KeyLayout::French, 30, false) == '&' && DecodeKey(KeyLayout::French, 30, true) == '1' && DecodeKey(KeyLayout::French, 31, false) == 0xE9);
    CHECK(DecodeKey(KeyLayout::French, 31, false, true) == '~' && DecodeKey(KeyLayout::French, 39, false, true) == '@');
    CHECK(DecodeKey(KeyLayout::French, 4, false, false, true) == 'Q' && DecodeKey(KeyLayout::French, 4, true, false, true) == 'q');
    // German QWERTZ
    CHECK(DecodeKey(KeyLayout::German, 28, false) == 'z' && DecodeKey(KeyLayout::German, 29, false) == 'y');
    CHECK(DecodeKey(KeyLayout::German, 47, false) == 0xFC && DecodeKey(KeyLayout::German, 47, true) == 0xDC && DecodeKey(KeyLayout::German, 52, true) == 0xC4);
    CHECK(DecodeKey(KeyLayout::German, 45, false) == 0xDF && DecodeKey(KeyLayout::German, 45, true) == '?' && DecodeKey(KeyLayout::German, 45, false, true) == '\\');
    CHECK(DecodeKey(KeyLayout::German, 36, true) == '/' && DecodeKey(KeyLayout::German, 31, true) == '"' && DecodeKey(KeyLayout::German, 36, false, true) == '{');
    // Nordic (Swedish / Finnish)
    CHECK(DecodeKey(KeyLayout::Nordic, 47, false) == 0xE5 && DecodeKey(KeyLayout::Nordic, 47, true) == 0xC5);
    CHECK(DecodeKey(KeyLayout::Nordic, 51, false) == 0xF6 && DecodeKey(KeyLayout::Nordic, 52, false) == 0xE4 && DecodeKey(KeyLayout::Nordic, 33, true) == 0xA4);
    CHECK(DecodeKey(KeyLayout::Nordic, 31, false, true) == '@' && DecodeKey(KeyLayout::Nordic, 4, false) == 'a');
    // Russian
    CHECK(DecodeKey(KeyLayout::Russian, 20, false) == 0x439 && DecodeKey(KeyLayout::Russian, 20, true) == 0x419);
    CHECK(DecodeKey(KeyLayout::Russian, 4, false) == 0x444 && DecodeKey(KeyLayout::Russian, 53, false) == 0x451 && DecodeKey(KeyLayout::Russian, 53, true) == 0x401);
    CHECK(DecodeKey(KeyLayout::Russian, 32, true) == 0x2116 && DecodeKey(KeyLayout::Russian, 56, false) == '.' && DecodeKey(KeyLayout::Russian, 56, true) == ',');
    CHECK(DecodeKey(KeyLayout::Russian, 30, true) == '!' && DecodeKey(KeyLayout::Russian, 54, false) == 0x431);
    // inverse (the engine's KeyForChar)
    CHECK(KeyForChar(KeyLayout::French, 'a') == 20 && KeyForChar(KeyLayout::French, 'q') == 4 && KeyForChar(KeyLayout::French, 'M') == 51);
    CHECK(KeyForChar(KeyLayout::German, 'y') == 29 && KeyForChar(KeyLayout::Nordic, 0xE5) == 47 && KeyForChar(KeyLayout::US, 'a') == 4 && KeyForChar(KeyLayout::US, ' ') == 44);
    CHECK(KeyForChar(KeyLayout::Russian, 0x444) == 4 && KeyForChar(KeyLayout::Russian, 'a') == 0 && KeyForChar(KeyLayout::US, 0) == 0);
    // names
    CHECK(KeyName(KeyLayout::French, 4) == "Q" && KeyName(KeyLayout::US, 44) == "Space" && KeyName(KeyLayout::Russian, 20) == "\xD0\x99" && KeyName(KeyLayout::US, 200) == kEmpty);
    CHECK(KeyName(KeyLayout::German, 47) == "\xC3\x9C" && EncodeUtf8(0x2116) == "\xE2\x84\x96" && EncodeUtf8('a') == "a");
    // language tags
    CHECK(LayoutFromLanguageTag("fr-FR") == KeyLayout::French && LayoutFromLanguageTag("fr-CA") == KeyLayout::US && LayoutFromLanguageTag("de-AT") == KeyLayout::German);
    CHECK(LayoutFromLanguageTag("de-CH") == KeyLayout::US && LayoutFromLanguageTag("sv-SE") == KeyLayout::Nordic && LayoutFromLanguageTag("RU_ru") == KeyLayout::Russian);
    CHECK(LayoutFromLanguageTag("en-US") == KeyLayout::US && LayoutFromLanguageTag("") == KeyLayout::US && LayoutFromLanguageTag("nb-NO") == KeyLayout::US);
}

namespace {
struct FakeHost : ClipboardHost {
    std::string system; bool writeOk = true, canRead = true, inFlight = false, ready = false; std::string readValue; int writes = 0;
    bool Write(const std::string& t) override { ++writes; if (!writeOk) return false; system = t; return true; }
    bool StartRead() override { if (!canRead) return false; inFlight = true; readValue = system; return true; }   // snapshot at start, like an async read
    bool TakeRead(std::string& t) override { if (inFlight && ready) { inFlight = ready = false; t = readValue; return true; } return false; }
};
}

static void ClipboardTests() {
    { // set reaches the system; get returns it; text copied elsewhere arrives after a read finishes, without blocking
        FakeHost h; UwpClipboard c(&h);
        CHECK(c.SetText("hello") && h.system == "hello" && !c.InAppOnly() && c.GetText() == "hello");
        h.system = "from another app"; h.ready = true;                   // read started by the first GetText snapshot "hello"
        CHECK(c.GetText() == "hello");                                    // that read carried the old text: same, and the next read starts
        h.ready = true; CHECK(c.GetText() == "from another app");   // the second read carried the new system text
    }
    { // a read that started before our own SetText must not overwrite the new text
        FakeHost h; UwpClipboard c(&h); h.system = "old";
        CHECK(c.GetText().empty());                                       // starts a read (snapshot "old")
        c.SetText("new"); h.ready = true;
        CHECK(c.GetText() == "new");                                      // the stale "old" is dropped
    }
    { // no system clipboard: still works inside the app and says so
        FakeHost h; h.writeOk = false; h.canRead = false; UwpClipboard c(&h);
        CHECK(c.SetText("x") && c.InAppOnly() && c.GetText() == "x");
        UwpClipboard none(nullptr); CHECK(none.SetText("y") && none.InAppOnly() && none.GetText() == "y");
    }
}

// ---- microphone capture (XboxCapture.hpp): permission, format conversion, queue, silence ----
namespace {
std::vector<std::int16_t> ReadS16(okay::xbox::UwpCaptureDevice& d) {
    std::vector<std::int16_t> v(d.QueuedBytes() / 2); const std::uint32_t n = d.Dequeue(v.data(), (std::uint32_t)v.size() * 2); v.resize(n / 2); return v;
}
}
static void CaptureTests() {
    using namespace okay::xbox;
    // permission: needs the capability AND not-denied consent
    CHECK(EvaluateMicPermission(false, MicConsent::Allowed).state == MicPermission::Denied);
    CHECK(EvaluateMicPermission(false, MicConsent::Allowed).reason.find("manifest") != std::string::npos);
    CHECK(EvaluateMicPermission(true, MicConsent::Denied).state == MicPermission::Denied);
    CHECK(EvaluateMicPermission(true, MicConsent::Denied).reason.find("privacy") != std::string::npos);
    CHECK(EvaluateMicPermission(true, MicConsent::Allowed).state == MicPermission::Granted);
    CHECK(EvaluateMicPermission(true, MicConsent::Unknown).state == MicPermission::Unknown);
    CHECK(SupportedCaptureFormat({48000, 2, true, 32}) && SupportedCaptureFormat({16000, 1, false, 16}) && !SupportedCaptureFormat({48000, 2, false, 24}) && !SupportedCaptureFormat({48000, 9, true, 32}) && !SupportedCaptureFormat({100, 1, true, 32}));
    okay::platform::AudioSpec want; want.freq = 48000; want.channels = 1; want.format = okay::platform::SampleFormat::S16;
    { // same rate, float stereo -> S16 mono: average of the channels, hand-computed (0.5 and -0.25 -> 0.125 -> 4096 by F32ToS16 = 0.125 * 32767 rounded)
        FakeCaptureHost h; h.fmt = {48000, 2, true, 32};
        UwpCaptureDevice d(want, h.fmt, &h, 0); std::string err; CHECK(d.Begin(err) && h.starts == 1);
        const float in[] = {0.5f, -0.25f, 0.5f, -0.25f, 0.5f, -0.25f};
        d.Deliver(in, sizeof(in), false);
        CHECK(d.QueuedBytes() == 0);                                       // paused (devices start paused): nothing recorded
        d.Pause(false); d.Deliver(in, sizeof(in), false);
        std::vector<std::int16_t> v = ReadS16(d);
        CHECK(v.size() == 2 && v[0] == 4096 && v[1] == 4096);              // 3 frames in, one held back for the interpolator
        d.Deliver(in, sizeof(in), false); v = ReadS16(d); CHECK(v.size() == 3);   // the held frame comes out with the next chunk: no frame lost
    }
    { // S16 mono 16 kHz in -> S16 mono 48 kHz out: 3x frames, linear ramp, state kept between chunks
        FakeCaptureHost h; h.fmt = {16000, 1, false, 16};
        UwpCaptureDevice d(want, h.fmt, &h, 0); d.Pause(false);
        const std::int16_t a[] = {0, 3000, 6000};
        d.Deliver(a, sizeof(a), false);
        std::vector<std::int16_t> v = ReadS16(d);
        CHECK(v.size() == 6);                                              // two source intervals -> 6 output frames
        CHECK(v[0] == 0 && v[1] == 1000 && v[2] == 2000 && v[3] == 3000 && v[4] == 4000 && v[5] == 5000);
        const std::int16_t b[] = {9000};
        d.Deliver(b, sizeof(b), false); v = ReadS16(d);
        CHECK(v.size() == 3 && v[0] == 6000 && v[1] == 7000 && v[2] == 8000);   // continues across the chunk boundary
    }
    { // 48 kHz -> 16 kHz: every third frame
        okay::platform::AudioSpec w2 = want; w2.freq = 16000; FakeCaptureHost h; h.fmt = {48000, 1, false, 16};
        UwpCaptureDevice d(w2, h.fmt, &h, 0); d.Pause(false);
        std::int16_t in[10]; for (int i = 0; i < 10; ++i) in[i] = (std::int16_t)(i * 100);
        d.Deliver(in, sizeof(in), false);
        std::vector<std::int16_t> v = ReadS16(d);
        CHECK(v.size() == 3 && v[0] == 0 && v[1] == 300 && v[2] == 600);
    }
    { // F32 spec, mono device to stereo spec; a partial trailing frame is ignored
        okay::platform::AudioSpec w2; w2.freq = 48000; w2.channels = 2; w2.format = okay::platform::SampleFormat::F32;
        FakeCaptureHost h; h.fmt = {48000, 1, true, 32};
        UwpCaptureDevice d(w2, h.fmt, &h, 0); d.Pause(false);
        const float in[] = {0.25f, 0.5f, 0.75f};
        d.Deliver(in, sizeof(in) - 2, false);                              // 2 whole frames and 2 stray bytes
        float out[8]; const std::uint32_t n = d.Dequeue(out, sizeof(out));
        CHECK(n == 8 && out[0] == 0.25f && out[1] == 0.25f);               // 2 frames in, 1 out (one held back): mono -> both channels
    }
    { // silent flag -> zeros of the same length; NaN -> 0
        FakeCaptureHost h; h.fmt = {48000, 1, true, 32};
        UwpCaptureDevice d(want, h.fmt, &h, 0); d.Pause(false);
        const float in[] = {0.9f, 0.9f, 0.9f};
        d.Deliver(in, sizeof(in), true); std::vector<std::int16_t> v = ReadS16(d);
        CHECK(v.size() == 2 && v[0] == 0 && v[1] == 0);
        const float nan[] = {std::nanf(""), std::nanf(""), 0.0f};
        d.Deliver(nan, sizeof(nan), false); v = ReadS16(d); CHECK(v.size() >= 1 && v[0] == 0);
    }
    { // underrun: Dequeue returns only what is there, DequeueFilled pads with silence and counts it
        FakeCaptureHost h; h.fmt = {48000, 1, false, 16};
        UwpCaptureDevice d(want, h.fmt, &h, 0); d.Pause(false);
        const std::int16_t in[] = {1000, 1000, 1000}; d.Deliver(in, sizeof(in), false);     // 2 frames queued (4 bytes)
        std::int16_t out[4] = {7, 7, 7, 7};
        CHECK(d.DequeueFilled(out, 8) == 4 && out[0] == 1000 && out[1] == 1000 && out[2] == 0 && out[3] == 0 && d.UnderrunBytes() == 4);
        CHECK(d.Dequeue(out, 8) == 0);
    }
    { // callback mode gets the converted bytes; the queue stays empty
        struct Sink { static void Cb(void* u, std::uint8_t*, int len) { *static_cast<int*>(u) += len; } };
        int got = 0; okay::platform::AudioSpec w2 = want; w2.callback = &Sink::Cb; w2.user = &got;
        FakeCaptureHost h; h.fmt = {48000, 1, false, 16};
        UwpCaptureDevice d(w2, h.fmt, &h, 0); d.Pause(false);
        const std::int16_t in[] = {1, 2, 3, 4, 5}; d.Deliver(in, sizeof(in), false);
        CHECK(got == 8 && d.QueuedBytes() == 0);
    }
    { // the queue is capped by the engine's DeliverCapture (oldest dropped): a long recording never grows without bound
        FakeCaptureHost h; h.fmt = {48000, 1, false, 16};
        UwpCaptureDevice d(want, h.fmt, &h, 0); d.Pause(false);
        std::vector<std::int16_t> big(300000, 5); for (int i = 0; i < 4; ++i) d.Deliver(big.data(), (std::uint32_t)(big.size() * 2), false);
        CHECK(d.QueuedBytes() <= (1u << 20));
        d.ClearQueued(); CHECK(d.QueuedBytes() == 0);
    }
    { // closing stops the host exactly once; a refused start gives no device state to stop
        FakeCaptureHost h; h.fmt = {48000, 1, false, 16};
        { UwpCaptureDevice d(want, h.fmt, &h, 0); std::string e; CHECK(d.Begin(e)); }
        CHECK(h.starts == 1 && h.stops == 1);
        h.startOk = false; h.refuseText = "the user said no";
        { UwpCaptureDevice d(want, h.fmt, &h, 0); std::string e; CHECK(!d.Begin(e) && e == "the user said no"); }
        CHECK(h.stops == 1);
    }
}

int main() {
    CaptureTests();
    PendingTests(); SplashTests(); LayoutTests(); ClipboardTests();
    if (g_fail) { std::printf("test_xbox_uwp_pieces: %d FAILED\n", g_fail); return 1; }
    std::printf("test_xbox_uwp_pieces: all passed\n");
    return 0;
}
