// See OkayUIHostObjects.hpp. Built like OkayUIHostPanels.cpp: its own object library with okayui/probe first on the include path (so `#include <imgui.h>` in the
// editor sources is OkayUI's compatibility header and no Dear ImGui is involved), the names the editor sources DEFINE renamed (the *_WO macros) so this copy of
// InspectorPanel.cpp and EditorWidgets.cpp can live in the same program as the Dear ImGui editor's own, and the headers they need included at global scope first.
#include "OkayUIHostObjects.hpp"

#undef OKAY_HAVE_OKAYUI   // the editor sources must take their Dear ImGui API path (compiled against OkayUI): never the hosted-in-ImGui OkayUI path

#define AssetField AssetField_WO
#define ObjectField ObjectField_WO
#define DrawMixedNote DrawMixedNote_WO
#define WatchEditUndo WatchEditUndo_WO
#define AssetPathPicker AssetPathPicker_WO
#define PrefabFieldMark PrefabFieldMark_WO
#define SetTipComponent SetTipComponent_WO
#define DrawPlayModeNote DrawPlayModeNote_WO
#define DrawPrefabHeader DrawPrefabHeader_WO
#define PrefabChangedFor PrefabChangedFor_WO
#define SetPrefabUiHooks SetPrefabUiHooks_WO
#define DrawPrefabChanges DrawPrefabChanges_WO
#define QueuePrefabAction QueuePrefabAction_WO
#define RunPrefabRequests RunPrefabRequests_WO
#define ComponentValueMenu ComponentValueMenu_WO
#define HighlightAssetDrop HighlightAssetDrop_WO
#define BeginInspectorFrame BeginInspectorFrame_WO
#define PrefabComponentMenu PrefabComponentMenu_WO
#define RefreshPrefabChanges RefreshPrefabChanges_WO
#define DrawFallbackInspector DrawFallbackInspector_WO
#define InvalidatePrefabChanges InvalidatePrefabChanges_WO
#define OpenCreateVariantDialog OpenCreateVariantDialog_WO
#define ApplyComponentValueRequests ApplyComponentValueRequests_WO
#define FieldTip FieldTip_WO
#define PropertyField PropertyField_WO
#define DrawTbIcon DrawTbIcon_WO
#define IconButton IconButton_WO
#define SearchField SearchField_WO
#define CategoryColor CategoryColor_WO
#define CategoryGlyph CategoryGlyph_WO
#define SameLineRight SameLineRight_WO
#define TipArrowButton TipArrowButton_WO
#define TipSmallButton TipSmallButton_WO
#define IconLabelButton IconLabelButton_WO
#define AccentToggleButton AccentToggleButton_WO
#define AccentCol AccentCol_WO
#define TipButton TipButton_WO

#include "DialogService.hpp"
#include "EditorState.hpp"
#include "okay/Platform/Platform.hpp"
#include "InspectorPanel.hpp"
#include "EditUndo.hpp"
#include "EditorWidgets.hpp"
#include "ConsolePanel.hpp"
#include "InspectorModel.hpp"
#include "PrefabEdit.hpp"
#include "AddComponentRegistry.hpp"   // the Add Component browser's model, shared with the Dear ImGui window
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIImGuiNames.hpp"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_set>

#include "EditorWidgets.cpp"
#include "InspectorPanel.cpp"

#undef AssetField
#undef ObjectField
#undef DrawMixedNote
#undef WatchEditUndo
#undef AssetPathPicker
#undef PrefabFieldMark
#undef SetTipComponent
#undef DrawPlayModeNote
#undef DrawPrefabHeader
#undef PrefabChangedFor
#undef SetPrefabUiHooks
#undef DrawPrefabChanges
#undef QueuePrefabAction
#undef RunPrefabRequests
#undef ComponentValueMenu
#undef HighlightAssetDrop
#undef BeginInspectorFrame
#undef PrefabComponentMenu
#undef RefreshPrefabChanges
#undef DrawFallbackInspector
#undef InvalidatePrefabChanges
#undef OpenCreateVariantDialog
#undef ApplyComponentValueRequests
#undef FieldTip
#undef PropertyField
#undef DrawTbIcon
#undef IconButton
#undef SearchField
#undef CategoryColor
#undef CategoryGlyph
#undef SameLineRight
#undef TipArrowButton
#undef TipSmallButton
#undef IconLabelButton
#undef AccentToggleButton
#undef AccentCol
#undef TipButton

namespace oked {
namespace wm {

namespace {
ObjectsProbe g_probe;
std::string PrettyName_WO_(const std::string& n) { return PrettyName(n); }   // InspectorModel.cpp: "MeshRenderer" -> "Mesh Renderer"
using okay::GameObject;
using okay::Component;

// ---- the Hierarchy ----
struct HierState {
    std::unordered_set<const GameObject*> expanded;
    char filter[128] = {0};
    bool sort = false;
    GameObject* pressed = nullptr; float downX = 0, downY = 0; bool dragActive = false;
};

std::string DisplayName(const GameObject* go) { return go->name.empty() ? std::string("(unnamed)") : go->name; }

struct HierDraw {
    okay::editor::EditorState& ed; HierState& hs;
    int rows = 0; bool first = true;
    GameObject* deleteAfter = nullptr; GameObject* duplicateAfter = nullptr;
    void Row(GameObject* go, int depth) {
        using namespace OkayUI;
        PushID(go);
        const bool hasKids = go->transform && go->transform->ChildCount() > 0;
        const bool open = hs.expanded.count(go) != 0;
        Indent(depth * 14.0f);
        if (hasKids) { if (SmallButton(open ? "v" : ">")) { if (open) hs.expanded.erase(go); else hs.expanded.insert(go); } SameLine(); }
        else { Dummy(OkayUI::Vec2{20.0f, 0.0f}); SameLine(); }
        const std::string label = DisplayName(go) + "##row";
        const bool sel = ed.IsSelected(go);
        if (Selectable(label.c_str(), sel)) { if (GetFrameInput().ctrl) ed.ToggleSelect(go); else ed.Select(go); }
        if (first) { g_probe.firstRow = GetItemRect(); first = false; }
        g_probe.rowRects.push_back(GetItemRect()); g_probe.rowObjects.push_back(go);
        if (IsItemHovered() && IsMouseClicked(0)) { hs.pressed = go; hs.downX = GetFrameInput().mouseX; hs.downY = GetFrameInput().mouseY; hs.dragActive = false; }
        if (IsItemHovered() && IsMouseDoubleClicked(0) && go->transform) { const okay::Vec3 p = go->transform->Position(); ed.camTarget = p; }   // frame it in the Scene view
        if (BeginPopupContextItem("rowctx")) {
            if (!ed.IsSelected(go)) ed.Select(go);
            if (MenuItem("Duplicate")) duplicateAfter = go;
            if (MenuItem("Delete")) deleteAfter = go;
            EndPopup();
        }
        Unindent(depth * 14.0f);
        PopID();
        ++rows;
        if (hasKids && open) {
            std::vector<GameObject*> kids;
            for (okay::Transform* t : go->transform->Children()) if (t && t->gameObject) kids.push_back(t->gameObject);
            if (hs.sort) std::sort(kids.begin(), kids.end(), [](GameObject* a, GameObject* b) { return a->name < b->name; });
            for (GameObject* k : kids) Row(k, depth + 1);
        }
    }
};

void DrawHierarchyWindow(okay::editor::EditorState& ed, HierState& hs, HostModeState* st) {
    using namespace OkayUI;
    if (!DockBegin(kPanelHierarchy, st ? &st->OpenRef(kPanelHierarchy) : nullptr)) { DockEnd(); return; }
    if (Button("+")) OpenPopup("HierCreate");
    g_probe.createButton = GetItemRect();
    SameLine();
    SetNextItemWidth(160.0f);
    InputTextWithHint("##hfilter", "Search (name, t:tag, c:component, is:off)", hs.filter, (int)sizeof(hs.filter));
    g_probe.filterField = GetItemRect();
    SameLine();
    Checkbox("A-Z", &hs.sort);
    if (BeginPopup("HierCreate")) {
        if (MenuItem("Empty")) ed.CreateEmpty();
        g_probe.createEmpty = GetItemRect();
        if (MenuItem("Cube")) ed.CreateCube();
        if (MenuItem("Sprite")) ed.CreateSprite();
        if (MenuItem("Camera")) ed.CreateCamera();
        EndPopup();
    }
    {   // the scene header: name, an unsaved marker and the object count
        const std::string sname = ed.scene().Name().empty() ? std::string("Untitled") : ed.scene().Name();
        const std::string head = sname + (ed.dirty ? "  -  unsaved" : "") + "   (" + std::to_string(ed.scene().Objects().size()) + " objects)";
        TextDisabled(head.c_str());
    }
    Separator();
    g_probe.rowRects.clear(); g_probe.rowObjects.clear();
    HierDraw hd{ed, hs};
    if (hs.filter[0]) {   // a flat list of every match (the search model is the Dear ImGui Hierarchy's: InspectorModel.cpp)
        const HierFilter filter = HierFilter::Parse(hs.filter);
        std::unordered_set<const GameObject*> pathHits;
        if (!filter.PathText().empty()) for (GameObject* g : ed.scene().FindAllByPath(filter.PathText())) pathHits.insert(g);
        int hits = 0;
        for (const auto& up : ed.scene().Objects()) {
            if (!filter.Matches(*up, &pathHits)) continue;
            ++hits; hd.Row(up.get(), 0);
        }
        if (hits == 0) { const std::string m = std::string("No objects match '") + hs.filter + "'."; TextDisabled(m.c_str()); }
    } else {
        std::vector<GameObject*> roots;
        for (const auto& up : ed.scene().Objects()) if (up->transform && up->transform->Parent() == nullptr) roots.push_back(up.get());
        if (hs.sort) std::sort(roots.begin(), roots.end(), [](GameObject* a, GameObject* b) { return a->name < b->name; });
        if (roots.empty()) TextDisabled("Empty scene. Use + to add an object.");
        for (GameObject* r : roots) hd.Row(r, 0);
    }
    g_probe.hierarchyRows = hd.rows;
    {   // drag a row onto another: Before / Inside / After by where in the row the pointer is; below the last row = the top level. One undo step.
        const OkayUI::Input& in = GetFrameInput();
        if (hs.pressed && in.mouseDown) {
            const float dx = in.mouseX - hs.downX, dy = in.mouseY - hs.downY;
            if (dx * dx + dy * dy > 25.0f) hs.dragActive = true;
        }
        g_probe.dragging = hs.dragActive && in.mouseDown;
        if (g_probe.dragging) {
            const DrawListHandle& fg = GetForegroundDrawList();
            const std::string tip = "Move " + DisplayName(hs.pressed);
            fg->AddText(OkayUI::Vec2{in.mouseX + 12.0f, in.mouseY + 6.0f}, RGBA(255, 255, 255, 230), tip.c_str());
            for (size_t i = 0; i < g_probe.rowRects.size(); ++i) {   // the drop mark
                const OkayUI::Rect& r = g_probe.rowRects[i];
                if (in.mouseX < r.x || in.mouseX > r.x + r.w || in.mouseY < r.y || in.mouseY > r.y + r.h) continue;
                const float f = (in.mouseY - r.y) / (r.h > 0 ? r.h : 1.0f);
                if (f < 0.25f) fg->AddLine(OkayUI::Vec2{r.x, r.y}, OkayUI::Vec2{r.x + r.w, r.y}, RGBA(255, 190, 60, 255), 2.0f);
                else if (f > 0.75f) fg->AddLine(OkayUI::Vec2{r.x, r.y + r.h}, OkayUI::Vec2{r.x + r.w, r.y + r.h}, RGBA(255, 190, 60, 255), 2.0f);
                else fg->AddRect(OkayUI::Vec2{r.x, r.y}, OkayUI::Vec2{r.x + r.w, r.y + r.h}, RGBA(255, 190, 60, 255));
            }
        }
        if (hs.pressed && !in.mouseDown) {
            if (hs.dragActive) {
                GameObject* target = nullptr; DropWhere where = DropWhere::Inside; bool over = false;
                for (size_t i = 0; i < g_probe.rowRects.size(); ++i) {
                    const OkayUI::Rect& r = g_probe.rowRects[i];
                    if (in.mouseX < r.x || in.mouseX > r.x + r.w || in.mouseY < r.y || in.mouseY > r.y + r.h) continue;
                    const float f = (in.mouseY - r.y) / (r.h > 0 ? r.h : 1.0f);
                    target = g_probe.rowObjects[i]; over = true; where = f < 0.25f ? DropWhere::Before : f > 0.75f ? DropWhere::After : DropWhere::Inside;
                }
                if (!over && !g_probe.rowRects.empty()) { const OkayUI::Rect& last = g_probe.rowRects.back(); over = in.mouseY > last.y + last.h && in.mouseX >= last.x && in.mouseX <= last.x + last.w; }   // below the list: top level
                if (over) {
                    std::vector<GameObject*> dragged;
                    if (ed.IsSelected(hs.pressed)) for (GameObject* g : ed.MultiSelection()) dragged.push_back(g);
                    if (dragged.empty()) dragged.push_back(hs.pressed);
                    if (!PlanDrop(ed.scene(), dragged, target, where).empty()) {
                        ed.PushUndo("Reparent");
                        g_probe.reparented = ApplyDrop(ed.scene(), dragged, target, where);
                        ed.dirty = true;
                        if (where == DropWhere::Inside && target) hs.expanded.insert(target);
                    }
                }
            }
            hs.pressed = nullptr; hs.dragActive = false;
        }
    }
    // edits that change the list wait until it has been walked
    if (hd.duplicateAfter) ed.DuplicateSelected();
    if (hd.deleteAfter) ed.DeleteSelected();
    DockEnd();
}

// ---- the Inspector ----
// A component's header: its enabled check box and a collapsing header (open at first). The editor's own is main.cpp's; this is the window-mode one.
bool InspectorHeader(const char* label, Component* comp, Component** toRemove, bool) {
    using namespace OkayUI;
    PushID(comp);
    bool en = comp->enabled;
    if (Checkbox("##en", &en)) comp->enabled = en;
    SameLine();
    if (SmallButton("x") && toRemove) *toRemove = comp;   // remove this component (before the header, which takes the rest of the row)
    if (!g_probe.firstRemoveButton.w) g_probe.firstRemoveButton = GetItemRect();
    SameLine();
    const bool open = CollapsingHeader(label, true);
    PopID();
    return open;
}

void DrawFieldsOf(okay::editor::EditorState& ed, const std::vector<GameObject*>& targets, const char* infoName, bool playing) {
    auto info = okay::PropertyRegistry::FindByName(infoName);
    if (!info) return;
    std::string group;
    for (const okay::PropertyDesc& d : info->properties) {
        if (d.IsHidden()) continue;
        if (d.IsRuntimeOnly() && !playing) continue;
        if (d.group != group) { group = d.group; if (!group.empty()) OkayUI::SeparatorText(group.c_str()); }
        PropertyField_WO(ed, targets, info->name, 0, d);
        if (d.name == "active" && info->objectLevel) g_probe.activeField = OkayUI::GetItemRect();
        ++g_probe.fieldsDrawn;
    }
}

void DrawInspectorWindow(okay::editor::EditorState& ed, HostModeState* st) {
    using namespace OkayUI;
    if (!DockBegin(kPanelInspector, st ? &st->OpenRef(kPanelInspector) : nullptr)) { DockEnd(); return; }
    g_probe.inspectorCards = 0; g_probe.fieldsDrawn = 0; g_probe.selectedName.clear();
    WatchEditUndo_WO(ed);
    BeginInspectorFrame_WO();
    GameObject* go = ed.selected();
    if (!go) { TextDisabled("Select an object in the Hierarchy."); DockEnd(); return; }
    g_probe.selectedName = go->name; g_probe.firstRemoveButton = OkayUI::Rect{};
    DrawPlayModeNote_WO(ed);
    std::vector<GameObject*> targets;
    for (GameObject* g : ed.MultiSelection()) if (g) targets.push_back(g);
    if (std::find(targets.begin(), targets.end(), go) == targets.end()) targets = {go};
    if (targets.size() > 1) TextDisabled(("Editing " + std::to_string(targets.size()) + " selected objects").c_str());
    const bool playing = ed.isPlaying();
    ++g_probe.inspectorCards; DrawFieldsOf(ed, targets, "GameObject", playing);   // name, active, tag, layer ...
    Separator();
    if (go->transform) { ++g_probe.inspectorCards; if (CollapsingHeader("Transform", true)) DrawFieldsOf(ed, targets, "Transform", playing); }
    Component* toRemove = nullptr;
    for (const auto& cp : go->Components()) {
        Component* c = cp.get();
        if (!c || c == go->transform) continue;
        const float y0 = GetCursorPosY();
        DrawFallbackInspector_WO(ed, go, c, toRemove, y0, &InspectorHeader);
        if (GetCursorPosY() != y0) ++g_probe.inspectorCards;
    }
    ApplyComponentValueRequests_WO(ed, go);
    if (toRemove) {   // the (x) of a component header: refused when another component needs this one
        if (Component* needs = go->RequiredBy(toRemove)) {
            const std::string why = "Another component on this object needs it (" + (okay::PropertyRegistry::FindFor(*needs) ? okay::PropertyRegistry::FindFor(*needs)->name : std::string("another component")) + "). Remove that one first.";
            if (DialogService* d = CurrentDialogs()) d->ShowError("Can't remove this component", why);
        } else { ed.PushUndo("Remove Component"); go->RemoveComponent(toRemove); ed.dirty = true; }
    }
    Separator();
    static oked::addcomp::State acState; static oked::addcomp::Prefs acPrefs; static bool acLoaded = false; static char acQuery[64] = "";
    if (Button("Add Component")) { OpenPopup("##addcomp"); acState = oked::addcomp::State(); acQuery[0] = '\0'; }
    g_probe.addComponentButton = GetItemRect();
    if (BeginPopup("##addcomp")) {   // the same browser model as the Dear ImGui window: search, plain groups, Recent / Favourites, suggestions, greyed reasons
        using namespace oked::addcomp;
        if (!acLoaded) { acLoaded = true; LoadPrefs("okay_addcomponent.txt", acPrefs); }
        g_probe.addMenuItems = 0; g_probe.firstCategory = OkayUI::Rect{}; g_probe.firstAddItem = OkayUI::Rect{};
        std::vector<std::shared_ptr<const okay::ComponentTypeInfo>> infos;
        const std::vector<Entry> entries = EntriesFromRegistry(*go, &infos);
        if (acState.category.empty()) acState.category = DefaultCategory(entries, acPrefs);
        if (IsWindowAppearing()) SetKeyboardFocusHere();
        SetNextItemWidth(-1.0f);
        InputTextWithHint("##acq", "Search  (try: jump, light, sound)", acQuery, (int)sizeof(acQuery));
        acState.SetQuery(acQuery);
        View v = Build(entries, acState, acPrefs);
        ClampCursor(acState, v);
        int pick = -1;
        struct K { OkayUI::KeyId id; Key key; };
        static const K keys[] = {{OkayUI::KeyId_Up, Key::Up}, {OkayUI::KeyId_Down, Key::Down}, {OkayUI::KeyId_PageUp, Key::PageUp}, {OkayUI::KeyId_PageDown, Key::PageDown},
                                 {OkayUI::KeyId_Tab, Key::Tab}, {OkayUI::KeyId_Enter, Key::Enter}, {OkayUI::KeyId_Escape, Key::Escape}};
        for (const K& k : keys) {
            if (!IsKeyPressed(k.id, k.key != Key::Tab && k.key != Key::Enter && k.key != Key::Escape)) continue;
            Outcome o = HandleKey(acState, v, entries, k.key);
            if (o.close) CloseCurrentPopup();
            if (o.activate >= 0) pick = o.activate;
            if (acState.query != acQuery) std::snprintf(acQuery, sizeof(acQuery), "%s", acState.query.c_str());
            if (o.rebuild) { v = Build(entries, acState, acPrefs); ClampCursor(acState, v); }
        }
        const float colH = 300.0f;
        if (BeginChild("##acc", 150.0f, colH, true)) {   // the groups, with how many can still be added
            for (std::size_t i = 0; i < v.cats.size(); ++i) {
                const CatCount& c = v.cats[i];
                PushID((int)i);
                const std::string lbl = c.name + "  (" + std::to_string(c.count) + ")";
                if (Selectable(lbl.c_str(), !v.searching && acState.category == c.name)) { acState.SelectCategory(c.name); acQuery[0] = '\0'; }
                if (!g_probe.firstCategory.w) g_probe.firstCategory = GetItemRect();
                PopID();
            }
            EndChild();
        }
        SameLine();
        if (BeginChild("##acr", 0.0f, colH, true)) {   // the components: name, a grey group tag when searching, one plain line under it
            const bool tag = v.searching || acState.category == kCatForObject || acState.category == kCatRecent || acState.category == kCatFavorites;
            for (int i = 0; i < (int)v.rows.size(); ++i) {
                const Row& r = v.rows[i]; const Entry& e = entries[r.entry];
                if (r.groupStart) SeparatorText(r.group.c_str());
                PushID(i);
                const std::string lbl = e.name + (tag ? "     " + e.category : std::string());
                const bool hit = Selectable(lbl.c_str(), i == acState.cursor);
                if (!g_probe.firstAddItem.w) g_probe.firstAddItem = GetItemRect();
                ++g_probe.addMenuItems;
                TextDisabled(("    " + (e.available ? (r.note.empty() ? e.description : r.note) : e.reason)).c_str());
                PopID();
                if (hit) { acState.cursor = i; Outcome o = Choose(acState, entries, r.entry); if (o.activate >= 0) pick = o.activate; }
            }
            if (v.rows.empty()) {
                if (v.searching) { TextDisabled(("Nothing matches \"" + acState.query + "\".").c_str());
                    for (const std::string& d : v.didYouMean) if (SmallButton(("Did you mean " + d + "?").c_str())) { std::snprintf(acQuery, sizeof(acQuery), "%s", d.c_str()); acState.SetQuery(acQuery); } }
                else TextDisabled("Nothing in this group.");
            }
            EndChild();
        }
        if (!acState.notice.empty()) TextDisabled(acState.notice.c_str());
        if (pick >= 0 && pick < (int)infos.size() && infos[pick]) {
            ed.PushUndo("Add Component");
            const std::size_t before = go->Components().size();
            AddRegistered(*go, *infos[pick]);
            ed.dirty = true;
            acPrefs.NoteUsed(entries[pick].nkey); SavePrefs("okay_addcomponent.txt", acPrefs);
            if (go->Components().size() > before + 1) ConsoleLog("Added " + entries[pick].name + ". It also added " + std::to_string(go->Components().size() - before - 1) + " more it needs.");
            CloseCurrentPopup();
        }
        EndPopup();
    }
    DockEnd();
}
}   // namespace

const ObjectsProbe& GetObjectsProbe() { return g_probe; }

void AddObjectPanels(HostModeState::Panels& panels, HostModeState* st, okay::editor::EditorState* ed) {
    if (!ed) return;
    auto hs = std::make_shared<HierState>();
    panels.hierarchy = [ed, hs, st] { DrawHierarchyWindow(*ed, *hs, st); };
    panels.inspector = [ed, st] { DrawInspectorWindow(*ed, st); };
}

} // namespace wm
} // namespace oked
