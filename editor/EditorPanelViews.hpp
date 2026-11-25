#pragma once
// The Dear ImGui bodies of the Problems, Stats, Tags and Layers, Physics, Profiler, Import settings, Post-processing, Player and
// Rendering panels and sections, drawn from the models in shared/EditorPanelModels.hpp.
// Not compiled into the editor (section 25 of docs/ui/own_ui_toolkit.md): only the headless comparison tests call these, in a bare
// window, as the Dear ImGui reference the OkayUI ports are measured against. Only Dear ImGui and the model header: no EditorState.
#include "EditorPanelModels.hpp"

#include <cstddef>
#include <cstdint>

struct ImFont;

namespace oked {

/// What the bodies borrow from the editor. In the editor: the editor's own search field (with its magnifier and clear
/// button) and its heading font. A null searchField falls back to a plain InputTextWithHint; a null heading font uses the
/// current font.
struct PanelViewHooks {
    bool (*searchField)(const char* id, char* buf, std::size_t bufSize, const char* hint, float width) = nullptr;
    ImFont* headingFont = nullptr;
    std::uint32_t accent = 0xFFCC793Cu;   // the accent colour packed as IM_COL32 (the editor's first accent, 0.235 0.475 0.800)
};

okaypanels::ProblemsIntent DrawProblemsBodyImGui(const okaypanels::ProblemsModel& m, okaypanels::ProblemsState& st,
                                                 const PanelViewHooks& hooks);
okaypanels::StatsIntent DrawStatsBodyImGui(okaypanels::StatsModel& m, const PanelViewHooks& hooks);
/// Project Settings > Tags and Layers (inside its collapsing header).
okaypanels::TagsLayersIntent DrawTagsLayersBodyImGui(const okaypanels::TagsLayersModel& m, okaypanels::TagsLayersState& st,
                                                     const PanelViewHooks& hooks);
/// Project Settings > Physics (inside its collapsing header).
okaypanels::PhysicsSettingsIntent DrawPhysicsSettingsBodyImGui(okaypanels::PhysicsSettingsModel& m, const PanelViewHooks& hooks);
/// The Profiler window's body.
okaypanels::ProfilerIntent DrawProfilerBodyImGui(okaypanels::ProfilerModel& m, const PanelViewHooks& hooks);
/// The Project panel's import settings for the selected texture, model or sound.
okaypanels::ImportSettingsIntent DrawImportSettingsBodyImGui(okaypanels::ImportSettingsModel& m, okaypanels::ImportSettingsState& st,
                                                             const PanelViewHooks& hooks);
/// Lighting > Post-processing (inside its collapsing header).
okaypanels::PostProcessIntent DrawPostProcessBodyImGui(okaypanels::PostProcessModel& m, const PanelViewHooks& hooks);
/// Project Settings > Player and > Rendering (each inside its collapsing header).
okaypanels::PlayerSettingsIntent DrawPlayerSettingsBodyImGui(okaypanels::PlayerSettingsModel& m, const PanelViewHooks& hooks);
okaypanels::RenderingSettingsIntent DrawRenderingSettingsBodyImGui(okaypanels::RenderingSettingsModel& m, const PanelViewHooks& hooks);
okaypanels::LoadingScreenIntent DrawLoadingScreenBodyImGui(okaypanels::LoadingScreenModel& m, const PanelViewHooks& hooks);

/// The Hierarchy panel's body (docs/own_ui_toolkit.md section 12), drawn from the shared model like the OkayUI port: only the rows in
/// view are asked for. The editor itself still draws its own Hierarchy (editor/main.cpp DrawHierarchy); this body is the same panel
/// written against the model, which the comparison test runs against the OkayUI port. Rename is in place here, as in the port (the
/// editor's own body opens a "Rename Object" box).
okaypanels::HierarchyIntent DrawHierarchyBodyImGui(const okaypanels::HierarchyModel& m, okaypanels::HierarchyState& st,
                                                   const PanelViewHooks& hooks);

/// The Inspector, stage 1 (docs/own_ui_toolkit.md section 14): the object header card, a component's header bar, the Transform, the
/// Mesh Renderer's shading fields and the Add Component button (true when clicked), drawn from shared/InspectorPanelModel.hpp. The editor
/// still draws its own Inspector (editor/main.cpp DrawInspector); these are the same parts written against the model, which the
/// comparison test runs against the OkayUI port.
okaypanels::InspectorHeaderIntent DrawInspectorHeaderBodyImGui(okaypanels::InspectorHeaderModel& m, okaypanels::InspectorHeaderState& st,
                                                               const PanelViewHooks& hooks);
okaypanels::CompHeaderIntent DrawComponentHeaderBodyImGui(const okaypanels::CompHeaderModel& m, const PanelViewHooks& hooks);
okaypanels::TransformIntent DrawTransformBodyImGui(okaypanels::TransformModel& m, const PanelViewHooks& hooks);
okaypanels::MeshRendererIntent DrawMeshRendererBodyImGui(okaypanels::MeshRendererModel& m, const PanelViewHooks& hooks);
bool DrawAddComponentButtonImGui(const PanelViewHooks& hooks);

/// The Inspector, stage 2 (docs/own_ui_toolkit.md section 15): a component body as field rows (shared/InspectorFieldsModel.hpp), drawn
/// with the same Dear ImGui widgets the editor's component code uses. The editor still draws its own bodies; this is the same rows
/// written against the model, which the comparison test runs against the OkayUI port.
void DrawFieldListBodyImGui(okaypanels::InspFieldList& list, const PanelViewHooks& hooks);

} // namespace oked
