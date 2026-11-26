#pragma once
// Draws the start screen (the big recipe cards) for an empty graph. The rules (which cards, when it shows, the
// layout) are in VisualScriptStart.hpp and tested; this file only paints them with ImGui and handles the clicks.
// Included from VisualScriptEditor.hpp after the editor state exists.
#include "VisualScriptStart.hpp"
#include "EditorTabs.hpp"   // okay::edtabs::UserFile: the per-user folder
#include "VisualScriptUsagePrefs.hpp"

namespace okay::editor::vsed {

/// What is remembered about this person (loaded once from okay_vs_usage.txt, saved when it changes).
inline VsUsagePrefs& VsPrefs() { static VsUsagePrefs p = VsUsagePrefsLoad(okay::edtabs::UserFile("okay_vs_usage.txt")); return p; }   // the per-user folder
inline void VsPrefsSave() { VsUsagePrefsSave(VsPrefs(), okay::edtabs::UserFile("okay_vs_usage.txt")); }

/// Put recipe `index` on the canvas, wired, selected and framed, and give the selected object the parts it needs.
/// One undo step (the canvas records a graph change once).
inline void VsStartInsertRecipe(EditorState& ed, VsEditState& st, int index) {
    const auto& all = VsRecipes();
    if (index < 0 || index >= (int)all.size()) return;
    const VsRecipe& r = all[(std::size_t)index];
    const std::set<int> made = VsInsertRecipe(st.g, r.graph);
    st.sel = made;
    st.selected = made.empty() ? -1 : *made.begin();
    st.frameSel = !made.empty();
    if (GameObject* go = ed.selected()) VsAddTemplateComponents(go, r.add);
}

inline void VsDrawStartScreen(EditorState& ed, VsEditState& st, ImVec2 cmin, ImVec2 cmax) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = cmax.x - cmin.x, H = cmax.y - cmin.y;
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    auto centred = [&](const char* t, float size, float y, ImU32 col) {
        const ImVec2 ts = font->CalcTextSizeA(size, 1.0e9f, 0.0f, t);
        dl->AddText(font, size, ImVec2(cmin.x + (W - ts.x) * 0.5f, cmin.y + y), col, t);
    };
    centred("What do you want to make?", fs * 1.7f, 22.0f, IM_COL32(238, 242, 248, 255));
    centred("Pick one. It appears already connected, and you can change it.", fs * 1.05f, 22.0f + fs * 2.3f, IM_COL32(168, 180, 198, 255));

    const std::vector<VsStartCard> cards = VsStartCards();
    const std::vector<VsStartRect> rects = VsStartLayout((int)cards.size(), W, H);
    int picked = -1;
    for (std::size_t i = 0; i < cards.size() && i < rects.size(); ++i) {
        const VsStartRect& q = rects[i];
        const ImVec2 p0(cmin.x + q.x, cmin.y + q.y), p1(p0.x + q.w, p0.y + q.h);
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID((int)i);
        const bool clicked = ImGui::InvisibleButton("##startcard", ImVec2(q.w, q.h));
        const bool hot = ImGui::IsItemHovered();
        ImGui::PopID();
        dl->AddRectFilled(p0, p1, hot ? IM_COL32(48, 58, 78, 255) : IM_COL32(34, 39, 52, 250), 12.0f);
        dl->AddRect(p0, p1, hot ? IM_COL32(130, 175, 245, 255) : IM_COL32(86, 98, 124, 200), 12.0f, 0, hot ? 2.5f : 1.5f);
        // A small sketch: one coloured block per step, joined by a line.
        const int blocks = std::min(5, std::max(2, VsGraphNodeCount(VsRecipes()[(std::size_t)cards[i].recipe].graph)));
        const float bw = 22.0f, bh = 12.0f, gap = 10.0f;
        float bx = p0.x + 14.0f;
        const float by = p0.y + 14.0f;
        for (int b = 0; b < blocks; ++b) {
            if (b) dl->AddLine(ImVec2(bx - gap, by + bh * 0.5f), ImVec2(bx, by + bh * 0.5f), IM_COL32(220, 224, 232, 230), 2.0f);
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), b == 0 ? IM_COL32(236, 120, 96, 255) : IM_COL32(96, 160, 236, 255), 3.0f);
            bx += bw + gap;
        }
        const float titleSize = fs * 1.1f;
        const ImVec2 tsz = font->CalcTextSizeA(titleSize, 1.0e9f, q.w - 28.0f, cards[i].title.c_str());   // wraps: a long title takes two lines
        dl->PushClipRect(p0, p1, true);
        dl->AddText(font, titleSize, ImVec2(p0.x + 14.0f, p0.y + 36.0f), IM_COL32(244, 247, 252, 255), cards[i].title.c_str(), nullptr, q.w - 28.0f);
        dl->AddText(font, fs, ImVec2(p0.x + 14.0f, p0.y + 36.0f + tsz.y + 6.0f), IM_COL32(176, 188, 206, 255),
                    cards[i].line.c_str(), nullptr, q.w - 28.0f);
        dl->PopClipRect();
        if (clicked) picked = cards[i].recipe;
    }
    // Below the cards: start empty, and never show this again.
    float by = rects.empty() ? 140.0f : rects.back().y + rects.back().h + 22.0f;
    by = std::min(by, H - 52.0f);
    const float bw = 190.0f;
    ImGui::SetCursorScreenPos(ImVec2(cmin.x + W * 0.5f - bw - 12.0f, cmin.y + by));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 9.0f));
    if (ImGui::Button("Start empty", ImVec2(bw, 0.0f))) st.startedEmpty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide these cards for this graph and start with a blank canvas.\nThe Pick a recipe button in the toolbar brings them back.");
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(cmin.x + W * 0.5f + 12.0f, cmin.y + by + 8.0f));
    bool never = VsPrefs().hideStart;
    if (ImGui::Checkbox("Do not show this again", &never)) { VsPrefs().hideStart = never; VsPrefsSave(); }
    if (picked >= 0) VsStartInsertRecipe(ed, st, picked);
}

} // namespace okay::editor::vsed
