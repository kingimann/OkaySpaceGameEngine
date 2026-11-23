#include "OkayPanels.hpp"
#include "MainShared.hpp"

#ifdef OKAY_HAVE_OKAYUI
#include "imgui.h"
#include "imgui_internal.h"
#include "OkayUIHost.hpp"
#include "OkayUIHostImGui.hpp"
#include "OkayUIPanels.hpp"
#include "OkayUIEditorPanels.hpp"
#include "EditorPanelViews.hpp"
#include "okay/Audio/AudioMixer.hpp"
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

// The ImGui window stays: it is the dock tab and the close box, and hands its free area to OkayUI as one rectangle (the same
// arrangement as the Console panel). Returns false when an ImGui popup or another ImGui window in front covers the window (OkayUI
// is drawn after ImGui and would paint over it), and the caller then draws the Dear ImGui version, which ImGui layers correctly.
//
// The body is one placeholder as big as the free area, so ImGui cannot fit a new floating window to an OkayUI body. `fitBody`, when
// given, is the panel's Dear ImGui body: a window that does not exist yet and has no saved place is fitted by Dear ImGui to what its
// first frame holds, and that frame is never shown, so the first frame draws the Dear ImGui body and the window opens exactly as big
// as the Dear ImGui version does; OkayUI draws from the next frame on. Without `fitBody` a new window gets a fixed first size.
template <class Draw>
bool HostWindow(const char* title, bool* open, const char* itemId, Draw draw, const std::function<void()>& fitBody = {}) {
    const ImGuiWindow* cw = ImGui::FindWindowByName(title);
    if (cw && okayuihost::CoveredInImGui(cw, cw->Pos.x, cw->Pos.y, cw->Size.x, cw->Size.y)) return false;   // a popup or window in front
    if (okayuihost::OutsideMainWindow(cw)) return false;
    if (fitBody && !cw && !ImGui::FindWindowSettingsByID(ImHashStr(title))) {
        if (ImGui::Begin(title, open)) fitBody();   // the hidden first frame: Dear ImGui measures it, nobody sees it
        ImGui::End();
        return true;
    }
    if (!fitBody) ImGui::SetNextWindowSize(ImVec2(440.0f * g_uiScale, 340.0f * g_uiScale), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 p = okayuihost::ToMainWindowPixels(ImGui::GetCursorScreenPos());   // OkayUI's pixels, not ImGui's desktop ones
        ImVec2 a = ImGui::GetContentRegionAvail();
        a.x = std::max(a.x, 8.0f); a.y = std::max(a.y, 8.0f);
        ImGui::InvisibleButton(itemId, a);               // an ImGui item, so ImGui does not move or scroll the window under it
        if (okayuihost::HiddenInImGui(ImGui::GetCurrentWindow())) { ImGui::End(); return true; }   // a frame ImGui keeps hidden: nothing shows
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);    // the wheel scrolls the OkayUI body, not the ImGui window
        OkayUIRegion().Offer(p.x, p.y, a.x, a.y, ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
        OkayUI::PushTextScale(g_uiScale);
        MetricsFromImGui metrics_;
        const int overlays = OkayUI::GetOverlayCount();
        draw(p, a);
        OkayUI::PopTextScale();
        for (int i = overlays; i < OkayUI::GetOverlayCount(); ++i) {   // a menu or a list the body opened hangs out of the window: it takes the mouse
            const OkayUI::Rect r = OkayUI::GetOverlayRect(i);
            if (r.w > 0.0f && r.h > 0.0f) OkayUIRegion().Offer(r.x, r.y, r.w, r.h, true, true);
        }
    }
    ImGui::End();
    return true;
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

} // namespace

bool DrawCrashLogOkayUI(bool* open, const std::string& text, const std::string& path, std::uint64_t revision, const CrashLogActions& actions) {
    static okayuiscreens::CrashLogState state;
    okayuiscreens::CrashLogModel model;
    model.text = text; model.path = path; model.revision = revision;   // (a copy per frame: crash logs are small)
    okayuiscreens::CrashLogHost host;
    host.refresh = actions.refresh;
    host.openFolder = actions.openFolder;
    host.clear = actions.clear;
    host.copyAll = [](const std::string& s) { ImGui::SetClipboardText(s.c_str()); };
    return HostWindow("Crash Log", open, "##okayui_crashlog", [&](ImVec2 p, ImVec2 a) {
        okayuiscreens::DrawCrashLog(model, state, host, p.x, p.y, a.x, a.y);
    });
}

bool DrawAudioMixerOkayUI(bool* open) {
    using okay::AudioMixer;
    using okay::AudioGroup;
    okayuiscreens::AudioMixerModel m;
    m.peakLeft = AudioMixer::peak[0]; m.peakRight = AudioMixer::peak[1];
    m.masterVolume = &AudioMixer::masterVolume; m.masterMuted = &AudioMixer::muted;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        m.groupNames.push_back(AudioMixer::GroupName((AudioGroup)i));
        m.groupVolume.push_back(&AudioMixer::groupVolume[i]);
        m.groupMuted.push_back(&AudioMixer::groupMuted[i]);
    }
    m.duckMusic = &AudioMixer::duckMusicUnderVoice; m.duckLevel = &AudioMixer::duckLevel;
    m.duckAttack = &AudioMixer::duckAttack; m.duckRelease = &AudioMixer::duckRelease; m.duckGain = AudioMixer::duckGain;
    return HostWindow("Audio Mixer", open, "##okayui_audiomixer", [&](ImVec2 p, ImVec2 a) {
        okayuiscreens::DrawAudioMixer(m, p.x, p.y, a.x, a.y);
    });
}

// The Problems, Stats and History panels: the editor's 18-pixel text on top of the UI scale (the Dear ImGui versions draw at that size).
bool DrawProblemsOkayUI(const char* title, bool* open, const okaypanels::ProblemsModel& model, okaypanels::ProblemsState& state,
                        okaypanels::ProblemsIntent& intent) {
    return HostWindow(title, open, "##okayui_problems", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawProblems(model, state, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    });
}

bool DrawStatsOkayUI(bool* open, const std::function<void(okaypanels::StatsModel&)>& fill) {
    return HostWindow("Stats", open, "##okayui_stats", [&](ImVec2 p, ImVec2 a) {
        okaypanels::StatsModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        const okaypanels::StatsIntent intent = okayuiscreens::DrawStats(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        if (intent.copy) ImGui::SetClipboardText(okaypanels::StatsCopyText(model).c_str());
    });
}

bool DrawHistoryOkayUI(bool* open, const okaypanels::HistoryModel& model, okaypanels::HistoryIntent& intent) {
    return HostWindow("History", open, "##okayui_history", [&](ImVec2 p, ImVec2 a) {
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawHistory(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
    }, [&] { DrawHistoryBodyImGui(model, PanelViewHooks()); });
}

bool DrawTagsLayersOkayUI(const okaypanels::TagsLayersModel& model, okaypanels::TagsLayersState& state, okaypanels::TagsLayersIntent& intent) {
    return HostSection("##okayui_tagslayers", [&](float x, float y, float w, float h) {
        const okayuiscreens::TagsLayersResult r = okayuiscreens::DrawTagsLayers(model, state, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    });
}

bool DrawPhysicsSettingsOkayUI(okaypanels::PhysicsSettingsModel& model, okaypanels::PhysicsSettingsIntent& intent) {
    return HostSection("##okayui_physics", [&](float x, float y, float w, float h) {
        const okayuiscreens::PhysicsSettingsResult r = okayuiscreens::DrawPhysicsSettings(model, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    });
}

bool DrawImportSettingsOkayUI(const char* id, okaypanels::ImportSettingsModel& model, okaypanels::ImportSettingsState& state,
                              okaypanels::ImportSettingsIntent& intent) {
    return HostSection(id, [&](float x, float y, float w, float h) {
        const okayuiscreens::ImportSettingsResult r = okayuiscreens::DrawImportSettings(model, state, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    });
}

bool DrawPostProcessOkayUI(okaypanels::PostProcessModel& model, okaypanels::PostProcessIntent& intent) {
    return HostSection("##okayui_post", [&](float x, float y, float w, float h) {
        const okayuiscreens::PostProcessResult r = okayuiscreens::DrawPostProcess(model, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, r.popup};
    });
}

bool DrawPlayerSettingsOkayUI(okaypanels::PlayerSettingsModel& model, okaypanels::PlayerSettingsIntent& intent) {
    return HostSection("##okayui_player", [&](float x, float y, float w, float h) {
        const okayuiscreens::PlayerSettingsResult r = okayuiscreens::DrawPlayerSettings(model, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, OkayUI::Rect{}};
    });
}

bool DrawRenderingSettingsOkayUI(okaypanels::RenderingSettingsModel& model, okaypanels::RenderingSettingsIntent& intent) {
    return HostSection("##okayui_rendering", [&](float x, float y, float w, float h) {
        const okayuiscreens::RenderingSettingsResult r = okayuiscreens::DrawRenderingSettings(model, x, y, w, h);
        intent = r.intent;
        return SectionDrawn{r.contentHeight, OkayUI::Rect{}};
    });
}

bool DrawProfilerOkayUI(bool* open, const std::function<void(okaypanels::ProfilerModel&)>& fill, okaypanels::ProfilerIntent& intent) {
    return HostWindow("Profiler", open, "##okayui_profiler", [&](ImVec2 p, ImVec2 a) {
        okaypanels::ProfilerModel model;
        fill(model);
        OkayUI::PushTextScale(okayuiscreens::kEditorTextScale);
        intent = okayuiscreens::DrawProfiler(model, p.x, p.y, a.x, a.y).intent;
        OkayUI::PopTextScale();
        if (intent.copyCsv) ImGui::SetClipboardText(okaypanels::ProfilerCsv(model).c_str());
    }, [&] { okaypanels::ProfilerModel model; fill(model); DrawProfilerBodyImGui(model, PanelViewHooks()); });
}

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
bool DrawCrashLogOkayUI(bool*, const std::string&, const std::string&, std::uint64_t, const CrashLogActions&) { return false; }
bool DrawAudioMixerOkayUI(bool*) { return false; }
bool DrawProblemsOkayUI(const char*, bool*, const okaypanels::ProblemsModel&, okaypanels::ProblemsState&, okaypanels::ProblemsIntent&) { return false; }
bool DrawStatsOkayUI(bool*, const std::function<void(okaypanels::StatsModel&)>&) { return false; }
bool DrawHistoryOkayUI(bool*, const okaypanels::HistoryModel&, okaypanels::HistoryIntent&) { return false; }
bool DrawTagsLayersOkayUI(const okaypanels::TagsLayersModel&, okaypanels::TagsLayersState&, okaypanels::TagsLayersIntent&) { return false; }
bool DrawPhysicsSettingsOkayUI(okaypanels::PhysicsSettingsModel&, okaypanels::PhysicsSettingsIntent&) { return false; }
bool DrawProfilerOkayUI(bool*, const std::function<void(okaypanels::ProfilerModel&)>&, okaypanels::ProfilerIntent&) { return false; }
bool DrawImportSettingsOkayUI(const char*, okaypanels::ImportSettingsModel&, okaypanels::ImportSettingsState&, okaypanels::ImportSettingsIntent&) { return false; }
bool DrawPostProcessOkayUI(okaypanels::PostProcessModel&, okaypanels::PostProcessIntent&) { return false; }
bool DrawPlayerSettingsOkayUI(okaypanels::PlayerSettingsModel&, okaypanels::PlayerSettingsIntent&) { return false; }
bool DrawRenderingSettingsOkayUI(okaypanels::RenderingSettingsModel&, okaypanels::RenderingSettingsIntent&) { return false; }
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
