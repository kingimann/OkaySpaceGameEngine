#pragma once
// Editor panels drawn with OkayUI (the opt-in "OkayUI (preview)" backend, docs/own_ui_toolkit.md sections 9 to 11): Problems,
// Stats, History, Project Settings' Tags and Layers, Physics, Player and Rendering sections, the Profiler, the Project panel's import
// settings and the Lighting window's Post-processing section. Same shape as OkayUIPanels.hpp: a plain function takes the panel's model (shared/EditorPanelModels.hpp, the
// same one the Dear ImGui version in editor/EditorPanelViews.cpp draws) and a rectangle, draws into a bare OkayUI window that fills
// it, and answers with the model's intent plus the rectangles of what it drew (for the tests). No SDL, no Dear ImGui.
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUIImCompat.hpp"   // BeginChild(id, size, childFlags) and its ChildFlags
#include "EditorPanelModels.hpp"

#include <string>
#include <vector>

namespace okayuiscreens {

/// The text scale a host pushes (times its own UI scale) around these panels so OkayUI's 16-pixel text matches the editor's
/// 18-pixel Roboto, the size the Dear ImGui versions draw with.
constexpr float kEditorTextScale = 18.0f / 16.0f;

/// The spacing of the Dear ImGui window a panel is hosted in. The defaults are the editor's own style (editor/main.cpp ApplyTheme at
/// 100%: items 12 across and 8 down, frames a text line and 8 pixels above and below with the text 12 in, a label 8 after its field).
/// A host copies the real values from ImGui::GetStyle() before it draws (the Inspector pushes its own: Appearance > Density, the
/// UI scale), so the rows come out exactly as tall and as far apart as the Dear ImGui version's in that window. `itemWidth` is
/// Dear ImGui's default field width there (CalcItemWidth); 0 means 65% of the window, Dear ImGui's default for a window.
struct EditorMetrics {
    OkayUI::Vec2 itemSpacing{12.0f, 8.0f};
    OkayUI::Vec2 framePadding{12.0f, 8.0f};
    OkayUI::Vec2 innerSpacing{8.0f, 6.0f};
    float itemWidth = 0.0f;
};
/// Set the metrics the panels below use until the next call (`EditorMetrics{}` puts the defaults back). Non-finite or negative
/// values fall back to the defaults one by one.
void SetEditorMetrics(const EditorMetrics& m);
const EditorMetrics& GetEditorMetrics();

// ---- Problems ---------------------------------------------------------------------------------------------------------------

struct ProblemsResult {
    okaypanels::ProblemsIntent intent;
    OkayUI::Rect searchField, errorPill, warningPill, infoPill, list;
    std::vector<OkayUI::Rect> groupHeaders;          // one per group drawn
    std::vector<int> rowIndex;                       // the model row of each rows[] entry
    std::vector<OkayUI::Rect> rows;                  // the row text (click: open; right-click: the menu)
    std::vector<OkayUI::Rect> fixButtons;            // same order as rows; zero size when the row has no Fix
    OkayUI::Rect menuOpen, menuFix, menuCopy;        // the right-click menu's entries while it is open (zero otherwise)
    bool empty = false;                              // the "Nothing ..." line is showing
};

ProblemsResult DrawProblems(const okaypanels::ProblemsModel& model, okaypanels::ProblemsState& state,
                            float x, float y, float w, float h);

// ---- Stats ------------------------------------------------------------------------------------------------------------------

struct StatsResult {
    okaypanels::StatsIntent intent;
    OkayUI::Rect fpsText, plot, table, frustumBox, occlusionBox, staticBox, dynamicBox, footer;
    OkayUI::Rect menuCopy;                           // "Copy stats" while the census menu is open
    std::vector<OkayUI::Rect> valueCells;            // one per counter, the number's cell
    float guideY = 0.0f;                             // the 60 FPS line (0 when the scale does not reach 60)
};

StatsResult DrawStats(okaypanels::StatsModel& model, float x, float y, float w, float h);

// ---- History ----------------------------------------------------------------------------------------------------------------

struct HistoryResult {
    okaypanels::HistoryIntent intent;
    OkayUI::Rect undoButton, redoButton, list;
    std::vector<OkayUI::Rect> rows;                  // one per HistoryRows entry
    OkayUI::Rect menuJump, menuCopy;                 // the open row menu's entries (zero otherwise)
    bool empty = false;
};

HistoryResult DrawHistory(const okaypanels::HistoryModel& model, float x, float y, float w, float h);

// ---- Tags and Layers (Project Settings) ---------------------------------------------------------------------------------------

struct TagsLayersResult {
    okaypanels::TagsLayersIntent intent;
    OkayUI::Rect strictBox, addUnlisted, tagList, newTagField, addTagButton, layerList, resetAll;
    std::vector<OkayUI::Rect> renameButtons, removeButtons;   // one per custom tag (zero size when scrolled out of the list)
    std::vector<OkayUI::Rect> layerFields, layerResets;       // one per layer; a reset is zero size when the layer has none
    OkayUI::Rect renameField, renameButton, popup;            // the rename popup while it is open (zero otherwise)
    float contentHeight = 0.0f;                               // how tall the section drew (the host sizes its placeholder by it)
};

TagsLayersResult DrawTagsLayers(const okaypanels::TagsLayersModel& model, okaypanels::TagsLayersState& state,
                                float x, float y, float w, float h);

// ---- Physics (Project Settings) -------------------------------------------------------------------------------------------------

struct PhysicsSettingsResult {
    okaypanels::PhysicsSettingsIntent intent;
    OkayUI::Rect gravity2D, gravity3D, stepRate, smooth, quality, iterations, continuous, apply;   // zero size when not shown
    float contentHeight = 0.0f;
    OkayUI::Rect popup;                                       // the quality list while it is open
};

PhysicsSettingsResult DrawPhysicsSettings(okaypanels::PhysicsSettingsModel& model, float x, float y, float w, float h);

// ---- Profiler -----------------------------------------------------------------------------------------------------------------

struct ProfilerResult {
    okaypanels::ProfilerIntent intent;
    OkayUI::Rect pauseBox, frameSlider, framePlot, gpuPlot, copyCsv, help, table;
    OkayUI::Rect bars[4];                                     // Scripts, Physics, Render, Other
    std::vector<OkayUI::Rect> rows;                           // the table's rows drawn (in ProfilerRows order)
    float guide60 = 0.0f, guide30 = 0.0f, scrubX = 0.0f;     // the guide lines' y and the scrub marker's x (0 = not drawn)
    bool empty = false;
};

ProfilerResult DrawProfiler(okaypanels::ProfilerModel& model, float x, float y, float w, float h);

// ---- Import settings (the Project panel) -------------------------------------------------------------------------------------

struct ImportSettingsResult {
    okaypanels::ImportSettingsIntent intent;
    OkayUI::Rect sizeCombo, filterCombo, wrapCombo, mipmaps;          // texture (zero size when not shown)
    OkayUI::Rect scale, normalsCombo, smoothing, animations;          // model
    OkayUI::Rect loadCombo, mono;                                     // audio
    OkayUI::Rect apply, revert, popup;                                // the buttons; the failure popup while it is open
    bool enabled = false;                                             // Apply and Revert take clicks (the values differ from the saved ones)
    float contentHeight = 0.0f;
};

ImportSettingsResult DrawImportSettings(okaypanels::ImportSettingsModel& model, okaypanels::ImportSettingsState& state,
                                        float x, float y, float w, float h);

// ---- Post-processing (Lighting) ---------------------------------------------------------------------------------------------------

struct PostProcessResult {
    okaypanels::PostProcessIntent intent;
    OkayUI::Rect preset, reset, filmic, exposure, tonemap, bloom, grading, lift, vignette, fxaa, ssao, dof, focus, note;
    OkayUI::Rect popup;                                               // the preset list or the tonemap list while it is open
    float contentHeight = 0.0f;
};

PostProcessResult DrawPostProcess(okaypanels::PostProcessModel& model, float x, float y, float w, float h);

// ---- Player and Rendering (Project Settings) ------------------------------------------------------------------------------------

struct PlayerSettingsResult {
    okaypanels::PlayerSettingsIntent intent;
    OkayUI::Rect company, version, width, height;
    OkayUI::Rect widthField, heightField;   // the number boxes alone (no step buttons: a click types a number)
    float contentHeight = 0.0f;
};

PlayerSettingsResult DrawPlayerSettings(okaypanels::PlayerSettingsModel& model, float x, float y, float w, float h);

struct RenderingSettingsResult {
    okaypanels::RenderingSettingsIntent intent;
    OkayUI::Rect ambient, skybox, apply;
    float contentHeight = 0.0f;
};

RenderingSettingsResult DrawRenderingSettings(okaypanels::RenderingSettingsModel& model, float x, float y, float w, float h);

// ---- Hierarchy -----------------------------------------------------------------------------------------------------------------

struct HierarchyResult {
    okaypanels::HierarchyIntent intent;
    OkayUI::Rect createButton, search, optionsButton, list, emptyArea;
    std::vector<int> rowIndex;                       // the model row of each rows[] entry (only the rows in view are drawn)
    std::vector<OkayUI::Rect> rows;                  // the row (its click area; the arrow, the eye and the padlock are inside it)
    std::vector<OkayUI::Rect> arrows, eyes, locks;   // same order as rows; an arrow is zero size on a row without children
    OkayUI::Rect renameField;                        // the name field of a rename in place (zero otherwise)
    std::vector<std::string> menuIds;                // the open menu's top-level rows (separators and headings left out) ...
    std::vector<OkayUI::Rect> menuRows;              // ... and where they are
    OkayUI::Rect menu;                               // the open menu (zero when none)
    int rowsFilled = 0;                              // rows asked of the model this frame
    float rowHeight = 0.0f, scrollY = 0.0f;
    bool empty = false;                              // the "Empty scene" note is showing
};

/// The Hierarchy panel (docs/own_ui_toolkit.md section 12): the toolbar, the scene strip and the list, of which only the rows in
/// view are asked of the model and drawn. Drag and drop of rows (and of files dragged in from another panel, see
/// HierarchyModel::externalDragType) goes through OkayUI's drag and drop.
HierarchyResult DrawHierarchy(const okaypanels::HierarchyModel& model, okaypanels::HierarchyState& state,
                              float x, float y, float w, float h);

} // namespace okayuiscreens
