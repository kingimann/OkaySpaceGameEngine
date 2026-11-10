#pragma once
// The add-node panel: search, collapsible categories, favourites, and a details
// pane. Shared by the right-click menu, a wire dropped in empty space, and Add node.
#include "VisualScriptNodeDocs.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
inline void DrawNodePalette(VsEditState& st, const VsEditGraph& g,
                            const std::function<void(const VsDef&, const std::string&)>& addNode) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    static const char* catNames[CAT_COUNT] = {
        "Events", "Flow", "Variables", "Math", "Logic", "Input", "Transform", "Actions", "Data", "Multiplayer", "Engine API"};
    okay::vs::PinType dragged = okay::vs::PinType::Any;
    if (st.linkPending && !st.linkExec)
        dragged = st.linkFromOutput ? VsOutPinType(g, st.linkNode, st.linkPin)
                                     : VsInPinType(g, st.linkNode, st.linkPin);
    auto fits = [&](const VsDef& d) {
        return !st.linkPending || VsCompatible(d, st.linkExec, st.linkFromOutput, dragged);
    };

    struct Entry {
        const VsDef* def = nullptr; const VsStepInfo* step = nullptr; bool cond = false;
        std::string title, key, blob; int group = 0; ImU32 col = 0; VsKindFilter kind = VsKindFilter::All;
        const VsNodeDoc* doc = nullptr;
    };
    std::vector<Entry> all;
    all.reserve(Registry().size() + 64);
    for (const auto& d : Registry()) {
        if (!fits(d)) continue;
        Entry e; e.def = &d; e.title = d.title; e.key = std::string("n:") + d.type;
        e.group = d.cat; e.col = CatColor(d.cat); e.kind = VsKindOf(d);
        e.doc = VsNodeDocFor(d.type);
        e.blob = std::string(catNames[d.cat]) + " " + d.type;
        if (e.doc) e.blob += " " + VsSearchBlob(*e.doc);
        all.push_back(std::move(e));
    }
    for (int pass = 0; pass < 2; ++pass) {
        const bool cond = pass == 1;
        const VsDef* sd = FindDef(cond ? "Condition" : "Action");
        if (!StepCatalog || !sd || !fits(*sd)) continue;
        if (const auto* cat = StepCatalog(cond))
            for (const auto& si : *cat) {
                if (si.listOnly) continue;
                Entry e; e.def = sd; e.step = &si; e.cond = cond; e.title = si.label;
                e.key = std::string(cond ? "c:" : "a:") + si.op;
                e.group = CAT_COUNT + pass; e.col = CatColor(CAT_ACTION);
                e.kind = cond ? VsKindFilter::Flow : VsKindFilter::Action;
                e.blob = std::string(cond ? "Check " : "Do ") + si.group + " " + si.op + " " + si.desc;
                all.push_back(std::move(e));
            }
    }

    static std::vector<std::string> s_fav, s_recent;
    static bool s_loaded = false;
    auto saveLists = [&] {
        std::ofstream f("okay_vsnodes.txt");
        for (const auto& k : s_fav) f << "fav " << k << "\n";
        for (const auto& k : s_recent) f << "rec " << k << "\n";
    };
    if (!s_loaded) {
        s_loaded = true;
        std::ifstream f("okay_vsnodes.txt"); std::string kind, k;
        while (f >> kind >> k) (kind == "fav" ? s_fav : s_recent).push_back(k);
    }
    auto isFav = [&](const std::string& k) { return std::find(s_fav.begin(), s_fav.end(), k) != s_fav.end(); };

    static int s_kb = 0;
    static char search[96] = "";
    static VsKindFilter s_kind = VsKindFilter::All;
    static bool s_open[CAT_COUNT + 4];
    static bool s_openInit = false;
    if (!s_openInit) { for (bool& b : s_open) b = false; s_open[CAT_EVENT] = s_open[CAT_FLOW] = s_open[CAT_ACTION] = true; s_openInit = true; }
    if (ImGui::IsWindowAppearing()) { search[0] = '\0'; s_kb = 0; s_kind = VsKindFilter::All; ImGui::SetKeyboardFocusHere(); }

    if (st.linkPending && st.linkExec)
        ImGui::TextColored(ImVec4(0.62f, 0.78f, 0.95f, 1.0f), "Showing nodes that can connect to this flow pin");
    else if (st.linkPending)
        ImGui::TextColored(ImVec4(0.62f, 0.78f, 0.95f, 1.0f), "Showing nodes that fit a %s pin", okay::vs::PinTypeName(dragged));

    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool edited = ImGui::InputTextWithHint("##vssearch", "Search by name, what it does, or a keyword", search, sizeof search);
    if (edited) s_kb = 0;
    const std::string needle = VsLower(search);

    ImGui::Spacing();
    auto chip = [&](VsKindFilter k, const char* label) {
        const bool on = s_kind == k;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.38f, 0.56f, 1.0f));
        if (ImGui::Button(label)) { s_kind = k; s_kb = 0; }
        if (on) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    chip(VsKindFilter::All, "All");
    chip(VsKindFilter::Event, "Events");
    chip(VsKindFilter::Action, "Actions");
    chip(VsKindFilter::Getter, "Getters");
    chip(VsKindFilter::Flow, "Flow");
    ImGui::NewLine();

    struct Row { const Entry* e; int sc; std::vector<int> hits; int group; bool header = false; int count = 0; };
    std::vector<Row> rows;
    auto wantKind = [&](const Entry& e) { return s_kind == VsKindFilter::All || e.kind == s_kind; };
    auto consider = [&](const Entry& e, int group) {
        if (!wantKind(e)) return;
        Row r; r.e = &e; r.group = group;
        r.sc = VsFuzzyScore(needle, e.title, e.blob, r.hits);
        if (r.sc > 0) rows.push_back(std::move(r));
    };
    // Recent and favourites first, then each category that has anything.
    if (needle.empty()) {
        for (const auto& e : all) if (std::find(s_recent.begin(), s_recent.end(), e.key) != s_recent.end()) consider(e, -2);
        for (const auto& e : all) if (isFav(e.key)) consider(e, -1);
    }
    for (int c = 0; c < CAT_COUNT + 2; ++c)
        for (const auto& e : all) if (e.group == c) consider(e, c);
    if (!needle.empty())
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.sc > b.sc; });
    else if (!rows.empty()) {
        // Keep recent order.
        std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            if (a.group != b.group) return a.group < b.group;
            if (a.group == -2) {
                auto ia = std::find(s_recent.begin(), s_recent.end(), a.e->key);
                auto ib = std::find(s_recent.begin(), s_recent.end(), b.e->key);
                return ia < ib;
            }
            return false;
        });
    }

    // Insert collapsible headers when browsing (search shows a flat list).
    std::vector<Row> shown;
    if (needle.empty()) {
        int last = -99; int run = 0;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].group != last) {
                last = rows[i].group; run = 0;
                for (std::size_t k = i; k < rows.size() && rows[k].group == last; ++k) ++run;
                Row h; h.header = true; h.group = last; h.count = run; h.e = rows[i].e;
                shown.push_back(h);
            }
            const int slot = last < 0 ? (last == -2 ? CAT_COUNT + 2 : CAT_COUNT + 3) : last;
            if (slot >= 0 && slot < (int)(sizeof s_open / sizeof s_open[0]) && s_open[slot]) shown.push_back(rows[i]);
        }
    } else shown = rows;

    // Keyboard walks selectable rows only.
    std::vector<int> selectable;
    for (int i = 0; i < (int)shown.size(); ++i) if (!shown[i].header) selectable.push_back(i);
    if (s_kb >= (int)selectable.size()) s_kb = std::max(0, (int)selectable.size() - 1);
    const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
    const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
    if (down) s_kb = std::min(s_kb + 1, (int)selectable.size() - 1);
    if (up) s_kb = std::max(s_kb - 1, 0);
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();

    const float rowH = ImGui::GetFontSize() + 18.0f;
    const Entry* hovered = nullptr;
    const Entry* toAdd = nullptr;
    const float detailsW = 380.0f;
    ImGui::BeginChild("##vspres", ImVec2(-detailsW - 12.0f, -4.0f), ImGuiChildFlags_Borders);
    if (shown.empty()) ImGui::TextDisabled(needle.empty() ? "Nothing to add." : "No node matches that.");
    int selShown = (s_kb >= 0 && s_kb < (int)selectable.size()) ? selectable[s_kb] : -1;
    for (int i = 0; i < (int)shown.size(); ++i) {
        const Row& r = shown[i];
        if (r.header) {
            const int slot = r.group < 0 ? (r.group == -2 ? CAT_COUNT + 2 : CAT_COUNT + 3) : r.group;
            const char* name = r.group == -2 ? "Recent" : r.group == -1 ? "Favourites"
                : r.group < CAT_COUNT ? catNames[r.group] : r.group == CAT_COUNT ? "Do something" : "Check something";
            ImGui::PushID(slot + 100);
            const bool open = slot >= 0 && s_open[slot];
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            if (ImGui::Selectable("##head", false, 0, ImVec2(0, rowH))) if (slot >= 0) s_open[slot] = !open;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 col = r.e ? r.e->col : IM_COL32(160, 170, 190, 255);
            dl->AddRectFilled(ImVec2(p.x, p.y + 6.0f), ImVec2(p.x + 4.0f, p.y + rowH - 6.0f), col, 2.0f);
            DrawCategoryIcon(dl, ImVec2(p.x + 22.0f, p.y + rowH * 0.5f), 9.0f, r.group < CAT_COUNT ? r.group : CAT_ACTION, col);
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s %s    %d", open ? "v" : ">", name, r.count);
            dl->AddText(ImVec2(p.x + 40.0f, p.y + (rowH - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), buf);
            (void)w;
            ImGui::PopID();
            continue;
        }
        const Entry& e = *r.e;
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool kbSel = i == selShown;
        if (ImGui::Selectable("##row", kbSel, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowH))) toAdd = &e;
        if (ImGui::IsItemHovered()) hovered = &e;
        if (kbSel && (down || up)) ImGui::SetScrollHereY(0.5f);
        ImDrawList* rdl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        rdl->AddCircleFilled(ImVec2(p.x + 14.0f, p.y + rowH * 0.5f), 6.0f, e.col);
        float x = p.x + 30.0f;
        const ImU32 normal = ImGui::GetColorU32(ImGuiCol_Text), hit = IM_COL32(130, 190, 255, 255);
        for (int c = 0; c < (int)e.title.size(); ++c) {
            const char buf[2] = {e.title[c], 0};
            const bool h = std::find(r.hits.begin(), r.hits.end(), c) != r.hits.end();
            rdl->AddText(ImVec2(x, p.y + (rowH - ImGui::GetFontSize()) * 0.5f), h ? hit : normal, buf);
            x += ImGui::CalcTextSize(buf).x;
        }
        const bool fav = isFav(e.key);
        ImGui::SameLine(w - 28.0f);
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##fav", ImVec2(26.0f, rowH))) {
            if (fav) s_fav.erase(std::remove(s_fav.begin(), s_fav.end(), e.key), s_fav.end());
            else s_fav.push_back(e.key);
            saveLists();
        }
        const bool sh = ImGui::IsItemHovered();
        if (sh) ImGui::SetTooltip(fav ? "Remove from favourites" : "Pin to favourites");
        {
            const ImVec2 c(sp.x + 12.0f, sp.y + rowH * 0.5f);
            ImVec2 pts[10];
            for (int k = 0; k < 10; ++k) {
                const float a = -1.5707963f + k * 0.6283185f, rad = (k % 2) ? 3.2f : 7.0f;
                pts[k] = ImVec2(c.x + std::cos(a) * rad, c.y + std::sin(a) * rad);
            }
            const ImU32 sc = fav ? IM_COL32(255, 204, 64, 255) : sh ? IM_COL32(200, 200, 210, 220) : IM_COL32(140, 146, 160, 140);
            if (fav) rdl->AddConcavePolyFilled(pts, 10, sc); else rdl->AddPolyline(pts, 10, sc, ImDrawFlags_Closed, 1.4f);
        }
        ImGui::PopID();
    }
    if (enter && selShown >= 0) toAdd = shown[selShown].e;
    ImGui::EndChild();
    ImGui::SameLine();

    const Entry* info = hovered ? hovered : (selShown >= 0 ? shown[selShown].e : nullptr);
    ImGui::BeginChild("##vspinfo", ImVec2(0, -4.0f), ImGuiChildFlags_Borders);
    if (info) {
        ImGui::TextUnformatted(info->title.c_str());
        ImGui::Spacing();
        if (info->doc) VsDrawDocBody(*info->doc, false, nullptr, false);
        else if (info->step) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s", info->step->desc.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::TextDisabled("%s", info->cond
                ? "A ready-made check. It needs no extra component. It runs in Play mode."
                : "A ready-made step. It needs no extra component beyond what that step names. It runs in Play mode.");
        } else ImGui::TextDisabled("No description.");
    } else {
        ImGui::TextWrapped("Search, or open a category. The pane on this side explains the highlighted node.");
    }
    ImGui::EndChild();

    if (toAdd && toAdd->def) {
        std::string arg;
        if (toAdd->step) arg = okay::ActionList::EncodeItem({toAdd->step->op, {}});
        s_recent.erase(std::remove(s_recent.begin(), s_recent.end(), toAdd->key), s_recent.end());
        s_recent.insert(s_recent.begin(), toAdd->key);
        if (s_recent.size() > 12) s_recent.resize(12);
        saveLists();
        addNode(*toAdd->def, arg);
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleVar(3);
}

}  // namespace okay::editor::vsed
