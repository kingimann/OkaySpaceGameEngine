#pragma once
// The Dear ImGui bodies of the Problems, Stats, History, Tags and Layers, Physics, Profiler, Import settings, Post-processing, Player and
// Rendering panels and sections, drawn from the models in shared/EditorPanelModels.hpp.
// The editor opens the window (title, dock tab, close box) and calls a body inside it; the headless comparison test calls
// the same body in a bare window, with no editor around it. Only Dear ImGui and the model header: no EditorState.
#include "EditorPanelModels.hpp"

#include <cstddef>

struct ImFont;

namespace oked {

/// What the bodies borrow from the editor. In the editor: the editor's own search field (with its magnifier and clear
/// button) and its heading font. A null searchField falls back to a plain InputTextWithHint; a null heading font uses the
/// current font.
struct PanelViewHooks {
    bool (*searchField)(const char* id, char* buf, std::size_t bufSize, const char* hint, float width) = nullptr;
    ImFont* headingFont = nullptr;
};

okaypanels::ProblemsIntent DrawProblemsBodyImGui(const okaypanels::ProblemsModel& m, okaypanels::ProblemsState& st,
                                                 const PanelViewHooks& hooks);
okaypanels::StatsIntent DrawStatsBodyImGui(okaypanels::StatsModel& m, const PanelViewHooks& hooks);
okaypanels::HistoryIntent DrawHistoryBodyImGui(const okaypanels::HistoryModel& m, const PanelViewHooks& hooks);
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

} // namespace oked
