#pragma once
// Find references across a project's graphs, and bookmarks in a graph (no ImGui, so tests run them).
//   - Find references: a node type, a variable, an event or a function, in every .okayvs file under a folder (the open
//     tabs' unsaved text wins over the file on disk). Each hit names the file and the block, so a click opens it there.
//   - Bookmarks: blocks marked to come back to (Ctrl+B on the canvas), per graph file, kept in okay_vs_bookmarks.txt.
// Files are read with the editor's own reader (VsEditGraph::fromText), so a hit is exactly a block the editor shows.
// Included from inside VisualScriptEditor.hpp after the graph model. Written for this project.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace okay::editor::vsed {

enum class VsFindWhat { NodeType, Variable, Event, Function };

inline const char* VsFindWhatLabel(VsFindWhat k) {
    switch (k) {
        case VsFindWhat::NodeType: return "block type";
        case VsFindWhat::Variable: return "variable";
        case VsFindWhat::Event:    return "event";
        case VsFindWhat::Function: return "function";
    }
    return "";
}

/// What to look for.
struct VsRefQuery { VsFindWhat kind = VsFindWhat::NodeType; std::string name; };

/// One place a reference was found: a block (node >= 0) or a declaration line (node -1).
struct VsRefHit {
    std::string path;    // the graph file
    int node = -1;       // the block's id in that file (-1: a `decl` / `var` line)
    std::string type, arg;
    std::string role;    // "reads", "sets", "calls", "defines", "sends", "listens", "uses"
};

inline bool VsIsVarNodeType(const std::string& t) {
    static const std::set<std::string> k = {
        "GetVar", "SetVar", "AddVar", "ToggleVar", "Toggle", "HasVar", "OnVarChanged",
        "GetSceneVar", "SetSceneVar", "AddSceneVar", "OnSceneVarChanged",
        "GetGlobalVar", "SetGlobalVar", "AddGlobalVar", "OnGlobalVarChanged",
        "GetLocal", "SetLocal", "GetObjectVar", "SetObjectVar", "SaveValue", "LoadValue"};
    return k.count(t) > 0;
}
inline bool VsIsEventSender(const std::string& t) {
    return t == "SendEvent" || t == "BroadcastEvent" || t == "NetSendEvent";
}

/// The query a block suggests (right-click > Find References): its variable, function or event when it has one,
/// else its type.
inline VsRefQuery VsRefQueryFor(const VsEditNode& n) {
    if (VsIsVarNodeType(n.type) && !n.arg.empty()) return {VsFindWhat::Variable, n.arg};
    if (n.type == "FunctionStart" || n.type == "CallFunction" || n.type == "Return") {
        const std::string f = okay::vs::FunctionNameOf(n.arg);
        if (!f.empty()) return {VsFindWhat::Function, f};
    }
    if ((n.type == "OnEvent" || VsIsEventSender(n.type)) && !n.arg.empty()) return {VsFindWhat::Event, n.arg};
    const std::string en = VsEntryName(n.type, n.arg);
    if (!en.empty()) return {VsFindWhat::Event, en};
    return {VsFindWhat::NodeType, n.type};
}

/// The references to `q` in one graph (already read).
inline std::vector<VsRefHit> VsFindRefsInGraph(const VsEditGraph& g, const std::string& path, const VsRefQuery& q) {
    std::vector<VsRefHit> out;
    if (q.name.empty()) return out;
    auto hit = [&](const VsEditNode& n, const char* role) { out.push_back({path, n.id, n.type, n.arg, role}); };
    for (const VsEditNode& n : g.nodes) {
        switch (q.kind) {
            case VsFindWhat::NodeType:
                if (n.type == q.name) hit(n, "uses");
                break;
            case VsFindWhat::Variable:
                if (VsIsVarNodeType(n.type) && n.arg == q.name)
                    hit(n, n.type.rfind("Get", 0) == 0 || n.type == "HasVar" || n.type == "LoadValue" ? "reads"
                         : n.type.rfind("On", 0) == 0 ? "listens" : "sets");
                break;
            case VsFindWhat::Event: {
                const std::string en = VsEntryName(n.type, n.arg);
                if (en == q.name || (n.type == q.name && !en.empty()) || (n.type == "OnEvent" && n.arg == q.name)) hit(n, "listens");
                else if (VsIsEventSender(n.type) && n.arg == q.name) hit(n, "sends");
                break;
            }
            case VsFindWhat::Function:
                if ((n.type == "FunctionStart" || n.type == "CallFunction" || n.type == "Return") && okay::vs::FunctionNameOf(n.arg) == q.name)
                    hit(n, n.type == "FunctionStart" ? "defines" : n.type == "CallFunction" ? "calls" : "returns");
                break;
        }
    }
    if (q.kind == VsFindWhat::Variable)
        for (const auto& v : g.vars) if (v.name == q.name) out.push_back({path, -1, "decl", v.type + " " + v.scope, "declares"});
    return out;
}

/// Every graph file under `root` (.okayvs), sorted, skipping hidden folders and build output. At most `limit`.
inline std::vector<std::string> VsProjectGraphFiles(const std::string& root, std::size_t limit = 4000) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && !ec; it.increment(ec)) {
        const fs::path& p = it->path();
        const std::string name = p.filename().string();
        if (it->is_directory(ec)) {
            if (!name.empty() && (name[0] == '.' || name == "build" || name == "node_modules")) it.disable_recursion_pending();
            continue;
        }
        if (p.extension() == ".okayvs") out.push_back(p.generic_string());
        if (out.size() >= limit) break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// References to `q` in every graph under `root`. `openText` gives the text of graphs open in the editor (path ->
/// text), which wins over the file on disk so unsaved edits are found too.
inline std::vector<VsRefHit> VsFindRefs(const std::string& root, const VsRefQuery& q,
                                        const std::map<std::string, std::string>& openText = {}) {
    std::vector<VsRefHit> out;
    std::set<std::string> done;
    auto scan = [&](const std::string& path, const std::string& text) {
        VsEditGraph g;
        g.fromText(text);
        auto hits = VsFindRefsInGraph(g, path, q);
        out.insert(out.end(), hits.begin(), hits.end());
    };
    for (const std::string& f : VsProjectGraphFiles(root)) {
        done.insert(f);
        auto it = openText.find(f);
        if (it != openText.end()) { scan(f, it->second); continue; }
        std::ifstream in(f, std::ios::binary);
        if (!in) continue;
        std::stringstream ss; ss << in.rdbuf();
        scan(f, ss.str());
    }
    for (const auto& kv : openText) if (!done.count(kv.first)) scan(kv.first, kv.second);   // open graphs outside the folder
    return out;
}

// ---- Bookmarks ----------------------------------------------------------------------------------------------------
struct VsBookmark { int node = -1; std::string label; };

/// Bookmarked blocks per graph file.
struct VsBookmarks {
    std::map<std::string, std::vector<VsBookmark>> byPath;
    bool has(const std::string& path, int node) const {
        auto it = byPath.find(path);
        if (it == byPath.end()) return false;
        for (const VsBookmark& b : it->second) if (b.node == node) return true;
        return false;
    }
    /// Mark the block, or unmark it when it is already marked. True when it is marked afterwards.
    bool toggle(const std::string& path, int node, const std::string& label = "") {
        auto& v = byPath[path];
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i].node == node) { v.erase(v.begin() + (long)i); if (v.empty()) byPath.erase(path); return false; }
        v.push_back({node, label});
        return true;
    }
    /// The bookmark after (`forward`) or before the block `from`, going round; -1 when the graph has none.
    int next(const std::string& path, int from, bool forward = true) const {
        auto it = byPath.find(path);
        if (it == byPath.end() || it->second.empty()) return -1;
        std::vector<int> ids;
        for (const VsBookmark& b : it->second) ids.push_back(b.node);
        std::sort(ids.begin(), ids.end());
        if (forward) { for (int id : ids) if (id > from) return id; return ids.front(); }
        for (auto r = ids.rbegin(); r != ids.rend(); ++r) if (*r < from) return *r;
        return ids.back();
    }
    /// Drop the bookmarks of `path` whose block is gone.
    template <class Graph> void prune(const std::string& path, const Graph& g) {
        auto it = byPath.find(path);
        if (it == byPath.end()) return;
        auto& v = it->second;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const VsBookmark& b) { return !VsFind(g, b.node); }), v.end());
        if (v.empty()) byPath.erase(it);
    }
    /// One line per bookmark: path, tab, node id, tab, label.
    std::string toText() const {
        std::ostringstream o;
        for (const auto& kv : byPath) for (const VsBookmark& b : kv.second) o << kv.first << '\t' << b.node << '\t' << b.label << '\n';
        return o.str();
    }
    void fromText(const std::string& text) {
        byPath.clear();
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const auto t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            const auto t2 = line.find('\t', t1 + 1);
            const std::string idText = line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
            char* end = nullptr;
            const long id = std::strtol(idText.c_str(), &end, 10);
            if (idText.empty() || (end && *end) || id < 0 || id > 1000000000L) continue;
            byPath[line.substr(0, t1)].push_back({(int)id, t2 == std::string::npos ? std::string() : line.substr(t2 + 1)});
        }
    }
};

/// The editor's bookmarks (one set for every graph), loaded from / saved to okay_vs_bookmarks.txt beside the tab list.
inline VsBookmarks& Bookmarks() {
    static VsBookmarks b;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream f("okay_vs_bookmarks.txt", std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); b.fromText(ss.str()); }
    }
    return b;
}
inline void SaveBookmarks() {
    std::ofstream f("okay_vs_bookmarks.txt", std::ios::binary);
    if (f) f << Bookmarks().toText();
}

} // namespace okay::editor::vsed
