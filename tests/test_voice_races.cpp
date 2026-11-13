#include "test_framework.hpp"
#include "../launcher/VoiceChat.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

// The voice session's net thread publishes a failure ("the voice server isn't
// answering") while the launcher reads Error() / StateNow() every frame. Run it
// under ThreadSanitizer: it reports the race; the checks alone can pass by luck.
// Uses SDL's dummy audio driver and a local UDP port nobody answers on.
int main() {
    RUN_SUITE("voice_races");
#if defined(_WIN32)
    _putenv_s("SDL_AUDIODRIVER", "dummy");
#else
    setenv("SDL_AUDIODRIVER", "dummy", 1);
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
