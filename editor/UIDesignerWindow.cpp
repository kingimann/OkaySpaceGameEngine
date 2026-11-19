#include "UIDesignerWindow.hpp"
#include "UIDesignerModel.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIUserStyle.hpp"
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_set>

using namespace okay;
using okay::editor::EditorState;

namespace oked {
namespace {
constexpr float kRuler = 16.0f;
std::vector<uid::Guide> g_guides;
int g_dragGuide = -1;              // index being dragged, -1 none
bool g_showLayers = false;
std::unordered_set<const GameObject*> g_locked;
GameObject* g_renaming = nullptr;
char g_nameBuf[128];

bool InRect(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.y >= a.y && p.x <= b.x && p.y <= b.y; }
bool IsUI(GameObject* g) { return g && GetUIRect(g).valid; }
bool HasUI(GameObject* g) {
    if (IsUI(g)) return true;
    if (g && g->transform) for (Transform* c : g->transform->Children()) if (c && HasUI(c->gameObject)) return true;
    return false;
}
}  // namespace

bool UIDesignerLocked(const GameObject* g) { return g && g_locked.count(g) > 0; }

void UIDesignerGuideCandidates(std::vector<float>& xs, std::vector<float>& ys, float zoom) {
    for (const auto& g : g_guides) (g.vertical ? xs : ys).push_back(g.pos * zoom);
}

bool UIDesignerRulerInput(ImVec2 pp, ImVec2 pe, ImVec2 vp, float zoom, bool hovered, const ImGuiIO& io) {
    if (zoom < 1e-3f) zoom = 1.0f;
    const ImVec2 m = io.MousePos;
    if (g_dragGuide >= 0) {   // a guide is being dragged
        if (g_dragGuide >= (int)g_guides.size()) { g_dragGuide = -1; return false; }
        uid::Guide& g = g_guides[g_dragGuide];
        g.pos = g.vertical ? (m.x - vp.x) / zoom : (m.y - vp.y) / zoom;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const bool onRuler = g.vertical ? m.x < pp.x + kRuler : m.y < pp.y + kRuler;
            if (onRuler || !InRect(m, pp, pe)) g_guides.erase(g_guides.begin() + g_dragGuide);   // dragged back: remove it
            g_dragGuide = -1;
        }
        return true;
    }
    if (!hovered || !InRect(m, pp, pe)) return false;
    const bool top = m.y < pp.y + kRuler, left = m.x < pp.x + kRuler;
    if (top && left) return true;   // the corner buttons
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (top)  { g_guides.push_back({false, (m.y - vp.y) / zoom}); g_dragGuide = (int)g_guides.size() - 1; return true; }
        if (left) { g_guides.push_back({true,  (m.x - vp.x) / zoom}); g_dragGuide = (int)g_guides.size() - 1; return true; }
        for (int i = 0; i < (int)g_guides.size(); ++i) {   // grab an existing guide
            const float d = g_guides[i].vertical ? std::fabs(m.x - (vp.x + g_guides[i].pos * zoom))
                                                 : std::fabs(m.y - (vp.y + g_guides[i].pos * zoom));
            if (d <= 4.0f) { g_dragGuide = i; return true; }
        }
    }
    return false;
}

void UIDesignerDrawRulers(ImDrawList* dl, ImVec2 pp, ImVec2 pe, ImVec2 vp, float zoom, bool* fit) {
    if (zoom < 1e-3f) zoom = 1.0f;
    const ImU32 bg = IM_COL32(34, 36, 44, 235), fg = IM_COL32(190, 196, 210, 255), tick = IM_COL32(120, 128, 148, 255);
    const ImU32 guideCol = IM_COL32(40, 200, 220, 230);
    // guides first, so the strips sit over their ends
    for (const auto& g : g_guides) {
        if (g.vertical) { const float x = vp.x + g.pos * zoom; dl->AddLine({x, pp.y}, {x, pe.y}, guideCol, 1.0f); }
        else            { const float y = vp.y + g.pos * zoom; dl->AddLine({pp.x, y}, {pe.x, y}, guideCol, 1.0f); }
    }
    dl->AddRectFilled(pp, {pe.x, pp.y + kRuler}, bg);
    dl->AddRectFilled(pp, {pp.x + kRuler, pe.y}, bg);
    for (const auto& t : uid::RulerTicks((pp.x - vp.x) / zoom, (pe.x - vp.x) / zoom, zoom)) {
        const float x = vp.x + t.pos * zoom; if (x < pp.x + kRuler) continue;
        dl->AddLine({x, pp.y + (t.major ? 3.0f : 10.0f)}, {x, pp.y + kRuler}, tick);
        if (t.major) { char b[24]; std::snprintf(b, sizeof(b), "%g", t.pos); dl->AddText({x + 2, pp.y}, fg, b); }
    }
    for (const auto& t : uid::RulerTicks((pp.y - vp.y) / zoom, (pe.y - vp.y) / zoom, zoom)) {
        const float y = vp.y + t.pos * zoom; if (y < pp.y + kRuler) continue;
        dl->AddLine({pp.x + (t.major ? 3.0f : 10.0f), y}, {pp.x + kRuler, y}, tick);
        if (t.major) { char b[24]; std::snprintf(b, sizeof(b), "%g", t.pos); dl->AddText({pp.x + 1, y + 1}, fg, b); }
    }
    // corner: toggles the Layers panel; a small Fit button just right of it on the top ruler
    const ImGuiIO& io = ImGui::GetIO();
    dl->AddRectFilled(pp, {pp.x + kRuler, pp.y + kRuler}, g_showLayers ? IM_COL32(70, 130, 220, 255) : IM_COL32(52, 56, 68, 255));
    dl->AddText({pp.x + 4, pp.y}, fg, "L");
    const ImVec2 fa{pp.x + kRuler + 2, pp.y}, fb{pp.x + kRuler + 34, pp.y + kRuler};
    dl->AddRectFilled(fa, fb, IM_COL32(52, 56, 68, 255)); dl->AddText({fa.x + 5, fa.y}, fg, "Fit");
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g_dragGuide < 0) {
        if (InRect(io.MousePos, pp, {pp.x + kRuler, pp.y + kRuler})) g_showLayers = !g_showLayers;
        else if (fit && InRect(io.MousePos, fa, fb)) *fit = true;
    }
    if (InRect(io.MousePos, pp, {pp.x + kRuler, pp.y + kRuler})) ImGui::SetTooltip("Layers panel");
    char z[24]; std::snprintf(z, sizeof(z), "%d%%", (int)(zoom * 100.0f + 0.5f));
    dl->AddText({fb.x + 6, pp.y}, fg, z);
}

namespace {
bool IsDescendant(Transform* maybe, Transform* of) {
    for (Transform* t = maybe; t; t = t->Parent()) if (t == of) return true;
    return false;
}

void DrawRow(GameObject* g, EditorState& ed, int depth) {
    ImGui::PushID(g);
    ImGui::Indent(depth * 14.0f);
    bool on = g->active;
    if (ImGui::Checkbox("##vis", &on)) { ed.PushUndo(on ? "Show UI widget" : "Hide UI widget"); g->active = on; ed.dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show or hide");
    ImGui::SameLine();
    bool lk = g_locked.count(g) > 0;
    if (ImGui::Checkbox("##lock", &lk)) { if (lk) g_locked.insert(g); else g_locked.erase(g); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock: stops clicks on the canvas from picking it");
    ImGui::SameLine();
    if (g_renaming == g) {
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rn", g_nameBuf, sizeof(g_nameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            if (g_nameBuf[0] && g->name != g_nameBuf) { ed.PushUndo("Rename UI widget"); g->name = g_nameBuf; ed.dirty = true; }
            g_renaming = nullptr;
        } else if (ImGui::IsItemDeactivated()) g_renaming = nullptr;
    } else {
        const bool sel = ed.IsSelected(g);
        if (ImGui::Selectable(g->name.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { g_renaming = g; std::snprintf(g_nameBuf, sizeof(g_nameBuf), "%s", g->name.c_str()); }
            else if (ImGui::GetIO().KeyCtrl) ed.ToggleSelect(g);
            else ed.Select(g);
        }
        if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("OKAY_UI_LAYER", &g, sizeof(g)); ImGui::TextUnformatted(g->name.c_str()); ImGui::EndDragDropSource(); }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("OKAY_UI_LAYER")) {
                GameObject* src = *(GameObject**)p->Data;
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                const float t = (ImGui::GetIO().MousePos.y - a.y) / std::max(1.0f, b.y - a.y);   // top = in front, bottom = behind, middle = inside
                if (src && src != g && src->transform && g->transform && !IsDescendant(g->transform, src->transform)) {
                    ed.PushUndo("Reorder UI layers");
                    if (t > 0.25f && t < 0.75f) src->transform->SetParent(g->transform, true);
                    else {
                        Transform* par = g->transform->Parent();
                        const int idx = g->transform->GetSiblingIndex() + (t <= 0.25f ? 1 : 0);   // later sibling = drawn in front
                        src->transform->SetParent(par, true, idx);
                    }
                    ed.dirty = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::Unindent(depth * 14.0f);
    ImGui::PopID();
}

void DrawTree(GameObject* g, EditorState& ed, int depth) {
    if (!g || !HasUI(g)) return;
    DrawRow(g, ed, depth);
    if (!g->transform) return;
    const auto kids = g->transform->Children();
    for (int i = (int)kids.size() - 1; i >= 0; --i) if (kids[i]) DrawTree(kids[i]->gameObject, ed, depth + 1);   // front first
}

// Shared styles: named looks. Editing one restyles every widget that uses it (ApplyUserStyles), one undo step per edit.
void DrawStyles(EditorState& ed) {
    ImGui::TextDisabled("Name a look once, use it on many widgets.");
    std::vector<UIUserStyle*> all;
    for (const auto& up : ed.scene().Objects()) if (up) for (auto* u : up->GetComponents<UIUserStyle>()) all.push_back(u);
    auto commit = [&](const char* what) { ed.PushUndo(what); };
    for (UIUserStyle* u : all) {
        ImGui::PushID(u);
        if (ImGui::TreeNode(u->styleName.empty() ? "(unnamed style)" : u->styleName.c_str())) {
            bool ch = false;
            char nb[96]; std::snprintf(nb, sizeof(nb), "%s", u->styleName.c_str());
            if (ImGui::InputText("Name", nb, sizeof(nb), ImGuiInputTextFlags_EnterReturnsTrue)) { commit("Rename style"); u->styleName = nb; ch = true; }
            auto col = [&](const char* l, Color& c, bool* on) {
                float v[4] = {c.r, c.g, c.b, c.a};
                if (on) { ImGui::Checkbox((std::string("##on") + l).c_str(), on); ImGui::SameLine(); }
                if (ImGui::ColorEdit4(l, v)) { if (ImGui::IsItemActivated()) {} c = Color(v[0], v[1], v[2], v[3]); ch = true; }
                if (ImGui::IsItemActivated()) commit("Edit style colour");
            };
            col("Fill", u->fill, &u->applyFill); col("Text colour", u->textColor, &u->applyText); col("Border colour", u->borderColor, &u->applyBorder);
            if (ImGui::DragFloat("Border width", &u->borderWidth, 0.1f, 0.0f, 20.0f)) ch = true;
            if (ImGui::IsItemActivated()) commit("Edit style");
            if (ImGui::DragFloat("Corner roundness", &u->cornerRadius, 0.2f, -1.0f, 200.0f, u->cornerRadius < 0 ? "leave as is" : "%.1f")) ch = true;
            if (ImGui::IsItemActivated()) commit("Edit style");
            if (ImGui::DragFloat("Text size", &u->textSize, 0.005f, 0.0f, 2.0f, u->textSize <= 0 ? "leave as is" : "%.3f")) ch = true;
            if (ImGui::IsItemActivated()) commit("Edit style");
            if (ch) { ApplyUserStyles(ed.scene()); ed.dirty = true; }
            if (ImGui::Button("Use on selected")) {
                int n = 0; commit("Use shared style");
                for (GameObject* g : ed.MultiSelection()) if (g) { auto* v = g->GetComponent<UIStyleVariant>(); if (!v) v = g->AddComponent<UIStyleVariant>(); v->userStyle = u->styleName; ++n; }
                if (n) { ApplyUserStyles(ed.scene()); ed.dirty = true; }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ImGui::Button("New style")) {
        ed.PushUndo("New shared style");
        GameObject* g = ed.scene().CreateGameObject("Style");
        auto* u = g->AddComponent<UIUserStyle>();
        std::vector<std::string> taken; for (auto* o : all) taken.push_back(o->styleName);
        u->styleName = uid::UniqueName(taken, "New style"); g->name = "Style: " + u->styleName; ed.dirty = true;
    }
}
}  // namespace

void UIDesignerLayersWindow(EditorState& ed) {
    if (!g_showLayers) return;
    ImGui::SetNextWindowSize({260, 340}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("UI Layers", &g_showLayers)) {
        ImGui::TextDisabled("Front first. Drag a row to reorder; drop on the middle to put it inside.");
        std::vector<GameObject*> roots;
        for (const auto& up : ed.scene().Objects()) if (up && up->transform && !up->transform->Parent()) roots.push_back(up.get());
        for (int i = (int)roots.size() - 1; i >= 0; --i) DrawTree(roots[i], ed, 0);
        ImGui::Separator();
        if (ImGui::Button("Clear guides")) g_guides.clear();
        if (ImGui::CollapsingHeader("Shared styles")) DrawStyles(ed);
    }
    ImGui::End();
}

}  // namespace oked
