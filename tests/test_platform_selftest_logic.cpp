// Pure tests for tools/platform_selftest/SelfTestLogic.hpp: tone, event tally, summary text.
#include "SelfTestLogic.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace okay;
using namespace okay::selftest;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main() {
    // 440 Hz for exactly one second at 48 kHz crosses zero upward 440 times (+-1 for the edges).
    {
        ToneGen g(440.0, 48000, 2, 0.5);
        std::vector<int16_t> s(48000 * 2);
        g.FillS16(s.data(), 48000);
        const int n = UpwardCrossings(s.data(), 48000, 2);
        CHECK(n >= 439 && n <= 441);
        int peak = 0;
        bool same = true;
        for (int i = 0; i < 48000; ++i) { peak = std::max(peak, std::abs((int)s[2 * i])); if (s[2 * i] != s[2 * i + 1]) same = false; }
        CHECK(peak >= 16380 && peak <= 16384);      // 0.5 * 32767 = 16383.5
        CHECK(same);
        CHECK(s[0] == 0);                           // starts at phase 0
        CHECK(s[2 * 27] > 0 && s[2 * 27] < 16384);  // quarter period of 48000/440 = 109.09 frames is 27.3: near the top
    }
    // Chunks join exactly like one long run (no click at the seams).
    {
        ToneGen a(440.0, 44100, 1, 0.8), b(440.0, 44100, 1, 0.8);
        std::vector<int16_t> whole(10000), parts(10000);
        a.FillS16(whole.data(), 10000);
        b.FillS16(parts.data(), 1);
        b.FillS16(parts.data() + 1, 4095);
        b.FillS16(parts.data() + 4096, 5904);
        CHECK(whole == parts);
        // The biggest step between neighbours is small (a clean sine at 440 Hz: 2*pi*440/44100*0.8*32767 = 1642).
        int maxd = 0;
        for (int i = 1; i < 10000; ++i) maxd = std::max(maxd, std::abs((int)parts[i] - (int)parts[i - 1]));
        CHECK(maxd <= 1645);
    }
    // Float output stays inside the amplitude; amplitude is clamped to 0..1; a bad rate gives silence, not a crash.
    {
        ToneGen g(1000.0, 48000, 1, 2.0);
        std::vector<float> f(48000);
        g.FillF32(f.data(), 48000);
        float lo = 0, hi = 0;
        for (float v : f) { lo = std::min(lo, v); hi = std::max(hi, v); }
        CHECK(hi <= 1.0f && hi > 0.99f && lo >= -1.0f && lo < -0.99f);
        ToneGen z(440.0, 0, 1, 1.0);
        int16_t b[4] = {1, 1, 1, 1};
        z.FillS16(b, 4);
        CHECK(b[0] == 0 && b[3] == 0);
        ToneGen m(440.0, 48000, 0, -3.0);           // channels < 1 becomes 1, amplitude < 0 becomes 0
        int16_t q[3] = {9, 9, 9};
        m.FillS16(q, 3);
        CHECK(q[0] == 0 && q[1] == 0 && q[2] == 0);
    }
    // Key names and event lines.
    {
        using platform::Key;
        CHECK(KeyName(Key::A) == "A" && KeyName(Key::Z) == "Z" && KeyName(Key::N1) == "1" && KeyName(Key::N9) == "9" && KeyName(Key::N0) == "0");
        CHECK(KeyName(Key::F1) == "F1" && KeyName(Key::F12) == "F12" && KeyName(Key::Escape) == "Esc" && KeyName(Key::Space) == "Space");
        CHECK(KeyName(Key::Application) == "key#101" && KeyName(Key::Unknown) == "?");
        platform::Event e;
        e.type = platform::EventType::KeyDown; e.key = Key::W;
        CHECK(DescribeEvent(e) == "key down W");
        e.repeat = true;
        CHECK(DescribeEvent(e) == "key down W (repeat)");
        e = platform::Event(); e.type = platform::EventType::MouseWheel; e.wheelY = 1.0f;
        CHECK(DescribeEvent(e) == "wheel 0.00,1.00");
        e = platform::Event(); e.type = platform::EventType::MouseButtonDown; e.button = 1; e.x = 10; e.y = 20; e.clicks = 2;
        CHECK(DescribeEvent(e) == "mouse button 1 down at 10,20 clicks=2");
        e = platform::Event(); e.type = platform::EventType::TextInput; e.text = "\xC3\xA4";
        CHECK(DescribeEvent(e) == "text \"\xC3\xA4\"");
        e = platform::Event(); e.type = platform::EventType::MouseMove;
        CHECK(DescribeEvent(e).empty());
    }
    // Tally: repeats do not count as presses, distinct keys are counted once, Esc and close are noticed.
    {
        EventTally t;
        platform::Event e;
        e.type = platform::EventType::KeyDown; e.key = platform::Key::A; t.Add(e);
        e.repeat = true; t.Add(e); t.Add(e);
        e.repeat = false; e.key = platform::Key::B; t.Add(e);
        e.key = platform::Key::A; t.Add(e);
        CHECK(t.keyDown == 3 && t.distinctKeys == 2 && !t.sawEscape);
        e.key = platform::Key::Escape; t.Add(e);
        CHECK(t.sawEscape);
        e = platform::Event(); e.type = platform::EventType::MouseMove; t.Add(e); t.Add(e);
        e.type = platform::EventType::MouseWheel; t.Add(e);
        e.type = platform::EventType::Quit; t.Add(e);
        CHECK(t.mouseMove == 2 && t.wheel == 1 && t.sawClose);
        e.key = (platform::Key)5000; e.type = platform::EventType::KeyDown; t.Add(e);   // out-of-range key id must not crash
        CHECK(t.keyDown == 5);
    }
    // Report text.
    {
        Report r;
        r.Add("window", Status::Pass, "640x400");
        r.Add("keyboard", Status::Skip, "no key pressed");
        r.Add("note", Status::Info);
        CHECK(r.ExitCode() == 0 && r.Count(Status::Pass) == 1 && r.Count(Status::Skip) == 1);
        const std::string t = r.Format("Title", "own", "");
        CHECK(t == "Title\n=====\nbackend: own\n\n[PASS] window - 640x400\n[NOT TESTED] keyboard - no key pressed\n[info] note\n\n"
                   "1 passed, 0 failed, 1 not tested.\nRESULT: nothing failed. Steps marked NOT TESTED need you to press, click or plug something in.\n");
        r.Add("audio", Status::Fail, "no device");
        CHECK(r.ExitCode() == 1);
        const std::string t2 = r.Format("T", "own", "now");
        CHECK(t2.find("when: now\n") != std::string::npos && t2.find("[FAIL] audio - no device") != std::string::npos &&
              t2.find("1 passed, 1 failed, 1 not tested.") != std::string::npos && t2.find("RESULT: FAILED.") != std::string::npos);
    }
    // --ci: a step that needs hardware a build machine may lack (GPU OpenGL, a sound card) is NOT TESTED when it does
    // not work there, never PASS; on a person's PC the same failure is a FAIL. A success is a PASS either way.
    {
        CHECK(HardwareStatus(true, false) == Status::Pass && HardwareStatus(true, true) == Status::Pass);
        CHECK(HardwareStatus(false, false) == Status::Fail && HardwareStatus(false, true) == Status::Skip);
        CHECK(HardwareDetail("no device", false) == "no device");
        CHECK(HardwareDetail("no device", true) == "no device (counted as not tested: --ci, this machine may have none)");
    }
    // Event pump verdict: a pushed event must come back through PollEvent, and a resize asked of the window must
    // arrive as a WindowResized event with the new size (that one goes through the operating system's queue).
    {
        std::string d;
        CHECK(PumpVerdict(true, true, 520, 340, 520, 340, d) == Status::Pass);
        CHECK(d == "a pushed event came back; asking for 520x340 gave a resize event of 520x340");
        CHECK(PumpVerdict(false, true, 520, 340, 520, 340, d) == Status::Fail);
        CHECK(d.find("the pushed event did not come back") != std::string::npos);
        CHECK(PumpVerdict(true, false, 0, 0, 520, 340, d) == Status::Fail);
        CHECK(d.find("no resize event arrived") != std::string::npos);
        CHECK(PumpVerdict(true, true, 500, 340, 520, 340, d) == Status::Fail);
        CHECK(d.find("gave a resize event of 500x340") != std::string::npos);
    }
    std::printf("platform_selftest_logic: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
