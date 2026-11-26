#pragma once
// The Visual Script editor's side of the script IDE's shared layers (Search Everywhere, quick fixes, editor plugins),
// as plain providers with no ImGui. The script IDE (branch claude/script-ide-1) owns those layers; this file only says
// what Visual Script puts into them, so there is one Search Everywhere box, one quick-fix menu and one plugin
// registry for both editors. To connect them once both are merged (no second copy of either side):
//   - Search Everywhere: register VsSearchEverywhere(projectDir, query, open, limit) as a provider; each entry has a
//     title, a detail line, a category and (path, node) to open with okay::editor::vsed::ShowGraphFile(path, node).
//   - Quick fixes (Alt+Enter): for the graph on screen, VsQuickFixesFor(g, st.issues, node) lists the fixes at a block,
//     VsApplyQuickFix(g, fix) applies one (both use the canvas's own "Fix it for me" rules, VsFixIssue).
//   - Plugins: VsCommands() is the table of Visual Script commands (id, title, shortcut); VsRunCommand(st, id) in
//     VsIdeCanvas.hpp runs one on a graph tab, so the plugin layer can list them and a plugin can call them by id.
// Included from inside VisualScriptEditor.hpp after the graph model. Written for this project.
#include "VisualScriptSearch.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// One Search Everywhere row from the graphs of a project.
struct VsSearchEntry {
    std::string title, detail, category;   // category: "Function", "Event", "Variable", "Graph"
    std::string path;                      // the graph file to open
    int node = -1;                         // the block to show (-1: the whole graph)
    int score = 0;
};

/// The functions, events, variables and graph files of every graph under `projectDir` that match `query`, best first.
/// `open` (path -> text) is the open tabs' text, which wins over the file on disk.
inline std::vector<VsSearchEntry> VsSearchEverywhere(const std::string& projectDir, const std::string& query,
                                                     const std::map<std::string, std::string>& open = {},
                                                     std::size_t limit = 50) {
    std::vector<VsSearchEntry> out;
    auto consider = [&](VsSearchEntry e, const std::string& keywords) {
        e.score = VsMatchScore(query, e.title, keywords, e.detail);
        if (e.score > 0) out.push_back(std::move(e));
    };
    auto scanGraph = [&](const std::string& path, const std::string& text) {
        VsEditGraph g;
        g.fromText(text);
        std::string file = path;
        if (const auto s = file.find_last_of("/\\"); s != std::string::npos) file = file.substr(s + 1);
        consider({file, path, "Graph", path, -1}, "graph visual script");
        std::set<std::string> seenVars;
        for (const VsEditNode& n : g.nodes) {
            if (n.type == "FunctionStart") {
                consider({okay::vs::FunctionNameOf(n.arg), n.arg + "  -  " + file, "Function", path, n.id}, "function");
            } else if (n.type == "OnEvent" && !n.arg.empty()) {
                consider({n.arg, "custom event in " + file, "Event", path, n.id}, "event custom");
            } else if (const std::string en = VsEntryName(n.type, n.arg); !en.empty()) {
                const VsDef* d = FindDef(n.type);
                consider({d && d->title ? std::string(d->title) + (n.arg.empty() ? "" : " " + n.arg) : en, en + " in " + file, "Event", path, n.id}, "event");
            } else if (VsIsVarNodeType(n.type) && !n.arg.empty() && seenVars.insert(n.arg).second) {
                consider({n.arg, "variable used in " + file, "Variable", path, n.id}, "variable");
            }
        }
        for (const auto& v : g.vars)
            if (seenVars.insert(v.name).second) consider({v.name, v.type + " variable (" + v.scope + ") in " + file, "Variable", path, -1}, "variable");
    };
    VsForEachProjectGraph(projectDir, open, scanGraph);
    std::stable_sort(out.begin(), out.end(), [](const VsSearchEntry& a, const VsSearchEntry& b) { return a.score > b.score; });
    if (out.size() > limit) out.resize(limit);
    return out;
}

/// One quick fix the canvas can apply.
struct VsQuickFix {
    std::string title;      // "Create variable 'score'"
    int node = -1;          // the block it is about
    std::string message;    // the checker finding it fixes (as the checker wrote it)
    int pin = -1, pinDir = 0;
};

/// The fixes for the checker's findings at block `node` (-1: every block). Only fixes that would really change the
/// graph are listed (each one is tried on a copy).
inline std::vector<VsQuickFix> VsQuickFixesFor(const VsEditGraph& g, const std::vector<okay::vs::VsIssue>& issues, int node = -1) {
    std::vector<VsQuickFix> out;
    for (const auto& is : issues) {
        if (node >= 0 && is.node != node) continue;
        if (!VsFixAvailable(g, is.message, is.node, is.pin, is.pinDir)) continue;
        const std::string title = VsFixLabel(g, is.message, is.node, is.pin, is.pinDir);
        out.push_back({title.empty() ? std::string(VsPlainFixIt()) : title, is.node, is.message, is.pin, is.pinDir});
    }
    return out;
}
inline bool VsApplyQuickFix(VsEditGraph& g, const VsQuickFix& f) { return VsFixIssue(g, f.message, f.node, f.pin, f.pinDir); }

/// A Visual Script command a plugin (or the command palette) can list and run by id.
struct VsCommand { const char* id; const char* title; const char* shortcut; };
inline const std::vector<VsCommand>& VsCommands() {
    static const std::vector<VsCommand> k = {
        {"vs.collapseToFunction", "Collapse Selection to Function", ""},
        {"vs.alignLeft", "Align Left", "Shift+A"},
        {"vs.alignRight", "Align Right", "Shift+D"},
        {"vs.alignTop", "Align Top", "Shift+W"},
        {"vs.alignBottom", "Align Bottom", "Shift+S"},
        {"vs.alignCenter", "Align Centers (vertical line)", ""},
        {"vs.alignMiddle", "Align Middles (horizontal line)", ""},
        {"vs.distributeH", "Distribute Horizontally", "Shift+H"},
        {"vs.distributeV", "Distribute Vertically", "Shift+V"},
        {"vs.straighten", "Straighten Wire", "Q"},
        {"vs.autoArrange", "Auto-Arrange Selection", "Shift+L"},
        {"vs.toggleBookmark", "Toggle Bookmark", "Ctrl+B"},
        {"vs.nextBookmark", "Next Bookmark", "F2"},
        {"vs.prevBookmark", "Previous Bookmark", "Shift+F2"},
        {"vs.findReferences", "Find References", "Shift+F12"},
        {"vs.findInGraphs", "Find in All Graphs", "Ctrl+Shift+F"},
        {"vs.watchValues", "Watch Values of Selection", ""},
        {"vs.fixAll", "Apply Every Quick Fix", ""},
    };
    return k;
}

} // namespace okay::editor::vsed
