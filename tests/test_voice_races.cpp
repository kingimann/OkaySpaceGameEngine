#include "test_framework.hpp"
#include "../launcher/VoiceChat.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>

// The voice session's net thread publishes a failure ("the voice server isn't
// answering") while the launcher reads Error() / StateNow() every frame. Run it
// under ThreadSanitizer: it reports the race; the checks alone can pass by luck.
// Uses a sound device that needs no sound card (OKAY_AUDIO_DEVICE=null, the platform layer's null device) and a local UDP port
// nobody answers on.
int main() {
    RUN_SUITE("voice_races");
    // ---- the codec and cipher are the engine's shared ones (provenance audit N1) ----
    {
        // One IMA table: the voice codec reads the WAV reader's.
        CHECK(okay::voice::AdpcmSteps() == okay::wav::ImaAdpcmSteps());
        CHECK(okay::voice::AdpcmSteps()[0] == 7 && okay::voice::AdpcmSteps()[88] == 32767);
        // A 20 ms tone survives encode -> decode closely (4-bit ADPCM).
        std::int16_t pcm[okay::voice::kFrame], back[okay::voice::kFrame];
        for (int i = 0; i < okay::voice::kFrame; ++i) pcm[i] = (std::int16_t)(8000.0 * std::sin(i * 0.11));
        std::uint8_t codes[okay::voice::kFrame / 2];
        okay::voice::AdpcmState e, d;
        okay::voice::AdpcmEncode(pcm, okay::voice::kFrame, e, codes);
        okay::voice::AdpcmDecode(codes, okay::voice::kFrame, d, back);
        long worst = 0;
        for (int i = 40; i < okay::voice::kFrame; ++i) worst = std::max(worst, std::labs((long)pcm[i] - back[i]));
        CHECK(worst < 600);
        // The cipher is ChaCha20 with block counter 1: the same bytes as the engine's ChaCha20 (whichever backend
        // the build uses), and applying it twice gives the frame back.
        std::array<std::uint8_t, 32> key{};
        for (int i = 0; i < 32; ++i) key[i] = (std::uint8_t)(i * 7 + 1);
        const std::uint8_t nonce[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        std::vector<std::uint8_t> frame(333), ref(333), out;
        for (std::size_t i = 0; i < frame.size(); ++i) frame[i] = (std::uint8_t)(i * 13 + 5);
        out = frame;
        okay::voice::ChaCha20Xor(key, nonce, out.data(), out.size());
        CHECK(okay::crypto::ChaCha20Xor(ref.data(), frame.data(), frame.size(), key.data(), nonce, 1));
        CHECK(out == ref);
        okay::voice::ChaCha20Xor(key, nonce, out.data(), out.size());
        CHECK(out == frame);
    }
    // The platform layer's null audio device (engine/src/Platform/NullAudio.cpp), on every backend: CI runners have no
    // sound card (on the Windows runner WASAPI has no default device, so Start failed with "No audio output device").
#if defined(_WIN32)
    _putenv_s("OKAY_AUDIO_DEVICE", "null");
#else
    setenv("OKAY_AUDIO_DEVICE", "null", 1);
#endif
    using okay::voice::Session;
    Session s;
    const bool started = s.Start("127.0.0.1", 45999, "room", "key", "uid", "name");
    CHECK(started);
    if (started) {
        // Hammer the accessors the launcher calls each frame until the net thread gives up (~8 s).
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        std::size_t seen = 0;
        while (s.StateNow() != Session::Failed && std::chrono::steady_clock::now() < until) {
            seen += s.Error().size();   // a copy: never a reference into the string the net thread assigns
            (void)s.Active(); (void)s.SelfLevel(); (void)s.SelfSpeaking();
        }
        CHECK(s.StateNow() == Session::Failed);
        CHECK(s.Error() == "The voice server isn't answering");
        (void)seen;
        s.Stop();
        CHECK(s.StateNow() == Session::Failed);   // a failure stays visible after Stop
        CHECK(s.Error() == "The voice server isn't answering");
    }
    TEST_MAIN_RESULT();
}
