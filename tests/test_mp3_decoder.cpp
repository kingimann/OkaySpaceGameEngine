// Tests for the engine's own MP3 decoder (engine/src/Audio/Mp3Decoder.cpp).
//
// Accuracy: tests/data/audio/mp3 holds small MP3 files with reference PCM
// decoded by ffmpeg (the *.s16 files, 16-bit, scaled by 2^gainLog2 from
// manifest.txt). The decoder output must match the reference to at least
// kMinSnrDb; the measured SNR of every file is printed so a run documents the
// accuracy. The 16-bit references cap the measurable SNR at about 86 dB.
//   * LAME encodes (made with ffmpeg's libmp3lame from a sweep + noise + click
//     signal, 0.5 s): MPEG-1 at 44.1/48/32 kHz (CBR, joint stereo, VBR, ABR,
//     CRC, ID3v2, no Xing header), MPEG-2 at 22.05/16 kHz, MPEG-2.5 at 11.025
//     and 8 kHz, mono and stereo.
//   * synth_* streams: random but valid frame contents written by a throwaway
//     generator to reach what LAME never emits - intensity stereo (MPEG-1 and
//     MPEG-2), MS + intensity, mixed blocks, short blocks, scfsi, every
//     Huffman table and both count1 tables.
// Robustness: truncation at every length, bit flips, garbage, tags, resync.
// Everything here runs under ASan/UBSan in CI's sanitizer job.
#include "test_framework.hpp"
#include "okay/Audio/Mp3Decoder.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {

const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/audio/mp3/";
constexpr double kMinSnrDb = 60.0;

bool ReadAll(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Small deterministic generator (the standard <random> engines differ between libraries).
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ull + 1442695040888963407ull) {}
    std::uint32_t Next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return static_cast<std::uint32_t>((s * 2685821657736338717ull) >> 32);
    }
    std::uint32_t Below(std::uint32_t n) { return n ? Next() % n : 0; }
};

bool AllFinite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}

Mp3Audio Decode(const std::vector<unsigned char>& d, bool trim, bool* ok = nullptr) {
    Mp3Audio a;
    Mp3Options o;
    o.trimGapless = trim;
    const bool r = DecodeMp3(d.data(), d.size(), a, nullptr, &o);
    if (ok) *ok = r;
    return a;
}

struct Entry { std::string name; int rate; int channels; int gainLog2; int trim; };

std::vector<Entry> LoadManifest() {
    std::vector<Entry> list;
    std::ifstream f(kDir + "manifest.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Entry e;
        if (ss >> e.name >> e.rate >> e.channels >> e.gainLog2 >> e.trim) list.push_back(e);
    }
    return list;
}

} // namespace

int main() {
    RUN_SUITE("mp3_decoder");

    // ---- accuracy against the ffmpeg reference -------------------------------------------------
    const std::vector<Entry> manifest = LoadManifest();
    CHECK(manifest.size() >= 20);
    for (const Entry& e : manifest) {
        std::vector<unsigned char> mp3, refBytes;
        const bool haveFiles = ReadAll(kDir + e.name + ".mp3", mp3) && ReadAll(kDir + e.name + ".s16", refBytes);
        CHECK(haveFiles);
        if (!haveFiles) continue;
        bool ok = false;
        const Mp3Audio a = Decode(mp3, e.trim != 0, &ok);
        CHECK(ok);
        CHECK(a.sampleRate == e.rate);
        CHECK(a.channels == e.channels);
        CHECK(AllFinite(a.samples));
        const std::size_t refN = refBytes.size() / 2;
        CHECK(a.samples.size() == refN);
        const std::size_t n = std::min(a.samples.size(), refN);
        const double gain = std::ldexp(1.0, e.gainLog2);
        double sig = 0.0, err = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const std::int16_t r16 = static_cast<std::int16_t>(refBytes[2 * i] | (refBytes[2 * i + 1] << 8));
            const double ref = r16 / 32768.0;
            const double d = a.samples[i] * gain - ref;
            sig += ref * ref;
            err += d * d;
        }
        const double snr = err > 0.0 ? 10.0 * std::log10(sig / err) : 200.0;
        std::cout << "  " << e.name << ": " << a.channels << " ch " << a.sampleRate << " Hz, " << a.FrameCount()
                  << " frames, SNR vs ffmpeg " << snr << " dB\n";
        CHECK(sig > 0.0);
        CHECK(snr >= kMinSnrDb);
    }

    // ---- gapless handling and header frames ----------------------------------------------------
    std::vector<unsigned char> lame, noXing;
    CHECK(ReadAll(kDir + "stereo_44k_cbr128.mp3", lame));
    CHECK(ReadAll(kDir + "noxing_44k.mp3", noXing));
    {
        bool ok = false;
        const Mp3Audio trimmed = Decode(lame, true, &ok);
        const Mp3Audio raw = Decode(lame, false, &ok);
        CHECK(trimmed.hasLameTag && trimmed.encoderDelay == 576);
        CHECK(trimmed.FrameCount() == 22050);                  // 0.5 s exactly
        CHECK(raw.FrameCount() == 1152u * static_cast<std::size_t>(raw.framesDecoded));
        CHECK(raw.FrameCount() >= 22050u + 529u + 576u);       // delay + decoder delay come before the audio
        // Trimming only drops samples at the ends.
        bool same = true;
        for (std::size_t i = 0; i < trimmed.samples.size() && same; ++i)
            same = trimmed.samples[i] == raw.samples[(576 + 529) * 2 + i];
        CHECK(same);
        const Mp3Audio plain = Decode(noXing, true, &ok);
        CHECK(ok && !plain.hasLameTag);
        CHECK(plain.FrameCount() == 1152u * static_cast<std::size_t>(plain.framesDecoded));   // nothing to trim
        // The default options trim as well.
        Mp3Audio def;
        CHECK(DecodeMp3(lame.data(), lame.size(), def));
        CHECK(def.samples == trimmed.samples);
    }

    // ---- tags, garbage around the stream, resynchronisation ------------------------------------
    {
        bool ok = false;
        const Mp3Audio ref = Decode(noXing, false, &ok);
        CHECK(ok && ref.framesDecoded > 10);

        // ID3v2 with false sync words inside and a footer-less size of 3000 bytes.
        std::vector<unsigned char> tagged = {'I', 'D', '3', 4, 0, 0, 0, 0, 0x17, 0x38};   // 3000 = 0x17*128 + 0x38
        for (int i = 0; i < 3000; ++i) tagged.push_back(i % 5 == 0 ? 0xFF : (i % 5 == 1 ? 0xFB : (i % 5 == 2 ? 0x90 : 0x00)));
        tagged.insert(tagged.end(), noXing.begin(), noXing.end());
        const Mp3Audio t = Decode(tagged, false, &ok);
        CHECK(ok && t.samples == ref.samples);

        // Two stacked ID3v2 tags, trailing ID3v1 and junk.
        std::vector<unsigned char> two = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 4, 1, 2, 3, 4};
        two.insert(two.end(), tagged.begin(), tagged.end());
        two.push_back('T'); two.push_back('A'); two.push_back('G');
        Rng rng(5);
        for (int i = 0; i < 125; ++i) two.push_back(static_cast<unsigned char>(rng.Next()));
        const Mp3Audio t2 = Decode(two, false, &ok);
        CHECK(ok && t2.samples == ref.samples);

        // Random leading junk without sync-like bytes.
        std::vector<unsigned char> junk;
        for (int i = 0; i < 1500; ++i) junk.push_back(static_cast<unsigned char>(rng.Below(0xF0)));
        junk.insert(junk.end(), noXing.begin(), noXing.end());
        const Mp3Audio j = Decode(junk, false, &ok);
        CHECK(ok && j.samples == ref.samples);

        // A damaged stretch in the middle: the frames before it are untouched, the damaged
        // frames are bridged and the stream picks up again afterwards.
        std::vector<unsigned char> hole = noXing;
        const std::size_t at = hole.size() / 2;
        for (std::size_t i = at; i < at + 900; ++i) hole[i] = static_cast<unsigned char>(rng.Next());
        const Mp3Audio h = Decode(hole, false, &ok);
        CHECK(ok && AllFinite(h.samples));
        CHECK(h.FrameCount() + 3u * 1152u >= ref.FrameCount());   // lost at most a few frames
        const std::size_t bytesPerFrame = noXing.size() / static_cast<std::size_t>(ref.framesDecoded);
        const std::size_t framesBefore = at / bytesPerFrame - 2;
        bool prefix = true;
        for (std::size_t i = 0; i < framesBefore * 1152 * 2 && prefix; ++i)
            prefix = i < h.samples.size() && h.samples[i] == ref.samples[i];
        CHECK(framesBefore > 5 && prefix);

        // Streams whose later frames change channel count / sample rate keep the first format.
        std::vector<unsigned char> mono, mixed = noXing;
        CHECK(ReadAll(kDir + "mono_44k_cbr64.mp3", mono));
        mixed.insert(mixed.end(), mono.begin(), mono.end());
        const Mp3Audio m = Decode(mixed, false, &ok);
        CHECK(ok && m.channels == 2 && m.sampleRate == 44100 && m.samples == ref.samples);
    }

    // ---- unsupported and degenerate input ------------------------------------------------------
    {
        Mp3Audio a;
        std::string err;
        CHECK(!DecodeMp3(nullptr, 0, a, &err) && !err.empty());
        for (std::size_t n = 0; n <= 12; ++n) {
            std::vector<unsigned char> tiny(n, 0xFF);
            CHECK(!DecodeMp3(tiny.data(), tiny.size(), a, nullptr));
            CHECK(a.samples.empty());
        }
        // Layer II and free-format headers carry no Layer III audio.
        std::vector<unsigned char> l2, ff;
        for (int i = 0; i < 40; ++i) { l2.insert(l2.end(), {0xFF, 0xFD, 0x90, 0x00}); l2.resize(l2.size() + 413, 0); }
        for (int i = 0; i < 40; ++i) { ff.insert(ff.end(), {0xFF, 0xFB, 0x00, 0x00}); ff.resize(ff.size() + 413, 0); }
        CHECK(!DecodeMp3(l2.data(), l2.size(), a, nullptr));
        CHECK(!DecodeMp3(ff.data(), ff.size(), a, nullptr));
        // Valid headers over zero side info decode to silence of the right length.
        std::vector<unsigned char> silent;
        for (int i = 0; i < 10; ++i) { silent.insert(silent.end(), {0xFF, 0xFB, 0x90, 0x00}); silent.resize(silent.size() + 413, 0); }
        CHECK(DecodeMp3(silent.data(), silent.size(), a, nullptr));
        CHECK(a.FrameCount() == 10u * 1152u && a.channels == 2 && a.sampleRate == 44100);
        bool allZero = true;
        for (float x : a.samples) allZero = allZero && x == 0.0f;
        CHECK(allZero);
    }

    // ---- truncation at every length ------------------------------------------------------------
    {
        std::vector<unsigned char> shortFile, longFile;
        CHECK(ReadAll(kDir + "mono_11k_mpeg25.mp3", shortFile));
        CHECK(ReadAll(kDir + "stereo_22k_mpeg2.mp3", longFile));
        bool ok = false;
        const Mp3Audio fullShort = Decode(shortFile, false, &ok);
        const Mp3Audio fullLong = Decode(longFile, false, &ok);
        auto sweep = [&](const std::vector<unsigned char>& src, const Mp3Audio& full, std::size_t step) {
            int decodedOk = 0;
            std::size_t lastFrames = 0;
            for (std::size_t len = 0; len <= src.size(); len += step) {
                std::vector<unsigned char> cut(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(len));
                const Mp3Audio a = Decode(cut, false, &ok);
                CHECK(AllFinite(a.samples));
                if (!ok) { CHECK(a.samples.empty()); continue; }
                ++decodedOk;
                // The decoder is causal: a prefix of the file decodes to a prefix of the full result.
                bool pre = a.samples.size() <= full.samples.size();
                for (std::size_t i = 0; pre && i < a.samples.size(); ++i) pre = a.samples[i] == full.samples[i];
                CHECK(pre);
                CHECK(a.FrameCount() >= lastFrames);
                lastFrames = a.FrameCount();
            }
            CHECK(decodedOk > 10);
        };
        sweep(shortFile, fullShort, 1);
        sweep(longFile, fullLong, 11);
    }

    // ---- bit flips, burst damage and pure noise ------------------------------------------------
    {
        const char* names[] = {"stereo_44k_cbr128", "joint_48k_cbr192", "mono_8k_mpeg25", "synth_mpeg1_ms_intensity_mixed",
                               "synth_mpeg2_intensity", "synth_mpeg25_8k_mixed_stereo"};
        Rng rng(1234);
        int decoded = 0, total = 0;
        for (const char* name : names) {
            std::vector<unsigned char> src;
            CHECK(ReadAll(kDir + name + std::string(".mp3"), src));
            for (int iter = 0; iter < 60; ++iter) {
                std::vector<unsigned char> d = src;
                const int flips = 1 + static_cast<int>(rng.Below(24));
                for (int f = 0; f < flips; ++f) d[rng.Below(static_cast<std::uint32_t>(d.size()))] ^= static_cast<unsigned char>(1u << rng.Below(8));
                if (iter % 5 == 0) {      // a burst of random bytes
                    const std::size_t at2 = rng.Below(static_cast<std::uint32_t>(d.size()));
                    for (std::size_t i = at2; i < std::min(d.size(), at2 + 1 + rng.Below(400)); ++i) d[i] = static_cast<unsigned char>(rng.Next());
                }
                if (iter % 7 == 0) d.resize(rng.Below(static_cast<std::uint32_t>(d.size())) + 1);
                bool ok = false;
                const Mp3Audio a = Decode(d, true, &ok);
                ++total;
                CHECK(AllFinite(a.samples));
                CHECK(a.samples.size() % (ok ? static_cast<std::size_t>(a.channels) : 1u) == 0);
                if (ok) ++decoded;
            }
        }
        CHECK(decoded > total / 2);       // most damaged files still yield audio
        // Damage aimed at the 4-byte headers and side info of the first frames.
        std::vector<unsigned char> src;
        CHECK(ReadAll(kDir + "stereo_22k_mpeg2.mp3", src));
        for (std::size_t pos = 0; pos < 1200 && pos < src.size(); pos += 2) {
            for (int bit = 1; bit < 8; bit += 4) {
                std::vector<unsigned char> d = src;
                d[pos] ^= static_cast<unsigned char>(1u << bit);
                bool ok = false;
                const Mp3Audio a = Decode(d, true, &ok);
                CHECK(AllFinite(a.samples));
            }
        }
        // Noise, constant fills, and valid headers followed by noise.
        for (int iter = 0; iter < 40; ++iter) {
            std::vector<unsigned char> d(20000);
            for (auto& b : d) b = static_cast<unsigned char>(rng.Next());
            if (iter % 2) for (std::size_t i = 0; i + 4 <= d.size(); i += 417) { d[i] = 0xFF; d[i + 1] = 0xFB; d[i + 2] = 0x90; d[i + 3] = static_cast<unsigned char>(rng.Next() & 0xC0); }
            if (iter % 8 == 3) std::fill(d.begin(), d.end(), static_cast<unsigned char>(iter % 2 ? 0xFF : 0x00));
            bool ok = false;
            const Mp3Audio a = Decode(d, true, &ok);
            CHECK(AllFinite(a.samples));
        }
    }

    TEST_MAIN_RESULT();
}
