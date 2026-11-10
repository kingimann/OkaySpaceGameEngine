#pragma once
#include "okay/Core/Time.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace okay {

/// Global game-control state the runtimes (player + editor Play) honour: a pause
/// flag (freezes gameplay by zeroing the time scale) and a quit request (the host
/// loop exits when it sees this). Components like PauseMenu drive these; the
/// windowed runtimes read them once per frame (GameLoop does it for you).
///
/// Pausing is by REASON. SetPaused(true/false) is one reason ("manual"); PushPause
/// ("cutscene") / PopPause("cutscene") are others, and the game stays paused until
/// every reason is gone, so two systems that pause never un-pause each other:
///
///     Game::PushPause("menu");      // the pause menu opens
///     Game::PushPause("cutscene");  // a cutscene starts under it
///     Game::PopPause("menu");       // menu closes: still paused, the cutscene holds it
///
/// Threads: Game is a main-thread API (it drives Time and calls your guard). The
/// exceptions are QuitRequested(), ForceQuit(), QuitReason() and ExitCode(), which a
/// worker thread, a signal handler's flag-setter or a network callback may use.
class Game {
public:
    static constexpr const char* kManualPause = "manual";   // what SetPaused() uses
    static constexpr const char* kFocusPause  = "focus";    // what pause-on-focus-loss uses

    // ---- Pause ---------------------------------------------------------
    static bool Paused() { return s_paused; }
    /// Pause/resume gameplay. Pausing freezes the time (DeltaTime is 0 and
    /// TimeScale() reads 0, so movement/physics stop). The time scale itself is left
    /// alone: one set while paused is remembered and applies on resume.
    /// This is the reason "manual": SetPaused(false) removes only that one, so it does
    /// not resume a pause somebody else holds with PushPause.
    static void SetPaused(bool paused);
    /// The pause button: flips the "manual" hold (SetPaused's reason) and returns whether it is now
    /// held. It always does what the player asked, even while another system holds a pause: with a
    /// cutscene running (Paused() already true) the first press ADDS the manual hold, so the game
    /// stays paused after the cutscene ends, and the next press releases it. It used to flip
    /// "is the game paused" and so did nothing in that case (SetPaused(false) cannot release
    /// somebody else's hold). Paused() can still be true after a press that released the manual hold:
    /// ask PauseReasons() who else holds it.
    static bool TogglePause();

    /// Hold the game paused for `reason` (any short name: "menu", "cutscene", "dialogue").
    /// Pushes of the same reason count, so two PushPause("menu") need two PopPause("menu").
    /// An empty reason is stored as "unnamed".
    static void PushPause(const std::string& reason);
    /// Release one hold of `reason`. False (and nothing changes) when it is not held.
    static bool PopPause(const std::string& reason);
    /// Release every hold of `reason` at once (a screen that closes without knowing how
    /// many times it pushed).
    static void ClearPause(const std::string& reason);
    static bool IsPausedBy(const std::string& reason);
    /// The reasons holding the pause, oldest first.
    static std::vector<std::string> PauseReasons();

    /// Pause the game while the window is not focused (default off). The platform layer
    /// calls NotifyFocus(false/true) from its focus-lost / focus-gained event; the pause
    /// is the reason "focus", so a menu opened before the alt-tab stays paused after it.
    static void SetPauseOnFocusLoss(bool on);
    static bool PauseOnFocusLoss();
    static void NotifyFocus(bool focused);
    static bool HasFocus();

    // ---- Quit ----------------------------------------------------------
    static bool QuitRequested() { return s_quit.load(std::memory_order_acquire); }
    /// Ask to quit, with a reason for the log or a crash report and the process exit
    /// code (0 = clean). Returns true when the request stands. False when the quit
    /// guard (SetQuitGuard) vetoed it, so the game carries on. Asking again while a quit
    /// is pending keeps the first reason, but a non-zero code replaces a zero one: an
    /// error is never hidden by a clean quit that came after.
    static bool RequestQuit(const std::string& reason = std::string(), int exitCode = 0);
    /// Quit without asking the guard (the OS is shutting down, a fatal error). Safe from
    /// any thread.
    static void ForceQuit(const std::string& reason = std::string(), int exitCode = 0);
    /// Withdraw a pending request (the player chose "keep playing" in a confirm dialog).
    static void CancelQuit();
    static std::string QuitReason();
    static int ExitCode();
    /// Called before a RequestQuit is accepted: return true to allow it, false to veto
    /// it ("you have unsaved changes": open a dialog and let the dialog's Quit button call
    /// ForceQuit). It runs on the thread that called RequestQuit. A guard that throws
    /// allows the quit (a bug in it must not trap the player). An empty function removes it.
    static void SetQuitGuard(std::function<bool()> guard);
    /// How many requests the guard has vetoed since Reset.
    static int QuitVetoCount();

    /// Clear the flags — call when (re)starting Play so stale state doesn't leak
    /// between runs: every pause reason, the quit request (reason, code), the quit guard
    /// and any hit stop or time-scale ramp. The time scale itself is left alone (use
    /// BeginSession for a clean slate). A window that is still unfocused with
    /// pause-on-focus-loss on starts paused.
    static void Reset();
    /// Reset() plus the time scale back to 1 and Time::MarkSessionStart(): everything a new Play
    /// session or scene load should start from. Time::ElapsedTime() and FrameCount() are NOT
    /// rewound (stamps taken from them stay valid); Time::SessionTime() and SessionFrameCount()
    /// start again from 0.
    static void BeginSession();

private:
    static void Refresh();
    static inline bool              s_paused = false;
    static inline std::atomic<bool> s_quit{false};
};

} // namespace okay
