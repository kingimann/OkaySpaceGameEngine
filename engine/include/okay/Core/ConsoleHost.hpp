#pragma once
// What a place where a person types console lines needs (the player's backquote console, the editor's Console
// panel): the line being typed (history, Tab completion, Enter), `--cvar NAME=VALUE` from the command line, and the
// engine's standard cvars and profiler commands. The console itself is okay/Core/Console.hpp; the frame loop's
// commands are okay/Core/LoopConsole.hpp.
//
//     CommandLine cl;
//     consolehost::DeclareOptions(cl);                 // --cvar NAME=VALUE (repeatable), --console
//     cl.Parse(argc, argv);
//     consolehost::RegisterStandard();                 // game.speed, r.vsync, debug.show_fps, debug.profile, prof.*
//     for (const std::string& w : consolehost::ApplyCvarArgs(cl, consoleAllowed)) Log::Warn(w);
//
//     ConsoleInput line;                               // per frame, while the console is open:
//     line.Type(typedText); line.Backspace(); line.HistoryPrev(); line.Complete(); line.Submit();
//
// Standard cvars (registered by RegisterStandard; a host reads the ones that only it can apply):
//   game.speed      float 0..100   the gameplay time scale (Time::SetTimeScale), like Build Settings > Game speed
//   r.vsync         bool           wait for the display's refresh, for this run only (DisplaySettings::SetVSyncForThisRun:
//                                  the saved display settings keep the player's own choice)
//   debug.show_fps  bool           the small FPS label in the corner (the host draws it)
//   debug.profile   bool           record profiler sections every frame, not only while a profiler view is open
// Standard commands:
//   prof.save <file.json>          the profiler's recorded frames as a Chrome trace (open in a trace viewer)
//   prof.sections                  the newest recorded frame's sections, milliseconds and calls
// Main thread only, like the console.
#include "okay/Core/Console.hpp"
#include <string>
#include <vector>

namespace okay {

class CommandLine;

/// One line of console input: the text typed so far, Up / Down through the history, Tab completion of the first
/// word, Enter to run it.
class ConsoleInput {
public:
    std::string text;

    /// Append typed text. The backquote (the key that opens the console) and control characters are dropped.
    void Type(const std::string& utf8);
    /// Remove the last character (a whole UTF-8 character, not one byte).
    void Backspace();
    /// Older / newer line from Console::History(). Past the newest line comes back to what was being typed.
    void HistoryPrev();
    void HistoryNext();
    /// Complete the first word from the commands and cvars. One match: the whole name and a space. Several: the
    /// part they share, and the names are printed to the console output. Returns the matches.
    std::vector<std::string> Complete();
    /// Run the line: print "> line" to the console output, then Console::Execute it. Clears the text and the
    /// history position. An empty line does nothing.
    Console::Result Submit();

private:
    int m_hist = -1;        // index into Console::History() while browsing, -1 = editing a new line
    std::string m_draft;    // what was being typed when browsing started
};

namespace consolehost {

/// Declare `--cvar NAME=VALUE` (repeatable) and the `--console` flag.
void DeclareOptions(CommandLine& cl);
/// Apply every `--cvar` given, in order, when `allowed` (use PlayerConsoleAllowed's answer in a player; the editor
/// passes true). Returns one warning per value that was not applied ("--cvar nope=1: unknown variable 'nope'").
/// Not allowed: nothing is applied and one warning says they were ignored (none when no --cvar was given), so a
/// shipped game's speed cannot be changed from a shortcut. Register the cvars first.
std::vector<std::string> ApplyCvarArgs(const CommandLine& cl, bool allowed);
std::vector<std::string> ApplyCvarArgs(const std::vector<std::string>& assignments);

/// Register the standard cvars and commands listed at the top (safe to call again). game.speed and r.vsync start
/// at the engine's current values.
void RegisterStandard();
/// Copy the engine's current time scale and vsync into game.speed and r.vsync, so a change made elsewhere (the
/// Settings screen, a script) shows in the console. Call once a frame; it changes nothing else.
void SyncFromEngine();

/// Whether the profiler should record this frame: a profiler view is open, or debug.profile is on.
bool ProfilerWanted(bool viewOpen);

/// Whether a shipped player may open its backquote console: a development build, the `--console` flag, or the
/// environment variable OKAY_CONSOLE set to something other than empty or "0" (pass getenv's result). Off otherwise.
bool PlayerConsoleAllowed(bool developmentBuild, bool consoleFlag, const char* envValue);

/// The newest `maxLines` lines of the console output, oldest first (what an overlay draws above the input line).
std::vector<std::string> TailLines(std::size_t maxLines);

}  // namespace consolehost
}  // namespace okay
