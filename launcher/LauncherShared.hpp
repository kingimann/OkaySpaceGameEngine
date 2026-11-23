#pragma once
// Functions and state still defined in launcher/main.cpp that the split-out launcher files use (the launcher's
// editor/MainShared.hpp; docs/engine/modularization_plan.md, Part C). This list shrinks as main.cpp is split further;
// each entry moves to the header of the file that ends up owning it. Everything is in namespace okl (the launcher's
// names; main.cpp's anonymous namespace became okl so the split-out files can reach them).
#include "imgui.h"
#include <SDL.h>
#include "OkayThreads.hpp"   // okay::thr (the launcher's mutexes and threads)
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;   // the same alias as main.cpp's

namespace okl {

// Look and messages (main.cpp).
extern ImVec4 kAccent;                          // the accent colour (Settings > Appearance)
ImVec4 NameColor(const std::string& s);         // a stable colour for a user name
void Toast(const std::string& m, int kind);     // a short message in the corner (kind: 0 info, 1 success, 2 error)
// Settings (main.cpp, persisted in the prefs file).
extern bool g_linkPreviews;                     // fetch title / image cards for links in chat
// Opening things and running commands (main.cpp).
bool ShellSafeUrl(const std::string& url);      // a URL that is safe to hand to the shell / browser
void OpenExternal(const std::string& target);   // open a URL or folder with the system's default program
int RunCmd(const std::string& cmd, bool wait);  // run a command without a console window; wait = return its exit code
// Chat media (main.cpp).
void DrawEmojiToken(ImDrawList* dl, ImVec2 c, float s, const std::string& tok);   // any emoji token centred at c
SDL_Texture* ChatImageTexture(const std::string& url, int* w = nullptr, int* h = nullptr);   // a chat image / GIF frame (cached)
std::string ChatMediaPicker(const char* id, char* buf, std::size_t cap,
                            const std::vector<std::string>* recentGifs, bool canSendMedia);   // emoji / GIF / sticker picker
} // namespace okl
