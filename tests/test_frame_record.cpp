// 1.1 Frame recorder and player: the "okayframes 1" text form (strict loading, fuzzing), the recorder
// and player objects, the summary, and the property that a recorded GameLoop session replays with
// identical Time values. Made-up frame times only: nothing sleeps and nothing reads a real clock.
#include "test_framework.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/FrameRecord.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace okay;

namespace {

void Fresh() {
    Game::SetPauseOnFocusLoss(false);
    Game::NotifyFocus(true);
    Game::Reset();
    Time::ResetForTesting();
    Time::SetFixedDeltaTime(0.02f);
    ClockSet::Global().Reset();
}

std::uint32_t g_s = 20240601u;
std::uint32_t Rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5; return g_s; }
float Rand01() { return (float)(Rnd() >> 8) / 16777216.0f; }

// Text with a correct checksum around an arbitrary body (so the parser, not the checksum, is what rejects it).
std::string Seal(const std::string& headerAndBody) {
    return headerAndBody + "checksum " + savefmt::Hex16(savefmt::Fnv1a(headerAndBody.data(), headerAndBody.size())) + "\n";
}
std::string Doc(const std::string& frames, const std::string& micros, const std::string& tokens) {
    return Seal("okayframes 1\nframes " + frames + "\nmicros " + micros + "\n" + tokens + (tokens.empty() ? "" : "\n"));
}

bool Rejects(const std::string& text, const char* why = nullptr) {
    FrameRecording keep;
    keep.Add(0.016);
    FrameRecording out = keep;
    std::string err;
    const bool ok = FrameRecording::FromText(text, out, &err);
    if (ok) return false;
    if (out != keep) return false;               // a failed load never touches the target
    if (err.empty()) return false;
    if (why && err.find(why) == std::string::npos) { std::fprintf(stderr, "  (error was: %s)\n", err.c_str()); return false; }
    return true;
}

FrameRecording Sample() {
    FrameRecording r;
    for (int i = 0; i < 120; ++i) r.Add(0.016667);
    r.Add(0.016666);
    r.Add(0.25, kFrameHitch);
    for (int i = 0; i < 3; ++i) r.Add(0.016667, kFramePaused, 7);
    r.Add(0.02, kFrameStepped | kFramePaused);
    r.Add(0.0333333, kFrameSynthetic, -42);
    return r;
}

}  // namespace

int main() {
    RUN_SUITE("frame_record");
    Log::console = false;

    // ---- microseconds ------------------------------------------------------------------------------------------------------------------
    {
        CHECK(FrameRecording::ToMicros(0.0166666) == 16667);
        CHECK(FrameRecording::ToMicros(0.0166664) == 16666);
        CHECK(FrameRecording::ToMicros(1.0) == 1000000);
        CHECK(FrameRecording::ToMicros(-1.0) == 0);
        CHECK(FrameRecording::ToMicros(std::numeric_limits<double>::quiet_NaN()) == 0);
        CHECK(FrameRecording::ToMicros(std::numeric_limits<double>::infinity()) == 0);
        CHECK(FrameRecording::ToMicros(7200.0) == FrameRecording::kMaxFrameMicros);          // two hours: capped at one
        CHECK(FrameRecording::ToMicros(1e300) == FrameRecording::kMaxFrameMicros);
        // a quantized time reads back bit for bit (what makes live and replay agree)
        bool stable = true;
        for (int i = 0; i < 20000; ++i) {
            const double q = FrameRecording::Quantize((double)Rand01() * 0.3);
            if (FrameRecording::Quantize(q) != q) stable = false;
            if (FrameRecording::ToSeconds(FrameRecording::ToMicros(q)) != q) stable = false;
        }
        CHECK(stable);
    }

    // ---- the data: runs, access, caps ----------------------------------------------------------------------------------------------------
    {
        FrameRecording r;
        CHECK(r.Empty() && r.Frames() == 0 && r.Runs() == 0 && r.TotalMicros() == 0);
        for (int i = 0; i < 10; ++i) CHECK(r.Add(0.016667));
        CHECK(r.Frames() == 10 && r.Runs() == 1 && r.TotalMicros() == 166670);   // neighbours merge into a run
        r.Add(0.016667, kFramePaused);
        r.Add(0.016667, kFramePaused, 3);
        r.Add(0.016667, kFramePaused, 3);
        CHECK(r.Frames() == 13 && r.Runs() == 3);
        FrameSample s;
        CHECK(r.At(0, s) && s.micros == 16667 && s.flags == 0 && s.marker == 0);
        CHECK(r.At(9, s) && s.flags == 0);
        CHECK(r.At(10, s) && s.flags == kFramePaused && s.marker == 0);
        CHECK(r.At(12, s) && s.flags == kFramePaused && s.marker == 3);
        CHECK(!r.At(13, s) && !r.At(~std::uint64_t(0), s));
        std::uint64_t count = 0;
        CHECK(r.RunAt(2, s, count) && count == 2 && s.marker == 3);
        CHECK(!r.RunAt(3, s, count));
        std::vector<FrameSample> all = r.Expand();
        CHECK(all.size() == 13 && all[10].flags == kFramePaused);
        // unknown flag bits are dropped, a time over an hour is capped
        FrameRecording f;
        FrameSample big; big.micros = 4000000000u; big.flags = 0xFF;
        f.Add(big);
        CHECK(f.At(0, s) && s.micros == FrameRecording::kMaxFrameMicros && s.flags == (kFramePaused | kFrameHitch | kFrameStepped | kFrameSynthetic));
        // the frame cap
        FrameRecording cap;
        FrameSample one; one.micros = 5;
        CHECK(cap.AddRun(one, FrameRecording::kMaxFrames));
        CHECK(!cap.Add(one) && cap.Frames() == FrameRecording::kMaxFrames);
        FrameRecording cap2;
        CHECK(!cap2.AddRun(one, FrameRecording::kMaxFrames + 1) && cap2.Empty());
        CHECK(cap2.AddRun(one, 0) && cap2.Empty());
        // a long capture of one rate is one run, and At() finds the last frame
        FrameRecording cap3;
        cap3.AddRun(one, 999999);
        one.marker = 1;
        cap3.Add(one);
        CHECK(cap3.Runs() == 2 && cap3.At(999999, s) && s.marker == 1 && cap3.At(999998, s) && s.marker == 0);
        // equality
        FrameRecording a = Sample(), b = Sample();
        CHECK(a == b);
        b.Add(0.001);
        CHECK(a != b);
        a.Clear();
        CHECK(a.Empty() && a.Runs() == 0);
    }

    // ---- the text form: layout, round trip ------------------------------------------------------------------------------------------------
    {
        FrameRecording r;
        r.AddRun([] { FrameSample s; s.micros = 16667; return s; }(), 120);
        r.Add(0.016666);
        r.Add(0.25, kFrameHitch);
        r.Add(0.016667, kFramePaused, 7);
        r.Add(0.016667, kFramePaused, 7);
        r.Add(0.02, kFrameStepped | kFramePaused);
        r.Add(0.0333333, kFrameSynthetic, -42);
        const std::string t = r.ToText();
        CHECK(t.compare(0, 14, "okayframes 1\nf") == 0);
        CHECK(t.find("frames 126\n") != std::string::npos);
        CHECK(t.find("16667*120 16666 250000/h 16667/p@7*2 20000/ps 33333/c@-42\n") != std::string::npos);
        CHECK(t.find("\nchecksum ") != std::string::npos && t.back() == '\n');
        FrameRecording back; std::string err;
        CHECK(FrameRecording::FromText(t, back, &err) && err.empty());
        CHECK(back == r && back.Frames() == 126 && back.TotalMicros() == r.TotalMicros());
        CHECK(back.ToText() == t);                             // canonical: writing it again gives the same bytes
        // an empty recording
        FrameRecording e, e2;
        CHECK(FrameRecording::FromText(e.ToText(), e2) && e2.Empty());
        CHECK(e.ToText().find("frames 0\nmicros 0\nchecksum ") != std::string::npos);
        // lines of at most 12 tokens
        FrameRecording many;
        for (int i = 0; i < 100; ++i) many.Add(0.001 * (double)(i + 1));
        const std::string mt = many.ToText();
        std::size_t maxTokens = 0, pos = 0;
        while (pos < mt.size()) {
            const std::size_t e2 = mt.find('\n', pos);
            const std::string line = mt.substr(pos, e2 - pos);
            if (!line.empty() && line[0] >= '0' && line[0] <= '9') {
                std::size_t n = 1;
                for (char c : line) if (c == ' ') ++n;
                if (n > maxTokens) maxTokens = n;
            }
            pos = e2 + 1;
        }
        CHECK(maxTokens == 12);
        FrameRecording mb;
        CHECK(FrameRecording::FromText(mt, mb) && mb == many);
        // CRLF line ends (a file that went through a Windows editor or git) still load
        std::string crlf;
        for (char c : t) { if (c == '\n') crlf += '\r'; crlf += c; }
        FrameRecording cb;
        CHECK(FrameRecording::FromText(crlf, cb) && cb == r);
        // lenient where it is safe: blanks and an uppercase checksum
        std::string upper = t;
        const std::size_t ck = upper.rfind("checksum ") + 9;
        for (std::size_t i = ck; i < ck + 16; ++i) if (upper[i] >= 'a' && upper[i] <= 'f') upper[i] = (char)(upper[i] - 'a' + 'A');
        FrameRecording ub;
        CHECK(FrameRecording::FromText(upper, ub) && ub == r);
        FrameRecording tb;
        CHECK(FrameRecording::FromText(t + "\n\n  \n", tb) && tb == r);
    }

    // ---- strict loading: every kind of damage is an error with a message, never a crash ----------------------------------------------------------
    {
        const std::string good = Sample().ToText();
        FrameRecording g;
        CHECK(FrameRecording::FromText(good, g));
        CHECK(Rejects("", "empty"));
        CHECK(Rejects("hello", "okayframes"));
        CHECK(Rejects("okayframes 2\nframes 0\nmicros 0\nchecksum 0000000000000000\n", "version"));
        CHECK(Rejects("okayframes 1x\n", "version"));
        CHECK(Rejects("okayframes \n", "version"));
        CHECK(Rejects("okayframes 99999999999999999999\n", "version"));
        CHECK(Rejects("okayframes 1\n", "frames"));
        CHECK(Rejects("okayframes 1\nframes 0\n", "micros"));
        CHECK(Rejects("okayframes 1\nframes x\nmicros 0\n", "frames"));
        CHECK(Rejects("okayframes 1\nframes 0\nmicros -5\n", "micros"));
        CHECK(Rejects("okayframes 1\nframes 0\nmicros 0\n", "checksum"));                        // truncated: no checksum line
        CHECK(Rejects(Seal("okayframes 1\nframes 0\nmicros 0\n") + "extra\n", "after"));
        CHECK(Rejects("okayframes 1\nframes 0\nmicros 0\nchecksum 123\n", "checksum"));
        CHECK(Rejects("okayframes 1\nframes 0\nmicros 0\nchecksum zzzzzzzzzzzzzzzz\n", "checksum"));
        CHECK(Rejects("okayframes 1\nframes 0\nmicros 0\nchecksum 0000000000000000\n", "mismatch"));
        // headers that lie about the content (the checksum is right, the totals are not)
        CHECK(Rejects(Doc("5", "100", "10*5"), "total time"));
        CHECK(Rejects(Doc("4", "50", "10*5"), "frame count"));
        CHECK(Rejects(Doc("0", "0", "10"), "frame count"));
        CHECK(Rejects(Doc("1000001", "0", ""), "too many"));
        CHECK(Rejects(Doc("99999999999999", "0", ""), "too many"));
        CHECK(Rejects(Doc("1", "99999999999999999999", "5"), "micros"));
        // tokens
        CHECK(Rejects(Doc("1", "5", "abc"), "number"));
        CHECK(Rejects(Doc("1", "5", "5x"), "unexpected"));
        CHECK(Rejects(Doc("1", "5", "-5"), "number"));
        CHECK(Rejects(Doc("1", "5", "+5"), "number"));
        CHECK(Rejects(Doc("1", "4000000000", "4000000000"), "hour"));
        CHECK(Rejects(Doc("1", "0", "99999999999"), "number"));
        CHECK(Rejects(Doc("1", "5", "5*0"), "run length"));
        CHECK(Rejects(Doc("1", "5", "5*"), "run length"));
        CHECK(Rejects(Doc("1", "5", "5*-1"), "run length"));
        CHECK(Rejects(Doc("1", "5", "5*99999999999"), "run length"));
        CHECK(Rejects(Doc("1", "5", "5*2000000"), "limit"));
        CHECK(Rejects(Doc("1", "5", "5/"), "flags"));
        CHECK(Rejects(Doc("1", "5", "5/x"), "flags"));
        CHECK(Rejects(Doc("1", "5", "5/pp"), "repeated"));
        CHECK(Rejects(Doc("1", "5", "5/ph*2x"), "unexpected"));
        CHECK(Rejects(Doc("1", "5", "5@"), "marker"));
        CHECK(Rejects(Doc("1", "5", "5@-"), "marker"));
        CHECK(Rejects(Doc("1", "5", "5@99999999999"), "marker"));
        CHECK(Rejects(Doc("1", "5", "5@3000000000"), "range"));
        CHECK(Rejects(Doc("1", "5", "5*2*3"), "unexpected"));
        CHECK(Rejects(Doc("1", "5", "5@1/p"), "unexpected"));                                   // the order is time, flags, marker, count
        CHECK(Rejects(Doc("1", "5", std::string(100, '5')), "long"));
        CHECK(Rejects(Doc("1000000", "1000000", "1*1000000 2*1"), "too many"));                 // a second run passes the cap
        // the size cap is checked before anything is parsed
        {
            std::string huge(FrameRecording::kMaxFileBytes + 1, 'a');
            CHECK(Rejects(huge, "large"));
        }
        // accepted extremes
        FrameRecording x;
        CHECK(FrameRecording::FromText(Doc("1", "3600000000", "3600000000"), x) && x.TotalMicros() == 3600000000ull);
        CHECK(FrameRecording::FromText(Doc("1", "0", "0"), x) && x.Frames() == 1 && x.TotalMicros() == 0);
        CHECK(FrameRecording::FromText(Doc("1", "7", "7@2147483647"), x));
        CHECK(FrameRecording::FromText(Doc("1", "7", "7@-2147483647"), x));
        CHECK(FrameRecording::FromText(Doc("1000000", "1000000", "1*1000000"), x) && x.Frames() == 1000000 && x.Runs() == 1);
        CHECK(FrameRecording::FromText(Doc("3", "3", "1*1 1*1 1*1 "), x) && x.Runs() == 1);     // adjacent equal runs merge: still canonical
        CHECK(FrameRecording::FromText(Doc("2", "10", "5\t5"), x));                              // blanks are blanks
    }

    // ---- every one-byte change and every cut is caught ------------------------------------------------------------------------------------------------
    {
        const std::string good = Sample().ToText();
        int flipsAccepted = 0, flips = 0;
        for (std::size_t i = 0; i < good.size(); ++i) {
            std::string bad = good;
            bad[i] = (char)(bad[i] ^ 0x01);
            FrameRecording out;
            ++flips;
            if (FrameRecording::FromText(bad, out)) ++flipsAccepted;
        }
        CHECK(flips == (int)good.size() && flipsAccepted == 0);
        int cutsAccepted = 0;
        for (std::size_t n = 0; n < good.size(); ++n) {
            FrameRecording out;
            if (FrameRecording::FromText(good.substr(0, n), out)) ++cutsAccepted;
        }
        CHECK(cutsAccepted == 1);                              // only the file without its last newline is whole
        // a deleted byte, and a doubled byte
        int mangled = 0;
        for (std::size_t i = 0; i < good.size(); ++i) {
            FrameRecording out;
            std::string del = good; del.erase(i, 1);
            std::string dup = good; dup.insert(i, 1, good[i]);
            if (FrameRecording::FromText(del, out)) ++mangled;
            if (FrameRecording::FromText(dup, out)) ++mangled;
        }
        CHECK(mangled <= 4);                                   // a repeated space or newline is still the same text, nothing else
    }

    // ---- fuzz: garbage never crashes, and whatever loads loads consistently --------------------------------------------------------------------------
    {
        g_s = 987654321u;
        const std::string good = Sample().ToText();
        int accepted = 0, rejected = 0;
        bool consistent = true;
        for (int k = 0; k < 4000; ++k) {
            std::string t;
            const int mode = (int)(Rnd() % 4u);
            if (mode == 0) {                                  // raw random bytes
                const int len = (int)(Rnd() % 200u);
                for (int i = 0; i < len; ++i) t += (char)(Rnd() >> 11);
            } else if (mode == 1) {                           // a good file with a few bytes changed
                t = good;
                const int edits = 1 + (int)(Rnd() % 4u);
                for (int e = 0; e < edits; ++e) t[Rnd() % t.size()] = (char)(Rnd() >> 11);
            } else if (mode == 2) {                           // a valid header and checksum around random token soup
                static const char alphabet[] = "0123456789/@*-phsc \n.x";
                std::string body = "okayframes 1\nframes " + std::to_string(Rnd() % 50u) + "\nmicros " + std::to_string(Rnd() % 5000u) + "\n";
                const int len = (int)(Rnd() % 120u);
                for (int i = 0; i < len; ++i) body += alphabet[Rnd() % (sizeof(alphabet) - 1)];
                body += "\n";
                t = Seal(body);
            } else {                                          // a consistent document whose totals were guessed from the content
                FrameRecording r;
                const int n = (int)(Rnd() % 40u);
                for (int i = 0; i < n; ++i) r.Add((double)(Rnd() % 300000u) / 1e6, (std::uint8_t)(Rnd() & 15u), (int)(Rnd() % 9u) - 4);
                t = r.ToText();
            }
            FrameRecording out; std::string err;
            if (FrameRecording::FromText(t, out, &err)) {
                ++accepted;
                FrameRecording again;                          // whatever was accepted writes out and reads back the same
                if (!FrameRecording::FromText(out.ToText(), again) || !(again == out)) consistent = false;
                if (out.Frames() > FrameRecording::kMaxFrames) consistent = false;
            } else {
                ++rejected;
                if (err.empty()) consistent = false;
            }
        }
        CHECK(consistent);
        CHECK(accepted > 500 && rejected > 1000);              // both kinds really happened
        // huge counts: a header or a run claiming more than the cap, a million tiny tokens
        FrameRecording big;
        for (int i = 0; i < 2000; ++i) big.Add((double)(1 + i % 97) / 1e5);
        FrameRecording bigBack;
        CHECK(FrameRecording::FromText(big.ToText(), bigBack) && bigBack == big);
    }

    // ---- files: atomic save, load, failures ---------------------------------------------------------------------------------------------------------------
    {
        const std::string path = "test_frame_record_tmp.okayframes";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        const FrameRecording r = Sample();
        std::string err;
        CHECK(r.Save(path, &err) && err.empty());
        CHECK(std::filesystem::exists(path) && !std::filesystem::exists(path + ".tmp"));   // written whole, no temp file left
        FrameRecording back;
        CHECK(FrameRecording::Load(path, back, &err) && back == r);
        // an old good file survives a save that fails
        CHECK(!r.Save("no_such_dir_okay_frames/x.okayframes", &err) && !err.empty());
        FrameRecording keep = r;
        CHECK(!FrameRecording::Load("no_such_file.okayframes", keep, &err) && !err.empty() && keep == r);
        // overwrite replaces it
        FrameRecording r2;
        r2.Add(0.5);
        CHECK(r2.Save(path) && FrameRecording::Load(path, back) && back == r2);
        // a damaged file is an error and leaves the target alone
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f << "okayframes 1\nframes 1\nmicros 5\n5\nchecksum 0000000000000000\n";
        }
        FrameRecording untouched = r;
        CHECK(!FrameRecording::Load(path, untouched, &err) && untouched == r && err.find("checksum") != std::string::npos);
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            for (int i = 0; i < 3000; ++i) f << (char)(Rnd() >> 11);
        }
        CHECK(!FrameRecording::Load(path, untouched, &err) && untouched == r);
        CHECK(!r.Save("", &err));
        std::filesystem::remove(path, ec);
    }

    // ---- the recorder ------------------------------------------------------------------------------------------------------------------------------------------
    {
        FrameRecorder rec;
        CHECK(rec.IsRecording() && !rec.Full() && !rec.Truncated());
        CHECK(rec.Add(0.016) && rec.Add(0.017, kFrameHitch, 5));
        rec.Stop();
        CHECK(!rec.IsRecording() && !rec.Add(0.018));
        CHECK(rec.Recording().Frames() == 2);
        rec.Start();
        CHECK(rec.Add(0.018) && rec.Recording().Frames() == 3);
        FrameSample s;
        CHECK(rec.Recording().At(1, s) && s.micros == 17000 && s.flags == kFrameHitch && s.marker == 5);
        CHECK(rec.Recording().At(0, s) && s.micros == 16000);
        // a small cap: it says it ran out
        FrameRecorder small(3);
        CHECK(small.MaxFrames() == 3);
        for (int i = 0; i < 3; ++i) CHECK(small.Add(0.01 * (i + 1)));
        CHECK(small.Full() && !small.Truncated());
        CHECK(!small.Add(0.5) && small.Truncated() && small.Recording().Frames() == 3);
        small.Clear();
        CHECK(small.Recording().Empty() && !small.Truncated() && small.Add(0.1));
        FrameRecorder over(FrameRecording::kMaxFrames * 10);   // the cap cannot be raised past the recording's own
        CHECK(over.MaxFrames() == FrameRecording::kMaxFrames);
    }

    // ---- the player -------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const FrameRecording r = Sample();
        FramePlayer p(r);
        CHECK(p.Frames() == r.Frames() && p.HasNext() && !p.Finished() && p.Position() == 0 && p.Remaining() == r.Frames());
        std::vector<FrameSample> all = r.Expand(), got;
        FrameSample s;
        while (p.Next(s)) got.push_back(s);
        CHECK(got == all && p.Finished() && p.Remaining() == 0 && p.Position() == r.Frames());
        FrameSample untouched; untouched.micros = 123;
        CHECK(!p.Next(untouched) && untouched.micros == 123);
        p.Rewind();
        CHECK(p.HasNext() && p.Next(s) && s == all[0]);
        // seeking: every index, in a shuffled order, then reading on from there
        bool seekOk = true;
        for (int k = 0; k < 500; ++k) {
            const std::uint64_t at = Rnd() % r.Frames();
            if (!p.Seek(at) || p.Position() != at) { seekOk = false; continue; }
            FrameSample a, b;
            if (!p.Next(a) || !(a == all[(std::size_t)at])) seekOk = false;
            if (at + 1 < r.Frames() && (!p.Next(b) || !(b == all[(std::size_t)at + 1]))) seekOk = false;
        }
        CHECK(seekOk);
        CHECK(!p.Seek(r.Frames()) && !p.Seek(~std::uint64_t(0)));
        // an empty recording has nothing to play
        FramePlayer none;
        CHECK(!none.HasNext() && none.Finished() && !none.Next(s) && none.Remaining() == 0);
        FramePlayer empty{FrameRecording()};
        CHECK(!empty.HasNext() && !empty.Next(s));
        // looping
        FrameRecording three;
        three.Add(0.001); three.Add(0.002); three.Add(0.003);
        FramePlayer loop(three);
        loop.SetLooping(true);
        std::vector<std::uint32_t> micros;
        for (int i = 0; i < 8; ++i) { loop.Next(s); micros.push_back(s.micros); }
        CHECK((micros == std::vector<std::uint32_t>{1000, 2000, 3000, 1000, 2000, 3000, 1000, 2000}));
        CHECK(loop.Looping() && loop.HasNext() && loop.Loops() == 2);
        // loading a new recording rewinds
        p.Load(three);
        CHECK(p.Frames() == 3 && p.Position() == 0 && p.Loops() == 0);
    }

    // ---- the summary ---------------------------------------------------------------------------------------------------------------------------------------------------
    {
        FrameRecording r;
        for (int i = 0; i < 100; ++i) r.Add(0.016667);
        r.Add(0.25, kFrameHitch);
        FrameSummary s = r.Summary();
        CHECK(s.frames == 101);
        CHECK_NEAR(s.seconds, 1.91670, 1e-9);
        CHECK_NEAR(s.minMs, 16.667, 1e-9);
        CHECK_NEAR(s.maxMs, 250.0, 1e-9);
        CHECK_NEAR(s.avgMs, (100 * 16.667 + 250.0) / 101.0, 1e-9);
        CHECK_NEAR(s.p50Ms, 16.667, 1e-9);
        CHECK_NEAR(s.p95Ms, 16.667, 1e-9);
        CHECK_NEAR(s.p99Ms, 16.667, 1e-9);                     // one slow frame in 101 is above the 99th percentile
        CHECK_NEAR(s.avgFps, 101.0 / 1.9167, 1e-6);
        CHECK(s.hitches == 1 && std::fabs(s.longestHitchMs - 250.0) < 1e-9 && s.hitchMs == 100.0);
        // percentiles move when more frames are slow
        for (int i = 0; i < 10; ++i) r.Add(0.1);
        s = r.Summary(50.0);
        CHECK(s.frames == 111 && s.hitches == 11 && s.hitchMs == 50.0);
        CHECK(s.p99Ms > 100.0 - 1e-9 && s.p95Ms > s.p50Ms);
        // the other counters
        FrameRecording f = Sample();
        FrameSummary fs = f.Summary();
        CHECK(fs.pausedFrames == 4 && fs.steppedFrames == 1 && fs.syntheticFrames == 1 && fs.markedFrames == 4);
        CHECK(fs.hitches == 1);
        // text and JSON
        const std::string text = s.ToText();
        CHECK(text.find("111 frames") != std::string::npos && text.find("11 hitches") != std::string::npos && text.find("longest 250.00 ms") != std::string::npos);
        const std::string js = fs.ToJson();
        CHECK(js.front() == '{' && js.back() == '}' && js.find(' ') == std::string::npos);
        CHECK(js.find("\"frames\":127") != std::string::npos && js.find("\"hitches\":1") != std::string::npos && js.find("\"steppedFrames\":1") != std::string::npos);
        CHECK(js.find("\"p99Ms\":") != std::string::npos && js.find("\"avgFps\":") != std::string::npos);
        FrameRecording none;
        CHECK(none.Summary().ToText() == "no frames" && none.Summary().frames == 0 && none.Summary().avgFps == 0.0);
        CHECK(none.Summary().ToJson().find("\"frames\":0") != std::string::npos);
        // one frame
        FrameRecording single;
        single.Add(0.02);
        s = single.Summary();
        CHECK(s.frames == 1 && s.minMs == s.maxMs && s.p50Ms == s.p99Ms && s.hitches == 0 && std::fabs(s.avgFps - 50.0) < 1e-9);
        // a one-hour frame and a hitch threshold of zero (counting off)
        FrameRecording hour;
        hour.Add(5000.0);
        CHECK(hour.Summary().maxMs == 3600000.0 && hour.Summary(0.0).hitches == 0 && hour.Summary(std::numeric_limits<double>::quiet_NaN()).hitches == 0);
    }

    // ---- GameLoop: markers and flags go into the recording ---------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        FrameRecorder rec;
        loop.AttachRecorder(&rec);
        loop.SetFrameMarker(9);
        loop.BeginFrame(0.0166667);
        CHECK(loop.FrameMarker() == 9);
        loop.BeginFrame(0.0166667);                           // a marker is for one frame
        CHECK(loop.FrameMarker() == 0);
        Game::SetPaused(true);
        loop.BeginFrame(0.0166667);
        Game::SetPaused(false);
        loop.BeginFrame(0.75);                                // a hitch: recorded as it was measured, not as clamped
        const FrameRecording& r = rec.Recording();
        FrameSample s;
        CHECK(r.Frames() == 4);
        CHECK(r.At(0, s) && s.micros == 16667 && s.marker == 9 && s.flags == 0);
        CHECK(r.At(1, s) && s.marker == 0 && s.flags == 0);
        CHECK(r.At(2, s) && s.flags == kFramePaused);
        CHECK(r.At(3, s) && s.micros == 750000 && s.flags == kFrameHitch);
        // detach: nothing more is recorded
        loop.AttachRecorder(nullptr);
        loop.BeginFrame(0.016);
        CHECK(rec.Recording().Frames() == 4 && loop.Recorder() == nullptr);
        // a recorder that is stopped records nothing but the loop still runs
        loop.AttachRecorder(&rec);
        rec.Stop();
        CHECK(loop.BeginFrame(0.016) && rec.Recording().Frames() == 4);
        Fresh();
    }

    // ---- property: record a session, replay it, get identical Time values ------------------------------------------------------------------------------------
    {
        struct Snap {
            double elapsed, unscaled, fixed, alpha, ui, game, cinematic;
            float delta, unscaledDelta;
            int steps, marker;
            bool operator==(const Snap& o) const {
                return elapsed == o.elapsed && unscaled == o.unscaled && fixed == o.fixed && alpha == o.alpha && ui == o.ui && game == o.game &&
                       cinematic == o.cinematic && delta == o.delta && unscaledDelta == o.unscaledDelta && steps == o.steps && marker == o.marker;
            }
        };
        const int kFrames = 600;
        // the "game": a scripted set of things that happen at given frames, identical in both runs
        auto script = [](int i, GameLoop& loop, bool live) {
            if (i == 60) Time::HitStop(0.08f, 0.0f);
            if (i == 100) Game::SetPaused(true);
            if (live && (i == 110 || i == 130)) loop.RequestStep(1 + (i % 3), 0.01f);   // a replay gets its steps from the recording
            if (i == 160) Game::SetPaused(false);
            if (i == 200) Time::SetTimeScale(0.5f);
            if (i == 230) Time::RampTimeScale(1.0f, 0.3f);
            if (i == 300) Game::PushPause("menu");
            if (i == 320) Game::PopPause("menu");
            if (i == 400) loop.SetDebugTimeScale(2.0f);
            if (i == 450) loop.SetDebugTimeScale(1.0f);
            if (live && i % 25 == 0) loop.SetFrameMarker(i / 25 + 1);
        };
        auto snap = [](GameLoop& loop) {
            return Snap{Time::ElapsedTimeD(), Time::UnscaledTime(), Time::FixedTime(), (double)loop.Alpha(), Clock::Get("ui").Elapsed(),
                        Clock::Get("game").Elapsed(), Clock::Get("cinematic").Elapsed(), Time::DeltaTime(), Time::UnscaledDeltaTime(),
                        loop.FixedStepsThisFrame(), loop.FrameMarker()};
        };
        // a hitchy session: steady frames, jitter, spikes, a zero, NaN
        auto measured = [](int i) {
            const float r = Rand01();
            if (i % 97 == 0) return 0.4 + (double)Rand01();               // a stall
            if (i % 53 == 0) return 0.0;
            if (i % 61 == 0) return std::numeric_limits<double>::quiet_NaN();
            return r < 0.8f ? 0.0166 + (double)Rand01() * 0.002 : (double)Rand01() * 0.06;
        };
        for (int variant = 0; variant < 4; ++variant) {
            GameLoop::Options o;
            o.smoothDelta = (variant & 1) != 0;
            o.refreshHz = (variant & 2) ? 60.0f : 0.0f;
            o.maxDelta = 0.25f;
            g_s = 1357u + (std::uint32_t)variant;

            std::vector<Snap> live;
            FrameRecording recording;
            {
                Fresh();
                GameLoop loop(o);
                FrameRecorder rec;
                loop.AttachRecorder(&rec);
                for (int i = 0; i < kFrames; ++i) {
                    script(i, loop, true);
                    loop.BeginFrame(measured(i));
                    live.push_back(snap(loop));
                }
                recording = rec.Recording();
                CHECK(recording.Frames() == (std::uint64_t)kFrames);
            }
            // replay from memory; the host's own time is garbage and ignored
            {
                Fresh();
                GameLoop loop(o);
                FramePlayer player(recording);
                loop.AttachPlayer(&player);
                std::vector<Snap> again;
                bool lastFalse = false, onlyLastFalse = true;
                for (int i = 0; i < kFrames; ++i) {
                    script(i, loop, false);
                    const bool more = loop.BeginFrame(i % 2 ? 99.0 : -1.0);
                    again.push_back(snap(loop));
                    if (i == kFrames - 1) lastFalse = !more; else if (!more) onlyLastFalse = false;
                    if (!loop.Replaying()) onlyLastFalse = false;
                }
                CHECK(lastFalse && onlyLastFalse);
                CHECK(again.size() == live.size());
                bool same = again.size() == live.size();
                int firstDiff = -1;
                for (std::size_t i = 0; same && i < live.size(); ++i) if (!(live[i] == again[i])) { same = false; firstDiff = (int)i; }
                if (!same) std::fprintf(stderr, "  variant %d first differs at frame %d\n", variant, firstDiff);
                CHECK(same);
                // and nothing more is played after the end
                const std::uint64_t tf = Time::FrameCount();
                CHECK(!loop.BeginFrame(0.016) && Time::FrameCount() == tf && loop.PlaybackFinished());
            }
            // replay from the text form, through a file
            {
                const std::string path = "test_frame_record_replay.okayframes";
                CHECK(recording.Save(path));
                FrameRecording loaded;
                CHECK(FrameRecording::Load(path, loaded) && loaded == recording);
                std::error_code ec; std::filesystem::remove(path, ec);
                Fresh();
                GameLoop loop(o);
                FramePlayer player(loaded);
                loop.AttachPlayer(&player);
                std::vector<Snap> again;
                for (int i = 0; i < kFrames; ++i) { script(i, loop, false); loop.BeginFrame(0.0); again.push_back(snap(loop)); }
                bool same = again.size() == live.size();
                for (std::size_t i = 0; same && i < live.size(); ++i) if (!(live[i] == again[i])) same = false;
                CHECK(same);
            }
            // recording a replay reproduces the recording (the replay is itself recordable)
            {
                Fresh();
                GameLoop loop(o);
                FramePlayer player(recording);
                FrameRecorder second;
                loop.AttachPlayer(&player);
                loop.AttachRecorder(&second);
                for (int i = 0; i < kFrames; ++i) { script(i, loop, false); loop.BeginFrame(0.0); }
                CHECK(second.Recording() == recording);
            }
        }
        Fresh();
    }

    // ---- replay options ------------------------------------------------------------------------------------------------------------------------------------------------------
    {
        FrameRecording r;
        for (int i = 0; i < 5; ++i) r.Add(0.02);
        Fresh();
        GameLoop::Options o; o.stopAtPlaybackEnd = false;
        GameLoop loop(o);
        FramePlayer p(r);
        loop.AttachPlayer(&p);
        for (int i = 0; i < 5; ++i) CHECK(loop.BeginFrame(0.5) && loop.Replaying());
        CHECK(!loop.PlaybackFinished());
        CHECK(loop.BeginFrame(0.03) && !loop.Replaying());      // the host's time again, once the recording has run out
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.03, 1e-7);
        CHECK_NEAR(Time::UnscaledTime(), 0.13, 1e-6);
        // the default stops: an empty recording runs no frames at all
        Fresh();
        GameLoop stop;
        FramePlayer none;
        stop.AttachPlayer(&none);
        CHECK(!stop.BeginFrame(0.016) && Time::FrameCount() == 0 && stop.PlaybackFinished());
        stop.AttachPlayer(nullptr);
        CHECK(stop.BeginFrame(0.016) && Time::FrameCount() == 1);
        // a recorded hitch is clamped and counted again on replay, exactly as it was live
        Fresh();
        FrameRecording h;
        h.Add(0.016); h.Add(2.0, kFrameHitch); h.Add(0.016);
        GameLoop hl;
        FramePlayer hp(h);
        hl.AttachPlayer(&hp);
        hl.BeginFrame(0.0); hl.BeginFrame(0.0); hl.BeginFrame(0.0);
        CHECK(hl.HitchCount() == 1 && hl.ClampedFrames() == 1 && std::fabs(hl.WorstHitch() - 2.0f) < 1e-6f);
        // a recording is replayed in a loop with looping on
        Fresh();
        GameLoop lp;
        FramePlayer lplay(r);
        lplay.SetLooping(true);
        lp.AttachPlayer(&lplay);
        for (int i = 0; i < 23; ++i) CHECK(lp.BeginFrame(0.0));
        CHECK(lplay.Loops() == 4);
        Fresh();
    }

    Fresh();
    TEST_MAIN_RESULT();
}
