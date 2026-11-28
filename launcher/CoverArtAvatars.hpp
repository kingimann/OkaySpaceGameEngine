#pragma once
// Game cover art and avatar textures (CoverArtAvatars.cpp). Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 6, batch L3).
#include "LauncherShared.hpp"
#include <map>
#include <set>
#include <string>

namespace okl {

// The texture caches, one per render slot (g_rslot): game path / avatar URL -> texture (nullptr = none, cached).
extern std::map<std::string, okay::ui2d::Tex> g_coverCaches[2];
#define g_coverCache g_coverCaches[g_rslot]
extern std::map<std::string, okay::ui2d::Tex> g_avatarCaches[2];
#define g_avatarCache g_avatarCaches[g_rslot]
extern std::string* g_myAvatarSlot;                  // points at the Account card's cached avatar URL
// The background avatar download (the chat's image downloads share it).
extern okay::thr::Mutex* g_avatarMutex;              // guards the two sets below
extern std::set<std::string> g_avatarPending;        // URLs currently downloading
extern std::set<std::string> g_avatarFailed;         // URLs that failed (don't retry)
struct AvatarDl { std::string url, path; };
int AvatarDlWorker(void* p);                         // thread body: download one AvatarDl, then delete it

okay::ui2d::Tex GameCover(const std::string& gamePath);                                // a game's cover texture, or nullptr
void DrawGameIcon(const std::string& gamePath, const std::string& name, float size); // the cover, else the initial tile
okay::ui2d::Tex AvatarTexture(const std::string& url);                                 // an avatar texture (downloads in the background)
void DrawAvatar(const std::string& url, const std::string& name, float size);       // a round avatar, else a coloured initial
} // namespace okl
