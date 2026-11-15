// Own WAV streaming (okay::wav::WavStream, and AudioStream through it when OKAY_OWN_WAV is on), RF64 / BW64, and the
// "smpl" / "cue " chunks. Written from the public format descriptions (Microsoft RIFF/WAVE notes, EBU TECH 3306 for
// RF64, the IMA and Microsoft ADPCM block descriptions); every "hand" value below was worked out on paper, nibble by
// nibble, not by running the decoder. The streamed decode must equal the whole-clip decode byte for byte.
//
// Build with -DWAVSTREAM_GOLDEN_ONLY against the OLD sources to print the golden hashes of the old AudioStream path
// (that is how kGolden below was made, from commit 9a258154, before this change).
#include "test_framework.hpp"
#include "okay/Audio/AudioStream.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#ifndef WAVSTREAM_GOLDEN_ONLY
#include "okay/Audio/WavDecoder.hpp"
#include "okay/Audio/AudioByteSource.hpp"
#endif

using namespace okay;
using Bytes = std::vector<unsigned char>;

static void SetEnv(const char* v) {
#ifdef _WIN32
    _putenv_s("OKAY_OWN_WAV", v ? v : "");
#else
    if (v) setenv("OKAY_OWN_WAV", v, 1); else unsetenv("OKAY_OWN_WAV");
#endif
}

// ---- file building ---------------------------------------------------------------------------------------------
static void P16(Bytes& b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void P32(Bytes& b, std::uint32_t v) { P16(b, v & 0xFFFF); P16(b, v >> 16); }
static void P64(Bytes& b, std::uint64_t v) { P32(b, std::uint32_t(v)); P32(b, std::uint32_t(v >> 32)); }
static void Tag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }
static const std::int64_t kNoSize = -1;

// A chunk: tag, size field (the real size unless `sizeField` is given), payload, pad byte.
static Bytes Ck(const char* tag, const Bytes& payload, std::int64_t sizeField = kNoSize) {
    Bytes b; Tag(b, tag);
    P32(b, sizeField == kNoSize ? std::uint32_t(payload.size()) : std::uint32_t(sizeField));
    b.insert(b.end(), payload.begin(), payload.end());
    if (payload.size() & 1) b.push_back(0);
    return b;
}
static Bytes File(const char* magic, const std::vector<Bytes>& chunks) {
    Bytes b; Tag(b, magic); P32(b, 0); Tag(b, "WAVE");
    for (const auto& c : chunks) b.insert(b.end(), c.begin(), c.end());
    return b;
}
static Bytes FmtBody(unsigned tag, unsigned ch, std::uint32_t rate, unsigned align, unsigned bits, const Bytes& extra = {}) {
    Bytes f; P16(f, tag); P16(f, ch); P32(f, rate); P32(f, rate * align); P16(f, align); P16(f, bits);
    f.insert(f.end(), extra.begin(), extra.end());
    return f;
}
static Bytes Wav(const Bytes& fmt, const Bytes& data) { return File("RIFF", {Ck("fmt ", fmt), Ck("data", data)}); }
static Bytes Ds64(std::uint64_t dataSize, const std::vector<std::pair<const char*, std::uint64_t>>& table = {}) {
    Bytes p; P64(p, 0); P64(p, dataSize); P64(p, 0); P32(p, std::uint32_t(table.size()));
    for (const auto& e : table) { Tag(p, e.first); P64(p, e.second); }
    return p;
}

struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    unsigned Byte() { return Next() & 255; }
};

static Bytes RandomBytes(Rng& r, std::size_t n) { Bytes b(n); for (auto& x : b) x = (unsigned char)r.Byte(); return b; }

// IMA ADPCM data: `blocks` full blocks of `groups` groups, then an optional partial block of `restGroups` groups (-1: none).
static Bytes ImaFile(Rng& r, unsigned ch, unsigned groups, unsigned blocks, int restGroups, bool wildIndex = false) {
    const unsigned align = 4 * ch + groups * 4 * ch;
    Bytes data;
    auto block = [&](unsigned g) {
        for (unsigned c = 0; c < ch; ++c) {
            P16(data, r.Next() & 0xFFFF);
            data.push_back((unsigned char)(wildIndex ? r.Byte() % 120 : r.Byte() % 89));
            data.push_back(0);
        }
        for (unsigned i = 0; i < g * 4 * ch; ++i) data.push_back((unsigned char)r.Byte());
    };
    for (unsigned b = 0; b < blocks; ++b) block(groups);
    if (restGroups >= 0) block(unsigned(restGroups));
    Bytes extra; P16(extra, 2); P16(extra, 1 + 8 * groups);
    return Wav(FmtBody(0x11, ch, 22050, align, 4, extra), data);
}

// Microsoft ADPCM data: blocks of `align` bytes, then a partial block of `restBytes` (0: none). customCoef: a 3-pair table.
static Bytes MsFile(Rng& r, unsigned ch, unsigned align, unsigned blocks, unsigned restBytes, bool customCoef = false) {
    Bytes data;
    const unsigned nco = customCoef ? 3 : 7;
    auto block = [&](unsigned len) {
        for (unsigned c = 0; c < ch; ++c) data.push_back((unsigned char)(r.Byte() % nco));
        for (unsigned c = 0; c < ch; ++c) P16(data, 16 + r.Next() % 500);   // delta
        for (unsigned c = 0; c < 2 * ch; ++c) P16(data, r.Next() & 0xFFFF);   // sample1, sample2
        for (unsigned i = 7 * ch; i < len; ++i) data.push_back((unsigned char)r.Byte());
    };
    for (unsigned b = 0; b < blocks; ++b) block(align);
    if (restBytes) block(restBytes);
    static const int std7[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};
    static const int cust[3][2] = {{300, -44}, {100, 100}, {-256, 512}};
    Bytes extra; P16(extra, 2 + 2 + 4 * nco); P16(extra, (align - 7 * ch) * 2 / ch + 2); P16(extra, nco);
    for (unsigned i = 0; i < nco; ++i) { P16(extra, unsigned((customCoef ? cust[i][0] : std7[i][0])) & 0xFFFF); P16(extra, unsigned((customCoef ? cust[i][1] : std7[i][1])) & 0xFFFF); }
    return Wav(FmtBody(0x02, ch, 22050, align, 4, extra), data);
}

static std::uint64_t Fnv(const float* p, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n * sizeof(float); ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

// ---- the old AudioStream path: golden hashes -------------------------------------------------------------------
struct GoldenCase { const char* name; unsigned fmt, ch, bits; unsigned frames; };
static const GoldenCase kGoldenCases[] = {
    {"pcm16_mono", 1, 1, 16, 5000}, {"pcm16_stereo", 1, 2, 16, 5000}, {"pcm8_mono", 1, 1, 8, 3000},
    {"pcm24_stereo", 1, 2, 24, 3000}, {"pcm32_mono", 1, 1, 32, 3000}, {"float32_stereo", 3, 2, 32, 3000},
    {"pcm16_3ch", 1, 3, 16, 3000},
};
static Bytes GoldenFile(const GoldenCase& g) {
    Rng r(0xC0FFEEu + g.fmt * 7 + g.ch * 13 + g.bits);
    Bytes d;
    for (unsigned i = 0; i < g.frames * g.ch; ++i) {
        if (g.fmt == 3) { float f = float(int(r.Next() % 20001) - 10000) / 10000.0f; unsigned char b[4]; std::memcpy(b, &f, 4); d.insert(d.end(), b, b + 4); }
        else for (unsigned k = 0; k < g.bits / 8; ++k) d.push_back((unsigned char)r.Byte());
    }
    return Wav(FmtBody(g.fmt, g.ch, 22050, g.ch * g.bits / 8, g.bits), d);
}
// Streams the file through AudioStream with odd read sizes (plus a seek and a re-read) and hashes everything.
static std::uint64_t StreamHash(const Bytes& file, std::uint64_t* framesOut, int* chOut = nullptr, std::vector<float>* all = nullptr) {
    auto bytes = std::make_shared<const Bytes>(file);
    std::string e;
    auto s = AudioStream::OpenMemory(bytes, &e);
    if (!s) { std::fprintf(stderr, "  open failed: %s\n", e.c_str()); *framesOut = 0; return 0; }
    if (chOut) *chOut = s->Channels();
    std::uint64_t h = 1469598103934665603ull, frames = 0;
    std::vector<float> buf(400 * 2);
    for (;;) {
        const std::size_t n = s->Read(buf.data(), 333);
        if (n == 0) break;
        h = Fnv(buf.data(), n * std::size_t(s->Channels()), h);
        if (all) all->insert(all->end(), buf.begin(), buf.begin() + std::ptrdiff_t(n * std::size_t(s->Channels())));
        frames += n;
    }
    s->Seek(1234);
    const std::size_t n = s->Read(buf.data(), 400);
    h = Fnv(buf.data(), n * std::size_t(s->Channels()), h);
    frames += n;
    *framesOut = frames;
    return h;
}

#ifdef WAVSTREAM_GOLDEN_ONLY
int main() {
    SetEnv("0");
    for (const auto& g : kGoldenCases) {
        std::uint64_t fr = 0;
        const std::uint64_t h = StreamHash(GoldenFile(g), &fr);
        std::printf("    {\"%s\", 0x%016llXull, %llu},\n", g.name, (unsigned long long)h, (unsigned long long)fr);
    }
}
#else

// Golden values: printed by this very test file built against the unchanged sources of commit 9a258154.
struct Golden { const char* name; std::uint64_t hash, frames; };
static const Golden kGolden[] = {
    {"pcm16_mono", 0x49C41AC82867F382ull, 5400},
    {"pcm16_stereo", 0x9FC242C176187618ull, 5400},
    {"pcm8_mono", 0x465DFA41A108CDC0ull, 3400},
    {"pcm24_stereo", 0x4175F765957001FEull, 3400},
    {"pcm32_mono", 0xE7D097062F79273Bull, 3400},
    {"float32_stereo", 0x0635BF1A62A582CAull, 3400},
    {"pcm16_3ch", 0x129A287BBA4E604Dull, 3400},
};

static bool Same(const std::vector<float>& a, const float* b, std::size_t n) {
    return a.size() >= n && (n == 0 || std::memcmp(a.data(), b, n * sizeof(float)) == 0);
}
static std::shared_ptr<const Bytes> Mem(const Bytes& b) { return std::make_shared<const Bytes>(b); }
static bool DecodeClip(const Bytes& w, wav::WavAudio& a, std::string* e = nullptr, const wav::Limits* l = nullptr) {
    return wav::DecodeWav(w.data(), w.size(), a, e, l);
}

// Whole stream with reads of `readSize` frames.
static bool StreamAll(const Bytes& w, std::size_t readSize, std::vector<float>& out, wav::WavMeta* meta = nullptr,
                      std::uint64_t* total = nullptr, const wav::Limits* l = nullptr) {
    wav::WavStream s;
    std::string e;
    if (!s.Open(AudioByteSource::FromMemory(Mem(w)), &e, l)) return false;
    if (meta) *meta = s.Info();
    if (total) *total = s.TotalFrames();
    const std::size_t ch = std::size_t(s.Info().channels);
    out.clear();
    std::vector<float> buf(readSize * ch);
    for (;;) {
        const std::size_t n = s.Read(buf.data(), readSize);
        if (n == 0) break;
        out.insert(out.end(), buf.begin(), buf.begin() + std::ptrdiff_t(n * ch));
    }
    return !s.Failed();
}

// ---- hand-computed values ---------------------------------------------------------------------------------------
static void TestHandIma() {
    // One mono IMA block (align 8): predictor 0, step index 0, then 8 nibbles 7,0,F,8,0,0,0,0 (low nibble of a byte first).
    // step[0]=7: 7 -> 0+1+3+7 = 11 -> 11, index 0+8 = 8
    // step[8]=16: 0 -> 16>>3 = 2 -> 13, index 7 ; step[7]=14: F -> -(1+3+7+14) = -25 -> -12, index 15
    // step[15]=31: 8 -> -(31>>3) = -3 -> -15, index 14 ; step[14]=28: 0 -> +(28>>3) = +3 -> -12 (13) ; step[13]=25: +3 -> -9 (12) ;
    // step[12]=23: +2 -> -7 (11) ; step[11]=21: +2 -> -5
    Bytes blk; P16(blk, 0); blk.push_back(0); blk.push_back(0); blk.push_back(0x07); blk.push_back(0x8F); blk.push_back(0); blk.push_back(0);
    Bytes two = blk; two.insert(two.end(), blk.begin(), blk.end());
    const int want[9] = {0, 11, 13, -12, -15, -12, -9, -7, -5};
    Bytes w = Wav(FmtBody(0x11, 1, 8000, 8, 4), two);
    wav::WavStream s; std::string e;
    CHECK(s.Open(AudioByteSource::FromMemory(Mem(w)), &e));
    CHECK(s.TotalFrames() == 18 && s.Info().encoding == wav::Encoding::ImaAdpcm && s.Info().channels == 1 && s.Info().sampleRate == 8000);
    std::vector<float> got(18);
    CHECK(s.Read(got.data(), 18) == 18);
    for (int i = 0; i < 18; ++i) CHECK(got[i] == float(want[i % 9]) / 32768.0f);
    CHECK(s.Read(got.data(), 5) == 0);   // at the end
    // seek into the second block (frame 12 = its frame 3 = -12) and read 4 frames
    CHECK(s.Seek(12) && s.Position() == 12);
    CHECK(s.Read(got.data(), 4) == 4 && got[0] == -12.0f / 32768.0f && got[1] == -15.0f / 32768.0f && got[3] == -9.0f / 32768.0f);
    // seek to the very first frame of block two (frame 9) and to the last frame
    CHECK(s.Seek(9) && s.Read(got.data(), 1) == 1 && got[0] == 0.0f);
    CHECK(s.Seek(17) && s.Read(got.data(), 5) == 1 && got[0] == -5.0f / 32768.0f);
    CHECK(s.Seek(1000) && s.Position() == 18 && s.Read(got.data(), 1) == 0);   // clamped to the end

    // Step index 88 (the top of the table, 32767) with nibbles 7,F,0,0,0,0,0,0: the index must stay at 88 after +8.
    // 7: +(4095+8191+16383+32767 = 61436) clamps to 32767, index 96 -> 88
    // F: -61436 -> -28669, index 96 -> 88 ; then 0 (+4095, index 87): -24574 ; step[87]=29794: +3724 -> -20850 (86) ;
    // step[86]=27086: +3385 -> -17465 (85) ; step[85]=24623: +3077 -> -14388 (84) ; step[84]=22385: +2798 -> -11590 (83) ;
    // step[83]=20350: +2543 -> -9047
    Bytes top; P16(top, 0); top.push_back(88); top.push_back(0); top.push_back(0xF7); top.push_back(0); top.push_back(0); top.push_back(0);
    const int wantTop[9] = {0, 32767, -28669, -24574, -20850, -17465, -14388, -11590, -9047};
    wav::WavAudio a;
    CHECK(DecodeClip(Wav(FmtBody(0x11, 1, 8000, 8, 4), top), a) && a.samples.size() == 9 && a.warnings.empty());
    for (int i = 0; i < 9 && i < int(a.samples.size()); ++i) CHECK(a.samples[std::size_t(i)] == float(wantTop[i]) / 32768.0f);
    std::vector<float> st;
    CHECK(StreamAll(Wav(FmtBody(0x11, 1, 8000, 8, 4), top), 4, st) && st.size() == 9);
    for (int i = 0; i < 9 && i < int(st.size()); ++i) CHECK(st[std::size_t(i)] == float(wantTop[i]) / 32768.0f);
    // Step index 0 with nibble 0 must not go below 0: predictor 100 stays 100 (step 7 >> 3 = 0), eight times.
    Bytes low; P16(low, 100); low.push_back(0); low.push_back(0); for (int i = 0; i < 4; ++i) low.push_back(0);
    CHECK(StreamAll(Wav(FmtBody(0x11, 1, 8000, 8, 4), low), 100, st) && st.size() == 9);
    for (std::size_t i = 0; i < st.size(); ++i) CHECK(st[i] == 100.0f / 32768.0f);
    // A header index above 88 is clamped, with ONE warning however many blocks have it.
    Bytes bad; for (int b = 0; b < 3; ++b) { P16(bad, 0); bad.push_back(200); bad.push_back(0); for (int i = 0; i < 4; ++i) bad.push_back(0); }
    wav::WavStream ws;
    CHECK(ws.Open(AudioByteSource::FromMemory(Mem(Wav(FmtBody(0x11, 1, 8000, 8, 4), bad)))));
    std::vector<float> buf(30);
    CHECK(ws.Read(buf.data(), 30) == 27 && ws.Info().warnings.size() == 1);
}

static void TestHandMs() {
    // Mono MS ADPCM, align 9 (7 header + 2 data bytes), standard coefficient table, predictor 0 = (256, 0).
    // header: predictor 0, delta 16, sample1 1000, sample2 900. Output starts with sample2 then sample1.
    // byte 0x2E: nibble 2: (1000*256 + 900*0) >> 8 = 1000, +2*16 = 1032, delta = (230*16)>>8 = 14 -> floor 16
    //            nibble E (-2): pred = 1032, 1032 - 2*16 = 1000, delta stays 16
    // byte 0x70: nibble 7: pred 1000 + 7*16 = 1112, delta = (614*16)>>8 = 38 ; nibble 0: pred 1112 + 0 = 1112, delta (230*38)>>8 = 34
    Bytes blk; blk.push_back(0); P16(blk, 16); P16(blk, 1000); P16(blk, 900); blk.push_back(0x2E); blk.push_back(0x70);
    Bytes two = blk; two.insert(two.end(), blk.begin(), blk.end());
    const int want[6] = {900, 1000, 1032, 1000, 1112, 1112};
    Bytes w = Wav(FmtBody(0x02, 1, 8000, 9, 4), two);
    wav::WavStream s;
    CHECK(s.Open(AudioByteSource::FromMemory(Mem(w))));
    CHECK(s.TotalFrames() == 12 && s.Info().encoding == wav::Encoding::MsAdpcm);
    std::vector<float> got(12);
    CHECK(s.Read(got.data(), 12) == 12);
    for (int i = 0; i < 12; ++i) CHECK(got[std::size_t(i)] == float(want[i % 6]) / 32768.0f);
    CHECK(s.Seek(8) && s.Read(got.data(), 4) == 4 && got[0] == 1032.0f / 32768.0f && got[3] == 1112.0f / 32768.0f);
    CHECK(s.Seek(6) && s.Read(got.data(), 1) == 1 && got[0] == 900.0f / 32768.0f);
    // a predictor index outside the table (7 with the standard seven) is an error in the second block: block one still plays
    Bytes badBlk = blk; badBlk[0] = 7; Bytes both = blk; both.insert(both.end(), badBlk.begin(), badBlk.end());
    wav::WavStream t;
    CHECK(t.Open(AudioByteSource::FromMemory(Mem(Wav(FmtBody(0x02, 1, 8000, 9, 4), both)))));
    CHECK(t.Read(got.data(), 12) == 6 && t.Failed() && !t.Error().empty());   // block one plays, block two is refused
    CHECK(t.Read(got.data(), 12) == 0 && !t.Seek(0));
    wav::WavAudio a; std::string e;
    CHECK(!DecodeClip(Wav(FmtBody(0x02, 1, 8000, 9, 4), both), a, &e) && !e.empty());
}

static void TestHandPcm() {
    // 16-bit stereo, 4 frames; stream reads of 3 then 1, and a seek back.
    Bytes d; const int v[8] = {0, 16384, -32768, 8192, 1, -1, 32767, -16384};
    for (int x : v) P16(d, unsigned(x) & 0xFFFF);
    wav::WavStream s;
    CHECK(s.Open(AudioByteSource::FromMemory(Mem(Wav(FmtBody(1, 2, 44100, 4, 16), d)))));
    CHECK(s.TotalFrames() == 4 && s.Info().channels == 2);
    float f[8];
    CHECK(s.Read(f, 3) == 3);
    for (int i = 0; i < 6; ++i) CHECK(f[i] == float(v[i]) / 32768.0f);
    CHECK(s.Read(f, 3) == 1 && f[0] == float(v[6]) / 32768.0f && f[1] == float(v[7]) / 32768.0f);
    CHECK(s.Read(f, 3) == 0);
    CHECK(s.Seek(1) && s.Read(f, 1) == 1 && f[0] == -32768.0f / 32768.0f && f[1] == 0.25f);
}

static Bytes SmplBody(const std::vector<std::vector<std::uint32_t>>& loops, std::uint32_t declared) {
    Bytes p; P32(p, 0x11); P32(p, 0x22); P32(p, 22675); P32(p, 69); P32(p, 0x80000000u); P32(p, 24); P32(p, 5); P32(p, declared); P32(p, 0);
    for (const auto& l : loops) for (std::uint32_t x : l) P32(p, x);
    return p;
}
static Bytes CueBody(const std::vector<std::vector<std::uint32_t>>& pts, std::uint32_t declared) {
    Bytes p; P32(p, declared);
    for (const auto& q : pts) { P32(p, q[0]); P32(p, q[1]); Tag(p, "data"); P32(p, q[2]); P32(p, q[3]); P32(p, q[4]); }
    return p;
}
// ---- RF64 / BW64 -----------------------------------------------------------------------------------------------
static void TestRf64() {
    Bytes d; const int v[4] = {1000, -1000, 16384, -16384};
    for (int x : v) P16(d, unsigned(x) & 0xFFFF);
    const Bytes fmt = FmtBody(1, 1, 8000, 2, 16);
    wav::WavAudio a; std::string e;
    for (const char* magic : {"RF64", "BW64"}) {
        // ds64 first, then fmt, then data whose 32-bit size is 0xFFFFFFFF (the real size is in ds64)
        Bytes w = File(magic, {Ck("ds64", Ds64(8)), Ck("fmt ", fmt), Ck("data", d, 0xFFFFFFFFll)});
        CHECK(DecodeClip(w, a, &e) && a.rf64 && a.samples.size() == 4 && a.warnings.empty());
        for (int i = 0; i < 4 && i < int(a.samples.size()); ++i) CHECK(a.samples[std::size_t(i)] == float(v[i]) / 32768.0f);
        std::vector<float> st; wav::WavMeta m; std::uint64_t tot = 0;
        CHECK(StreamAll(w, 3, st, &m, &tot) && m.rf64 && tot == 4 && st == a.samples);
    }
    // ds64 data size smaller than what is in the file: only that much is audio; a chunk after it is still found
    {
        Bytes smpl; for (int i = 0; i < 9; ++i) P32(smpl, 0);
        Bytes w = File("RF64", {Ck("ds64", Ds64(4)), Ck("fmt ", fmt), Ck("data", d, 0xFFFFFFFFll), Ck("smpl", smpl)});
        // the data chunk is "4 bytes long" by ds64, so the next chunk starts after 2 frames, inside our payload bytes:
        // the walk must follow ds64's size, not the file length. Build it right: data payload is only 4 bytes here.
        Bytes d2(d.begin(), d.begin() + 4);
        Bytes smplOk; P32(smplOk, 1); P32(smplOk, 2); P32(smplOk, 125000); P32(smplOk, 60); P32(smplOk, 0); P32(smplOk, 0); P32(smplOk, 0); P32(smplOk, 0); P32(smplOk, 0);
        Bytes w2 = File("RF64", {Ck("ds64", Ds64(4)), Ck("fmt ", fmt), Ck("data", d2, 0xFFFFFFFFll), Ck("smpl", smplOk)});
        CHECK(DecodeClip(w2, a, &e) && a.samples.size() == 2 && a.sampler.present && a.sampler.midiUnityNote == 60 && a.sampler.manufacturer == 1);
        CHECK(a.sampler.samplePeriodNs == 125000);
        (void)w;
    }
    // a non-data chunk of 4 GB or more: its size is in the ds64 table; the walk lands on the right place
    {
        Bytes junk(6, 0x55);
        Bytes w = File("RF64", {Ck("ds64", Ds64(8, {{"junk", 6}})), Ck("junk", junk, 0xFFFFFFFFll), Ck("fmt ", fmt), Ck("data", d, 0xFFFFFFFFll)});
        CHECK(DecodeClip(w, a, &e) && a.samples.size() == 4 && a.samples[2] == 0.5f);
        // the same file with the table entry missing: the junk chunk claims 4 GB, so nothing follows it: no fmt, an error, no crash
        Bytes w2 = File("RF64", {Ck("ds64", Ds64(8)), Ck("junk", junk, 0xFFFFFFFFll), Ck("fmt ", fmt), Ck("data", d, 0xFFFFFFFFll)});
        CHECK(!DecodeClip(w2, a, &e) && e == "missing fmt chunk");
    }
    // refusals: no ds64 before a 0xFFFFFFFF data chunk; a ds64 chunk that is too short
    CHECK(!DecodeClip(File("RF64", {Ck("fmt ", fmt), Ck("data", d, 0xFFFFFFFFll)}), a, &e) && e.find("ds64") != std::string::npos);
    CHECK(!DecodeClip(File("RF64", {Ck("ds64", Bytes(20, 0)), Ck("fmt ", fmt), Ck("data", d, 0xFFFFFFFFll)}), a, &e) && e.find("ds64") != std::string::npos);
    // a plain RIFF file with a ds64 chunk ignores it and still treats 0xFFFFFFFF as "to the end of the file", with a warning
    CHECK(DecodeClip(File("RIFF", {Ck("ds64", Ds64(2)), Ck("fmt ", fmt), Ck("data", d, 0xFFFFFFFFll)}), a, &e) && a.samples.size() == 4 && !a.rf64 && !a.warnings.empty());
    // hostile ds64 sizes: 2^63, 2^64-1, 2^32: the data is cut at the end of the file with a warning, nothing wraps or reads outside
    for (std::uint64_t huge : {std::uint64_t(1) << 63, ~std::uint64_t(0), std::uint64_t(1) << 32, (std::uint64_t(1) << 63) - 1}) {
        Bytes w = File("RF64", {Ck("ds64", Ds64(huge)), Ck("fmt ", fmt), Ck("data", d, 0xFFFFFFFFll)});
        CHECK(DecodeClip(w, a, &e) && a.samples.size() == 4 && !a.warnings.empty());
        std::vector<float> st;
        CHECK(StreamAll(w, 2, st) && st == a.samples);
        // and the same huge value as a table entry for a chunk in front
        Bytes w2 = File("RF64", {Ck("ds64", Ds64(8, {{"junk", huge}})), Ck("junk", Bytes(2, 1), 0xFFFFFFFFll), Ck("fmt ", fmt), Ck("data", d)});
        CHECK(!DecodeClip(w2, a, &e));   // the junk chunk swallows the rest: no fmt found, refused, no crash
    }
    // the size 2^64-1 on the data chunk must not wrap the walk back into the audio: a smpl chunk hidden in the sample bytes is not a chunk
    {
        Bytes hidden = Ck("smpl", SmplBody({{9, 0, 1, 2, 0, 0}}, 1));
        Bytes w = File("RF64", {Ck("ds64", Ds64(~std::uint64_t(0))), Ck("fmt ", fmt), Ck("data", hidden, 0xFFFFFFFFll)});
        CHECK(DecodeClip(w, a, &e) && a.samples.size() == hidden.size() / 2 && !a.sampler.present && a.sampler.loops.empty());
        wav::WavStream s; CHECK(s.Open(AudioByteSource::FromMemory(Mem(w))) && !s.Info().sampler.present);
    }
    // a huge ds64 table length with a tiny chunk: cut with a warning
    {
        Bytes p; P64(p, 0); P64(p, 8); P64(p, 0); P32(p, 0xFFFFFFFFu);
        Bytes w = File("RF64", {Ck("ds64", p), Ck("fmt ", fmt), Ck("data", d, 0xFFFFFFFFll)});
        CHECK(DecodeClip(w, a, &e) && a.samples.size() == 4 && !a.warnings.empty());
    }
}

// ---- smpl and cue ----------------------------------------------------------------------------------------------
static void TestMarkers() {
    const Bytes fmt = FmtBody(1, 1, 44100, 2, 16);
    Bytes d(40, 0);
    wav::WavAudio a; std::string e;
    // loops 100..900 forward endless, 200..300 ping-pong playing 3 times; cue points 1 at 0 and 2 at 500
    Bytes smpl = Ck("smpl", SmplBody({{7, 0, 100, 900, 0, 0}, {8, 1, 200, 300, 4096, 3}}, 2));
    Bytes cue = Ck("cue ", CueBody({{1, 0, 0, 0, 0}, {2, 500, 500, 0, 500}}, 2));
    for (int order = 0; order < 2; ++order) {   // markers before the audio, and after it
        std::vector<Bytes> ch = order == 0 ? std::vector<Bytes>{Ck("fmt ", fmt), smpl, cue, Ck("data", d)}
                                           : std::vector<Bytes>{Ck("fmt ", fmt), Ck("data", d), smpl, cue};
        Bytes w = File("RIFF", ch);
        CHECK(DecodeClip(w, a, &e) && a.samples.size() == 20 && a.warnings.empty());
        CHECK(a.sampler.present && a.sampler.manufacturer == 0x11 && a.sampler.product == 0x22 && a.sampler.samplePeriodNs == 22675);
        CHECK(a.sampler.midiUnityNote == 69 && a.sampler.midiPitchFraction == 0x80000000u && a.sampler.smpteFormat == 24 && a.sampler.smpteOffset == 5);
        CHECK(a.sampler.loops.size() == 2);
        if (a.sampler.loops.size() == 2) {
            CHECK(a.sampler.loops[0].id == 7 && a.sampler.loops[0].type == 0 && a.sampler.loops[0].start == 100 && a.sampler.loops[0].end == 900 && a.sampler.loops[0].playCount == 0);
            CHECK(a.sampler.loops[1].id == 8 && a.sampler.loops[1].type == 1 && a.sampler.loops[1].start == 200 && a.sampler.loops[1].end == 300 && a.sampler.loops[1].fraction == 4096 && a.sampler.loops[1].playCount == 3);
        }
        CHECK(a.cues.size() == 2);
        if (a.cues.size() == 2) {
            CHECK(a.cues[0].id == 1 && a.cues[0].position == 0 && std::memcmp(a.cues[0].dataChunk, "data", 4) == 0);
            CHECK(a.cues[1].id == 2 && a.cues[1].position == 500 && a.cues[1].chunkStart == 500 && a.cues[1].blockStart == 0 && a.cues[1].sampleOffset == 500);
        }
        wav::WavStream s;
        CHECK(s.Open(AudioByteSource::FromMemory(Mem(w))) && s.Info().sampler.loops.size() == 2 && s.Info().cues.size() == 2);
        CHECK(s.Info().sampler.loops[1].end == 300 && s.Info().cues[1].sampleOffset == 500);
    }
    // no chunks: nothing present
    CHECK(DecodeClip(Wav(fmt, d), a) && !a.sampler.present && a.sampler.loops.empty() && a.cues.empty());
    // a smpl chunk that declares a billion loops but holds one, and a cue chunk the same: cut, with warnings, no allocation blow-up
    {
        Bytes w = File("RIFF", {Ck("fmt ", fmt), Ck("smpl", SmplBody({{1, 0, 1, 2, 0, 0}}, 1000000000u)), Ck("cue ", CueBody({{1, 0, 0, 0, 0}}, 0xFFFFFFFFu)), Ck("data", d)});
        CHECK(DecodeClip(w, a, &e) && a.sampler.loops.size() == 1 && a.cues.size() == 1 && a.warnings.size() == 2);
    }
    // a smpl chunk shorter than its 36-byte header is ignored; a chunk size past the end of the file cuts what is read
    {
        Bytes w = File("RIFF", {Ck("fmt ", fmt), Ck("data", d), Ck("smpl", Bytes(20, 0))});
        CHECK(DecodeClip(w, a) && !a.sampler.present);
        Bytes w2 = File("RIFF", {Ck("fmt ", fmt), Ck("data", d), Ck("smpl", SmplBody({{1, 0, 1, 2, 0, 0}, {2, 0, 3, 4, 0, 0}}, 2), 0xFFFFFFF0ll)});
        CHECK(DecodeClip(w2, a) && a.sampler.present && a.sampler.loops.size() == 2);
        Bytes w3 = w2; w3.resize(w3.size() - 10);   // second loop cut in half: only the first survives
        CHECK(DecodeClip(w3, a) && a.sampler.loops.size() == 1 && a.warnings.size() == 1);
    }
    // the marker limit
    {
        wav::Limits lim; lim.maxMarkers = 1;
        Bytes w = File("RIFF", {Ck("fmt ", fmt), Ck("smpl", SmplBody({{1, 0, 1, 2, 0, 0}, {2, 0, 3, 4, 0, 0}, {3, 0, 5, 6, 0, 0}}, 3)),
                                Ck("cue ", CueBody({{1, 0, 0, 0, 0}, {2, 5, 5, 0, 5}}, 2)), Ck("data", d)});
        CHECK(DecodeClip(w, a, &e, &lim) && a.sampler.loops.size() == 1 && a.cues.size() == 1 && a.warnings.size() == 2);
        CHECK(a.sampler.loops[0].id == 1 && a.cues[0].id == 1);
    }
}

// ---- streamed == whole clip, every format, many read and seek sizes ------------------------------------------
struct Case { std::string name; Bytes file; unsigned spb; };   // spb: frames in a full block (0 for plain PCM)

static std::vector<Case> AllCases() {
    std::vector<Case> c;
    Rng r(12345);
    auto pcm = [&](const char* n, unsigned tag, unsigned ch, unsigned bits, unsigned frames) {
        const unsigned sb = (bits + 7) / 8;
        c.push_back({n, Wav(FmtBody(tag, ch, 22050, ch * sb, bits), RandomBytes(r, std::size_t(frames) * ch * sb)), 0});
    };
    pcm("pcm8 mono", 1, 1, 8, 3001); pcm("pcm16 mono", 1, 1, 16, 5003); pcm("pcm16 stereo", 1, 2, 16, 4097);
    pcm("pcm24 stereo", 1, 2, 24, 2500); pcm("pcm32 mono", 1, 1, 32, 2000); pcm("pcm12 in 16", 1, 1, 12, 1500);
    pcm("pcm16 6ch", 1, 6, 16, 1000); pcm("float32 stereo", 3, 2, 32, 2000); pcm("float64 mono", 3, 1, 64, 1500);
    pcm("alaw mono", 6, 1, 8, 3000); pcm("alaw stereo", 6, 2, 8, 2500); pcm("mulaw mono", 7, 1, 8, 3000); pcm("mulaw 3ch", 7, 3, 8, 1000);
    // PCM with a block align larger than a frame
    { Bytes d = RandomBytes(r, 3000); c.push_back({"pcm16 mono wide align", Wav(FmtBody(1, 1, 22050, 6, 16), d), 0}); }
    c.push_back({"ima mono g1", ImaFile(r, 1, 1, 40, -1), 9});
    c.push_back({"ima mono g31 rest", ImaFile(r, 1, 31, 7, 5), 249});
    c.push_back({"ima stereo g31 rest", ImaFile(r, 2, 31, 6, 9), 249});
    c.push_back({"ima stereo g7 wild index", ImaFile(r, 2, 7, 20, 0, true), 57});
    c.push_back({"ima 3ch g2", ImaFile(r, 3, 2, 30, 1), 17});
    c.push_back({"ima mono g1 rest only", ImaFile(r, 1, 1, 0, 1), 9});
    c.push_back({"ms mono a128", MsFile(r, 1, 128, 12, 50), 242});
    c.push_back({"ms stereo a256", MsFile(r, 2, 256, 9, 101), 498});
    c.push_back({"ms mono a9", MsFile(r, 1, 9, 100, 0), 6});
    c.push_back({"ms mono custom coef", MsFile(r, 1, 64, 20, 30, true), 116});
    c.push_back({"ms stereo custom coef", MsFile(r, 2, 40, 25, 15, true), 56});
    // RF64 carrying a 16-bit stereo file and an IMA file
    { Bytes d = RandomBytes(r, 4 * 3000);
      c.push_back({"rf64 pcm16 stereo", File("RF64", {Ck("ds64", Ds64(d.size())), Ck("fmt ", FmtBody(1, 2, 22050, 4, 16)), Ck("data", d, 0xFFFFFFFFll)}), 0}); }
    return c;
}

static void TestStreamEqualsClip() {
    int files = 0;
    for (const Case& cs : AllCases()) {
        wav::WavAudio ref; std::string e;
        const bool ok = DecodeClip(cs.file, ref, &e);
        CHECK(ok);
        if (!ok) { std::fprintf(stderr, "  %s: %s\n", cs.name.c_str(), e.c_str()); continue; }
        ++files;
        const std::size_t ch = std::size_t(ref.channels), total = ref.FrameCount();
        CHECK(total > 0);
        for (std::size_t rs : {std::size_t(1), std::size_t(3), std::size_t(64), std::size_t(1000), std::size_t(4096), std::size_t(1) << 20}) {
            if (rs == 1 && total > 6000) continue;
            std::vector<float> st; wav::WavMeta m; std::uint64_t tot = 0;
            CHECK(StreamAll(cs.file, rs, st, &m, &tot));
            const bool eq = st.size() == ref.samples.size() && Same(st, ref.samples.data(), ref.samples.size());
            CHECK(eq);
            if (!eq) std::fprintf(stderr, "  %s: read size %zu differs (%zu vs %zu floats)\n", cs.name.c_str(), rs, st.size(), ref.samples.size());
            CHECK(tot == total && m.channels == ref.channels && m.sampleRate == ref.sampleRate && m.encoding == ref.encoding && m.bitsPerSample == ref.bitsPerSample);
            CHECK(m.warnings.size() <= ref.warnings.size());
        }
        // seeks: block edges, ends, clamp, then random seeks each followed by a read of a random length
        wav::WavStream s;
        CHECK(s.Open(AudioByteSource::FromMemory(Mem(cs.file))));
        std::vector<std::uint64_t> where = {0, 1, total / 2, total - 1, total, total + 77};
        if (cs.spb) for (std::uint64_t k : {std::uint64_t(1), std::uint64_t(2), std::uint64_t(3)}) { if (k * cs.spb <= total) { where.push_back(k * cs.spb - 1); where.push_back(k * cs.spb); where.push_back(k * cs.spb + 1); } }
        Rng rr(777 + std::uint32_t(files));
        for (int i = 0; i < 60; ++i) where.push_back(rr.Next() % (total + 1));
        std::vector<float> buf(900 * ch);
        int seekOk = 0;
        for (std::uint64_t w : where) {
            const std::uint64_t at = std::min<std::uint64_t>(w, total);
            CHECK(s.Seek(w) && s.Position() == at);
            const std::size_t want = 1 + rr.Next() % 900;
            const std::size_t n = s.Read(buf.data(), want);
            const std::size_t expect = std::size_t(std::min<std::uint64_t>(want, total - at));
            const bool good = n == expect && std::memcmp(buf.data(), ref.samples.data() + at * ch, n * ch * sizeof(float)) == 0;
            CHECK(good);
            if (good) ++seekOk; else std::fprintf(stderr, "  %s: seek to %llu failed\n", cs.name.c_str(), (unsigned long long)w);
        }
        CHECK(seekOk == int(where.size()));
        // after all that the rest still reads in order from the current position
        CHECK(s.MemoryBytes() < 1500000);
    }
    std::printf("  streamed == whole clip: %d files\n", files);
    CHECK(files == int(AllCases().size()));
}

// ---- every truncation: streamed and whole decode agree --------------------------------------------------------
static void TestTruncation() {
    Rng r(99);
    std::vector<Bytes> files = {
        Wav(FmtBody(1, 2, 8000, 4, 16), RandomBytes(r, 40)), Wav(FmtBody(3, 1, 8000, 4, 32), RandomBytes(r, 32)),
        Wav(FmtBody(6, 1, 8000, 1, 8), RandomBytes(r, 20)), Wav(FmtBody(7, 2, 8000, 2, 8), RandomBytes(r, 20)),
        ImaFile(r, 1, 1, 3, 1), ImaFile(r, 2, 1, 2, 0), MsFile(r, 1, 11, 3, 9), MsFile(r, 2, 16, 2, 10, true),
        File("RF64", {Ck("ds64", Ds64(16)), Ck("fmt ", FmtBody(1, 1, 8000, 2, 16)), Ck("data", RandomBytes(r, 16), 0xFFFFFFFFll),
                      Ck("smpl", SmplBody({{1, 0, 2, 5, 0, 0}}, 1)), Ck("cue ", CueBody({{1, 0, 0, 0, 3}}, 1))}),
        File("RIFF", {Ck("fmt ", FmtBody(1, 1, 8000, 2, 16)), Ck("LIST", Bytes(7, 9)), Ck("data", RandomBytes(r, 10)), Ck("smpl", SmplBody({{1, 0, 2, 5, 0, 0}}, 1))}),
    };
    long runs = 0, bothOk = 0;
    for (const Bytes& full : files) {
        for (std::size_t len = 0; len <= full.size(); ++len) {
            Bytes cut(full.begin(), full.begin() + std::ptrdiff_t(len));
            wav::WavAudio ref; std::string e1;
            const bool refOk = DecodeClip(cut, ref, &e1);
            wav::WavStream s; std::string e2;
            const bool opened = s.Open(AudioByteSource::FromMemory(Mem(cut)), &e2);
            std::vector<float> st;
            bool streamOk = opened;
            if (opened) {
                std::vector<float> buf(17 * 8);
                for (;;) { const std::size_t n = s.Read(buf.data(), 17); if (!n) break; st.insert(st.end(), buf.begin(), buf.begin() + std::ptrdiff_t(n * std::size_t(s.Info().channels))); }
                streamOk = !s.Failed();
            }
            ++runs;
            CHECK(refOk == streamOk);
            if (refOk != streamOk) std::fprintf(stderr, "  truncation at %zu: clip %d stream %d (%s | %s)\n", len, refOk, streamOk, e1.c_str(), e2.c_str());
            if (refOk && streamOk) {
                ++bothOk;
                CHECK(st.size() == ref.samples.size() && Same(st, ref.samples.data(), ref.samples.size()));
                CHECK(s.TotalFrames() == ref.FrameCount() && s.Info().cues.size() == ref.cues.size() && s.Info().sampler.loops.size() == ref.sampler.loops.size());
            }
            if (!refOk && !opened) CHECK(e1 == e2);   // the same refusal
        }
    }
    std::printf("  truncations: %ld runs, %ld decoded by both\n", runs, bothOk);
    CHECK(bothOk > 100);
}

// ---- hostile sizes and the budgets ---------------------------------------------------------------------------
static void TestHostile() {
    const Bytes fmt = FmtBody(1, 1, 8000, 2, 16);
    wav::WavAudio a; std::string e;
    // a chunk size past the end of the file in front of the data: nothing found, a clean refusal on both paths
    for (std::int64_t sz : {std::int64_t(0xFFFFFFFF), std::int64_t(0xFFFFFFF0), std::int64_t(0x7FFFFFFF), std::int64_t(0x80000001)}) {
        Bytes w = File("RIFF", {Ck("fmt ", fmt), Ck("LIST", Bytes(4, 1), sz), Ck("data", Bytes(8, 1))});
        CHECK(!DecodeClip(w, a, &e) && e == "missing data chunk");
        wav::WavStream s; CHECK(!s.Open(AudioByteSource::FromMemory(Mem(w)), &e) && e == "missing data chunk");
        // the fmt chunk itself claiming more than the file
        Bytes w2 = File("RIFF", {Ck("fmt ", fmt, sz), Ck("data", Bytes(8, 1))});
        CHECK(!DecodeClip(w2, a, &e));
        wav::WavStream s2; CHECK(!s2.Open(AudioByteSource::FromMemory(Mem(w2)), &e));
        // the data chunk itself: cut at the end of the file with a warning (streaming writers)
        Bytes w3 = File("RIFF", {Ck("fmt ", fmt), Ck("data", Bytes(8, 1), sz)});
        CHECK(DecodeClip(w3, a, &e) && a.samples.size() == 4 && !a.warnings.empty());
        std::vector<float> st; CHECK(StreamAll(w3, 3, st) && st == a.samples);
    }
    // limits: channels, rate, chunks, input size
    Bytes three = Wav(FmtBody(1, 3, 8000, 6, 16), Bytes(60, 3));
    { wav::Limits l; l.maxChannels = 2; CHECK(!DecodeClip(three, a, &e, &l) && e == "too many channels");
      wav::WavStream s; CHECK(!s.Open(AudioByteSource::FromMemory(Mem(three)), &e, &l) && e == "too many channels"); }
    { wav::Limits l; l.maxSampleRate = 4000; wav::WavStream s; CHECK(!s.Open(AudioByteSource::FromMemory(Mem(three)), &e, &l) && e == "sample rate out of range"); }
    { wav::Limits l; l.maxInputBytes = 50; wav::WavStream s; CHECK(!s.Open(AudioByteSource::FromMemory(Mem(three)), &e, &l) && e == "input larger than the size budget"); }
    { std::vector<Bytes> ch = {Ck("fmt ", fmt)}; for (int i = 0; i < 20; ++i) ch.push_back(Ck("junk", Bytes(2, 0))); ch.push_back(Ck("data", Bytes(8, 1)));
      Bytes w = File("RIFF", ch);
      CHECK(DecodeClip(w, a));
      wav::Limits l; l.maxChunks = 10;
      CHECK(!DecodeClip(w, a, &e, &l) && e == "too many chunks");
      wav::WavStream s; CHECK(!s.Open(AudioByteSource::FromMemory(Mem(w)), &e, &l) && e == "too many chunks"); }
    // the block budget: a 64 KB ADPCM block decodes to about 131,000 floats; a budget of 1000 refuses it before decoding anything
    {
        Rng r(5);
        Bytes w = ImaFile(r, 1, 100, 2, -1);   // align 404 -> 801 frames per block
        wav::Limits l; l.maxOutputSamples = 500;
        CHECK(!DecodeClip(w, a, &e, &l) && !e.empty());
        wav::WavStream s; CHECK(!s.Open(AudioByteSource::FromMemory(Mem(w)), &e, &l) && e == "ADPCM block over the budget");
        wav::Limits ok; ok.maxOutputSamples = 801;
        wav::WavStream s2; CHECK(s2.Open(AudioByteSource::FromMemory(Mem(w)), &e, &ok));
        // the whole-clip total budget: 1602 frames do not fit 801, the stream (which never holds the total) still plays it all
        CHECK(!DecodeClip(w, a, &e, &ok) && e == "decoded size over the budget");
        std::vector<float> buf(801); std::size_t tot = 0, n;
        while ((n = s2.Read(buf.data(), 801)) > 0) tot += n;
        CHECK(tot == 1602 && !s2.Failed());
    }
    // 6000 random byte mutations of valid files: streamed and whole decode must agree (same verdict, same samples)
    {
        Rng r(2024);
        std::vector<Bytes> base;
        for (const Case& cs : AllCases()) if (cs.file.size() < 2000 || cs.name.find("g1") != std::string::npos || cs.name.find("a9") != std::string::npos) base.push_back(cs.file);
        Rng g(6);
        base.push_back(ImaFile(g, 2, 2, 3, 1)); base.push_back(MsFile(g, 2, 24, 3, 11, true));
        base.push_back(File("RF64", {Ck("ds64", Ds64(16, {{"junk", 4}})), Ck("junk", Bytes(4, 1), 0xFFFFFFFFll), Ck("fmt ", FmtBody(1, 1, 8000, 2, 16)),
                                     Ck("data", Bytes(16, 2), 0xFFFFFFFFll), Ck("smpl", SmplBody({{1, 0, 2, 5, 0, 0}}, 1))}));
        int both = 0, agree = 0, total = 0;
        for (int i = 0; i < 6000; ++i) {
            Bytes m = base[r.Next() % base.size()];
            if (m.size() > 400) m.resize(400 + r.Next() % 200 < m.size() ? 400 + r.Next() % 200 : m.size());
            const int hits = 1 + int(r.Next() % 4);
            for (int h = 0; h < hits; ++h) { const std::size_t at = r.Next() % std::min<std::size_t>(m.size(), 120); m[at] = (unsigned char)r.Byte(); }
            wav::WavAudio ref; std::string e1;
            const bool refOk = DecodeClip(m, ref, &e1);
            wav::WavStream st; std::string e2; std::vector<float> got;
            bool sok = st.Open(AudioByteSource::FromMemory(Mem(m)), &e2);
            if (sok) { std::vector<float> b(31 * 40); std::size_t n; while ((n = st.Read(b.data(), 31)) > 0) got.insert(got.end(), b.begin(), b.begin() + std::ptrdiff_t(n * std::size_t(st.Info().channels))); sok = !st.Failed(); }
            ++total;
            const bool same = refOk == sok && (!refOk || (got.size() == ref.samples.size() && Same(got, ref.samples.data(), ref.samples.size())));
            CHECK(same);
            if (same) ++agree;
            if (refOk) ++both;
        }
        std::printf("  mutations: %d, both decoded %d, agree %d\n", total, both, agree);
        CHECK(both > 500);
    }
    // a stream that is closed or never opened is harmless
    { wav::WavStream s; float f[4]; CHECK(!s.IsOpen() && s.Read(f, 4) == 0 && !s.Seek(0) && s.TotalFrames() == 0);
      std::string e2; CHECK(!s.Open(nullptr, &e2)); }
}

// A byte source that makes up a huge file: a real header at the front, zeros after, and optionally fewer real bytes than it claims.
class HugeSource : public AudioByteSource {
public:
    HugeSource(Bytes head, std::uint64_t size, std::uint64_t readable) : head_(std::move(head)), size_(size), readable_(readable) {}
    std::uint64_t Size() const override { return size_; }
    std::size_t Read(std::uint64_t off, void* dst, std::size_t n) override {
        if (off >= readable_) return 0;
        n = std::size_t(std::min<std::uint64_t>(n, readable_ - off));
        unsigned char* d = static_cast<unsigned char*>(dst);
        for (std::size_t i = 0; i < n; ++i) d[i] = (off + i < head_.size()) ? head_[std::size_t(off + i)] : (unsigned char)((off + i) & 0x7F);
        return n;
    }
private:
    Bytes head_; std::uint64_t size_, readable_;
};

static void TestHuge() {
    // 16-bit mono, data chunk of 0x7FFFFF00 bytes (1.07 billion frames: far over the clip budget of 2^27 samples)
    Bytes head = File("RIFF", {Ck("fmt ", FmtBody(1, 1, 8000, 2, 16))});
    Tag(head, "data"); P32(head, 0x7FFFFF00u);
    const std::uint64_t size = head.size() + 0x7FFFFF00ull;
    wav::WavStream s; std::string e;
    wav::Limits big; big.maxInputBytes = std::size_t(1) << 40;
    CHECK(s.Open(std::make_shared<HugeSource>(head, size, size), &e, &big));
    CHECK(s.TotalFrames() == 0x7FFFFF00ull / 2);
    std::vector<float> buf(1000);
    CHECK(s.Read(buf.data(), 1000) == 1000);
    CHECK(s.Seek(s.TotalFrames() - 3) && s.Read(buf.data(), 1000) == 3);
    CHECK(s.MemoryBytes() < 200000);
    // with the default input budget (1 GiB) the file is refused up front
    wav::WavStream s2; CHECK(!s2.Open(std::make_shared<HugeSource>(head, size, size), &e) && e == "input larger than the size budget");
    // RF64: 5 GB of data through ds64 (a size over 2^32), seek past 2^31 frames, exact positions
    {
        Bytes h = File("RF64", {Ck("ds64", Ds64(5000000000ull)), Ck("fmt ", FmtBody(1, 1, 8000, 2, 16))});
        Tag(h, "data"); P32(h, 0xFFFFFFFFu);
        const std::uint64_t sz = h.size() + 5000000000ull;
        wav::WavStream t;
        CHECK(t.Open(std::make_shared<HugeSource>(h, sz, sz), &e, &big));
        CHECK(t.TotalFrames() == 2500000000ull && t.Info().rf64);
        CHECK(t.Seek(2400000000ull) && t.Position() == 2400000000ull);
        CHECK(t.Read(buf.data(), 4) == 4);
        const std::uint64_t byteOff = h.size() + 2400000000ull * 2;   // the first byte of frame 2.4e9
        CHECK(buf[0] == float(std::int16_t(((byteOff & 0x7F) | (((byteOff + 1) & 0x7F) << 8)))) / 32768.0f);
        CHECK(t.Seek(~std::uint64_t(0)) && t.Position() == 2500000000ull && t.Read(buf.data(), 4) == 0);
    }
    // a source that has fewer bytes than it claims (a file that shrank): the stream ends, no crash, PCM and ADPCM
    {
        Rng r(3);
        Bytes w = ImaFile(r, 1, 3, 10, -1);
        auto src = std::make_shared<HugeSource>(w, w.size(), w.size() - 50);   // the last block is partly gone
        wav::WavStream t;
        CHECK(t.Open(src, &e));
        std::vector<float> b(100); std::size_t tot = 0, n;
        while ((n = t.Read(b.data(), 100)) > 0) tot += n;
        CHECK(tot < t.TotalFrames() && tot > 0 && !t.Failed());
        Bytes p = Wav(FmtBody(1, 1, 8000, 2, 16), Bytes(2000, 1));
        wav::WavStream u;
        CHECK(u.Open(std::make_shared<HugeSource>(p, p.size(), p.size() - 700), &e));
        tot = 0; std::vector<float> b2(300);
        while ((n = u.Read(b2.data(), 300)) > 0) tot += n;
        CHECK(tot == (2000 - 700 + 44 - 44) / 2 || tot > 0);   // whatever is left, whole frames only
        CHECK(tot <= 1000 && tot >= 600);
    }
}

// ---- AudioStream on top of it -------------------------------------------------------------------------------
static void TestAudioStream() {
    SetEnv("1");
    CHECK(wav::OwnWavEnabled());
    int n = 0;
    for (const Case& cs : AllCases()) {
        wav::WavAudio ref;
        if (!DecodeClip(cs.file, ref)) continue;
        std::string e;
        auto s = AudioStream::OpenMemory(Mem(cs.file), &e);
        CHECK(s != nullptr);
        if (!s) { std::fprintf(stderr, "  %s: %s\n", cs.name.c_str(), e.c_str()); continue; }
        ++n;
        const std::size_t total = ref.FrameCount(), rc = std::size_t(ref.channels), oc = std::size_t(s->Channels());
        CHECK(s->SampleRate() == ref.sampleRate && s->TotalFrames() == std::int64_t(total) && oc == (rc > 2 ? 1u : rc));
        // expected output: the clip itself, or (more than two channels) the mono average the old reader also made
        std::vector<float> want;
        if (rc == oc) want = ref.samples;
        else { const float k = 1.0f / float(rc); for (std::size_t i = 0; i < total; ++i) { float acc = 0; for (std::size_t c = 0; c < rc; ++c) acc += ref.samples[i * rc + c]; want.push_back(acc * k); } }
        std::vector<float> got((total + 1600) * oc, 7.0f);   // room for a misbehaving stream to be caught, not to overflow
        std::size_t done = 0, k;
        while (done <= total && (k = s->Read(got.data() + done * oc, 1 + done % 1500)) > 0) done += k;
        CHECK(done == total && Same(want, got.data(), total * oc));
        // seek, then read
        Rng r(1 + std::uint32_t(n));
        for (int i = 0; i < 8; ++i) {
            const std::int64_t at = std::int64_t(r.Next() % total);
            CHECK(s->Seek(at) && s->Position() == at);
            const std::size_t cnt = std::min<std::size_t>(total - std::size_t(at), 1 + r.Next() % 3000);
            CHECK(s->Read(got.data(), cnt) == cnt && std::memcmp(got.data(), want.data() + std::size_t(at) * oc, cnt * oc * sizeof(float)) == 0);
        }
        // loop: three laps are one timeline, no gap and no repeat of the join
        s->loop = true;
        CHECK(s->Seek(0));
        std::vector<float> lap(total * oc * 3);
        std::size_t lf = 0;
        while (lf < total * 3 && (k = s->Read(lap.data() + lf * oc, std::min<std::size_t>(total * 3 - lf, 777))) > 0) lf += k;
        CHECK(lf == total * 3);
        bool loopOk = lf == total * 3;
        for (int l = 0; l < 3 && loopOk; ++l) loopOk = std::memcmp(lap.data() + std::size_t(l) * total * oc, want.data(), total * oc * sizeof(float)) == 0;
        CHECK(loopOk);
        if (!loopOk) std::fprintf(stderr, "  %s: loop differs\n", cs.name.c_str());
        CHECK(s->MemoryBytes() < 1800000);
    }
    std::printf("  AudioStream (own path): %d files\n", n);
    CHECK(n == int(AllCases().size()));
    // a ten minute IMA file stays small and seeks fast
    {
        Rng r(8);
        Bytes w = ImaFile(r, 1, 31, 30000, -1);   // 7.5 million frames
        auto s = AudioStream::OpenMemory(Mem(w));
        CHECK(s && s->TotalFrames() == 30000 * 249);
        if (s) {
            std::vector<float> b(5000);
            CHECK(s->Read(b.data(), 5000) == 5000 && s->Seek(7000000) && s->Read(b.data(), 5000) == 5000);
            CHECK(s->MemoryBytes() < 1000000);
        }
    }
    // an RF64 file from disk, OpenFile
    {
        Bytes d; for (int i = 0; i < 200; ++i) P16(d, unsigned(i * 100) & 0xFFFF);
        Bytes w = File("RF64", {Ck("ds64", Ds64(d.size())), Ck("fmt ", FmtBody(1, 1, 8000, 2, 16)), Ck("data", d, 0xFFFFFFFFll)});
        const std::string path = "own_wav_stream_rf64.wav";
        { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(w.data()), std::streamsize(w.size())); }
        std::string e;
        auto s = AudioStream::OpenFile(path, &e);
        CHECK(s && s->TotalFrames() == 200 && s->SampleRate() == 8000);
        float f[3];
        if (s) CHECK(s->Seek(150) && s->Read(f, 3) == 3 && f[0] == 15000.0f / 32768.0f && f[2] == 15200.0f / 32768.0f);
        s.reset();
        std::remove(path.c_str());
    }
    // errors come through with the own path
    { std::string e; Bytes junk = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'x', 'x'}; CHECK(!AudioStream::OpenMemory(Mem(junk), &e) && !e.empty()); }
    SetEnv(nullptr);
}

// ---- off by default: the old path is unchanged ---------------------------------------------------------------
static void TestOldPathUnchanged() {
    SetEnv("0");
    CHECK(!wav::OwnWavEnabled());
    std::vector<std::uint64_t> ownHash;
    for (const auto& g : kGoldenCases) {
        const Golden* gold = nullptr;
        for (const auto& x : kGolden) if (std::strcmp(x.name, g.name) == 0) gold = &x;
        CHECK(gold != nullptr);
        std::uint64_t fr = 0;
        const std::uint64_t h = StreamHash(GoldenFile(g), &fr);
        CHECK(gold && h == gold->hash && fr == gold->frames);
        if (gold && (h != gold->hash || fr != gold->frames)) std::fprintf(stderr, "  golden %s: %016llX / %llu\n", g.name, (unsigned long long)h, (unsigned long long)fr);
    }
    // the old reader does not play ADPCM or RF64: that is exactly what the switch adds
    Rng r(4);
    CHECK(AudioStream::OpenMemory(Mem(ImaFile(r, 1, 3, 3, -1))) == nullptr);
    CHECK(AudioStream::OpenMemory(Mem(MsFile(r, 1, 32, 3, 0))) == nullptr);
    {
        Bytes d(8, 1);
        CHECK(AudioStream::OpenMemory(Mem(File("RF64", {Ck("ds64", Ds64(8)), Ck("fmt ", FmtBody(1, 1, 8000, 2, 16)), Ck("data", d, 0xFFFFFFFFll)}))) == nullptr);
    }
    // with the switch on, the plain PCM files decode to the same bytes as the old path (8, 16, 24 bit and float exactly;
    // 32-bit integers and the 3-channel average to float rounding: different but equivalent arithmetic)
    SetEnv("1");
    for (const auto& g : kGoldenCases) {
        SetEnv("0");
        std::uint64_t f0 = 0, f1 = 0, h0, h1;
        int c0 = 0, c1 = 0;
        std::vector<float> v0, v1;
        h0 = StreamHash(GoldenFile(g), &f0, &c0, &v0);
        SetEnv("1");
        h1 = StreamHash(GoldenFile(g), &f1, &c1, &v1);
        CHECK(f0 == f1 && c0 == c1 && v0.size() == v1.size());
        if (g.bits != 32 && g.ch <= 2) { CHECK(h0 == h1); if (h0 != h1) std::fprintf(stderr, "  own differs from old on %s\n", g.name); }
        else if (v0.size() == v1.size()) for (std::size_t i = 0; i < v0.size(); ++i) CHECK_NEAR(v0[i], v1[i], 2e-7);
    }
    SetEnv(nullptr);
}

int main() {
    RUN_SUITE("own_wav_stream");
    TestHandIma();
    TestHandMs();
    TestHandPcm();
    TestRf64();
    TestMarkers();
    TestStreamEqualsClip();
    TestTruncation();
    TestHostile();
    TestHuge();
    TestAudioStream();
    TestOldPathUnchanged();
    TEST_MAIN_RESULT();
}
#endif
