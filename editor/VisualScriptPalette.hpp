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
#include <string>
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

inline void VsNodeTooltip(const char* title, const VsNodeDoc* doc) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextUnformatted(title ? title : (doc ? doc->title.c_str() : ""));
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

inline void VsPinTooltip(const VsPinDoc* pin, const char* name, const char* typeName) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
    ImGui::Text("%s   %s", (name && *name) ? name : "flow", typeName ? typeName : (pin ? pin->type.c_str() : ""));
    if (pin) {
        ImGui::TextWrapped("%s", pin->meaning.c_str());
        if (!pin->fallback.empty()) ImGui::TextDisabled("Default: %s", pin->fallback.c_str());
    }
    ImGui::TextDisabled("Drag to wire. Alt+click breaks wires.");
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
    std::vector<Entry> all;
    for (const auto& d : Registry()) {
        if (!fits(d)) continue;
        const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
        VsPlace place = api ? VsPlaceForApi(api->apiType.c_str(), api->member.c_str()) : VsPlaceForNode(d.type);
        bool dup = false;
        if (api) {
            for (const auto& h : Registry()) {
                if (std::strchr(h.type, '.')) continue;
                if (VsApiDuplicatesHandNode(api->member.c_str(), h.type)) { dup = true; break; }
            }
        }
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
        e.keywords = api ? "engine api" : "";
        if (d.cat >= 0 && d.cat < CAT_COUNT) {
            static const char* oldCat[] = {"Events","Flow","Variables","Math","Logic","Input","Transform","Actions","Data","Multiplayer & Steam","Engine API"};
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
        all.push_back(std::move(e));
    }
    for (int pass = 0; pass < 2; ++pass) {
        const bool cond = pass == 1;
        const VsDef* sd = FindDef(cond ? "Condition" : "Action");
        if (!StepCatalog || !sd || !fits(*sd)) continue;
        if (const auto* cat = StepCatalog(cond))
            for (const auto& si : *cat) {
                if (si.listOnly) continue;
                VsPlace place = VsPlaceForStep(si.op.c_str(), si.group.c_str());
                bool sameAsNode = false;
                for (const auto& h : Registry()) if (h.title && si.label == h.title) { sameAsNode = true; break; }
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
                all.push_back(std::move(e));
            }
    }
    // Favourites and recently added, remembered between sessions.
    static std::vector<std::string> s_fav, s_recent;
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
        const std::string t = lower(e.title);
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
        std::string pins;
        if (e.def) {
            for (int i = 0; i < (int)e.def->dataIn.size(); ++i)
                pins += std::string(okay::vs::PinTypeName(okay::vs::InputType(e.def->type, i))) + " ";
            for (int i = 0; i < (int)e.def->dataOut.size(); ++i)
                pins += std::string(okay::vs::PinTypeName(okay::vs::OutputType(e.def->type, i, ""))) + " ";
        }
        const std::string extra = lower(e.cat + " " + e.sub + " " + e.keywords + " " +
                                        (e.def ? e.def->type : "") + " " +
                                        (e.def ? VsSearchKeywords(e.def->type) : std::string()) + " " +
                                        (e.step ? e.step->op : "") + " " + e.desc + " " + pins +
                                        (e.doc ? " " + VsSearchBlob(*e.doc) : std::string()));
        return extra.find(needle) != std::string::npos ? 40 : 0;
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
    auto browseCount = [&](int cat, const std::string& sub, bool anySub) {
        int n = 0;
        for (const auto& e : all) {
            if (!e.browse || !passesLevel(e.level) || e.catIndex != cat) continue;
            if (!anySub && e.sub != sub) continue;
            ++n;
        }
        return n;
    };
    int browseAll = 0, starterN = 0;
    for (const auto& e : all) if (e.browse && passesLevel(e.level)) {
        ++browseAll;
        if (e.def && VsIsStarter(e.def->type)) ++starterN;
    }
    if (sideItem(-4, "Starter", 0, starterN, s_side == -4, 0)) { s_side = -4; s_sub = -1; s_subName.clear(); }
    if (sideItem(-2, "Recently used", 0, -1, s_side == -2, 0)) { s_side = -2; s_sub = -1; s_subName.clear(); }
    if (sideItem(-3, "All", 0, browseAll, s_side == -3, 0)) { s_side = -3; s_sub = -1; s_subName.clear(); }
    if (sideItem(-1, "Favourites", 0, (int)s_fav.size(), s_side == -1, 0)) { s_side = -1; s_sub = -1; s_subName.clear(); }
    ImGui::Separator();
    for (int c = 0; c < VsPaletteCategoryCount(); ++c) {
        const int n = browseCount(c, "", true);
        if (!n) continue;
        if (sideItem(c, kPaletteCategories[c].name, bright(catColor(c)), n, s_side == c && s_sub < 0, 0)) {
            s_side = c; s_sub = -1; s_subName.clear();
        }
        if (s_side != c) continue;   // expanded: the topic you are in shows its subcategories
        std::vector<std::pair<std::string, int>> subs;
        for (const auto& e : all) {
            if (!e.browse || !passesLevel(e.level) || e.catIndex != c || e.sub.empty()) continue;
            bool found = false;
            for (auto& s : subs) if (s.first == e.sub) { ++s.second; found = true; break; }
            if (!found) subs.push_back({e.sub, 1});
        }
        if (subs.size() < 2) continue;
        std::stable_sort(subs.begin(), subs.end(), [](const auto& a, const auto& b) {
            const int ao = VsSubOrder(a.first.c_str()), bo = VsSubOrder(b.first.c_str());
            if (ao != bo) return ao < bo;
            return a.first < b.first;
        });
        for (int i = 0; i < (int)subs.size(); ++i) {
            if (sideItem(1000 + c * 40 + i, subs[i].first.c_str(), 0, subs[i].second, s_side == c && s_sub == i, 1)) {
                s_side = c; s_sub = i; s_subName = subs[i].first;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::SameLine();

    // ---- Results ----
    struct Row { const Entry* e; int sc; std::vector<int> hits; };
    std::vector<Row> rows;
    auto recentRank = [&](const std::string& key) {
        const auto it = std::find(s_recent.begin(), s_recent.end(), key);
        return it == s_recent.end() ? 10000 : (int)(it - s_recent.begin());
    };
    for (const auto& e : all) {
        if (!passesLevel(e.level)) continue;
        if (needle.empty() && !e.browse) continue;
        if (s_side == -4 && !(e.def && VsIsStarter(e.def->type))) continue;
        if (s_side >= 0 && e.catIndex != s_side) continue;
        if (s_side >= 0 && s_sub >= 0 && e.sub != s_subName) continue;
        if (s_side == -1 && !isFav(e.key)) continue;
        if (s_side == -2 && std::find(s_recent.begin(), s_recent.end(), e.key) == s_recent.end()) continue;
        Row r{&e, 0, {}};
        r.sc = score(e, r.hits);
        if (r.sc > 0) rows.push_back(std::move(r));
    }
    if (!needle.empty())
        std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            return VsSearchRanksBefore(a.sc, a.e->level, recentRank(a.e->key),
                                       b.sc, b.e->level, recentRank(b.e->key));
        });
    else if (s_side == -2)
        std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            return std::find(s_recent.begin(), s_recent.end(), a.e->key) < std::find(s_recent.begin(), s_recent.end(), b.e->key);
        });
    else if (s_side == -4)
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            return VsStarterIndex(a.e->def ? a.e->def->type : "") < VsStarterIndex(b.e->def ? b.e->def->type : "");
        });
    else if (st.linkPending)
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.e->level != b.e->level) return (int)a.e->level < (int)b.e->level;
            if (a.e->catIndex != b.e->catIndex) return a.e->catIndex < b.e->catIndex;
            return a.e->rank < b.e->rank;
        });
    else
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.e->catIndex != b.e->catIndex) return a.e->catIndex < b.e->catIndex;
            const int ao = VsSubOrder(a.e->sub.c_str()), bo = VsSubOrder(b.e->sub.c_str());
            if (ao != bo) return ao < bo;
            if (a.e->sub != b.e->sub) return a.e->sub < b.e->sub;
            if (a.e->level != b.e->level) return (int)a.e->level < (int)b.e->level;
            return a.e->rank < b.e->rank;
        });
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
                          : needle.empty() ? "Nothing here at this level." : "No node matches that.");
    } else if (st.linkPending && needle.empty()) {
        ImGui::TextDisabled("Suggested for this pin");
    } else if (s_side == -4 && needle.empty()) {
        ImGui::TextDisabled("Start here");
    }
    const float lh = ImGui::GetTextLineHeight();
    std::string lastHeader;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const Entry& e = *rows[i].e;
        // Topic headers while browsing (not while searching). Inside a topic, the header is the subcategory.
        if (needle.empty() && !st.linkPending && s_side != -1 && s_side != -2 && s_side != -4 && s_sub < 0) {
            const std::string header = s_side < 0 ? e.cat : e.sub;
            if (!header.empty() && header != lastHeader) {
                lastHeader = header;
                if (i) ImGui::Spacing();
                ImGui::TextDisabled("%s", header.c_str());
            }
        }
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
            const bool h = std::find(rows[i].hits.begin(), rows[i].hits.end(), c) != rows[i].hits.end();
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
