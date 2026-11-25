#pragma once
// The sidebar's line icons, the round chat-ribbon buttons, hover ring and rating stars (NavIcons.cpp). Moved out of
// launcher/main.cpp unchanged (docs/engine/modularization_plan.md, Part C section 8, batch L4).
#include "LauncherShared.hpp"
#include <string>

namespace okl {

// Icon ids past the launcher tabs (0..10), and the round chat-ribbon buttons.
enum { kIcoBell = 11, kIcoChat = 12, kIcoPanel = 13, kIcoRefresh = 14, kIcoShield = 15, kIcoSpark = 16, kIcoBulb = 17, kIcoGlobe = 18, kIcoAlert = 19, kIcoTerminal = 20 };
enum { kRibChat, kRibPlay, kRibCheck, kRibClose, kRibMore };

void DrawNavIcon(ImDrawList* d, int id, ImVec2 c, ImU32 col);   // line icon `id` centred at c
bool RoundIconButton(const char* id, int icon, float d, ImU32 bg, ImU32 bgHover, ImU32 fg, const char* tip);   // a round button with a kRib* icon
std::string ModifiedAgo(const fs::path& p);    // "edited 3 days ago" for a file
void HoverRing();                              // a ring around the last item while it is hovered
float DrawStars(ImDrawList* dl, ImVec2 pos, float sz, float avg, ImU32 on, ImU32 off);   // five rating stars; returns the width
} // namespace okl
