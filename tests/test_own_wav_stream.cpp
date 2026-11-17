// Streaming with our own WAV decoder (okay::wav::WavStream, docs/own_wav_decoder.md, "Streaming"). The rule it checks:
// a streamed decode equals the whole-file decode (okay::wav::DecodeWav) bit for bit, whatever the piece sizes and
// seek positions, on every format the decoder reads, and on every truncation of the small files (a truncated file is
// refused, or plays exactly what DecodeWav gives, and is never read past its end). Also: a source that gives fewer
// bytes than it promised (a file cut while it plays) stops the stream safely; an MS ADPCM block with a bad predictor
// ends the stream where DecodeWav stops.
//
// The core part needs only WavDecoder.cpp and WavPlayback.cpp, so it can be built on its own with sanitizers:
//   g++ -std=c++17 -g -O1 -fsanitize=address,undefined -Iengine/include -Itests tests/test_own_wav_stream.cpp
//       engine/src/Audio/WavDecoder.cpp engine/src/Audio/WavPlayback.cpp      (one command line)
// With OKAY_STREAM_TEST_ENGINE (the ctest build, linked with the engine) it also checks AudioStream against
// AudioClip::LoadWAV, looping included, and the OKAY_OWN_WAV=0 fallback.
// The test files are written here by our own helpers; the ADPCM payloads are pseudo-random bytes behind valid headers.
#include "test_framework.hpp"
#include "okay/Audio/AudioByteSource.hpp"
#include "okay/Audio/WavDecoder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

#ifdef OKAY_STREAM_TEST_ENGINE
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <fstream>
#endif

using namespace okay;
using Bytes = std::vector<unsigned char>;

namespace {

std::mt19937 g_rng(20261116u);
int Rand(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }

void P16(Bytes& b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
void P32(Bytes& b, std::uint32_t v) { P16(b, v & 0xFFFF); P16(b, v >> 16); }
void Tag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }

Bytes Fmt(unsigned tag, unsigned ch, std::uint32_t rate, unsigned align, unsigned bits, const Bytes& extra = {}) {
    Bytes f; P16(f, tag); P16(f, ch); P32(f, rate); P32(f, rate * align); P16(f, align); P16(f, bits);
    f.insert(f.end(), extra.begin(), extra.end());
    return f;
}
// order: 0 fmt then data, 1 data then fmt, 2 fmt, junk (odd size), data, junk after
Bytes Wav(const Bytes& fmt, const Bytes& data, int order = 0, std::uint32_t dataSizeField = 0xFFFFFFFEu) {
    Bytes b; Tag(b, "RIFF"); P32(b, 0); Tag(b, "WAVE");
    auto fmtChunk = [&] { Tag(b, "fmt "); P32(b, std::uint32_t(fmt.size())); b.insert(b.end(), fmt.begin(), fmt.end()); if (fmt.size() & 1) b.push_back(0); };
    auto junk = [&] { Tag(b, "LIST"); P32(b, 5); for (int i = 0; i < 5; ++i) b.push_back(std::uint8_t('a' + i)); b.push_back(0); };
    auto dataChunk = [&] { Tag(b, "data"); P32(b, dataSizeField == 0xFFFFFFFEu ? std::uint32_t(data.size()) : dataSizeField); b.insert(b.end(), data.begin(), data.end()); if (data.size() & 1 && dataSizeField == 0xFFFFFFFEu) b.push_back(0); };
    if (order == 1) { dataChunk(); fmtChunk(); }
    else if (order == 2) { fmtChunk(); junk(); dataChunk(); junk(); }
    else { fmtChunk(); dataChunk(); }
    return b;
}
Bytes RandomBytes(std::size_t n) { Bytes d(n); for (auto& x : d) x = std::uint8_t(Rand(0, 255)); return d; }

// Bytes in memory that record any read outside [0, Size()), and can pretend to be cut: Size() keeps saying `size`
// while reads stop at `cutAt` (a file truncated while it streams).
class TestSource : public AudioByteSource {
public:
    TestSource(const Bytes& b, std::size_t len, std::size_t cutAt = std::numeric_limits<std::size_t>::max())
        : b_(b), len_(len), cut_(std::min(cutAt, len)) {}
    std::uint64_t Size() const override { return len_; }
    std::size_t Read(std::uint64_t off, void* dst, std::size_t n) override {
        if (off + n > len_) ++outside;
        if (off >= cut_ || !dst) return 0;
        n = std::size_t(std::min<std::uint64_t>(n, cut_ - off));
        std::memcpy(dst, b_.data() + off, n);
        bytesRead += n;
        return n;
    }
    int outside = 0;
    std::uint64_t bytesRead = 0;
private:
    const Bytes& b_;
    std::size_t len_, cut_;
};

bool SameBits(const float* a, const float* b, std::size_t n) { return n == 0 || std::memcmp(a, b, n * sizeof(float)) == 0; }

int g_streamChecks = 0;   // whole comparisons made (for the summary line)

// Sequential read in random piece sizes from frame 0: the result must be exactly `want`.
bool ReadAllRandom(wav::WavStream& s, const std::vector<float>& want, int maxPiece) {
    const std::size_t ch = std::size_t(s.Channels());
    std::vector<float> got;
    std::vector<float> buf;
    s.Seek(0);
    for (int guard = 0; guard < 10000000; ++guard) {
        const std::size_t n = std::size_t(Rand(1, maxPiece));
        buf.assign(n * ch, -7.0f);
        const std::size_t r = s.Read(buf.data(), n);
        if (r > n) return false;
        got.insert(got.end(), buf.begin(), buf.begin() + std::ptrdiff_t(r * ch));
        if (r == 0) break;
    }
    ++g_streamChecks;
    return got.size() == want.size() && SameBits(got.data(), want.data(), got.size());
}

// Random seeks, each followed by a read of a random size; each result compared with `want` at that position.
bool RandomSeeks(wav::WavStream& s, const std::vector<float>& want, int count, int maxPiece) {
    const std::size_t ch = std::size_t(s.Channels());
    const std::size_t frames = want.size() / ch;
    std::vector<float> buf;
    for (int i = 0; i < count; ++i) {
        const std::size_t at = std::size_t(Rand(0, int(frames + 2)));   // past the end too
        const std::size_t n = std::size_t(Rand(1, maxPiece));
        if (!s.Seek(at)) return false;
        const std::size_t start = std::min(at, frames);
        if (s.Position() != start) return false;
        buf.assign(n * ch, -7.0f);
        const std::size_t r = s.Read(buf.data(), n);
        const std::size_t expect = std::min(n, frames - start);
        if (r != expect) return false;
        if (!SameBits(buf.data(), want.data() + start * ch, r * ch)) return false;
        // and a second read straight after continues where the first stopped
        const std::size_t n2 = std::size_t(Rand(1, maxPiece));
        buf.assign(n2 * ch, -7.0f);
        const std::size_t r2 = s.Read(buf.data(), n2);
        const std::size_t expect2 = std::min(n2, frames - start - r);
        if (r2 != expect2 || !SameBits(buf.data(), want.data() + (start + r) * ch, r2 * ch)) return false;
        ++g_streamChecks;
    }
    return true;
}

// A loop as AudioStream plays one: read to the end, seek to 0, read on; 2.5 times through, in random pieces.
bool Loops(wav::WavStream& s, const std::vector<float>& want, int maxPiece) {
    const std::size_t ch = std::size_t(s.Channels());
    const std::size_t frames = want.size() / ch;
    if (frames == 0) return true;
    const std::size_t total = frames * 5 / 2;
    std::vector<float> buf;
    std::size_t done = 0;
    s.Seek(0);
    while (done < total) {
        const std::size_t n = std::min<std::size_t>(std::size_t(Rand(1, maxPiece)), total - done);
        buf.assign(n * ch, -7.0f);
        std::size_t r = s.Read(buf.data(), n);
        if (r == 0) { s.Seek(0); continue; }
        for (std::size_t i = 0; i < r; ++i)
            if (!SameBits(buf.data() + i * ch, want.data() + ((done + i) % frames) * ch, ch)) return false;
        done += r;
    }
    ++g_streamChecks;
    return true;
}

// The whole check for one file: open must agree with DecodeWav; samples equal under random pieces, seeks and loops.
// `deep`: also the seeks and loops (the truncation sweep only does the sequential reads, to keep it quick).
void CheckFile(const Bytes& file, std::size_t len, const char* name, bool deep) {
    wav::WavAudio whole;
    std::string e1, e2;
    const bool okWhole = wav::DecodeWav(file.data(), len, whole, &e1);
    auto src = std::make_shared<TestSource>(file, len);
    wav::WavStream s;
    const bool okStream = s.Open(src, &e2);
    if (okWhole) {
        CHECK(okStream);
        if (!okStream) { std::cerr << "    " << name << " len " << len << ": stream refused: " << e2 << "\n"; return; }
    } else {
        // DecodeWav failed. Either the header was refused (the stream refuses it too, with the same message), or
        // decoding stopped inside the data (a bad MS ADPCM block): the stream then gives what DecodeWav decoded
        // before it stopped, and ends there.
        if (!okStream) { CHECK(e1 == e2); CHECK(src->outside == 0); return; }
        CHECK(e1 == "MS ADPCM predictor index out of range");
    }
    CHECK(s.SampleRate() == whole.sampleRate && s.Channels() == whole.channels);
    CHECK(s.GetEncoding() == whole.encoding && s.BitsPerSample() == whole.bitsPerSample);
    if (okWhole) CHECK(s.TotalFrames() == whole.FrameCount());
    // The header warnings are DecodeWav's first warnings, in order (DecodeWav adds IMA clamp warnings after them).
    CHECK(s.Warnings().size() <= whole.warnings.size());
    for (std::size_t i = 0; i < s.Warnings().size() && i < whole.warnings.size(); ++i) CHECK(s.Warnings()[i] == whole.warnings[i]);
    const bool seq = ReadAllRandom(s, whole.samples, 700);
    CHECK(seq);
    if (!seq) std::cerr << "    " << name << " len " << len << ": sequential read differs\n";
    CHECK(s.Failed() == !okWhole);
    if (deep) {
        const bool big = ReadAllRandom(s, whole.samples, 20000);
        CHECK(big);
        const bool one = ReadAllRandom(s, whole.samples, 1);
        CHECK(one);
        if (okWhole) {
            const bool sk = RandomSeeks(s, whole.samples, 300, 900);
            CHECK(sk);
            if (!sk) std::cerr << "    " << name << ": seek read differs\n";
            const bool lp = Loops(s, whole.samples, 1500);
            CHECK(lp);
            if (!lp) std::cerr << "    " << name << ": loop read differs\n";
        }
    }
    CHECK(src->outside == 0);
}

struct Case { std::string name; Bytes file; bool small; };

std::vector<Case> MakeCases() {
    std::vector<Case> c;
    auto pcm = [&](const char* n, unsigned bits, unsigned ch, std::size_t frames, unsigned alignExtra, int order, bool small) {
        const unsigned sb = (bits + 7) / 8, align = sb * ch + alignExtra;
        c.push_back({n, Wav(Fmt(1, ch, 22050, align, bits), RandomBytes(frames * align + (order == 0 ? 0 : 1)), order), small});
    };
    pcm("pcm8_mono", 8, 1, 3001, 0, 0, false);
    pcm("pcm8_stereo_small", 8, 2, 37, 0, 2, true);
    pcm("pcm16_stereo", 16, 2, 9000, 0, 0, false);
    pcm("pcm16_mono_small", 16, 1, 41, 0, 1, true);
    pcm("pcm16_padded_align", 16, 2, 2000, 2, 0, false);    // block align larger than a frame
    pcm("pcm12_in_16", 12, 1, 1000, 0, 0, false);
    pcm("pcm24_stereo", 24, 2, 7001, 0, 0, false);
    pcm("pcm24_mono_small", 24, 1, 29, 0, 2, true);
    pcm("pcm32_stereo", 32, 2, 5000, 0, 0, false);
    pcm("pcm32_3ch_small", 32, 3, 17, 0, 0, true);
    {   // float32 and float64 with NaN, infinity and huge values among ordinary ones
        Bytes d;
        for (int i = 0; i < 4000; ++i) {
            float v = float(Rand(-30000, 30000)) / 20000.0f;
            if (i % 97 == 0) v = std::numeric_limits<float>::quiet_NaN();
            if (i % 101 == 0) v = std::numeric_limits<float>::infinity();
            std::uint32_t u; std::memcpy(&u, &v, 4); P32(d, u);
        }
        c.push_back({"f32_stereo", Wav(Fmt(3, 2, 48000, 8, 32), d), false});
        Bytes s(d.begin(), d.begin() + 8 * 13);
        c.push_back({"f32_stereo_small", Wav(Fmt(3, 2, 48000, 8, 32), s, 2), true});
        Bytes d64;
        for (int i = 0; i < 3000; ++i) {
            double v = double(Rand(-30000, 30000)) / 20000.0;
            if (i % 89 == 0) v = 1e300;
            if (i % 83 == 0) v = std::numeric_limits<double>::quiet_NaN();
            std::uint64_t u; std::memcpy(&u, &v, 8); P32(d64, std::uint32_t(u)); P32(d64, std::uint32_t(u >> 32));
        }
        c.push_back({"f64_mono", Wav(Fmt(3, 1, 44100, 8, 64), d64), false});
    }
    c.push_back({"alaw_stereo", Wav(Fmt(6, 2, 8000, 2, 8), RandomBytes(2 * 6000)), false});
    c.push_back({"alaw_mono_small", Wav(Fmt(6, 1, 8000, 1, 8), RandomBytes(33)), true});
    c.push_back({"mulaw_stereo", Wav(Fmt(7, 2, 8000, 2, 8), RandomBytes(2 * 6000), 2), false});
    c.push_back({"mulaw_mono_small", Wav(Fmt(7, 1, 8000, 1, 8), RandomBytes(31), 1), true});
    {   // extensible: 24-bit PCM, 6 channels; and float32 in extensible
        auto ext = [](unsigned tag, unsigned ch, unsigned bits, unsigned align) {
            Bytes x; P16(x, 22); P16(x, bits); P32(x, 0);
            P16(x, tag); const unsigned char tail[14] = {0, 0, 0, 0, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
            x.insert(x.end(), tail, tail + 14);
            return Fmt(0xFFFE, ch, 48000, align, bits, x);
        };
        c.push_back({"ext_pcm24_6ch", Wav(ext(1, 6, 24, 18), RandomBytes(18 * 1500)), false});
        c.push_back({"ext_pcm16_6ch_small", Wav(ext(1, 6, 16, 12), RandomBytes(12 * 5)), true});
    }
    // MS ADPCM: valid block headers (predictor 0..6, delta 16..), random nibbles; a shorter last block.
    auto msBlock = [](unsigned ch, unsigned align, int nco, int badAt, int blk) {
        Bytes b;
        for (unsigned k = 0; k < ch; ++k) b.push_back(std::uint8_t(blk == badAt ? nco + 3 : Rand(0, nco - 1)));
        for (unsigned k = 0; k < ch; ++k) P16(b, unsigned(Rand(16, 3000)));
        for (unsigned k = 0; k < 2 * ch; ++k) P16(b, unsigned(Rand(0, 65535)));
        while (b.size() < align) b.push_back(std::uint8_t(Rand(0, 255)));
        return b;
    };
    auto ms = [&](const char* n, unsigned ch, unsigned align, int blocks, unsigned lastLen, bool customCoef, int badAt, bool small) {
        Bytes ext; const unsigned spb = (align - 7 * ch) * 2 / ch + 2;
        P16(ext, 32); P16(ext, spb);
        int nco = 7;
        if (customCoef) { nco = 9; P16(ext, 9); for (int i = 0; i < 9; ++i) { P16(ext, unsigned(Rand(0, 600)) & 0xFFFF); P16(ext, unsigned(Rand(-300, 300)) & 0xFFFF); } }
        else { P16(ext, 7); const int k[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};
               for (auto& p : k) { P16(ext, unsigned(p[0]) & 0xFFFF); P16(ext, unsigned(p[1]) & 0xFFFF); } }
        ext[0] = std::uint8_t(ext.size() - 2);
        Bytes d;
        for (int b = 0; b < blocks; ++b) { Bytes blk = msBlock(ch, align, nco, badAt, b); d.insert(d.end(), blk.begin(), blk.end()); }
        if (lastLen) { Bytes blk = msBlock(ch, align, nco, badAt, blocks); blk.resize(lastLen); d.insert(d.end(), blk.begin(), blk.end()); }
        c.push_back({n, Wav(Fmt(2, ch, 22050, align, 4, ext), d), small});
    };
    ms("msadpcm_mono", 1, 256, 23, 100, false, -1, false);
    ms("msadpcm_stereo", 2, 512, 17, 77, true, -1, false);
    ms("msadpcm_stereo_small", 2, 40, 2, 21, false, -1, true);
    ms("msadpcm_mono_small", 1, 24, 3, 9, true, -1, true);
    ms("msadpcm_bad_block", 2, 128, 9, 0, false, 5, false);           // block 5 has a predictor index past the table
    ms("msadpcm_bad_block_small", 1, 20, 4, 0, false, 2, true);
    // IMA ADPCM: valid headers with step indexes up to 100 (over 88 is clamped with a warning), random nibbles.
    auto ima = [&](const char* n, unsigned ch, unsigned align, int blocks, unsigned lastLen, bool small) {
        Bytes ext; P16(ext, 2); P16(ext, 1 + 8 * ((align - 4 * ch) / (4 * ch)));
        Bytes d;
        for (int b = 0; b <= blocks; ++b) {
            Bytes blk;
            for (unsigned k = 0; k < ch; ++k) { P16(blk, unsigned(Rand(0, 65535))); blk.push_back(std::uint8_t(Rand(0, 100))); blk.push_back(0); }
            while (blk.size() < align) blk.push_back(std::uint8_t(Rand(0, 255)));
            if (b == blocks) { if (!lastLen) break; blk.resize(lastLen); }
            d.insert(d.end(), blk.begin(), blk.end());
        }
        c.push_back({n, Wav(Fmt(0x11, ch, 22050, align, 4, ext), d), small});
    };
    ima("ima_mono", 1, 256, 25, 70, false);
    ima("ima_stereo", 2, 1024, 9, 515, false);
    ima("ima_3ch", 3, 12 * 9, 30, 12 * 3 + 5, false);
    ima("ima_stereo_small", 2, 24, 3, 17, true);
    ima("ima_mono_small", 1, 12, 4, 5, true);
    // Odd layouts: data chunk size 0xFFFFFFFF (a streaming writer), and longer than the file.
    c.push_back({"pcm16_ffffffff", Wav(Fmt(1, 2, 44100, 4, 16), RandomBytes(4 * 500 + 3), 0, 0xFFFFFFFFu), false});
    c.push_back({"pcm16_ffffffff_small", Wav(Fmt(1, 1, 44100, 2, 16), RandomBytes(2 * 9 + 1), 0, 0xFFFFFFFFu), true});
    c.push_back({"empty_data", Wav(Fmt(1, 2, 44100, 4, 16), Bytes{}), true});
    return c;
}

void TestFormats(const std::vector<Case>& cases) {
    RUN_SUITE("streamed equals whole-file on every format (random pieces, seeks, loops)");
    for (const Case& k : cases) CheckFile(k.file, k.file.size(), k.name.c_str(), true);
}

void TestTruncations(const std::vector<Case>& cases) {
    RUN_SUITE("every truncation of the small files");
    int n = 0;
    for (const Case& k : cases) {
        if (!k.small) continue;
        for (std::size_t len = 0; len <= k.file.size(); ++len, ++n) CheckFile(k.file, len, k.name.c_str(), len % 7 == 0);
    }
    std::cout << "  " << n << " truncated files\n";
}

void TestCutWhilePlaying(const std::vector<Case>& cases) {
    RUN_SUITE("a source cut while it plays stops the stream safely");
    for (const Case& k : cases) {
        wav::WavAudio whole;
        if (!wav::DecodeWav(k.file.data(), k.file.size(), whole) || whole.samples.empty()) continue;
        for (int t = 0; t < 12; ++t) {
            const std::size_t cut = std::size_t(Rand(0, int(k.file.size())));
            auto src = std::make_shared<TestSource>(k.file, k.file.size(), cut);
            wav::WavStream s;
            std::string e;
            if (!s.Open(src, &e)) { CHECK(!e.empty()); continue; }   // cut inside the header
            const std::size_t ch = std::size_t(s.Channels());
            std::vector<float> got, buf;
            for (;;) {
                const std::size_t n = std::size_t(Rand(1, 600));
                buf.assign(n * ch, 0.0f);
                const std::size_t r = s.Read(buf.data(), n);
                if (r == 0) break;
                got.insert(got.end(), buf.begin(), buf.begin() + std::ptrdiff_t(r * ch));
            }
            // What it gave is the start of the real decode, and it stopped before the end (or reached it: a cut
            // after the data chunk's last byte changes nothing).
            CHECK(got.size() <= whole.samples.size());
            CHECK(SameBits(got.data(), whole.samples.data(), std::min(got.size(), whole.samples.size())));
            CHECK(got.size() == whole.samples.size() || s.Failed());
            CHECK(src->outside == 0);
            // Reading again after the stop gives nothing more and does not crash; a seek back works for the part before.
            CHECK(s.Read(buf.data(), 1) == 0);
            if (!got.empty()) { s.Seek(0); CHECK(s.Read(buf.data(), 1) == 1 && SameBits(buf.data(), whole.samples.data(), ch)); }
        }
    }
}

void TestMisc() {
    RUN_SUITE("open errors, memory, read window");
    wav::WavStream s;
    std::string e;
    CHECK(!s.Open(nullptr, &e) && !e.empty());
    float f[4];
    CHECK(s.Read(f, 1) == 0 && !s.IsOpen() && s.TotalFrames() == 0);
    Bytes notWav = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '};
    CHECK(!s.Open(std::make_shared<TestSource>(notWav, notWav.size()), &e) && e == "not a RIFF/WAVE file");
    // A long 16-bit file: the stream reads it in windows of at most 16 KiB; memory stays small.
    Bytes big = Wav(Fmt(1, 2, 44100, 4, 16), RandomBytes(4 * 200000));
    auto src = std::make_shared<TestSource>(big, big.size());
    CHECK(s.Open(src, &e));
    std::vector<float> buf(2 * 2048);
    std::size_t total = 0;
    while (std::size_t r = s.Read(buf.data(), 2048)) total += r;
    CHECK(total == 200000);
    CHECK(s.MemoryBytes() < 200 * 1024);
    CHECK(src->bytesRead < big.size() + 4096);   // the samples are read once, plus the header
    // Seek past the end is clamped; reading there gives nothing.
    CHECK(s.Seek(9999999) && s.Position() == 200000 && s.Read(buf.data(), 5) == 0);
    // The budgets: the stream has none by default (StreamLimits); a given Limits applies as in DecodeWav.
    wav::Limits tight; tight.maxOutputSamples = 1000;
    CHECK(!s.Open(src, &e, &tight) && e == "decoded size over the budget");
    CHECK(s.Open(src, &e) && s.TotalFrames() == 200000);
    CHECK(wav::StreamLimits().maxInputBytes > wav::Limits().maxInputBytes);
    CHECK(std::string(wav::ReaderLogLine(true)).find("our own reader") != std::string::npos);
    CHECK(std::string(wav::ReaderLogLine(false)).find("OLD reader") != std::string::npos);
}

#ifdef OKAY_STREAM_TEST_ENGINE
void SetOwn(const char* v) {
#ifdef _WIN32
    _putenv_s("OKAY_OWN_WAV", v ? v : "");
#else
    if (v) setenv("OKAY_OWN_WAV", v, 1); else unsetenv("OKAY_OWN_WAV");
#endif
}

std::vector<float> StreamAll(AudioStream& st, std::size_t frames) {
    std::vector<float> o(frames * std::size_t(st.Channels()));
    const std::size_t r = st.Read(o.data(), frames);
    o.resize(r * std::size_t(st.Channels()));
    return o;
}

void TestAudioStream(const std::vector<Case>& cases) {
    RUN_SUITE("AudioStream (engine) on our decoder equals AudioClip::LoadWAV, loops included");
    const char* prev = std::getenv("OKAY_OWN_WAV");
    const std::string saved = prev ? prev : "";
    SetOwn("1");
    const std::string path = "own_wav_stream_tmp.wav";
    int compared = 0;
    for (const Case& k : cases) {
        { std::ofstream o(path, std::ios::binary); o.write(reinterpret_cast<const char*>(k.file.data()), std::streamsize(k.file.size())); }
        AudioClip clip;
        std::string e1, e2;
        const bool okClip = clip.LoadWAV(path, &e1);
        auto st = AudioStream::OpenFile(path, &e2);
        if (!okClip) { if (k.name.find("bad_block") == std::string::npos) CHECK(!st); continue; }
        CHECK(st != nullptr);
        if (!st) { std::cerr << "    " << k.name << ": " << e2 << "\n"; continue; }
        CHECK(st->SampleRate() == clip.sampleRate && st->Channels() == clip.channels);
        CHECK(st->TotalFrames() == std::int64_t(clip.FrameCount()));
        const std::vector<float> got = StreamAll(*st, clip.FrameCount() + 10);
        CHECK(got.size() == clip.samples.size() && SameBits(got.data(), clip.samples.data(), got.size()));
        // Looping through AudioStream itself: 2.5 times the length, wrapping at the end.
        if (clip.FrameCount() > 0) {
            auto lp = AudioStream::OpenFile(path);
            lp->loop = true;
            const std::size_t frames = clip.FrameCount(), ch = std::size_t(clip.channels), want = frames * 5 / 2;
            const std::vector<float> l = StreamAll(*lp, want);
            CHECK(l.size() == want * ch);
            bool same = l.size() == want * ch;
            for (std::size_t i = 0; same && i < want; ++i) same = SameBits(l.data() + i * ch, clip.samples.data() + (i % frames) * ch, ch);
            CHECK(same);
            // Seek inside the file (the middle of an ADPCM block, for the ADPCM files) and read on.
            const std::int64_t at = std::int64_t(frames / 3 + 1);
            lp->loop = false;
            CHECK(lp->Seek(at));
            const std::vector<float> r = StreamAll(*lp, frames);
            CHECK(r.size() == (frames - std::size_t(at)) * ch && SameBits(r.data(), clip.samples.data() + std::size_t(at) * ch, r.size()));
        }
        ++compared;
    }
    std::cout << "  " << compared << " files compared through AudioStream\n";
    // An A-law file: streamed with ours it is the G.711 decode (the old streamed reader read it as 8-bit PCM: noise).
    {
        Bytes alaw = Wav(Fmt(6, 1, 8000, 1, 8), Bytes{0xD5, 0x55, 0x2A});   // +8, -8, -32256 / 32768 (hand values, own_wav)
        { std::ofstream o(path, std::ios::binary); o.write(reinterpret_cast<const char*>(alaw.data()), std::streamsize(alaw.size())); }
        auto st = AudioStream::OpenFile(path);
        CHECK(st != nullptr);
        if (st) {
            const std::vector<float> v = StreamAll(*st, 3);
            CHECK(v.size() == 3 && v[0] == 8.0f / 32768.0f && v[1] == -8.0f / 32768.0f && v[2] == -32256.0f / 32768.0f);
        }
        // OKAY_OWN_WAV=0: the old streamed reader, kept for this release (it opens the file; its values are the old ones).
        SetOwn("0");
        auto old = AudioStream::OpenFile(path);
        CHECK(old != nullptr);
        if (old) { const std::vector<float> v = StreamAll(*old, 3); CHECK(v.size() == 3 && v[0] == (0xD5 - 128) / 128.0f); }
        // and a plain 16-bit file is the same on both paths
        Bytes pcm = Wav(Fmt(1, 2, 44100, 4, 16), RandomBytes(4 * 3000));
        { std::ofstream o(path, std::ios::binary); o.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(pcm.size())); }
        auto a = AudioStream::OpenFile(path);
        SetOwn("1");
        auto b = AudioStream::OpenFile(path);
        CHECK(a && b);
        if (a && b) { const auto va = StreamAll(*a, 3000), vb = StreamAll(*b, 3000); CHECK(va.size() == 6000 && va.size() == vb.size() && SameBits(va.data(), vb.data(), va.size())); }
    }
    std::remove(path.c_str());
    SetOwn(prev ? saved.c_str() : nullptr);
}

// The shipped sample (docs/try_it.md): examples/own_wav/stream_test.okayscene has a Music Player that streams
// alaw_tone.wav; streamed with our decoder it is the 440 Hz sine it was made from (scripts/gen_own_wav_alaw_tone.py).
void TestShippedSample() {
#ifdef OKAY_SOURCE_DIR
    RUN_SUITE("shipped sample: examples/own_wav/stream_test.okayscene streams alaw_tone.wav");
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/own_wav/";
    Scene sc("t");
    std::string err;
    CHECK(SceneSerializer::LoadFromFile(sc, dir + "stream_test.okayscene", &err));
    GameObject* g = sc.Find("Music");
    MusicPlayer* m = g ? g->GetComponent<MusicPlayer>() : nullptr;
    CHECK(m != nullptr);
    if (m) CHECK(m->stream && m->playOnStart && m->tracks.size() == 1 && m->tracks[0] == "alaw_tone.wav");
    const char* prev = std::getenv("OKAY_OWN_WAV");
    const std::string saved = prev ? prev : "";
    SetOwn(nullptr);   // the build default: ours
    auto st = AudioStream::OpenFile(dir + "alaw_tone.wav");
    CHECK(st != nullptr);
    if (st) {
        CHECK(st->SampleRate() == 22050 && st->Channels() == 1 && st->TotalFrames() == 44100);
        const std::vector<float> v = StreamAll(*st, 44100);
        double worst = 0;
        for (std::size_t i = 0; i < v.size(); ++i)
            worst = std::max(worst, std::fabs(double(v[i]) - 0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * double(i) / 22050.0)));
        CHECK(v.size() == 44100 && worst < 0.01);   // G.711 A-law steps near 0.5 are 1/128
        std::cout << "  alaw_tone.wav streamed: largest difference from the sine " << worst << "\n";
    }
    SetOwn(prev ? saved.c_str() : nullptr);
#endif
}
#endif

} // namespace

int main() {
    const std::vector<Case> cases = MakeCases();
    TestFormats(cases);
    TestTruncations(cases);
    TestCutWhilePlaying(cases);
    TestMisc();
#ifdef OKAY_STREAM_TEST_ENGINE
    TestAudioStream(cases);
    TestShippedSample();
#endif
    std::cout << "  " << g_streamChecks << " whole stream comparisons\n";
    TEST_MAIN_RESULT();
}
