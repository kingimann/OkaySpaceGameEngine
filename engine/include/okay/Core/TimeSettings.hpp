#pragma once
// The Project Settings > Time page as plain data: what the editor stores in project.okayproj, what the
// build writes into game.okayconfig, and how the player and the editor's Play mode apply it. Both files
// use the same lines, written only when a value is not its default, so a project that never opened the
// page is written exactly as before and an older build ignores the lines it does not know.
//
//     time_scale=1            speed of the game when it starts (0.5 = half speed)
//     time_max_frame=0.1      a frame longer than this many seconds counts as this long (the hitch clamp)
//     time_catch_up=0.3333    after a hitch the physics may catch up this many seconds of steps in one frame
//     time_policy=drop        drop: time past that is lost; spread: it is paid back over the next frames
//     time_unfocused_fps=0    frame rate cap while the window is not active (0 = no extra cap)
//     time_clock=slowmo|game|0.5   a clock: name | parent clock (empty = real time) | start scale
//
// Written from the settings list above; plain C++17, no threads (it is reachable from the player and the
// editor, which cannot use <mutex> on Windows).
#include "okay/Core/GameLoop.hpp"
#include <iosfwd>
#include <string>
#include <vector>

namespace okay {

class ClockSet;

/// One extra clock in the list: "enemies" under "game" at half speed, and so on.
struct ClockSpec {
    std::string name;
    std::string parent;     ///< "" = runs on real time; else the clock it follows (its scale and pause apply first)
    float scale = 1.0f;     ///< the scale it starts with
    bool operator==(const ClockSpec& o) const { return name == o.name && parent == o.parent && scale == o.scale; }
};

struct TimeSettings {
    static constexpr float kDefaultMaxFrame = 0.1f;          ///< what the player and the editor always used
    static constexpr float kDefaultCatchUp = 1.0f / 3.0f;    ///< what Scene::maxPhysicsCatchUp always was
    static constexpr int   kMaxClocks = 64;

    float timeScale = 1.0f;
    float maxFrameSeconds = kDefaultMaxFrame;
    float maxCatchUpSeconds = kDefaultCatchUp;
    bool  spreadCatchUp = false;
    float unfocusedFps = 0.0f;
    std::vector<ClockSpec> clocks;

    bool IsDefault() const;
    bool operator==(const TimeSettings& o) const;
    /// Pull every number into its range (NaN becomes the default): scale 0 to 100, frame 0.01 to 5 s,
    /// catch-up 0.02 to 5 s, unfocused cap 0 or 5 to 1000 fps; clock names are trimmed, a clock may not be
    /// its own parent, duplicates and unnamed clocks are dropped, at most kMaxClocks.
    void Clamp();

    /// Read one `key=value` line. True when the key is one of the time_ keys (even if the value was bad
    /// and was ignored), false for any other key.
    bool ParseLine(const std::string& key, const std::string& value);
    /// Write the lines for the values that differ from the default (each ends with a newline).
    void WriteLines(std::ostream& out) const;

    /// Put the clamp and the catch-up into a loop's options. `stepSeconds` is the fixed step (0 = default).
    void ApplyToLoop(GameLoop::Options& options, float stepSeconds = 0.0f) const;
    /// Add the listed clocks to `set` (an existing clock keeps its code-defined source but takes the
    /// listed parent and scale) and make children follow their parents.
    void ApplyClocks(ClockSet& set) const;
    /// The game's start: the time scale, the physics catch-up of every scene loaded from now on.
    void ApplyAtStart() const;
    /// The clocks a project usually wants, ready to add: audio and physics under game, network on real time.
    static std::vector<ClockSpec> StandardClocks();
};

} // namespace okay
