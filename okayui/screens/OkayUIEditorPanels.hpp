#pragma once
// Three more editor panels drawn with OkayUI (the opt-in "OkayUI (preview)" backend, docs/own_ui_toolkit.md section 9): Problems,
// Stats and History. Same shape as OkayUIPanels.hpp: a plain function takes the panel's model (shared/EditorPanelModels.hpp, the
// same one the Dear ImGui version in editor/EditorPanelViews.cpp draws) and a rectangle, draws into a bare OkayUI window that fills
// it, and answers with the model's intent plus the rectangles of what it drew (for the tests). No SDL, no Dear ImGui.
#include "okay/UI/OkayUI.hpp"
#include "EditorPanelModels.hpp"

#include <vector>

namespace okayuiscreens {

/// The text scale a host pushes (times its own UI scale) around these panels so OkayUI's 16-pixel text matches the editor's
/// 18-pixel Roboto, the size the Dear ImGui versions draw with.
constexpr float kEditorTextScale = 18.0f / 16.0f;

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

} // namespace okayuiscreens
