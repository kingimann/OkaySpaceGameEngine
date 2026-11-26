#pragma once
// Script Editor (docs/feature_map/13-scripting.md 13.5): the script editor drawn with OkayUI inside a rectangle. A project folder's
// scripts as a tree, one tab per open file (our own code widget, OkayUI::CodeEdit through scripteditor::ScriptEditorCodeEdit),
// save, Find in Files, Go to File, Local History with a diff and Restore, the checker's errors and warnings on the lines, a
// status line, and settings kept in a file of their own.
// ONE implementation for two hosts (owner, 2026-11-26: "script editor ... should also be their own separate program while also
// allowing user to use it inside"), the same way as the Browser (shared/BrowserApp.hpp):
//   * the Script Editor program (scripteditor/main.cpp, `okay-script-editor <folder|file>`) draws it over its whole window;
//   * the editor hosts it in a window of its own (editor/ScriptEditorAppHost.cpp), on the open project, with the editor's live
//     data: its language words and hover docs, its palette, the editor plugins' checks, and "show in the scene's Script Editor".
// What it decides comes from the headless model (editor/ScriptIde.hpp: the script list and tree, Go to File ranking, find in
// texts, BasicLint, Local History, LineDiff) and the engine's checkers (okay::CreateScriptVM(...)->ValidateAll). Nothing here
// knows the editor; the editor's data comes in through Config's hooks.
//
// Written from general editor behaviour (a tree beside tabs of files, a search panel, a status line); no code, names, icons or
// look taken from any other editor (rule 19a).
#include "ScriptIde.hpp"
#include "ScriptEditorCodeEdit.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay { class ScriptComponent; }

namespace scriptide {

/// What the Script Editor keeps between runs (one small JSON file, read with the engine's okay::json). Shared by the program and the
/// editor's window when both use the same file.
struct AppSettings {
    int tabSize = 4;
    bool wordWrap = false;
    bool showWhitespace = false;
    bool showTree = true;
    float codeFontPx = 15.0f;                 ///< the code's line of text, in pixels (10..32)
    std::string lastFolder;                   ///< the folder that was open last (the program opens it again with no argument)
    std::vector<std::string> recentFolders;   ///< most recent first, at most 10

    std::string Serialize() const;
    /// Read what Serialize wrote; unknown keys, bad values and a file that is not JSON are skipped (each field keeps its default),
    /// numbers are clamped.
    static AppSettings Parse(const std::string& text);
    /// Put `folder` first in recentFolders (moving it when it was there) and make it lastFolder.
    void NoteFolder(const std::string& folder);
};

/// The checks the Script Editor runs without the editor: the engine's own checker for the language (ValidateAll; Lua and
/// JavaScript also their Lint), then BasicLint with the language's built-in names as the known names. Severity 0 error, 1 warning.
std::vector<Diagnostic> CheckScript(const std::string& language, const std::string& text);

/// Run > Run without a scene: the script on a throwaway object in a scene of its own (no physics), Start and three Update frames
/// of 1/60 s, so nothing in a real scene is touched. `prepare` sets the component up first (the editor copies the Inspector's
/// field values). The message says what happened ("Test run OK ...", the compile error, the runtime error). Its print / log
/// lines go to the engine's log.
struct TestRunResult { bool ok = false; std::string message; };
TestRunResult TestRunScript(const std::string& language, const std::string& text,
                            const std::function<void(okay::ScriptComponent&)>& prepare = {});

/// The words the code is coloured with when the host gives none: the language's keywords and the engine's built-in names
/// ("okayscript", "lua", "javascript"; anything else: C-like words). Made once per language.
const scripteditor::LanguageWords& DefaultWordsFor(const std::string& language);

/// One suggestion of the completion list: what the list shows, what replaces the word before the caret, a short line beside it.
struct CompletionItem { std::string label, insert, detail; };
/// The default completions: the language's keywords and known names (with the first line of their hover doc) and the words of the
/// text, matching `prefix` as a fuzzy subsequence (FuzzyScore), best first, the prefix itself left out; at most `maxItems`.
std::vector<CompletionItem> DefaultCompletions(const scripteditor::LanguageWords& words, const std::string& text,
                                               const std::string& prefix, std::size_t maxItems = 40);

/// A document a host opens that is not simply a file on disk, or whose saving the host does: the editor's Script components
/// (a component's script, saved to its file and given to the component), or any text the host keeps.
struct DocumentSpec {
    std::string key;        ///< unique among the open documents ("scene:<object guid>/<n>", or the file's path)
    std::string path;       ///< the file it is ("" when none: no Local History, no Find in Files on disk)
    std::string label;      ///< the tab's text ("" = the file name)
    std::string language;   ///< "okayscript", "lua", "javascript", "csharp" ("" = from the path)
    std::string text;       ///< the text to edit
    /// Save: the host keeps the text (true) or says why not (false and `why`). Empty: written to `path`.
    std::function<bool(const std::string& text, std::string& why)> save;
};

/// A command of the command list (Ctrl+Shift+P) and Search Everywhere: its title, its keys as text ("" = none), what it does.
struct AppCommand { std::string title, shortcut; std::function<void()> run; };

/// One script of the host's scene (the editor: a Script component), listed in the tree's "Scene scripts" section.
struct SceneScript {
    std::string key;        ///< the document key the host opens it under (DocumentSpec::key)
    std::string label;      ///< the script ("Player.lua", or "(no file)")
    std::string object;     ///< the object it is on ("" = not on an object: a script opened from the project)
    std::string path;       ///< its file ("" = it lives only in the scene)
};

class ScriptIdeApp {
public:
    struct Config {
        std::string settingsPath;               ///< the settings file ("" = nothing is kept: tests)
        std::string idPrefix = "##scripteditor";   ///< prefix of the OkayUI window names (unique in the host's frame)
        bool quitItem = false;                  ///< File has Quit (Ctrl+Q): the program's own window
        bool useCodeFont = true;                ///< draw code in the embedded JetBrains Mono (false: the UI font; tests)
        // ---- hooks (all optional) ----
        /// The words of a language (keywords, types, known names with hover docs). Empty or null: DefaultWordsFor.
        std::function<const scripteditor::LanguageWords*(const std::string& language)> wordsFor;
        /// The code widget's palette (kScriptPaletteSize colours). Null: the widget's own.
        const scripteditor::ScriptPalette* palette = nullptr;
        /// The checks of a file. Empty: CheckScript.
        std::function<std::vector<Diagnostic>(const std::string& language, const std::string& path, const std::string& text)> check;
        /// More problems added after the checks (the editor: its plugins' lint).
        std::function<std::vector<Diagnostic>(const std::string& language, const std::string& path, const std::string& text,
                                              std::vector<Diagnostic> own)> moreProblems;
        /// Run > Run (F5): the host runs the document (its path, or its key when it has none). Returns a line for the status line.
        /// Empty: TestRunScript (a sandbox scene: Start and three Update frames).
        std::function<std::string(const std::string& path)> run;
        std::string runLabel = "Run";
        /// Run > "Show in <hostName>": the host shows the file in its own view at a line (the editor: its Script Editor
        /// window). Empty: no such item.
        std::function<void(const std::string& path, int line)> showInHost;
        std::string hostName;
        /// A file was saved (the editor: a line in its Console; the program: nothing).
        std::function<void(const std::string& path, const std::string& text)> saved;
        /// File > Open Folder's "Browse..." button: ask the system for a folder and call back with it (the program:
        /// okay::filedialog::FolderOne). Empty: only the typed path.
        std::function<void(const std::string& start, std::function<void(const std::string&)> chosen)> pickFolder;
        /// The completion list while typing a word (Ctrl+Space asks at once). Empty: DefaultCompletions with the language's words.
        std::function<std::vector<CompletionItem>(const std::string& language, const std::string& text, int line, int col,
                                                  const std::string& prefix)> complete;
        /// A document's breakpoints changed (a click in the gutter or F9): its key and the 1-based lines.
        std::function<void(const std::string& key, const std::vector<int>& lines)> breakpointsChanged;
        /// Run > Continue / Step Over while a document shows an execution line (SetExecutionLine): the host's debugger gets
        /// "continue" or "step". Empty: no such items.
        std::function<void(const std::string& command)> debug;
        // ---- the host's scene (the editor's Script components) ----
        /// The scene's scripts for the tree's "Scene scripts" section (asked every frame the tree shows). Empty: no section.
        std::function<std::vector<SceneScript>()> sceneScripts;
        /// The document for a key the app was given before (a click in "Scene scripts", Ctrl+Shift+T on a closed tab). False
        /// when the host has no such script any more.
        std::function<bool(const std::string& key, DocumentSpec& out)> documentFor;
        /// The languages the language menu and New Script offer. Empty: okay::AvailableScriptLanguages().
        std::vector<std::string> languages;
        /// A document's language was changed in the app (the editor: the component's language).
        std::function<void(const std::string& key, const std::string& language)> languageChanged;
        /// New Script's "Also attach it to <name>": the name of the object the host would attach to ("" = none, no box).
        std::function<std::string()> attachTarget;
        /// Attach a script file New Script made to that object; true and the key of its document (opened with documentFor), or
        /// false (the file is then opened as a file).
        std::function<bool(const std::string& path, const std::string& language, std::string& key)> attachScript;
        /// More commands for the command list and Search Everywhere (the editor: its plugins' commands).
        std::function<std::vector<AppCommand>()> commands;
        /// A clock in milliseconds since 1970 for Local History (empty: the system clock). Tests pin it.
        std::function<std::int64_t()> nowMs;
    };

    explicit ScriptIdeApp(Config config);
    ~ScriptIdeApp();
    ScriptIdeApp(const ScriptIdeApp&) = delete;
    ScriptIdeApp& operator=(const ScriptIdeApp&) = delete;

    /// Open a project folder: its scripts show in the tree, Find in Files and Go to File search them, Local History is kept in
    /// its `.okay/history`. Open files stay open. False when it is not a folder.
    bool OpenFolder(const std::string& folder);
    /// Open a file in a tab (or show its tab) at a 1-based line (0: where it was). False when it cannot be read.
    bool OpenFile(const std::string& path, int line = 0);
    /// Open a host's document in a tab (or show its tab when one with the same key is open; its text is then left as it is).
    bool OpenDocument(const DocumentSpec& spec, int line = 0);
    /// The open document with this key: its text in the tab (with unsaved edits), whether it differs from what was saved.
    bool HasDocument(const std::string& key) const;
    std::string DocumentText(const std::string& key) const;
    bool DocumentDirty(const std::string& key) const;
    /// Put new text into an open document from outside (the file changed on disk, the host's copy changed): replaces the text
    /// and what counts as saved. False when no such document is open.
    bool ReloadDocument(const std::string& key, const std::string& text);
    /// Breakpoints (1-based lines) of an open document, set by the host or by a click in the gutter.
    bool SetBreakpoints(const std::string& key, const std::vector<int>& lines);
    std::vector<int> Breakpoints(const std::string& key) const;
    /// The line a paused script stands on (1-based; 0 clears it): marked and shown. False when no such document is open.
    bool SetExecutionLine(const std::string& key, int line);
    /// The completion list (open while typing a word, Ctrl+Space; Up / Down, Enter or Tab to take one, Esc to close).
    bool CompletionOpen() const;
    const std::vector<CompletionItem>& CompletionList() const;

    // ---- the host's scene, New Script, closing and reopening tabs, the language ----
    /// Open each document and show the first (the editor: the scripts of the object that was just selected).
    void ShowDocuments(const std::vector<DocumentSpec>& specs);
    /// Open the New Script box (name, language, template, attach).
    void OpenNewScript();
    bool NewScriptOpen() const;
    /// What New Script's Create does: <folder>/Assets/Scripts/<stem>.<ext> from the template (`templateIndex` into
    /// TemplatesFor(language); 0 = the starter with the class named after the file), not overwritten when it exists; then
    /// attached through the host (attachScript) or opened as a file. False with the reason in Notice() (no folder open...).
    bool CreateScript(const std::string& typedName, const std::string& language, std::size_t templateIndex, bool attach,
                      std::string* path = nullptr);
    /// Close a tab as the person does (the x, Ctrl+W, a middle click): a changed one asks first (Save, Don't Save, Cancel).
    void RequestClose(const std::string& key);
    /// The "Save changes?" box is open (closing a changed tab, or quitting with changed tabs).
    bool ClosePromptOpen() const;
    /// Answer the box: 0 Save, 1 Don't Save, 2 Cancel (what its buttons do).
    void AnswerClosePrompt(int answer);
    /// Reopen the tab closed last (Ctrl+Shift+T): a file again, a host's document through documentFor. False when there is none.
    bool ReopenClosed();
    /// Change a document's language (Edit > Script Language): its colours, checks and words follow; an empty or starter text
    /// becomes the new language's starter (one undo step); the host is told (languageChanged).
    bool SetLanguage(const std::string& key, const std::string& language);
    std::string ActiveLanguage() const;

    // ---- find and navigation ----
    /// The find bar over the code (Ctrl+F; with Replace: Ctrl+H): what to find, the replacement, the options.
    void OpenFindBar(const std::string& needle, bool replace = false, const FindOptions& options = FindOptions(),
                     const std::string& with = "");
    bool FindBarOpen() const;
    /// Select the next / previous match in the shown document (F3 / Shift+F3), wrapping round. False when there is none.
    bool FindNext(bool forward = true);
    /// The matches of the bar's text in the shown document, and which one is selected (-1: none).
    std::size_t FindCount();
    int FindCurrent();
    /// Replace the selected match and select the next one / replace every match in the shown document (one undo step).
    bool ReplaceNext();
    int ReplaceAllInDocument();
    /// Find in Files' "Replace in All Files": every project script and open document; open ones change in their tab (not
    /// saved), the others are written (a Local History copy first). The count replaced.
    std::size_t ReplaceInFiles(const std::string& needle, const std::string& with, const FindOptions& options = FindOptions());
    /// Go to a 1-based line (and column) of the shown document; the place before is kept for Back.
    bool GoToLine(int line, int col = 0);
    /// Go to where `word` is defined (empty: the word at the caret): this document first, then every project script. False
    /// when it is defined nowhere.
    bool GoToDefinition(const std::string& word = "");
    /// Every use of `word` (empty: the word at the caret) in the project's scripts and the open documents, shown in the
    /// References panel. The count.
    std::size_t FindReferences(const std::string& word = "");
    const std::vector<FileMatch>& References() const;
    bool OpenReference(std::size_t index);
    /// Back / Forward (Alt+Left / Alt+Right) through the places jumped from.
    bool GoBack();
    bool GoForward();
    /// The lists to pick from: 1 Go to Line (Ctrl+G), 2 Go to Symbol (Ctrl+Shift+O), 3 switch tabs (Ctrl+E), 4 bookmarks
    /// (Shift+F11), 5 Search Everywhere (Shift twice), 6 the commands (Ctrl+Shift+P). 0 closes.
    void OpenPicker(int which, const std::string& query = "");
    int PickerOpen() const;
    /// The picker's rows for its query as they are shown: "label" (detail left out).
    std::vector<std::string> PickerRows();
    /// Take the row `index` (Enter or a click does the same).
    bool PickRow(std::size_t index);
    /// Every command: the app's own and the host's.
    std::vector<AppCommand> Commands();
    /// Toggle a bookmark on the caret's line (F11; the gutter marks are the breakpoints' marks).
    void ToggleBookmark();

    /// What the command line names: a folder opens as the project, a file opens in a tab (its folder becomes the project when
    /// none is open). False when it is neither.
    bool OpenPath(const std::string& path);

    /// Draw the Script Editor in (x, y, w, h) and act on what the person did. Between OkayUI::BeginFrame and EndFrame.
    void Frame(float x, float y, float w, float h);

    /// Save the shown file / every changed file. False when a write failed (the status line says which).
    bool Save();
    bool SaveAll();
    /// Close a tab by its key (a file's path; a changed document is closed without saving: the host asks first).
    void Close(const std::string& key);

    /// Find in Files over the project's scripts (open files with their unsaved text). Fills the results the panel shows.
    std::size_t FindInFiles(const std::string& needle, const FindOptions& options = FindOptions());
    const std::vector<FileMatch>& FindResults() const;
    /// Open a Find in Files result in its tab, at its line.
    bool OpenResult(std::size_t index);

    /// The Local History versions of the shown file, newest first; Restore puts one back into its tab (one undo step).
    std::vector<HistoryVersion> HistoryOfActive() const;
    bool RestoreVersion(const HistoryVersion& v);

    /// Ctrl+Q or File > Quit (only with Config::quitItem).
    bool WantsQuit() const;
    /// The window's title: "<file> - <folder> - Script Editor" ("*" before a changed file).
    std::string WindowTitle() const;
    /// Write the settings now (also done when they change and at the end).
    void SaveSettings();

    // ---- what the hosts and the tests read ----
    const std::string& Folder() const;
    const std::vector<std::string>& ProjectScripts() const;
    std::vector<std::string> OpenFiles() const;
    std::string ActivePath() const;
    /// The shown document's key (its path for a file).
    std::string ActiveKey() const;
    /// The shown file's text in its tab (with unsaved edits) and whether it differs from the file.
    std::string ActiveText() const;
    bool ActiveDirty() const;
    /// The shown file's code widget (tests drive it; the host may set its caret).
    OkayUI::CodeEdit* ActiveEdit();
    /// The checks of the shown file as of the last check (made a short while after the last edit, and at once on open and save).
    const std::vector<Diagnostic>& ActiveProblems() const;
    /// Re-check the shown file now.
    void CheckActiveNow();
    /// The line at the bottom ("" when none): what happened last.
    const std::string& Notice() const;
    AppSettings& Settings();

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

} // namespace scriptide
