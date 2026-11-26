#pragma once
// ScriptIde: the Script Editor's workspace model, with no GUI (no Dear ImGui, no OkayUI, no SDL), so tests/test_script_ide.cpp
// runs it headless. The Script Editor (shared/ScriptIdeApp.hpp: the program and the editor's Script Editor window)
// draw it; this file only decides.
//
//  - Fuzzy matching (FuzzyScore) for Quick Open, Search Everywhere, the command palette and autocomplete.
//  - Quick Open: every script in the project, ranked, recently opened files first (RankQuickOpen, RecentFiles).
//  - Project script list and the script file tree (ListProjectScripts, BuildFileTree).
//  - Symbol index over every project script (SymbolIndex): definitions, references, the outline of a file, rename across
//    files with a preview (PlanRename). Built off the main thread by SymbolIndexBuilder (okay::thr, rule 21a).
//  - Find / replace in one text with plain or regex search, case and whole-word options (FindAll, ReplaceAll), and across
//    files (FindInTexts).
//  - Text edits and quick fixes (TextEdit, QuickFix, Diagnostic, ApplyEdits); the built-in fixes: an unknown engine name
//    becomes the nearest known one, an undeclared variable gets declared, an unused local is removed (BasicLint).
//  - Line comment toggle over line ranges (ToggleComment), the parameter hint at the caret (CallAtCaret).
//  - Script templates per language for File > New Script (TemplatesFor).
//  - Local history: a copy of a script on every save and every few minutes of edits, kept in the project's hidden
//    .okay/history folder with a size bound, a line diff against the current text and restore (LocalHistory, LineDiff).
//
// Written from general editor ideas (fuzzy subsequence matching, an inverted index of identifiers, LCS line diff); no code
// from any other editor (rule 19a).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace scriptide {

// ---- fuzzy matching ----------------------------------------------------------------------------------------------------
/// Score of `pat` as a case-insensitive subsequence of `cand` (higher is better, negative = no match, 0 for an empty pattern).
/// Runs of consecutive letters, word starts (after '_', '.', '/', ' ', or a lower-to-upper change) and an early first hit score more.
int FuzzyScore(const std::string& pat, const std::string& cand);

// ---- languages -----------------------------------------------------------------------------------------------------------
/// The script language of a file by its extension: "okayscript" (.okay), "lua" (.lua), "csharp" (.cs), "" for anything else.
std::string LanguageForPath(const std::string& path);
/// The line comment of a language ("--" for Lua, "//" otherwise).
std::string LineCommentFor(const std::string& language);
/// 1 for every byte that is code (not a comment and not inside a string) in a script of this language.
std::vector<char> CodeMaskFor(const std::string& language, const std::string& text);
/// The language's keywords (never symbols, never renamed to).
bool IsKeyword(const std::string& language, const std::string& word);

// ---- recent files and Quick Open -----------------------------------------------------------------------------------------
class RecentFiles {
public:
    explicit RecentFiles(std::size_t capacity = 30) : cap_(capacity) {}
    /// Put `path` at the front (moving it if it was there already).
    void Push(const std::string& path);
    void Remove(const std::string& path);
    const std::vector<std::string>& Items() const { return items_; }   // most recent first
    /// One path per line, most recent first; Parse reads it back (blank lines skipped, capacity kept).
    std::string Serialize() const;
    void Parse(const std::string& text);
private:
    std::size_t cap_;
    std::vector<std::string> items_;
};

struct QuickOpenItem {
    std::string path;    ///< the file
    std::string label;   ///< what the list shows: the path relative to the root
    int score = 0;
    bool recent = false;
};
/// "Player:12" -> {"Player", 12}; a query with no ":<number>" keeps line 0.
struct QuickOpenQuery { std::string text; int line = 0; };
QuickOpenQuery ParseQuickOpenQuery(const std::string& q);
/// Rank `files` for a Quick Open query. Empty query: the recent files (in order) then the rest alphabetically. Otherwise
/// every fuzzy match of the file name (or, weaker, of its relative path), a recent file ranking above an equal match.
std::vector<QuickOpenItem> RankQuickOpen(const std::string& query, const std::vector<std::string>& files,
                                         const std::vector<std::string>& recent, const std::string& root = "",
                                         std::size_t maxItems = 50);

/// Every script (.okay, .lua, .cs) under `root`, sorted, skipping hidden folders (a name that starts with '.'); at most `limit`.
std::vector<std::string> ListProjectScripts(const std::string& root, std::size_t limit = 5000);

/// The script file tree: folders and files under the root, filtered by a case-insensitive substring of the relative path
/// (a folder stays when anything inside it matches). Depth-first, folders before files, each level alphabetical.
struct TreeRow { std::string name, path; int depth = 0; bool folder = false; };
std::vector<TreeRow> BuildFileTree(const std::vector<std::string>& files, const std::string& root, const std::string& filter);

// ---- symbols ------------------------------------------------------------------------------------------------------------
struct Symbol {
    std::string name;      ///< the plain name ("Fire"; Lua "Script:Update" is "Update")
    std::string display;   ///< what lists show ("Gun.Fire", "Script:Update")
    char kind = 'f';       ///< 'f' function, 'C' class / table, 'E' enum, 'v' variable
    std::string file;
    int line = 0;          ///< 1-based
};
struct Occurrence {
    std::string file;
    int line = 0;          ///< 1-based
    int col = 0;           ///< 0-based byte column
    std::size_t offset = 0;
};

/// The declarations of one script (functions, classes, enums; Lua: functions, methods, tables and top-level locals).
std::vector<Symbol> OutlineOf(const std::string& language, const std::string& text, const std::string& file = "");

/// Every project script's symbols and the places each identifier is used in code. Filled from texts (SetFile), so the
/// editor can give it the open buffers' unsaved text.
class SymbolIndex {
public:
    void SetFile(const std::string& path, const std::string& text, const std::string& language = "");
    void RemoveFile(const std::string& path);
    void Clear() { files_.clear(); }
    bool HasFile(const std::string& path) const { return files_.count(path) != 0; }
    std::size_t FileCount() const { return files_.size(); }
    const std::string* Text(const std::string& path) const;
    std::vector<std::string> Files() const;

    std::vector<Symbol> Definitions(const std::string& name) const;
    std::vector<Symbol> FileSymbols(const std::string& path) const;
    std::vector<Symbol> AllSymbols() const;
    /// Every use of the identifier in code (comments and strings skipped), file by file, in text order.
    std::vector<Occurrence> References(const std::string& name) const;
    /// The identifier at a 1-based line and 0-based column of a file ("" when none).
    std::string WordAt(const std::string& path, int line, int col) const;

private:
    struct FileData {
        std::string language, text;
        std::vector<Symbol> symbols;
        std::unordered_map<std::string, std::vector<Occurrence>> uses;
    };
    std::map<std::string, FileData> files_;
};

/// Rename across files: every code use of `from` becomes `to`.
struct RenameChange { std::string file; int line = 0; std::string before, after; };
struct RenamePlan {
    std::string from, to, error;                      ///< error: why it cannot be done ("" = fine)
    std::vector<RenameChange> changes;                ///< the preview: one row per changed line
    std::map<std::string, std::string> newTexts;      ///< file -> its text after the rename
    int count = 0;                                    ///< occurrences replaced
};
RenamePlan PlanRename(const SymbolIndex& index, const std::string& from, const std::string& to);

/// Builds a SymbolIndex from files on disk on a worker thread (okay::thr). Start() while one runs is refused.
class SymbolIndexBuilder {
public:
    SymbolIndexBuilder();
    ~SymbolIndexBuilder();
    SymbolIndexBuilder(const SymbolIndexBuilder&) = delete;
    SymbolIndexBuilder& operator=(const SymbolIndexBuilder&) = delete;
    bool Start(const std::vector<std::string>& files);
    bool Busy() const;
    /// When a build finished: move its index into `out` and return true (once per build).
    bool Poll(SymbolIndex& out);
    /// Wait for a running build (tests, shutdown).
    void Wait();
    struct Job;
private:
    Job* job_ = nullptr;
};

// ---- find and replace ---------------------------------------------------------------------------------------------------
struct FindOptions { bool regex = false, matchCase = false, wholeWord = false; };
struct TextMatch { std::size_t offset = 0, length = 0; int line = 0, col = 0; };   // line 1-based, col 0-based bytes
/// Every match of `needle` in `text`. A bad regex returns nothing and sets `error`.
std::vector<TextMatch> FindAll(const std::string& text, const std::string& needle, const FindOptions& opt,
                               std::string* error = nullptr);
/// Replace every match; a regex replacement can use $1.. and $&. `count` gets the number replaced.
std::string ReplaceAll(const std::string& text, const std::string& needle, const std::string& replacement,
                       const FindOptions& opt, int* count = nullptr, std::string* error = nullptr);
struct FileMatch { std::string file; TextMatch match; std::string lineText; };
std::vector<FileMatch> FindInTexts(const std::map<std::string, std::string>& texts, const std::string& needle,
                                   const FindOptions& opt, std::size_t maxResults = 2000, std::string* error = nullptr);

// ---- edits, diagnostics and quick fixes ---------------------------------------------------------------------------------
/// Replace `length` bytes at 1-based `line`, 0-based byte `col` with `text` (length 0: insert). A col past the end of the
/// line is the end of the line; line 0 or past the end is the end of the text.
struct TextEdit { int line = 1, col = 0, length = 0; std::string text; };
struct QuickFix { std::string title; std::vector<TextEdit> edits; };
struct Diagnostic {
    int line = 0, col = 0;          ///< 1-based line, 1-based column (0: the whole line)
    int severity = 1;               ///< 0 error, 1 warning, 2 info
    std::string message, source;    ///< source: "lint", a plugin's name...
    std::vector<QuickFix> fixes;
};
/// Apply edits to a text (from the last to the first, so positions stay valid).
std::string ApplyEdits(const std::string& text, std::vector<TextEdit> edits);
/// The known name nearest to `word` by single-letter edits (okay::editor::vsed::searchnav::EditDistance; at most
/// max(1, size / 3) edits, the first of equals wins), "" when none.
std::string NearestName(const std::string& word, const std::vector<std::string>& known);
/// The built-in checks with quick fixes: a call to a name that is neither defined in the file nor known (fix: the nearest
/// known name), a name that is read but never declared (fix: declare it above the line: `local x = nil` in Lua, `var x = 0`
/// in OkayScript), and a local that is declared but never used (fix: remove the line). `known`: the engine's names.
std::vector<Diagnostic> BasicLint(const std::string& language, const std::string& text, const std::vector<std::string>& known);
/// A quick fix for a checker's message that ends in "Did you mean X?" (the Lua checker and the typed checker say that):
/// replace the word at the diagnostic's column on its line with X. Empty title when the message has no suggestion.
QuickFix FixFromSuggestion(const Diagnostic& d, const std::string& text);

// ---- small editing helpers ----------------------------------------------------------------------------------------------
/// Toggle the line comment on lines [first, last] (1-based): add `prefix` + ' ' before the first non-blank of every non-blank
/// line, or remove it when every such line has it.
std::string ToggleComment(const std::string& text, int first, int last, const std::string& prefix);
/// The call the caret is inside: the function name and which argument (0-based) the caret is in; empty name when none.
struct CaretCall { std::string name; int argIndex = 0; };
CaretCall CallAtCaret(const std::string& text, std::size_t caretOffset);
/// Byte offset of a 1-based line and 0-based column.
std::size_t OffsetOf(const std::string& text, int line, int col);
/// Format Document for brace languages (OkayScript, C#, the fallback): every line re-indented by its brace depth, 4 spaces a level,
/// counting only braces that are code (not in strings, comments or $"...{x}..." holes); trailing blanks dropped; a line that starts
/// with '}' sits at its block's level.
std::string ReindentByBraces(const std::string& text);
/// Join lines: line `first` (1-based) and the next `count` lines become one, each joined line's leading blanks replaced by one
/// space (none before a ')' ']' '}' ',' ';' or after a '(' '[' '{'). Lines past the end are ignored.
std::string JoinLines(const std::string& text, int first, int count = 1);

// ---- templates ----------------------------------------------------------------------------------------------------------
struct ScriptTemplate { std::string id, title, body; };
/// The New Script templates of a language ("okayscript", "lua", "csharp"); `{Name}` in a body is the script's name.
/// The first is the language's starter (okay::StarterScriptSource).
std::vector<ScriptTemplate> TemplatesFor(const std::string& language);
std::string FillTemplate(const std::string& body, const std::string& name);

// ---- snippets and reference ---------------------------------------------------------------------------------------------------
/// A piece of code Insert puts at the caret: its group, title, what it does, the code and its language.
struct Snippet { std::string group, title, description, body, language; };
/// Insert > Behaviour Template (plain-English OkayScript behaviours, "Basics" to "Multiplayer") and the C# snippets (group "C#").
const std::vector<Snippet>& BuiltinSnippets();
/// Help > C# Quick Reference: rows of {code, what it does}.
const std::vector<std::pair<std::string, std::string>>& CSharpQuickReference();
/// What Insert > API Command puts at the caret for a function: its whole signature when it is a plain call ("move(x, y)"), else
/// the name and "(".
std::string CallTemplate(const std::string& name, const std::string& signature);

// ---- new script files -----------------------------------------------------------------------------------------------------
/// The file extension of a script language, without the dot: "lua", "js", "cs", else "okay".
std::string ExtensionFor(const std::string& language);
/// A file stem as a class name: letters, digits and '_' kept, anything else '_', "_" before a leading digit, "NewScript" for "".
std::string ClassIdentifier(const std::string& stem);
/// Rename the first `class <X>` of a script to `newName` (the class follows the file's name); the text as it is when it has none.
std::string RenameScriptClass(const std::string& src, const std::string& newName);
/// The language's starter script (okay::StarterScriptSource) with its class named after the file stem.
std::string StarterScriptNamed(const std::string& language, const std::string& stem);
/// What New Script makes of a typed name: letters, digits and '_' only, a trailing "okay" left by a typed ".okay" dropped, and
/// "Script" before a name that is empty or starts with a digit.
std::string ScriptStem(const std::string& typed);
/// Where a new script, or a script with no file yet, is saved: <project>/Assets/Scripts/<stem>.<extension> ("Assets/Scripts/..."
/// relative when there is no project). '/' separators.
std::string DefaultScriptPath(const std::string& projectDir, const std::string& stem, const std::string& language);

// ---- local history ------------------------------------------------------------------------------------------------------
struct HistoryVersion { std::string file; std::int64_t time = 0; std::string label; std::size_t bytes = 0; };
/// Copies of scripts kept in `<project>/.okay/history/` (one folder per script, a file per version named by its time).
/// At most `maxPerFile` versions per script and `maxBytes` in all (the oldest go first). Same text as the newest copy: not
/// kept again.
class LocalHistory {
public:
    explicit LocalHistory(std::string projectDir = "", std::size_t maxPerFile = 50, std::size_t maxBytes = 20u << 20);
    void SetProject(const std::string& projectDir) { dir_ = projectDir; }
    const std::string& Project() const { return dir_; }
    /// Keep a version of `scriptPath` now (`timeMs`: milliseconds since 1970; label: "Saved", "Auto"...). False when nothing
    /// was written (no project, same text as the last copy, or a write error).
    bool Record(const std::string& scriptPath, const std::string& text, std::int64_t timeMs, const std::string& label);
    /// Keep a version while editing: only when `intervalMs` passed since this script's last copy.
    bool MaybeRecord(const std::string& scriptPath, const std::string& text, std::int64_t timeMs, std::int64_t intervalMs = 5 * 60 * 1000);
    /// The kept versions of a script, newest first.
    std::vector<HistoryVersion> Versions(const std::string& scriptPath) const;
    std::string Load(const HistoryVersion& e) const;
    /// The folder a script's versions are kept in.
    std::string FolderFor(const std::string& scriptPath) const;
private:
    void Prune(const std::string& folder);   // drop the oldest copies over the bounds
    std::string dir_;
    std::size_t maxPerFile_, maxBytes_;
    std::unordered_map<std::string, std::int64_t> last_;
};
/// A line diff (longest common subsequence): ' ' same, '-' only in `a`, '+' only in `b`.
struct DiffLine { char op = ' '; std::string text; };
std::vector<DiffLine> LineDiff(const std::string& a, const std::string& b);

} // namespace scriptide
