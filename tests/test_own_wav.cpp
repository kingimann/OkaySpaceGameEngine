// Own WAV decoder (okay/Audio/WavDecoder.hpp). Every expected number below was worked out by hand from the format
// descriptions (G.711, the MS ADPCM and IMA ADPCM block layouts), not by running the decoder. The files are built
// in memory by the helpers here. Also: hostile input (every truncation, byte mutations, budgets), the opt-in
// switch in AudioClip::LoadWAV, and a differential run of IMA ADPCM against Python's audioop at test time only
// (python3 optional: SKIP, unless OKAY_REQUIRE_DIFFERENTIAL=1).
#include "test_framework.hpp"
#include "okay/Audio/WavDecoder.hpp"
#include "okay/Audio/AudioClip.hpp"
#ifndef OWN_WAV_STANDALONE
#include "okay/Audio/AudioStream.hpp"
#endif
#include <algorithm>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace okay;
using Bytes = std::vector<unsigned char>;

static void P16(Bytes& b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void P32(Bytes& b, std::uint32_t v) { P16(b, v & 0xFFFF); P16(b, v >> 16); }
static void Tag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }

// fmt body (16 bytes) plus `extra` bytes
static Bytes Fmt(unsigned tag, unsigned ch, std::uint32_t rate, unsigned align, unsigned bits, const Bytes& extra = {}) {
    Bytes f; P16(f, tag); P16(f, ch); P32(f, rate); P32(f, rate * align); P16(f, align); P16(f, bits);
    if (!extra.empty() || tag == 0xFFFE || tag == 2 || tag == 0x11) f.insert(f.end(), extra.begin(), extra.end());
    return f;
}
// One chunk: tag, size field (the body size unless given), body, pad byte when the body is odd.
static void Chunk(Bytes& b, const char* tag, const Bytes& body, std::uint32_t sizeField = 0xFFFFFFFEu) {
    Tag(b, tag); P32(b, sizeField == 0xFFFFFFFEu ? std::uint32_t(body.size()) : sizeField);
    b.insert(b.end(), body.begin(), body.end()); if (body.size() & 1) b.push_back(0);
}
static Bytes Wav(const Bytes& fmt, const Bytes& data, bool dataFirst = false, std::uint32_t dataSizeField = 0xFFFFFFFEu) {
    Bytes b; Tag(b, "RIFF"); P32(b, 0); Tag(b, "WAVE");
    if (dataFirst) { Chunk(b, "data", data, dataSizeField); Chunk(b, "fmt ", fmt); } else { Chunk(b, "fmt ", fmt); Chunk(b, "data", data, dataSizeField); }
    return b;
}
static bool Dec(const Bytes& w, wav::WavAudio& a, std::string* e = nullptr, const wav::Limits* l = nullptr) {
    return wav::DecodeWav(w.data(), w.size(), a, e, l);
}
static std::vector<int> S16s(const wav::WavAudio& a) { auto v = wav::ToS16(a); return std::vector<int>(v.begin(), v.end()); }

static std::uint32_t g_rng = 12345;
static std::uint32_t Rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

// Builders for longer files (the streaming tests): random bytes, ADPCM blocks with sane headers, fmt extras.
static Bytes Rand(std::size_t n) { Bytes d; for (std::size_t i = 0; i < n; ++i) d.push_back(Rnd() & 255); return d; }
static Bytes ImaBlocks(unsigned ch, unsigned align, unsigned blocks, unsigned lastLen) {
    Bytes d;
    for (unsigned b = 0; b <= blocks; ++b) {
        const unsigned len = b < blocks ? align : lastLen;
        if (len == 0) break;
        Bytes blk = Rand(len);
        for (unsigned c = 0; c < ch && 4 * c + 3 < len; ++c) { blk[4 * c + 2] = (unsigned char)(Rnd() % 89); blk[4 * c + 3] = 0; }
        d.insert(d.end(), blk.begin(), blk.end());
    }
    return d;
}
static Bytes MsBlocks(unsigned ch, unsigned align, unsigned blocks, unsigned lastLen) {
    Bytes d;
    for (unsigned b = 0; b <= blocks; ++b) {
        const unsigned len = b < blocks ? align : lastLen;
        if (len == 0) break;
        Bytes blk = Rand(len);
        for (unsigned c = 0; c < ch && c < len; ++c) blk[c] = (unsigned char)(Rnd() % 7);
        for (unsigned c = 0; c < ch && ch + 2 * c + 1 < len; ++c) { blk[ch + 2 * c] = (unsigned char)(16 + Rnd() % 200); blk[ch + 2 * c + 1] = 0; }
        d.insert(d.end(), blk.begin(), blk.end());
    }
    return d;
}
static Bytes MsCoefExtra(unsigned spb) {
    Bytes x; P16(x, 32); P16(x, spb); P16(x, 7);
    const int c[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};
    for (auto& p : c) { P16(x, unsigned(p[0]) & 0xFFFF); P16(x, unsigned(p[1]) & 0xFFFF); }
    return x;
}
static Bytes ExtExtra(unsigned validBits, unsigned subTag) {
    Bytes x; P16(x, 22); P16(x, validBits); P32(x, 0);
    const unsigned char guid[16] = {(unsigned char)subTag, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
    x.insert(x.end(), guid, guid + 16);
    return x;
}
// RIFF/WAVE with odd-sized unknown chunks (and their pad bytes) before fmt and data, in either order.
static Bytes OddChunks(const Bytes& fmt, const Bytes& data, bool dataFirst) {
    Bytes b; Tag(b, "RIFF"); P32(b, 0); Tag(b, "WAVE");
    Tag(b, "junk"); P32(b, 3); b.push_back(1); b.push_back(2); b.push_back(3); b.push_back(0);
    Tag(b, "LIST"); P32(b, 5); for (int i = 0; i < 5; ++i) b.push_back((unsigned char)('a' + i)); b.push_back(0);
    const Bytes odd = {9};
    if (dataFirst) { Chunk(b, "data", data); Chunk(b, "odd ", odd); Chunk(b, "fmt ", fmt); }
    else { Chunk(b, "fmt ", fmt); Chunk(b, "odd ", odd); Chunk(b, "data", data); }
    return b;
}


static void TestPcm() {
    wav::WavAudio a; std::string e;
    { // 16 bit: 0, 16384, -32768
        Bytes d; P16(d, 0); P16(d, 16384); P16(d, 0x8000);
        CHECK(Dec(Wav(Fmt(1, 1, 8000, 2, 16), d), a, &e));
        CHECK(a.sampleRate == 8000 && a.channels == 1 && a.samples.size() == 3 && a.encoding == wav::Encoding::Pcm);
        CHECK(a.samples[0] == 0.0f && a.samples[1] == 0.5f && a.samples[2] == -1.0f);
    }
    { // 8 bit unsigned: 128 -> 0, 255 -> 127/128, 0 -> -1
        Bytes d = {128, 255, 0};
        CHECK(Dec(Wav(Fmt(1, 1, 8000, 1, 8), d), a));
        CHECK(a.samples.size() == 3 && a.samples[0] == 0.0f && a.samples[1] == 127.0f / 128.0f && a.samples[2] == -1.0f);
    }
    { // 24 bit: 00 00 40 = 0x400000 = 0.5, 00 00 80 = -1
        Bytes d = {0, 0, 0x40, 0, 0, 0x80};
        CHECK(Dec(Wav(Fmt(1, 1, 8000, 3, 24), d), a));
        CHECK(a.samples.size() == 2 && a.samples[0] == 0.5f && a.samples[1] == -1.0f);
    }
    { // 32 bit: 0x40000000 = 0.5, 0xC0000000 = -0.5
        Bytes d; P32(d, 0x40000000u); P32(d, 0xC0000000u);
        CHECK(Dec(Wav(Fmt(1, 1, 8000, 4, 32), d), a));
        CHECK(a.samples.size() == 2 && a.samples[0] == 0.5f && a.samples[1] == -0.5f);
    }
    { // 12 valid bits in a 16 bit container is left-justified: same scale as 16 bit
        Bytes d; P16(d, 0x4000);
        CHECK(Dec(Wav(Fmt(1, 1, 8000, 2, 12), d), a));
        CHECK(a.samples.size() == 1 && a.samples[0] == 0.5f);
    }
    { // stereo order: frame 0 = (1000, -1000)
        Bytes d; P16(d, 1000); P16(d, unsigned(-1000) & 0xFFFF); P16(d, 0); P16(d, 0);
        CHECK(Dec(Wav(Fmt(1, 2, 44100, 4, 16), d), a));
        CHECK(a.channels == 2 && a.FrameCount() == 2 && a.samples[0] == 1000.0f / 32768.0f && a.samples[1] == -1000.0f / 32768.0f);
    }
    { // float 32 (0.25, NaN, +inf) and float 64 (-0.5)
        Bytes d; const float f[3] = {0.25f, std::nanf(""), 1.0f / 0.0f};
        for (float x : f) { std::uint32_t u; std::memcpy(&u, &x, 4); P32(d, u); }
        CHECK(Dec(Wav(Fmt(3, 1, 8000, 4, 32), d), a));
        CHECK(a.samples.size() == 3 && a.samples[0] == 0.25f && a.samples[1] == 0.0f && a.samples[2] == 0.0f && a.encoding == wav::Encoding::Float);
        Bytes d2; const double x = -0.5; std::uint64_t u; std::memcpy(&u, &x, 8); P32(d2, std::uint32_t(u)); P32(d2, std::uint32_t(u >> 32));
        CHECK(Dec(Wav(Fmt(3, 1, 8000, 8, 64), d2), a));
        CHECK(a.samples.size() == 1 && a.samples[0] == -0.5f);
    }
    { // WAVE_FORMAT_EXTENSIBLE, PCM sub-format, stereo 16 bit
        Bytes x; P16(x, 22); P16(x, 16); P32(x, 3);
        const unsigned char guid[16] = {1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
        x.insert(x.end(), guid, guid + 16);
        Bytes d; P16(d, 100); P16(d, 200);
        CHECK(Dec(Wav(Fmt(0xFFFE, 2, 8000, 4, 16, x), d), a, &e));
        CHECK(a.channels == 2 && a.samples.size() == 2 && a.samples[0] == 100.0f / 32768.0f && a.samples[1] == 200.0f / 32768.0f);
        // same with the float sub-format (tag 3) at 32 bit
        Bytes x2 = x; x2[8] = 3; x2[2 + 0] = 32;
        Bytes d2; { float v = 0.5f; std::uint32_t u; std::memcpy(&u, &v, 4); P32(d2, u); P32(d2, u); }
        CHECK(Dec(Wav(Fmt(0xFFFE, 2, 8000, 8, 32, x2), d2), a, &e));
        CHECK(a.encoding == wav::Encoding::Float && a.samples.size() == 2 && a.samples[0] == 0.5f);
        // a GUID that is not the standard base is refused
        Bytes x3 = x; x3[8 + 6] = 0x11;
        CHECK(!Dec(Wav(Fmt(0xFFFE, 2, 8000, 4, 16, x3), d), a, &e));
    }
    { // fmt after data, an unknown chunk before both, odd-sized chunk padding
        Bytes d; P16(d, 16384);
        Bytes w = Wav(Fmt(1, 1, 8000, 2, 16), d, true);
        CHECK(Dec(w, a) && a.samples.size() == 1 && a.samples[0] == 0.5f);
        Bytes junk; Tag(junk, "RIFF"); P32(junk, 0); Tag(junk, "WAVE"); Tag(junk, "LIST"); P32(junk, 3); junk.push_back(1); junk.push_back(2); junk.push_back(3); junk.push_back(0);
        junk.insert(junk.end(), w.begin() + 12, w.end());
        CHECK(Dec(junk, a) && a.samples.size() == 1 && a.samples[0] == 0.5f);
    }
    { // data chunk size 0xFFFFFFFF (streaming writers) is cut at the file end; half a frame is dropped; both warn
        Bytes d; P16(d, 1); P16(d, 2); d.push_back(7); d.push_back(8);   // stereo 16 bit: one frame and half of the next
        CHECK(Dec(Wav(Fmt(1, 2, 8000, 4, 16), d, false, 0xFFFFFFFFu), a));
        CHECK(a.samples.size() == 2 && a.warnings.size() == 2);
    }
    { // an empty data chunk is a valid empty clip
        CHECK(Dec(Wav(Fmt(1, 1, 8000, 2, 16), Bytes()), a) && a.samples.empty());
    }
    { // ToS16: rounds and clamps
        wav::WavAudio t; t.channels = 1; t.sampleRate = 1; t.samples = {0.5f, -1.0f, 2.0f, -2.0f, 1.0f / 65536.0f, 1.5f / 32768.0f};
        auto v = wav::ToS16(t);
        CHECK(v[0] == 16384 && v[1] == -32768 && v[2] == 32767 && v[3] == -32768 && v[4] == 0 && v[5] == 2);
    }
}

static void TestG711() {
    wav::WavAudio a;
    // A-law (G.711): 0xD5 = +8, 0x55 = -8, 0xAA = +32256 (top segment, mantissa 15), 0x2A = -32256, 0xA8 = +... see below
    {
        Bytes d = {0xD5, 0x55, 0xAA, 0x2A};
        CHECK(Dec(Wav(Fmt(6, 1, 8000, 1, 8), d), a));
        auto v = S16s(a);
        CHECK(v.size() == 4 && v[0] == 8 && v[1] == -8 && v[2] == 32256 && v[3] == -32256);
        // segment 1, mantissa 0: code 0xD5 with the segment bits set: (0xD5^0x55)=0x80 -> 0x90 ^ 0x55 = 0xC5 -> +0x108 = 264
        Bytes d2 = {0xC5};
        CHECK(Dec(Wav(Fmt(6, 1, 8000, 1, 8), d2), a));
        CHECK(S16s(a)[0] == 264);
    }
    // mu-law: 0xFF = 0, 0x7F = -0, 0x80 = +32124, 0x00 = -32124, 0xEF: u=0x10: exp1 man0 -> (0x84<<1)-0x84 = 132, positive
    {
        Bytes d = {0xFF, 0x7F, 0x80, 0x00, 0xEF};
        CHECK(Dec(Wav(Fmt(7, 1, 8000, 1, 8), d), a));
        auto v = S16s(a);
        CHECK(v.size() == 5 && v[0] == 0 && v[1] == 0 && v[2] == 32124 && v[3] == -32124 && v[4] == 132);
        CHECK(a.encoding == wav::Encoding::MuLaw);
    }
}

// IMA ADPCM, one block, mono, blockAlign 8: header pred=0 idx=0, then 0x07 0x8F 0x00 0x00. Worked by hand:
// 7 (step 7: 0+1+3+7=11) -> 11, idx 8; 0 (step 16: diff 2) -> 13, idx 7; F (step 14: 1+3+7+14=25, minus) -> -12, idx 15;
// 8 (step 31: diff 3, minus) -> -15, idx 14; then four 0 nibbles with steps 28, 25, 23, 21: +3, +3, +2, +2 (the want list).
static void TestImaAdpcm() {
    wav::WavAudio a; std::string e;
    Bytes blk; P16(blk, 0); blk.push_back(0); blk.push_back(0); blk.push_back(0x07); blk.push_back(0x8F); blk.push_back(0); blk.push_back(0);
    Bytes x; P16(x, 2); P16(x, 505);   // cbSize, samplesPerBlock = 1 + 8
    CHECK(Dec(Wav(Fmt(0x11, 1, 8000, 8, 4, x), blk), a, &e));
    CHECK(a.encoding == wav::Encoding::ImaAdpcm && a.channels == 1);
    const int want[9] = {0, 11, 13, -12, -15, -12, -9, -7, -5};
    CHECK(a.samples.size() == 9);
    auto v = S16s(a);
    for (int i = 0; i < 9 && i < int(v.size()); ++i) CHECK(v[i] == want[i]);
    CHECK(a.warnings.size() == 1);   // 505 disagrees with 9: the warning, block align wins

    // stereo, blockAlign 16: L pred 100 idx 0 bytes 0; R pred -100 idx 0 bytes 44 00 00 00
    Bytes s; P16(s, 100); s.push_back(0); s.push_back(0); P16(s, unsigned(-100) & 0xFFFF); s.push_back(0); s.push_back(0);
    for (int i = 0; i < 4; ++i) s.push_back(0);
    s.push_back(0x44); s.push_back(0); s.push_back(0); s.push_back(0);
    CHECK(Dec(Wav(Fmt(0x11, 2, 8000, 16, 4), s), a, &e));
    CHECK(a.channels == 2 && a.FrameCount() == 9);
    v = S16s(a);
    const int wantR[9] = {-100, -93, -83, -82, -81, -80, -79, -79, -79};   // hand-worked; step index +2 for nibble 4
    bool ok = v.size() == 18;
    for (int i = 0; ok && i < 9; ++i) ok = v[2 * i] == 100 && v[2 * i + 1] == wantR[i];
    CHECK(ok);

    // a step index above 88 is clamped with a warning; a block align that is not a multiple of 4*channels is refused
    Bytes bad = blk; bad[2] = 200;
    CHECK(Dec(Wav(Fmt(0x11, 1, 8000, 8, 4), bad), a) && !a.warnings.empty());
    bad[2] = 89; CHECK(Dec(Wav(Fmt(0x11, 1, 8000, 8, 4), bad), a) && !a.warnings.empty());   // 89 is the first bad index
    CHECK(!Dec(Wav(Fmt(0x11, 1, 8000, 9, 4), blk), a, &e));
    CHECK(!Dec(Wav(Fmt(0x11, 1, 8000, 8, 8), blk), a, &e));   // not 4 bit
    // a trailing partial block: header only gives one frame
    Bytes more = blk; for (int i = 0; i < 4; ++i) more.push_back(0); P16(more, 0x1234 & 0xFFFF);
    // (rest = 6 bytes: >= 4 header bytes, 0 whole groups) -> 1 + 9 frames; pred 0x1234 = 4660
    CHECK(Dec(Wav(Fmt(0x11, 1, 8000, 8, 4), more), a) && a.samples.size() == 10 && S16s(a)[9] == 0);
}

// Microsoft ADPCM with the standard coefficients. Mono, blockAlign 9: predictor 0 (256, 0), delta 16, s1 = 100, s2 = 50,
// then 0x1F 0x70. Output: s2, s1 first. 1: 100 + 16 = 116 (delta 14 -> 16); F: 116 - 16 = 100 (delta 16);
// 7: 100 + 7*16 = 212 (delta 614*16>>8 = 38); 0: 212 (delta 34).
static void TestMsAdpcm() {
    wav::WavAudio a; std::string e;
    Bytes m; m.push_back(0); P16(m, 16); P16(m, 100); P16(m, 50); m.push_back(0x1F); m.push_back(0x70);
    CHECK(Dec(Wav(Fmt(2, 1, 8000, 9, 4, MsCoefExtra(6)), m), a, &e));
    auto v = S16s(a);
    const int want[6] = {50, 100, 116, 100, 212, 212};
    CHECK(v.size() == 6);
    for (int i = 0; i < 6 && i < int(v.size()); ++i) CHECK(v[i] == want[i]);
    CHECK(a.warnings.empty());
    // the same file without a coefficient table in fmt (cbSize 0) uses the standard seven
    CHECK(Dec(Wav(Fmt(2, 1, 8000, 9, 4, Bytes{0, 0}), m), a, &e) && S16s(a).size() == 6 && S16s(a)[5] == 212);
    // stereo, blockAlign 15: L pred 0 (256,0) delta 16 s1 100 s2 0; R pred 1 (512,-256) delta 32 s1 200 s2 100; byte 0x2E
    Bytes s; s.push_back(0); s.push_back(1); P16(s, 16); P16(s, 32); P16(s, 100); P16(s, 200); P16(s, 0); P16(s, 100); s.push_back(0x2E);
    CHECK(Dec(Wav(Fmt(2, 2, 8000, 15, 4, MsCoefExtra(3)), s), a, &e));
    v = S16s(a);
    const int ws[6] = {0, 100, 100, 200, 132, 236};   // L: 100+2*16 = 132; R: (200*512-100*256)>>8 = 300, minus 2*32 = 236
    CHECK(v.size() == 6);
    for (int i = 0; i < 6 && i < int(v.size()); ++i) CHECK(v[i] == ws[i]);
    // clamping: s1 = 32767, nibble 7, delta 5000 -> 32767 (no wrap)
    Bytes c; c.push_back(0); P16(c, 5000); P16(c, 32767); P16(c, 32767); c.push_back(0x70);
    CHECK(Dec(Wav(Fmt(2, 1, 8000, 8, 4, MsCoefExtra(4)), c), a, &e) && S16s(a).size() == 4 && S16s(a)[2] == 32767);
    // the standard coefficient table, every pair, mono, delta 16, s1 = 1000, s2 = -2000, one nibble 0: the third sample is the prediction.
    // Worked by hand: (1000*c1 - 2000*c2) >> 8 = 1000, 4000, 0, 250, 937 (937.5 floors), 3421 (3421.875), 3343 (3343.75)
    {
        const int want[7] = {1000, 4000, 0, 250, 937, 3421, 3343};
        for (int p = 0; p < 7; ++p) {
            Bytes q; q.push_back(unsigned(p)); P16(q, 16); P16(q, 1000); P16(q, unsigned(-2000) & 0xFFFF); q.push_back(0x00);
            CHECK(Dec(Wav(Fmt(2, 1, 8000, 8, 4, Bytes{0, 0}), q), a, &e) && S16s(a).size() == 4 && S16s(a)[0] == -2000 && S16s(a)[1] == 1000 && S16s(a)[2] == want[p]);
        }
        // a negative prediction floors (arithmetic shift): pair 4, s1 = -1000, s2 = 0: -240000 >> 8 = -938 (truncation would give -937)
        Bytes q; q.push_back(4); P16(q, 16); P16(q, unsigned(-1000) & 0xFFFF); P16(q, 0); q.push_back(0x00);
        CHECK(Dec(Wav(Fmt(2, 1, 8000, 8, 4, Bytes{0, 0}), q), a, &e) && S16s(a)[2] == -938);
        // the delta adaptation: delta 2000, nibbles 8 then 1: -8*2000 = -16000, delta (768*2000)>>8 = 6000, then -16000 + 6000 = -10000
        Bytes r; r.push_back(0); P16(r, 2000); P16(r, 0); P16(r, 0); r.push_back(0x81);
        CHECK(Dec(Wav(Fmt(2, 1, 8000, 8, 4, Bytes{0, 0}), r), a, &e) && S16s(a).size() == 4 && S16s(a)[2] == -16000 && S16s(a)[3] == -10000);
        // every adaptation entry through its own nibble: delta 4096, nibble n (as signed) from s1 = s2 = 0, then nibble 0 shows delta
        // via a second nibble of 1 (pred stays, +delta): expected delta = (table[n] * 4096) >> 8 = table[n] * 16
        const int tab[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
        for (int n = 0; n < 16; ++n) {
            Bytes t; t.push_back(0); P16(t, 4096); P16(t, 0); P16(t, 0); t.push_back(unsigned(n << 4) | 1u);
            const int first = (n >= 8 ? n - 16 : n) * 4096;
            const int clampedFirst = first < -32768 ? -32768 : first > 32767 ? 32767 : first;
            int second = clampedFirst + tab[n] * 16; second = second > 32767 ? 32767 : second;
            CHECK(Dec(Wav(Fmt(2, 1, 8000, 8, 4, Bytes{0, 0}), t), a, &e) && S16s(a).size() == 4 && S16s(a)[2] == clampedFirst && S16s(a)[3] == second);
        }
    }
    // predictor index outside the table, three channels, wrong bit depth
    Bytes b2 = m; b2[0] = 7;
    CHECK(!Dec(Wav(Fmt(2, 1, 8000, 9, 4, MsCoefExtra(6)), b2), a, &e));
    CHECK(!Dec(Wav(Fmt(2, 3, 8000, 30, 4, MsCoefExtra(6)), m), a, &e));
    CHECK(!Dec(Wav(Fmt(2, 1, 8000, 9, 8, MsCoefExtra(6)), m), a, &e));
}

static void TestRefusals() {
    wav::WavAudio a; std::string e;
    Bytes d; P16(d, 1); P16(d, 2);
    CHECK(!Dec(Wav(Fmt(0x55, 1, 8000, 2, 16), d), a, &e) && !e.empty());          // MP3-in-WAV tag: not ours
    CHECK(!Dec(Wav(Fmt(1, 0, 8000, 2, 16), d), a));                              // zero channels
    CHECK(!Dec(Wav(Fmt(1, 1, 0, 2, 16), d), a));                                 // zero rate
    CHECK(!Dec(Wav(Fmt(1, 1, 8000000, 2, 16), d), a));                           // rate over the budget
    CHECK(!Dec(Wav(Fmt(1, 33, 8000, 66, 16), d), a));                            // too many channels
    CHECK(!Dec(Wav(Fmt(1, 1, 8000, 2, 0), d), a));                               // bit depth 0
    CHECK(!Dec(Wav(Fmt(3, 1, 8000, 2, 16), d), a));                              // float must be 32 or 64
    CHECK(!Dec(Wav(Fmt(6, 1, 8000, 2, 16), d), a));                              // A-law must be 8 bit
    CHECK(!Dec(Bytes(), a) && !Dec(Bytes{'R', 'I', 'F', 'F'}, a));
    Bytes noFmt; Tag(noFmt, "RIFF"); P32(noFmt, 0); Tag(noFmt, "WAVE"); Tag(noFmt, "data"); P32(noFmt, 2); P16(noFmt, 0);
    CHECK(!Dec(noFmt, a, &e) && e.find("fmt") != std::string::npos);
    Bytes noData = Wav(Fmt(1, 1, 8000, 2, 16), d); noData.resize(12 + 8 + 16);
    CHECK(!Dec(noData, a, &e) && e.find("data") != std::string::npos);
    // size budgets: input and output
    wav::Limits tiny; tiny.maxOutputSamples = 1;
    CHECK(!Dec(Wav(Fmt(1, 1, 8000, 2, 16), d), a, &e, &tiny) && e.find("budget") != std::string::npos);
    wav::Limits small; small.maxInputBytes = 20;
    CHECK(!Dec(Wav(Fmt(1, 1, 8000, 2, 16), d), a, &e, &small));
    // a header that claims a huge ADPCM file cannot allocate more than the file holds: output is bounded by the data bytes
    Bytes big(1 << 16, 0xFF);
    wav::Limits lim; lim.maxOutputSamples = 1000;
    CHECK(!Dec(Wav(Fmt(0x11, 1, 8000, 8, 4), big), a, &e, &lim));
    // chunk-count budget: 5000 empty chunks
    Bytes many; Tag(many, "RIFF"); P32(many, 0); Tag(many, "WAVE");
    for (int i = 0; i < 5000; ++i) { Tag(many, "junk"); P32(many, 0); }
    CHECK(!Dec(many, a, &e) && e.find("chunks") != std::string::npos);
    // a chunk whose size runs past 2^32 - 8 must not wrap the position
    Bytes wrap = Wav(Fmt(1, 1, 8000, 2, 16), d); Tag(wrap, "zzzz"); P32(wrap, 0xFFFFFFFFu);
    CHECK(Dec(wrap, a));
}


static void TestHostile() {
    // every truncation of every encoding's valid file, then random byte mutations: no crash (the sanitizer job runs this under
    // ASan and UBSan), no output larger than the budget, and success only with sane fields.
    std::vector<Bytes> files;
    { Bytes d; for (int i = 0; i < 40; ++i) P16(d, Rnd() & 0xFFFF); files.push_back(Wav(Fmt(1, 2, 8000, 4, 16), d)); }
    { Bytes d; for (int i = 0; i < 30; ++i) d.push_back(Rnd() & 255); files.push_back(Wav(Fmt(6, 1, 8000, 1, 8), d)); files.push_back(Wav(Fmt(7, 2, 8000, 2, 8), d)); }
    { Bytes d; for (int i = 0; i < 24; ++i) P32(d, Rnd()); files.push_back(Wav(Fmt(3, 1, 8000, 4, 32), d)); }
    { Bytes d; for (int i = 0; i < 2 * 8 + 16; ++i) d.push_back(Rnd() & 255); P16(d, 0);
      Bytes x; P16(x, 2); P16(x, 9); files.push_back(Wav(Fmt(0x11, 1, 8000, 8, 4, x), d)); files.push_back(Wav(Fmt(0x11, 2, 8000, 16, 4), d)); }
    { const Bytes x = MsCoefExtra(6);
      Bytes d; for (int i = 0; i < 27; ++i) d.push_back(i % 9 == 0 ? 3 : (Rnd() & 255));
      files.push_back(Wav(Fmt(2, 1, 8000, 9, 4, x), d)); }
    wav::Limits lim; lim.maxOutputSamples = 1 << 20;
    long runs = 0, oks = 0;
    for (const Bytes& f : files) {
        wav::WavAudio a; std::string e;
        for (std::size_t n = 0; n <= f.size(); ++n) {
            Bytes t(f.begin(), f.begin() + long(n));
            const bool ok = wav::DecodeWav(t.data(), t.size(), a, &e, &lim);
            ++runs; if (ok) ++oks;
            CHECK(!ok || (a.channels >= 1 && a.sampleRate >= 1 && a.samples.size() <= lim.maxOutputSamples));
            if (!ok) CHECK(!e.empty());
        }
        for (int it = 0; it < 3000; ++it) {
            Bytes t = f;
            const int k = 1 + int(Rnd() % 4);
            for (int j = 0; j < k; ++j) t[Rnd() % t.size()] = (Rnd() & 3) == 0 ? 0xFF : (unsigned char)(Rnd() & 255);
            const bool ok = wav::DecodeWav(t.data(), t.size(), a, &e, &lim);
            ++runs; if (ok) ++oks;
            CHECK(!ok || (a.channels >= 1 && a.samples.size() <= lim.maxOutputSamples && a.samples.size() % std::size_t(a.channels) == 0));
        }
    }
    CHECK(oks > 0 && oks < runs);   // the mutations hit both outcomes
    std::printf("  hostile runs %ld, accepted %ld\n", runs, oks);
}

static bool ReadFile(const std::string& p, Bytes& out) {
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); return true;
}

#ifndef OWN_WAV_STANDALONE
static void TestLoadWavSwitch() {
    // AudioClip::LoadWAV goes through our own decoder (the only WAV reader since the OKAY_OWN_WAV switch and the older
    // parser path were deleted). The expected values were what the old path and the own path both gave when they were
    // still compared here; they are now fixed numbers.
    const char* path = "own_wav_switch.wav";
    // 6 channels, two frames: 6 * 16 bit each, kept as a mono average (more than 2 channels always downmix).
    Bytes d; for (int i = 0; i < 12; ++i) { P16(d, unsigned((i * 1000) & 0xFFFF)); }
    Bytes w = Wav(Fmt(1, 6, 8000, 12, 16), d);
    { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(w.data()), long(w.size())); }
    AudioClip c6; CHECK(c6.LoadWAV(path));
    CHECK(c6.channels == 1 && c6.sampleRate == 8000 && c6.samples.size() == 2);
    if (c6.samples.size() == 2) {
        CHECK_NEAR(c6.samples[0], 2500.0f / 32768.0f, 1e-6);    // (0 + 1000 + ... + 5000) / 6
        CHECK_NEAR(c6.samples[1], 8500.0f / 32768.0f, 1e-6);    // (6000 + ... + 11000) / 6
    }
    // stereo kept as stereo, downmix flag halves it
    Bytes ds; P16(ds, 16384); P16(ds, 0);
    Bytes ws = Wav(Fmt(1, 2, 8000, 4, 16), ds);
    { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(ws.data()), long(ws.size())); }
    AudioClip st; CHECK(st.LoadWAV(path) && st.channels == 2 && st.samples.size() == 2);
    AudioClip mo; CHECK(mo.LoadWAV(path, nullptr, true) && mo.channels == 1 && mo.samples.size() == 1 && mo.samples[0] == 0.25f);
    // IMA ADPCM loads (the deleted older path could not read it)
    Bytes blk; P16(blk, 0); blk.push_back(0); blk.push_back(0); blk.push_back(0x07); blk.push_back(0x8F); blk.push_back(0); blk.push_back(0);
    Bytes wa = Wav(Fmt(0x11, 1, 8000, 8, 4), blk);
    { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(wa.data()), long(wa.size())); }
    AudioClip ad; CHECK(ad.LoadWAV(path) && ad.samples.size() == 9 && ad.samples[1] == 11.0f / 32768.0f);
    // errors come through
    std::string err; AudioClip bad; { std::ofstream f(path, std::ios::binary); f << "RIFFxxxxWAVEjunk"; }
    CHECK(!bad.LoadWAV(path, &err) && !err.empty());
    // an environment variable left over from the old switch changes nothing
    setenv("OKAY_OWN_WAV", "0", 1);
    { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(wa.data()), long(wa.size())); }
    AudioClip ad0; CHECK(ad0.LoadWAV(path) && ad0.samples.size() == 9);
    unsetenv("OKAY_OWN_WAV");
    std::remove(path);
}

// ---- streamed WAV (AudioStream) == okay::wav whole-file decode, sample for sample ----------------------------------
// The files are built here: every encoding, several blocks for ADPCM (with a short last block), extensible, chunks in
// unusual order with odd sizes. Expected samples come from DecodeWav (more than two channels averaged to mono, as
// AudioStream and AudioClip::LoadWAV both do).
struct StreamCase { const char* name; Bytes file; };

static std::vector<StreamCase> StreamCases() {
    std::vector<StreamCase> v;
    { Bytes x; P16(x, 2); P16(x, 505); v.push_back({"ima mono", Wav(Fmt(0x11, 1, 22050, 256, 4, x), ImaBlocks(1, 256, 21, 100))}); }
    { Bytes x; P16(x, 2); P16(x, 505); v.push_back({"ima stereo", Wav(Fmt(0x11, 2, 22050, 512, 4, x), ImaBlocks(2, 512, 12, 200))}); }
    v.push_back({"ima 3ch", Wav(Fmt(0x11, 3, 22050, 12 * 9, 4), ImaBlocks(3, 108, 60, 0))});
    v.push_back({"ms adpcm mono", Wav(Fmt(2, 1, 22050, 256, 4, MsCoefExtra(500)), MsBlocks(1, 256, 22, 77))});
    v.push_back({"ms adpcm stereo", Wav(Fmt(2, 2, 22050, 512, 4, MsCoefExtra(500)), MsBlocks(2, 512, 11, 300))});
    v.push_back({"ms adpcm std coef", Wav(Fmt(2, 1, 8000, 64, 4, Bytes{0, 0}), MsBlocks(1, 64, 90, 9))});
    v.push_back({"a-law stereo", Wav(Fmt(6, 2, 8000, 2, 8), Rand(2 * 9001))});
    v.push_back({"mu-law mono", Wav(Fmt(7, 1, 8000, 1, 8), Rand(9003))});
    { Bytes d; for (int i = 0; i < 2 * 7001; ++i) { float f = float(int(Rnd() % 20001) - 10000) / 10000.0f; std::uint32_t u; std::memcpy(&u, &f, 4); P32(d, u); }
      v.push_back({"float32 stereo", Wav(Fmt(3, 2, 32000, 8, 32), d)}); }
    { Bytes d; for (int i = 0; i < 6007; ++i) { double f = double(int(Rnd() % 20001) - 10000) / 7000.0; std::uint64_t u; std::memcpy(&u, &f, 8); P32(d, std::uint32_t(u)); P32(d, std::uint32_t(u >> 32)); }
      v.push_back({"float64 mono", Wav(Fmt(3, 1, 32000, 8, 64), d)}); }
    v.push_back({"pcm24 stereo", Wav(Fmt(1, 2, 44100, 6, 24), Rand(6 * 8000))});
    v.push_back({"pcm32 mono", Wav(Fmt(1, 1, 44100, 4, 32), Rand(4 * 8000))});
    v.push_back({"pcm8 stereo", Wav(Fmt(1, 2, 11025, 2, 8), Rand(2 * 8000))});
    v.push_back({"pcm12-in-16 mono", Wav(Fmt(1, 1, 11025, 2, 12), Rand(2 * 8000))});
    v.push_back({"extensible pcm16 4ch", Wav(Fmt(0xFFFE, 4, 16000, 8, 16, ExtExtra(16, 1)), Rand(8 * 5000))});
    v.push_back({"extensible float stereo", [] { Bytes d; for (int i = 0; i < 2 * 5000; ++i) { float f = float(int(Rnd() % 2001) - 1000) / 1000.0f; std::uint32_t u; std::memcpy(&u, &f, 4); P32(d, u); }
                                               return Wav(Fmt(0xFFFE, 2, 16000, 8, 32, ExtExtra(32, 3)), d); }()});
    v.push_back({"extensible ima stereo", Wav(Fmt(0xFFFE, 2, 22050, 256, 4, ExtExtra(4, 0x11)), ImaBlocks(2, 256, 30, 0))});
    v.push_back({"extensible a-law mono", Wav(Fmt(0xFFFE, 1, 8000, 1, 8, ExtExtra(8, 6)), Rand(7777))});
    v.push_back({"pcm16 odd chunks, data last", OddChunks(Fmt(1, 2, 8000, 4, 16), Rand(4 * 6000 + 2), false)});
    v.push_back({"pcm16 odd chunks, data first", OddChunks(Fmt(1, 1, 8000, 2, 16), Rand(2 * 6001), true)});
    { Bytes x; P16(x, 2); P16(x, 505); v.push_back({"ima odd chunks, data first", OddChunks(Fmt(0x11, 1, 22050, 256, 4, x), ImaBlocks(1, 256, 9, 61), true)}); }
    v.push_back({"ms adpcm odd chunks", OddChunks(Fmt(2, 1, 22050, 256, 4, MsCoefExtra(500)), MsBlocks(1, 256, 9, 33), false)});
    return v;
}

// What AudioStream should give for `file`: DecodeWav's samples, averaged to mono when there are more than two channels.
static bool Expected(const Bytes& file, AudioClip& out, std::string* e = nullptr) {
    wav::WavAudio a;
    if (!Dec(file, a, e)) return false;
    out.sampleRate = a.sampleRate; out.channels = a.channels; out.samples = std::move(a.samples);
    if (out.channels > 2) out = out.Downmixed();
    return true;
}
static bool SameFloats(const float* a, const float* b, std::size_t n) { return n == 0 || std::memcmp(a, b, n * sizeof(float)) == 0; }
static std::vector<float> ReadStream(AudioStream& s, std::size_t chunk, std::size_t limit) {
    std::vector<float> out, buf(chunk * 2);
    for (;;) {
        const std::size_t n = s.Read(buf.data(), chunk);
        out.insert(out.end(), buf.begin(), buf.begin() + long(n * std::size_t(s.Channels())));
        if (n < chunk || out.size() > limit) break;
    }
    return out;
}

static void TestStreamMatchesWhole() {
    int good = 0;
    const auto cases = StreamCases();
    for (const StreamCase& sc : cases) {
        AudioClip want; std::string e;
        if (!Expected(sc.file, want, &e)) { std::fprintf(stderr, "  case %s: DecodeWav failed: %s\n", sc.name, e.c_str()); CHECK(false); continue; }
        auto bytes = std::make_shared<std::vector<unsigned char>>(sc.file);
        std::string serr;
        auto st = AudioStream::OpenMemory(bytes, &serr);
        if (!st) { std::fprintf(stderr, "  case %s: stream refused: %s\n", sc.name, serr.c_str()); CHECK(false); continue; }
        const std::size_t ch = std::size_t(want.channels), n = want.FrameCount();
        bool ok = st->Channels() == want.channels && st->SampleRate() == want.sampleRate && st->TotalFrames() == std::int64_t(n);
        if (!ok) std::fprintf(stderr, "  case %s: format differs: ch %d/%d rate %d/%d frames %lld/%zu\n", sc.name, st->Channels(), want.channels,
                              st->SampleRate(), want.sampleRate, (long long)st->TotalFrames(), n);
        // whole file in assorted piece sizes
        for (std::size_t chunk : {std::size_t(1), std::size_t(333), std::size_t(4096)}) {
            if (!ok) break;
            st->Seek(0);
            const auto got = ReadStream(*st, chunk, want.samples.size() * 2 + 100);
            if (got.size() != want.samples.size() || !SameFloats(got.data(), want.samples.data(), got.size())) {
                std::fprintf(stderr, "  case %s: streamed != whole (piece %zu, %zu vs %zu samples)\n", sc.name, chunk, got.size(), want.samples.size());
                ok = false;
            }
        }
        // seeks: block edges (505 and 500 frame ADPCM blocks), random places, then reading on to the end
        std::vector<std::int64_t> targets = {0, 1, 499, 500, 501, 504, 505, 506, 1009, 1010, std::int64_t(n) / 2 + 3, std::int64_t(n) - 1};
        for (int i = 0; i < 8; ++i) targets.push_back(std::int64_t(Rnd() % std::uint32_t(n)));
        for (std::int64_t t : targets) {
            if (!ok || t < 0 || t >= std::int64_t(n)) continue;
            st->Seek(t);
            const std::size_t len = std::min<std::size_t>(700, n - std::size_t(t));
            std::vector<float> buf(len * ch);
            const std::size_t got = st->Read(buf.data(), len);
            if (got != len || !SameFloats(buf.data(), want.samples.data() + std::size_t(t) * ch, len * ch)) {
                std::fprintf(stderr, "  case %s: seek to %lld differs\n", sc.name, (long long)t); ok = false;
            }
        }
        if (ok) {   // reading on from a seek point continues the same audio to the end
            const std::size_t t = n / 3;
            st->Seek(std::int64_t(t));
            const auto rest = ReadStream(*st, 1000, want.samples.size() * 2 + 100);
            if (rest.size() != (n - t) * ch || !SameFloats(rest.data(), want.samples.data() + t * ch, rest.size())) {
                std::fprintf(stderr, "  case %s: read on after a seek differs\n", sc.name); ok = false;
            }
        }
        if (ok) {   // a loop is the file again, with no gap, twice and a bit
            st->loop = true;
            st->Seek(0);
            const std::size_t total = 2 * n + 17;
            std::vector<float> buf(total * ch);
            const std::size_t got = st->Read(buf.data(), total);
            bool same = got == total;
            for (std::size_t i = 0; same && i < total * ch; ++i) same = std::memcmp(&buf[i], &want.samples[i % (n * ch)], sizeof(float)) == 0;
            if (!same) { std::fprintf(stderr, "  case %s: loop differs\n", sc.name); ok = false; }
            st->loop = false;
        }
        CHECK(ok);
        if (ok) ++good;
    }
    std::printf("  streamed WAV equal to the whole-file decode: %d of %zu cases\n", good, cases.size());
}

// Hostile input on the streaming path: every truncation and random byte mutations of small files of every encoding.
// No crash, no read past the data; when DecodeWav accepts the bytes the stream gives exactly its samples; when
// DecodeWav refuses them the stream either refuses too or ends early (a damaged ADPCM block ends it there).
static void TestStreamHostile() {
    std::vector<Bytes> files;
    { Bytes x; P16(x, 2); P16(x, 57); files.push_back(Wav(Fmt(0x11, 1, 8000, 32, 4, x), ImaBlocks(1, 32, 3, 13))); }
    files.push_back(Wav(Fmt(0x11, 2, 8000, 24, 4), ImaBlocks(2, 24, 3, 9)));
    files.push_back(Wav(Fmt(2, 1, 8000, 20, 4, MsCoefExtra(28)), MsBlocks(1, 20, 3, 11)));
    files.push_back(Wav(Fmt(2, 2, 8000, 30, 4, Bytes{0, 0}), MsBlocks(2, 30, 3, 17)));
    files.push_back(Wav(Fmt(6, 2, 8000, 2, 8), Rand(41)));
    files.push_back(Wav(Fmt(0xFFFE, 3, 8000, 9, 24, ExtExtra(24, 1)), Rand(9 * 7)));
    files.push_back(OddChunks(Fmt(3, 1, 8000, 8, 64), Rand(8 * 9), true));
    long runs = 0, opened = 0, equal = 0;
    auto exercise = [&](const Bytes& f) {
        ++runs;
        AudioClip want; const bool wholeOk = Expected(f, want);
        auto st = AudioStream::OpenMemory(std::make_shared<std::vector<unsigned char>>(f));
        if (!st) { CHECK(!wholeOk); return; }   // the stream refuses nothing that the whole-file decode accepts
        ++opened;
        const auto got = ReadStream(*st, 37, 1u << 20);
        CHECK(st->TotalFrames() >= 0 && got.size() <= std::size_t(st->TotalFrames()) * std::size_t(st->Channels()));
        if (wholeOk) {
            const bool eq = got.size() == want.samples.size() && SameFloats(got.data(), want.samples.data(), got.size());
            CHECK(eq);
            if (eq) ++equal;
        }
        // seeks anywhere (also past the end) and a loop never hang or crash
        st->loop = true;
        for (int k = 0; k < 4; ++k) {
            st->Seek(std::int64_t(Rnd() % 400) - 50);
            float buf[2 * 64];
            st->Read(buf, 64);
        }
    };
    for (const Bytes& f : files) {
        for (std::size_t n = 0; n <= f.size(); ++n) exercise(Bytes(f.begin(), f.begin() + long(n)));
        for (int it = 0; it < 400; ++it) {
            Bytes t = f;
            const int k = 1 + int(Rnd() % 4);
            for (int j = 0; j < k; ++j) t[Rnd() % t.size()] = (Rnd() & 3) == 0 ? 0xFF : (unsigned char)(Rnd() & 255);
            exercise(t);
        }
    }
    CHECK(opened > 0 && equal > 0 && opened < runs);
    std::printf("  streamed hostile runs %ld, opened %ld, equal to the whole-file decode %ld\n", runs, opened, equal);
}

#endif

// Differential: IMA ADPCM against Python's audioop at test time only. The Python script builds random blocks (mono and
// stereo, random predictors and step indexes, random nibbles), writes the WAV file and the samples audioop decodes
// (nibbles swapped to WAV's low-first order), and this test compares them exactly.
static void TestImaDifferential() {
#ifdef _WIN32
    std::printf("SKIP differential test: POSIX only\n");
    return;
#else
    const char* rq = std::getenv("OKAY_REQUIRE_DIFFERENTIAL");
    const bool require = rq && std::string(rq) == "1";
    char tmpl[] = "/tmp/okwavXXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("SKIP differential test: no temp directory\n"); CHECK(!require); return; }
    const std::string dir = tmpl;
    const std::string script =
        "import audioop, random, struct, sys\n"
        "d = sys.argv[1]\n"
        "random.seed(7)\n"
        "n = 0\n"
        "for ch in (1, 2, 3):\n"
        "  for trial in range(40):\n"
        "    groups = random.choice((1, 2, 7, 31))\n"
        "    align = 4 * ch + groups * 4 * ch\n"
        "    blocks = random.randint(1, 3)\n"
        "    data = b''; exp = []\n"
        "    for b in range(blocks):\n"
        "      hdr = b''; streams = []; preds = []\n"
        "      for c in range(ch):\n"
        "        p = random.randint(-32768, 32767); i = random.randint(0, 88)\n"
        "        hdr += struct.pack('<hBB', p, i, 0); preds.append((p, i))\n"
        "        streams.append(bytes(random.randrange(256) for _ in range(4 * groups)))\n"
        "      body = b''\n"
        "      for g in range(groups):\n"
        "        for c in range(ch): body += streams[c][4*g:4*g+4]\n"
        "      data += hdr + body\n"
        "      per = []\n"
        "      for c in range(ch):\n"
        "        sw = bytes(((x & 15) << 4) | (x >> 4) for x in streams[c])\n"
        "        pcm, _ = audioop.adpcm2lin(sw, 2, preds[c])\n"
        "        per.append([preds[c][0]] + list(struct.unpack('<%dh' % (len(pcm)//2), pcm)))\n"
        "      for f in range(len(per[0])):\n"
        "        for c in range(ch): exp.append(per[c][f])\n"
        "    fmt = struct.pack('<HHIIHHHH', 0x11, ch, 22050, 1, align, 4, 2, 1 + 8 * groups)\n"
        "    wav = b'RIFF' + struct.pack('<I', 0) + b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data\n"
        "    open('%s/%d.wav' % (d, n), 'wb').write(wav)\n"
        "    open('%s/%d.raw' % (d, n), 'wb').write(struct.pack('<%dh' % len(exp), *exp))\n"
        "    n += 1\n"
        "print(n)\n";
    { std::ofstream f(dir + "/gen.py"); f << script; }
    const std::string cmd = "python3 -W ignore -I " + dir + "/gen.py " + dir + " > " + dir + "/count.txt 2>" + dir + "/err.txt";
    const int rc = std::system(cmd.c_str());
    Bytes cnt; int n = 0;
    if (rc != 0 || !ReadFile(dir + "/count.txt", cnt) || (n = std::atoi(std::string(cnt.begin(), cnt.end()).c_str())) <= 0) {
        std::printf("SKIP differential test: python3 with audioop is not available (rc=%d)\n", rc);
        CHECK(!require);
    } else {
        int same = 0;
        for (int i = 0; i < n; ++i) {
            Bytes w, raw;
            CHECK(ReadFile(dir + "/" + std::to_string(i) + ".wav", w));
            CHECK(ReadFile(dir + "/" + std::to_string(i) + ".raw", raw));
            wav::WavAudio a; std::string e;
            const bool ok = Dec(w, a, &e);
            CHECK(ok);
            if (!ok) { std::fprintf(stderr, "  file %d: %s\n", i, e.c_str()); continue; }
            auto v = wav::ToS16(a);
            bool eq = v.size() * 2 == raw.size() && std::memcmp(v.data(), raw.data(), raw.size()) == 0;
            CHECK(eq);
            if (eq) ++same;
        }
        std::printf("  IMA differential: %d of %d files identical to audioop\n", same, n);
    }
    std::string rm = "rm -rf " + dir;
    if (std::system(rm.c_str()) != 0) {}
#endif
}

int main() {
    RUN_SUITE("own_wav");
    TestPcm();
    TestG711();
    TestImaAdpcm();
    TestMsAdpcm();
    TestRefusals();
    TestHostile();
#ifndef OWN_WAV_STANDALONE
    TestLoadWavSwitch();
    TestStreamMatchesWhole();
    TestStreamHostile();
#endif
    TestImaDifferential();
    TEST_MAIN_RESULT();
}
