#pragma once
// The node search popup's pure logic: how big the popup is and how it is kept on screen, how the selection moves for
// Up / Down / Home / End / PageUp / PageDown, how far a list scrolls to show the selected row, and how Left / Right step
// through the categories. No ImGui here, so tests/test_vs_search_nav.cpp covers it; VisualScriptPalette.hpp draws it.
//
// Written from scratch for this engine from plain list-box behaviour (move, clamp, reveal); the pixel numbers are our own
// choices and the row height reads the shared Density table (ScriptingDensity.hpp).
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace okay::editor::vsed::searchnav {

// ---- Panel size ------------------------------------------------------------------------------------------------------

struct PanelSize { float w = 0, h = 0; bool bad = false; };

constexpr float kScreenMargin = 24.0f;   // the popup never touches the edge of the screen
constexpr float kMinW = 420.0f, kMinH = 320.0f;

/// Never bigger than the screen minus a margin; never smaller than a usable size (unless the screen itself is smaller).
inline PanelSize ClampPanelSize(PanelSize want, float screenW, float screenH) {
    const float maxW = std::max(160.0f, screenW - kScreenMargin);
    const float maxH = std::max(120.0f, screenH - kScreenMargin);
    PanelSize r;
    r.w = std::min(std::max(want.w, kMinW), maxW);
    r.h = std::min(std::max(want.h, kMinH), maxH);
    if (!(want.w > 0)) r.w = std::min(kMinW, maxW);
    if (!(want.h > 0)) r.h = std::min(kMinH, maxH);
    return r;
}

/// The size a fresh editor opens at: about 560 x 520 at Comfortable (the default Density), a little smaller at Compact
/// and bigger at Spacious, clamped to the screen.
inline PanelSize DefaultPanelSize(int density, float screenW, float screenH) {
    PanelSize p{560.0f, 520.0f};
    if (density <= 0) p = {520.0f, 480.0f};
    else if (density >= 2) p = {640.0f, 580.0f};
    return ClampPanelSize(p, screenW, screenH);
}

/// "700x600" in the settings file. `bad` is set when the text is not two positive numbers.
inline PanelSize ParsePanelSize(const std::string& s) {
    PanelSize r; r.bad = true;
    const std::size_t x = s.find('x');
    if (x == std::string::npos || x == 0 || x + 1 >= s.size()) return r;
    char* e1 = nullptr; char* e2 = nullptr;
    const std::string a = s.substr(0, x), b = s.substr(x + 1);
    const float w = std::strtof(a.c_str(), &e1), h = std::strtof(b.c_str(), &e2);
    if (*e1 != '\0' || *e2 != '\0' || !(w > 0) || !(h > 0)) return r;
    r.w = w; r.h = h; r.bad = false;
    return r;
}
inline std::string FormatPanelSize(PanelSize p) {
    return std::to_string((int)std::lround(p.w)) + "x" + std::to_string((int)std::lround(p.h));
}

/// The category list on the left: about a third of the panel, within limits, so the node rows keep the room.
inline float CategoryPaneWidth(float panelW) { return std::min(240.0f, std::max(160.0f, std::floor(panelW * 0.34f))); }

/// One node row: the Density's row height, or two lines of text plus padding when that is taller (name, then description).
inline float RowHeight(float densityRowH, float textH, float pad) { return std::max(densityRowH, 2.0f * textH + 2.0f * pad); }

// ---- Selection and scrolling -----------------------------------------------------------------------------------------

enum class Move { None, Up, Down, Home, End, PageUp, PageDown };

/// Where the selection goes. Stops at both ends (no wrap, so holding a key does not jump around). An out-of-range
/// selection is pulled back into the list; an empty list gives 0.
inline int MoveSelection(int sel, int count, Move m, int pageRows) {
    if (count <= 0) return 0;
    const int last = count - 1, page = std::max(1, pageRows);
    int r = sel;
    switch (m) {
        case Move::Up:       r = sel - 1; break;
        case Move::Down:     r = sel + 1; break;
        case Move::Home:     r = 0; break;
        case Move::End:      r = last; break;
        case Move::PageUp:   r = sel - page; break;
        case Move::PageDown: r = sel + page; break;
        case Move::None:     break;
    }
    return std::min(std::max(r, 0), last);
}

/// The new scroll offset that shows the row at [rowTop, rowTop + rowH) inside a view `viewH` tall, with `margin` of air.
/// The offset does not change while the row is already visible. A row taller than the view shows its top.
inline float RevealScroll(float scroll, float viewH, float rowTop, float rowH, float margin) {
    const float top = rowTop - margin, bottom = rowTop + rowH + margin;
    float s = scroll;
    if (bottom - top > viewH) s = top;
    else if (top < s) s = top;
    else if (bottom > s + viewH) s = bottom - viewH;
    return std::max(0.0f, s);
}
inline float MaxScroll(float contentH, float viewH) { return std::max(0.0f, contentH - viewH); }

// ---- Categories ------------------------------------------------------------------------------------------------------

/// Left / Right: the id one step along `ids` (the visible categories, top to bottom). Stops at the ends. When `cur` is not
/// in the list, the first id; an empty list keeps `cur`.
inline int StepCategory(const std::vector<int>& ids, int cur, int dir) {
    if (ids.empty()) return cur;
    const auto it = std::find(ids.begin(), ids.end(), cur);
    if (it == ids.end()) return ids.front();
    const int i = (int)(it - ids.begin()) + (dir < 0 ? -1 : (dir > 0 ? 1 : 0));
    return ids[(std::size_t)std::min(std::max(i, 0), (int)ids.size() - 1)];
}

// ---- Recent ----------------------------------------------------------------------------------------------------------

constexpr std::size_t kRecentMax = 8;   // the last 8 nodes added

/// Put `key` at the front; an earlier copy is removed; the list never holds more than kRecentMax. An empty key is ignored.
inline void RecentPush(std::vector<std::string>& list, const std::string& key) {
    if (key.empty()) return;
    list.erase(std::remove(list.begin(), list.end(), key), list.end());
    list.insert(list.begin(), key);
    if (list.size() > kRecentMax) list.resize(kRecentMax);
}
/// After loading from the settings file: drop empty and repeated keys (first one wins) and cut to kRecentMax.
inline void RecentClean(std::vector<std::string>& list) {
    std::vector<std::string> out;
    for (const auto& k : list)
        if (!k.empty() && std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    if (out.size() > kRecentMax) out.resize(kRecentMax);
    list = std::move(out);
}

// ---- Pin context and empty results -----------------------------------------------------------------------------------

/// Is a node listed? With no wire being dragged everything is; with one, only nodes that fit its pin, unless "show all".
inline bool ShowEntry(bool wireDragged, bool showAll, bool fits) { return !wireDragged || showAll || fits; }

/// The short line at the top of the popup while a wire is dragged.
inline const char* PinContextLine(bool fitOnly) {
    return fitOnly ? "Showing nodes that fit this plug" : "Showing all nodes (the ones that fit come first)";
}

/// What to say when the list is empty. `side`: -4 Starter, -2 Recent, -1 Favourites, otherwise a topic or All.
inline std::string EmptyMessage(int side, bool searching, bool fitOnly, bool levelFiltered) {
    if (searching) {
        if (fitOnly) return "Nothing that fits this plug matches that. Press Show all to search every node.";
        if (levelFiltered) return "No node matches that at this level. Try the All button to see every level.";
        return "No node matches that. Check the spelling, or try a simpler word.";
    }
    if (side == -1) return "No favourites yet. Click the star on a node to keep it here.";
    if (side == -2) return "Nodes you add show up here, newest first.";
    if (fitOnly) return "No node in this list fits this plug. Press Show all to see every node.";
    if (levelFiltered) return "Nothing here at this level. Try the All button to see every level.";
    return "Nothing here yet. Pick another topic on the left.";
}

/// Number of single-letter edits (insert, delete, change) between two strings.
inline int EditDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (std::size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

/// For a search that found nothing: the indexes of up to `maxN` titles (already lower-case) that look most like `needle`,
/// closest first. It compares the needle with the whole title and with each word of it, and always gives the best few
/// (so the list is never a blank box). No needle, no titles or maxN 0 give nothing.
inline std::vector<int> NearestIndices(const std::vector<std::string>& titlesLower, const std::string& needle, std::size_t maxN) {
    std::vector<int> out;
    if (needle.empty() || titlesLower.empty() || maxN == 0) return out;
    std::vector<std::pair<int, int>> scored;   // (distance, index)
    scored.reserve(titlesLower.size());
    for (std::size_t i = 0; i < titlesLower.size(); ++i) {
        const std::string& t = titlesLower[i];
        int best = EditDistance(needle, t);
        std::size_t start = 0;
        while (start <= t.size()) {
            std::size_t end = t.find(' ', start);
            if (end == std::string::npos) end = t.size();
            if (end > start) best = std::min(best, EditDistance(needle, t.substr(start, end - start)));
            start = end + 1;
        }
        scored.push_back({best, (int)i});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t i = 0; i < scored.size() && i < maxN; ++i) out.push_back(scored[i].second);
    return out;
}

}  // namespace okay::editor::vsed::searchnav
