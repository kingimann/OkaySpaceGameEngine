#pragma once
// The Problems panel: everything currently wrong in the open scene, grouped by
// object, asset or script. Clicking a row selects the object, opens the script
// or frames the graph node. Fix runs a safe one-click repair.
#include "okay/Debug/Diagnostics.hpp"
#include "okay/Debug/ProjectDiagnostics.hpp"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include <string>
#include <vector>

namespace okay { class Scene; class GameObject; namespace editor { class EditorState; } }

namespace oked {

struct DiagTotals { int info = 0, warning = 0, error = 0; };

// Re-run the validators when the scene changed, throttled. Edit mode only.
void TickProjectDiagnostics(okay::editor::EditorState& ed);
// The Problems window.
void DrawProblems(okay::editor::EditorState& ed);

DiagTotals ProjectDiagTotals();
// Hierarchy / Inspector markers. `label` is the component header text.
void DrawObjectDiagMarker(const okay::GameObject* go, ImVec2 rowMin, ImVec2 rowMax);
void DrawComponentDiagMarker(const okay::GameObject* go, const char* label, ImVec2 headerMin, ImVec2 headerMax);
bool ObjectHasDiag(const okay::GameObject* go);
bool ComponentHasDiag(const okay::GameObject* go, const char* label);
void DrawSceneDiagMarker(ImVec2 rowMin, ImVec2 rowMax);

// Console: a click on a row with a source selects that object. The editor
// binds itself once a frame; DrawConsole calls the activate without an EditorState.
void BindProblemsEditor(okay::editor::EditorState* ed);
void ActivateBoundConsoleSource(const okay::ConsoleSource& source);

// Build: errors block, warnings are listed. `label` prefixes each line ("[Level1] ").
struct BuildDiagGate {
    int errors = 0;
    int warnings = 0;
    std::vector<std::string> errorLines;
    std::vector<std::string> warningLines;
};
BuildDiagGate DiagnoseSceneForBuild(const okay::Scene& scene, const std::string& assetRoot, const std::string& label);

// Dev screenshot hook (OKAY_UI_DIAG=1). Builds a broken scene and drives the panel.
void DiagnosticsDemoPrepare(okay::editor::EditorState& ed);
// A path the frame loop should write the current framebuffer to, or empty.
std::string TakePendingEditorShot();

} // namespace oked
