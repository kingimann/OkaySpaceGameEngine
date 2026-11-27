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
    struct PalRow { const vsed::VsDef* def = nullptr; int score = 0; int fit = 0; std::string title, help, place; };
    std::vector<PalRow> palRows;
    std::string palBuiltFor;                      // the query (and wire) the rows were made for

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
    bool wantsKeys = false;
    double lastCheckAt = -1.0;

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
    bool modalOpen() const { return openBox || saveAsBox || promptOpen || renameBox; }
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
    void frameAll(bool selectionOnly);
    float scale() const;
    UI::Vec2 toScreen(float wx, float wy) const;
    UI::Vec2 toWorld(float sx, float sy) const;
};

}  // namespace vsapp
