#pragma once
// The Nodes tab of the Visual Script side panel: what it remembers and how its
// rows are laid out. Pure logic, no ImGui and no node registry, so
// tests/test_vs_node_list.cpp can drive it directly. The pop-up and the tab draw
// the same list (DrawNodePalette in VisualScriptPalette.hpp); this file is the
// part that does not need a window.
//
//  * VsNodeListPrefs  - favourites, recently used, level filter, which groups are
//                       open, panel width / tab / shown. One small text file.
//  * VsBuildListRows  - sections (Favourites, Recently used, Starter, topics) and
//                       their rows, or one ranked "Results" list while searching.
//  * Drop maths       - where a node dragged from the list lands on the graph.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace okay::editor::vsed {

// ---- Remembered state ---------------------------------------------------------
inline constexpr float kVsSideMinW = 200.0f;      // narrowest the panel goes
inline constexpr float kVsSideMaxW = 640.0f;      // widest the panel goes
inline constexpr float kVsSideDefaultW = 300.0f;
inline constexpr float kVsSideWideW = 420.0f;     // from here the bigger layout shows a details strip
inline constexpr float kVsSideRoomyW = 250.0f;    // below this, rows drop their one-line help
inline constexpr std::size_t kVsRecentMax = 12;

enum class VsSideTab : int { Outline = 0, Nodes = 1 };

struct VsNodeListPrefs {
    int   level = 0;                              // 0 all, 1 basic, 2 intermediate, 3 advanced
    std::vector<std::string> fav;                 // first is pinned highest
    std::vector<std::string> recent;              // newest first
    std::map<std::string, bool> open;             // groups the user opened or closed
    bool  panelShown = true;                      // the left panel
    int   tab = (int)VsSideTab::Nodes;            // which tab is in front
    bool  rail = false;                           // panel folded to a narrow strip
    float width = kVsSideDefaultW;

    bool isFav(const std::string& k) const { return std::find(fav.begin(), fav.end(), k) != fav.end(); }
    bool operator==(const VsNodeListPrefs& o) const {
        return level == o.level && fav == o.fav && recent == o.recent && open == o.open &&
               panelShown == o.panelShown && tab == o.tab && rail == o.rail && width == o.width;
    }

    std::string toText() const {
        std::ostringstream f;
        f << "lvl " << level << "\n";
        f << "side " << (panelShown ? 1 : 0) << "\n";
        f << "tab " << tab << "\n";
        f << "rail " << (rail ? 1 : 0) << "\n";
        f << "width " << (int)std::lround(width) << "\n";
        for (const auto& k : fav) f << "fav " << k << "\n";
        for (const auto& k : recent) f << "rec " << k << "\n";
        for (const auto& kv : open) f << "open " << (kv.second ? 1 : 0) << " " << kv.first << "\n";
        return f.str();
    }
    // Reads what toText wrote, and the older two-word lines (lvl / fav / rec).
    // Unknown lines are skipped; numbers are clamped; nothing throws.
    static VsNodeListPrefs fromText(const std::string& text);
};

inline float VsClampSideWidth(float w, float avail) {
    const float hi = std::max(kVsSideMinW, std::min(kVsSideMaxW, avail * 0.6f));
    return std::min(std::max(w, kVsSideMinW), hi);
}

inline VsNodeListPrefs VsNodeListPrefs::fromText(const std::string& text) {
    VsNodeListPrefs p;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const std::size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string kind = line.substr(0, sp), rest = line.substr(sp + 1);
        if (rest.empty()) continue;
        if (kind == "lvl") p.level = std::clamp(std::atoi(rest.c_str()), 0, 3);
        else if (kind == "side") p.panelShown = std::atoi(rest.c_str()) != 0;
        else if (kind == "tab") p.tab = std::clamp(std::atoi(rest.c_str()), 0, 1);
        else if (kind == "rail") p.rail = std::atoi(rest.c_str()) != 0;
        else if (kind == "width") p.width = std::clamp((float)std::atoi(rest.c_str()), kVsSideMinW, kVsSideMaxW);
        else if (kind == "fav") { if (!p.isFav(rest)) p.fav.push_back(rest); }
        else if (kind == "rec") {
            if (std::find(p.recent.begin(), p.recent.end(), rest) == p.recent.end() && p.recent.size() < kVsRecentMax)
                p.recent.push_back(rest);
        } else if (kind == "open") {
            const std::size_t s2 = rest.find(' ');
            if (s2 != std::string::npos && s2 + 1 < rest.size() && p.open.size() < 400)
                p.open[rest.substr(s2 + 1)] = std::atoi(rest.c_str()) != 0;
        }
    }
    return p;
}

/// Star on, star off. Starring puts it at the top.
inline bool VsToggleFav(VsNodeListPrefs& p, const std::string& key) {
    const auto it = std::find(p.fav.begin(), p.fav.end(), key);
    if (it != p.fav.end()) { p.fav.erase(it); return false; }
    p.fav.insert(p.fav.begin(), key);
    return true;
}
/// A node that was just added: newest first, no repeats, at most kVsRecentMax.
inline void VsPushRecent(VsNodeListPrefs& p, const std::string& key) {
    p.recent.erase(std::remove(p.recent.begin(), p.recent.end(), key), p.recent.end());
    p.recent.insert(p.recent.begin(), key);
    if (p.recent.size() > kVsRecentMax) p.recent.resize(kVsRecentMax);
}
/// Whether a group is open. A group the user never touched uses its default.
inline bool VsGroupOpen(const VsNodeListPrefs& p, const std::string& id, bool dflt) {
    const auto it = p.open.find(id);
    return it == p.open.end() ? dflt : it->second;
}
inline void VsSetGroupOpen(VsNodeListPrefs& p, const std::string& id, bool open, bool dflt) {
    if (open == dflt) p.open.erase(id); else p.open[id] = open;   // only exceptions are stored
}
/// Starter, favourites and recent start open; a topic starts folded so the list stays short.
inline bool VsGroupDefaultOpen(const std::string& id) { return id.rfind("topic:", 0) != 0; }

// ---- Rows ---------------------------------------------------------------------
struct VsNodeItem {
    std::string key;          // "n:Type", "a:op" or "c:op"
    std::string topic;        // topic name ("Math")
    int topicIndex = 0;       // topics list in this order
    int starter = -1;         // position in the starter set, -1 when not in it
};

struct VsListRow {
    enum Kind { Header, Item, Hint } kind = Item;
    std::string id;           // header: group id ("fav", "recent", "starter", "topic:Math", "results")
    std::string label;        // header title, or hint text
    int count = 0;            // header: how many nodes
    bool open = true;         // header: showing its nodes
    bool foldable = true;     // header: can be closed (the search results cannot)
    int item = -1;            // Item: index into the items
};

/// `order` holds the items that pass the level filter, in browse order (topic, then
/// the order inside it), or ranked best-first when `searching`. Returns the rows to draw.
inline std::vector<VsListRow> VsBuildListRows(const std::vector<VsNodeItem>& items,
                                              const std::vector<int>& order, bool searching,
                                              const VsNodeListPrefs& prefs) {
    std::vector<VsListRow> rows;
    auto header = [&](const std::string& id, const std::string& label, int count, bool foldable) {
        VsListRow r; r.kind = VsListRow::Header; r.id = id; r.label = label; r.count = count; r.foldable = foldable;
        r.open = foldable ? VsGroupOpen(prefs, id, VsGroupDefaultOpen(id)) : true;
        rows.push_back(r);
        return r.open;
    };
    auto item = [&](int idx) { VsListRow r; r.kind = VsListRow::Item; r.item = idx; rows.push_back(r); };
    auto hint = [&](const std::string& text) { VsListRow r; r.kind = VsListRow::Hint; r.label = text; rows.push_back(r); };

    if (searching) {
        header("results", "Results", (int)order.size(), false);
        if (order.empty()) hint("No node matches that.");
        for (int idx : order) item(idx);
        return rows;
    }
    auto inOrder = [&](int idx) { return std::find(order.begin(), order.end(), idx) != order.end(); };
    auto byKey = [&](const std::string& key) {
        for (int i = 0; i < (int)items.size(); ++i) if (items[i].key == key) return i;
        return -1;
    };
    {   // Favourites: always shown, so the star is discoverable.
        std::vector<int> favs;
        for (const auto& k : prefs.fav) { const int i = byKey(k); if (i >= 0 && inOrder(i)) favs.push_back(i); }
        if (header("fav", "Favourites", (int)favs.size(), true)) {
            if (favs.empty()) hint("Click the star on a node to pin it here.");
            for (int i : favs) item(i);
        }
    }
    {
        std::vector<int> rec;
        for (const auto& k : prefs.recent) { const int i = byKey(k); if (i >= 0 && inOrder(i)) rec.push_back(i); }
        if (!rec.empty() && header("recent", "Recently used", (int)rec.size(), true))
            for (int i : rec) item(i);
    }
    {
        std::vector<int> st;
        for (int i : order) if (items[(std::size_t)i].starter >= 0) st.push_back(i);
        std::stable_sort(st.begin(), st.end(), [&](int a, int b) { return items[(std::size_t)a].starter < items[(std::size_t)b].starter; });
        if (!st.empty() && header("starter", "Starter", (int)st.size(), true))
            for (int i : st) item(i);
    }
    for (std::size_t a = 0; a < order.size();) {   // topics: consecutive runs of one topic
        std::size_t b = a;
        while (b < order.size() && items[(std::size_t)order[b]].topic == items[(std::size_t)order[a]].topic) ++b;
        const std::string topic = items[(std::size_t)order[a]].topic.empty() ? std::string("Other") : items[(std::size_t)order[a]].topic;
        if (header("topic:" + topic, topic, (int)(b - a), true))
            for (std::size_t k = a; k < b; ++k) item(order[k]);
        a = b;
    }
    return rows;
}

/// Up / Down over the rows. Stays inside the list; headers count (Enter opens or closes them).
inline int VsStepRow(int current, int delta, int rowCount) {
    if (rowCount <= 0) return 0;
    return std::clamp(current + delta, 0, rowCount - 1);
}

// ---- Where a dropped node goes ------------------------------------------------
/// Screen point -> graph position, the same maths the canvas uses for everything
/// it places (pan, zoom and the layout spread).
inline void VsScreenToGraph(float sx, float sy, float canvasX, float canvasY, float panX, float panY,
                            float zoom, float layout, float& gx, float& gy) {
    const float z = std::max(zoom, 0.0001f) * layout;
    gx = (sx - canvasX - panX) / z;
    gy = (sy - canvasY - panY) / z;
}

/// Nudge a spot that an existing node already sits on, so adding twice at the same
/// place does not stack the nodes. Tries down-and-right, a few steps.
inline void VsFreeSpot(const std::vector<std::pair<float, float>>& taken, float& x, float& y, float step = 24.0f) {
    auto busy = [&](float px, float py) {
        for (const auto& t : taken) if (std::fabs(t.first - px) < 8.0f && std::fabs(t.second - py) < 8.0f) return true;
        return false;
    };
    for (int i = 0; i < 24 && busy(x, y); ++i) { x += step; y += step; }
}

/// "n:Type" -> ("n", "Type"); "a:op" / "c:op" likewise. False for anything else.
inline bool VsSplitListKey(const std::string& key, char& kind, std::string& rest) {
    if (key.size() < 3 || key[1] != ':' || (key[0] != 'n' && key[0] != 'a' && key[0] != 'c')) return false;
    kind = key[0]; rest = key.substr(2);
    return true;
}

// ---- File ---------------------------------------------------------------------
inline VsNodeListPrefs VsLoadNodeListPrefs(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return VsNodeListPrefs();
    std::ostringstream ss; ss << f.rdbuf();
    return VsNodeListPrefs::fromText(ss.str());
}
inline bool VsSaveNodeListPrefs(const std::string& path, const VsNodeListPrefs& p) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << p.toText();
    return (bool)f;
}

}  // namespace okay::editor::vsed
