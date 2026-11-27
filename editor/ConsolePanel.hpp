#pragma once
// The editor log (ConsoleLog, written to by every tool) and the Console panel.
// The panel draws ConsoleModel::Rows(): filters, collapse, Error Pause and
// Open Script live on that model, not on a second buffer.
#include "okay/Debug/ConsoleModel.hpp"
#include "okay/Core/LoopConsole.hpp"
#include <memory>
#include <string>

namespace oked {

extern int g_consoleCounts[3];   // info, warning, error currently stored in the model

// Log a line to the Console (0 = info, 1 = warning, 2 = error). A leading
// `[tag] ` (no colon) is the category. The source is parsed from the text and
// from an active ConsoleSourceScope.
void ConsoleLog(const std::string& msg, int level = 0);
void ConsoleClear();
// A second reader of every line (window mode: the OkayUI Console copy). Null (the default) = none. Called after the line is stored.
void ConsoleSetMirror(void (*fn)(const std::string&, int));
// The Console window: severity filters with counts, category, search, collapse, details.
void DrawConsole();
// The console commands behind the panel's command line: the standard cvars and commands (okay/Core/ConsoleHost.hpp),
// the Play loop's time.* commands, and `--cvar NAME=VALUE` from the command line (a bad one is a warning here).
// Keep the result alive while `loop` lives (it removes the time.* commands when destroyed).
std::unique_ptr<okay::LoopConsole> ConsoleAttachHost(okay::GameLoop& loop, int argc, char** argv);
// Run one console line as if typed into the panel: "> line" and the output go into the log (category "console").
void ConsoleRunLine(const std::string& line);
// Demo / tests: the category dropdown starts on this name ("All" clears it).
void ConsolePreferCategory(const std::string& category);
/// The open project (main.cpp, on every project change): Console > Save writes <project>/Logs/console_log.txt, or the
/// person's log folder with no project open (docs/engine/project_layout.md).
void ConsoleSetProjectDir(const std::string& dir);

} // namespace oked
