#pragma once
// What's New history (NewsHistory.cpp): every release's notes this launcher has seen. Moved out of launcher/main.cpp
// unchanged (docs/engine/modularization_plan.md, Part C section 8, batch L4).
#include "LauncherShared.hpp"
#include <string>
#include <vector>

namespace okl {

struct NewsRelease { std::string ver; std::vector<std::string> lines; };
extern std::vector<NewsRelease> g_newsHistory;   // newest first, up to 12 (news.cfg)
extern std::string g_newsSeen;                   // newest version whose notes were opened (prefs)
void LoadNewsHistory();
void SyncNewsHistory();                          // record the latest fetched notes; cheap when nothing changed
} // namespace okl
