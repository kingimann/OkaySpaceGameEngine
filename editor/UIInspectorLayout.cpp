#include "UIInspectorLayout.hpp"
#include "imgui.h"
#include <cstring>
#include <string>

using namespace okay;
using okay::editor::EditorState;

namespace oked {

void DrawLayoutGroupFlexFields(UILayoutGroup& lg, EditorState& ed) {
    if (!lg.IsFlex()) return;
    float box[2] = {lg.boxSize.x, lg.boxSize.y};
    if (ImGui::DragFloat2("Box Size##lgf", box, 1.0f, 1.0f, 8000.0f)) { lg.boxSize = {box[0], box[1]}; ed.dirty = true; }
    const char* justify[] = {"Start", "Center", "End", "Space Between", "Space Around", "Space Evenly"};
    int j = (int)lg.justify;
    if (ImGui::Combo("Justify##lgf", &j, justify, 6)) { lg.justify = (UILayoutGroup::Justify)j; ed.dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where spare room along a line goes.");
    const char* align[] = {"Start", "Center", "End", "Stretch"};
    int a = (int)lg.align;
    if (ImGui::Combo("Align##lgf", &a, align, 4)) { lg.align = (UILayoutGroup::Align)a; ed.dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How children sit across a line. Stretch resizes children that have a UI Flex Item.");
    if (ImGui::Checkbox("Wrap##lgf", &lg.wrap)) ed.dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start a new line when the box is full.");
    ImGui::TextDisabled("Children with a UI Flex Item grow and shrink to fill the box.");
}

void DrawScrollViewAxisFields(UIScrollView& sv, EditorState& ed) {
    const char* axes[] = {"Vertical", "Horizontal", "Both"};
    int ax = (int)sv.axis;
    if (ImGui::Combo("Scroll Axis##usv", &ax, axes, 3)) { sv.axis = (UIScrollView::Axis)ax; sv.SetScroll(sv.scroll); sv.SetScrollX(sv.scrollX); ed.dirty = true; }
    if (sv.ScrollsX()) {
        ImGui::BeginDisabled(sv.autoContent);
        if (ImGui::DragFloat("Content Width##usv", &sv.contentWidth, 1.0f, 0.0f, 100000.0f)) ed.dirty = true;
        ImGui::EndDisabled();
        if (ImGui::SliderFloat("Scroll X##usv", &sv.scrollX, 0.0f, sv.ScrollMaxX())) ed.dirty = true;
    }
}

void DrawUILayoutComponentInspector(Component* comp, EditorState& ed, Component** toRemove,
                                    const UIInspectorHooks& hooks) {
    if (auto* fi = dynamic_cast<UIFlexItem*>(comp)) {
        if (hooks.header("UI Flex Item", fi, toRemove, true)) {
            if (ImGui::DragFloat("Grow##ufi", &fi->grow, 0.05f, 0.0f, 100.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Share of the line's spare room this widget takes (0 = keep its size).");
            if (ImGui::DragFloat("Shrink##ufi", &fi->shrink, 0.05f, 0.0f, 100.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Share of an overflow it gives up (0 = never shrinks).");
            float bs[2] = {fi->baseSize.x, fi->baseSize.y};
            if (ImGui::DragFloat2("Base Size##ufi", bs, 1.0f, 0.0f, 8000.0f)) { fi->baseSize = {bs[0], bs[1]}; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Size before growing, shrinking or stretching (0 = its size when first laid out).");
            const char* align[] = {"Auto (layout's)", "Start", "Center", "End", "Stretch"};
            int a = (int)fi->alignSelf;
            if (ImGui::Combo("Align Self##ufi", &a, align, 5)) { fi->alignSelf = (UIFlexItem::Align)a; ed.dirty = true; }
            ImGui::TextDisabled("Used when the parent is a Layout Group in Flex Row / Flex Column mode.");
        }
        return;
    }
    if (auto* mk = dynamic_cast<UIMask*>(comp)) {
        if (hooks.header("UI Mask", mk, toRemove, true)) {
            if (ImGui::Checkbox("Show Graphic##umk", &mk->showGraphic)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draw this object's own widget. Off = an invisible clipping area.");
            ImGui::TextDisabled("Children are clipped to this object's widget rect.");
        }
        return;
    }
    if (auto* lv = dynamic_cast<UIListView*>(comp)) {
        if (hooks.header("UI List View", lv, toRemove, true)) {
            float pos[2] = {lv->position.x, lv->position.y};
            if (ImGui::DragFloat2("Pos (px)##ulv", pos, 1.0f)) { lv->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {lv->size.x, lv->size.y};
            if (ImGui::DragFloat2("Size (px)##ulv", sz, 1.0f, 16.0f, 8000.0f)) { lv->size = {sz[0], sz[1]}; ed.dirty = true; }
            if (ImGui::DragFloat("Row Height##ulv", &lv->itemHeight, 0.5f, 4.0f, 1000.0f)) { lv->Refresh(); ed.dirty = true; }
            if (ImGui::DragFloat("Spacing##ulv", &lv->spacing, 0.5f, 0.0f, 200.0f)) { lv->Refresh(); ed.dirty = true; }
            ImGui::BeginDisabled(!lv->items.empty());
            if (ImGui::DragInt("Item Count##ulv", &lv->itemCount, 1.0f, 0, 1000000)) { lv->Refresh(); ed.dirty = true; }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Entries when no text items are listed (scripts bind each row in on_list_bind).");
            // Text items: one per line.
            std::string joined;
            for (std::size_t i = 0; i < lv->items.size(); ++i) { if (i) joined += '\n'; joined += lv->items[i]; }
            static char buf[8192];
            std::strncpy(buf, joined.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputTextMultiline("Items##ulv", buf, sizeof(buf), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f))) {
                lv->items.clear();
                std::string cur;
                for (const char* c = buf; ; ++c) {
                    if (*c == '\n' || *c == '\0') { if (!cur.empty() || *c == '\n') lv->items.push_back(cur); cur.clear(); if (!*c) break; }
                    else cur += *c;
                }
                lv->Refresh();
                ed.dirty = true;
            }
            if (ImGui::DragInt("Selected##ulv", &lv->selected, 0.2f, -1, lv->Count() - 1)) ed.dirty = true;
            float bg[4] = {lv->background.r, lv->background.g, lv->background.b, lv->background.a};
            if (ImGui::ColorEdit4("Background##ulv", bg)) { lv->background = {bg[0], bg[1], bg[2], bg[3]}; ed.dirty = true; }
            hooks.anchorCombo("Anchor##ulv", lv->anchor, ed);
            if (!lv->Template())
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Add a child widget: it becomes the row template.");
            else {
                ImGui::TextDisabled("Row template: %s (%d pooled rows)", lv->Template()->name.c_str(), (int)lv->Rows().size());
                if (ImGui::SmallButton("Rebuild Rows##ulv")) lv->Rebuild();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clone the template again after editing it.");
            }
        }
    }
}

void BuildUIFlexBox(GameObject* g) {
    auto* lg = g->AddComponent<UILayoutGroup>();
    lg->direction = UILayoutGroup::Direction::FlexRow;
    lg->origin = {0.0f, 0.0f};
}

void BuildUIMaskedPanel(GameObject* g) {
    g->AddComponent<UIPanel>();
    g->AddComponent<UIMask>();
}

void BuildUIListView(GameObject* g) {
    auto* lv = g->AddComponent<UIListView>();
    lv->items = {"First entry", "Second entry", "Third entry", "Fourth entry", "Fifth entry",
                 "Sixth entry", "Seventh entry", "Eighth entry", "Ninth entry", "Tenth entry"};
    if (Scene* s = g->scene()) {
        GameObject* row = s->CreateGameObject("Row");
        row->transform->SetParent(g->transform, false);
        auto* b = row->AddComponent<UIButton>();
        b->size = {lv->size.x, lv->itemHeight};
        b->label = "Row";
        row->active = false;   // the template stays hidden; the list shows clones
    }
}

} // namespace oked
