#include "test_framework.hpp"
#include "VoiceClip.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay::voice;
namespace fs = std::filesystem;

static int g_freed = 0;
static void CountFree(Uint8* p) { ++g_freed; SDL_FreeWAV(p); }

static void Le(std::string& s, std::uint32_t v, int bytes) { for (int i = 0; i < bytes; ++i) s += (char)((v >> (8 * i)) & 0xff); }
// A minimal PCM WAV: `bits` per sample, mono, 8 kHz, `n` samples of a ramp.
static std::string MakeWav(int bits, int n) {
    std::string data;
    for (int i = 0; i < n; ++i) { if (bits == 8) data += (char)(128 + i); else Le(data, (std::uint32_t)(i * 100), bits / 8); }
    std::string w = "RIFF"; Le(w, 36 + (std::uint32_t)data.size(), 4); w += "WAVEfmt "; Le(w, 16, 4); Le(w, 1, 2); Le(w, 1, 2);
    Le(w, 8000, 4); Le(w, 8000 * bits / 8, 4); Le(w, bits / 8, 2); Le(w, (std::uint32_t)bits, 2);
    w += "data"; Le(w, (std::uint32_t)data.size(), 4); w += data;
    return w;
}

// SDL_LoadWAV hands back SDL's own buffer. The launcher used to re-point the clip at a converted copy (or just keep it
// forever) and never call SDL_FreeWAV, so every voice note loaded in a long session leaked its samples.
int main() {
    RUN_SUITE("launcher_voice_clip");
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "okay_test_voice_clip";
    fs::create_directories(dir, ec);

    // A clip converted to 16-bit gives SDL's buffer back immediately and plays from its own copy.
    {
        const fs::path f = dir / "u8.wav";
        { std::ofstream o(f, std::ios::binary); const std::string w = MakeWav(8, 64); o.write(w.data(), (std::streamsize)w.size()); }
        g_freed = 0;
        {
            VoiceClip c; c.freeWav = &CountFree;
            CHECK(VoiceClipLoadFile(c, f.string()));
            CHECK(c.spec.format == AUDIO_S16SYS);
            CHECK(c.len == 128 && c.pcm == c.owned.data());
            CHECK(c.wav == nullptr);
            CHECK(g_freed == 1);          // freed at conversion, not left until exit
        }
        CHECK(g_freed == 1);              // and not freed twice by the destructor
    }

    // A clip that plays as loaded (native 16-bit) keeps SDL's buffer until the clip is destroyed, then frees it once.
    {
        const fs::path f = dir / "s16.wav";
        { std::ofstream o(f, std::ios::binary); const std::string w = MakeWav(16, 64); o.write(w.data(), (std::streamsize)w.size()); }
        g_freed = 0;
        {
            VoiceClip c; c.freeWav = &CountFree;
            CHECK(VoiceClipLoadFile(c, f.string()));
            CHECK(c.wav != nullptr && c.pcm == c.wav && c.len == 128);
            CHECK(g_freed == 0);
        }
        CHECK(g_freed == 1);
    }

    // A file SDL cannot read leaves nothing to free.
    {
        g_freed = 0;
        {
            VoiceClip c; c.freeWav = &CountFree;
            CHECK(!VoiceClipLoadFile(c, (dir / "missing.wav").string()));
        }
        CHECK(g_freed == 0);
    }

    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
