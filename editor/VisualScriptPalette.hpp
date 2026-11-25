#pragma once
// The add-node panel: search, a topic list on the left (grouped under section headings), favourites, and a
// details pane. Shared by the right-click menu, a wire dropped in empty space, and Add node.
#include "VisualScriptNodeDocs.hpp"
#include "VisualScriptSearch.hpp"   // plain-word search, synonyms, starter boost
#include "ScriptingDensity.hpp"   // Density: every size below comes from this table
#include "VisualScriptSearchNav.hpp"   // the popup's pure logic: size, selection movement, scrolling, recent list
#include "imgui.h"
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

inline void VsDrawDocBody(const VsNodeDoc& doc, bool withAdd, GameObject* owner, bool targetWired) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("%s", doc.description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Requires");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("%s", doc.requirements.c_str());
    ImGui::PopTextWrapPos();
    if (withAdd) {
        VsReqGap gap = VsRequirementGap(owner, &doc, targetWired);
        if (gap.missing && VsAddComponentById) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.32f, 0.16f, 1.0f));
            if (ImGui::Button(gap.addLabel.c_str())) VsAddComponentById(owner, gap.addId.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (!doc.setting.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("Setting");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", doc.setting.c_str());
        if (!doc.settingDefault.empty()) ImGui::TextDisabled("Default: %s", doc.settingDefault.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!doc.pins.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Pins");
        for (const VsPinDoc& p : doc.pins) {
            const char* dir = p.exec ? (p.input ? "flow in" : "flow out") : (p.input ? "in" : "out");
            const std::string name = p.name.empty() ? std::string(p.exec ? "flow" : "value") : p.name;
            ImGui::Spacing();
            ImGui::Text("%s  %s", name.c_str(), dir);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", p.type.c_str());
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s", p.meaning.c_str());
            if (!p.fallback.empty()) ImGui::TextDisabled("Default: %s", p.fallback.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    if (!doc.example.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Example");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", doc.example.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!doc.related.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Related: %s", doc.related.c_str());
    }
}

inline void VsNodeTooltip(const char* title, const VsNodeDoc* doc, const VsDef* def = nullptr) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextUnformatted(title ? title : (doc ? doc->title.c_str() : ""));
    if (def) {   // the three questions first: what it does, what it needs, what it gives
        const VsNodeBrief b = VsNodeBriefFor(*def);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.62f, 0.85f, 0.65f, 1), "Does");   ImGui::SameLine(); ImGui::TextWrapped("%s", b.does.c_str());
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.45f, 1), "Needs");  ImGui::SameLine(); ImGui::TextWrapped("%s", b.needs.c_str());
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.00f, 1), "Gives");  ImGui::SameLine(); ImGui::TextWrapped("%s", b.gives.c_str());
        ImGui::Separator();
    }
    if (doc) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", VsPlainText(doc->description).c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("%s", doc->requirements.c_str());
        if (!doc->example.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s", doc->example.c_str()); }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

inline void VsPinTooltip(const VsPinDoc* pin, const char* name, const char* typeName,
                         okay::vs::PinType pinType = okay::vs::PinType::Any, bool isOutput = false, bool exec = false,
                         const char* required = nullptr, const char* plainTip = nullptr) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
    ImGui::Text("%s   %s", (name && *name) ? name : "flow", typeName ? typeName : (pin ? pin->type.c_str() : ""));
    if (plainTip && *plainTip) {   // the plain-words tip (VisualScriptGuide.hpp): meaning, what to plug in, an example value
        ImGui::TextWrapped("%s", plainTip);
    } else if (pin) {
        ImGui::TextWrapped("%s", pin->meaning.c_str());
        if (!pin->fallback.empty()) ImGui::TextDisabled("Default: %s", pin->fallback.c_str());
    }
    if (required) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1), "Required: %s", required);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.62f, 0.85f, 0.65f, 1), "%s", VsPinConnectText(pinType, isOutput, exec).c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("Drag to wire. A flow output can have many wires (they run in the order made).");
    ImGui::TextDisabled("Alt+click a pin breaks its wires; Alt+click a wire breaks just that one.");
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

inline void DrawCategoryIcon(ImDrawList* dl, ImVec2 c, float r, int cat, ImU32 col) {
    switch (cat) {
        case CAT_EVENT: dl->AddCircleFilled(c, r * 0.45f, col, 12); break;
        case CAT_FLOW: {
            ImVec2 a(c.x - r * 0.55f, c.y - r * 0.4f), b(c.x - r * 0.55f, c.y + r * 0.4f), d(c.x + r * 0.55f, c.y);
            dl->AddTriangleFilled(a, b, d, col); break;
        }
        case CAT_MATH: dl->AddRectFilled(ImVec2(c.x - r * 0.5f, c.y - r * 0.12f), ImVec2(c.x + r * 0.5f, c.y + r * 0.12f), col); break;
        default: dl->AddRectFilled(ImVec2(c.x - r * 0.4f, c.y - r * 0.4f), ImVec2(c.x + r * 0.4f, c.y + r * 0.4f), col, 2.0f); break;
    }
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
        if (exec || pin == okay::vs::PinType::Any) return false;
        if (fromOutput) { for (int i = 0; i < (int)d->dataIn.size(); ++i) if (okay::vs::InputType(d->type, i) == pin) return true; }
        else            { for (int i = 0; i < (int)d->dataOut.size(); ++i) if (okay::vs::OutputType(d->type, i, "") == pin) return true; }
        return false;
    };
    std::vector<std::pair<const VsDef*, int>> ranked;   // (node, rank): lower is listed first
    for (const VsDef* d : out) ranked.push_back({d, (int)d->level * 4 + (exactMatch(d) ? 0 : 2) + (std::strchr(d->type, '.') ? 1 : 0)});
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second < b.second; });   // easy first, exact type first, hand-written before engine-API
    for (std::size_t i = 0; i < ranked.size(); ++i) out[i] = ranked[i].first;
    return cache.emplace(key, std::move(out)).first->second;
}

/// An Actions step with the same label as a hand-written node: the node is the one row.
inline bool VsPaletteStepIsDuplicate(const std::string& label) {
    for (const auto& h : Registry()) if (h.title && label == h.title) return true;
    return false;
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
        std::ifstream f("okay_vsnodes.txt"); std::string kind, k;
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
        std::ofstream f("okay_vsnodes.txt");
        f << "lvl " << filter << "\n";
        f << "all " << (allNodes ? 1 : 0) << "\n";
        if (size.w > 0 && size.h > 0) f << "size " << searchnav::FormatPanelSize(size) << "\n";
        for (const auto& k : fav) f << "fav " << k << "\n";
        for (const auto& k : recent) f << "rec " << k << "\n";
    }
};
inline VsPaletteStore& VsStore() { static VsPaletteStore s; if (!s.loaded) s.load(); return s; }

/// The size the popup opens at: what you left it at, or the default for the Density; always inside the screen.
inline ImVec2 VsPaletteWantedSize(ImVec2 screen) {
    const searchnav::PanelSize saved = VsStore().size;
    const searchnav::PanelSize p = saved.w > 0 && saved.h > 0 ? searchnav::ClampPanelSize(saved, screen.x, screen.y)
                                                              : searchnav::DefaultPanelSize(oked::scriptui::g_scriptDensity, screen.x, screen.y);
    return ImVec2(p.w, p.h);
}

/// `addNode(def, arg)` places the node. `arg` is set for action steps.
/// Topics (VisualScriptCategories.hpp) on the left, nodes in the middle, what the
/// highlighted node does on the right. Levels filter the list.
inline void DrawNodePalette(VsEditState& st, const VsEditGraph& g,
                            const std::function<void(const VsDef&, const std::string&)>& addNode) {
    const oked::scriptui::Metrics dm = oked::scriptui::Current();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dm.framePad.x, dm.framePad.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dm.itemSpacing.x, dm.itemSpacing.y * 0.75f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dm.windowPad.x, dm.windowPad.y));
    okay::vs::PinType dragged = okay::vs::PinType::Any;
    if (st.linkPending && !st.linkExec)
        dragged = st.linkFromOutput ? VsOutPinType(g, st.linkNode, st.linkPin)
                                     : VsInPinType(g, st.linkNode, st.linkPin);
    auto fits = [&](const VsDef& d) {
        return !st.linkPending || VsCompatible(d, st.linkExec, st.linkFromOutput, dragged);
    };
    auto lower = [](std::string x) { for (char& ch : x) ch = (char)std::tolower((unsigned char)ch); return x; };
    auto bright = [](ImU32 c) {   // the category colour, lifted so a dot reads on the dark list
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
        return ImGui::GetColorU32(ImVec4(v.x + (1 - v.x) * 0.45f, v.y + (1 - v.y) * 0.45f, v.z + (1 - v.z) * 0.45f, 1.0f));
    };
    // Every placeable thing: nodes, script-API nodes, and Actions steps, each in a topic.
    struct Entry {
        const VsDef* def = nullptr; const VsStepInfo* step = nullptr; bool cond = false;
        std::string title, cat, sub, key, desc, keywords, helpLine, example;
        const VsNodeDoc* doc = nullptr;
        VsLevel level = VsLevel::Intermediate;
        int intent = 11;                        // the "what do you want to do?" heading (VisualScriptGuide.hpp)
        int catIndex = 0, rank = 0; bool browse = true; ImU32 col = 0;
        bool fits = true;                       // can take the wire being dragged (set each frame, cheap)
        std::string titleLower;                 // lower-cased once, when the list is built
        mutable std::string blobLower;          // everything searchable, lower-cased on first use
        mutable bool blobBuilt = false;
    };
    auto catColor = [&](int index) {
        // One hue per section (Events, Flow, Data ...), so the topics of a section share a dot colour and the
        // list of ~70 topics still reads in groups. Common sections stay in the documented order.
        static const ImU32 cols[] = {
            IM_COL32(158, 96, 102, 255), IM_COL32(86, 112, 158, 255), IM_COL32(72, 138, 116, 255),
            IM_COL32(64, 132, 150, 255), IM_COL32(120, 150, 170, 255), IM_COL32(90, 140, 120, 255),
            IM_COL32(176, 114, 78, 255), IM_COL32(70, 146, 146, 255), IM_COL32(90, 120, 170, 255),
            IM_COL32(170, 110, 150, 255), IM_COL32(180, 140, 70, 255), IM_COL32(168, 132, 78, 255),
            IM_COL32(110, 130, 180, 255), IM_COL32(150, 120, 90, 255), IM_COL32(100, 140, 100, 255),
            IM_COL32(130, 130, 160, 255), IM_COL32(84, 114, 170, 255), IM_COL32(140, 140, 140, 255),
            IM_COL32(120, 110, 150, 255),
        };
        const int n = (int)(sizeof cols / sizeof cols[0]);
        const int section = VsPaletteSectionIndex(index);
        return cols[section >= 0 ? section % n : 0];
    };
    // The list is built once and reused every frame: rebuilding ~2300 entries (a dozen strings and several
    // lookups each) 60 times a second is what made the pop-up drop the frame rate. It is rebuilt only when
    // the registry or a step catalogue changes size (a plugin or graph function was added or removed).
    static std::vector<Entry> s_all;
    static std::size_t s_regN = (std::size_t)-1, s_actN = (std::size_t)-1, s_condN = (std::size_t)-1;
    static int s_modOn = -1;
    const std::size_t regN = Registry().size();
    const std::size_t actN = StepCatalog ? (StepCatalog(false) ? StepCatalog(false)->size() : 0) : 0;
    const std::size_t condN = StepCatalog ? (StepCatalog(true) ? StepCatalog(true)->size() : 0) : 0;
    int modOn = 0;
    for (const auto& inf : okay::ModuleRegistry::Get().All()) if (inf.enabled) ++modOn;
    const bool rebuild = regN != s_regN || actN != s_actN || condN != s_condN || modOn != s_modOn;
    std::vector<Entry>& all = s_all;
    static std::size_t s_gen = 0;                       // bumped each time the list is rebuilt (pointers into it go stale)
    if (rebuild) { all.clear(); s_regN = regN; s_actN = actN; s_condN = condN; s_modOn = modOn; ++s_gen; }
    if (rebuild) for (const auto& d : Registry()) {
        if (d.type && okay::vs::VsNodeSuppressed(d.type)) continue;   // its module is off
        const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
        VsPlace place = api ? VsPlaceForApi(api->apiType.c_str(), api->member.c_str()) : VsPlaceForNode(d.type);
        if (VsPaletteHidden(d)) continue;   // one row per job; untested nodes wait
        const VsPlace was = api ? VsOldPlaceForApi(api->apiType.c_str(), api->member.c_str()) : VsOldPlaceForNode(d.type);
        const bool dup = false;
        Entry e; e.def = &d; e.title = d.title; e.key = VsPaletteNodeKey(d.type);
        e.cat = place.category ? place.category : "";
        e.sub = place.subcategory ? place.subcategory : "";
        e.catIndex = VsPaletteCategoryIndex(place.category);
        e.browse = place.browse && !dup;
        e.level = d.level;
        e.intent = VsIntentFor(d);
        e.doc = VsNodeDocFor(d.type);
        e.rank = api ? 2 : 0;
        if (const char* what = VsNodeHelp(d.type)) e.helpLine = what;
        else if (api && !api->doc.empty()) e.helpLine = VsPlainText(api->doc);
        else if (d.title && d.title[0]) e.helpLine = std::string(d.title) + ".";
        if (const char* ex = VsNodeExample(d.type)) e.example = ex;
        e.keywords = VsNodeSearchText(d.type);
        e.keywords += std::string(" ") + kVsIntents[e.intent];   // "move things" finds the movement nodes
        if (const std::string syn = VsSynonymWords(std::string(d.title ? d.title : "") + " " + e.keywords); !syn.empty()) e.keywords += " " + syn;
        if (api) { if (!e.keywords.empty()) e.keywords += ' '; e.keywords += "engine api"; }
        // Where this node used to sit (Scene Atmosphere, Objects Survival ...): people who remember the old topic
        // still find it by typing that name.
        if (const std::string moved = VsMovedFromWords(was, place); !moved.empty()) {
            if (!e.keywords.empty()) e.keywords += ' ';
            e.keywords += moved;
        }
        if (d.cat >= 0 && d.cat < CAT_COUNT) {
            static const char* oldCat[] = {"Events","Flow","Variables","Math","Logic","Input","Transform","Actions","Data","Multiplayer & Steam","Engine API","Script builtins"};
            if (!e.keywords.empty()) e.keywords += ' ';
            e.keywords += oldCat[d.cat];
        }
        e.col = bright(catColor(e.catIndex));
        // Built from the pins: what goes in, what comes out.
        auto join = [](const std::vector<const char*>& v) {
            std::string s; for (const char* p : v) if (p && *p) { if (!s.empty()) s += ", "; s += p; } return s;
        };
        const std::string in = join(d.dataIn), out = join(d.dataOut), ex = join(d.execOut);
        // What it does in plain words (VisualScriptNodeHelp.hpp), then how it plugs in.
        if (const char* what = VsNodeHelp(d.type)) e.desc = std::string(what) + " ";
        if (api) e.desc = VsPlainText(api->doc) + " In a script: " + api->code + ". ";
        if (!d.execIn && !d.execOut.empty() && d.dataIn.empty()) e.desc += "An event: starts a flow. ";
        else if (d.execIn) e.desc += "Runs when the flow reaches it. ";
        else e.desc += "Gives a value to other nodes. ";
        if (!in.empty())  e.desc += "Takes: " + in + ". ";
        if (!out.empty()) e.desc += "Gives: " + out + ". ";
        if (!ex.empty())  e.desc += "Continues to: " + ex + ". ";
        if (d.param && d.paramHint) e.desc += std::string("Setting: ") + d.paramHint + ".";
        e.titleLower = lower(e.title);
        all.push_back(std::move(e));
    }
    if (rebuild) for (int pass = 0; pass < 2; ++pass) {
        const bool cond = pass == 1;
        const VsDef* sd = FindDef(cond ? "Condition" : "Action");
        if (!StepCatalog || !sd) continue;
        if (const auto* cat = StepCatalog(cond))
            for (const auto& si : *cat) {
                if (si.listOnly) continue;
                VsPlace place = VsPlaceForStep(si.op.c_str(), si.group.c_str());
                const VsPlace was = VsOldPlaceForStep(si.op.c_str(), si.group.c_str());
                if (VsPaletteStepIsDuplicate(si.label)) continue;   // the hand-written node is the one row
                const bool sameAsNode = false;
                Entry e; e.def = sd; e.step = &si; e.cond = cond; e.title = si.label;
                e.cat = place.category ? place.category : "";
                e.sub = place.subcategory ? place.subcategory : "";
                e.catIndex = VsPaletteCategoryIndex(place.category);
                e.browse = place.browse && !sameAsNode;
                e.level = VsLevelForStep(si.op.c_str(), si.group.c_str());
                e.intent = VsIntentForName(si.op + " " + si.group, CAT_ACTION, false);
                e.helpLine = si.desc;
                e.rank = 1;
                e.key = VsPaletteStepKey(cond, si.op.c_str()); e.desc = si.desc;
                e.keywords = si.group + (cond ? " check something" : " do something");
                if (const std::string moved = VsMovedFromWords(was, place); !moved.empty()) e.keywords += " " + moved;
                e.col = bright(catColor(e.catIndex));
                e.titleLower = lower(e.title);
                all.push_back(std::move(e));
            }
    }
    // Favourites and recently added, remembered between sessions (VsPaletteStore).
    VsPaletteStore& store = VsStore();
    std::vector<std::string>& s_fav = store.fav;
    std::vector<std::string>& s_recent = store.recent;
    int& s_filter = store.filter;
    bool& s_allNodes = store.allNodes;     // "Show all nodes" (not the wire switch s_showAll below)
    static bool s_searchAll = false;       // "Search all nodes" was clicked: the search looks through hidden nodes too
    static int s_hiddenHits = 0;           // how many hidden nodes the last search skipped
    static std::size_t s_listVer = 0;   // bumped when favourites or recents change (the cached rows depend on them)
    auto saveLists = [&] { store.save(); };
    const VsLevelFilter levelFilter = (VsLevelFilter)s_filter;
    auto passesLevel = [&](VsLevel level) { return VsLevelPasses(level, levelFilter); };
    auto isFav = [&](const std::string& k) { return std::find(s_fav.begin(), s_fav.end(), k) != s_fav.end(); };
    auto add = [&](const Entry& e) {
        if (e.step) addNode(*e.def, okay::ActionList::EncodeItem({e.step->op, {}}));
        else addNode(*e.def, std::string());
        searchnav::RecentPush(s_recent, e.key);   // newest first, no repeats, the last 8
        ++s_listVer;
        saveLists();
        ImGui::CloseCurrentPopup();
    };

    // The popup: a sticky top (pin line, search box, level buttons), the topics on the left and the nodes on the right,
    // each pane scrolling on its own. Sidebar ids: -4 Starter, -2 Recent, -1 Favourites, -3 All, otherwise a topic index.
    namespace sn = searchnav;
    ImGuiIO& io = ImGui::GetIO();
    static int s_side = -4;               // the Starter group first
    static int s_kb = 0;                  // the one highlighted row (mouse and keyboard share it)
    static bool s_listFocus = false;      // Tab: the keys drive the list and the text box is read-only
    static bool s_showAll = false;        // wire dragged: also list the nodes that do not fit
    static bool s_scrollTop = false;      // the list just changed: start at the top
    static bool s_sideReveal = false;     // the topic changed by key: scroll the topic list to it
    static int  s_prevSide = -3;
    static float s_viewH = 300.0f, s_rowStride = 60.0f;
    static std::vector<int> s_catIds;     // the topics on screen, top to bottom (Left / Right step through them)
    static char search[64] = "";
    static bool s_focusSearch = false;    // put the cursor in the search box (drawn this frame)
    const bool wire = st.linkPending;
    if (ImGui::IsWindowAppearing()) {
        search[0] = '\0'; s_kb = 0; s_listFocus = false; s_showAll = false; s_searchAll = false; s_scrollTop = true;
        s_focusSearch = true;   // asked for here, done just before the search box is drawn (not on the first widget)
        if (wire) s_side = -3;   // a wire dropped on empty space: start on the whole list, the nodes that fit
    }
    const bool fitOnly = wire && !s_showAll;
    const float winW = ImGui::GetWindowSize().x;
    // Typing anywhere goes to the search box. While the text box does not have the keyboard yet (the popup is still opening,
    // or the list had the keys) it would drop the first letters, so those are put in the box here.
    static bool s_searchActive = false;   // the text box had the keyboard last frame (then it takes typing itself)
    static int  s_keepEnd = 0;            // frames left in which the box puts its cursor at the end instead of selecting all
    if (ImGui::IsWindowAppearing()) s_searchActive = false;
    if (!s_searchActive && io.InputQueueCharacters.Size > 0 && !io.KeyCtrl && !io.KeyAlt) {
        std::vector<unsigned> typed;
        for (int ci = 0; ci < io.InputQueueCharacters.Size; ++ci) typed.push_back((unsigned)io.InputQueueCharacters[ci]);
        if (sn::AppendTypedChars(search, sizeof search, typed.data(), (int)typed.size()) > 0) { s_kb = 0; s_scrollTop = true; }
        io.InputQueueCharacters.resize(0);
        s_listFocus = false; s_focusSearch = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        s_listFocus = !s_listFocus;
        if (!s_listFocus) s_focusSearch = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    const float lhTop = ImGui::GetTextLineHeight();
    if (wire) {   // a clear banner: why the list is short, and the switch to see everything
        const bool exec = st.linkExec;
        const float bh = std::max(36.0f, lhTop + 16.0f);
        const ImVec2 b0 = ImGui::GetCursorScreenPos();
        const float bw = ImGui::GetContentRegionAvail().x;
        ImDrawList* bdl = ImGui::GetWindowDrawList();
        bdl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.35f), 8.0f);
        std::string msg = sn::PinContextLine(fitOnly);
        if (!exec && fitOnly) msg += std::string("  (") + okay::vs::PinTypeName(dragged) + " pin)";
        bdl->AddText(ImVec2(b0.x + 14.0f, b0.y + (bh - lhTop) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), msg.c_str());
        const char* sw = fitOnly ? "Show all" : "Only what fits";
        const float swW = ImGui::CalcTextSize(sw).x + 28.0f;
        ImGui::SetCursorScreenPos(ImVec2(b0.x + bw - swW - 4.0f, b0.y + 3.0f));
        if (ImGui::Button(sw, ImVec2(swW, bh - 6.0f))) { s_showAll = !s_showAll; s_kb = 0; s_scrollTop = true; }
        ImGui::SetCursorScreenPos(ImVec2(b0.x, b0.y + bh + ImGui::GetStyle().ItemSpacing.y));
    }
    // The search box: 40 px tall with a magnifier, and a Clear button at its right.
    const float boxH = 40.0f;
    const float clearW = ImGui::CalcTextSize("Clear").x + 3.0f * dm.framePad.x;
    if (s_focusSearch) { ImGui::SetKeyboardFocusHere(); s_focusSearch = false; s_keepEnd = 2; }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(40.0f, (boxH - lhTop) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    const ImVec2 boxP = ImGui::GetCursorScreenPos();
    if (s_listFocus) {
        // The list has the keys: the box is only drawn (a text box that keeps the keyboard would write its old text back
        // over what is typed next). Click it, press Tab or just type to go back to it.
        const float fakeW = ImGui::GetContentRegionAvail().x - (clearW + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InvisibleButton("##vssearchfake", ImVec2(fakeW, boxH))) { s_listFocus = false; s_focusSearch = true; }
        ImDrawList* fdl = ImGui::GetWindowDrawList();
        fdl->AddRectFilled(boxP, ImVec2(boxP.x + fakeW, boxP.y + boxH), ImGui::GetColorU32(ImGuiCol_FrameBg), 8.0f);
        const bool none = search[0] == '\0';
        fdl->AddText(ImVec2(boxP.x + 40.0f, boxP.y + (boxH - lhTop) * 0.5f),
                     ImGui::GetColorU32(none ? ImGuiCol_TextDisabled : ImGuiCol_Text), none ? "Search nodes by name or what they do..." : search);
        s_searchActive = false;
    } else {
        ImGui::SetNextItemWidth(-(clearW + ImGui::GetStyle().ItemSpacing.x));
        // A box focused by key selects all its text, so the next letter would replace what is already typed: the callback
        // puts the cursor at the end instead (for the first frames after the focus).
        auto keepEnd = [](ImGuiInputTextCallbackData* d) -> int {
            if (*(int*)d->UserData > 0) { d->CursorPos = d->BufTextLen; d->SelectionStart = d->SelectionEnd = d->BufTextLen; }
            return 0;
        };
        if (ImGui::InputTextWithHint("##vssearch", "Search nodes by name or what they do...", search, sizeof search,
                                     ImGuiInputTextFlags_CallbackAlways, keepEnd, &s_keepEnd))
            { s_kb = 0; s_scrollTop = true; }
        s_searchActive = ImGui::IsItemActive();
        if (s_keepEnd > 0 && s_searchActive) --s_keepEnd;
    }
    {   // the magnifier, drawn (the UI font has no glyph for it)
        ImDrawList* mdl = ImGui::GetWindowDrawList();
        const ImVec2 c(boxP.x + 20.0f, boxP.y + boxH * 0.5f - 2.0f);
        const ImU32 mc = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        mdl->AddCircle(c, 7.0f, mc, 16, 2.2f);
        mdl->AddLine(ImVec2(c.x + 5.0f, c.y + 5.0f), ImVec2(c.x + 11.0f, c.y + 11.0f), mc, 2.4f);
    }
    ImGui::PopStyleVar(2);
    const bool searchEmpty = search[0] == '\0';
    ImGui::SameLine();
    ImGui::BeginDisabled(searchEmpty);
    if (ImGui::Button("Clear", ImVec2(clearW, boxH))) { search[0] = '\0'; s_kb = 0; s_scrollTop = true; s_listFocus = false; s_focusSearch = true; }
    ImGui::EndDisabled();
    // Level buttons: one big segmented row.
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, ImGui::GetStyle().ItemSpacing.y));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        const float filterW = (ImGui::GetContentRegionAvail().x - 3.0f * 3.0f) / 4.0f;
        const float fh = std::max(36.0f, lhTop + 16.0f);
        for (int fi = 0; fi < 4; ++fi) {
            if (fi) ImGui::SameLine();
            const bool on = s_filter == fi;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
            if (ImGui::Button(VsLevelFilterName((VsLevelFilter)fi), ImVec2(filterW, fh))) {
                s_filter = fi; s_kb = 0; s_scrollTop = true; saveLists();
            }
            if (on) ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar(2);
    }
    if (ImGui::Checkbox("Show all nodes", &s_allNodes)) { s_kb = 0; s_scrollTop = true; ++s_listVer; saveLists(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: only the everyday blocks. On: the whole library, for advanced scripts.");
    if (!s_allNodes && !(s_side == -1 || s_side == -2 || s_side == -3 || s_side <= -10)) s_side = -3;
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const std::string needle = lower(search);

    // Fuzzy score: whole title > start > start of a word > anywhere > letters in
    // order; type, category and description count a little. `hits` marks the
    // matched letters of the title.
    auto score = [&](const Entry& e, std::vector<int>& hits) -> int {
        hits.clear();
        if (needle.empty()) return 1;
        const std::string& t = e.titleLower;
        if (t == needle) { for (int i = 0; i < (int)t.size(); ++i) hits.push_back(i); return 1000; }
        std::size_t p = t.find(needle);
        if (p != std::string::npos) {
            for (std::size_t i = 0; i < needle.size(); ++i) hits.push_back((int)(p + i));
            if (p == 0) return 800;
            if (t[p - 1] == ' ' || t[p - 1] == '(') return 650;
            return 500 - (int)p;
        }
        int run = 0, sc = 0; std::size_t j = 0;
        for (std::size_t i = 0; i < t.size() && j < needle.size(); ++i) {
            if (t[i] == needle[j]) { hits.push_back((int)i); ++j; run++; sc += 3 + run * 2 + (i == 0 || t[i - 1] == ' ' ? 6 : 0); }
            else run = 0;
        }
        if (j == needle.size()) return 100 + sc;
        hits.clear();
        if (!e.blobBuilt) {   // the long searchable text is built the first time a search reaches this entry
            std::string pins;
            if (e.def) {
                for (int i = 0; i < (int)e.def->dataIn.size(); ++i)
                    pins += std::string(okay::vs::PinTypeName(okay::vs::InputType(e.def->type, i))) + " ";
                for (int i = 0; i < (int)e.def->dataOut.size(); ++i)
                    pins += std::string(okay::vs::PinTypeName(okay::vs::OutputType(e.def->type, i, ""))) + " ";
            }
            e.blobLower = lower(e.cat + " " + e.sub + " " + e.keywords + " " +
                                (e.def ? e.def->type : "") + " " +
                                (e.def ? VsNodeSearchText(e.def->type) : std::string()) + " " +
                                (e.step ? e.step->op : "") + " " + e.desc + " " + pins +
                                (e.doc ? " " + VsSearchBlob(*e.doc) : std::string()));
            e.blobBuilt = true;
        }
        return e.blobLower.find(needle) != std::string::npos ? 40 : 0;
    };

    // ---- Topics (left) ----
    const float sideW = sn::CategoryPaneWidth(winW);
    const float footH = ImGui::GetTextLineHeight() + 16.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dm.paletteSideFramePad.x, dm.paletteSideFramePad.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dm.paletteSideItemSpacing.x, dm.paletteSideItemSpacing.y));
    ImGui::BeginChild("##vspside", ImVec2(sideW, -footH), ImGuiChildFlags_Borders);
    s_catIds.clear();
    auto sideItem = [&](int id, const char* label, ImU32 dot, int count, bool selected) {
        ImGui::PushID(id + 10);
        s_catIds.push_back(id);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        const float padY = dm.paletteSideRowPad;
        const bool clicked = ImGui::Selectable("##side", selected, 0, ImVec2(0, h + 2.0f * padY));
        if (selected && s_sideReveal) { ImGui::SetScrollHereY(0.5f); }
        ImDrawList* sdl = ImGui::GetWindowDrawList();
        if (dot) sdl->AddCircleFilled(ImVec2(p.x + 7, p.y + padY + h * 0.5f + 1), 4.0f, dot);
        const float right = p.x + ImGui::GetContentRegionAvail().x;
        char n[16] = ""; if (count >= 0) std::snprintf(n, sizeof n, "%d", count);
        const float nw = ImGui::CalcTextSize(n).x;
        sdl->PushClipRect(ImVec2(p.x, p.y), ImVec2(right - nw - 6, p.y + h + 2.0f * padY), true);
        const std::string shown = sn::ElideDescription(label, [&](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x; }, right - nw - 6 - (p.x + 18));
        sdl->AddText(ImVec2(p.x + 18, p.y + padY), ImGui::GetColorU32(ImGuiCol_Text), shown.c_str());
        sdl->PopClipRect();
        if (count >= 0) sdl->AddText(ImVec2(right - nw - 2, p.y + padY), ImGui::GetColorU32(ImGuiCol_TextDisabled), n);
        if (ImGui::IsItemHovered()) {
            const char* blurb = (id >= 0 && id < VsPaletteCategoryCount()) ? kPaletteCategories[id].blurb
                              : id == -4 ? "The nodes a first script is built from."
                              : id == -2 ? "The last nodes you added."
                              : id == -1 ? "Nodes you starred."
                              : id <= -100 && -100 - id < kVsIntentCount ? kVsIntentBlurbs[-100 - id]
                              : nullptr;
            if (blurb && *blurb) ImGui::SetTooltip("%s\n%s", label, blurb);
            else if (shown != label) ImGui::SetTooltip("%s", label);
        }
        ImGui::PopID();
        return clicked;
    };
    // The counts and the "fits the wire" flags only change when one of these does, not on every frame.
    static std::vector<int> s_catCount;
    static int s_browseAll = 0, s_starterN = 0, s_quickN = 0, s_beginnerN = 0;
    static int s_groupN[kGroupCount] = {};
    static int s_intentN[kVsIntentCount] = {};
    {
        using SideKey = std::tuple<std::size_t, bool, bool, bool, int, long long, long long, int, bool, bool>;
        static SideKey s_sideKey{};
        static bool s_sideInit = false;
        const SideKey key{s_gen, st.linkPending, st.linkExec, st.linkFromOutput, (int)dragged,
                          (long long)st.linkNode, (long long)st.linkPin, s_filter, s_showAll, s_allNodes};
        if (!s_sideInit || !(key == s_sideKey)) {
            s_sideKey = key; s_sideInit = true;
            for (auto& e : all) e.fits = !st.linkPending || !e.def || fits(*e.def);
            s_catCount.assign((std::size_t)std::max(1, VsPaletteCategoryCount()), 0);
            s_browseAll = 0; s_starterN = 0; s_quickN = 0; s_beginnerN = 0; for (int& gn : s_groupN) gn = 0; for (int& in : s_intentN) in = 0;
            for (const auto& e : all) {
                if (!sn::ShowEntry(st.linkPending, s_showAll, e.fits) || !e.browse || !passesLevel(e.level) || VsHiddenWhileBrowsing(e.def ? e.def->type : "", s_filter, s_allNodes)) continue;
                ++s_browseAll;
                if (e.intent >= 0 && e.intent < kVsIntentCount) ++s_intentN[e.intent];
                if (e.def && VsIsStarter(e.def->type)) ++s_starterN;
                if (e.def && okay::vs::FindQuickBlock(e.def->type)) ++s_quickN;
                if (const int bg = e.def ? VsBeginnerGroup(e.def->type) : -1; bg >= 0) { ++s_beginnerN; ++s_groupN[bg]; }
                if (e.catIndex >= 0 && e.catIndex < (int)s_catCount.size()) ++s_catCount[(std::size_t)e.catIndex];
            }
        }
    }
    // "What do you want to do?": the same nodes, grouped by the job they do (Move things, Show messages ...).
    auto intentItems = [&]() {
        ImGui::Spacing();
        ImGui::TextDisabled("What do you want to do?");
        for (int i = 0; i < kVsIntentCount; ++i) {
            if (!s_intentN[i]) continue;
            if (sideItem(-100 - i, kVsIntents[i], 0, s_intentN[i], s_side == -100 - i)) s_side = -100 - i;
        }
    };
    if (!s_allNodes) {
        // Beginner mode: six big groups and nothing else (no topic tree). Each lists about ten blocks.
        if (sideItem(-3, "Everything", 0, s_beginnerN, s_side == -3)) s_side = -3;
        ImGui::Spacing();
        for (int bg = 0; bg < kGroupCount; ++bg)
            if (sideItem(-10 - bg, kVsBeginnerGroupNames[bg], 0, s_groupN[bg], s_side == -10 - bg)) s_side = -10 - bg;
        ImGui::Separator();
        if (sideItem(-2, "Recent", 0, (int)s_recent.size(), s_side == -2)) s_side = -2;
        if (sideItem(-1, "Favourites", 0, (int)s_fav.size(), s_side == -1)) s_side = -1;
        intentItems();
    } else {
    if (sideItem(-4, "Starter", 0, s_starterN, s_side == -4)) s_side = -4;
    if (sideItem(-5, "Quick blocks", 0, s_quickN, s_side == -5)) s_side = -5;
    if (sideItem(-2, "Recent", 0, (int)s_recent.size(), s_side == -2)) s_side = -2;
    if (sideItem(-1, "Favourites", 0, (int)s_fav.size(), s_side == -1)) s_side = -1;
    if (sideItem(-3, "All", 0, s_browseAll, s_side == -3)) s_side = -3;
    intentItems();
    ImGui::Separator();
    {
        const char* lastSection = nullptr;
        for (int c = 0; c < VsPaletteCategoryCount(); ++c) {
            const int n = c < (int)s_catCount.size() ? s_catCount[(std::size_t)c] : 0;
            if (!n) continue;
            if (!lastSection || std::strcmp(lastSection, kPaletteCategories[c].section) != 0) {   // a new section heading
                lastSection = kPaletteCategories[c].section;
                ImGui::Spacing();
                ImGui::TextDisabled("%s", lastSection);
            }
            if (sideItem(c, kPaletteCategories[c].name, bright(catColor(c)), n, s_side == c)) s_side = c;
        }
    }
    }
    s_sideReveal = false;
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    if (s_side != s_prevSide) { s_prevSide = s_side; s_kb = 0; s_scrollTop = true; }
    ImGui::SameLine();

    // ---- Nodes (right) ----
    // The rows (which entries, in which order) only change when the search text, the topic, the level, the wire being
    // dragged, the favourites/recents or the list itself change, so they are worked out then and kept. Only the rows on
    // screen are drawn each frame (a clipper).
    struct Row { const Entry* e; int sc; };
    static std::vector<Row> s_rows;
    static bool s_suggest = false;                     // the rows are "did you mean" suggestions, not matches
    {
        using RowKey = std::tuple<std::size_t, std::size_t, bool, bool, bool, int, long long, long long, int, int, bool, std::string>;
        static RowKey s_rowKey{};
        static bool s_rowInit = false;
        const RowKey key{s_gen, s_listVer, st.linkPending, st.linkExec, st.linkFromOutput, (int)dragged,
                         (long long)st.linkNode, (long long)st.linkPin, s_filter, s_side, s_showAll, needle + (s_allNodes ? "|all" : "") + (s_searchAll ? "|sa" : "")};
        if (!s_rowInit || !(key == s_rowKey)) {
            s_rowKey = key; s_rowInit = true;
            s_rows.clear(); s_suggest = false; s_hiddenHits = 0;
            auto recentRank = [&](const std::string& k) {
                const auto it = std::find(s_recent.begin(), s_recent.end(), k);
                return it == s_recent.end() ? 10000 : (int)(it - s_recent.begin());
            };
            std::vector<int> hits;
            for (const auto& e : all) {
                if (!sn::ShowEntry(st.linkPending, s_showAll, e.fits) || !passesLevel(e.level)) continue;
                if (needle.empty() && (!e.browse || VsHiddenWhileBrowsing(e.def ? e.def->type : "", s_filter, s_allNodes))) continue;
                if (!needle.empty() && !s_searchAll && VsHiddenWhileBrowsing(e.def ? e.def->type : "", s_filter, s_allNodes)) {
                    if (score(e, hits) > 0) ++s_hiddenHits;   // found, but hidden: offered with "Search all nodes"
                    continue;
                }
                // Typing searches every topic, so a name is found wherever it lives.
                // Only the level filter still applies.
                if (needle.empty()) {
                    if (s_side == -4 && !(e.def && VsIsStarter(e.def->type))) continue;
                    if (s_side == -5 && !(e.def && okay::vs::FindQuickBlock(e.def->type))) continue;
                    if (s_side <= -10 && s_side > -100 && !(e.def && VsBeginnerGroup(e.def->type) == -10 - s_side)) continue;
                    if (s_side <= -100 && e.intent != -100 - s_side) continue;
                    if (s_side >= 0 && e.catIndex != s_side) continue;
                    if (s_side == -1 && !isFav(e.key)) continue;
                    if (s_side == -2 && std::find(s_recent.begin(), s_recent.end(), e.key) == s_recent.end()) continue;
                }
                const int sc = needle.empty() ? score(e, hits) : VsStarterBoost(score(e, hits), e.def && (VsIsStarter(e.def->type) || okay::vs::FindQuickBlock(e.def->type)), e.def && std::strncmp(e.def->type, "Fn.", 3) == 0);
                if (sc > 0) s_rows.push_back(Row{&e, sc});
            }
            if (!needle.empty())
                std::stable_sort(s_rows.begin(), s_rows.end(), [&](const Row& a, const Row& b) {
                    return VsSearchRanksBefore(a.sc, a.e->level, recentRank(a.e->key),
                                               b.sc, b.e->level, recentRank(b.e->key));
                });
            else if (s_side == -2)
                std::stable_sort(s_rows.begin(), s_rows.end(), [&](const Row& a, const Row& b) {
                    return recentRank(a.e->key) < recentRank(b.e->key);
                });
            else if (s_side == -4)
                std::stable_sort(s_rows.begin(), s_rows.end(), [](const Row& a, const Row& b) {
                    return VsStarterIndex(a.e->def ? a.e->def->type : "") < VsStarterIndex(b.e->def ? b.e->def->type : "");
                });
            else if (st.linkPending)
                std::stable_sort(s_rows.begin(), s_rows.end(), [](const Row& a, const Row& b) {
                    if (a.e->level != b.e->level) return (int)a.e->level < (int)b.e->level;
                    if (a.e->catIndex != b.e->catIndex) return a.e->catIndex < b.e->catIndex;
                    return a.e->rank < b.e->rank;
                });
            else
                std::stable_sort(s_rows.begin(), s_rows.end(), [](const Row& a, const Row& b) {
                    if (a.e->catIndex != b.e->catIndex) return a.e->catIndex < b.e->catIndex;
                    const int ao = VsSubOrder(a.e->sub.c_str()), bo = VsSubOrder(b.e->sub.c_str());
                    if (ao != bo) return ao < bo;
                    if (a.e->sub != b.e->sub) return a.e->sub < b.e->sub;
                    if (a.e->level != b.e->level) return (int)a.e->level < (int)b.e->level;
                    return a.e->rank < b.e->rank;
                });
            // "Show all" while dragging a wire: the nodes that fit stay on top.
            if (st.linkPending && s_showAll && needle.empty())
                std::stable_partition(s_rows.begin(), s_rows.end(), [](const Row& r) { return r.e->fits; });
            // Nothing matched what was typed: offer the nearest few instead of a blank box.
            if (s_rows.empty() && !needle.empty()) {
                std::vector<const Entry*> pool; std::vector<std::string> titles;
                for (const auto& e : all) {
                    if (!sn::ShowEntry(st.linkPending, s_showAll, e.fits) || !passesLevel(e.level) || !e.browse
                        || VsHiddenWhileBrowsing(e.def ? e.def->type : "", s_filter, s_allNodes)) continue;
                    pool.push_back(&e); titles.push_back(e.titleLower);
                }
                for (int i : sn::NearestIndices(titles, needle, 5)) s_rows.push_back(Row{pool[(std::size_t)i], 0});
                s_suggest = !s_rows.empty();
            }
        }
    }
    const std::vector<Row>& rows = s_rows;

    // Keys. Up / Down / PageUp / PageDown always move the highlight; Home / End and Left / Right belong to the text box
    // while it has text and the focus (so you can still edit), and to the list otherwise.
    const int count = (int)rows.size();
    const int pageRows = std::max(1, (int)(s_viewH / std::max(1.0f, s_rowStride)) - 1);
    const bool textKeys = !s_listFocus && !searchEmpty;   // the text box owns Home / End / Left / Right
    int kbBefore = s_kb;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  s_kb = sn::MoveSelection(s_kb, count, sn::Move::Down, pageRows);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    s_kb = sn::MoveSelection(s_kb, count, sn::Move::Up, pageRows);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown))   s_kb = sn::MoveSelection(s_kb, count, sn::Move::PageDown, pageRows);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp))     s_kb = sn::MoveSelection(s_kb, count, sn::Move::PageUp, pageRows);
    if (!textKeys && ImGui::IsKeyPressed(ImGuiKey_Home)) s_kb = sn::MoveSelection(s_kb, count, sn::Move::Home, pageRows);
    if (!textKeys && ImGui::IsKeyPressed(ImGuiKey_End))  s_kb = sn::MoveSelection(s_kb, count, sn::Move::End, pageRows);
    if (!textKeys && searchEmpty) {   // topics only mean something while browsing
        const int dir = ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? 1 : (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? -1 : 0);
        if (dir) { s_side = sn::StepCategory(s_catIds, s_side, dir); s_sideReveal = true; }
    }
    s_kb = sn::MoveSelection(s_kb, count, sn::Move::None, pageRows);
    const bool kbMoved = s_kb != kbBefore;

    // Three columns: topics | node rows | preview of the chosen node.
    const float previewW = sn::PreviewPaneWidth(winW);
    const Entry* hovered = nullptr;
    const Entry* toAdd = nullptr;
    ImGui::BeginChild("##vspres", ImVec2(-(previewW + ImGui::GetStyle().ItemSpacing.x), -footH), ImGuiChildFlags_Borders);
    if (!needle.empty() && !s_allNodes && !s_searchAll && s_hiddenHits > 0) {
        ImGui::TextDisabled("%d more nodes match, but they are advanced.", s_hiddenHits);
        ImGui::SameLine();
        if (ImGui::SmallButton("Search all nodes")) { s_searchAll = true; ++s_listVer; }
    }
    // What is said above the list: a suggestion line, or the friendly empty message.
    if (rows.empty() || s_suggest) {
        ImGui::PushTextWrapPos(0.0f);
        if (s_suggest) {
            ImGui::TextWrapped("Nothing is called \"%s\". Did you mean one of these?", search);
        } else {
            ImGui::TextWrapped("%s", sn::EmptyMessage(s_side, !needle.empty(), fitOnly, s_filter != 0).c_str());
            if (fitOnly && ImGui::Button("Show all")) { s_showAll = true; s_kb = 0; s_scrollTop = true; }
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    const float lh = ImGui::GetTextLineHeight();
    const float rowPad = 8.0f;
    const float lineH = sn::RowHeight(dm.rowH, lh, rowPad);
    const float stride = lineH + ImGui::GetStyle().ItemSpacing.y;
    s_rowStride = stride; s_viewH = ImGui::GetWindowHeight();
    const float listTop = ImGui::GetCursorPosY();
    if (s_scrollTop) { ImGui::SetScrollY(0.0f); s_scrollTop = false; }
    else if (kbMoved || s_sideReveal)
        ImGui::SetScrollY(sn::RevealScroll(ImGui::GetScrollY(), ImGui::GetWindowHeight(), listTop + (float)s_kb * stride, lineH, 8.0f));
    // One highlight for hover and keys: the row's own hover colour is switched off and the chosen row is painted by hand.
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    const bool mouseMoved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    const bool showCat = !needle.empty() || s_side < 0;
    const float hitW = std::max(32.0f, dm.smallHit + 6.0f);   // the star's clickable square
    ImGuiListClipper clip;
    clip.Begin((int)rows.size(), stride);
    std::vector<int> hitsNow;
    while (clip.Step())
    for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        const Entry& e = *rows[(std::size_t)i].e;
        score(e, hitsNow);
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, lineH))) toAdd = &e;
        if (ImGui::IsItemHovered() && mouseMoved) s_kb = i;
        ImDrawList* rdl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        const float right = p.x + w;
        if (i == s_kb) rdl->AddRectFilled(p, ImVec2(right, p.y + lineH), ImGui::GetColorU32(ImGuiCol_ButtonHovered, 0.65f), 8.0f);
        // Icon: a 36 px rounded tile in the topic's colour with the first letter of the name.
        const float tile = 36.0f;
        const ImVec2 t0(p.x + 12.0f, p.y + (lineH - tile) * 0.5f);
        rdl->AddRectFilled(t0, ImVec2(t0.x + tile, t0.y + tile), e.col, 8.0f);
        if (!e.title.empty()) {
            const char letter[2] = {(char)std::toupper((unsigned char)e.title[0]), 0};
            const ImVec2 ls = ImGui::CalcTextSize(letter);
            rdl->AddText(ImVec2(t0.x + (tile - ls.x) * 0.5f, t0.y + (tile - ls.y) * 0.5f), IM_COL32(20, 24, 32, 255), letter);
        }
        // Right column: the star (a big square to click). The level tag sits at the end of the name line.
        const char* badge = VsLevelBadge(e.level);
        const float badgeW = ImGui::CalcTextSize(badge).x + 16.0f;
        const float starX = right - hitW - 4.0f;
        const float textX = t0.x + tile + 12.0f;
        const float textRight = starX - 6.0f;
        const float textTop = p.y + (lineH - (3.0f * lh + 4.0f)) * 0.5f;
        const float badgeX = textRight - badgeW;
        {
            const ImVec2 b0(badgeX, textTop - 2.0f), b1(badgeX + badgeW, b0.y + lh + 4.0f);
            const ImU32 bg = e.level == VsLevel::Basic ? IM_COL32(46, 110, 78, 255)
                           : e.level == VsLevel::Advanced ? IM_COL32(96, 72, 120, 255)
                           : IM_COL32(52, 78, 120, 255);
            rdl->AddRectFilled(b0, b1, bg, 5.0f);
            rdl->AddText(ImVec2(b0.x + 8.0f, b0.y + 2.0f), IM_COL32(236, 240, 248, 255), badge);
        }
        // Name, matched letters in the accent colour.
        float x = textX;
        const ImU32 normal = ImGui::GetColorU32(ImGuiCol_Text), hit = ImGui::GetColorU32(ImGuiCol_PlotLinesHovered);
        for (int c = 0; c < (int)e.title.size(); ++c) {
            const char buf[2] = {e.title[(std::size_t)c], 0};
            const bool h = std::find(hitsNow.begin(), hitsNow.end(), c) != hitsNow.end();
            const float cw = ImGui::CalcTextSize(buf).x;
            if (x + cw > badgeX - 10.0f - ImGui::CalcTextSize("...").x) {   // a long name stops before the level tag
                rdl->AddText(ImVec2(x, textTop), normal, "...");
                x = badgeX; break;
            }
            rdl->AddText(ImVec2(x, textTop), h ? hit : normal, buf);
            x += cw;
        }
        if (showCat && !e.cat.empty()) {   // the topic, next to the name, when there is room
            const std::string tag = "  " + e.cat;
            if (x + ImGui::CalcTextSize(tag.c_str()).x < badgeX - 8.0f)
                rdl->AddText(ImVec2(x, textTop), ImGui::GetColorU32(ImGuiCol_TextDisabled), tag.c_str());
        }
        // Up to two lines under the name: what it does in plain words (RowDescription); "..." only when it runs out.
        {
            const auto lines = sn::WrapLines(sn::RowDescription(e.title, e.helpLine, e.sub),
                [&](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x; }, textRight - textX, 2);
            for (std::size_t li = 0; li < lines.size(); ++li)
                rdl->AddText(ImVec2(textX, textTop + lh + 2.0f + (float)li * (lh + 1.0f)), ImGui::GetColorU32(ImGuiCol_Text, 0.72f), lines[li].c_str());
        }
        // The favourite star.
        ImGui::SetCursorScreenPos(ImVec2(starX, p.y));
        const bool fav = isFav(e.key);
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##fav", ImVec2(hitW, lineH))) {
            if (fav) s_fav.erase(std::remove(s_fav.begin(), s_fav.end(), e.key), s_fav.end());
            else s_fav.push_back(e.key);
            ++s_listVer;
            saveLists();
        }
        const bool sh = ImGui::IsItemHovered();
        if (sh) ImGui::SetTooltip(fav ? "Remove from favourites" : "Add to favourites");
        {   // a drawn star (the UI font has no star glyph)
            const ImVec2 c(sp.x + hitW * 0.5f, sp.y + lineH * 0.5f);
            ImVec2 pts[10];
            for (int k = 0; k < 10; ++k) {
                const float a = -1.5707963f + (float)k * 0.6283185f, r = (k % 2) ? 4.4f : 10.0f;
                pts[k] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
            }
            const ImU32 sc = fav ? IM_COL32(255, 204, 64, 255) : sh ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            if (fav) rdl->AddConcavePolyFilled(pts, 10, sc); else rdl->AddPolyline(pts, 10, sc, ImDrawFlags_Closed, 1.6f);
        }
        ImGui::PopID();
    }
    ImGui::PopStyleColor(3);
    if (enter && !rows.empty()) toAdd = rows[(std::size_t)std::clamp(s_kb, 0, (int)rows.size() - 1)].e;
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- Preview (right): what the chosen node does, before you add it ----
    hovered = rows.empty() ? nullptr : rows[(std::size_t)std::clamp(s_kb, 0, (int)rows.size() - 1)].e;
    ImGui::BeginChild("##vspinfo", ImVec2(0, -footH), ImGuiChildFlags_Borders);
    if (hovered) {
        const Entry& e = *hovered;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::SetWindowFontScale(1.15f);
        ImGui::TextUnformatted(e.title.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("%s  -  %s", VsLevelName(e.level), e.cat.c_str());
        ImGui::Spacing();
        const std::string what = sn::RowDescription(e.title, e.helpLine, std::string());
        ImGui::TextWrapped("%s", what.empty() ? "No description yet." : what.c_str());
        if (!e.example.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Example");
            ImGui::TextWrapped("%s", e.example.c_str());
        }
        if (!needle.empty()) if (const char* mark = VsAdvancedMark(e.def ? e.def->type : "", s_allNodes); mark[0])
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.45f, 1.0f), "%s: turn on Show all nodes to browse it.", mark);
        if (e.def && !e.step) {
            auto names = [](const std::vector<const char*>& v) { std::vector<std::string> o; for (const char* c : v) if (c) o.push_back(c); return o; };
            ImGui::Spacing();
            ImGui::TextDisabled("How it plugs in");
            ImGui::TextWrapped("%s", sn::HowItPlugsIn(e.def->execIn, !e.def->execOut.empty(), !e.def->dataIn.empty()).c_str());
            ImGui::TextWrapped("Needs: %s", sn::NameList(names(e.def->dataIn)).c_str());
            ImGui::TextWrapped("Gives: %s", sn::NameList(names(e.def->dataOut)).c_str());
            if (!e.def->execOut.empty()) ImGui::TextWrapped("Then continues to: %s", sn::NameList(names(e.def->execOut)).c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Add this node", ImVec2(-FLT_MIN, 38.0f))) toAdd = hovered;
    } else {
        ImGui::TextWrapped("Pick a node on the left to read what it does before you add it.");
    }
    ImGui::EndChild();
    // The key hints: readable, grouped (the main three first).
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Up/Down choose     Enter add     Esc close");
    ImGui::SameLine(0.0f, 28.0f);
    ImGui::TextColored(ImVec4(ImGui::GetStyle().Colors[ImGuiCol_Text].x, ImGui::GetStyle().Colors[ImGuiCol_Text].y, ImGui::GetStyle().Colors[ImGuiCol_Text].z, 0.7f),
                       "Tab search or list     Left/Right topic");
    if (toAdd) add(*toAdd);
    ImGui::PopStyleVar(3);
    // A grip at the bottom-right corner: drag it to resize; the size is kept (okay_vsnodes.txt). The popup itself is
    // not resizable by ImGui (it would fall back to its content size), so the size is ours.
    {
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        const ImVec2 g0(wp.x + ws.x - 28.0f, wp.y + ws.y - 28.0f), g1(wp.x + ws.x, wp.y + ws.y);
        static bool s_gripDrag = false;
        const bool over = ImGui::IsMouseHoveringRect(g0, g1, false);
        if (over) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s_gripDrag = true;
        if (s_gripDrag) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            const ImVec2 scr = ImGui::GetMainViewport()->Size;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { s_gripDrag = false; saveLists(); }
            else if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
                const ImVec2 cur = VsPaletteWantedSize(scr);
                const sn::PanelSize ns = sn::DragResize({cur.x, cur.y}, io.MouseDelta.x, io.MouseDelta.y, scr.x, scr.y);
                store.size = ns;
            }
        }
        ImDrawList* gdl = ImGui::GetWindowDrawList();
        const ImU32 gc = ImGui::GetColorU32(over || s_gripDrag ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        for (int k = 1; k <= 3; ++k)
            gdl->AddLine(ImVec2(g1.x - 4.0f - (float)k * 6.0f, g1.y - 4.0f), ImVec2(g1.x - 4.0f, g1.y - 4.0f - (float)k * 6.0f), gc, 1.5f);
    }
}

}  // namespace okay::editor::vsed
