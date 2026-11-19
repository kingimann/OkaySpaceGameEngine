#include "UIDesignerWindow.hpp"
#include "UIDesignerModel.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIUserStyle.hpp"
#include "okay/Components/UIGuides.hpp"
#include "okay/Components/UIFrame.hpp"
#include "PrefabEdit.hpp"
#include "UIDesignerActions.hpp"
#include "okay/Scene/UILayoutTools.hpp"
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
bool g_showPalette = false, g_showCheat = false, g_showFind = false, g_fitRequested = false;
char g_query[96] = "", g_layerFilter[96] = "", g_findText[96] = "", g_replText[96] = "";
bool g_findCase = false;
float g_colFrom[4] = {1, 0, 0, 1}, g_colTo[4] = {0, 0, 1, 1}, g_colTol = 0.04f;
char g_styFrom[64] = "", g_styTo[64] = "";
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

bool TreeMatches(GameObject* g) {
    if (!g) return false;
    if (uid::NameMatches(g->name, g_layerFilter)) return true;
    if (g->transform) for (Transform* c : g->transform->Children()) if (c && TreeMatches(c->gameObject)) return true;
    return false;
}
void DrawTree(GameObject* g, EditorState& ed, int depth) {
    if (!g || !HasUI(g)) return;
    if (g_layerFilter[0] && !TreeMatches(g)) return;
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
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##lfilter", "Search layers by name", g_layerFilter, sizeof(g_layerFilter));
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
        if (g_layerFilter[0] && ImGui::SmallButton("Select all matches")) {
            ed.Select(nullptr);
            for (const auto& up : ed.scene().Objects())
                if (up && IsUI(up.get()) && uid::NameMatches(up->name, g_layerFilter) && !UIDesignerLocked(up.get())) { if (!ed.selected()) ed.Select(up.get()); else ed.ToggleSelect(up.get()); }
        }
        if (ImGui::SmallButton("Find and replace...")) g_showFind = true;
        if (ImGui::CollapsingHeader("Shared styles")) DrawStyles(ed);
        if (ImGui::CollapsingHeader("Reusable UI")) DrawReusable(ed);
    }
    ImGui::End();
}


namespace {
using CmdFn = std::function<void(EditorState&)>;
struct Cmd { uid::Command c; CmdFn run; };

void LockOrHide(EditorState& ed, bool hide) {
    if (ed.MultiSelection().empty()) return;
    if (hide) ed.PushUndo("Hide UI widgets");
    for (GameObject* g : ed.MultiSelection()) if (g) { if (hide) g->active = false; else g_locked.insert(g); }
    if (hide) ed.dirty = true;
}
void Group(EditorState& ed) {
    std::vector<GameObject*> sel(ed.MultiSelection().begin(), ed.MultiSelection().end());
    if (sel.size() < 2) return;
    ed.PushUndo("Group UI");
    if (GameObject* p = UILayout::WrapInPanel(ed.scene(), sel)) { p->name = "Group"; ed.Select(p); ed.dirty = true; }
}
const std::vector<Cmd>& Commands() {
    using uid::AlignMode;
    static const std::vector<Cmd> v = {
        {{"fit", "Fit: show the whole screen", "", "zoom reset view"}, [](EditorState&) { g_fitRequested = true; }},
        {{"layers", "Show or hide the Layers panel", "", "panel list tree"}, [](EditorState&) { g_showLayers = !g_showLayers; }},
        {{"guides.clear", "Clear all guides", "", "rulers lines"}, [](EditorState& ed) { g_guides.clear(); WriteBackGuides(); ed.dirty = true; }},
        {{"sel.all", "Select all widgets", "Ctrl+A", "everything"}, [](EditorState& ed) { uid::SelectTool(ed, uid::SelOp::All, UIDesignerLocked); }},
        {{"sel.invert", "Invert the selection", "Ctrl+Shift+I", "opposite"}, [](EditorState& ed) { uid::SelectTool(ed, uid::SelOp::Invert, UIDesignerLocked); }},
        {{"sel.same", "Select the same kind", "Ctrl+Shift+A", "type similar"}, [](EditorState& ed) { uid::SelectTool(ed, uid::SelOp::SameKind, UIDesignerLocked); }},
        {{"group", "Group (put on a card)", "Ctrl+G", "wrap panel"}, Group},
        {{"lock", "Lock selected", "Ctrl+Shift+L", "freeze"}, [](EditorState& ed) { LockOrHide(ed, false); }},
        {{"hide", "Hide selected", "Ctrl+Shift+H", "invisible"}, [](EditorState& ed) { LockOrHide(ed, true); }},
        {{"align.left", "Align left (to the key widget)", "", "edge"}, [](EditorState& ed) { uid::AlignSelection(ed, AlignMode::Left); }},
        {{"align.centerh", "Align centre, left to right", "", "middle horizontal"}, [](EditorState& ed) { uid::AlignSelection(ed, AlignMode::CenterH); }},
        {{"align.right", "Align right", "", "edge"}, [](EditorState& ed) { uid::AlignSelection(ed, AlignMode::Right); }},
        {{"align.top", "Align top", "", "edge"}, [](EditorState& ed) { uid::AlignSelection(ed, AlignMode::Top); }},
        {{"align.centerv", "Align centre, top to bottom", "", "middle vertical"}, [](EditorState& ed) { uid::AlignSelection(ed, AlignMode::CenterV); }},
        {{"align.bottom", "Align bottom", "", "edge"}, [](EditorState& ed) { uid::AlignSelection(ed, AlignMode::Bottom); }},
        {{"dist.h", "Space evenly, across", "", "distribute horizontal"}, [](EditorState& ed) { uid::DistributeSelection(ed, true, -1.0f); }},
        {{"dist.v", "Space evenly, down", "", "distribute vertical"}, [](EditorState& ed) { uid::DistributeSelection(ed, false, -1.0f); }},
        {{"gap.h8", "Gap of 8 pixels, across", "", "distribute spacing"}, [](EditorState& ed) { uid::DistributeSelection(ed, true, 8.0f); }},
        {{"gap.v8", "Gap of 8 pixels, down", "", "distribute spacing"}, [](EditorState& ed) { uid::DistributeSelection(ed, false, 8.0f); }},
        {{"match.w", "Same width as the key widget", "", "size"}, [](EditorState& ed) { uid::MatchSelection(ed, true, false); }},
        {{"match.h", "Same height as the key widget", "", "size"}, [](EditorState& ed) { uid::MatchSelection(ed, false, true); }},
        {{"tidy2", "Tidy into 2 columns", "", "grid arrange"}, [](EditorState& ed) { uid::TidySelection(ed, 2, 8.0f); }},
        {{"tidy3", "Tidy into 3 columns", "", "grid arrange"}, [](EditorState& ed) { uid::TidySelection(ed, 3, 8.0f); }},
        {{"flip.h", "Flip left to right", "", "mirror"}, [](EditorState& ed) { uid::FlipSelection(ed, true); }},
        {{"flip.v", "Flip top to bottom", "", "mirror"}, [](EditorState& ed) { uid::FlipSelection(ed, false); }},
        {{"find", "Find and replace text", "", "search"}, [](EditorState&) { g_showFind = true; }},
        {{"cheat", "Keyboard shortcuts", "F1", "help keys"}, [](EditorState&) { g_showCheat = true; }},
    };
    return v;
}
}  // namespace

namespace {
struct RotState { bool on = false; float startRot = 0, startAngle = 0; GameObject* go = nullptr; } g_rot;
bool RotRect(EditorState& ed, ImVec2 vp, ImVec2 vs, UIFrame*& fr, uid::Rect& r, float& px, float& py) {
    GameObject* g = ed.selected(); if (!g) return false;
    fr = g->GetComponent<UIFrame>(); if (!fr) return false;
    Vec2 o, sz; if (!GetUIScreenRect(g, vs.x, vs.y, o, sz)) return false;
    r = {vp.x + o.x, vp.y + o.y, sz.x, sz.y};
    px = r.x + fr->pivot.x * r.w; py = r.y + fr->pivot.y * r.h; return true;
}
}  // namespace

bool UIDesignerRotateInput(EditorState& ed, ImVec2 vp, ImVec2 vs, const ImGuiIO& io) {
    UIFrame* fr; uid::Rect r; float px, py;
    if (g_rot.on) {
        if (!ed.selected() || ed.selected() != g_rot.go || !RotRect(ed, vp, vs, fr, r, px, py) || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { g_rot.on = false; return false; }
        fr->rotation = uid::RotateDrag(g_rot.startRot, g_rot.startAngle, uid::AngleFromPivot(px, py, io.MousePos.x, io.MousePos.y, false), io.KeyShift);
        ed.dirty = true; return true;
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !RotRect(ed, vp, vs, fr, r, px, py)) return false;
    float hx, hy; uid::RotateHandlePos(r, px, py, fr->rotation, 26.0f, hx, hy);
    if (std::fabs(io.MousePos.x - hx) > 8.0f || std::fabs(io.MousePos.y - hy) > 8.0f) return false;
    ed.PushUndo("Rotate UI");
    g_rot = {true, fr->rotation, uid::AngleFromPivot(px, py, io.MousePos.x, io.MousePos.y, false), ed.selected()};
    return true;
}
void UIDesignerDrawRotate(ImDrawList* dl, EditorState& ed, ImVec2 vp, ImVec2 vs) {
    UIFrame* fr; uid::Rect r; float px, py;
    if (!RotRect(ed, vp, vs, fr, r, px, py)) return;
    float hx, hy, sx, sy; uid::RotateHandlePos(r, px, py, fr->rotation, 26.0f, hx, hy);
    sx = r.x + r.w * 0.5f; sy = r.y; uid::RotatePoint(sx, sy, px, py, fr->rotation);
    dl->AddLine({sx, sy}, {hx, hy}, IM_COL32(255, 210, 40, 200), 1.2f);
    dl->AddCircleFilled({hx, hy}, 6.0f, g_rot.on ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 210, 40, 255));
    dl->AddCircle({hx, hy}, 6.0f, IM_COL32(20, 20, 24, 255), 0, 1.5f);
    if (g_rot.on) { char b[24]; std::snprintf(b, sizeof(b), "%.0f deg", fr->rotation); dl->AddText({hx + 10, hy - 8}, IM_COL32(255, 230, 140, 255), b); }
}

bool UIDesignerTakeFitRequest() { const bool f = g_fitRequested; g_fitRequested = false; return f; }

void UIDesignerKeys(EditorState& ed, bool active) {
    if (!active || ImGui::GetIO().WantTextInput) return;
    const ImGuiIO& io = ImGui::GetIO();
    auto run = [&](const char* id) { for (const Cmd& c : Commands()) if (c.c.id == id) { c.run(ed); return; } };
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) { g_showPalette = true; g_query[0] = 0; }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) g_showCheat = !g_showCheat;
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false)) run("sel.all");
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_I, false)) run("sel.invert");
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false)) run("sel.same");
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false)) run("group");
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false)) run("lock");
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_H, false)) run("hide");
}

void UIDesignerOverlays(EditorState& ed) {
    if (g_showPalette) {   // Ctrl+P: type a few words, press Enter (or click) to run the first / chosen action
        std::vector<uid::Command> list; for (const Cmd& c : Commands()) list.push_back(c.c);
        ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x * 0.5f - 220.0f, 90.0f}, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize({440, 360}, ImGuiCond_Appearing);
        if (ImGui::Begin("Find an action", &g_showPalette, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1.0f);
            const bool enter = ImGui::InputTextWithHint("##q", "Type what you want to do", g_query, sizeof(g_query), ImGuiInputTextFlags_EnterReturnsTrue);
            const auto order = uid::RankCommands(list, g_query);
            int ran = -1;
            for (size_t k = 0; k < order.size(); ++k) {
                const Cmd& c = Commands()[order[k]];
                std::string row = c.c.label; if (!c.c.keys.empty()) row += "    " + c.c.keys;
                if (ImGui::Selectable(row.c_str(), k == 0)) ran = (int)order[k];
            }
            if (enter && !order.empty()) ran = (int)order[0];
            if (ran >= 0) { Commands()[ran].run(ed); g_showPalette = false; }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) g_showPalette = false;
        }
        ImGui::End();
    }
    if (g_showCheat) {
        ImGui::SetNextWindowSize({420, 380}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Keyboard shortcuts", &g_showCheat)) {
            for (const auto& sc : uid::Shortcuts()) { ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", sc.keys); ImGui::SameLine(190); ImGui::TextUnformatted(sc.what); }
        }
        ImGui::End();
    }
    if (g_showFind) {
        if (ImGui::Begin("Find and replace", &g_showFind, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Find", g_findText, sizeof(g_findText)); ImGui::InputText("Replace with", g_replText, sizeof(g_replText));
            ImGui::Checkbox("Match case", &g_findCase);
            if (ImGui::Button("Replace in all UI text")) {
                ed.PushUndo("Replace UI text"); int total = 0;
                for (const auto& up : ed.scene().Objects()) {
                    if (!up) continue; int n = 0;
                    if (auto* t = up->GetComponent<TextRenderer>()) if (t->screenSpace) { t->text = uid::ReplaceText(t->text, g_findText, g_replText, g_findCase, n); total += n; }
                    if (auto* b = up->GetComponent<UIButton>()) { b->label = uid::ReplaceText(b->label, g_findText, g_replText, g_findCase, n); total += n; }
                }
                if (total) ed.dirty = true;
                ImGui::OpenPopup("done");
            }
            if (ImGui::BeginPopup("done")) { ImGui::TextUnformatted("Done."); ImGui::EndPopup(); }
            ImGui::Separator();
            ImGui::ColorEdit4("Find colour", g_colFrom); ImGui::ColorEdit4("Replace with", g_colTo);
            ImGui::DragFloat("How close", &g_colTol, 0.005f, 0.0f, 0.5f, "%.3f");
            if (ImGui::Button("Replace colour")) uid::ReplaceColor(ed, Color(g_colFrom[0], g_colFrom[1], g_colFrom[2], g_colFrom[3]), Color(g_colTo[0], g_colTo[1], g_colTo[2], g_colTo[3]), g_colTol);
            ImGui::Separator();
            ImGui::InputText("Style name", g_styFrom, sizeof(g_styFrom)); ImGui::InputText("Use this style instead", g_styTo, sizeof(g_styTo));
            if (ImGui::Button("Replace style")) uid::ReplaceStyle(ed, g_styFrom, g_styTo);
        }
        ImGui::End();
    }
}

}  // namespace oked
