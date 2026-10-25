#pragma once
// The editor log (ConsoleLog, written to by every tool) and the Console panel.
#include <string>
#include <vector>

namespace oked {

// A console entry: severity, text, a short timestamp, and a repeat
// count (incremented when the same line is logged twice in a row).
struct ConsoleEntry {
    int level = 0;            // 0 = info, 1 = warning, 2 = error
    std::string text;
    std::string time;
    int count = 1;
};
extern std::vector<ConsoleEntry> g_console;
extern int g_consoleCounts[3];   // running totals per level (for the toggles)

// Log a line to the Console (0 = info, 1 = warning, 2 = error). A repeat of the
// last line bumps its count instead of adding a row.
void ConsoleLog(const std::string& msg, int level = 0);
void ConsoleClear();
// The Console window: severity filters with counts, search, collapse, details.
void DrawConsole();

} // namespace oked
