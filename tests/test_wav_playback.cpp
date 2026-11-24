// The WAV playback seam: okay/Audio/WavPlayback.hpp (own decoder, no SDL) and, when SDL2 is present,
// shared/WavPlayback.hpp (the launcher's seam: our own decoder into an SDL_AudioSpec; SDL_LoadWAV was deleted). The WAV files are built in
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

#ifdef OKAY_TEST_HAVE_SDL
// The launcher's seam: PCM16 mono and stereo come back bit for bit as the file's data chunk (what SDL_LoadWAV gave when
// the two were still compared here), and damaged files fail without a crash and leave nothing behind.
static void TestSdlSeam() {
    const Bytes files[] = {Pcm16(1, 22050, 300), Pcm16(2, 44100, 300)};
    const unsigned chans[] = {1, 2};
    const int rates[] = {22050, 44100};
    for (int k = 0; k < 2; ++k) {
        const Bytes& w = files[k];
        Put(w);
        SDL_AudioSpec sn{}; std::vector<Uint8> bn; std::string err;
        setenv("OKAY_OWN_WAV", "0", 1);   // left over from the deleted switch: must change nothing
        CHECK(wav::LoadWavForSdl(kPath, sn, bn, &err));
        unsetenv("OKAY_OWN_WAV");
        CHECK(sn.format == AUDIO_S16SYS && sn.channels == chans[k] && sn.freq == rates[k]);
        CHECK(bn.size() == w.size() - 44 && std::memcmp(bn.data(), w.data() + 44, bn.size()) == 0);
    }
    Bytes cut = Pcm16(2, 44100, 64); cut.resize(30);
    const Bytes bad[] = {cut, Wav(1, 1, 8000, 16, Bytes{}),
                         Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'}};
    for (const Bytes& b : bad) {
        Put(b);
        SDL_AudioSpec s{}; std::vector<Uint8> out; std::string err;
        CHECK(!wav::LoadWavForSdl(kPath, s, out, &err));   // the own decoder refuses all three
        CHECK(out.empty() && !err.empty());
    }
}
#endif

int main() {
    TestOwnPath();
    TestErrors();
#ifdef OKAY_TEST_HAVE_SDL
    TestSdlSeam();
#endif
    std::remove(kPath);
    TEST_MAIN_RESULT();
}
