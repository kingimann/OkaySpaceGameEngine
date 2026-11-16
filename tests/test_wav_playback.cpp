// The WAV playback seam: okay/Audio/WavPlayback.hpp (own decoder, no SDL) and, when SDL2 is present,
// shared/WavPlayback.hpp (SDL_LoadWAV with the option off, the own decoder with it on). The WAV files are built in
// memory here (no WAV fixtures are checked in) and written to a temp file where a path is needed.
#include "test_framework.hpp"
#include "okay/Audio/WavPlayback.hpp"
#include "okay/Audio/WavDecoder.hpp"
#ifdef OKAY_TEST_HAVE_SDL
#include <SDL.h>
#include "WavPlayback.hpp"
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
using Bytes = std::vector<unsigned char>;

static void P16(Bytes& b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void P32(Bytes& b, std::uint32_t v) { P16(b, v & 0xFFFF); P16(b, v >> 16); }
static void Tag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }
static Bytes Wav(unsigned tag, unsigned ch, std::uint32_t rate, unsigned bits, const Bytes& data,
                 std::uint32_t dataField = 0xFFFFFFFEu) {
    const unsigned align = ch * bits / 8;
    Bytes b; Tag(b, "RIFF"); P32(b, 36 + std::uint32_t(data.size())); Tag(b, "WAVE");
    Tag(b, "fmt "); P32(b, 16); P16(b, tag); P16(b, ch); P32(b, rate); P32(b, rate * align); P16(b, align); P16(b, bits);
    Tag(b, "data"); P32(b, dataField == 0xFFFFFFFEu ? std::uint32_t(data.size()) : dataField);
    b.insert(b.end(), data.begin(), data.end());
    return b;
}
static Bytes Pcm16(unsigned ch, std::uint32_t rate, int frames) {
    Bytes d;
    for (int i = 0; i < frames; ++i)
        for (unsigned c = 0; c < ch; ++c) P16(d, unsigned((i * 997 + int(c) * 5000 - 20000) & 0xFFFF));
    return Wav(1, ch, rate, 16, d);
}
static const char* kPath = "wav_playback_seam.wav";
static void Put(const Bytes& b) { std::ofstream f(kPath, std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()), long(b.size())); }
static std::int16_t S16At(const Bytes& b, std::size_t i) { std::int16_t v; std::memcpy(&v, &b[i * 2], 2); return v; }

static void TestOwnPath() {
    wav::PlaybackClip c; std::string err;
    { // PCM16 mono and stereo come back bit for bit
        Bytes w = Pcm16(1, 22050, 100);
        CHECK(wav::DecodeForPlayback(w.data(), w.size(), c, &err));
        CHECK(c.format == wav::PlaybackFormat::S16 && c.channels == 1 && c.sampleRate == 22050 && c.bytes.size() == 200);
        CHECK(std::memcmp(c.bytes.data(), w.data() + 44, 200) == 0);
        Bytes s = Pcm16(2, 44100, 50);
        CHECK(wav::DecodeForPlayback(s.data(), s.size(), c, &err));
        CHECK(c.channels == 2 && c.sampleRate == 44100 && c.bytes.size() == 200);
        CHECK(std::memcmp(c.bytes.data(), s.data() + 44, 200) == 0);
    }
    { // 8 bit: converted to 16 bit (0x80 = silence, 0xFF = +127/128)
        Bytes d = {0x80, 0xFF, 0x00};
        Bytes w = Wav(1, 1, 8000, 8, d);
        CHECK(wav::DecodeForPlayback(w.data(), w.size(), c, &err));
        CHECK(c.format == wav::PlaybackFormat::S16 && c.bytes.size() == 6);
        CHECK(S16At(c.bytes, 0) == 0 && S16At(c.bytes, 1) == 32512 - 0 && S16At(c.bytes, 2) == -32768);
    }
    { // float 32 stays float
        Bytes d; const float f[2] = {0.25f, -0.5f};
        for (float x : f) { std::uint32_t u; std::memcpy(&u, &x, 4); P32(d, u); }
        Bytes w = Wav(3, 1, 16000, 32, d);
        CHECK(wav::DecodeForPlayback(w.data(), w.size(), c, &err));
        CHECK(c.format == wav::PlaybackFormat::F32 && c.bytes.size() == 8);
        float g[2]; std::memcpy(g, c.bytes.data(), 8);
        CHECK(g[0] == 0.25f && g[1] == -0.5f);
    }
    { // 32 bit integer PCM goes to 16 bit
        Bytes d; P32(d, 0x40000000u);
        Bytes w = Wav(1, 1, 8000, 32, d);
        CHECK(wav::DecodeForPlayback(w.data(), w.size(), c, &err));
        CHECK(c.format == wav::PlaybackFormat::S16 && S16At(c.bytes, 0) == 16384);
    }
    { // through a real file
        Bytes w = Pcm16(2, 8000, 10); Put(w);
        CHECK(wav::LoadForPlayback(kPath, c, &err) && c.bytes.size() == 40);
    }
}

static void TestErrors() {
    wav::PlaybackClip c; std::string err;
    Bytes w = Pcm16(2, 44100, 64);
    for (std::size_t n = 0; n < w.size(); n += 3) {   // every few truncations: no crash; result is a clean true or false
        err.clear();
        const bool ok = wav::DecodeForPlayback(w.data(), n, c, &err);
        if (!ok) CHECK(!err.empty() && c.bytes.empty());
        else CHECK(!c.bytes.empty());
    }
    CHECK(!wav::DecodeForPlayback(w.data(), 20, c, &err));           // header cut
    CHECK(!wav::DecodeForPlayback(nullptr, 0, c, &err));
    { // zero length data
        Bytes z = Wav(1, 1, 8000, 16, Bytes{});
        err.clear(); CHECK(!wav::DecodeForPlayback(z.data(), z.size(), c, &err) && !err.empty() && c.bytes.empty());
    }
    { // huge data size in the header with little data: cut at the file end, never reads outside
        Bytes h = Pcm16(1, 8000, 10); h[40] = h[41] = h[42] = 0xFF; h[43] = 0x7F;
        CHECK(wav::DecodeForPlayback(h.data(), h.size(), c, &err) && c.bytes.size() == 20);
        Bytes h2 = Pcm16(1, 8000, 10); h2[40] = h2[41] = h2[42] = h2[43] = 0xFF;
        CHECK(wav::DecodeForPlayback(h2.data(), h2.size(), c, &err) && c.bytes.size() == 20);
    }
    { // absurd channel count and rate
        Bytes a = Wav(1, 200, 8000, 16, Bytes(400, 1)); CHECK(!wav::DecodeForPlayback(a.data(), a.size(), c, &err));
        Bytes r = Wav(1, 1, 99999999u, 16, Bytes(400, 1)); CHECK(!wav::DecodeForPlayback(r.data(), r.size(), c, &err));
    }
    CHECK(!wav::LoadForPlayback("no_such_dir/no_such.wav", c, &err) && !err.empty());
    { std::ofstream f(kPath, std::ios::binary); } // zero-byte file
    CHECK(!wav::LoadForPlayback(kPath, c, &err));
    Put(Bytes{'R', 'I', 'F', 'F', 1, 2, 3, 4, 'W', 'A', 'V', 'E', 'j', 'u', 'n', 'k'});
    CHECK(!wav::LoadForPlayback(kPath, c, &err));
}

// LoadForPlayback reads files over 64 KiB in two buffers and decodes PCM 16/24/32 and float in place. Whatever the
// chunk layout, its result must equal DecodeForPlayback on the same bytes (one buffer, never in place), and the 16-bit
// result must equal ToS16 of the float decode.
static Bytes Chunk(const char* tag, const Bytes& body) {
    Bytes b; Tag(b, tag); P32(b, std::uint32_t(body.size())); b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1) b.push_back(0);
    return b;
}
static Bytes Fmt(unsigned tag, unsigned ch, std::uint32_t rate, unsigned align, unsigned bits, const Bytes& extra = {}) {
    Bytes f; P16(f, tag); P16(f, ch); P32(f, rate); P32(f, rate * align); P16(f, align); P16(f, bits);
    if (!extra.empty()) { P16(f, unsigned(extra.size())); f.insert(f.end(), extra.begin(), extra.end()); }
    return f;
}
static Bytes Riff(const std::vector<Bytes>& chunks) {
    Bytes b; Tag(b, "RIFF"); P32(b, 0); Tag(b, "WAVE");
    for (const Bytes& c : chunks) b.insert(b.end(), c.begin(), c.end());
    const std::uint32_t n = std::uint32_t(b.size() - 8); std::memcpy(&b[4], &n, 4);
    return b;
}
static Bytes Noise(std::size_t n, std::uint32_t seed) {
    Bytes d(n);
    for (auto& x : d) { seed = seed * 1664525u + 1013904223u; x = (unsigned char)(seed >> 24); }
    return d;
}
static void SameAsOneBuffer(const Bytes& w, bool expectOk) {
    Put(w);
    wav::PlaybackClip a, b; std::string ea, eb;
    const bool oka = wav::LoadForPlayback(kPath, a, &ea);
    const bool okb = wav::DecodeForPlayback(w.data(), w.size(), b, &eb);
    CHECK(oka == expectOk && okb == expectOk);
    CHECK(ea == eb);
    CHECK(a.format == b.format && a.channels == b.channels && a.sampleRate == b.sampleRate && a.bytes == b.bytes);
    if (expectOk && a.format == wav::PlaybackFormat::S16) {
        wav::WavAudio f; CHECK(wav::DecodeWav(w.data(), w.size(), f));
        const std::vector<std::int16_t> s = wav::ToS16(f);
        CHECK(s.size() * 2 == a.bytes.size() && std::memcmp(s.data(), a.bytes.data(), a.bytes.size()) == 0);
    }
}
static void TestLargeFiles() {
    const Bytes pcm = Noise(300000, 1);
    // plain, with chunks before and after the data, with fmt after data, and with a data chunk past the first 64 KiB
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(1, 2, 44100, 4, 16)), Chunk("data", pcm)}), true);
    SameAsOneBuffer(Riff({Chunk("LIST", Noise(301, 2)), Chunk("fmt ", Fmt(1, 2, 44100, 4, 16)), Chunk("data", pcm),
                          Chunk("junk", Noise(77, 3))}), true);
    SameAsOneBuffer(Riff({Chunk("data", pcm), Chunk("fmt ", Fmt(1, 2, 44100, 4, 16))}), true);
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(1, 2, 44100, 4, 16)), Chunk("LIST", Noise(70000, 4)), Chunk("data", pcm)}), true);
    // odd sizes: half a frame at the end, a data size past the end of the file, an odd block align with padding
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(1, 2, 44100, 4, 16)), Chunk("data", Noise(300003, 5))}), true);
    { Bytes w = Riff({Chunk("fmt ", Fmt(1, 2, 44100, 4, 16)), Chunk("data", pcm)}); w.resize(w.size() - 1001); SameAsOneBuffer(w, true); }
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(1, 2, 44100, 6, 16)), Chunk("data", pcm)}), true);
    // every in-place width: 24 bit (all byte patterns appear), 32 bit, float32 (with NaN and infinity), float64
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(1, 2, 48000, 6, 24)), Chunk("data", Noise(600000, 6))}), true);
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(1, 1, 48000, 4, 32)), Chunk("data", Noise(400000, 7))}), true);
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(3, 2, 48000, 8, 32)), Chunk("data", Noise(400000, 8))}), true);
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(3, 2, 48000, 16, 64)), Chunk("data", Noise(400000, 9))}), true);
    // widening formats (not in place): 8 bit, A-law, MS ADPCM with the standard table, IMA ADPCM
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(1, 2, 22050, 2, 8)), Chunk("data", Noise(200000, 10))}), true);
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(6, 1, 8000, 1, 8, Bytes{})), Chunk("data", Noise(200000, 11))}), true);
    {
        Bytes d = Noise(256 * 400, 12);
        for (std::size_t b = 0; b < d.size(); b += 256) d[b] = d[b] % 7;   // a valid predictor index in every block
        Bytes ex; P16(ex, 500); P16(ex, 0);
        SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(2, 1, 22050, 256, 4, ex)), Chunk("data", d)}), true);
        d[256 * 300] = 9;   // a bad predictor index in block 300: refused, by both, with the same message
        SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(2, 1, 22050, 256, 4, ex)), Chunk("data", d)}), false);
    }
    {
        Bytes d = Noise(1024 * 200, 13);
        Bytes ex; P16(ex, 2041);
        SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(0x11, 2, 44100, 1024, 4, ex)), Chunk("data", d)}), true);
    }
    // the header read in two steps (data chunk after the first 512 bytes), small files, 8-bit with fmt after data
    SameAsOneBuffer(Riff({Chunk("LIST", Noise(1000, 14)), Chunk("fmt ", Fmt(1, 1, 8000, 1, 8)), Chunk("data", Noise(5000, 15))}), true);
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(7, 2, 8000, 2, 8)), Chunk("data", Noise(3001, 16)), Chunk("junk", Noise(9, 17))}), true);
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(1, 1, 8000, 2, 16)), Chunk("data", Noise(300, 18))}), true);
    SameAsOneBuffer(Riff({Chunk("data", Noise(200000, 19)), Chunk("fmt ", Fmt(1, 2, 22050, 2, 8))}), true);
    // refused large files: unknown tag, no fmt chunk
    SameAsOneBuffer(Riff({Chunk("fmt ", Fmt(0x55, 2, 44100, 4, 16)), Chunk("data", pcm)}), false);
    SameAsOneBuffer(Riff({Chunk("data", pcm)}), false);
}

#ifdef OKAY_TEST_HAVE_SDL
// Own switch on versus off through the launcher's seam: PCM16 mono and stereo must be identical, and errors must fail
// on both sides without a crash.
static void TestSdlSeam() {
    const Bytes files[] = {Pcm16(1, 22050, 300), Pcm16(2, 44100, 300)};
    for (const Bytes& w : files) {
        Put(w);
        SDL_AudioSpec so{}, sn{}; std::vector<Uint8> bo, bn; std::string err;
        setenv("OKAY_OWN_WAV", "0", 1);
        CHECK(wav::LoadWavForSdl(kPath, so, bo, &err));
        setenv("OKAY_OWN_WAV", "1", 1);
        CHECK(wav::LoadWavForSdl(kPath, sn, bn, &err));
        CHECK(so.format == AUDIO_S16SYS && sn.format == AUDIO_S16SYS);
        CHECK(so.channels == sn.channels && so.freq == sn.freq);
        CHECK(bo == bn && !bo.empty());
    }
    Bytes cut = Pcm16(2, 44100, 64); cut.resize(30);
    const Bytes bad[] = {cut, Wav(1, 1, 8000, 16, Bytes{}),
                         Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'}};
    for (int mode = 0; mode < 2; ++mode) {
        setenv("OKAY_OWN_WAV", mode ? "1" : "0", 1);
        for (const Bytes& b : bad) {
            Put(b);
            SDL_AudioSpec s{}; std::vector<Uint8> out;
            const bool ok = wav::LoadWavForSdl(kPath, s, out);
            if (mode == 1) CHECK(!ok);   // the own decoder refuses all three
            if (!ok) CHECK(out.empty());
        }
    }
    unsetenv("OKAY_OWN_WAV");
}
#endif

int main() {
    TestOwnPath();
    TestErrors();
    TestLargeFiles();
#ifdef OKAY_TEST_HAVE_SDL
    TestSdlSeam();
#endif
    std::remove(kPath);
    TEST_MAIN_RESULT();
}
