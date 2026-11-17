#pragma once
// Editor windows hosted by OkayUI (the opt-in "OkayUI (preview)" UI backend, docs/own_ui_toolkit.md sections 6, 9, 10 and 11): the Crash
// Log, the Audio Mixer, the Problems, Stats, History and Profiler panels, Project Settings' Tags and Layers, Physics, Player and Rendering
// sections, the Project panel's import settings and the Lighting window's Post-processing section. Each keeps its Dear ImGui window (the dock tab, title and close box) and hands the free area to the OkayUI screen in
// okayui/screens/OkayUIPanels. Only compiled into the editor when OkayUI is (OKAY_HAVE_OKAYUI).
#include "EditorPanelModels.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace oked {

struct CrashLogActions {
    std::function<void()> refresh, openFolder, clear;   // the host reloads, reveals the folder, deletes the files
};

/// Draw the Crash Log window with OkayUI. Returns false, drawing nothing, when an ImGui popup covers the window this frame (OkayUI
/// draws over every ImGui window, so the caller draws the Dear ImGui version then). `revision` goes up whenever `text` changed.
bool DrawCrashLogOkayUI(bool* open, const std::string& text, const std::string& path, std::uint64_t revision, const CrashLogActions& actions);

/// Draw the Audio Mixer window with OkayUI, editing okay::AudioMixer in place. Same return value as above.
bool DrawAudioMixerOkayUI(bool* open);

/// The Problems, Stats and History panels with OkayUI (okayui/screens/OkayUIEditorPanels): the same models the Dear ImGui bodies in
/// EditorPanelViews draw, the same intents back. Same return value as above (false: draw the Dear ImGui version this frame). `title`
/// is the ImGui window name (Problems carries its error count in it).
bool DrawProblemsOkayUI(const char* title, bool* open, const okaypanels::ProblemsModel& model, okaypanels::ProblemsState& state,
                        okaypanels::ProblemsIntent& intent);
/// Stats fills its model only while the window is visible, through `fill`; "Copy stats" goes to the clipboard here.
bool DrawStatsOkayUI(bool* open, const std::function<void(okaypanels::StatsModel&)>& fill);
bool DrawHistoryOkayUI(bool* open, const okaypanels::HistoryModel& model, okaypanels::HistoryIntent& intent);

/// Project Settings' Tags and Layers and Physics sections with OkayUI, drawn inside the current ImGui window (under their collapsing
/// header) in a placeholder as tall as the section was on the last frame; only the part of it the window shows is drawn and takes
/// the mouse. Same return value as above.
bool DrawTagsLayersOkayUI(const okaypanels::TagsLayersModel& model, okaypanels::TagsLayersState& state, okaypanels::TagsLayersIntent& intent);
bool DrawPhysicsSettingsOkayUI(okaypanels::PhysicsSettingsModel& model, okaypanels::PhysicsSettingsIntent& intent);
/// The Profiler window with OkayUI. It fills its model only while the window is visible, through `fill`.
bool DrawProfilerOkayUI(bool* open, const std::function<void(okaypanels::ProfilerModel&)>& fill, okaypanels::ProfilerIntent& intent);
/// Sections drawn inside the current ImGui window like Tags and Layers above: the Project panel's import settings, Lighting >
/// Post-processing, and Project Settings > Player and > Rendering. `id` keeps two hosts of the same section apart (the import settings
/// are shown in two places). Same return value as above.
bool DrawImportSettingsOkayUI(const char* id, okaypanels::ImportSettingsModel& model, okaypanels::ImportSettingsState& state,
                              okaypanels::ImportSettingsIntent& intent);
bool DrawPostProcessOkayUI(okaypanels::PostProcessModel& model, okaypanels::PostProcessIntent& intent);
bool DrawPlayerSettingsOkayUI(okaypanels::PlayerSettingsModel& model, okaypanels::PlayerSettingsIntent& intent);
bool DrawRenderingSettingsOkayUI(okaypanels::RenderingSettingsModel& model, okaypanels::RenderingSettingsIntent& intent);

/// The Hierarchy panel with OkayUI (docs/own_ui_toolkit.md section 12). The window keeps the keyboard focus flag (`model.focused` is
/// set here from the ImGui window). A file dragged in from a Dear ImGui panel arrives through model.externalDrag*, which the caller
/// fills from ImGui's payload; a row dragged out of the panel is handed on to Dear ImGui as its "GO_PTR" payload (the Inspector's
/// object fields take it). Same return value as above.
bool DrawHierarchyOkayUI(bool* open, okaypanels::HierarchyModel& model, okaypanels::HierarchyState& state, okaypanels::HierarchyIntent& intent);

/// The Inspector, stage 1 (docs/own_ui_toolkit.md section 14), each part a section inside the current ImGui window (the Inspector), drawn
/// where the Dear ImGui part would be: the object header card, a component's header bar, the Transform, the Mesh Renderer's shading
/// fields and the Add Component button. Same return value as above (false: draw the Dear ImGui part this frame). `barPressed`: the left
/// button went down on the bar this frame (the host starts its "drag onto the visual script" source then).
bool DrawInspectorHeaderOkayUI(okaypanels::InspectorHeaderModel& model, okaypanels::InspectorHeaderState& state, okaypanels::InspectorHeaderIntent& intent);
bool DrawCompHeaderOkayUI(const char* windowId, const okaypanels::CompHeaderModel& model, okaypanels::CompHeaderIntent& intent, bool* barPressed);
/// `pressedOn` (the Transform): the left button went down on 1 the header, 2 Position's label, 3 Rotation's, 4 Scale's (0 nothing).
bool DrawTransformOkayUI(okaypanels::TransformModel& model, okaypanels::TransformIntent& intent, int* pressedOn);
bool DrawMeshRendererOkayUI(okaypanels::MeshRendererModel& model, okaypanels::MeshRendererIntent& intent);
/// Stage 2 (docs/own_ui_toolkit.md section 15): a component body as field rows, an OkayUI section inside the Inspector window. `id`
/// names the section (unique within the current ImGui id scope). A file dragged from the Project panel is offered to the rows that
/// take one (dropKind): their outline shows while it is dragged, and a drop fills the row's *s and marks it changed. False: not
/// drawn this frame (the caller draws the Dear ImGui body).
bool DrawFieldListOkayUI(const char* id, okaypanels::InspFieldList& list);
/// `clicked`: the button was clicked (the host opens its Add Component browser).
bool DrawAddComponentOkayUI(std::uint32_t accent, bool& clicked);
/// A hosted OkayUI field has the keyboard (the Inspector's undo watcher keeps its step open while one is typed in).
bool OkayUIHasKeyboard();

} // namespace oked
