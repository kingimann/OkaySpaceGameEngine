#pragma once
// The editor log (ConsoleLog, written to by every tool) and the Console panel.
// The panel draws ConsoleModel::Rows(): filters, collapse, Error Pause and
// Open Script live on that model, not on a second buffer.
#include "okay/Debug/ConsoleModel.hpp"
#include <string>

namespace oked {

extern int g_consoleCounts[3];   // info, warning, error currently stored in the model

// Log a line to the Console (0 = info, 1 = warning, 2 = error). A leading
// `[tag] ` (no colon) is the category. The source is parsed from the text and
// from an active ConsoleSourceScope.
void ConsoleLog(const std::string& msg, int level = 0);
void ConsoleClear();
// The Console window: severity filters with counts, category, search, collapse, details.
void DrawConsole();
#ifdef OKAY_HAVE_OKAYUI
// The panel's OkayUI port (the opt-in backend): called by DrawConsole in place of the ImGui body.
void DrawConsoleOkayUI(const char* windowTitle);
#endif
// Demo / tests: the category dropdown starts on this name ("All" clears it).
void ConsolePreferCategory(const std::string& category);

} // namespace oked
