#include "OkayPanels.hpp"
#include "MainShared.hpp"

#if defined(OKAY_HAVE_OKAYUI) || defined(OKAY_ON_OKAYUI)
// What the Crash Log, Audio Mixer and History windows show, for both hosts below (the editor on Dear ImGui, and the editor compiled
// against OkayUI's Dear ImGui layer, okay-editor-okayui). These three windows are drawn by OkayUI only.
#include "imgui.h"
#include "OkayUIPanels.hpp"
#include "OkayUIEditorPanels.hpp"
#include "AudioMixerModel.hpp"   // FillAudioMixerModel
#ifdef OKAY_HAVE_CLI_PANEL
#include "CommandLinePanel.hpp"   // shared/: the Command Line panel (okay commands run inside the editor)
#endif

#include <algorithm>
#include <functional>
#include <vector>

namespace oked {

namespace {
okayuiscreens::CrashLogHost CrashLogHostOf(const CrashLogActions& actions) {
    okayuiscreens::CrashLogHost host;
    host.refresh = actions.refresh;
    host.openFolder = actions.openFolder;
    host.clear = actions.clear;
    host.copyAll = [](const std::string& s) { ImGui::SetClipboardText(s.c_str()); };
    return host;
}
// "Copy name" in a History row's menu: the row's label to the clipboard.
void CopyHistoryRow(const okaypanels::HistoryModel& model, const okaypanels::HistoryIntent& intent) {
    if (intent.copy < 0) return;
    const std::vector<okaypanels::HistoryRow> rows = okaypanels::HistoryRows(model);
    if ((std::size_t)intent.copy < rows.size()) ImGui::SetClipboardText(rows[(std::size_t)intent.copy].label.c_str());
}
} // namespace

} // namespace oked
#endif

#ifdef OKAY_HAVE_OKAYUI
#include "imgui.h"
#include "imgui_internal.h"
#include "OkayUIHost.hpp"
#include "OkayUIHostImGui.hpp"
#include "OkayUIImGuiEmbed.hpp"   // HostPanelWindow: a panel drawn by OkayUI only, in Dear ImGui's draw order
#include "InspectorModel.hpp"   // AssetKindAccepts: which rows take a file dragged from the Project panel

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace oked {

namespace {

// The spacing of the ImGui window being drawn (with whatever the window pushed, such as the Inspector's density), for the OkayUI body
// that stands in for its Dear ImGui version, so rows are as tall and as far apart as Dear ImGui's there. Put back after the body.
struct MetricsFromImGui {
    MetricsFromImGui() {
        const ImGuiStyle& st = ImGui::GetStyle();
        okayuiscreens::EditorMetrics m;
        m.itemSpacing = OkayUI::Vec2{st.ItemSpacing.x, st.ItemSpacing.y};
        m.framePadding = OkayUI::Vec2{st.FramePadding.x, st.FramePadding.y};
        m.innerSpacing = OkayUI::Vec2{st.ItemInnerSpacing.x, st.ItemInnerSpacing.y};
        m.itemWidth = ImGui::CalcItemWidth();
        m.indentSpacing = st.IndentSpacing;
        m.sepTextPadding = OkayUI::Vec2{st.SeparatorTextPadding.x, st.SeparatorTextPadding.y};
        m.sepTextBorder = st.SeparatorTextBorderSize;
        okayuiscreens::SetEditorMetrics(m);
    }
    ~MetricsFromImGui() { okayuiscreens::SetEditorMetrics(okayuiscreens::EditorMetrics{}); }
    MetricsFromImGui(const MetricsFromImGui&) = delete;
    MetricsFromImGui& operator=(const MetricsFromImGui&) = delete;
};

// A panel whose body is OkayUI only (okayuihost::HostPanelWindow, shared/OkayUIImGuiEmbed.hpp): the ImGui window is the dock tab and
// the close box, the body is drawn into the window's draw list, so Dear ImGui's popups and the windows in front cover it as they
// cover any ImGui panel. There is no Dear ImGui version to fall back to. A new window with no saved place opens at 440 x 340 (times
// the UI scale).
// `firstW` x `firstH`: the size a window with no saved place opens at (before the UI scale). `flags` are added to the window's;
// `fit` (optional): the body is that big (a window that sizes itself around its body, ImGuiWindowFlags_AlwaysAutoResize).
template <class Draw>
bool HostWindow(const char* title, bool* open, const char* itemId, Draw draw, float firstW = 440.0f, float firstH = 340.0f,
                ImGuiWindowFlags flags = 0, const ImVec2* fit = nullptr) {
    static OkayUI::CapturedGeometry cap;            // scratch: the panels are drawn one after another
    return okayuihost::HostPanelWindow(title, open, itemId, OkayUIRegion(), okayuihost::SharedImGuiEmbed(), cap,
                                       ImVec2(firstW * g_uiScale, firstH * g_uiScale), [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(g_uiScale);
        MetricsFromImGui metrics_;
        draw(p, a);
        OkayUI::PopTextScale();
    }, flags, fit);
}

// An OkayUI body inside part of an ImGui window (a section of Project Settings): a placeholder item as tall as the body drew on the
// last frame, the part of it the window shows offered to OkayUI and used as the clip (the window scrolls it like any item), and
// the popup the body has open offered too. `draw(x, y, w, h)` returns the body's height and its popup rectangle.
struct SectionDrawn { float height = 0.0f; OkayUI::Rect popup; };
// `bleed`: the body may draw that many pixels past the placeholder on the left and the right (Dear ImGui's framed headers reach half the
// window padding out); that strip is offered and clipped with it, within what the window shows.
// `bleedRight` (< 0: `bleed`): on the right instead (the field rows, stage 9).
template <class Draw>
bool HostSection(const char* itemId, Draw draw, float bleed = 0.0f, float bleedRight = -1.0f) {
    if (bleedRight < 0.0f) bleedRight = bleed;
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (!win || okayuihost::OutsideMainWindow(win)) return false;
    // A window ImGui does not show (the Inspector while the Hierarchy tab in its dock is picked, a collapsed window): nothing is drawn
    // and nothing is offered, so the body neither paints over the panel that is shown nor takes its clicks. The caller's Dear ImGui
    // version is skipped by ImGui alike.
    if (win->SkipItems) return false;
    float* height = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(itemId), -1.0f);   // -1: not measured yet
    {
        // The part of the window this section would show: when a popup or another ImGui window in front overlaps it, the Dear ImGui
        // body is drawn instead (ImGui layers it under that window; OkayUI would paint over it). Not measured yet: the whole window.
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        ImRect r = win->InnerClipRect;
        if (*height >= 0.0f) r.ClipWithFull(ImRect(sp.x, sp.y, sp.x + std::max(ImGui::GetContentRegionAvail().x, 8.0f), sp.y + std::max(*height, 8.0f)));
        else r = win->Rect();
        if (okayuihost::CoveredInImGui(win, r.Min.x, r.Min.y, r.GetWidth(), r.GetHeight())) return false;
    }
    const ImVec2 p = okayuihost::ToMainWindowPixels(ImGui::GetCursorScreenPos());   // OkayUI's pixels, not ImGui's desktop ones
    const float w = std::max(ImGui::GetContentRegionAvail().x, 8.0f);
    MetricsFromImGui metrics_;
    if (*height < 0.0f) {
        // The first frame: draw the body into an empty clip only to learn its height, and place a placeholder that tall. A new
        // window fits itself to this frame's content (with a guessed height Project Settings opened far too tall, and so in its own
        // OS window, where OkayUI cannot draw). The body shows from the next frame.
        OkayUI::PushTextScale(g_uiScale * okayuiscreens::kEditorTextScale);
        OkayUI::PushClipRect(p.x, p.y, 0.0f, 0.0f);
        const SectionDrawn d = draw(p.x, p.y, w, 20000.0f);
        OkayUI::PopClipRect();
        OkayUI::PopTextScale();
        *height = (d.height > 1.0f && std::isfinite(d.height)) ? std::min(d.height, 20000.0f) : 300.0f;
        ImGui::InvisibleButton(itemId, ImVec2(w, std::max(*height, 8.0f)));
        return true;
    }
    const float h = std::max(*height, 8.0f);
    ImGui::InvisibleButton(itemId, ImVec2(w, h));
    if (okayuihost::HiddenInImGui(win)) return true;   // a frame ImGui lays out but keeps hidden: the space stays, nothing is drawn
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const ImVec2 c0 = okayuihost::ToMainWindowPixels(win->InnerClipRect.Min), c1 = okayuihost::ToMainWindowPixels(win->InnerClipRect.Max);
    const ImRect clip(c0, c1);
    const float x0 = std::max(p.x - bleed, clip.Min.x), y0 = std::max(p.y, clip.Min.y);
    const float x1 = std::min(p.x + w + bleedRight, clip.Max.x + bleedRight - bleed), y1 = std::min(p.y + h, clip.Max.y);
    if (x1 <= x0 || y1 <= y0) return true;            // scrolled out of view: the space stays, nothing is drawn
    OkayUIRegion().Offer(x0, y0, x1 - x0, y1 - y0, hovered);
    OkayUI::PushTextScale(g_uiScale * okayuiscreens::kEditorTextScale);
    OkayUI::PushClipRect(x0, y0, x1 - x0, y1 - y0);
    const int overlays = OkayUI::GetOverlayCount();
    const SectionDrawn d = draw(p.x, p.y, w, h + 400.0f);   // room below: the height is measured, not cut
    OkayUI::PopClipRect();
    OkayUI::PopTextScale();
    if (d.height > 1.0f && std::isfinite(d.height)) *height = std::min(d.height, 20000.0f);
    if (d.popup.w > 0.0f && d.popup.h > 0.0f) OkayUIRegion().Offer(d.popup.x, d.popup.y, d.popup.w, d.popup.h, true, true);   // it is drawn over everything
    for (int i = overlays; i < OkayUI::GetOverlayCount(); ++i) {   // an open combo list hangs out of the section: it takes the mouse too
        const OkayUI::Rect r = OkayUI::GetOverlayRect(i);
        if (r.w > 0.0f && r.h > 0.0f) OkayUIRegion().Offer(r.x, r.y, r.w, r.h, true, true);
    }
    return true;
}

// The host of the panels drawn by OkayUI only (below the two hosts).
template <class Draw>
bool PanelWindow(const char* title, bool* open, const char* itemId, Draw draw, float firstW = 440.0f, float firstH = 340.0f,
                 ImGuiWindowFlags flags = 0, const ImVec2* fit = nullptr) {
    return HostWindow(title, open, itemId, draw, firstW, firstH, flags, fit);
}

// The host of the sections drawn by OkayUI only (Project Settings' Player, Physics, Rendering, Tags and Layers): the body goes into the
// ImGui window's draw list in Dear ImGui's order (okayuihost::HostPanelSection), so there is no Dear ImGui version to switch to when a
// popup or a window in front covers it. `draw(x, y, w, h)` returns the body's height.
template <class Draw>
bool PanelSection(const char* itemId, Draw draw) {
    static OkayUI::CapturedGeometry cap;            // scratch: the sections are drawn one after another
    return okayuihost::HostPanelSection(itemId, OkayUIRegion(), okayuihost::SharedImGuiEmbed(), cap, [&](float x, float y, float w, float h) {
        OkayUI::PushTextScale(g_uiScale * okayuiscreens::kEditorTextScale);
        MetricsFromImGui metrics_;
        const float drawn = draw(x, y, w, h);
        OkayUI::PopTextScale();
        return drawn;
    });
}

} // namespace

bool DrawHierarchyOkayUI(bool* open, okaypanels::HierarchyModel& model, okaypanels::HierarchyState& state, okaypanels::HierarchyIntent& intent) {
    return HostWindow("Hierarchy", open, "##okayui_hierarchy", [&](ImVec2 p, ImVec2 a) {
        model.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawHierarchy(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        // A row dragged out of the panel: Dear ImGui's targets (the Inspector's object fields, the Scene view) take the same payload
        // they take from the Dear ImGui Hierarchy, the object's pointer under "GO_PTR".
        const OkayUI::DragPayload* live = OkayUI::GetDragDropPayload();
        const ImVec2 mp = okayuihost::ToMainWindowPixels(ImGui::GetIO().MousePos);   // the same pixels as p
        const bool outside = mp.x < p.x || mp.y < p.y || mp.x >= p.x + a.x || mp.y >= p.y + a.y;
        if (live && !OkayUI::IsDragDropExternal() && std::strcmp(live->type, "GO_PTR") == 0 && live->size == (int)sizeof(void*) && outside &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
                ImGui::SetDragDropPayload("GO_PTR", live->data, sizeof(void*));
                ImGui::TextUnformatted("Object");
                ImGui::EndDragDropSource();
            }
        }
    });
}

// ---- the Inspector, stage 1 (docs/own_ui_toolkit.md section 14) ----------------------------------------------------------------------
// Each ported part is a section in the Inspector's own Dear ImGui window, between the parts that are still Dear ImGui's (the prefab
// banner, the UI layout card, every component body but the Mesh Renderer's shading fields): the window scrolls them all alike.

namespace {
float HeaderBleed() { return std::floor(ImGui::GetStyle().WindowPadding.x * 0.5f); }
}

bool DrawInspectorHeaderOkayUI(okaypanels::InspectorHeaderModel& model, okaypanels::InspectorHeaderState& state, okaypanels::InspectorHeaderIntent& intent) {
    return HostSection("##okayui_insp_header", [&](float x, float y, float w, float h) {
        const okayuiscreens::InspectorHeaderResult r = okayuiscreens::DrawInspectorHeader(model, state, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    });
}

bool DrawCompHeaderOkayUI(const char* windowId, const okaypanels::CompHeaderModel& model, okaypanels::CompHeaderIntent& intent, bool* barPressed) {
    const float bleed = HeaderBleed();
    if (barPressed) *barPressed = false;
    return HostSection("##okayui_comphdr", [&](float x, float y, float w, float h) {
        const okayuiscreens::CompHeaderResult r = okayuiscreens::DrawComponentHeader(model, windowId, bleed, x, y, w, h);
        intent = r.intent;
        if (barPressed && OkayUI::IsMouseClicked(0) &&
            OkayUI::IsMouseHoveringRect(OkayUI::Vec2{r.bar.x, r.bar.y}, OkayUI::Vec2{r.bar.x + r.bar.w, r.bar.y + r.bar.h}) &&
            !OkayUI::IsMouseHoveringRect(OkayUI::Vec2{r.enableBox.x, r.enableBox.y}, OkayUI::Vec2{r.enableBox.x + r.enableBox.w, r.enableBox.y + r.enableBox.h}) &&
            !OkayUI::IsMouseHoveringRect(OkayUI::Vec2{r.menuButton.x, r.menuButton.y}, OkayUI::Vec2{r.menuButton.x + r.menuButton.w, r.menuButton.y + r.menuButton.h}))
            *barPressed = true;
        return SectionDrawn{r.contentHeight, r.menu};
    }, bleed);
}

bool DrawTransformOkayUI(okaypanels::TransformModel& model, okaypanels::TransformIntent& intent, int* pressedOn) {
    const float bleed = HeaderBleed();
    if (pressedOn) *pressedOn = 0;
    return HostSection("##okayui_transform", [&](float x, float y, float w, float h) {
        const okayuiscreens::TransformResult r = okayuiscreens::DrawTransform(model, bleed, x, y, w, h);
        intent = r.intent;
        auto over = [](const OkayUI::Rect& q) { return q.w > 0.0f && OkayUI::IsMouseHoveringRect(OkayUI::Vec2{q.x, q.y}, OkayUI::Vec2{q.x + q.w, q.y + q.h}); };
        if (pressedOn && OkayUI::IsMouseClicked(0)) {
            if (over(r.header)) *pressedOn = 1;
            else if (over(r.posLabel)) *pressedOn = 2;
            else if (over(r.rotLabel)) *pressedOn = 3;
            else if (over(r.scaleLabel)) *pressedOn = 4;
        }
        OkayUI::Rect menu;
        if (r.menuCopy.w > 0.0f) menu = OkayUI::Rect{r.menuCopy.x - 16.0f, r.menuCopy.y - 12.0f, r.menuCopy.w + 32.0f, r.menuPaste.y + r.menuPaste.h - r.menuCopy.y + 24.0f};
        else if (r.fieldMenu.w > 0.0f) menu = r.fieldMenu;
        return SectionDrawn{r.contentHeight, menu};
    }, bleed);
}

bool DrawMeshRendererOkayUI(okaypanels::MeshRendererModel& model, okaypanels::MeshRendererIntent& intent) {
    return HostSection("##okayui_meshrenderer", [&](float x, float y, float w, float h) {
        const okayuiscreens::MeshRendererResult r = okayuiscreens::DrawMeshRenderer(model, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    }, HeaderBleed());   // stage 2: room for a field's prefab bar left of the region
}

bool DrawAddComponentOkayUI(std::uint32_t accent, bool& clicked) {
    clicked = false;
    return HostSection("##okayui_addcomponent", [&](float x, float y, float w, float h) {
        const okayuiscreens::AddComponentResult r = okayuiscreens::DrawAddComponentButton(accent, x, y, w, h);
        clicked = r.open;
        return SectionDrawn{r.contentHeight, OkayUI::Rect{}};
    });
}

bool DrawFieldListOkayUI(const char* id, okaypanels::InspFieldList& list) {
    // a file dragged from the Project panel, or an object from the Hierarchy: which rows take it (an outline), before they are drawn
    const ImGuiPayload* pl = ImGui::GetDragDropPayload();
    const char* dragged = (pl && pl->IsDataType("ASSET_PATH") && pl->Data) ? (const char*)pl->Data : nullptr;
    const bool draggedObject = pl && pl->IsDataType("GO_PTR") && pl->Data;
    auto objectKind = [](const char* k) { return std::strcmp(k, "object") == 0 || std::strcmp(k, "objectname") == 0; };
    for (okaypanels::InspRow& r : list.rows) {
        r.dropState = 0;
        if (!r.dropKind) continue;
        if (objectKind(r.dropKind)) { if (draggedObject) r.dropState = 1; }
        else if (dragged) r.dropState = AssetKindAccepts(r.dropKind, dragged) ? 1 : 2;
    }
    std::vector<OkayUI::Rect> rows;
    char win[96];
    std::snprintf(win, sizeof win, "%s%08x##okayui", id, (unsigned)ImGui::GetID(id));   // one OkayUI window per body
    const bool drawn = HostSection(id, [&](float x, float y, float w, float h) {
        okayuiscreens::FieldListResult r = okayuiscreens::DrawFieldList(list, win, x, y, w, h);
        rows = std::move(r.rows);
        return SectionDrawn{r.contentHeight, r.popup};
    // on the right up to 2 pixels short of the window padding's end: a line that does not wrap may run into the padding, as in Dear ImGui,
    // and OkayUI's text is a pixel or two longer than Dear ImGui's whole-pixel advances (the last "." of the Stat: Health note was cut)
    }, HeaderBleed(), std::max(HeaderBleed(), ImGui::GetStyle().WindowPadding.x - 2.0f));
    if (!drawn || (!dragged && !draggedObject)) return drawn;
    // a drop: a Dear ImGui drop target over each row that takes one (the rows are OkayUI's, the drag is Dear ImGui's)
    const ImVec2 o = ImGui::GetMainViewport()->Pos;
    for (std::size_t k = 0; k < list.rows.size() && k < rows.size(); ++k) {
        okaypanels::InspRow& r = list.rows[k];
        const OkayUI::Rect& q = rows[k];
        if (!r.dropKind || q.w <= 0.0f || q.h <= 0.0f) continue;
        const ImRect rr(o.x + q.x, o.y + q.y, o.x + q.x + q.w, o.y + q.y + q.h);
        ImGui::PushID((int)k);
        if (ImGui::BeginDragDropTargetCustom(rr, ImGui::GetID("##fielddrop"))) {
            if (objectKind(r.dropKind)) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GO_PTR", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    okay::GameObject* g = *(okay::GameObject**)p->Data;
                    if (g) { *r.S() = std::strcmp(r.dropKind, "object") == 0 ? g->Guid() : g->name; r.changed = true; }
                }
            } else if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                const std::string path((const char*)p->Data);
                if (AssetKindAccepts(r.dropKind, path)) { *r.S() = path; r.changed = true; }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }
    return true;
}

bool OkayUIHasKeyboard() { return OkayUI::WantsTextInput(); }

} // namespace oked

#else
namespace oked {
#ifdef OKAY_ON_OKAYUI
// The editor compiled against OkayUI's Dear ImGui layer (okay-editor-okayui, docs/ui/own_ui_toolkit.md 6.26): its ImGui windows are
// OkayUI windows already, so the body is drawn straight into the window.
namespace {
template <class Draw>
bool PanelWindow(const char* title, bool* open, const char*, Draw draw, float firstW = 440.0f, float firstH = 340.0f,
                 ImGuiWindowFlags flags = 0, const ImVec2* fit = nullptr) {
    if (open && !*open) return false;
    if (!fit) ImGui::SetNextWindowSize(ImVec2(firstW * g_uiScale, firstH * g_uiScale), ImGuiCond_FirstUseEver);
    const bool shown = ImGui::Begin(title, open, flags);
    if (shown) {
        const ImVec2 p = ImGui::GetCursorScreenPos(), a = fit ? *fit : ImGui::GetContentRegionAvail();
        draw(p, ImVec2(std::max(a.x, 8.0f), std::max(a.y, 8.0f)));
        if (fit) ImGui::Dummy(*fit);   // the window fits itself around the body
    }
    ImGui::End();
    return shown;
}
// A section: the body drawn at the cursor, then a placeholder as tall as it drew.
template <class Draw>
bool PanelSection(const char*, Draw draw) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(ImGui::GetContentRegionAvail().x, 8.0f);
    OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
    const float h = draw(p.x, p.y, w, 20000.0f);
    OkayUI::PopTextScale();
    ImGui::Dummy(ImVec2(w, (h > 1.0f && h < 20000.0f) ? h : 8.0f));
    return true;
}
} // namespace
#else
// A build without OkayUI (no okay_ui library): these windows do not exist there.
bool DrawCrashLogOkayUI(bool*, const std::string&, const std::string&, std::uint64_t, const CrashLogActions&) { return false; }
bool DrawAudioMixerOkayUI(bool*) { return false; }
bool DrawHistoryOkayUI(bool*, const std::function<void(okaypanels::HistoryModel&)>&, okaypanels::HistoryIntent&) { return false; }
bool DrawProblemsOkayUI(const char*, bool*, const okaypanels::ProblemsModel&, okaypanels::ProblemsState&, okaypanels::ProblemsIntent&, ProblemsRects*) { return false; }
bool DrawStatsOkayUI(bool*, const std::function<void(okaypanels::StatsModel&)>&) { return false; }
bool DrawProfilerOkayUI(bool*, const std::function<void(okaypanels::ProfilerModel&)>&, okaypanels::ProfilerIntent&) { return false; }
bool DrawRenderingSettingsOkayUI(okaypanels::RenderingSettingsModel&, okaypanels::RenderingSettingsIntent&) { return false; }
bool DrawPlayerSettingsOkayUI(okaypanels::PlayerSettingsModel&, okaypanels::PlayerSettingsIntent&) { return false; }
bool DrawPhysicsSettingsOkayUI(okaypanels::PhysicsSettingsModel&, okaypanels::PhysicsSettingsIntent&) { return false; }
bool DrawTagsLayersOkayUI(const okaypanels::TagsLayersModel&, okaypanels::TagsLayersState&, okaypanels::TagsLayersIntent&) { return false; }
bool DrawLoadingScreenOkayUI(okaypanels::LoadingScreenModel&, okaypanels::LoadingScreenIntent&) { return false; }
bool DrawImportSettingsOkayUI(const char*, okaypanels::ImportSettingsModel&, okaypanels::ImportSettingsState&, okaypanels::ImportSettingsIntent&) { return false; }
bool DrawPostProcessOkayUI(okaypanels::PostProcessModel&, okaypanels::PostProcessIntent&) { return false; }
bool DrawShortcutsOkayUI(bool*) { return false; }
bool DrawNetSimOkayUI(bool*, okaypanels::NetSimModel&, okaypanels::NetSimIntent& intent) { intent = okaypanels::NetSimIntent(); return false; }
bool DrawFindInFilesOkayUI(bool*, const okaypanels::FindInFilesModel&, okaypanels::FindInFilesState&, okaypanels::FindInFilesIntent& intent) {
    intent = okaypanels::FindInFilesIntent(); return false;
}
bool DrawDataAssetOkayUI(bool*, okaypanels::DataAssetModel&, okaypanels::DataAssetIntent& intent) { intent = okaypanels::DataAssetIntent(); return false; }
bool DrawPluginManagerOkayUI(bool*, const okaypanels::PluginManagerModel&, okaypanels::PluginManagerIntent& intent) {
    intent = okaypanels::PluginManagerIntent(); return false;
}
bool DrawBoneMapOkayUI(bool*, okaypanels::BoneMapModel&, okaypanels::BoneMapIntent& intent) { intent = okaypanels::BoneMapIntent(); return false; }
bool DrawSaveManagerOkayUI(bool*, okaypanels::SaveManagerModel&, const std::function<void(okaypanels::SaveManagerModel&)>&,
                           okaypanels::SaveManagerState&, okaypanels::SaveManagerIntent& intent) { intent = okaypanels::SaveManagerIntent(); return false; }
bool DrawMaterialOkayUI(bool*, okaypanels::MaterialModel&, okaypanels::MaterialIntent& intent) { intent = okaypanels::MaterialIntent(); return false; }
bool DrawPerfOverlayOkayUI(bool*, okaypanels::PerfOverlayModel&, const std::function<void(okaypanels::PerfOverlayModel&)>&,
                           okaypanels::PerfOverlayIntent& intent) { intent = okaypanels::PerfOverlayIntent(); return false; }
bool DrawInputPanelOkayUI(bool*, okaypanels::InputPanelModel&, const std::function<void(okaypanels::InputPanelModel&)>&,
                          okaypanels::InputPanelState&, okaypanels::InputPanelIntent& intent) { intent = okaypanels::InputPanelIntent(); return false; }
bool DrawTimersOkayUI(bool*, const std::function<std::string()>&) { return false; }
bool DrawCommandLineOkayUI(bool*, const std::string&) { return false; }
bool DrawInputActionsOkayUI(bool*, okaypanels::InputActionsModel&, const std::function<void(okaypanels::InputActionsModel&)>&,
                            okaypanels::InputActionsState&, okaypanels::InputActionsIntent& intent) { intent = okaypanels::InputActionsIntent(); return false; }
#endif
bool DrawHierarchyOkayUI(bool*, okaypanels::HierarchyModel&, okaypanels::HierarchyState&, okaypanels::HierarchyIntent&) { return false; }
bool DrawInspectorHeaderOkayUI(okaypanels::InspectorHeaderModel&, okaypanels::InspectorHeaderState&, okaypanels::InspectorHeaderIntent&) { return false; }
bool DrawCompHeaderOkayUI(const char*, const okaypanels::CompHeaderModel&, okaypanels::CompHeaderIntent&, bool*) { return false; }
bool DrawTransformOkayUI(okaypanels::TransformModel&, okaypanels::TransformIntent&, int*) { return false; }
bool DrawMeshRendererOkayUI(okaypanels::MeshRendererModel&, okaypanels::MeshRendererIntent&) { return false; }
bool DrawAddComponentOkayUI(std::uint32_t, bool& clicked) { clicked = false; return false; }
bool DrawFieldListOkayUI(const char*, okaypanels::InspFieldList&) { return false; }
bool OkayUIHasKeyboard() { return false; }
}
#endif

#if defined(OKAY_HAVE_OKAYUI) || defined(OKAY_ON_OKAYUI)
// ---- The panels drawn by OkayUI only (docs/ui/own_ui_toolkit.md sections 24 and 25): no Dear ImGui version exists. -------------
namespace oked {

bool DrawCrashLogOkayUI(bool* open, const std::string& text, const std::string& path, std::uint64_t revision, const CrashLogActions& actions) {
    static okayuiscreens::CrashLogState state;
    okayuiscreens::CrashLogModel model;
    model.text = text; model.path = path; model.revision = revision;   // (a copy per frame: crash logs are small)
    const okayuiscreens::CrashLogHost host = CrashLogHostOf(actions);
    return PanelWindow("Crash Log", open, "##okayui_crashlog", [&](ImVec2 p, ImVec2 a) {
        okayuiscreens::DrawCrashLog(model, state, host, p.x, p.y, a.x, a.y);
    });
}

bool DrawAudioMixerOkayUI(bool* open) {
    okayuiscreens::AudioMixerModel m;
    FillAudioMixerModel(m);
    return PanelWindow("Audio Mixer", open, "##okayui_audiomixer", [&](ImVec2 p, ImVec2 a) {
        okayuiscreens::DrawAudioMixer(m, p.x, p.y, a.x, a.y);
    });
}

// The Problems, Stats, History and Profiler panels: the editor's 18-pixel text on top of the UI scale (the size their Dear ImGui
// versions had).
bool DrawHistoryOkayUI(bool* open, const std::function<void(okaypanels::HistoryModel&)>& fill, okaypanels::HistoryIntent& intent) {
    intent = okaypanels::HistoryIntent();
    return PanelWindow("History", open, "##okayui_history", [&](ImVec2 p, ImVec2 a) {
        okaypanels::HistoryModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawHistory(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        CopyHistoryRow(model, intent);
    });
}

bool DrawProblemsOkayUI(const char* title, bool* open, const okaypanels::ProblemsModel& model, okaypanels::ProblemsState& state,
                        okaypanels::ProblemsIntent& intent, ProblemsRects* rects) {
    intent = okaypanels::ProblemsIntent();
    if (rects) *rects = ProblemsRects();
    return PanelWindow(title, open, "##okayui_problems", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okayuiscreens::ProblemsResult r = okayuiscreens::DrawProblems(model, state, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
        intent = r.intent;
        if (rects) {
            rects->rowIndex = r.rowIndex;
            for (const OkayUI::Rect& q : r.rows) rects->rows.push_back(PanelRect{q.x, q.y, q.w, q.h});
            for (const OkayUI::Rect& q : r.fixButtons) rects->fixButtons.push_back(PanelRect{q.x, q.y, q.w, q.h});
        }
    });
}

bool DrawStatsOkayUI(bool* open, const std::function<void(okaypanels::StatsModel&)>& fill) {
    return PanelWindow("Stats", open, "##okayui_stats", [&](ImVec2 p, ImVec2 a) {
        okaypanels::StatsModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okaypanels::StatsIntent intent = okayuiscreens::DrawStats(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        if (intent.copy) ImGui::SetClipboardText(okaypanels::StatsCopyText(model).c_str());
    });
}

bool DrawProfilerOkayUI(bool* open, const std::function<void(okaypanels::ProfilerModel&)>& fill, okaypanels::ProfilerIntent& intent) {
    intent = okaypanels::ProfilerIntent();
    return PanelWindow("Profiler", open, "##okayui_profiler", [&](ImVec2 p, ImVec2 a) {
        okaypanels::ProfilerModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawProfiler(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        if (intent.copyCsv) ImGui::SetClipboardText(okaypanels::ProfilerCsv(model).c_str());
    });
}

// Project Settings' Player, Physics, Rendering and Tags and Layers sections (docs/ui/own_ui_toolkit.md section 26): OkayUI only, each
// in its place under its collapsing header inside the Dear ImGui window.
bool DrawTagsLayersOkayUI(const okaypanels::TagsLayersModel& model, okaypanels::TagsLayersState& state, okaypanels::TagsLayersIntent& intent) {
    intent = okaypanels::TagsLayersIntent();
    return PanelSection("##okayui_tagslayers", [&](float x, float y, float w, float h) {
        const okayuiscreens::TagsLayersResult r = okayuiscreens::DrawTagsLayers(model, state, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawPhysicsSettingsOkayUI(okaypanels::PhysicsSettingsModel& model, okaypanels::PhysicsSettingsIntent& intent) {
    intent = okaypanels::PhysicsSettingsIntent();
    return PanelSection("##okayui_physics", [&](float x, float y, float w, float h) {
        const okayuiscreens::PhysicsSettingsResult r = okayuiscreens::DrawPhysicsSettings(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawPlayerSettingsOkayUI(okaypanels::PlayerSettingsModel& model, okaypanels::PlayerSettingsIntent& intent) {
    intent = okaypanels::PlayerSettingsIntent();
    return PanelSection("##okayui_player", [&](float x, float y, float w, float h) {
        const okayuiscreens::PlayerSettingsResult r = okayuiscreens::DrawPlayerSettings(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawRenderingSettingsOkayUI(okaypanels::RenderingSettingsModel& model, okaypanels::RenderingSettingsIntent& intent) {
    intent = okaypanels::RenderingSettingsIntent();
    return PanelSection("##okayui_rendering", [&](float x, float y, float w, float h) {
        const okayuiscreens::RenderingSettingsResult r = okayuiscreens::DrawRenderingSettings(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawLoadingScreenOkayUI(okaypanels::LoadingScreenModel& model, okaypanels::LoadingScreenIntent& intent) {
    intent = okaypanels::LoadingScreenIntent();
    return PanelSection("##okayui_loadingscreen", [&](float x, float y, float w, float h) {
        const okayuiscreens::LoadingScreenResult r = okayuiscreens::DrawLoadingScreen(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

// The Project panel's import settings (shown in the Project panel and in the Inspector for the selected asset; `id` keeps the two
// apart) and Lighting > Post-processing (docs/ui/own_ui_toolkit.md section 27): OkayUI only, like the Project Settings sections.
bool DrawImportSettingsOkayUI(const char* id, okaypanels::ImportSettingsModel& model, okaypanels::ImportSettingsState& state,
                              okaypanels::ImportSettingsIntent& intent) {
    intent = okaypanels::ImportSettingsIntent();
    return PanelSection(id, [&](float x, float y, float w, float h) {
        const okayuiscreens::ImportSettingsResult r = okayuiscreens::DrawImportSettings(model, state, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

bool DrawPostProcessOkayUI(okaypanels::PostProcessModel& model, okaypanels::PostProcessIntent& intent) {
    intent = okaypanels::PostProcessIntent();
    return PanelSection("##okayui_post", [&](float x, float y, float w, float h) {
        const okayuiscreens::PostProcessResult r = okayuiscreens::DrawPostProcess(model, x, y, w, h);
        intent = r.intent;
        return r.contentHeight;
    });
}

// The small tool windows (docs/ui/own_ui_toolkit.md section 28): OkayUI only, each a whole window like Stats. The titles are the
// ones the Dear ImGui windows had, so saved layouts keep their place.
bool DrawShortcutsOkayUI(bool* open) {
    static okaypanels::ShortcutsState state;
    return PanelWindow("Keyboard Shortcuts", open, "##okayui_shortcuts", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        okayuiscreens::DrawShortcuts(okaypanels::KeyboardShortcuts(), state, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
    }, 520.0f, 560.0f);
}

bool DrawNetSimOkayUI(bool* open, okaypanels::NetSimModel& model, okaypanels::NetSimIntent& intent) {
    intent = okaypanels::NetSimIntent();
    return PanelWindow("Net###Net", open, "##okayui_net", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawNetSim(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    });
}

bool DrawFindInFilesOkayUI(bool* open, const okaypanels::FindInFilesModel& model, okaypanels::FindInFilesState& state,
                           okaypanels::FindInFilesIntent& intent) {
    intent = okaypanels::FindInFilesIntent();
    return PanelWindow("Find in Files", open, "##okayui_findinfiles", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawFindInFiles(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 600.0f, 420.0f);
}

bool DrawDataAssetOkayUI(bool* open, okaypanels::DataAssetModel& model, okaypanels::DataAssetIntent& intent) {
    intent = okaypanels::DataAssetIntent();
    return PanelWindow("Data Asset", open, "##okayui_dataasset", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawDataAsset(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    });
}

bool DrawPluginManagerOkayUI(bool* open, const okaypanels::PluginManagerModel& model, okaypanels::PluginManagerIntent& intent) {
    intent = okaypanels::PluginManagerIntent();
    return PanelWindow("Plugin Manager", open, "##okayui_plugins", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawPluginManager(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 560.0f, 360.0f);
}

// More tool windows (docs/ui/own_ui_toolkit.md section 29): OkayUI only, each a whole window. The titles are the Dear ImGui ones.
bool DrawBoneMapOkayUI(bool* open, okaypanels::BoneMapModel& model, okaypanels::BoneMapIntent& intent) {
    intent = okaypanels::BoneMapIntent();
    return PanelWindow("Map Humanoid Bones", open, "##okayui_bonemap", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawBoneMap(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 460.0f, 560.0f);
}

bool DrawSaveManagerOkayUI(bool* open, okaypanels::SaveManagerModel& model, const std::function<void(okaypanels::SaveManagerModel&)>& fill,
                           okaypanels::SaveManagerState& state, okaypanels::SaveManagerIntent& intent) {
    intent = okaypanels::SaveManagerIntent();
    return PanelWindow("Save Manager", open, "##okayui_savemanager", [&](ImVec2 p, ImVec2 a) {
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawSaveManager(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 560.0f, 480.0f);
}

bool DrawMaterialOkayUI(bool* open, okaypanels::MaterialModel& model, okaypanels::MaterialIntent& intent) {
    intent = okaypanels::MaterialIntent();
    return PanelWindow("Material", open, "##okayui_material", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okayuiscreens::MaterialResult r = okayuiscreens::DrawMaterial(model, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
        intent = r.intent;
#ifdef OKAY_HAVE_OKAYUI
        // an image dragged from the Project panel (a Dear ImGui drag) drops onto the texture field (OkayUI's)
        const ImVec2 o = ImGui::GetMainViewport()->Pos;
        const OkayUI::Rect& q = r.texture;
        if (q.w > 0.0f && q.h > 0.0f && ImGui::BeginDragDropTargetCustom(ImRect(o.x + q.x, o.y + q.y, o.x + q.x + q.w, o.y + q.y + q.h),
                                                                         ImGui::GetID("##mattexdrop"))) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_PATH")) { model.texture = (const char*)pl->Data; intent.changed = true; }
            ImGui::EndDragDropTarget();
        }
#endif
    });
}

bool DrawPerfOverlayOkayUI(bool* open, okaypanels::PerfOverlayModel& model, const std::function<void(okaypanels::PerfOverlayModel&)>& fill,
                           okaypanels::PerfOverlayIntent& intent) {
    intent = okaypanels::PerfOverlayIntent();
    static ImVec2 fit(400.0f, 300.0f);   // the body's size on the last frame (the window fits itself around it)
    if (open && !*open) return false;    // (before the next window's place is set: it would go to another window)
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12.0f, vp->WorkPos.y + 36.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    return PanelWindow("Frame time###PerfOverlay", open, "##okayui_perfoverlay", [&](ImVec2 p, ImVec2 a) {
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okayuiscreens::PerfOverlayResult r = okayuiscreens::DrawPerfOverlay(model, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
        intent = r.intent;
        if (r.contentW > 1.0f && r.contentH > 1.0f) fit = ImVec2(r.contentW, r.contentH);
    }, 400.0f, 300.0f, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav, &fit);
}

bool DrawInputPanelOkayUI(bool* open, okaypanels::InputPanelModel& model, const std::function<void(okaypanels::InputPanelModel&)>& fill,
                          okaypanels::InputPanelState& state, okaypanels::InputPanelIntent& intent) {
    intent = okaypanels::InputPanelIntent();
    return PanelWindow("Input", open, "##okayui_input", [&](ImVec2 p, ImVec2 a) {
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawInputPanel(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 520.0f, 560.0f);
}

// Timers and Input Actions (docs/ui/own_ui_toolkit.md section 30): OkayUI only, each a whole window, titled as before.
bool DrawTimersOkayUI(bool* open, const std::function<std::string()>& text) {
    return PanelWindow("Timers", open, "##okayui_timers", [&](ImVec2 p, ImVec2 a) {
        const std::string t = text ? text() : std::string();
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        okayuiscreens::DrawTimers(t, p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
    }, 560.0f, 360.0f);
}

// View > Panels > Command Line: the one Command Line panel (shared/CommandLinePanel.hpp), commands run inside the editor.
bool DrawCommandLineOkayUI(bool* open, const std::string& projectDir) {
#ifdef OKAY_HAVE_CLI_PANEL
    static okaycli::CommandLinePanel panel([] {
        okaycli::CommandLinePanel::Config c;
        c.idPrefix = "##okayui_cmdline";
        c.copy = [](const std::string& t) { ImGui::SetClipboardText(t.c_str()); };   // the whole text (OkayUI's clipboard keeps 4 KB)
        return c;
    }());
    panel.SetFolder(projectDir);
    return PanelWindow("Command Line", open, "##okayui_cmdline", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        panel.Frame(p.x, p.y, a.x, a.y);
        OkayUI::PopTextScale();
    }, 640.0f, 320.0f);
#else
    (void)open; (void)projectDir;
    return false;
#endif
}

bool DrawInputActionsOkayUI(bool* open, okaypanels::InputActionsModel& model, const std::function<void(okaypanels::InputActionsModel&)>& fill,
                            okaypanels::InputActionsState& state, okaypanels::InputActionsIntent& intent) {
    intent = okaypanels::InputActionsIntent();
    return PanelWindow("Input Actions", open, "##okayui_inputactions", [&](ImVec2 p, ImVec2 a) {
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawInputActions(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, 900.0f, 600.0f);
}

} // namespace oked
#endif
