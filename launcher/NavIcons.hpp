#pragma once
// The sidebar's line icons, the round chat-ribbon buttons, hover ring and rating stars (NavIcons.cpp). Moved out of
// launcher/main.cpp unchanged (docs/engine/modularization_plan.md, Part C section 8, batch L4).
// Also the launcher's page kit (one copy for every page): the search pill, filter chips, the centred empty state,
// loading placeholders and the small-caps section label. Where they go is worked out in PageLayout.hpp.
#include "LauncherShared.hpp"
#include <string>

namespace okl {

// Icon ids past the launcher tabs (0..10), and the round chat-ribbon buttons.
enum { kIcoBell = 11, kIcoChat = 12, kIcoPanel = 13, kIcoRefresh = 14, kIcoShield = 15, kIcoSpark = 16, kIcoBulb = 17, kIcoGlobe = 18, kIcoAlert = 19, kIcoTerminal = 20,
       kIcoSearch = 21, kIcoStar = 22, kIcoSort = 23, kIcoCode = 24, kIcoNodes = 25 };
enum { kRibChat, kRibPlay, kRibCheck, kRibClose, kRibMore, kRibStar, kRibStarOn };   // kRibStar: an outline star, kRibStarOn: a filled gold one

void DrawNavIcon(ImDrawList* d, int id, ImVec2 c, ImU32 col);   // line icon `id` centred at c
bool RoundIconButton(const char* id, int icon, float d, ImU32 bg, ImU32 bgHover, ImU32 fg, const char* tip);   // a round button with a kRib* icon
std::string ModifiedAgo(const fs::path& p);    // "edited 3 days ago" for a file
void HoverRing();                              // a ring around the last item while it is hovered
float DrawStars(ImDrawList* dl, ImVec2 pos, float sz, float avg, ImU32 on, ImU32 off);   // five rating stars; returns the width

// ---- Page kit ----
extern float g_uiScale;                        // Settings > Appearance > UI scale (main.cpp)
inline float UiPx(float px) { return px * g_uiScale; }   // a layout size in pixels at the current UI scale
/// The height a page leaves free at the bottom of the content panel for the pinned footer (the version line): 46 px
/// at UI scale 1, growing with the text.
inline float PageFooterH() { return ImGui::GetTextLineHeight() + 29.0f; }
/// The rounded search field with a drawn magnifier, `w` wide (-1 = the rest of the row). True when Enter was pressed
/// (with ImGuiInputTextFlags_EnterReturnsTrue) or the text changed. `h` > 0: the field's height (else text + 14 px).
bool SearchPill(const char* id, const char* hint, char* buf, std::size_t cap, float w, int flags = 0, float h = 0.0f);
/// `s` on at most two lines of `maxW`: the first line that fits, the rest shortened with "..." onto the second.
void WrapTwoLines(const std::string& s, float maxW, std::string& l1, std::string& l2);
/// One filter chip of a chip row.
struct Chip { std::string label; int count = -1; ImU32 dot = 0; bool countBadge = false; };
/// A wrapping row of filter chips from the cursor, `availW` wide; `selected` is lit. Returns the chip clicked (-1 none)
/// and leaves the cursor under the row.
int ChipRow(const char* id, const std::vector<Chip>& chips, int selected, float availW);
/// The centred empty / error / signed-out message: a round icon, a title and a dimmed line (wrapped to `availW`).
/// `ok` adds the small green "all done" tick to the icon.
void EmptyState(int icon, const char* title, const char* sub, float topPad = 40.0f, bool ok = false);
/// A page card's background (the Slate raised surface, rounded) with the hover look: a faint lift and a border (`hoverBorder`,
/// 0 = the soft white one).
void CardBackground(ImDrawList* dl, ImVec2 a, ImVec2 b, bool hover, ImU32 hoverBorder = 0, float borderW = 1.0f);
/// A soft pulsing block that stands in for content while it loads.
void SkeletonBlock(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding);
/// The small-caps label over a page section ("YOUR LIBRARY  -  3"; n < 0 = no count).
void SectionLabel(const char* title, int n);
/// The launcher's dropdown: one control, a chevron inside the same rounded box as the text (Dear ImGui draws its arrow in a
/// separate darker square). Same arguments as ImGui::BeginCombo / ImGui::Combo; close with ImGui::EndCombo.
bool BeginSlateCombo(const char* label, const char* preview, int flags = 0);
bool SlateCombo(const char* label, int* current, const char* itemsZeroSeparated, int popupMaxItems = -1);
bool SlateCombo(const char* label, int* current, const char* const items[], int count, int popupMaxItems = -1);
/// The fold / unfold button of a side panel (the sidebar, Servers' channels, Forums' sections): the panel icon in a rounded
/// square, `d` pixels; the tooltip names the panel and the shortcut. True when clicked.
bool PanelToggle(const char* id, bool open, const char* what, float d, ImU32 hoverBg, ImU32 fg, ImU32 fgHover);
/// Ctrl+B this frame, for the page whose panel it folds (a page takes it with TakePanelShortcut; if no page did, it folds
/// the launcher sidebar at the end of the frame).
extern bool g_panelShortcut;
inline bool TakePanelShortcut() { const bool b = g_panelShortcut; g_panelShortcut = false; return b; }
/// One icon of an icon rail (Servers' servers, Forums' folded sections): a round badge that turns into a rounded square when
/// hovered or selected, with the rail's selection pill on the left edge, a tooltip with the full name, and an optional dot
/// (new / unread). Content: `initials` on `fill`, or a line icon (`navIcon`), or a "+" (`plus`). Drawn in `r` (screen
/// coordinates); `railX` is the rail's left edge (for the pill). True when clicked.
struct RailIcon { std::string tip, initials; int navIcon = -1; bool plus = false; ImU32 fill = 0; bool selected = false, dot = false; int count = -1; };   // count >= 0: a small number badge
bool DrawRailIcon(const char* id, const RailIcon& it, ImVec2 pos, float d, float railX);
/// The thin line between groups of a rail, centred in a band from `a` to `b`.
void DrawRailDivider(ImVec2 a, ImVec2 b);
/// A plain button exactly as wide as its label needs (UI-scaled padding), at least `minW`.
bool FitButton(const char* label, float h, float minW = 0.0f, bool primary = false);
float FitButtonW(const char* label, float minW = 0.0f);   // the width FitButton will take
} // namespace okl
