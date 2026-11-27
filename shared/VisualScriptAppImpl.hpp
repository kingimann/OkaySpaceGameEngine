#pragma once
// Visual Script app (VisualScriptApp.hpp): what its .cpp files share. The app's own state (the dialogs, the Add Block list, what
// the canvas drew last frame, the drags in progress); the graphs themselves are the model's open tabs
// (okay::editor::vsed::Session(), editor/VisualScriptModel.hpp). Written for this project.
#include "VisualScriptApp.hpp"
#include "VisualScriptModel.hpp"
#include "OkayUIAppKit.hpp"         // the colours, the dialog box, the tab strip
#include "HierarchyPanelModel.hpp"   // okaypanels::HierEllipsize: text cut to a width with an ellipsis
#include "okay/UI/OkayUI.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vsapp {

namespace fs = std::filesystem;
namespace UI = OkayUI;
namespace vsed = okay::editor::vsed;

namespace detail {
// The OkayUI apps' colours (OkayUIAppKit.hpp), and the canvas's own: a darker canvas, its grid, a block's body, the selection.
using namespace okayuiapp;
inline const UI::Color32 kCanvas = Rgb(24, 26, 31), kGrid = Rgb(34, 37, 44), kGridBold = Rgb(44, 48, 57), kBody = Rgb(46, 49, 57),
                  kSelect = Rgb(255, 196, 84);
/// `text` cut to `maxW` pixels of the UI font, with an ellipsis.
inline std::string Fit(const std::string& text, float maxW) {
    return okaypanels::HierEllipsize(text, maxW, [](const std::string& s) { return UI::CalcTextWidth(s.c_str()); });
}
/// The file name of a path ("Untitled" for none).
inline std::string FileName(const std::string& path) {
    return path.empty() ? std::string("Untitled") : fs::u8path(path).filename().u8string();
}
}  // namespace detail

using namespace detail;

struct VisualScriptApp::Impl {
    Config cfg;
    AppSettings settings;
    std::string folder, notice;
    bool quit = false;

    // ---- dialogs ----
    bool openBox = false, openFocus = false;      // File > Open: a typed path and the folder's graphs
    char openBuf[1024] = "";
    std::vector<std::string> openFiles;            // the folder's graphs, listed once when the box opens (not every frame)
    std::string codeFor, codeText;                // the OkayScript panel's text and the graph text it was made from
    bool saveAsBox = false, saveAsFocus = false;  // File > Save As
    char saveAsBuf[1024] = "";
    int saveAsTab = -1;
    bool promptOpen = false, promptQuit = false;  // "Save changes?" (closing a changed tab, or quitting)
    int promptTab = -1;
    bool renameBox = false;                       // a comment frame's name
    char navBuf[160] = "";                        // the Navigator's search text

    // ---- the Add Block list ----
    bool palOpen = false, palFocus = false;
    char palBuf[96] = "";
    int palSel = 0;
    float palX = 0, palY = 0;                     // where it opens on screen
    struct PalRow {   // a block, or an Actions step (an Action / Check block set to `arg`)
        const vsed::VsDef* def = nullptr; int score = 0; int fit = 0; std::string title, help, place, key, keywords, arg;
        vsed::VsLevel level = vsed::VsLevel::Intermediate; int catIndex = 0; bool browse = true;
    };
    std::vector<PalRow> palAll;                   // every row the list can show, made once (again when the registry or the steps change)
    std::size_t palAllFor = (std::size_t)-1;
    std::vector<PalRow> palRows;
    std::string palBuiltFor;                      // the query (and wire, side, level) the rows were made for
    int palSide = -3;                             // what the list shows with nothing typed: -4 Starter, -5 Quick blocks, -2 Recent,
                                                  // -1 Favourites, -3 everything, else a topic (kPaletteCategories)
    bool palShowAll = false;                      // a wire is dragged: also the blocks that do not fit it
    int palHidden = 0;                            // blocks the search found but the level and "everyday blocks" choice hide
    bool palSearchAll = false;                    // "Search all blocks" was clicked
    bool palSuggest = false;                      // the rows are the nearest names to what was typed, not matches

    // ---- what the canvas drew this frame (the next frame's clicks and the tests read it) ----
    struct Rect { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool in(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; } };
    struct Plug { int node = -1; bool exec = false, output = false; int pin = 0; float x = 0, y = 0; };
    std::map<int, Rect> blockRects;
    std::vector<Plug> plugs;
    Rect canvas;                                  // the canvas's rectangle on screen
    float drawnScale = 1.4f;                      // screen pixels per stored unit as drawn (kLayout x zoom)

    // ---- drags in progress ----
    int nodeDrag = -1;                            // a block grabbed (the selection moves with it)
    float dragLastX = 0, dragLastY = 0;
    bool panning = false, boxing = false, leftOnEmpty = false;
    float pressX = 0, pressY = 0, rightPressX = 0, rightPressY = 0, rightMax = 0;
    int commentDrag = -1;
    int nodeMenu = -1;                            // the block whose right-click menu is open
    float menuX = 0, menuY = 0;
    bool canvasFocused = false;
    int refit = 0;                                // Frame All asked: fit again in this many next frames (negative: the selection)
    bool wantsKeys = false;
    double lastCheckAt = -1.0;

    // ---- the editor's parts, the beginner aids, the canvas's tools (VisualScriptAppAids.cpp, VisualScriptAppFields.cpp) ----
    int promptMode = 0;                           // what "Save changes?" is for: 0 closing promptTab, 1 quitting, 2 Close Others, 3 Close All
    bool recipeBox = false, templateBox = false;  // Pick a Recipe, Templates
    char tplBuf[64] = "";
    int tplLevel = 0;
    int tabMenu = -1, commentMenu = -1, connectNode = -1;   // a tab's / a comment frame's right-click menu, a block's Connect to...
    float popX = 0, popY = 0;                     // where that menu opens
    std::vector<Rect> startCards;                 // the start cards as drawn (empty: not shown)
    Rect startEmpty, startNever, helpCard, tryButton, helpToggle;
    std::vector<std::pair<Rect, std::size_t>> chips;   // the help card's Next steps chips
    std::string hintLine, helpTitle;
    std::map<int, std::string> sentences, badges, liveLines;   // what each block showed under its title, its badge, its live line
    std::vector<std::pair<int, UI::Vec2>> grips;  // each comment frame's resize grip (frame index, where)
    std::vector<UI::Vec2> knotsDrawn;             // each knot as drawn
    std::vector<Rect> commentRects;               // each comment frame as drawn
    int resizeComment = -1;
    bool nodeMoved = false;                       // the block being dragged has moved (let go: it may go onto a wire)
    const okay::vs::NodeGraph* live = nullptr;    // the running copy of the shown graph (this frame), and the object it runs on
    std::string liveName;
    std::string keyWait;                          // the key field waiting for the next key press ("" none)
    char declName[64] = "", newStruct[64] = "", newField[64] = "";   // the saved values panel
    int declScope = 0;
    std::string declType = "Number", declMsg;
    std::size_t valueSig = 0;                     // the typed values' fingerprint (a change: the person edited a number)
    bool wiredThisFrame = false;
    std::map<std::string, Rect> fieldRects;       // where the Details pane's fields were drawn (FieldOnScreen: the tests click them)
    void markField(const std::string& id) {
        const UI::Vec2 a = UI::GetItemRectMin(), b = UI::GetItemRectMax();
        fieldRects[id] = Rect{a.x, a.y, b.x, b.y};
    }

    okay::editor::EditorState* ed() const { return cfg.editor ? cfg.editor() : nullptr; }
    std::string project() const;                  // the editor's project folder, else the app's folder
    bool overCanvasCard(float x, float y) const;  // the help card or the start cards take the pointer there
    void refreshLive();
    void closeMany(int keep);                     // Close Others (keep >= 0) / Close All (-1), asking first when one is changed
    void drawStartCards(float X, float Y, float W, float H, bool overlay);
    void drawHelpCard(bool modal);
    void drawHintBar(float X, float Y, float W, float H);
    void drawRecipeBox(float X, float Y, float W, float H);
    void drawTemplateBox(float X, float Y, float W, float H);
    void drawTabMenu();
    void drawCommentMenu();
    void drawConnectMenu();
    void drawOutline(float X, float Y, float W, float H, bool modal);
    void drawPaused();
    void drawBlockFields(vsed::VsEditNode& n, const vsed::VsDef& d, float W, bool modal);
    void drawSavedValues(float W, bool modal);
    void drawSteps(vsed::VsEditNode& n, const vsed::VsDef& d, float W);
    bool keyField(const std::string& id, std::string& value, float width);
    bool refSlot(const std::string& id, std::string& value, okay::vs::VsRefKind kind, bool defaultSelf, float width);
    bool choiceField(const std::string& id, std::string& value, vsed::VsChoiceKind kind, float width);
    bool quickField(vsed::VsEditNode& n, const okay::vs::QuickBlock& qb, int i, std::string& value, float width);
    std::vector<std::string> recipeNames() const;
    bool pickRecipe(std::size_t index);
    bool attachSelected();
    void toggleBreakpointOn(int id);   // a breakpoint on a step (said in words when it cannot go there)
    int menuBox(const std::string& id, float px, float py, const std::vector<std::string>& rows, bool& close);
    bool connectFrom(int node, std::size_t row);
    bool AddNextStepAt(std::size_t index);
    bool TryBlock();
    void restoreVersion(const std::string& text);

    explicit Impl(Config c);
    // tabs and files (VisualScriptApp.cpp)
    vsed::VsEditState& st();
    std::string root() const;
    bool openFile(const std::string& path);
    bool saveTab(int index);
    bool saveTabAs(int index, const std::string& path);
    void close(int index);
    void requestClose(int index);
    void answerPrompt(int answer);
    void requestQuit();
    bool modalOpen() const { return openBox || saveAsBox || promptOpen || renameBox || recipeBox || templateBox; }
    void note(const std::string& line, int level = 0);
    void saveSettings();
    void frame(float X, float Y, float W, float H);
    void shortcuts(bool modal);
    void drawMenu(float X, float Y, float W, float h);
    void drawTabs(float X, float Y, float W, float h, bool modal);
    void drawDetails(float X, float Y, float W, float H, bool modal);
    void drawPanel(float X, float Y, float W, float H, bool modal);
    void drawStatus(float X, float Y, float W, float h);
    void drawOpenBox(float X, float Y, float W, float H);
    void drawSaveAsBox(float X, float Y, float W, float H);
    void drawPrompt(float X, float Y, float W, float H);
    void drawRenameBox(float X, float Y, float W, float H);
    void drawDebugBar(float X, float Y, float W, float h);
    bool beginDialog(const std::string& id, float X, float Y, float W, float H, float bw, float bh, float& bx, float& by);
    void checkNow();
    std::vector<std::string> compareRows();
    // the canvas and the Add Block list (VisualScriptAppCanvas.cpp)
    void drawCanvas(float X, float Y, float W, float H, bool modal);
    void drawPalette(float X, float Y, float W, float H);
    void drawNodeMenu();
    void openPalette(float sx, float sy, float wx, float wy);
    void buildPalette();
    bool pickPalette(std::size_t index);
    void buildPaletteAll();
    void toggleFavourite(std::size_t index);
    void frameAll(bool selectionOnly);
    float scale() const;
    UI::Vec2 toScreen(float wx, float wy) const;
    UI::Vec2 toWorld(float sx, float sy) const;
};

}  // namespace vsapp
