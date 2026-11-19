#include "UIDesignerWindow.hpp"
#include "UIDesignerModel.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIUserStyle.hpp"
#include "okay/Components/UIGuides.hpp"
#include "PrefabEdit.hpp"
#include <filesystem>
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
Scene* g_scene = nullptr;           // the scene the guides belong to (for keeping them)
ImVec2 g_panelPos{0, 0}, g_panelEnd{0, 0};
uid::Rect g_area; int g_areaFrame = -10;   // the free area inside the rulers, from the last frame they were drawn
GameObject* g_renaming = nullptr;
char g_nameBuf[128];

UIGuides* FindGuides(Scene& sc) {
    for (const auto& up : sc.Objects()) if (up) if (auto* g = up->GetComponent<UIGuides>()) return g;
    return nullptr;
}
void WriteBackGuides() {   // the saved copy follows the session guides, when the scene keeps them
    if (!g_scene) return;
    if (UIGuides* sg = FindGuides(*g_scene)) {
        sg->vertical.clear(); sg->horizontal.clear();
        for (const auto& g : g_guides) (g.vertical ? sg->vertical : sg->horizontal).push_back(g.pos);
    }
}
bool InRect(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.y >= a.y && p.x <= b.x && p.y <= b.y; }
bool IsUI(GameObject* g) { return g && GetUIRect(g).valid; }
bool HasUI(GameObject* g) {
    if (IsUI(g)) return true;
    if (g && g->transform) for (Transform* c : g->transform->Children()) if (c && HasUI(c->gameObject)) return true;
    return false;
}
}  // namespace

void UIDesignerSyncScene(Scene& scene) {
    if (g_scene == &scene) return;
    g_scene = &scene; g_guides.clear(); g_dragGuide = -1; g_locked.clear();
    if (UIGuides* sg = FindGuides(scene)) {
        for (float x : sg->vertical) g_guides.push_back({true, x});
        for (float y : sg->horizontal) g_guides.push_back({false, y});
    }
}

bool UIDesignerLocked(const GameObject* g) { return g && g_locked.count(g) > 0; }

bool UIDesignerRulerInput(ImVec2 pp, ImVec2 pe, ImVec2 vp, float zoom, bool hovered, const ImGuiIO& io, bool* fit) {
    if (zoom < 1e-3f) zoom = 1.0f;
    const ImVec2 m = io.MousePos;
    const uid::RulerLayout L = uid::MakeRulerLayout(pp.x, pp.y, pe.x - pp.x, pe.y - pp.y);
    g_area = L.content; g_areaFrame = ImGui::GetFrameCount(); g_panelPos = pp; g_panelEnd = pe;
    if (g_dragGuide >= 0) {   // a guide is being dragged
        if (g_dragGuide >= (int)g_guides.size()) { g_dragGuide = -1; return false; }
        uid::Guide& g = g_guides[g_dragGuide];
        g.pos = g.vertical ? (m.x - vp.x) / zoom : (m.y - vp.y) / zoom;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const bool onRuler = g.vertical ? m.x < L.content.x : m.y < L.content.y;
            if (onRuler || !InRect(m, pp, pe)) g_guides.erase(g_guides.begin() + g_dragGuide);   // dragged back: remove it
            g_dragGuide = -1; WriteBackGuides();
        }
        return true;
    }
    if (!hovered || !InRect(m, pp, pe)) return false;
    const bool inChrome = L.Contains(L.bar, m.x, m.y) || L.Contains(L.topRuler, m.x, m.y) || L.Contains(L.leftRuler, m.x, m.y) || L.Contains(L.corner, m.x, m.y);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        switch (uid::RulerPress(L, m.x, m.y)) {   // tested in test_ui_designer: the buttons win over the rulers
            case uid::RulerHit::ToggleLayers: g_showLayers = !g_showLayers; return true;
            case uid::RulerHit::Fit: if (fit) *fit = true; return true;
            case uid::RulerHit::NewGuideFromTop: g_guides.push_back({false, (m.y - vp.y) / zoom}); g_dragGuide = (int)g_guides.size() - 1; return true;
            case uid::RulerHit::NewGuideFromLeft: g_guides.push_back({true, (m.x - vp.x) / zoom}); g_dragGuide = (int)g_guides.size() - 1; return true;
            case uid::RulerHit::None: break;
        }
        for (int i = 0; i < (int)g_guides.size(); ++i) {   // grab an existing guide
            const float d = g_guides[i].vertical ? std::fabs(m.x - (vp.x + g_guides[i].pos * zoom))
                                                 : std::fabs(m.y - (vp.y + g_guides[i].pos * zoom));
            if (d <= 4.0f) { g_dragGuide = i; return true; }
        }
    }
    return inChrome;   // the bar and rulers never pass a press to the widgets under them
}

void UIDesignerDrawRulers(ImDrawList* dl, ImVec2 pp, ImVec2 pe, ImVec2 vp, float zoom) {
    if (zoom < 1e-3f) zoom = 1.0f;
    const uid::RulerLayout L = uid::MakeRulerLayout(pp.x, pp.y, pe.x - pp.x, pe.y - pp.y);
    const ImU32 bg = IM_COL32(34, 36, 44, 245), fg = IM_COL32(190, 196, 210, 255), tick = IM_COL32(120, 128, 148, 255);
    const ImU32 guideCol = IM_COL32(40, 200, 220, 230), btn = IM_COL32(52, 56, 68, 255), btnOn = IM_COL32(70, 130, 220, 255);
    auto A = [](const uid::Rect& r) { return ImVec2(r.x, r.y); };
    auto B = [](const uid::Rect& r) { return ImVec2(r.R(), r.B()); };
    for (const auto& g : g_guides) {   // guides, kept out of the bar and rulers
        if (g.vertical) { const float x = vp.x + g.pos * zoom; if (x > L.content.x) dl->AddLine({x, L.content.y}, {x, pe.y}, guideCol, 1.0f); }
        else            { const float y = vp.y + g.pos * zoom; if (y > L.content.y) dl->AddLine({L.content.x, y}, {pe.x, y}, guideCol, 1.0f); }
    }
    dl->AddRectFilled(A(L.bar), B(L.bar), bg);
    dl->AddRectFilled(A(L.topRuler), B(L.topRuler), bg);
    dl->AddRectFilled(A(L.leftRuler), B(L.leftRuler), bg);
    dl->AddRectFilled(A(L.corner), B(L.corner), bg);
    dl->PushClipRect(A(L.topRuler), B(L.topRuler), true);
    for (const auto& t : uid::RulerTicks((L.topRuler.x - vp.x) / zoom, (pe.x - vp.x) / zoom, zoom)) {
        const float x = vp.x + t.pos * zoom; if (x < L.content.x) continue;
        dl->AddLine({x, L.topRuler.y + (t.major ? 3.0f : 10.0f)}, {x, L.topRuler.B()}, tick);
        if (t.major) { char b[24]; std::snprintf(b, sizeof(b), "%g", t.pos); dl->AddText({x + 2, L.topRuler.y}, fg, b); }
    }
    dl->PopClipRect();
    dl->PushClipRect(A(L.leftRuler), B(L.leftRuler), true);
    for (const auto& t : uid::RulerTicks((L.content.y - vp.y) / zoom, (pe.y - vp.y) / zoom, zoom)) {
        const float y = vp.y + t.pos * zoom; if (y < L.content.y) continue;
        dl->AddLine({L.leftRuler.x + (t.major ? 3.0f : 10.0f), y}, {L.leftRuler.R(), y}, tick);
        if (t.major) { char b[24]; std::snprintf(b, sizeof(b), "%g", t.pos); dl->AddText({L.leftRuler.x + 1, y + 1}, fg, b); }
    }
    dl->PopClipRect();
    dl->AddRectFilled(A(L.layersBtn), B(L.layersBtn), g_showLayers ? btnOn : btn, 3.0f); dl->AddText({L.layersBtn.x + 6, L.layersBtn.y}, fg, "L");
    dl->AddRectFilled(A(L.fitBtn), B(L.fitBtn), btn, 3.0f); dl->AddText({L.fitBtn.x + 7, L.fitBtn.y}, fg, "Fit");
    char z[24]; std::snprintf(z, sizeof(z), "%d%%", (int)(zoom * 100.0f + 0.5f));
    dl->AddText({L.zoomSlot.x, L.zoomSlot.y + 1}, fg, z);
    const ImVec2 mp = ImGui::GetIO().MousePos;
    if (L.Contains(L.layersBtn, mp.x, mp.y)) ImGui::SetTooltip("Layers, styles and tools (Ctrl+P: every action)");
    else if (L.Contains(L.fitBtn, mp.x, mp.y)) ImGui::SetTooltip("Fit: show the whole screen at 100%%");
}

bool UIDesignerRulersShown(ImVec2 panelPos) {
    return g_areaFrame >= ImGui::GetFrameCount() - 1 && g_panelPos.x == panelPos.x && g_panelPos.y == panelPos.y;
}
ImVec2 UIDesignerClampPoint(ImVec2 p) {
    if (g_areaFrame < ImGui::GetFrameCount() - 1) return p;
    float x = p.x, y = p.y; uid::ClampPoint(g_area, x, y); return {x, y};
}
ImVec2 UIDesignerClampLabel(ImVec2 pos, ImVec2 size) {
    if (g_areaFrame < ImGui::GetFrameCount() - 1) return pos;
    const uid::Rect b = uid::ClampBox(g_area, {pos.x, pos.y, size.x, size.y});
    return {b.x, b.y};
}

void UIDesignerGuideCandidates(std::vector<float>& xs, std::vector<float>& ys, float zoom) {
    for (const auto& g : g_guides) (g.vertical ? xs : ys).push_back(g.pos * zoom);
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

// Reusable UI: the engine's prefab system, shown as plain words. Save a group once, add copies, change a few things on
// one copy (an override), save that as a variant, or push a change to every copy.
void DrawReusable(EditorState& ed) {
    GameObject* go = ed.selected();
    if (!go) { ImGui::TextDisabled("Select a widget or group first."); return; }
    auto say = [&](const oked::PrefabOpResult& r) { (void)r; ed.dirty = true; };
    if (go->prefabSource.empty()) {
        if (ImGui::Button("Save as reusable UI")) {
            std::string p = go->name + ".okayprefab";
            if (!ed.projectDir().empty()) {
                std::error_code pe;
                std::filesystem::create_directories(std::filesystem::path(ed.projectDir()) / "Assets" / "UI", pe);
                p = (std::filesystem::path(ed.projectDir()) / "Assets" / "UI" / p).string();
            }
            if (oked::SaveAsPrefabFile(*go, p)) { ed.PushUndo("Save reusable UI"); go->prefabSource = p; ed.dirty = true; }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keeps this group in Assets/UI. Drag the file into the scene for more copies.");
        return;
    }
    ImGui::TextWrapped("Copy of: %s", std::filesystem::path(go->prefabSource).filename().string().c_str());
    if (ImGui::Button("Save my changes as a variant")) say(oked::PrefabCreateVariant(ed, *go, oked::DefaultVariantPath(go->prefabSource)));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new reusable UI that is this one plus the few things you changed here.");
    if (ImGui::Button("Update the original from this copy")) say(oked::PrefabApplyAll(ed, *go));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every copy of the original follows, keeping its own changes.");
    if (ImGui::Button("Undo my changes on this copy")) say(oked::PrefabRevertAll(ed, *go));
    if (ImGui::Button("Make it plain objects again")) say(oked::PrefabUnpack(ed, *go, false));
}
}  // namespace

void UIDesignerLayersWindow(EditorState& ed) {
    if (!g_showLayers) return;
    {   // first open: docked down the right side of the view, tall, so it never covers the ruler bar or the top-left
        const float w = 280.0f, h = std::max(300.0f, g_panelEnd.y - g_panelPos.y - 60.0f);
        ImGui::SetNextWindowPos({g_panelEnd.x - w - 10.0f, g_panelPos.y + 40.0f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({w, h}, ImGuiCond_FirstUseEver);
    }
    if (ImGui::Begin("UI Layers", &g_showLayers)) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Front first. Drag a row to reorder; drop on the middle to put it inside.");
        ImGui::PopTextWrapPos();
        std::vector<GameObject*> roots;
        for (const auto& up : ed.scene().Objects()) if (up && up->transform && !up->transform->Parent()) roots.push_back(up.get());
        for (int i = (int)roots.size() - 1; i >= 0; --i) DrawTree(roots[i], ed, 0);
        ImGui::Separator();
        if (ImGui::Button("Clear guides")) { g_guides.clear(); WriteBackGuides(); ed.dirty = true; }
        {
            UIGuides* kept = FindGuides(ed.scene());
            bool keep = kept != nullptr;
            if (ImGui::Checkbox("Keep guides with the scene", &keep)) {
                ed.PushUndo(keep ? "Keep guides" : "Forget guides");
                if (keep) { auto* g = ed.scene().CreateGameObject("UI Guides")->AddComponent<UIGuides>(); (void)g; WriteBackGuides(); }
                else if (kept) ed.scene().Destroy(kept->gameObject);
                ed.dirty = true;
            }
        }
        if (ImGui::CollapsingHeader("Shared styles")) DrawStyles(ed);
        if (ImGui::CollapsingHeader("Reusable UI")) DrawReusable(ed);
    }
    ImGui::End();
}

}  // namespace oked
