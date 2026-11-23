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

namespace okay { struct JoinLink; }
namespace okay { struct MyBanInfo; namespace account { class AccountService; } }

// An RGBA8 picture as a texture of `renderer` (main.cpp, global scope); null when it cannot be made.
SDL_Texture* TextureFromRgba(SDL_Renderer* renderer, const unsigned char* rgba, int w, int h);

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
// Shared look helpers (main.cpp).
std::string RelTime(long when);                // "5 min ago"
bool PrimaryButton(const char* label, const ImVec2& size);   // the accent-coloured main button
std::string FormatDate(long t);                // a date for lists
bool ToggleSwitch(const char* id, bool* v);     // the on/off pill switch; true on the frame it flips
std::string FitText(const std::string& s, float maxW);   // s shortened with "..." to fit maxW
void DrawVerifiedBadge(ImDrawList* d, ImVec2 c, float r);   // the blue "verified" tick
// State and config (main.cpp).
extern std::string g_exeDir;                   // the folder the launcher runs from
extern SDL_Renderer* g_renderer;               // the renderer textures are uploaded to (switched while the chat window draws)
extern int g_rslot;                            // render slot: 0 = launcher window, 1 = chat window (each texture cache keeps one copy per slot)
bool Download(const std::string& url, const fs::path& out);   // fetch a URL to a file (blocking)
extern std::string g_pollCfgDir, g_pollUrl, g_pollKey;   // account config for worker threads (set once at startup)
extern int g_myRank;                           // 3 admin, 2 staff, 1 moderator, 0 player, -1 unknown / not set up
extern okay::MyBanInfo g_myBan;                // my own site ban, if any
extern std::string g_acReportUid, g_acReportName; extern bool g_acReportOpen;   // the "Report cheating" dialog's target
// Public profiles (main.cpp).
void OpenPublicProfile(const std::string& uid, const std::string& username,
                       const std::string& name = std::string(), const std::string& avatar = std::string(),
                       int accent = 0);   // open someone's public profile page
// The official game (main.cpp).
void DrawOfficialAdmin(okay::account::AccountService& account);   // Admin > Official game
// Multiplayer (main.cpp).
void MpLaunch(const okay::JoinLink& in);       // start the game to join a server
// Updates and versions (main.cpp).
void GetNews(std::string& ver, std::string& lines);   // the latest release notes fetched by the update check
int CompareVersions(const std::string& a, const std::string& b);   // <0, 0, >0 like strcmp
// Marketplace listing types (workshop_items.kind).
struct MktKind { const char* key; const char* label; const char* plural; const char* desc; ImVec4 color; };
static const MktKind kMktKinds[] = {
    {"game",     "Full game",   "Games",       "A finished, playable game (File > Build Game).",   ImVec4(0.26f, 0.55f, 0.85f, 1)},
    {"template", "Template",    "Templates",   "A starter project others build on.",               ImVec4(0.58f, 0.45f, 0.82f, 1)},
    {"model",    "3D model",    "3D models",   "Meshes and characters (.obj, .gltf, .fbx ...).",   ImVec4(0.90f, 0.55f, 0.25f, 1)},
    {"texture",  "Texture",     "Textures",    "Materials, PBR sets, skyboxes.",                   ImVec4(0.30f, 0.70f, 0.55f, 1)},
    {"sprite",   "2D art",      "2D art",      "Sprites, tilesets, UI packs.",                     ImVec4(0.92f, 0.42f, 0.62f, 1)},
    {"audio",    "Audio",       "Audio",       "Music, sound effects, voice.",                     ImVec4(0.95f, 0.75f, 0.25f, 1)},
    {"script",   "Script",      "Scripts",     "OkayScript code, behaviours, visual graphs.",      ImVec4(0.35f, 0.80f, 0.85f, 1)},
    {"tool",     "Tool",        "Tools",       "Editor tools, plugins and utilities.",             ImVec4(0.60f, 0.65f, 0.72f, 1)},
    {"scene",    "Scene",       "Scenes",      "A single scene or level.",                         ImVec4(0.30f, 0.62f, 0.40f, 1)},
};
static const int kMktKindN = (int)(sizeof(kMktKinds) / sizeof(kMktKinds[0]));
int MktKindIndex(const std::string& k);       // the kMktKinds row of a kind key (unknown -> game)
} // namespace okl
