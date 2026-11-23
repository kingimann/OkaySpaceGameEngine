#pragma once
// The public profile page (PublicProfilePage.cpp). Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 21, batch L6).
#include "LauncherShared.hpp"
#include <string>
#include <vector>

namespace okl {

struct PfLocalShowcase { std::vector<std::string> ach; std::vector<long> games; int banner = -1; };
// Sample profiles for the OKAY_UI_DEMO_USER screenshot hook.
// Social links a profile can show: key, label, URL prefix for a handle ("" =
// the value is a full URL (website) or not a link at all (Discord tag)).
struct SocialPlatform { const char* key; const char* label; const char* prefix; ImU32 color; };
static const SocialPlatform kSocials[] = {
    {"x",         "X",         "https://x.com/",          IM_COL32(230, 233, 240, 255)},
    {"youtube",   "YouTube",   "https://youtube.com/@",   IM_COL32(255, 72, 72, 255)},
    {"twitch",    "Twitch",    "https://twitch.tv/",      IM_COL32(160, 110, 255, 255)},
    {"tiktok",    "TikTok",    "https://tiktok.com/@",    IM_COL32(60, 220, 230, 255)},
    {"instagram", "Instagram", "https://instagram.com/",  IM_COL32(240, 90, 160, 255)},
    {"discord",   "Discord",   "",                        IM_COL32(110, 130, 245, 255)},
    {"website",   "Website",   "",                        IM_COL32(120, 200, 150, 255)},
};
struct PfPageOut { int gotoTab = -1; std::string marketSearch; };

// What main.cpp uses from PublicProfilePage.cpp.
extern bool g_pfOpen;                                  // the profile page is showing
void ClosePublicProfile();
PfLocalShowcase LoadLocalShowcase();                   // my showcase picks kept on this device
void SaveLocalShowcase(const PfLocalShowcase& s);
std::string CleanSocial(const std::string& key, std::string v);   // what a user typed -> a stored handle or URL ("" = invalid)
void DrawAchMedal(ImDrawList* dl, ImVec2 c, float r, const std::string& id, float alpha);   // an achievement's hexagon medal
std::string PfPrettyId(const std::string& id);         // "first_win" -> "First Win"
PfPageOut DrawPublicProfilePage(okay::account::AccountService& account, int fromTab);
} // namespace okl
