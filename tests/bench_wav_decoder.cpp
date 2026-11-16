// Speed and memory of our own WAV decoder (okay::wav) against SDL_LoadWAV, on the same files.
//
// Run:  bench_wav_decoder [--runs N] [--gen DIR] [--digest] [--only WHAT] [file.wav | directory] ...
//       With no file argument it reads the repository's WAV files (tests/data/audio/wav_ffmpeg, examples/) and the
//       generated set in DIR (default: <temp>/okay_wav_bench), writing that set first if a file is missing.
//
// The generated set (our own writer below, 44.1 kHz stereo unless named otherwise): 16-bit PCM at 1 s, 60 s and
// 10 min; 60 s each of 8-bit, 24-bit and 32-bit PCM, float32, float64, A-law, mu-law, MS ADPCM (2048-byte blocks),
// IMA ADPCM (2048-byte blocks), and WAVE_FORMAT_EXTENSIBLE 16-bit 6-channel 48 kHz. PCM and float carry a tone plus
// noise; the A-law, mu-law and ADPCM payloads are pseudo-random bytes behind valid block headers (decoding speed does
// not depend on the values).
//
// For every file, each as the median wall time over at least N runs (default 7; small files run more often, until
// about 50 ms have passed), and the PEAK heap bytes the call needed above what was allocated before it:
//   own-raw     read the file + okay::wav::DecodeWav          (our decoder's own output: interleaved float)
//   sdl-raw     SDL_LoadWAV + SDL_FreeWAV                      (SDL's own output format)
//   own-final   okay::wav::LoadWavForSdl with OKAY_OWN_WAV=1, then the launcher's VoiceClipToPlayable
//   sdl-final   okay::wav::LoadWavForSdl with OKAY_OWN_WAV=0 (SDL_LoadWAV, copy, SDL_FreeWAV), then VoiceClipToPlayable
// "final" is what the launcher's voice-clip load (launcher/main.cpp, VoiceClipFor) ends with: native 16-bit, or
// native float for float32 files. VoiceClipToPlayable is copied from launcher/main.cpp (our own code) because it is a
// static function there.
//
// The heap is counted by replacing malloc / free / realloc / calloc / memalign in this program (glibc only: they
// forward to glibc's own __libc_* functions), so SDL's C allocations and our C++ `new` are counted the same way. On
// other C libraries the memory columns print "-".
//
// Not part of ctest: it prints numbers, it does not check them. Correctness is checked by own_wav, wav_playback,
// wav_ffmpeg_diff and audio_formats. `--digest` prints, per file, a hash of our decoder's float output (DecodeWav,
// bit for bit, with its warnings) and of our playback bytes (DecodeForPlayback and LoadForPlayback), so a speed change
// can be shown not to change the output (run before and after, compare the lines). `--only own-raw|sdl-raw|own-final|
// sdl-final` runs just that, once per file, for a profiler (valgrind --tool=callgrind).
// Written for this project; the counting allocator, the WAV writer and the statistics are our own.
#include <SDL.h>
#include "okay/Audio/WavDecoder.hpp"
#include "okay/Audio/WavPlayback.hpp"
#include "WavPlayback.hpp"   // shared/WavPlayback.hpp: the launcher's seam

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
#include <malloc.h>
#define OKAY_BENCH_COUNT_HEAP 1
#endif

// ---- heap counting ------------------------------------------------------------------------------------------------
namespace {
std::atomic<long long> g_cur{0};
std::atomic<long long> g_peak{0};
inline void HeapAdd(void* p) {
#ifdef OKAY_BENCH_COUNT_HEAP
    if (!p) return;
    const long long n = (long long)malloc_usable_size(p);
    const long long now = g_cur.fetch_add(n, std::memory_order_relaxed) + n;
    long long pk = g_peak.load(std::memory_order_relaxed);
    while (now > pk && !g_peak.compare_exchange_weak(pk, now, std::memory_order_relaxed)) {}
#else
    (void)p;
#endif
}
inline void HeapSub(void* p) {
#ifdef OKAY_BENCH_COUNT_HEAP
    if (p) g_cur.fetch_sub((long long)malloc_usable_size(p), std::memory_order_relaxed);
#else
    (void)p;
#endif
}
} // namespace

#ifdef OKAY_BENCH_COUNT_HEAP
extern "C" {
void* __libc_malloc(size_t);
void __libc_free(void*);
void* __libc_realloc(void*, size_t);
void* __libc_calloc(size_t, size_t);
void* __libc_memalign(size_t, size_t);

void* malloc(size_t n) { void* p = __libc_malloc(n); HeapAdd(p); return p; }
void free(void* p) { HeapSub(p); __libc_free(p); }
void* calloc(size_t a, size_t b) { void* p = __libc_calloc(a, b); HeapAdd(p); return p; }
void* realloc(void* old, size_t n) {
    const long long before = old ? (long long)malloc_usable_size(old) : 0;
    void* p = __libc_realloc(old, n);
    if (!p) return p;
    g_cur.fetch_sub(before, std::memory_order_relaxed);
    HeapAdd(p);
    return p;
}
void* memalign(size_t a, size_t n) { void* p = __libc_memalign(a, n); HeapAdd(p); return p; }
void* aligned_alloc(size_t a, size_t n) { void* p = __libc_memalign(a, n); HeapAdd(p); return p; }
int posix_memalign(void** out, size_t a, size_t n) {
    void* p = __libc_memalign(a, n);
    if (!p) return 12;   // ENOMEM
    HeapAdd(p); *out = p; return 0;
}
}
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct Measure { double ms = -1; long long peak = -1; bool ok = false; };

// Median time of `fn` over at least `runs` runs (more for fast calls, until 50 ms or 400 runs), then one more run
// with the heap peak recorded.
Measure Run(int runs, const std::function<bool()>& fn) {
    Measure m;
    std::vector<double> t;
    double total = 0;
    for (int k = 0; k < 400 && ((int)t.size() < runs || total < 50.0); ++k) {
        const auto a = Clock::now();
        const bool ok = fn();
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count();
        if (!ok) return m;
        t.push_back(ms); total += ms;
    }
    std::sort(t.begin(), t.end());
    m.ms = t.size() % 2 ? t[t.size() / 2] : 0.5 * (t[t.size() / 2 - 1] + t[t.size() / 2]);
    m.ok = true;
#ifdef OKAY_BENCH_COUNT_HEAP
    const long long base = g_cur.load();
    g_peak.store(base);
    fn();
    m.peak = g_peak.load() - base;
#endif
    return m;
}

// ---- the launcher's conversion after the load (copied from launcher/main.cpp, VoiceClipToPlayable) ----------------
struct VoiceClip { SDL_AudioSpec spec{}; Uint8* pcm = nullptr; Uint32 len = 0; std::vector<Uint8> owned; };
void VoiceClipToPlayable(VoiceClip& c) {
    const SDL_AudioFormat f = c.spec.format;
    if (f == AUDIO_S16SYS || f == AUDIO_F32SYS || !c.pcm || !c.len) return;
    const int width = SDL_AUDIO_BITSIZE(f) / 8;
    if (width < 1 || width > 4) { c.len = 0; return; }
    const std::size_t n = c.len / (std::size_t)width;
    std::vector<Uint8> out(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        const Uint8* p = c.pcm + i * (std::size_t)width;
        float v = 0.0f;
        if (SDL_AUDIO_ISFLOAT(f)) {
            Uint32 u = 0;
            for (int b = 0; b < 4; ++b) u |= (Uint32)p[SDL_AUDIO_ISBIGENDIAN(f) ? 3 - b : b] << (8 * b);
            std::memcpy(&v, &u, 4);
        } else if (width == 1) {
            v = SDL_AUDIO_ISSIGNED(f) ? (float)(Sint8)p[0] / 128.0f : ((float)p[0] - 128.0f) / 128.0f;
        } else {
            Uint32 u = 0;
            for (int b = 0; b < width; ++b) u |= (Uint32)p[SDL_AUDIO_ISBIGENDIAN(f) ? width - 1 - b : b] << (8 * b);
            const long long top = 1ll << (8 * width - 1);
            long long sv = SDL_AUDIO_ISSIGNED(f) ? (long long)(Sint32)(u << (32 - 8 * width)) >> (32 - 8 * width) : (long long)u - top;
            v = (float)sv / (float)top;
        }
        const Sint16 s16 = (Sint16)std::clamp(v * 32767.0f, -32768.0f, 32767.0f);
        std::memcpy(out.data() + i * 2, &s16, 2);
    }
    c.owned = std::move(out);
    c.pcm = c.owned.data(); c.len = (Uint32)c.owned.size();
    c.spec.format = AUDIO_S16SYS;
}
bool LauncherLoad(const std::string& path, bool own) {
    setenv("OKAY_OWN_WAV", own ? "1" : "0", 1);
    VoiceClip c;
    SDL_AudioSpec spec{};
    if (!okay::wav::LoadWavForSdl(path, spec, c.owned)) return false;
    c.spec = spec;
    c.pcm = c.owned.empty() ? nullptr : c.owned.data();
    c.len = (Uint32)c.owned.size();
    VoiceClipToPlayable(c);
    return c.len > 0;
}

bool OwnRaw(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff n = f.tellg();
    if (n <= 0) return false;
    std::vector<unsigned char> buf((std::size_t)n);
    f.seekg(0);
    if (!f.read((char*)buf.data(), n)) return false;
    okay::wav::WavAudio a;
    return okay::wav::DecodeWav(buf.data(), buf.size(), a) && !a.samples.empty();
}
bool SdlRaw(const std::string& path) {
    SDL_AudioSpec s{}; Uint8* b = nullptr; Uint32 len = 0;
    if (!SDL_LoadWAV(path.c_str(), &s, &b, &len)) return false;
    SDL_FreeWAV(b);
    return len > 0;
}

// ---- the generated set ----------------------------------------------------------------------------------------------
struct Rng { std::uint64_t s = 0x9E3779B97F4A7C15ull; std::uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (std::uint32_t)(s >> 16); } };

void Put16(std::vector<unsigned char>& v, unsigned x) { v.push_back((unsigned char)x); v.push_back((unsigned char)(x >> 8)); }
void Put32(std::vector<unsigned char>& v, std::uint32_t x) { Put16(v, x & 0xFFFF); Put16(v, x >> 16); }
void Tag(std::vector<unsigned char>& v, const char* t) { v.insert(v.end(), t, t + 4); }

// fmt body (without the chunk header). `extra` is appended after cbSize when present.
std::vector<unsigned char> FmtBody(unsigned tag, int ch, std::uint32_t rate, unsigned blockAlign, int bits,
                                   bool withCb, const std::vector<unsigned char>& extra = {}) {
    std::vector<unsigned char> f;
    Put16(f, tag); Put16(f, (unsigned)ch); Put32(f, rate);
    Put32(f, tag == 2 || tag == 0x11 ? rate * blockAlign / 1024 : rate * blockAlign);
    Put16(f, blockAlign); Put16(f, (unsigned)bits);
    if (withCb) { Put16(f, (unsigned)extra.size()); f.insert(f.end(), extra.begin(), extra.end()); }
    return f;
}
bool WriteWav(const fs::path& p, const std::vector<unsigned char>& fmt, const std::vector<unsigned char>& data,
              std::uint32_t factFrames = 0) {
    std::vector<unsigned char> w;
    Tag(w, "RIFF"); Put32(w, 0); Tag(w, "WAVE");
    Tag(w, "fmt "); Put32(w, (std::uint32_t)fmt.size()); w.insert(w.end(), fmt.begin(), fmt.end());
    if (fmt.size() & 1) w.push_back(0);
    if (factFrames) { Tag(w, "fact"); Put32(w, 4); Put32(w, factFrames); }
    Tag(w, "data"); Put32(w, (std::uint32_t)data.size());
    w.insert(w.end(), data.begin(), data.end());
    if (data.size() & 1) w.push_back(0);
    const std::uint32_t riff = (std::uint32_t)(w.size() - 8);
    std::memcpy(w.data() + 4, &riff, 4);   // little-endian host (the bench runs on x86-64 Linux)
    std::ofstream o(p, std::ios::binary);
    return o.write((const char*)w.data(), (std::streamsize)w.size()).good();
}

// One sample of a tone plus noise, -1 to 1.
double Signal(std::size_t frame, int c, std::uint32_t rate, Rng& r) {
    const double t = (double)frame / rate;
    return 0.6 * std::sin(2.0 * 3.14159265358979 * (220.0 + 110.0 * c) * t) + 0.2 * ((double)(r.Next() & 0xFFFF) / 32768.0 - 1.0);
}

bool GenPcm(const fs::path& p, int ch, std::uint32_t rate, double secs, int bits, bool isFloat, bool extensible) {
    const std::size_t frames = (std::size_t)(rate * secs);
    const int sb = bits / 8;
    std::vector<unsigned char> d;
    d.reserve(frames * (std::size_t)ch * (std::size_t)sb);
    Rng r;
    for (std::size_t i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c) {
            const double v = Signal(i, c, rate, r);
            if (isFloat && bits == 32) { float x = (float)v; unsigned char b[4]; std::memcpy(b, &x, 4); d.insert(d.end(), b, b + 4); }
            else if (isFloat) { double x = v; unsigned char b[8]; std::memcpy(b, &x, 8); d.insert(d.end(), b, b + 8); }
            else if (bits == 8) d.push_back((unsigned char)std::lround(128 + v * 127));
            else {
                const long long x = std::llround(v * (double)((1ll << (bits - 1)) - 1));
                for (int b = 0; b < sb; ++b) d.push_back((unsigned char)(x >> (8 * b)));
            }
        }
    const unsigned ba = (unsigned)(ch * sb);
    if (extensible) {
        std::vector<unsigned char> ex;
        Put16(ex, (unsigned)bits);                     // valid bits
        Put32(ex, ch == 6 ? 0x3Fu : ch == 2 ? 0x3u : 0u);   // channel mask
        const unsigned char guid[16] = {(unsigned char)(isFloat ? 3 : 1), 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
        ex.insert(ex.end(), guid, guid + 16);
        return WriteWav(p, FmtBody(0xFFFE, ch, rate, ba, bits, true, ex), d);
    }
    return WriteWav(p, FmtBody(isFloat ? 3 : 1, ch, rate, ba, bits, isFloat), d);
}

bool GenLaw(const fs::path& p, unsigned tag, int ch, std::uint32_t rate, double secs) {
    const std::size_t n = (std::size_t)(rate * secs) * (std::size_t)ch;
    std::vector<unsigned char> d(n);
    Rng r;
    for (auto& b : d) b = (unsigned char)r.Next();
    return WriteWav(p, FmtBody(tag, ch, rate, (unsigned)ch, 8, true), d, (std::uint32_t)(n / (std::size_t)ch));
}

bool GenMsAdpcm(const fs::path& p, int ch, std::uint32_t rate, double secs, unsigned blockAlign) {
    const unsigned hdr = 7u * (unsigned)ch;
    const unsigned spb = (blockAlign - hdr) * 2 / (unsigned)ch + 2;
    const std::size_t blocks = (std::size_t)(rate * secs) / spb + 1;
    std::vector<unsigned char> d(blocks * blockAlign);
    Rng r;
    for (std::size_t b = 0; b < blocks; ++b) {
        unsigned char* q = d.data() + b * blockAlign;
        for (int c = 0; c < ch; ++c) q[c] = (unsigned char)(r.Next() % 7);                                  // predictor
        for (int c = 0; c < ch; ++c) { const unsigned dl = 16 + r.Next() % 512; q[ch + 2 * c] = (unsigned char)dl; q[ch + 2 * c + 1] = (unsigned char)(dl >> 8); }
        for (unsigned k = 3u * (unsigned)ch; k < hdr; ++k) q[k] = (unsigned char)r.Next();                   // two samples each
        for (unsigned k = hdr; k < blockAlign; ++k) q[k] = (unsigned char)r.Next();
    }
    static const int coef[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};
    std::vector<unsigned char> ex;
    Put16(ex, spb); Put16(ex, 7);
    for (auto& cf : coef) { Put16(ex, (unsigned)(cf[0] & 0xFFFF)); Put16(ex, (unsigned)(cf[1] & 0xFFFF)); }
    return WriteWav(p, FmtBody(2, ch, rate, blockAlign, 4, true, ex), d, (std::uint32_t)(blocks * spb));
}

bool GenImaAdpcm(const fs::path& p, int ch, std::uint32_t rate, double secs, unsigned blockAlign) {
    const unsigned hdr = 4u * (unsigned)ch;
    const unsigned spb = 1 + 8 * ((blockAlign - hdr) / hdr);
    const std::size_t blocks = (std::size_t)(rate * secs) / spb + 1;
    std::vector<unsigned char> d(blocks * blockAlign);
    Rng r;
    for (std::size_t b = 0; b < blocks; ++b) {
        unsigned char* q = d.data() + b * blockAlign;
        for (int c = 0; c < ch; ++c) { q[4 * c] = (unsigned char)r.Next(); q[4 * c + 1] = (unsigned char)r.Next(); q[4 * c + 2] = (unsigned char)(r.Next() % 89); q[4 * c + 3] = 0; }
        for (unsigned k = hdr; k < blockAlign; ++k) q[k] = (unsigned char)r.Next();
    }
    std::vector<unsigned char> ex;
    Put16(ex, spb);
    return WriteWav(p, FmtBody(0x11, ch, rate, blockAlign, 4, true, ex), d, (std::uint32_t)(blocks * spb));
}

struct GenSpec { const char* name; std::function<bool(const fs::path&)> make; };
std::vector<GenSpec> GenSet() {
    const std::uint32_t R = 44100;
    return {
        {"gen_pcm16_stereo_1s.wav",    [=](const fs::path& p) { return GenPcm(p, 2, R, 1, 16, false, false); }},
        {"gen_pcm16_stereo_60s.wav",   [=](const fs::path& p) { return GenPcm(p, 2, R, 60, 16, false, false); }},
        {"gen_pcm16_stereo_10min.wav", [=](const fs::path& p) { return GenPcm(p, 2, R, 600, 16, false, false); }},
        {"gen_pcm8_stereo_60s.wav",    [=](const fs::path& p) { return GenPcm(p, 2, R, 60, 8, false, false); }},
        {"gen_pcm24_stereo_60s.wav",   [=](const fs::path& p) { return GenPcm(p, 2, R, 60, 24, false, false); }},
        {"gen_pcm32_stereo_60s.wav",   [=](const fs::path& p) { return GenPcm(p, 2, R, 60, 32, false, false); }},
        {"gen_f32_stereo_60s.wav",     [=](const fs::path& p) { return GenPcm(p, 2, R, 60, 32, true, false); }},
        {"gen_f64_stereo_60s.wav",     [=](const fs::path& p) { return GenPcm(p, 2, R, 60, 64, true, false); }},
        {"gen_alaw_stereo_60s.wav",    [=](const fs::path& p) { return GenLaw(p, 6, 2, R, 60); }},
        {"gen_mulaw_stereo_60s.wav",   [=](const fs::path& p) { return GenLaw(p, 7, 2, R, 60); }},
        {"gen_msadpcm_stereo_60s.wav", [=](const fs::path& p) { return GenMsAdpcm(p, 2, R, 60, 2048); }},
        {"gen_ima_stereo_60s.wav",     [=](const fs::path& p) { return GenImaAdpcm(p, 2, R, 60, 2048); }},
        {"gen_ext_pcm16_6ch_48k_60s.wav", [=](const fs::path& p) { return GenPcm(p, 6, 48000, 60, 16, false, true); }},
    };
}

// ---- digest ----------------------------------------------------------------------------------------------------------
struct Hasher {
    std::uint64_t h = 1469598103934665603ull;
    void Bytes(const void* p, std::size_t n) { const auto* b = (const unsigned char*)p; for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } }
    template <class T> void Pod(const T& v) { Bytes(&v, sizeof(T)); }
};
std::vector<unsigned char> ReadAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
// The decoder's and the playback conversion's whole result for one byte buffer, into `a` and `b`.
void HashDecode(const std::vector<unsigned char>& buf, Hasher& a, Hasher& b) {
    okay::wav::WavAudio w; std::string e1, e2;
    const bool ok1 = okay::wav::DecodeWav(buf.data(), buf.size(), w, &e1);
    a.Pod(ok1); a.Pod(w.sampleRate); a.Pod(w.channels); a.Pod(w.encoding); a.Pod(w.bitsPerSample); a.Pod(w.samples.size());
    a.Bytes(w.samples.data(), w.samples.size() * sizeof(float));
    for (const std::string& s : w.warnings) a.Bytes(s.data(), s.size());
    a.Bytes(e1.data(), e1.size());
    okay::wav::PlaybackClip p1;
    const bool ok2 = okay::wav::DecodeForPlayback(buf.data(), buf.size(), p1, &e2);
    b.Pod(ok2); b.Pod(p1.format); b.Pod(p1.channels); b.Pod(p1.sampleRate); b.Pod(p1.bytes.size()); b.Bytes(p1.bytes.data(), p1.bytes.size());
    b.Bytes(e2.data(), e2.size());
}
// For small files (the repository's): every truncation and 2,000 byte mutations, so the error and warning paths are
// covered by the digest too.
void PrintDamageDigest(const std::string& path) {
    const std::vector<unsigned char> buf = ReadAll(path);
    if (buf.size() > (1u << 20)) return;
    Hasher a, b;
    for (std::size_t n = 0; n < buf.size(); n += 1 + n / 64) HashDecode(std::vector<unsigned char>(buf.begin(), buf.begin() + (std::ptrdiff_t)n), a, b);
    Rng r;
    for (int k = 0; k < 2000; ++k) {
        std::vector<unsigned char> m = buf;
        const int edits = 1 + (int)(r.Next() % 4);
        for (int e = 0; e < edits; ++e) {
            const std::size_t at = (r.Next() % 3 == 0) ? r.Next() % std::min<std::size_t>(m.size(), 128) : r.Next() % m.size();
            m[at] = (unsigned char)r.Next();
        }
        HashDecode(m, a, b);
    }
    std::printf("damage %s decode=%016llx playback=%016llx\n", fs::path(path).filename().string().c_str(),
                (unsigned long long)a.h, (unsigned long long)b.h);
}
void PrintDigest(const std::string& path) {
    PrintDamageDigest(path);
    const std::vector<unsigned char> buf = ReadAll(path);
    Hasher a, b, c;
    okay::wav::WavAudio w; std::string e1, e2, e3;
    const bool ok1 = okay::wav::DecodeWav(buf.data(), buf.size(), w, &e1);
    a.Pod(ok1); a.Pod(w.sampleRate); a.Pod(w.channels); a.Pod(w.encoding); a.Pod(w.bitsPerSample); a.Pod(w.samples.size());
    a.Bytes(w.samples.data(), w.samples.size() * sizeof(float));
    for (const std::string& s : w.warnings) a.Bytes(s.data(), s.size());
    a.Bytes(e1.data(), e1.size());
    okay::wav::PlaybackClip p1, p2;
    const bool ok2 = okay::wav::DecodeForPlayback(buf.data(), buf.size(), p1, &e2);
    b.Pod(ok2); b.Pod(p1.format); b.Pod(p1.channels); b.Pod(p1.sampleRate); b.Pod(p1.bytes.size()); b.Bytes(p1.bytes.data(), p1.bytes.size());
    b.Bytes(e2.data(), e2.size());
    const bool ok3 = okay::wav::LoadForPlayback(path, p2, &e3);
    c.Pod(ok3); c.Pod(p2.format); c.Pod(p2.channels); c.Pod(p2.sampleRate); c.Pod(p2.bytes.size()); c.Bytes(p2.bytes.data(), p2.bytes.size());
    c.Bytes(e3.data(), e3.size());
    std::printf("digest %s decode=%016llx playback=%016llx load=%016llx\n", fs::path(path).filename().string().c_str(),
                (unsigned long long)a.h, (unsigned long long)b.h, (unsigned long long)c.h);
}

std::string Mb(long long b) {
    if (b < 0) return "-";
    char s[32]; std::snprintf(s, sizeof(s), "%.3f", (double)b / (1024.0 * 1024.0)); return s;
}
std::string Ms(const Measure& m) {
    if (!m.ok) return "fail";
    char s[32]; std::snprintf(s, sizeof(s), m.ms < 10 ? "%.3f" : "%.1f", m.ms); return s;
}
std::string Ratio(const Measure& a, const Measure& b, bool mem) {
    if (!a.ok || !b.ok) return "-";
    const double x = mem ? (double)a.peak : a.ms, y = mem ? (double)b.peak : b.ms;
    if (x < 0 || y <= 0) return "-";
    char s[32]; std::snprintf(s, sizeof(s), "%.2f", x / y); return s;
}

void Collect(const fs::path& p, std::vector<std::string>& out) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        std::vector<std::string> here;
        for (auto it = fs::recursive_directory_iterator(p, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext == ".wav") here.push_back(it->path().string());
        }
        std::sort(here.begin(), here.end());
        out.insert(out.end(), here.begin(), here.end());
    } else if (fs::is_regular_file(p, ec)) {
        out.push_back(p.string());
    }
}

} // namespace

int main(int argc, char** argv) {
    int runs = 7;
    bool digest = false;
    std::string only;
    fs::path genDir = fs::temp_directory_path() / "okay_wav_bench";
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--runs" && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--gen" && i + 1 < argc) genDir = argv[++i];
        else if (a == "--digest") digest = true;
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else Collect(a, files);
    }
    if (files.empty()) {
#ifdef OKAY_SOURCE_DIR
        Collect(fs::path(OKAY_SOURCE_DIR) / "tests" / "data" / "audio", files);
        Collect(fs::path(OKAY_SOURCE_DIR) / "examples", files);
#endif
        std::error_code ec;
        fs::create_directories(genDir, ec);
        for (const GenSpec& g : GenSet()) {
            const fs::path p = genDir / g.name;
            if (!fs::exists(p, ec)) {
                std::fprintf(stderr, "writing %s\n", p.string().c_str());
                if (!g.make(p)) { std::fprintf(stderr, "cannot write %s\n", p.string().c_str()); return 1; }
            }
            files.push_back(p.string());
        }
    }
    if (digest) { for (const std::string& f : files) PrintDigest(f); return 0; }
    if (!only.empty()) {
        for (const std::string& f : files) {
            bool ok = false;
            if (only == "own-raw") ok = OwnRaw(f);
            else if (only == "sdl-raw") ok = SdlRaw(f);
            else if (only == "own-final") ok = LauncherLoad(f, true);
            else if (only == "sdl-final") ok = LauncherLoad(f, false);
            else { std::fprintf(stderr, "unknown --only %s\n", only.c_str()); return 2; }
            std::printf("%s %s\n", ok ? "ok  " : "FAIL", f.c_str());
        }
        return 0;
    }

    std::printf("Median ms over >= %d runs; peak heap in MiB above the start of the call. Ratio = own / SDL (below 1 is better for us).\n\n", runs);
    std::printf("| File | MiB on disk | own-raw ms | SDL-raw ms | time ratio | own-raw MiB | SDL-raw MiB | mem ratio | own-final ms | SDL-final ms | time ratio | own-final MiB | SDL-final MiB | mem ratio |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const std::string& f : files) {
        std::error_code ec;
        const auto bytes = (long long)fs::file_size(f, ec);
        const Measure orw = Run(runs, [&] { return OwnRaw(f); });
        const Measure srw = Run(runs, [&] { return SdlRaw(f); });
        const Measure ofi = Run(runs, [&] { return LauncherLoad(f, true); });
        const Measure sfi = Run(runs, [&] { return LauncherLoad(f, false); });
        std::printf("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n",
                    fs::path(f).filename().string().c_str(), Mb(bytes).c_str(),
                    Ms(orw).c_str(), Ms(srw).c_str(), Ratio(orw, srw, false).c_str(),
                    orw.ok ? Mb(orw.peak).c_str() : "-", srw.ok ? Mb(srw.peak).c_str() : "-", Ratio(orw, srw, true).c_str(),
                    Ms(ofi).c_str(), Ms(sfi).c_str(), Ratio(ofi, sfi, false).c_str(),
                    ofi.ok ? Mb(ofi.peak).c_str() : "-", sfi.ok ? Mb(sfi.peak).c_str() : "-", Ratio(ofi, sfi, true).c_str());
        std::fflush(stdout);
    }
    return 0;
}
