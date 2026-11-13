#pragma once
// The add-node panel: search, collapsible categories, favourites, and a details
// pane. Shared by the right-click menu, a wire dropped in empty space, and Add node.
#include "VisualScriptNodeDocs.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>
#include <fstream>
#include <functional>
#include <map>
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
        ImGui::TextWrapped("%s", doc->description.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("%s", doc->requirements.c_str());
        if (!doc->example.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s", doc->example.c_str()); }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

inline void VsPinTooltip(const VsPinDoc* pin, const char* name, const char* typeName,
                         okay::vs::PinType pinType = okay::vs::PinType::Any, bool isOutput = false, bool exec = false,
                         const char* required = nullptr) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
    ImGui::Text("%s   %s", (name && *name) ? name : "flow", typeName ? typeName : (pin ? pin->type.c_str() : ""));
    if (pin) {
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

/// `addNode(def, arg)` places the node. `arg` is set for action steps.
/// Topics (VisualScriptCategories.hpp) on the left, nodes in the middle, what the
/// highlighted node does on the right. Levels filter the list.
inline void DrawNodePalette(VsEditState& st, const VsEditGraph& g,
                            const std::function<void(const VsDef&, const std::string&)>& addNode) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
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
        int catIndex = 0, rank = 0; bool browse = true; ImU32 col = 0;
        bool fits = true;                       // can take the wire being dragged (set each frame, cheap)
        std::string titleLower;                 // lower-cased once, when the list is built
        mutable std::string blobLower;          // everything searchable, lower-cased on first use
        mutable bool blobBuilt = false;
    };
    auto catColor = [&](int index) {
        // Stable hues so a topic keeps its dot. Common topics stay in the documented order.
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
        return cols[(index >= 0 && index < n) ? index : 0];
    };
    // The list is built once and reused every frame: rebuilding ~2300 entries (a dozen strings and several
    // lookups each) 60 times a second is what made the pop-up drop the frame rate. It is rebuilt only when
    // the registry or a step catalogue changes size (a plugin or graph function was added or removed).
    static std::vector<Entry> s_all;
    static std::size_t s_regN = (std::size_t)-1, s_actN = (std::size_t)-1, s_condN = (std::size_t)-1;
    const std::size_t regN = Registry().size();
    const std::size_t actN = StepCatalog ? (StepCatalog(false) ? StepCatalog(false)->size() : 0) : 0;
    const std::size_t condN = StepCatalog ? (StepCatalog(true) ? StepCatalog(true)->size() : 0) : 0;
    const bool rebuild = regN != s_regN || actN != s_actN || condN != s_condN;
    std::vector<Entry>& all = s_all;
    static std::size_t s_gen = 0;                       // bumped each time the list is rebuilt (pointers into it go stale)
    if (rebuild) { all.clear(); s_regN = regN; s_actN = actN; s_condN = condN; ++s_gen; }
    if (rebuild) for (const auto& d : Registry()) {
        const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
        VsPlace place = api ? VsPlaceForApi(api->apiType.c_str(), api->member.c_str()) : VsPlaceForNode(d.type);
        if (VsPaletteHidden(d)) continue;   // one row per job; untested nodes wait
        const bool dup = false;
        Entry e; e.def = &d; e.title = d.title; e.key = std::string("n:") + d.type;
        e.cat = place.category ? place.category : "";
        e.sub = place.subcategory ? place.subcategory : "";
        e.catIndex = VsPaletteCategoryIndex(place.category);
        e.browse = place.browse && !dup;
        e.level = d.level;
        e.doc = VsNodeDocFor(d.type);
        e.rank = api ? 2 : 0;
        if (const char* what = VsNodeHelp(d.type)) e.helpLine = what;
        else if (api && !api->doc.empty()) e.helpLine = api->doc;
        else if (d.title && d.title[0]) e.helpLine = std::string(d.title) + ".";
        if (const char* ex = VsNodeExample(d.type)) e.example = ex;
        e.keywords = VsNodeSearchText(d.type);
        if (api) { if (!e.keywords.empty()) e.keywords += ' '; e.keywords += "engine api"; }
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
        if (api) e.desc = api->doc + " In a script: " + api->code + ". ";
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
                if (VsPaletteStepIsDuplicate(si.label)) continue;   // the hand-written node is the one row
                const bool sameAsNode = false;
                Entry e; e.def = sd; e.step = &si; e.cond = cond; e.title = si.label;
                e.cat = place.category ? place.category : "";
                e.sub = place.subcategory ? place.subcategory : "";
                e.catIndex = VsPaletteCategoryIndex(place.category);
                e.browse = place.browse && !sameAsNode;
                e.level = VsLevelForStep(si.op.c_str(), si.group.c_str());
                e.helpLine = si.desc;
                e.rank = 1;
                e.key = std::string(cond ? "c:" : "a:") + si.op; e.desc = si.desc;
                e.keywords = si.group + (cond ? " check something" : " do something");
                e.col = bright(catColor(e.catIndex));
                e.titleLower = lower(e.title);
                all.push_back(std::move(e));
            }
    }
    // Favourites and recently added, remembered between sessions.
    static std::vector<std::string> s_fav, s_recent;
    static std::size_t s_listVer = 0;   // bumped when favourites or recents change (the cached rows depend on them)
    static int s_filter = 0;   // VsLevelFilter: 0 all, 1 basic, 2 intermediate, 3 advanced
    static bool s_loaded = false;
    auto saveLists = [&] {
        std::ofstream f("okay_vsnodes.txt");
        f << "lvl " << s_filter << "\n";
        for (const auto& k : s_fav) f << "fav " << k << "\n";
        for (const auto& k : s_recent) f << "rec " << k << "\n";
    };
    if (!s_loaded) {
        s_loaded = true;
        std::ifstream f("okay_vsnodes.txt"); std::string kind, k;
        while (f >> kind >> k) {
            if (kind == "lvl") s_filter = std::clamp(std::atoi(k.c_str()), 0, 3);
            else (kind == "fav" ? s_fav : s_recent).push_back(k);
        }
    }
    const VsLevelFilter levelFilter = (VsLevelFilter)s_filter;
    auto passesLevel = [&](VsLevel level) { return VsLevelPasses(level, levelFilter); };
    auto isFav = [&](const std::string& k) { return std::find(s_fav.begin(), s_fav.end(), k) != s_fav.end(); };
    auto add = [&](const Entry& e) {
        if (e.step) addNode(*e.def, okay::ActionList::EncodeItem({e.step->op, {}}));
        else addNode(*e.def, std::string());
        s_recent.erase(std::remove(s_recent.begin(), s_recent.end(), e.key), s_recent.end());
        s_recent.insert(s_recent.begin(), e.key);
        if (s_recent.size() > 12) s_recent.resize(12);
        ++s_listVer;
        saveLists();
        ImGui::CloseCurrentPopup();
    };

    // Sidebar: -3 All, -2 Recent, -1 Favourites, otherwise a topic index. s_sub picks a subcategory.
    static int s_side = -3;
    static int s_sub = -1;
    static std::string s_subName;
    static int s_kb = 0;
    static char search[64] = "";
    if (ImGui::IsWindowAppearing()) {
        search[0] = '\0'; s_kb = 0; ImGui::SetKeyboardFocusHere();
        // A wire dropped on empty space: start on the whole list, compatible nodes first.
        if (st.linkPending) { s_side = -3; s_sub = -1; s_subName.clear(); }
    }
    if (st.linkPending && st.linkExec)
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "Suggested first: nodes that fit this flow pin");
    else if (st.linkPending)
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "Suggested first: nodes that fit a %s pin", okay::vs::PinTypeName(dragged));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##vssearch", "Search nodes and actions...  (Up / Down, Enter to add)", search, sizeof search))
        s_kb = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    const float filterW = (ImGui::GetContentRegionAvail().x - 18.0f) / 4.0f;
    for (int fi = 0; fi < 4; ++fi) {
        if (fi) ImGui::SameLine();
        const bool on = s_filter == fi;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 110, 160, 255));
        if (ImGui::Button(VsLevelFilterName((VsLevelFilter)fi), ImVec2(filterW, 0))) {
            s_filter = fi; s_kb = 0; saveLists();
        }
        if (on) ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
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

    // ---- Sidebar ----
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
    ImGui::BeginChild("##vspside", ImVec2(214, -4), ImGuiChildFlags_Borders);
    auto sideItem = [&](int id, const char* label, ImU32 dot, int count, bool selected, int indent) {
        ImGui::PushID(id + 10);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (indent) ImGui::Indent(12.0f);
        if (ImGui::Selectable("##side", selected)) { s_kb = 0; }
        ImDrawList* sdl = ImGui::GetWindowDrawList();
        const float h = ImGui::GetTextLineHeight();
        if (dot) sdl->AddCircleFilled(ImVec2(p.x + 6, p.y + h * 0.5f + 1), 3.5f, dot);
        const float right = p.x + ImGui::GetContentRegionAvail().x;
        char n[16] = ""; if (count >= 0) std::snprintf(n, sizeof n, "%d", count);
        const float nw = ImGui::CalcTextSize(n).x;
        sdl->PushClipRect(ImVec2(p.x, p.y), ImVec2(right - nw - 6, p.y + h + 4), true);   // long names stop before the count
        sdl->AddText(ImVec2(p.x + 16 + (indent ? 14.0f : 0.0f), p.y), ImGui::GetColorU32(ImGuiCol_Text), label);
        sdl->PopClipRect();
        if (count >= 0) sdl->AddText(ImVec2(right - nw, p.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), n);
        if (ImGui::IsItemHovered()) {
            const char* blurb = (id >= 0 && id < VsPaletteCategoryCount()) ? kPaletteCategories[id].blurb
                              : id == -4 ? "The 26 nodes a first script is built from."
                              : id == -2 ? "Nodes you have added lately."
                              : nullptr;
            if (blurb && *blurb) {
                ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
                ImGui::TextUnformatted(label); ImGui::TextDisabled("%s", blurb);
                ImGui::PopTextWrapPos(); ImGui::EndTooltip();
            } else if (ImGui::CalcTextSize(label).x + 16 > right - nw - 6 - p.x) ImGui::SetTooltip("%s", label);
        }
        const bool clicked = ImGui::IsItemClicked();
        if (indent) ImGui::Unindent(12.0f);
        ImGui::PopID();
        return clicked;
    };
    // The counts, the subcategories and the "fits the wire" flags only change when one of these does, so they are
    // worked out then and not on every frame the pop-up is open.
    static std::vector<int> s_catCount;
    static int s_browseAll = 0, s_starterN = 0;
    static std::vector<std::pair<std::string, int>> s_subs;
    {
        using SideKey = std::tuple<std::size_t, bool, bool, bool, int, long long, long long, int, int>;
        static SideKey s_sideKey{};
        static bool s_sideInit = false;
        const SideKey key{s_gen, st.linkPending, st.linkExec, st.linkFromOutput, (int)dragged,
                          (long long)st.linkNode, (long long)st.linkPin, s_filter, s_side};
        if (!s_sideInit || !(key == s_sideKey)) {
            s_sideKey = key; s_sideInit = true;
            for (auto& e : all) e.fits = !st.linkPending || !e.def || fits(*e.def);
            s_catCount.assign((std::size_t)std::max(1, VsPaletteCategoryCount()), 0);
            s_browseAll = 0; s_starterN = 0; s_subs.clear();
            for (const auto& e : all) {
                if (!e.fits || !e.browse || !passesLevel(e.level)) continue;
                ++s_browseAll;
                if (e.def && VsIsStarter(e.def->type)) ++s_starterN;
                if (e.catIndex >= 0 && e.catIndex < (int)s_catCount.size()) ++s_catCount[(std::size_t)e.catIndex];
                if (s_side >= 0 && e.catIndex == s_side && !e.sub.empty()) {
                    bool found = false;
                    for (auto& sb : s_subs) if (sb.first == e.sub) { ++sb.second; found = true; break; }
                    if (!found) s_subs.push_back({e.sub, 1});
                }
            }
            std::stable_sort(s_subs.begin(), s_subs.end(), [](const auto& a, const auto& b) {
                const int ao = VsSubOrder(a.first.c_str()), bo = VsSubOrder(b.first.c_str());
                if (ao != bo) return ao < bo;
                return a.first < b.first;
            });
        }
    }
    const int browseAll = s_browseAll, starterN = s_starterN;
    if (sideItem(-4, "Starter", 0, starterN, s_side == -4, 0)) { s_side = -4; s_sub = -1; s_subName.clear(); }
    if (sideItem(-2, "Recently used", 0, -1, s_side == -2, 0)) { s_side = -2; s_sub = -1; s_subName.clear(); }
    if (sideItem(-3, "All", 0, browseAll, s_side == -3, 0)) { s_side = -3; s_sub = -1; s_subName.clear(); }
    if (sideItem(-1, "Favourites", 0, (int)s_fav.size(), s_side == -1, 0)) { s_side = -1; s_sub = -1; s_subName.clear(); }
    ImGui::Separator();
    for (int c = 0; c < VsPaletteCategoryCount(); ++c) {
        const int n = c < (int)s_catCount.size() ? s_catCount[(std::size_t)c] : 0;
        if (!n) continue;
        if (sideItem(c, kPaletteCategories[c].name, bright(catColor(c)), n, s_side == c && s_sub < 0, 0)) {
            s_side = c; s_sub = -1; s_subName.clear();
        }
        if (s_side != c) continue;   // expanded: the topic you are in shows its subcategories
        if (s_subs.size() < 2) continue;
        for (int i = 0; i < (int)s_subs.size(); ++i) {
            if (sideItem(1000 + c * 40 + i, s_subs[i].first.c_str(), 0, s_subs[i].second, s_side == c && s_sub == i, 1)) {
                s_side = c; s_sub = i; s_subName = s_subs[i].first;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::SameLine();

    // ---- Results ----
    // The rows (which entries, in which order, under which headers) only change when the search text, the topic, the
    // level, the wire being dragged, the favourites/recents or the list itself change, so they are worked out then and
    // kept. Only the rows on screen are drawn each frame (a clipper), so an open pop-up costs about the same however
    // many nodes there are.
    struct Row { const Entry* e; int sc; };
    struct Disp { int row; const char* header; };     // one list line: a row, or a topic header (row = -1)
    static std::vector<Row> s_rows;
    static std::vector<Disp> s_disp;
    static std::vector<int> s_rowDisp;                 // row index -> line in s_disp
    {
        using RowKey = std::tuple<std::size_t, std::size_t, bool, bool, bool, int, long long, long long, int, int, int, std::string, std::string>;
        static RowKey s_rowKey{};
        static bool s_rowInit = false;
        const RowKey key{s_gen, s_listVer, st.linkPending, st.linkExec, st.linkFromOutput, (int)dragged,
                         (long long)st.linkNode, (long long)st.linkPin, s_filter, s_side, s_sub, s_subName, needle};
        if (!s_rowInit || !(key == s_rowKey)) {
            s_rowKey = key; s_rowInit = true;
            s_rows.clear(); s_disp.clear(); s_rowDisp.clear();
            auto recentRank = [&](const std::string& k) {
                const auto it = std::find(s_recent.begin(), s_recent.end(), k);
                return it == s_recent.end() ? 10000 : (int)(it - s_recent.begin());
            };
            std::vector<int> hits;
            for (const auto& e : all) {
                if (!e.fits || !passesLevel(e.level)) continue;
                if (needle.empty() && !e.browse) continue;
                // Typing searches every topic, so a name is found wherever it lives.
                // Only the level filter still applies.
                if (needle.empty()) {
                    if (s_side == -4 && !(e.def && VsIsStarter(e.def->type))) continue;
                    if (s_side >= 0 && e.catIndex != s_side) continue;
                    if (s_side >= 0 && s_sub >= 0 && e.sub != s_subName) continue;
                    if (s_side == -1 && !isFav(e.key)) continue;
                    if (s_side == -2 && std::find(s_recent.begin(), s_recent.end(), e.key) == s_recent.end()) continue;
                }
                const int sc = score(e, hits);
                if (sc > 0) s_rows.push_back(Row{&e, sc});
            }
            if (!needle.empty())
                std::stable_sort(s_rows.begin(), s_rows.end(), [&](const Row& a, const Row& b) {
                    return VsSearchRanksBefore(a.sc, a.e->level, recentRank(a.e->key),
                                               b.sc, b.e->level, recentRank(b.e->key));
                });
            else if (s_side == -2)
                std::stable_sort(s_rows.begin(), s_rows.end(), [&](const Row& a, const Row& b) {
                    return std::find(s_recent.begin(), s_recent.end(), a.e->key) < std::find(s_recent.begin(), s_recent.end(), b.e->key);
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
            // Topic headers while browsing (not while searching). Inside a topic, the header is the subcategory.
            const bool headers = needle.empty() && !st.linkPending && s_side != -1 && s_side != -2 && s_side != -4 && s_sub < 0;
            const char* lastHeader = nullptr;
            for (int i = 0; i < (int)s_rows.size(); ++i) {
                if (headers) {
                    const std::string& h = s_side < 0 ? s_rows[i].e->cat : s_rows[i].e->sub;
                    if (!h.empty() && (!lastHeader || h != lastHeader)) { lastHeader = h.c_str(); s_disp.push_back(Disp{-1, lastHeader}); }
                }
                s_rowDisp.push_back((int)s_disp.size());
                s_disp.push_back(Disp{i, nullptr});
            }
        }
    }
    const std::vector<Row>& rows = s_rows;
    const bool kbMoved = ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) s_kb = std::min(s_kb + 1, (int)rows.size() - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   s_kb = std::max(s_kb - 1, 0);
    if (s_kb >= (int)rows.size()) s_kb = std::max(0, (int)rows.size() - 1);

    const float detailsW = 360.0f;
    const Entry* hovered = nullptr;
    const Entry* toAdd = nullptr;
    ImGui::BeginChild("##vspres", ImVec2(-detailsW - 12.0f, -4.0f), ImGuiChildFlags_Borders);
    if (rows.empty()) {
        ImGui::TextDisabled(s_side == -1 ? "No favourites yet: click the star on a node to add one."
                          : s_side == -2 ? "Nodes you add show up here."
                          : s_side == -4 ? "Nothing at this level in the starter set."
                          : needle.empty() ? "Nothing here at this level."
                              : s_filter != 0 ? "No node matches that at this level. Try All." : "No node matches that.");
    } else if (st.linkPending && needle.empty()) {
        ImGui::TextDisabled("Suggested for this pin");
    } else if (s_side == -4 && needle.empty()) {
        ImGui::TextDisabled("Start here");
    }
    const float lh = ImGui::GetTextLineHeight();
    const float lineH = lh + 14;                                   // every line (row or header) is this tall, which the clipper needs
    ImGuiListClipper clip;
    clip.Begin((int)s_disp.size(), lineH + ImGui::GetStyle().ItemSpacing.y);
    if (kbMoved && s_kb >= 0 && s_kb < (int)s_rowDisp.size()) clip.IncludeItemByIndex(s_rowDisp[(std::size_t)s_kb]);
    std::vector<int> hitsNow;
    while (clip.Step())
    for (int di = clip.DisplayStart; di < clip.DisplayEnd; ++di) {
        if (s_disp[(std::size_t)di].row < 0) {
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(1.0f, lineH));
            ImGui::GetWindowDrawList()->AddText(ImVec2(hp.x + 4.0f, hp.y + lineH - lh - 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), s_disp[(std::size_t)di].header);
            continue;
        }
        const int i = s_disp[(std::size_t)di].row;
        const Entry& e = *rows[i].e;
        score(e, hitsNow);
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool kbSel = i == s_kb;
        if (ImGui::Selectable("##row", kbSel, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, lh + 14))) toAdd = &e;
        if (ImGui::IsItemHovered()) {
            hovered = &e;
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(e.helpLine.empty() ? "No description." : e.helpLine.c_str());
            if (!e.example.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("Example: %s", e.example.c_str());
            }
            ImGui::TextDisabled("%s", VsLevelName(e.level));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        if (kbSel && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))) ImGui::SetScrollHereY(0.5f);
        ImDrawList* rdl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        rdl->AddCircleFilled(ImVec2(p.x + 8, p.y + (lh + 14) * 0.5f), 5.0f, e.col);
        // Title, matched letters in the accent colour.
        float x = p.x + 18;
        const ImU32 normal = ImGui::GetColorU32(ImGuiCol_Text), hit = IM_COL32(110, 180, 255, 255);
        for (int c = 0; c < (int)e.title.size(); ++c) {
            const char buf[2] = {e.title[c], 0};
            const bool h = std::find(hitsNow.begin(), hitsNow.end(), c) != hitsNow.end();
            rdl->AddText(ImVec2(x, p.y + 2), h ? hit : normal, buf);
            x += ImGui::CalcTextSize(buf).x;
        }
        // Level badge: Basic, then Inter, then Adv. Colour says which, the word says it too.
        {
            const char* badge = VsLevelBadge(e.level);
            const ImVec2 bs = ImGui::CalcTextSize(badge);
            const float bw = bs.x + 8.0f, bh = lh + 2.0f;
            if (x + 8.0f + bw < p.x + w - ImGui::CalcTextSize(e.cat.c_str()).x - 36.0f) {
                const ImVec2 b0(x + 8.0f, p.y + 4.0f), b1(b0.x + bw, b0.y + bh);
                const ImU32 bg = e.level == VsLevel::Basic ? IM_COL32(46, 110, 78, 255)
                               : e.level == VsLevel::Advanced ? IM_COL32(96, 72, 120, 255)
                               : IM_COL32(52, 78, 120, 255);
                rdl->AddRectFilled(b0, b1, bg, 3.0f);
                rdl->AddText(ImVec2(b0.x + 4.0f, b0.y + 1.0f), IM_COL32(230, 236, 244, 255), badge);
            }
        }
        // Category on the right, then the favourite star.
        const float catW = ImGui::CalcTextSize(e.cat.c_str()).x;
        if (x + 20 < p.x + w - catW - 32)
            rdl->AddText(ImVec2(p.x + w - catW - 32, p.y + 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), e.cat.c_str());
        ImGui::SameLine(w - 18);
        const bool fav = isFav(e.key);
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##fav", ImVec2(22, lh + 14))) {
            if (fav) s_fav.erase(std::remove(s_fav.begin(), s_fav.end(), e.key), s_fav.end());
            else s_fav.push_back(e.key);
            ++s_listVer;
            saveLists();
        }
        const bool sh = ImGui::IsItemHovered();
        if (sh) ImGui::SetTooltip(fav ? "Remove from favourites" : "Add to favourites");
        {   // a drawn star (the UI font has no star glyph)
            const ImVec2 c(sp.x + 10, sp.y + (lh + 14) * 0.5f);
            ImVec2 pts[10];
            for (int k = 0; k < 10; ++k) {
                const float a = -1.5707963f + k * 0.6283185f, r = (k % 2) ? 2.6f : 6.0f;
                pts[k] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
            }
            const ImU32 sc = fav ? IM_COL32(255, 204, 64, 255) : sh ? IM_COL32(200, 200, 210, 200) : IM_COL32(120, 124, 134, 120);
            if (fav) rdl->AddConcavePolyFilled(pts, 10, sc); else rdl->AddPolyline(pts, 10, sc, ImDrawFlags_Closed, 1.2f);
        }
        ImGui::PopID();
    }
    if (enter && !rows.empty()) toAdd = rows[std::clamp(s_kb, 0, (int)rows.size() - 1)].e;
    ImGui::EndChild();

    // ---- Details: the hovered (or keyboard) node ----
    const Entry* info = hovered ? hovered : (!rows.empty() ? rows[std::clamp(s_kb, 0, (int)rows.size() - 1)].e : nullptr);
    ImGui::SameLine();
    ImGui::BeginChild("##vspinfo", ImVec2(0, -4.0f), ImGuiChildFlags_Borders);
    if (info) {
        ImGui::TextUnformatted(info->title.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s · %s", VsLevelName(info->level), info->cat.c_str());
        ImGui::Spacing();
        if (info->doc) VsDrawDocBody(*info->doc, false, nullptr, false);
        else {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", info->desc.empty() ? "No description." : info->desc.c_str());
            ImGui::PopTextWrapPos();
        }
    } else {
        ImGui::TextWrapped("Search, or pick a topic. This pane explains the highlighted node. Use the level buttons to see only the easier nodes.");
    }
    ImGui::EndChild();
    if (toAdd) add(*toAdd);
    ImGui::PopStyleVar(3);

}

}  // namespace okay::editor::vsed
