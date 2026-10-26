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

namespace {
// The size of the rect a widget is placed in: its UI parent's, else the canvas.
Vec2 FrameParentSize(GameObject* go) {
    if (GameObject* p = OwningUIParent(go)) return UIResolveSize(p, UICanvas::Width(), UICanvas::Height());
    return Vec2{UICanvas::Width(), UICanvas::Height()};
}

// Switch a frame to preset `a` without moving the widget: solve position / size so
// the resolved rect stays where it was.
void ApplyPresetKeepRect(UIFrame& f, UIAnchor a, const Vec2& parent) {
    Vec2 o, sz;
    f.Resolve(f.position, f.size, parent.x, parent.y, o, sz);
    f.ApplyPreset(a);
    auto solve = [](float a0, float a1, float pv, float extent, float o, float s, float& pos, float& size) {
        if (a0 == a1) { size = s; pos = o - extent * a0 + s * pv; }
        else          { pos = o - extent * a0; size = extent * (a1 - a0) - pos - s; }
    };
    solve(f.anchorMin.x, f.anchorMax.x, f.pivot.x, parent.x, o.x, sz.x, f.position.x, f.size.x);
    solve(f.anchorMin.y, f.anchorMax.y, f.pivot.y, parent.y, o.y, sz.y, f.position.y, f.size.y);
}
} // namespace

void DrawUIFrameConvertButtons(GameObject* go, EditorState& ed) {
    if (!go || go->GetComponent<UIFrame>()) return;
    UIRect r = GetUIRect(go);
    if (!r.valid || !r.sizePtr) return;
    if (ImGui::SmallButton("Use UI Frame")) {
        ed.PushUndo();
        if (ConvertToUIFrame(go)) ed.dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Place this widget with a UI Frame (free anchors and pivot). It stays exactly where it is.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Convert All UI")) {
        ed.PushUndo();   // the whole scene converts as one undo step
        if (ConvertSceneToUIFrames(ed.scene()) > 0) ed.dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Give every widget in the scene a UI Frame. Nothing moves; Undo reverts all of them.");
}

void DrawUILayoutComponentInspector(Component* comp, EditorState& ed, Component** toRemove,
                                    const UIInspectorHooks& hooks) {
    if (auto* fr = dynamic_cast<UIFrame*>(comp)) {
        if (hooks.header("UI Frame", fr, toRemove, true)) {
            static const char* kPresets[] = {"Top-Left", "Top", "Top-Right", "Left", "Center", "Right",
                                             "Bottom-Left", "Bottom", "Bottom-Right",
                                             "Stretch Horizontal", "Stretch Vertical", "Stretch (Fill)"};
            int pr = (int)fr->Preset();
            if (ImGui::Combo("Preset##ufr", &pr, kPresets, 12)) {
                ed.PushUndo();
                ApplyPresetKeepRect(*fr, (UIAnchor)pr, FrameParentSize(fr->gameObject));
                ed.dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set anchors and pivot to a preset; the widget stays where it is.");
            float amin[2] = {fr->anchorMin.x, fr->anchorMin.y}, amax[2] = {fr->anchorMax.x, fr->anchorMax.y};
            if (ImGui::DragFloat2("Anchor Min##ufr", amin, 0.005f, 0.0f, 1.0f)) { fr->anchorMin = {amin[0], amin[1]}; ed.dirty = true; }
            if (ImGui::DragFloat2("Anchor Max##ufr", amax, 0.005f, 0.0f, 1.0f)) { fr->anchorMax = {amax[0], amax[1]}; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fractions of the parent rect. Different min and max on an axis stretch the widget between them.");
            float pv[2] = {fr->pivot.x, fr->pivot.y};
            if (ImGui::DragFloat2("Pivot##ufr", pv, 0.005f, 0.0f, 1.0f)) { fr->pivot = {pv[0], pv[1]}; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The point of the widget its position places (0,0 = top-left, 0.5,0.5 = center).");
            float pos[2] = {fr->position.x, fr->position.y}, sz[2] = {fr->size.x, fr->size.y};
            const bool sx = fr->StretchesX(), sy = fr->StretchesY();
            if (ImGui::DragFloat2(sx || sy ? "Position / Start Margin##ufr" : "Position##ufr", pos, 1.0f)) { fr->position = {pos[0], pos[1]}; ed.dirty = true; }
            if (ImGui::DragFloat2(sx || sy ? "Size / End Margin##ufr" : "Size##ufr", sz, 1.0f)) { fr->size = {sz[0], sz[1]}; ed.dirty = true; }
            if ((sx || sy) && ImGui::IsItemHovered())
                ImGui::SetTooltip("On a stretched axis these are the margins from the two anchors.");
        }
        return;
    }
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
