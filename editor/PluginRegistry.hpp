#pragma once
// PluginRegistry: what editor plugins add to a host window (the Script Editor now; the Browser later, with its own instance):
// commands with shortcuts (the command palette, Search Everywhere and the menus list them), completion, lint (diagnostics
// with quick fixes), formatters, hover text, snippets and side panels. Part of the Editor Plugins system
// (docs/editor/editor-plugins.md): the same plugin files, the same Plugin Manager and the same on / off switch. Every entry
// carries its owner (the plugin's file path), so turning a plugin off or reloading it removes everything it added
// (RemoveOwner). No GUI and no script VM here: the OkayScript side is editor/PluginScriptApi.cpp, the drawing is the host's.
//
// The Browser (branch claude/browser-full-1) reuses this as it is: it makes its own PluginRegistry, gives its documents
// host "browser" in DocContext, and binds the same calls (PluginScriptApi) under its own prefix.
#include "ScriptIde.hpp"   // scriptide::Diagnostic, QuickFix, FuzzyScore

#include <functional>
#include <string>
#include <vector>

namespace pluginreg {

/// A key chord: "Ctrl+Shift+P", "Alt+Enter", "F12", "Ctrl+/". Key names are upper case ("P", "F12", "ENTER", "/").
struct Shortcut {
    bool ctrl = false, shift = false, alt = false;
    std::string key;
    bool Empty() const { return key.empty(); }
    bool operator==(const Shortcut& o) const { return ctrl == o.ctrl && shift == o.shift && alt == o.alt && key == o.key; }
};
/// False for text that is not a chord (no key, an unknown modifier, two keys).
bool ParseShortcut(const std::string& text, Shortcut& out);
std::string FormatShortcut(const Shortcut& s);

struct Command {
    std::string id, title, category, shortcut, owner;   ///< owner: the plugin's path ("" = built in)
    std::function<void()> run;
};
/// Commands matching a palette query, best first (fuzzy on the title, then on "category: title"); an empty query keeps the
/// order given.
std::vector<const Command*> RankCommands(const std::string& query, const std::vector<const Command*>& commands);

/// The document a provider is asked about.
struct DocContext {
    std::string host = "script-editor";   ///< "script-editor" (or "browser")
    std::string language, path, text;
    int line = 0, col = 0;                ///< the caret: 1-based line, 0-based column
    std::string word;                     ///< the word at the caret or under the pointer (completion: the prefix typed)
};
struct CompletionItem { std::string label, insert, detail, owner; };
struct Snippet { std::string prefix, title, body, language, owner; };
struct SidePanel { std::string id, title, owner; std::function<void()> draw; };

using CompletionFn = std::function<std::vector<CompletionItem>(const DocContext&)>;
using LintFn = std::function<std::vector<scriptide::Diagnostic>(const DocContext&)>;
using FormatFn = std::function<bool(const DocContext&, std::string& out)>;   ///< true and the new text, or false
using HoverFn = std::function<std::string(const DocContext&)>;

class PluginRegistry {
public:
    // ---- commands ----
    /// Add a command (one with the same id is replaced). False without an id or a run function.
    bool AddCommand(Command c);
    bool RemoveCommand(const std::string& id);
    const Command* FindCommand(const std::string& id) const;
    std::vector<const Command*> Commands() const;
    const Command* CommandForShortcut(const Shortcut& s) const;
    bool Run(const std::string& id) const;

    // ---- providers (language "" or "*": every language) ----
    void AddCompletion(const std::string& owner, const std::string& language, CompletionFn fn);
    void AddLint(const std::string& owner, const std::string& language, LintFn fn);
    void AddFormatter(const std::string& owner, const std::string& language, FormatFn fn);
    void AddHover(const std::string& owner, const std::string& language, HoverFn fn);
    void AddSnippet(Snippet s);
    void AddPanel(SidePanel p);

    /// Every provider for the document's language, each one's failure (an exception) skipped. Lint diagnostics get the
    /// owner's name as their source when they have none.
    std::vector<CompletionItem> Complete(const DocContext& ctx) const;
    std::vector<scriptide::Diagnostic> Lint(const DocContext& ctx) const;
    /// The first formatter for the language that answers.
    bool Format(const DocContext& ctx, std::string& out) const;
    /// Hover texts of every provider, joined by blank lines.
    std::string Hover(const DocContext& ctx) const;
    std::vector<Snippet> Snippets(const std::string& language) const;
    const std::vector<SidePanel>& Panels() const { return panels_; }
    bool HasFormatter(const std::string& language) const;

    /// Remove everything one plugin added; returns how many entries went.
    int RemoveOwner(const std::string& owner);
    /// The name shown for an owner (a plugin's @name); falls back to the file name.
    void SetOwnerName(const std::string& owner, const std::string& name);
    std::string OwnerName(const std::string& owner) const;
    void Clear();

private:
    template <class F> struct Provider { std::string owner, language; F fn; };
    static bool ForLanguage(const std::string& want, const std::string& lang) { return want.empty() || want == "*" || want == lang; }
    std::vector<Command> commands_;
    std::vector<Provider<CompletionFn>> completions_;
    std::vector<Provider<LintFn>> lints_;
    std::vector<Provider<FormatFn>> formatters_;
    std::vector<Provider<HoverFn>> hovers_;
    std::vector<Snippet> snippets_;
    std::vector<SidePanel> panels_;
    std::vector<std::pair<std::string, std::string>> names_;
};

/// The Script Editor's Problems list for a document: the editor's own findings (`own`: the language checker's and the
/// built-in lint's) followed, line by line, by every plugin lint's (a stable merge by line).
std::vector<scriptide::Diagnostic> ScriptProblems(const PluginRegistry& reg, const DocContext& ctx, std::vector<scriptide::Diagnostic> own);

/// The Script Editor's registry (one per editor).
PluginRegistry& ScriptEditorPlugins();

} // namespace pluginreg
