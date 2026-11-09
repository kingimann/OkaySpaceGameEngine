#include "okay/Core/LoopConsole.hpp"
#include "okay/Core/Console.hpp"
#include "okay/Core/FrameOverlay.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Time.hpp"

#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>

namespace okay {

namespace {

using Args = std::vector<std::string>;

std::string Num(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

// A finite number from text, with nothing left over ("0.5", not "0.5x", "nan" or "0x10"), read with
// a dot whatever the process locale is.
bool ParseNumber(const std::string& text, double& out) {
    if (text.empty()) return false;
    std::istringstream is(text);
    is.imbue(std::locale::classic());
    double v = 0.0;
    is >> v;
    if (is.fail()) return false;
    is >> std::ws;
    if (!is.eof() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

}  // namespace

LoopConsole::LoopConsole(GameLoop& loop, const std::string& prefix) : m_prefix(prefix) {
    GameLoop* l = &loop;
    auto add = [&](const std::string& name, Console::Command fn, const std::string& help, const std::string& usage) {
        Console::Register(m_prefix + name, std::move(fn), help, usage.empty() ? std::string() : m_prefix + usage);
        m_names.push_back(m_prefix + name);
    };

    add("stats", [l](const Args&) {
        std::string out = l->Stats().ToText();
        out += "\n" + std::to_string(l->FrameCount()) + " frames run, " + std::to_string(l->HitchCount()) + " hitches (worst " +
               Num(l->WorstHitch() * 1000.0) + " ms), " + std::to_string(l->ClampedFrames()) + " clamped, " +
               Num(l->DroppedFixedTime()) + " s of fixed time dropped";
        return out;
    }, "frame time summary of the loop", "stats");

    add("stats_csv", [l](const Args& a) -> std::string {
        if (a.empty()) return "error: usage: " "stats_csv <file>";
        std::string why;
        if (!l->Stats().SaveCsv(a[0], &why)) return "error: " + why;
        return "wrote " + std::to_string(l->Stats().Size()) + " frames to " + a[0];
    }, "write the frame times of the stats window as CSV", "stats_csv <file>");

    add("scale", [](const Args& a) -> std::string {
        if (a.empty()) return "time.scale = " + Num(Time::BaseTimeScale());
        double v = 0.0;
        if (!ParseNumber(a[0], v) || v < 0.0) return "error: expected a number >= 0, got '" + a[0] + "'";
        Time::SetTimeScale((float)v);
        return "time.scale = " + Num(Time::BaseTimeScale());
    }, "show or set the gameplay time scale", "scale [x]");

    add("debug_scale", [l](const Args& a) -> std::string {
        if (a.empty()) return "debug scale = " + Num(l->DebugTimeScale());
        double v = 0.0;
        if (!ParseNumber(a[0], v) || !l->SetDebugTimeScale((float)v)) return "error: expected a number >= 0, got '" + a[0] + "'";
        return "debug scale = " + Num(l->DebugTimeScale());
    }, "show or set the loop's debug time scale (slow motion or fast forward for debugging)", "debug_scale [x]");

    add("pause", [](const Args& a) -> std::string {
        bool on = !Game::IsPausedBy(Game::kManualPause);
        if (!a.empty()) {
            if (a[0] == "on" || a[0] == "1" || a[0] == "true") on = true;
            else if (a[0] == "off" || a[0] == "0" || a[0] == "false") on = false;
            else return "error: expected on or off, got '" + a[0] + "'";
        }
        Game::SetPaused(on);
        return Game::Paused() ? (on ? "paused" : "still paused by: " + Game::PauseReasons().front()) : "running";
    }, "pause or resume the game (no argument toggles)", "pause [on|off]");

    add("step", [l](const Args& a) -> std::string {
        double n = 1.0;
        if (!a.empty() && (!ParseNumber(a[0], n) || n < 1.0 || n > (double)GameLoop::kMaxStepRequest)) return "error: expected a whole number of frames >= 1";
        if (!l->RequestStep((int)n)) return "error: the game is not paused (time.pause on)";
        return "stepping " + std::to_string(l->StepsRemaining()) + " frame(s)";
    }, "while paused, run n frames of simulation", "step [n]");

    add("run", [l](const Args& a) -> std::string {
        double n = 1.0, fps = 0.0;
        if (!a.empty() && (!ParseNumber(a[0], n) || n < 1.0 || n > (double)GameLoop::kMaxStepRequest)) return "error: expected a whole number of frames >= 1";
        if (a.size() > 1 && (!ParseNumber(a[1], fps) || fps <= 0.0 || fps > (double)GameLoop::kMaxCaptureFps)) return "error: expected a frame rate above 0 and up to 1000";
        const bool wasPaused = Game::Paused();
        if (!l->RunFrames((int)n, fps > 0.0 ? (float)(1.0 / fps) : 0.0f)) return "error: could not start the run";
        return wasPaused ? "stepping " + std::to_string((int)n) + " frame(s), the game stays paused"
                         : "running " + std::to_string((int)n) + " frame(s), then pausing";
    }, "run n frames of the running game, then pause (a frame rate makes every frame exactly that long)", "run [n] [fps]");

    add("overlay", [l](const Args&) { return FrameOverlay::Text(*l); }, "the performance overlay text: frame times, hitches, time scale, pause reasons", "overlay");
}

LoopConsole::~LoopConsole() {
    for (const std::string& name : m_names) Console::Unregister(name);
}

}  // namespace okay
