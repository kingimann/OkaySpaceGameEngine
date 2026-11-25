#pragma once
// Editor panels drawn with OkayUI (the opt-in "OkayUI (preview)" backend, docs/own_ui_toolkit.md sections 9 to 11): Problems,
// Stats, History, Project Settings' Tags and Layers, Physics, Player, Rendering and Loading screen sections, the Profiler, the Project panel's import
// settings and the Lighting window's Post-processing section. Same shape as OkayUIPanels.hpp: a plain function takes the panel's model (shared/EditorPanelModels.hpp, the
// same one the Dear ImGui version in editor/EditorPanelViews.cpp draws) and a rectangle, draws into a bare OkayUI window that fills
// it, and answers with the model's intent plus the rectangles of what it drew (for the tests). No SDL, no Dear ImGui.
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"     // BeginChild(id, size, childFlags) and its ChildFlags
#include "EditorPanelModels.hpp"

#include <cstdint>
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
    float indentSpacing = 22.0f;   ///< a fold-out's rows are this far in (Dear ImGui's IndentSpacing)
    OkayUI::Vec2 sepTextPadding{20.0f, 3.0f};   ///< a group title's text in from the left, and above and below it (SeparatorTextPadding)
    float sepTextBorder = 3.0f;    ///< its rules' thickness (SeparatorTextBorderSize); the editor's style is 22 / 8 and 1
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
    OkayUI::Rect gravity2D, gravity3D, stepRate, smooth, iterations, apply;   // zero size when not shown
    float contentHeight = 0.0f;
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
    OkayUI::Rect texturesHeader;                              // the "Textures" fold-out (zero without model.textures)
    std::vector<OkayUI::Rect> textureRows;                    // its rows drawn while open (the file name cell)
    bool texturesOverBudget = false;                          // the budget warning is showing
};

ProfilerResult DrawProfiler(okaypanels::ProfilerModel& model, float x, float y, float w, float h);

// ---- Import settings (the Project panel) -------------------------------------------------------------------------------------

struct ImportSettingsResult {
    okaypanels::ImportSettingsIntent intent;
    OkayUI::Rect sizeCombo, kindCombo, filterCombo, anisoCombo, wrapCombo, mipmaps, mipFilterCombo, compress, formatCombo;   // texture (zero size when not shown)
    OkayUI::Rect ddsOutCombo, ddsFilterCombo, ddsColour, ddsNormal, ddsCutout, generate;   // texture: the tools (mipmaps and compression)
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
    OkayUI::Rect company, version, width, height;                  // plain number fields (no step buttons, as the editor's)
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

// ---- Inspector, stage 1 (docs/own_ui_toolkit.md section 14) ---------------------------------------------------------------------
// Each part is drawn on its own (the host puts each in its own placeholder inside the Dear ImGui Inspector window, between the parts
// that are still Dear ImGui's). `bleed`: how far a header bar reaches past the region on both sides (Dear ImGui's framed headers
// reach half the window padding out); the host offers that much room.

struct InspectorHeaderResult {
    okaypanels::InspectorHeaderIntent intent;
    OkayUI::Rect card, activeBox, nameField, staticBox, lockBox, tagCombo, layerCombo, savePrefab;
    std::vector<OkayUI::Rect> tagRows;               // the open tag list's rows: Untagged, the built-in tags, the project's tags
    std::vector<OkayUI::Rect> layerRows;             // the open layer list's 32 rows
    OkayUI::Rect newTagField, addTagButton;          // the tag list's add field and button (zero otherwise)
    OkayUI::Rect renameField, renameButton, removeTagRow;   // the right-click menu of a tag or a layer in its list (zero otherwise)
    OkayUI::Rect popup;                              // the open list
    float contentHeight = 0.0f;
};
/// The object header card: the active box, the type icon, the name, Static and Lock; the Tag and Layer lists and Save as Prefab.
InspectorHeaderResult DrawInspectorHeader(okaypanels::InspectorHeaderModel& model, okaypanels::InspectorHeaderState& state,
                                          float x, float y, float w, float h);

struct CompHeaderResult {
    okaypanels::CompHeaderIntent intent;
    OkayUI::Rect bar, arrow, enableBox, menuButton, menu;
    std::vector<std::string> menuIds;                // the open menu's rows (separators and headings left out) ...
    std::vector<OkayUI::Rect> menuRows;              // ... and where they are
    float contentHeight = 0.0f;
};
/// A component's header bar (every component has one): the fold arrow, the enable box, the family mark, the name, the problems
/// marker and the "..." menu (also on a right-click of the bar). `id` names the OkayUI window (one per component drawn).
CompHeaderResult DrawComponentHeader(const okaypanels::CompHeaderModel& model, const char* id, float bleed, float x, float y, float w, float h);

struct TransformResult {
    okaypanels::TransformIntent intent;
    OkayUI::Rect header, pos[3], rot[3], scale[3], resetPos, resetRot, resetScale, uniform;
    OkayUI::Rect posLabel, rotLabel, scaleLabel;     // the rows' labels (a drag from one goes onto the visual script)
    OkayUI::Rect menuCopy, menuPaste;                // the header's right-click menu (zero otherwise)
    OkayUI::Rect fieldMenu;                          // stage 2: a field's open prefab menu (zero otherwise)
    float contentHeight = 0.0f;
};
/// The Transform: its header, Position, Rotation and Scale (three drag cells each, with the axis colours), the resets and Uniform.
TransformResult DrawTransform(okaypanels::TransformModel& model, float bleed, float x, float y, float w, float h);

struct MeshRendererResult {
    okaypanels::MeshRendererIntent intent;
    OkayUI::Rect color, wireframe, shader, rim, outline, castShadows, receiveShadows, groundShadow, groundShadowY, shadowStrength,
        emissive, specular, primitive;
    std::vector<OkayUI::Rect> listRows;              // the rows of the open list (Shader or Primitive)
    OkayUI::Rect popup;                              // the open list, or a field's prefab menu
    float contentHeight = 0.0f;
};
/// The Mesh Renderer's shading fields, from the stats line to the Primitive list (the rest of the component is the host's).
MeshRendererResult DrawMeshRenderer(okaypanels::MeshRendererModel& model, float x, float y, float w, float h);

struct AddComponentResult {
    bool open = false;                               // the button was clicked: the host opens its component browser
    OkayUI::Rect button;
    float contentHeight = 0.0f;
};
/// The Add Component button (the accent colour packed as 0xAABBGGRR).
AddComponentResult DrawAddComponentButton(std::uint32_t accent, float x, float y, float w, float h);

// ---- Inspector, stage 2 (docs/own_ui_toolkit.md section 15) ---------------------------------------------------------------------

struct FieldListResult {
    std::vector<OkayUI::Rect> rows;                  // where each row was drawn (zero size: not drawn, inside a closed fold-out)
    std::vector<OkayUI::Rect> listRows;              // the open list's or picker's rows (combo, mask, picker; zero size out of view)
    OkayUI::Rect popup;                              // the open list, picker or menu
    float contentHeight = 0.0f;
};
/// A component body as field rows (shared/InspectorFieldsModel.hpp): the values are edited in place and the rows marked; tooltips,
/// prefab marks and pickers ask the list's hooks. `id` names the OkayUI window (one per body drawn).
FieldListResult DrawFieldList(okaypanels::InspFieldList& list, const char* id, float x, float y, float w, float h);

// ---- Loading screen (Project Settings) -------------------------------------------------------------------------------------------

struct LoadingScreenResult {
    okaypanels::LoadingScreenIntent intent;
    OkayUI::Rect logo, browse, clear, accentBox, accent, bgTop, bgBottom, showBar;   // the colour rows: the cells, the swatch and the label
    float contentHeight = 0.0f;
};

LoadingScreenResult DrawLoadingScreen(okaypanels::LoadingScreenModel& model, float x, float y, float w, float h);

// ---- the small tool windows: Keyboard Shortcuts, Net, Find in Files, Data Asset, Plugin Manager (own_ui_toolkit.md section 28) --

struct ShortcutsResult {
    OkayUI::Rect filter;
    std::vector<OkayUI::Rect> titles;                // one per section shown
    std::vector<OkayUI::Rect> keys;                  // one per row shown, its keys ...
    std::vector<OkayUI::Rect> whats;                 // ... and its description
};
/// The filter field, then each section's title and its rows (keys at the left, what they do at a fixed column), in a scrolling list.
ShortcutsResult DrawShortcuts(const std::vector<okaypanels::ShortcutSection>& all, okaypanels::ShortcutsState& state,
                              float x, float y, float w, float h);

struct NetSimResult {
    okaypanels::NetSimIntent intent;
    OkayUI::Rect simulate, latency, jitter, loss, duplicate, wire, table;
    std::vector<OkayUI::Rect> rows;                  // one per traffic row, its name cell
};
/// The Simulate switch and four sliders (edited in place, kept in range), the wire totals and the traffic table.
NetSimResult DrawNetSim(okaypanels::NetSimModel& model, float x, float y, float w, float h);

struct FindInFilesResult {
    okaypanels::FindInFilesIntent intent;
    OkayUI::Rect query, search, status, list;
    std::vector<OkayUI::Rect> hits;                  // one per hit, its file:line label
};
/// The search field and Search, the status line and the hits (file:line, then the line's text); a click on a hit opens it.
FindInFilesResult DrawFindInFiles(const okaypanels::FindInFilesModel& model, okaypanels::FindInFilesState& state, float x, float y, float w, float h);

struct DataAssetResult {
    okaypanels::DataAssetIntent intent;
    std::vector<OkayUI::Rect> keys, values, removes; // one per field
    OkayUI::Rect add, save;
};
/// The file's path and the script note, a key field, a value field and X per field, then Add Field and Save.
DataAssetResult DrawDataAsset(okaypanels::DataAssetModel& model, float x, float y, float w, float h);

struct PluginManagerResult {
    okaypanels::PluginManagerIntent intent;
    OkayUI::Rect newPlugin, reloadAll, openFolder, table;
    std::vector<OkayUI::Rect> switches, names, edits, openCloses;   // one per row (openCloses: zero size when the plugin has no window)
    bool empty = false;                              // the "No plugins found" line is showing
};
/// The three buttons, the note, then a table: the switch, the name with its description or error, the scope, Edit and Open / Close.
PluginManagerResult DrawPluginManager(const okaypanels::PluginManagerModel& model, float x, float y, float w, float h);

} // namespace okayuiscreens
