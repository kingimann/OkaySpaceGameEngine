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
    /// Pause/resume gameplay. Pausing stores the current time scale and sets it to
    /// 0 (so DeltaTime is 0 and movement/physics freeze); resuming restores it.
    static void SetPaused(bool paused) {
        if (paused == s_paused) return;
        if (paused) { s_prevScale = Time::TimeScale(); Time::SetTimeScale(0.0f); }
        else        { Time::SetTimeScale(s_prevScale); }
        s_paused = paused;
    }
    static void TogglePause() { SetPaused(!s_paused); }

    // ---- Quit ----------------------------------------------------------
    static bool QuitRequested() { return s_quit; }
    static void RequestQuit()   { s_quit = true; }
    /// Clear the flags — call when (re)starting Play so stale state doesn't leak
    /// between runs.
    static void Reset() { s_quit = false; if (s_paused) { Time::SetTimeScale(s_prevScale); s_paused = false; } }

private:
    static inline bool  s_paused    = false;
    static inline bool  s_quit      = false;
    static inline float s_prevScale = 1.0f;
};

} // namespace okay
