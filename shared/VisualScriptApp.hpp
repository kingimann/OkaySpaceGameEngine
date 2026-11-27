#pragma once
// Visual Script (docs/feature_map/14-visual-scripting.md 14.9): the graph editor drawn with OkayUI inside a rectangle. Tabs of .okayvs
// graphs; a canvas with the blocks, their plugs and wires, comment frames and knots (pan, zoom, select, box select, move, wire
// plugs together, drop a wire on empty space to add a block that takes it); the Add Block list (search with the plain words and
// synonyms, the blocks that fit a dragged wire first); a Details pane for the selected block (what it does, its setting, the
// values typed into unwired inputs); the checker's problems with their quick fixes; the IDE tools of #1317 (align, distribute,
// straighten, auto-arrange, collapse to function, bookmarks, find references in every graph of the folder, watch values, the
// debugger's breakpoints and paused bar, compare with the saved file); the graph as OkayScript; undo and redo; copy, paste,
// duplicate; Save, Save As, Save All, Reopen Closed Tab, Close Others / Close All, Restore Previous Version; a status line; settings
// kept in a file of their own. The beginner aids (start cards, the "What is this?" card with Next steps and Try this block, the
// next-step hint, a plain sentence and a badge on every block, plug tips), a block's fields with the widget its kind calls for (keys,
// object and file slots that take the editor's drops, choice lists, Quick block fields, Action steps, Switch cases), the outline and
// the saved values panel, recipes and templates, Connect to..., blocks put on wires, knots, comment frames, the live run, Add
// Component and Density (VisualScriptAppAids.cpp, VisualScriptAppFields.cpp). It replaced the editor's Dear ImGui window.
// ONE implementation for two hosts (owner, 2026-11-26: the Visual Script editor "must be its own program as well as inside the
// editor"), the same way as the Script Editor (shared/ScriptIdeApp.hpp) and the Browser (shared/BrowserApp.hpp):
//   * the Visual Script program (visualscript/main.cpp, `okay-visual-script [FOLDER | FILE.okayvs ...]`) draws it over its window;
//   * the editor hosts it in a window of its own (editor/VisualScriptAppHost.cpp), on the open project, with the editor's hooks
//     (saving publishes the graph to the scene's objects, the running graph's values while playing, Stop Play).
// The graph, its rules, the node registry and the open tabs are the editor's model (editor/VisualScriptModel.hpp:
// okay::editor::vsed::Session()), the same one the rest of the editor reads; this app only draws it and acts on it.
//
// Written from general node-graph editor behaviour (blocks with typed plugs, wires drawn as curves, a searchable add list, a
// properties pane); Unreal's Blueprints and Unity's graph tools were a reference for behaviour only. No code, names, icons or
// look were taken from any other editor (rule 19a).
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay::editor { class EditorState; }
namespace okay::editor::vsed { struct VsEditState; }
namespace okay::vs { class NodeGraph; }

namespace vsapp {

/// What the Visual Script app keeps between runs (one small JSON file, read with the engine's okay::json).
struct AppSettings {
    bool showDetails = true;     ///< the Details pane beside the canvas
    bool showMinimap = true;     ///< the whole graph in the canvas's corner (click to jump)
    bool snapGrid = true;        ///< a moved block snaps to the grid when it is let go
    bool scrollPans = false;     ///< trackpad mode: the wheel pans, Ctrl+wheel zooms
    bool showOutline = false;    ///< the outline beside the canvas (events, functions, breakpoints, saved values, inputs)
    bool liveValues = true;      ///< while the game runs: the blocks that ran flash, values beside the plugs, the line under a block
    bool showHint = true;        ///< the one-line next-step hint at the bottom of the canvas
    int density = -1;            ///< View > Density in the program (0 Compact .. 2 Spacious; -1 not chosen). The editor keeps its own.
    int panel = 0;               ///< the panel under the canvas: 0 none, 1 Problems, 2 Navigator, 3 OkayScript, 4 Compare
    std::string lastFolder;      ///< the folder that was open last (the program opens it again with no argument)
    std::vector<std::string> recentFiles;   ///< most recent first, at most 10

    std::string Serialize() const;
    /// Read what Serialize wrote; unknown keys, bad values and text that is not JSON keep the defaults.
    static AppSettings Parse(const std::string& text);
    /// Put `path` first in recentFiles (moving it when it was there).
    void NoteFile(const std::string& path);
};

/// A command for the host's menus: its title, its keys as text ("" = none), what it does.
struct AppCommand { std::string title, shortcut; std::function<void()> run; };

class VisualScriptApp {
public:
    struct Config {
        std::string settingsPath;                     ///< the settings file ("" = nothing is kept: tests)
        std::string idPrefix = "##visualscript";      ///< prefix of the OkayUI window names (unique in the host's frame)
        bool quitItem = false;                        ///< File has Quit (Ctrl+Q): the program's own window
        // ---- hooks (all optional) ----
        /// Save a tab: the host writes it (the editor: PublishOpenGraph, which also reloads the objects that run it) and says
        /// why not. Empty: the graph's text is written to its file (SaveTextFile when the host set one).
        std::function<bool(okay::editor::vsed::VsEditState& tab, std::string& why)> save;
        /// A graph was saved (the program: nothing; the editor: a Console line).
        std::function<void(const std::string& path)> saved;
        /// A line for the host's log (the editor: its Console). Level 0 info, 1 warning, 2 error. Empty: the status line only.
        std::function<void(const std::string& line, int level)> log;
        /// File > Open's "Browse..." button: ask the system for a .okayvs file and call back with it. Empty: the typed path only.
        std::function<void(const std::string& start, std::function<void(const std::string&)> chosen)> pickFile;
        /// The running copy of a graph file while the host plays (watch values, the lit path). Empty or null: not running.
        std::function<const okay::vs::NodeGraph*(const std::string& path)> liveGraph;
        /// Debug > Stop Play while paused at a breakpoint (the editor stops its game). Empty: no such item.
        std::function<void()> stopPlay;
        /// More items for View > Window (the editor: Float / Dock). Empty: none.
        std::function<std::vector<AppCommand>()> windowCommands;
        /// The editor: its scene and project (object lists in a block's slots, objects dropped from the Hierarchy, Attach to Selected
        /// Object, the component a block is missing and Add Component, the parts a recipe adds to the selected object, the running
        /// object's name and saved values). Empty or null (the program, which has no scene): those parts are not shown.
        std::function<okay::editor::EditorState*()> editor;
    };

    explicit VisualScriptApp(Config config);
    ~VisualScriptApp();
    VisualScriptApp(const VisualScriptApp&) = delete;
    VisualScriptApp& operator=(const VisualScriptApp&) = delete;

    // ---- files and tabs ----
    /// The folder File > Open lists, New graphs are saved in (its Assets folder) and Find References searches. False when it is
    /// not a folder.
    bool OpenFolder(const std::string& folder);
    /// Open a .okayvs file in a tab (or show its tab). False when it cannot be read (the tab then says so).
    bool OpenFile(const std::string& path);
    /// What the command line names: a folder opens as the folder, a file in a tab. False when it is neither.
    bool OpenPath(const std::string& path);
    /// A new tab: `starter` puts the Start and Update events on it, else it is empty.
    void NewGraph(bool starter = true);
    /// Save the shown tab (an untitled one opens Save As) / every changed tab. False when something could not be written.
    bool Save();
    bool SaveAs(const std::string& path);
    int SaveAll();
    /// Close a tab as the person does (Ctrl+W, the x): a changed one asks first (Save, Don't Save, Cancel).
    void RequestClose(int tab);
    bool ClosePromptOpen() const;
    /// Answer the box: 0 Save, 1 Don't Save, 2 Cancel.
    void AnswerClosePrompt(int answer);
    /// Reopen the tab closed last (Ctrl+Shift+T). False when there is none.
    bool ReopenClosed();
    int TabCount() const;
    int ActiveTab() const;
    void SetActiveTab(int index);
    okay::editor::vsed::VsEditState& Active();

    // ---- blocks and wires ----
    /// Open the Add Block list; a block picked from it goes at (worldX, worldY) in the graph's units.
    void OpenPalette(float worldX, float worldY);
    bool PaletteOpen() const;
    /// The list's search text, its rows as shown (titles) and taking one (Enter or a click does the same).
    void SetPaletteQuery(const std::string& query);
    std::vector<std::string> PaletteRows() const;
    std::vector<std::string> PaletteTypes() const;
    bool PickPaletteRow(std::size_t index);
    /// Add a block of `type` at (x, y) as the list does: selected, and wired to the selected block (or the one added last) when
    /// a plug fits. The new block's id, or -1 when there is no such block.
    int AddBlock(const std::string& type, float x, float y);
    /// Run a Visual Script command by its id (okay::editor::vsed::VsCommands(): vs.alignLeft, vs.collapseToFunction, ...).
    bool RunCommand(const std::string& id);
    bool Undo();
    bool Redo();
    void SelectAll();
    bool CopySelection();
    bool PasteClipboard();
    bool DuplicateSelection();
    int DeleteSelection();
    /// A comment frame around the selected blocks. False when nothing is selected.
    bool CommentSelection();
    /// The view: fit every block / the selected ones (in the next frame drawn).
    void FrameAll();
    void FrameSelection();
    /// Where a block's plug is on screen (as last drawn): `exec` flow plug, `output` its right side. False when not drawn.
    bool PlugOnScreen(int node, bool exec, bool output, int pin, float& x, float& y) const;
    /// A block's rectangle on screen as last drawn. False when it was not drawn.
    bool BlockOnScreen(int node, float& x0, float& y0, float& x1, float& y1) const;

    // ---- checks, panels and the IDE tools ----
    /// Check the shown graph now (also done whenever it changes). The problem count.
    std::size_t CheckNow();
    /// The shown graph's problems as sentences ("Error: ...") and applying a problem's first quick fix.
    std::vector<std::string> ProblemLines();
    bool FixProblem(std::size_t index);
    /// The graph as OkayScript (what View > OkayScript shows).
    std::string OkayScriptText();
    /// Compare the shown graph with its file on disk (the Compare panel). False when it has no file.
    bool CompareWithSaved();
    /// The Compare panel's rows ("+ Print", "- Branch", "~ Wait: 1 -> 2").
    std::vector<std::string> CompareRows();
    /// The Navigator's results (Find References), as "file: block" rows.
    std::vector<std::string> NavigatorRows() const;
    /// The panel under the canvas (AppSettings::panel's numbers).
    void ShowPanel(int panel);
    int Panel() const;
    /// Toggle a breakpoint on the selected block (F9; kept per graph file in the engine's debugger).
    bool ToggleBreakpoint();

    // ---- the beginner aids (what a test reads back of the last frame drawn) ----
    /// The start cards over an empty graph: shown, and where card `i` was drawn (false when it was not).
    bool StartCardsShown() const;
    bool StartCardOnScreen(int i, float& x0, float& y0, float& x1, float& y1) const;
    /// The next-step hint at the bottom of the canvas ("" when none was shown).
    const std::string& HintLine() const;
    /// The "What is this?" card of the selected block: its title ("" when not shown), its Next steps chips (titles) and taking one,
    /// and Try this node (a small graph that uses the block).
    const std::string& HelpCardTitle() const;
    std::vector<std::string> NextStepChips() const;
    bool AddNextStep(std::size_t index);
    bool TryThisBlock();
    /// The plain sentence drawn under a block's title, and its badge ("Ready", "Needs input", "Error"). "" when not drawn.
    std::string BlockSentence(int node) const;
    std::string BlockBadge(int node) const;

    // ---- recipes, templates, the editor's actions ----
    /// Pick a Recipe: recipe `index` of the model's list (VsRecipes()), then the "More recipes" (VsExtraRecipes()) after them.
    std::vector<std::string> RecipeNames() const;
    bool PickRecipe(std::size_t index);
    /// Templates: the names matching `query` at `level` (a VsLevelFilter) and taking one (it replaces the graph on screen).
    std::vector<std::string> TemplateNames(const std::string& query, int level) const;
    bool ApplyTemplate(const std::string& name);
    /// Attach to Selected Object (the editor): false with no editor, no selected object, or when the graph could not be saved.
    bool AttachToSelected();
    /// File > Restore Previous Version: the earlier saves' labels, and putting one on screen (Undo takes it back).
    std::vector<std::string> PreviousVersions() const;
    bool RestoreVersion(std::size_t index);
    /// Close Others / Close All, as the tab's right-click menu does (changed tabs ask first).
    void CloseOthers(int tab);
    void CloseAll();
    /// The outline's rows ("When this happens: On Start", "Named actions: Jump", "Saved values: score", ...).
    std::vector<std::string> OutlineRows() const;

    // ---- the canvas's tools ----
    /// Open the Connect to... list for block `node`: its plugs; picking plug `row` opens the Add Block list with the blocks that fit it.
    std::vector<std::string> ConnectRows(int node) const;
    bool ConnectFrom(int node, std::size_t row);
    /// Where a comment frame's resize grip and a knot were drawn (false when not drawn).
    bool CommentGripOnScreen(int comment, float& x, float& y) const;
    bool CommentOnScreen(int comment, float& x0, float& y0, float& x1, float& y1) const;
    bool KnotOnScreen(int knot, float& x, float& y) const;
    /// The text the live run shows under block `node` ("" when it has not run or the game is not running).
    std::string LiveLine(int node) const;
    /// A block's missing component (the editor; "" when nothing is missing) and Add Component for it.
    std::string MissingComponent(int node) const;
    bool AddMissingComponent(int node);

    /// Where a field of the Details pane was drawn last frame: "key<block>" (a key field), "arg<block>" (a block's setting: its slot,
    /// list or text), "pin<block>_<input>" (an unwired input's slot or text), "qk<block>_<field>" / "q<block>_<field>" (a Quick
    /// block's key / other field), "addstep<block>", "case<block>", "newvar" and "addvar" (the saved values panel).
    bool FieldOnScreen(const std::string& id, float& x0, float& y0, float& x1, float& y1) const;

    /// Draw the Visual Script app in (x, y, w, h) and act on what the person did. Between OkayUI::BeginFrame and EndFrame.
    void Frame(float x, float y, float w, float h);

    /// Ctrl+Q or File > Quit (only with Config::quitItem); with changed tabs it asks first.
    bool WantsQuit() const;
    /// The window's title: "<graph> - <folder> - Visual Script" ("*" before a changed graph).
    std::string WindowTitle() const;
    /// Write the settings now (also done when they change and at the end).
    void SaveSettings();
    const std::string& Folder() const;
    /// The line at the bottom: what happened last.
    const std::string& Notice() const;
    AppSettings& Settings();
    /// The keyboard is the app's this frame (a field has it, or the canvas is focused): a host routes keys to it.
    bool WantsKeyboard() const;

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

} // namespace vsapp
