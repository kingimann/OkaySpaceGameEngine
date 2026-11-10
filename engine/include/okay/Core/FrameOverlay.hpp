#pragma once
// The text of a performance overlay, made once for every host: the player's F3 panel and the editor's
// Game view stats show the same lines, so a number read in one is the number in the other.
//
//     const std::vector<std::string> lines = FrameOverlay::Lines(loop);     // once per UI refresh
//     for (const std::string& l : lines) DrawText(l);
//
//     59.8 fps  16.72 ms avg
//     min 15.90  max 22.10  p99 20.30 ms
//     1% low 48.2 fps  0.1% low 40.1 fps
//     hitches 2 (worst 130 ms)  dropped 1 frame (0.15 s lost)
//     fixed steps dropped 0.04 s
//     time x0.50  hit stop 0.08 s  debug x0.25
//     paused by: menu, cutscene
//
// The frame numbers come from GameLoop::Stats() (the last Options::statsWindow frames' REAL times, so a 5 s
// stall reads 5000 ms even though the game stepped 0.25 s), the rest from Time, Game and the loop's own
// counters. Lines that have nothing to say are left out: the hitch line only once a frame hitched, "dropped"
// only after the clamp threw time away, the fixed-step line only when steps were dropped, the paused line
// only while paused. The time line is always there (it shows the gameplay time scale, a running hit stop or
// ramp, the debug time scale, capture mode and a pending run or step). Numbers use a dot whatever the process
// locale, so a test can pin the text.
//
// Format(snapshot, state) is the pure form, for a test or for a host that has its own source of numbers:
// it reads no global. Lines(loop) = Format(loop.Stats().Snapshot(), StateOf(loop)). Snapshot() sorts a copy
// of the window, so call it once per refresh (not once per line). Main thread only, like Time and Game.
#include "okay/Core/FrameStats.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

class GameLoop;

/// Everything the overlay shows besides the frame times.
struct FrameOverlayState {
    float timeScale = 1.0f;            ///< the gameplay time scale (Time::BaseTimeScale())
    bool  hitStop = false;             ///< a hit stop is running
    float hitStopSeconds = 0.0f;       ///< real seconds until the last running one ends
    bool  ramping = false;             ///< a Time::RampTimeScale is running
    float rampTarget = 1.0f;
    float debugTimeScale = 1.0f;       ///< GameLoop::DebugTimeScale()
    float captureFps = 0.0f;           ///< > 0 in capture mode
    int   runFramesLeft = 0;           ///< GameLoop::RunFramesRemaining()
    int   stepsLeft = 0;               ///< GameLoop::StepsRemaining()
    int   clampedFrames = 0;           ///< frames the max-delta clamp shortened
    double lostRealSeconds = 0.0;      ///< real time the clamp threw away
    double droppedFixedSeconds = 0.0;  ///< fixed-step time dropped (a frame owed more than maxFixedSteps)
    std::vector<std::string> pausedBy; ///< Game::PauseReasons(), oldest first (empty = running)
};

class FrameOverlay {
public:
    /// The state of the loop, Time and Game right now.
    static FrameOverlayState StateOf(const GameLoop& loop);

    /// The overlay lines (see the top of the file). With no frame in the window the first line says so and
    /// the frame-time lines are left out; the time and paused lines still show.
    static std::vector<std::string> Format(const FrameStatsSnapshot& stats, const FrameOverlayState& state);
    static std::vector<std::string> Lines(const GameLoop& loop);

    /// The lines joined with '\n', for a multi-line text widget.
    static std::string Text(const std::vector<std::string>& lines);
    static std::string Text(const GameLoop& loop) { return Text(Lines(loop)); }

    /// The one-line readout of the "show FPS" switch: "FPS 60", the frames-per-second over the newest
    /// `recentFrames` (30) of the loop's REAL frame times rounded to a whole number, "FPS --" while there are
    /// no frames yet. The player's `show_fps` readout used to show the frame time cut to the step limit
    /// (never below 10 FPS) and smoothed by hand; a 5 s stall now reads as one.
    static std::string FpsLabel(const FrameStats& stats, std::size_t recentFrames = 30);
    static std::string FpsLabel(const GameLoop& loop, std::size_t recentFrames = 30);

    /// The last `count` frames as frames-per-second, oldest first, for a plot of fixed size: fewer frames than
    /// `count` pad the front with 0, a 0 ms frame reads 0. `count` is capped at 4096.
    static std::vector<float> RecentFps(const FrameStats& stats, std::size_t count);
    /// The same as frame times in milliseconds (a frame-time graph).
    static std::vector<float> RecentMs(const FrameStats& stats, std::size_t count);
};

}  // namespace okay
