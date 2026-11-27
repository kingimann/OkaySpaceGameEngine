#pragma once
// The Add Block list's rules (no drawing): which blocks it hides (one row per job; untested blocks wait), what kind a block is,
// the blocks that fit a dragged wire, a block's missing component, and the favourites and recent list kept between runs. The
// Visual Script app's Add Block list (shared/VisualScriptAppCanvas.cpp) reads these. Written for this project.
#include "VisualScriptNodeDocs.hpp"
#include "EditorTabs.hpp"   // okay::edtabs::UserFile: the per-user folder
#include "VisualScriptSearch.hpp"   // plain-word search, synonyms, starter boost
#include "ScriptingDensity.hpp"   // Density: every size below comes from this table
#include "VisualScriptSearchNav.hpp"   // the popup's pure logic: size, selection movement, scrolling, recent list
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/VisualScript/VsNodeRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace okay::editor::vsed {

inline bool (*VsHasComponent)(GameObject*, const char* id) = nullptr;
inline bool (*VsAddComponentById)(GameObject*, const char* id) = nullptr;

enum class VsKindFilter { All = 0, Event, Action, Getter, Flow };

inline VsKindFilter VsKindOf(const VsDef& d) {
    if (d.cat == CAT_EVENT) return VsKindFilter::Event;
    if (d.cat == CAT_FLOW) return VsKindFilter::Flow;
    if (!d.execIn && !d.dataOut.empty()) return VsKindFilter::Getter;
    if (d.execIn) return VsKindFilter::Action;
    return VsKindFilter::Getter;
}

inline const char* VsKindName(VsKindFilter k) {
    switch (k) {
        case VsKindFilter::Event: return "Events";
        case VsKindFilter::Action: return "Actions";
        case VsKindFilter::Getter: return "Getters";
        case VsKindFilter::Flow: return "Flow";
        default: return "All";
    }
}

struct VsReqGap {
    bool missing = false;
    std::string message;
    std::string addId;
    std::string addLabel;
};

/// The first component id this node can have added, when none of the alternatives
/// are on `owner`. A wired Target means the node may be aimed at another object,
/// so the owning object is not warned.
inline VsReqGap VsRequirementGap(GameObject* owner, const VsNodeDoc* doc, bool targetWired) {
    VsReqGap g;
    if (!doc) return g;
    g.message = doc->requirements;
    if (!owner || targetWired || doc->component.empty() || !VsHasComponent) return g;
    std::string first;
    bool any = false;
    std::string rest = doc->component;
    while (!rest.empty()) {
        auto bar = rest.find('|');
        std::string id = bar == std::string::npos ? rest : rest.substr(0, bar);
        rest = bar == std::string::npos ? std::string() : rest.substr(bar + 1);
        if (id.empty()) continue;
        if (first.empty()) first = id;
        if (VsHasComponent(owner, id.c_str())) any = true;
    }
    if (!any && !first.empty()) {
        g.missing = true;
        g.addId = first;
        g.addLabel = "Add " + first;
    }
    return g;
}


/// Nodes that stay out of the Add Node menu until a test shows what they do. The factory stays, so old graphs
/// and text scripts that already use them still load and run. tests/test_vs_registry_effect.cpp asserts this
/// exact list, so it cannot grow without a reason being written here.
struct VsHiddenUntilTested { const char* type; const char* reason; };
inline const VsHiddenUntilTested kHiddenUntilTested[] = {
    {"Fn.net_join_link",         "no suite joins by an okay:// link, and it needs a live host"},
    {"Fn.net_pin_host",          "no suite covers pinning the host, and it needs a live session"},
    {"Fn.pf_delete",             "PlayFab web request; no suite exercises it"},
    {"Fn.pf_leaderboard_top",    "PlayFab web request; no suite exercises it"},
    {"Fn.pf_load",               "PlayFab web request; no suite exercises it"},
    {"Fn.pf_login",              "PlayFab web request; no suite exercises it"},
    {"Fn.pf_login_password",     "PlayFab web request; no suite exercises it"},
    {"Fn.pf_register",           "PlayFab web request; no suite exercises it"},
    {"Fn.pf_save",               "PlayFab web request; no suite exercises it"},
    {"Fn.pf_set_stat",           "PlayFab web request; no suite exercises it"},
};
inline bool VsPaletteHiddenUntilTested(const char* type) {
    if (!type) return false;
    for (const VsHiddenUntilTested& h : kHiddenUntilTested)
        if (std::strcmp(h.type, type) == 0) return true;
    return false;
}

/// True when a Registry row is the same job as a hand-written node of the same title
/// (an engine-API binding such as Animator.Roll next to Roll). It is not listed twice.
inline bool VsPaletteHidesAsDuplicate(const VsDef& d) {
    // Indexes over the registry, built once per registry size: the checks below used to walk the whole registry
    // for every row, which made the first opening of the Add Node menu take most of a tenth of a second.
    struct Index {
        std::size_t n = (std::size_t)-1;
        std::unordered_map<std::string, const VsDef*> firstByTitle;      // first row with this title
        std::unordered_map<std::string, const VsDef*> firstApiByTitle;   // first engine-API row with this title
        std::unordered_map<std::string, bool> handTitle;                 // a hand-written (no '.', not Fn.) row has this title
        std::unordered_map<std::string, bool> handLower;                 // lower-cased type of each hand-written row (no '.')
        std::unordered_map<std::string, bool> handTitleNoDot;            // title of each hand-written row
    };
    static Index ix;
    if (ix.n != Registry().size()) {
        ix = Index();
        ix.n = Registry().size();
        for (const auto& h : Registry()) {
            const bool fn = std::strncmp(h.type, "Fn.", 3) == 0, dotted = std::strchr(h.type, '.') != nullptr;
            if (h.title) {
                ix.firstByTitle.emplace(h.title, &h);
                if (!fn) ix.handTitle[h.title] = true;
                if (okay::vs::VsApiFind(h.type)) ix.firstApiByTitle.emplace(h.title, &h);
            }
            if (!dotted) {
                std::string lo; for (const char* c = h.type; *c; ++c) lo += (char)std::tolower((unsigned char)*c);
                ix.handLower[lo] = true;
                if (h.title) ix.handTitleNoDot[h.title] = true;
            }
        }
    }
    // A text-script alias (play_sfx next to play_sound, vec2 next to vec3): one row per title.
    if (std::strncmp(d.type, "Fn.", 3) == 0) {
        if (!d.title) return false;
        const auto it = ix.firstByTitle.find(d.title);
        if (it != ix.firstByTitle.end() && it->second != &d) return true;   // an earlier row has this title
        return ix.handTitle.count(d.title) > 0;                              // or a hand-written one does
    }
    const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
    if (api) {
        // Same job as a hand-written node: its name, or Get/Set + its name (see VsApiDuplicatesHandNode), or its title.
        std::string m; for (const char* c = api->member.c_str(); *c; ++c) m += (char)std::tolower((unsigned char)*c);
        if (!m.empty() && (ix.handLower.count(m) || ix.handLower.count("get" + m) || ix.handLower.count("set" + m))) return true;
        if (d.title && ix.handTitleNoDot.count(d.title)) return true;
        // Two overloads of one binding with the same title: the first one is the row.
        if (d.title) {
            const auto it = ix.firstApiByTitle.find(d.title);
            if (it != ix.firstApiByTitle.end() && it->second != &d) {
                // only an EARLIER row counts
                for (const auto& h : Registry()) { if (&h == &d) break; if (&h == it->second) return true; }
            }
        }
    }
    return false;
}

/// Hidden from the palette (a duplicate of another row, or waiting to be tested). Worked out once per row: the
/// duplicate check walks the whole registry, which is slow to repeat.
inline bool VsPaletteHidden(const VsDef& d) {
    static std::unordered_map<const VsDef*, bool> memo;
    static std::size_t memoN = (std::size_t)-1;
    if (Registry().size() != memoN) { memo.clear(); memoN = Registry().size(); }
    const auto it = memo.find(&d);
    if (it != memo.end()) return it->second;
    const bool hidden = VsPaletteHidesAsDuplicate(d) || VsPaletteHiddenUntilTested(d.type);
    memo.emplace(&d, hidden);
    return hidden;
}

/// The listed nodes that can take (fromOutput) or give (!fromOutput) a wire at one pin of a node, easiest first.
/// This is the same rule as the list shown when a wire is dropped in empty space (VsCompatible), so the node
/// right-click "Connect to..." menu and the wire-drop palette always agree.
inline const std::vector<const VsDef*>& VsNodesFittingPin(bool exec, bool fromOutput, okay::vs::PinType pin) {
    static std::map<std::tuple<bool, bool, int>, std::vector<const VsDef*>> cache;
    static std::size_t cacheN = (std::size_t)-1;
    if (Registry().size() != cacheN) { cache.clear(); cacheN = Registry().size(); }
    const auto key = std::make_tuple(exec, fromOutput, exec ? 0 : (int)pin);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::vector<const VsDef*> out;
    for (const auto& d : Registry()) {
        if (!VsCompatible(d, exec, fromOutput, pin) || VsPaletteHidden(d)) continue;
        out.push_back(&d);
    }
    // A pin of exactly the same type is a better suggestion than one that merely accepts it (Any, or a conversion).
    auto exactMatch = [&](const VsDef* d) {
        return !exec && pin != okay::vs::PinType::Any && VsPinFitRank(*d, exec, fromOutput, pin) == 0;
    };
    std::vector<std::pair<const VsDef*, int>> ranked;   // (node, rank): lower is listed first
    for (const VsDef* d : out) ranked.push_back({d, (int)d->level * 4 + (exactMatch(d) ? 0 : 2) + (std::strchr(d->type, '.') ? 1 : 0)});
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second < b.second; });   // easy first, exact type first, hand-written before engine-API
    for (std::size_t i = 0; i < ranked.size(); ++i) out[i] = ranked[i].first;
    return cache.emplace(key, std::move(out)).first->second;
}

/// Extra search score for a typed search while a wire is dragged: a node with a pin of exactly the dragged type
/// (VsPinFitRank 0) is lifted above one that only takes the value through Any or a conversion.
inline int VsWireSearchBoost(int pinRank) { return pinRank == 0 ? 35 : pinRank == 1 ? 10 : 0; }

/// An Actions step with the same label as a hand-written node: the node is the one row.
inline bool VsPaletteStepIsDuplicate(const std::string& label) {
    for (const auto& h : Registry()) if (h.title && label == h.title) return true;
    return false;
}

/// Everything a search in the Add Block list finds a block by, besides its title: its own search words, what it is for ("move
/// things"), synonyms, where it used to sit (Scene Atmosphere, Objects Survival ...: people who remember the old topic still find it
/// by typing that name), its old category name, and "engine api" for a script-API block.
inline std::string VsPaletteKeywords(const VsDef& d) {
    const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
    const VsPlace place = api ? VsPlaceForApi(api->apiType.c_str(), api->member.c_str()) : VsPlaceForNode(d.type);
    const VsPlace was = api ? VsOldPlaceForApi(api->apiType.c_str(), api->member.c_str()) : VsOldPlaceForNode(d.type);
    std::string k = VsNodeSearchText(d.type);
    k += std::string(" ") + kVsIntents[VsIntentFor(d)];
    if (const std::string syn = VsSynonymWords(std::string(d.title ? d.title : "") + " " + k); !syn.empty()) k += " " + syn;
    if (api) k += " engine api";
    if (const std::string moved = VsMovedFromWords(was, place); !moved.empty()) k += " " + moved;
    if (d.cat >= 0 && d.cat < CAT_COUNT) {
        static const char* oldCat[] = {"Events", "Flow", "Variables", "Math", "Logic", "Input", "Transform", "Actions", "Data",
                                       "Multiplayer & Steam", "Engine API", "Script builtins"};
        k += std::string(" ") + oldCat[d.cat];
    }
    return k;
}
/// The same for an Actions step shown as a row of its own (an Action or a Check block set to that step).
inline std::string VsPaletteStepKeywords(const VsStepInfo& si, bool cond) {
    std::string k = si.group + (cond ? " check something" : " do something");
    const VsPlace place = VsPlaceForStep(si.op.c_str(), si.group.c_str()), was = VsOldPlaceForStep(si.op.c_str(), si.group.c_str());
    if (const std::string moved = VsMovedFromWords(was, place); !moved.empty()) k += " " + moved;
    return k;
}

/// What the add-node popup remembers between sessions, in okay_vsnodes.txt: the favourites, the last 8 nodes added, the
/// level button and the size you left the popup at. One per editor (not per graph).
struct VsPaletteStore {
    std::vector<std::string> fav, recent;
    int filter = 0;                         // VsLevelFilter: 0 all, 1 basic, 2 intermediate, 3 advanced
    bool allNodes = false;                  // "Show all nodes": off lists the everyday blocks only
    searchnav::PanelSize size;              // 0 x 0 until the popup has been resized once
    bool loaded = false;
    void load() {
        loaded = true;
        std::ifstream f(okay::edtabs::UserFile("okay_vsnodes.txt")); std::string kind, k;   // the per-user folder
        while (f >> kind >> k) {
            if (kind == "lvl") filter = std::clamp(std::atoi(k.c_str()), 0, 3);
            else if (kind == "all") allNodes = k == "1";
            else if (kind == "size") { const searchnav::PanelSize p = searchnav::ParsePanelSize(k); if (!p.bad) size = p; }
            else if (kind == "fav") fav.push_back(k);
            else if (kind == "rec") recent.push_back(k);
        }
        searchnav::RecentClean(recent);
    }
    void save() const {
        std::ofstream f(okay::edtabs::UserFile("okay_vsnodes.txt"));
        f << "lvl " << filter << "\n";
        f << "all " << (allNodes ? 1 : 0) << "\n";
        if (size.w > 0 && size.h > 0) f << "size " << searchnav::FormatPanelSize(size) << "\n";
        for (const auto& k : fav) f << "fav " << k << "\n";
        for (const auto& k : recent) f << "rec " << k << "\n";
    }
};
inline VsPaletteStore& VsStore() { static VsPaletteStore s; if (!s.loaded) s.load(); return s; }

}  // namespace okay::editor::vsed
