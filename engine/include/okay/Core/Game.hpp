#pragma once
#include "okay/Core/Time.hpp"

namespace okay {

/// Global game-control state the runtimes (player + editor Play) honour: a pause
/// flag (freezes gameplay by zeroing the time scale) and a quit request (the host
/// loop exits when it sees this). Components like PauseMenu drive these; the
/// windowed runtimes read them once per frame.
class Game {
public:
    // ---- Pause ---------------------------------------------------------
    static bool Paused() { return s_paused; }
    /// Pause/resume gameplay. Pausing freezes the time (DeltaTime is 0 and
    /// TimeScale() reads 0, so movement/physics stop). The time scale itself is left
    /// alone: one set while paused is remembered and applies on resume.
    static void SetPaused(bool paused) {
        if (paused == s_paused) return;
        Time::SetFrozen(paused);
        s_paused = paused;
    }
    static void TogglePause() { SetPaused(!s_paused); }

    // ---- Quit ----------------------------------------------------------
    static bool QuitRequested() { return s_quit; }
    static void RequestQuit()   { s_quit = true; }
    /// Clear the flags — call when (re)starting Play so stale state doesn't leak
    /// between runs.
    static void Reset() { s_quit = false; if (s_paused) { Time::SetFrozen(false); s_paused = false; } }

private:
    static inline bool  s_paused    = false;
    static inline bool  s_quit      = false;
};

} // namespace okay
