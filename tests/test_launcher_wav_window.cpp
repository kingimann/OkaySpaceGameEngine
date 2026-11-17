// The launcher off SDL for WAV reading and for its window calls (docs/own_code_roadmap.md, SDL2 row, stage A).
//
// 1. WAV: the launcher reads a voice clip with SDL by default and with our own decoder with OKAY_OWN_WAV=1
//    (launcher/VoiceClipLoadSdl.hpp picks; launcher/VoiceClipLoad.hpp is ours). Both paths are checked: the selection
//    follows OKAY_OWN_WAV, the SDL path gives exactly what the launcher always gave. Every WAV in the repository
//    (examples/ and the ffmpeg-made fixtures in tests/data/audio/wav_ffmpeg/) and a few made here (the launcher's own
//    16-bit mono recording format, 8-bit, 24-bit, float, an odd-sized extra chunk) are read both ways: with
//    SDL_LoadWAV plus the conversion the launcher used to run on SDL's result, and with the new helper. Format,
//    channels, rate and length must match; the samples must be byte for byte equal, except 8-bit and 24-bit PCM, where
//    the old launcher truncated and ours rounds (within 2 steps of 32,768). Every truncation of
//    a small file must fail cleanly or return a shorter clip, never crash or read past the end.
// 2. Window: okay::platform::WrapSdl2Window over a raw SDL_Window (the launcher still creates its windows with SDL)
//    must answer size, position, minimum size, id, visible / minimised / focused exactly as the SDL calls the launcher
//    used to make, under SDL's dummy video driver (headless, no X server needed).
//
// Written from the RIFF / WAVE layout (Microsoft's multimedia registration documents) and the SDL2 public API names;
// no SDL source or other project's code was read or followed.
#include "test_framework.hpp"
#define SDL_MAIN_HANDLED   // our own main()
#include <SDL.h>
#include "VoiceClipLoadSdl.hpp"   // launcher/: the helpers under test (includes VoiceClipLoad.hpp)
#include "okay/Audio/WavPlayback.hpp"
#include "okay/Platform/Platform.hpp"


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef OKAY_SOURCE_DIR
#  define OKAY_SOURCE_DIR "."
#endif

using Bytes = std::vector<unsigned char>;
namespace fs = std::filesystem;

// ---- WAV files made here ----------------------------------------------------------------------------------------
static void P16(Bytes& b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void P32(Bytes& b, std::uint32_t v) { P16(b, v & 0xFFFF); P16(b, v >> 16); }
static void Tag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }
// tag 1 = PCM, 3 = IEEE float. `extra` goes between "fmt " and "data" (an odd-sized chunk plus its pad byte).
static Bytes MakeWav(unsigned tag, unsigned ch, std::uint32_t rate, unsigned bits, const Bytes& data, const Bytes& extra = {}) {
    const unsigned align = ch * bits / 8;
    Bytes b; Tag(b, "RIFF"); P32(b, std::uint32_t(4 + 24 + extra.size() + 8 + data.size())); Tag(b, "WAVE");
    Tag(b, "fmt "); P32(b, 16); P16(b, tag); P16(b, ch); P32(b, rate); P32(b, rate * align); P16(b, align); P16(b, bits);
    b.insert(b.end(), extra.begin(), extra.end());
    Tag(b, "data"); P32(b, std::uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}
static std::string TempPath(const char* name) { return (fs::temp_directory_path() / name).string(); }
static bool Put(const std::string& path, const Bytes& b) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), long(b.size()));
    return bool(f);
}

// ---- The old launcher path: SDL_LoadWAV, then its own "to 16-bit unless already s16 / f32" pass --------------------
struct OldClip { bool ok = false; SDL_AudioSpec spec{}; std::vector<std::uint8_t> bytes; };
static OldClip OldLauncherLoad(const std::string& path) {
    OldClip c;
    Uint8* buf = nullptr; Uint32 len = 0;
    if (!SDL_LoadWAV(path.c_str(), &c.spec, &buf, &len)) return c;
    c.bytes.assign(buf, buf + len);
    SDL_FreeWAV(buf);
    const SDL_AudioFormat f = c.spec.format;
    if (f != AUDIO_S16SYS && f != AUDIO_F32SYS && !c.bytes.empty()) {   // what VoiceClipToPlayable did
        const int width = SDL_AUDIO_BITSIZE(f) / 8;
        const std::size_t n = c.bytes.size() / std::size_t(width);
        std::vector<std::uint8_t> out(n * 2);
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t* p = c.bytes.data() + i * std::size_t(width);
            float v = 0.0f;
            if (SDL_AUDIO_ISFLOAT(f)) {
                std::uint32_t u = 0;
                for (int k = 0; k < 4; ++k) u |= std::uint32_t(p[SDL_AUDIO_ISBIGENDIAN(f) ? 3 - k : k]) << (8 * k);
                std::memcpy(&v, &u, 4);
            } else if (width == 1) {
                v = SDL_AUDIO_ISSIGNED(f) ? float(std::int8_t(p[0])) / 128.0f : (float(p[0]) - 128.0f) / 128.0f;
            } else {
                std::uint32_t u = 0;
                for (int k = 0; k < width; ++k) u |= std::uint32_t(p[SDL_AUDIO_ISBIGENDIAN(f) ? width - 1 - k : k]) << (8 * k);
                const long long top = 1ll << (8 * width - 1);
                const long long sv = SDL_AUDIO_ISSIGNED(f) ? (long long)(std::int32_t(u << (32 - 8 * width)) >> (32 - 8 * width))
                                                           : (long long)u - top;
                v = float(sv) / float(top);
            }
            const std::int16_t s = std::int16_t(std::clamp(v * 32767.0f, -32768.0f, 32767.0f));
            std::memcpy(out.data() + i * 2, &s, 2);
        }
        c.bytes = std::move(out);
        c.spec.format = AUDIO_S16SYS;
    }
    c.ok = !c.bytes.empty();
    return c;
}

// Compares the two paths on one file. `tol` is the largest sample difference allowed (16-bit units; 0 = identical bytes).
static void CompareOne(const std::string& label, const std::string& path, int tol) {
    const OldClip old = OldLauncherLoad(path);
    okay::wav::PlaybackClip ours; std::string err;
    const bool okOurs = okay::launcher::LoadVoiceClipFile(path, ours, &err);
    CHECK(old.ok);
    CHECK(okOurs);
    if (!old.ok || !okOurs) { std::cerr << "  " << label << ": sdl=" << old.ok << " ours=" << okOurs << " (" << err << ")\n"; return; }
    const bool oldF32 = old.spec.format == AUDIO_F32SYS;
    CHECK(oldF32 == (ours.format == okay::wav::PlaybackFormat::F32));
    CHECK(int(old.spec.channels) == ours.channels);
    CHECK(old.spec.freq == ours.sampleRate);
    CHECK(old.bytes.size() == ours.bytes.size());
    CHECK(okay::launcher::VoiceFrameBytes(ours) == (oldF32 ? 4 : 2) * int(old.spec.channels));
    CHECK(okay::launcher::VoiceDeviceFormat(ours) == (oldF32 ? okay::platform::SampleFormat::F32 : okay::platform::SampleFormat::S16));
    const std::size_t n = std::min(old.bytes.size(), ours.bytes.size());
    double maxDiff = 0;
    if (oldF32) {
        for (std::size_t i = 0; i + 4 <= n; i += 4) {
            float a, b; std::memcpy(&a, &old.bytes[i], 4); std::memcpy(&b, &ours.bytes[i], 4);
            maxDiff = std::max(maxDiff, double(std::fabs(a - b)) * 32768.0);
        }
    } else {
        for (std::size_t i = 0; i + 2 <= n; i += 2) {
            std::int16_t a, b; std::memcpy(&a, &old.bytes[i], 2); std::memcpy(&b, &ours.bytes[i], 2);
            maxDiff = std::max(maxDiff, std::fabs(double(a) - double(b)));
        }
    }
    std::cout << "  " << label << ": " << ours.channels << " ch, " << ours.sampleRate << " Hz, "
              << (oldF32 ? "f32" : "s16") << ", " << ours.bytes.size() << " bytes, max sample difference " << maxDiff
              << " (allowed " << tol << ")\n";
    CHECK(maxDiff <= double(tol));
    if (tol == 0) CHECK(old.bytes == ours.bytes);
}

static void TestRepositoryWavs() {
    RUN_SUITE("WAV files in the repository: SDL_LoadWAV + old conversion vs our reader");
    struct F { const char* rel; int tol; };
    // Tolerances (measured on Linux, SDL 2 from the system, then fixed here): everything SDL itself decodes to 16-bit or
    // float (16-bit PCM, float, A-law, mu-law, MS and IMA ADPCM) must be byte for byte the same. 8-bit and 24-bit: the old
    // launcher truncated (v * 32767) where ours rounds the exact value, so at most 2 steps apart (measured: 1 and 2).
    const F files[] = {
        {"examples/own_wav/mulaw_tone.wav", 0},
        {"examples/spatial_audio/tone.wav", 0},
        {"tests/data/audio/wav_ffmpeg/alaw_stereo.wav", 0},
        {"tests/data/audio/wav_ffmpeg/f32_mono.wav", 0},
        {"tests/data/audio/wav_ffmpeg/ima_mono.wav", 0},
        {"tests/data/audio/wav_ffmpeg/ima_stereo.wav", 0},
        {"tests/data/audio/wav_ffmpeg/ms_mono.wav", 0},
        {"tests/data/audio/wav_ffmpeg/ms_mono_b256.wav", 0},
        {"tests/data/audio/wav_ffmpeg/ms_stereo.wav", 0},
        {"tests/data/audio/wav_ffmpeg/ms_stereo_b2048.wav", 0},
        {"tests/data/audio/wav_ffmpeg/mulaw_mono.wav", 0},
        {"tests/data/audio/wav_ffmpeg/s24_stereo.wav", 2},
        {"tests/data/audio/wav_ffmpeg/u8_mono.wav", 2},
    };
    for (const F& f : files) {
        const std::string p = std::string(OKAY_SOURCE_DIR) + "/" + f.rel;
        if (!fs::exists(p)) { std::cerr << "  missing fixture " << p << "\n"; CHECK(false); continue; }
        CompareOne(f.rel, p, f.tol);
    }
}

static void TestMadeWavs() {
    RUN_SUITE("WAV files made here: the launcher's recording format and edge cases");
    // The launcher's own voice note: 16-bit mono at 16 kHz, written like WriteWav16 in launcher/main.cpp.
    Bytes s16;
    for (int i = 0; i < 16000; ++i) P16(s16, unsigned(std::int16_t(std::lround(20000.0 * std::sin(i * 0.0393)))) & 0xFFFF);
    const std::string voice = TempPath("okay_lww_voice.wav");
    CHECK(Put(voice, MakeWav(1, 1, 16000, 16, s16)));
    CompareOne("voice note (16-bit mono 16 kHz)", voice, 0);

    Bytes u8;   // 8-bit unsigned stereo, every value
    for (int i = 0; i < 512; ++i) u8.push_back(std::uint8_t(i & 255));
    const std::string p8 = TempPath("okay_lww_u8.wav");
    CHECK(Put(p8, MakeWav(1, 2, 11025, 8, u8)));
    CompareOne("8-bit stereo", p8, 2);

    Bytes s24;  // 24-bit mono, a ramp across the whole range
    for (int i = 0; i < 2000; ++i) { const std::int32_t v = (i - 1000) * 8388; s24.push_back(v & 255); s24.push_back((v >> 8) & 255); s24.push_back((v >> 16) & 255); }
    const std::string p24 = TempPath("okay_lww_s24.wav");
    CHECK(Put(p24, MakeWav(1, 1, 48000, 24, s24)));
    CompareOne("24-bit mono", p24, 2);

    Bytes f32;  // float stereo
    for (int i = 0; i < 1000; ++i) { float v = float(std::sin(i * 0.05)) * 0.8f; std::uint32_t u; std::memcpy(&u, &v, 4); P32(f32, u); }
    const std::string pf = TempPath("okay_lww_f32.wav");
    CHECK(Put(pf, MakeWav(3, 2, 44100, 32, f32)));
    CompareOne("float32 stereo", pf, 0);

    // An odd-sized chunk ("LIST", 3 bytes) before the data: RIFF pads it with one byte, both readers must skip it.
    Bytes odd; Tag(odd, "LIST"); P32(odd, 3); odd.push_back('a'); odd.push_back('b'); odd.push_back('c'); odd.push_back(0);
    const std::string po = TempPath("okay_lww_odd.wav");
    CHECK(Put(po, MakeWav(1, 1, 16000, 16, s16, odd)));
    CompareOne("odd-sized chunk before data", po, 0);

    for (const std::string& p : {voice, p8, p24, pf, po}) std::remove(p.c_str());
}

static void TestTruncated() {
    RUN_SUITE("Truncated files: ours fails or returns less, never more");
    Bytes s16;
    for (int i = 0; i < 64; ++i) P16(s16, unsigned(i * 1000) & 0xFFFF);
    const Bytes full = MakeWav(1, 1, 16000, 16, s16);
    const std::string p = TempPath("okay_lww_cut.wav");
    int failed = 0, shorter = 0;
    bool headerCutsFail = true, neverLonger = true, okHasData = true;
    for (std::size_t cut = 0; cut < full.size(); ++cut) {
        const Bytes part(full.begin(), full.begin() + long(cut));
        // In memory (no file needed) and through the file path the launcher uses.
        okay::wav::PlaybackClip a, b; std::string ea, eb;
        const bool okMem = okay::wav::DecodeForPlayback(part.data(), part.size(), a, &ea);
        CHECK(Put(p, part));
        const bool okFile = okay::launcher::LoadVoiceClipFile(p, b, &eb);
        CHECK(okMem == okFile);
        if (cut < 44 && okFile) headerCutsFail = false;    // no complete header and no sample: must fail
        if (okFile) {
            ++shorter;
            if (b.bytes.size() > s16.size()) neverLonger = false;
            if (b.bytes.empty() || b.channels != 1 || b.sampleRate != 16000) okHasData = false;
        } else {
            ++failed;
            if (!b.bytes.empty()) okHasData = false;       // a failure leaves no samples behind
        }
    }
    std::cout << "  " << full.size() << " cuts: " << failed << " refused, " << shorter << " read as a shorter clip\n";
    CHECK(headerCutsFail);
    CHECK(neverLonger);
    CHECK(okHasData);
    CHECK(failed >= 44);
    // A file that is not there, an empty file and a non-WAV file fail with a message.
    okay::wav::PlaybackClip c; std::string e;
    CHECK(!okay::launcher::LoadVoiceClipFile(TempPath("okay_lww_does_not_exist.wav"), c, &e) && !e.empty());
    CHECK(Put(p, Bytes{})); e.clear();
    CHECK(!okay::launcher::LoadVoiceClipFile(p, c, &e) && !e.empty());
    CHECK(Put(p, Bytes{'h', 'e', 'l', 'l', 'o'})); e.clear();
    CHECK(!okay::launcher::LoadVoiceClipFile(p, c, &e) && !e.empty());
    std::remove(p.c_str());
}

static void TestBars() {
    RUN_SUITE("Waveform bars: peak per slice of a 16-bit clip");
    okay::wav::PlaybackClip c;
    c.format = okay::wav::PlaybackFormat::S16; c.channels = 1; c.sampleRate = 16000;
    // 4 slices of 100 frames with peaks 1000, 32767 (as -32767), 0, 16384.
    const int peaks[4] = {1000, -32767, 0, 16384};
    for (int s = 0; s < 4; ++s)
        for (int i = 0; i < 100; ++i) {
            const std::int16_t v = std::int16_t(i == 50 ? peaks[s] : 0);
            c.bytes.push_back(std::uint8_t(v & 255)); c.bytes.push_back(std::uint8_t((v >> 8) & 255));
        }
    float bars[4] = {};
    CHECK(okay::launcher::VoicePeakBars(c, bars, 4));
    CHECK_NEAR(bars[0], 1000.0 / 32768.0, 1e-6);
    CHECK_NEAR(bars[1], 32767.0 / 32768.0, 1e-6);
    CHECK_NEAR(bars[2], 0.0, 1e-9);
    CHECK_NEAR(bars[3], 16384.0 / 32768.0, 1e-6);
    c.format = okay::wav::PlaybackFormat::F32;   // float clips keep the stub (as before): false, bars untouched
    float keep[4] = {9, 9, 9, 9};
    CHECK(!okay::launcher::VoicePeakBars(c, keep, 4));
    CHECK(keep[0] == 9 && keep[3] == 9);
    okay::wav::PlaybackClip empty;
    CHECK(!okay::launcher::VoicePeakBars(empty, keep, 4));
}

// ---- The switch: default SDL, OKAY_OWN_WAV=1 ours ---------------------------------------------------------------
static void SetOwnWav(const char* v) {
#ifdef _WIN32
    _putenv_s("OKAY_OWN_WAV", v ? v : "");
#else
    if (v) setenv("OKAY_OWN_WAV", v, 1); else unsetenv("OKAY_OWN_WAV");
#endif
}
static bool Same(const okay::wav::PlaybackClip& a, const okay::wav::PlaybackClip& b) {
    return a.format == b.format && a.channels == b.channels && a.sampleRate == b.sampleRate && a.bytes == b.bytes && !a.bytes.empty();
}
static bool SameAsOld(const OldClip& o, const okay::wav::PlaybackClip& c) {
    const bool f32 = o.spec.format == AUDIO_F32SYS;
    return o.ok && f32 == (c.format == okay::wav::PlaybackFormat::F32) && int(o.spec.channels) == c.channels &&
           o.spec.freq == c.sampleRate && o.bytes == c.bytes;
}
static void TestSelection() {
    RUN_SUITE("Which reader: SDL by default, ours with OKAY_OWN_WAV=1 (both paths)");
    Bytes s16; for (int i = 0; i < 4000; ++i) P16(s16, unsigned(i * 37) & 0xFFFF);
    Bytes u8; for (int i = 0; i < 300; ++i) u8.push_back(std::uint8_t(i * 7));
    Bytes s24; for (int i = 0; i < 300; ++i) { const std::int32_t v = (i - 150) * 50000; s24.push_back(v & 255); s24.push_back((v >> 8) & 255); s24.push_back((v >> 16) & 255); }
    Bytes f32; for (int i = 0; i < 300; ++i) { float v = float(i - 150) / 200.0f; std::uint32_t u; std::memcpy(&u, &v, 4); P32(f32, u); }
    struct M { const char* name; Bytes wav; };
    std::vector<M> made = {{"okay_lww_sel_s16.wav", MakeWav(1, 1, 16000, 16, s16)}, {"okay_lww_sel_u8.wav", MakeWav(1, 2, 8000, 8, u8)},
                           {"okay_lww_sel_s24.wav", MakeWav(1, 1, 22050, 24, s24)}, {"okay_lww_sel_f32.wav", MakeWav(3, 1, 44100, 32, f32)}};
    std::vector<std::string> paths;
    for (const M& m : made) { paths.push_back(TempPath(m.name)); CHECK(Put(paths.back(), m.wav)); }
    paths.push_back(std::string(OKAY_SOURCE_DIR) + "/tests/data/audio/wav_ffmpeg/ima_stereo.wav");
    paths.push_back(std::string(OKAY_SOURCE_DIR) + "/examples/own_wav/mulaw_tone.wav");
    int checked = 0;
    for (const std::string& p : paths) {
        const OldClip old = OldLauncherLoad(p);   // raw SDL_LoadWAV + the old launcher conversion, whatever the switch says
        okay::wav::PlaybackClip ours, sdl, pick0, pick1, pickDefault;
        CHECK(okay::launcher::LoadVoiceClipFile(p, ours));
        SetOwnWav("0");
        CHECK(!okay::wav::OwnWavEnabled());
        CHECK(okay::launcher::LoadVoiceClipSdl(p, sdl));
        CHECK(okay::launcher::LoadVoiceClip(p, pick0));
        CHECK(SameAsOld(old, sdl));     // the SDL path is exactly what the launcher always played
        CHECK(Same(pick0, sdl));        // OKAY_OWN_WAV=0: the launcher takes the SDL path
        SetOwnWav("1");
        CHECK(okay::wav::OwnWavEnabled());
        CHECK(okay::launcher::LoadVoiceClip(p, pick1));
        CHECK(Same(pick1, ours));       // OKAY_OWN_WAV=1: our decoder
        SetOwnWav(nullptr);             // no variable: the build default (CMake OKAY_OWN_WAV, off unless set)
        CHECK(okay::launcher::LoadVoiceClip(p, pickDefault));
        CHECK(Same(pickDefault, okay::wav::OwnWavEnabled() ? ours : sdl));
        ++checked;
    }
    std::cout << "  this build's default (no variable): " << (okay::wav::OwnWavEnabled() ? "our decoder (OKAY_OWN_WAV=ON)" : "SDL") << "\n";
    // A broken file fails on both paths, and leaves nothing behind.
    const std::string bad = TempPath("okay_lww_sel_bad.wav");
    CHECK(Put(bad, Bytes{'R', 'I', 'F', 'F', 1, 0, 0, 0, 'W', 'A', 'V', 'E'}));
    for (const char* v : {"0", "1"}) {
        SetOwnWav(v);
        okay::wav::PlaybackClip c; std::string e;
        CHECK(!okay::launcher::LoadVoiceClip(bad, c, &e) && c.bytes.empty() && !e.empty());
    }
    SetOwnWav(nullptr);
    std::cout << "  " << checked << " files: default and OKAY_OWN_WAV=0 give the SDL result byte for byte, OKAY_OWN_WAV=1 gives ours\n";
    for (std::size_t i = 0; i < made.size(); ++i) std::remove(paths[i].c_str());
    std::remove(bad.c_str());
}

// ---- Window seam over a raw SDL window -------------------------------------------------------------------------
static void TestWindowSeam() {
    RUN_SUITE("okay::platform::WrapSdl2Window answers what the launcher asked SDL (dummy video driver)");
#ifdef _WIN32
    _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
    setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::cerr << "  SDL_Init(video, dummy) failed: " << SDL_GetError() << "\n"; CHECK(false); return; }
    SDL_Window* w = SDL_CreateWindow("seam", 100, 50, 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    CHECK(w != nullptr);
    if (!w) { SDL_Quit(); return; }
    {
        std::unique_ptr<okay::platform::Window> pw = okay::platform::WrapSdl2Window(w);
        CHECK(pw != nullptr);
        if (pw) {
            CHECK(pw->Kind() == okay::platform::BackendKind::Sdl2);
            CHECK(pw->NativeHandle() == static_cast<void*>(w));
            CHECK(pw->Id() == SDL_GetWindowID(w));
            int a = 0, b = 0, sa = 0, sb = 0;
            pw->GetSize(a, b); SDL_GetWindowSize(w, &sa, &sb);
            CHECK(a == sa && b == sb && a == 800 && b == 600);
            pw->GetPosition(a, b); SDL_GetWindowPosition(w, &sa, &sb);
            CHECK(a == sa && b == sb);
            pw->SetMinimumSize(720, 480);
            SDL_GetWindowMinimumSize(w, &sa, &sb);
            CHECK(sa == 720 && sb == 480);
            SDL_SetWindowSize(w, 300, 200);            // smaller than the minimum: SDL keeps the minimum
            pw->GetSize(a, b); SDL_GetWindowSize(w, &sa, &sb);
            CHECK(a == sa && b == sb && a >= 720 && b >= 480);
            const Uint32 f0 = SDL_GetWindowFlags(w);
            CHECK(pw->IsVisible() == ((f0 & SDL_WINDOW_HIDDEN) == 0));
            CHECK(!pw->IsVisible());
            pw->Show(); pw->Raise();
            const Uint32 f1 = SDL_GetWindowFlags(w);
            CHECK(pw->IsVisible() == ((f1 & SDL_WINDOW_HIDDEN) == 0));
            CHECK(pw->IsVisible());
            CHECK(pw->IsMinimized() == ((f1 & SDL_WINDOW_MINIMIZED) != 0));
            CHECK(pw->IsFocused() == ((f1 & SDL_WINDOW_INPUT_FOCUS) != 0));
            pw->Minimize();
            const Uint32 f2 = SDL_GetWindowFlags(w);
            CHECK(pw->IsMinimized() == ((f2 & SDL_WINDOW_MINIMIZED) != 0));
            pw->Restore();
            const Uint32 f3 = SDL_GetWindowFlags(w);
            CHECK(pw->IsMinimized() == ((f3 & SDL_WINDOW_MINIMIZED) != 0));
            CHECK(pw->IsVisible() == ((f3 & SDL_WINDOW_HIDDEN) == 0));
            std::cout << "  size " << sa << "x" << sb << ", flags hidden/minimised/focus seen the same way through the seam\n";
        }
    }
    // The wrapper does not own the window: it is still there after the wrapper is gone.
    CHECK(SDL_GetWindowFromID(SDL_GetWindowID(w)) == w);
    CHECK(okay::platform::WrapSdl2Window(nullptr) == nullptr);
    SDL_DestroyWindow(w);
    SDL_Quit();
}

int main(int, char**) {
    TestRepositoryWavs();
    TestMadeWavs();
    TestTruncated();
    TestBars();
    TestWindowSeam();      // SDL_Init(video) here; SDL_LoadWAV needs no init
    TestSelection();
    TEST_MAIN_RESULT();
}
