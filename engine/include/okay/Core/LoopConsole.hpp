#pragma once
// Console commands for the frame loop (Core/Console.hpp): look at frame statistics, export them, change
// the time scale, pause and step, from the in-game console, the editor's Console panel or a script of
// console lines (an autoexec file).
//
//     GameLoop loop;
//     LoopConsole commands(loop);               // registers time.stats, time.scale ... until it is destroyed
//     Console::Execute("time.stats");           // "240 frames: 59.8 fps avg (16.72 ms), 1% low 41.2 fps, ..."
//     Console::Execute("time.scale 0.25");      // slow motion for the whole game
//     Console::Execute("time.stats_csv frames.csv");
//
//   time.stats                 frame time summary of the loop's window (FrameStats::ToText) and hitch counters
//   time.stats_csv <file>      write the window as CSV (FrameStats::SaveCsv)
//   time.scale [x]             show / set the gameplay time scale (Time::SetTimeScale; capped, NaN refused)
//   time.debug_scale [x]       show / set the loop's debug time scale (GameLoop::SetDebugTimeScale)
//   time.pause [on|off]        pause / resume as the reason "manual" (no argument toggles it)
//   time.step [n]              while paused, run n frames of simulation (GameLoop::RequestStep)
//   time.run [n] [fps]         run n frames of the running game, then pause (GameLoop::RunFrames); with fps
//                              every one of those frames is exactly 1/fps seconds
//   time.overlay               the performance overlay text (FrameOverlay::Lines)
//
// The prefix ("time.") is a constructor argument, so two loops (a client and a server in one process)
// can each have their own. The object removes its commands when it is destroyed; destroy it before the
// loop (declare it after the loop). Main thread only, like the console.
#include <string>
#include <vector>

namespace okay {

class GameLoop;

class LoopConsole {
public:
    explicit LoopConsole(GameLoop& loop, const std::string& prefix = "time.");
    ~LoopConsole();
    LoopConsole(const LoopConsole&) = delete;
    LoopConsole& operator=(const LoopConsole&) = delete;

    const std::string& Prefix() const { return m_prefix; }

private:
    std::string m_prefix;
    std::vector<std::string> m_names;
};

}  // namespace okay
