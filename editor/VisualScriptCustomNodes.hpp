#pragma once
// The Visual Script editor's side of custom nodes (feature map 24.3): "Edit node" on a custom node, and "Make a custom version"
// on a built-in or Engine API node. The nodes themselves, their declarations and the script bodies are the engine's
// (okay/VisualScript/VsCustomNodes.hpp); this file only builds the request from a block and changes the graph. Built-in nodes
// are never changed: "Make a custom version" writes a new script in the project and swaps the block for the new node.
// Written for this project.
#include "VisualScriptModel.hpp"
#include "okay/VisualScript/VsCustomNodes.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace okay::editor::vsed {

/// Where a custom node is written: its script and the 1-based line of its definition. False for any other block.
inline bool VsCustomNodeSource(const VsEditNode& n, std::string& path, int& line) {
    okay::vs::VsNodeInfo info;
    if (!okay::vs::IsCustomNodeType(n.type) || !okay::vs::CustomVsNodeInfo(n.type, info) || info.source.empty()) return false;
    path = info.source;
    line = info.line;
    return true;
}

/// Why a block can't get a custom version ("" = it can). Events, functions, loops, Sequence, Switch, the Actions blocks and
/// Quick blocks run other chains or start them, which a script body can't do yet.
inline std::string VsCustomVersionRefusal(const VsEditGraph& g, const VsEditNode& n) {
    const VsDef* d = VsDefFor(g, n);
    if (!d) return "this block is not known";
    if (okay::vs::IsCustomNodeType(n.type)) return "this is already a custom node: use Edit node";
    if (!d->execIn && !d->execOut.empty()) return "an event starts a chain; a custom node can't";
    if (IsFunctionNode(n.type) || IsSwitchNode(n.type)) return "functions and Switch blocks can't have custom versions";
    if (okay::vs::FindQuickBlock(n.type) || n.type == "Action" || n.type == "Actions" || n.type == "Condition")
        return "Quick blocks and Actions blocks can't have custom versions";
    if (n.type == "Sequence") return "Sequence runs other chains; a custom node can't";
    for (const char* e : d->execOut) {
        const std::string l = e ? e : "";
        if (l == "Body" || l == "Each" || l == "Loop") return "a loop runs other chains; a custom node can't";
    }
    return std::string();
}

/// What the engine needs to write a custom version of block `n`: its pins (labels, types), title, help and setting.
inline okay::vs::CustomVersionSpec VsCustomVersionSpecFor(const VsEditGraph& g, const VsEditNode& n) {
    okay::vs::CustomVersionSpec s;
    const VsDef* d = VsDefFor(g, n);
    if (!d) return s;
    s.baseType = n.type;
    s.baseArg = n.arg;
    s.title = VsNodeTitle(n, *d);
    if (const char* h = VsNodeHelp(n.type.c_str())) s.help = h;
    const VsPlace place = VsPlaceForNode(n.type.c_str());
    s.category = place.category ? place.category : "Custom";
    s.execIn = d->execIn;
    for (const char* e : d->execOut) s.exits.push_back(e ? e : "");
    for (int i = 0; i < (int)d->dataIn.size(); ++i) {
        okay::vs::VsPinSpec p;
        p.name = d->dataIn[(std::size_t)i] ? d->dataIn[(std::size_t)i] : "In";
        p.type = VsInPinType(g, n.id, i);
        s.ins.push_back(p);
    }
    for (int i = 0; i < (int)d->dataOut.size(); ++i) {
        okay::vs::VsPinSpec p;
        p.name = d->dataOut[(std::size_t)i] ? d->dataOut[(std::size_t)i] : "Out";
        p.type = VsOutPinType(g, n.id, i);
        s.outs.push_back(p);
    }
    return s;
}

/// The file extension a custom-node script of `language` gets.
inline const char* VsCustomNodeExtension(const std::string& language) {
    return language == "lua" ? ".lua" : language == "cs" ? ".cs" : ".okay";
}

/// The folder new custom-node scripts go in: <project>/Assets/Nodes when there is a project, else a Nodes folder beside the graph.
inline std::string VsCustomNodeFolder(const std::string& projectDir, const std::string& graphPath) {
    namespace fsys = std::filesystem;
    std::error_code ec;
    if (!projectDir.empty()) {
        const fsys::path assets = fsys::u8path(projectDir) / "Assets";
        return ((fsys::is_directory(assets, ec) ? assets : fsys::u8path(projectDir)) / "Nodes").generic_u8string();
    }
    if (!graphPath.empty()) return (fsys::u8path(graphPath).parent_path() / "Nodes").generic_u8string();
    return "Nodes";
}

struct VsCustomVersionResult {
    bool ok = false;
    std::string why;      ///< when not ok
    std::string path;     ///< the new script
    std::string type;     ///< the new node type ("Custom.add_custom")
    int line = 0;         ///< its definition's line in the script
};

/// "Make a custom version" of block `nodeId` in `language` ("okayscript", "lua", "cs"): write a new script into `folder` with the
/// same pins (named "<title> (custom)"), load it, and put the new node in the block's place (same id, position, wires and typed
/// values), as one change of the graph (one undo step). The built-in node is not changed.
inline VsCustomVersionResult VsMakeCustomVersion(VsEditGraph& g, int nodeId, const std::string& language, const std::string& folder) {
    namespace fsys = std::filesystem;
    VsCustomVersionResult r;
    VsEditNode* n = g.find(nodeId);
    if (!n) { r.why = "there is no such block"; return r; }
    if (const std::string no = VsCustomVersionRefusal(g, *n); !no.empty()) { r.why = no; return r; }
    const okay::vs::CustomVersionSpec spec = VsCustomVersionSpecFor(g, *n);
    // A function name that is neither a registered node nor a file already in the folder.
    std::string fn = okay::vs::CustomVersionFunctionName(spec.title, language);
    std::error_code ec;
    fsys::path file = fsys::u8path(folder) / (fn + VsCustomNodeExtension(language));
    for (int k = 2; fsys::exists(file, ec) && k < 1000; ++k) {
        fn = okay::vs::CustomVersionFunctionName(spec.title + " " + std::to_string(k), language);
        file = fsys::u8path(folder) / (fn + VsCustomNodeExtension(language));
    }
    std::string text, why;
    if (!okay::vs::MakeCustomVersionScript(spec, language, fn, text, &why)) { r.why = why; return r; }
    fsys::create_directories(fsys::u8path(folder), ec);
    {
        std::ofstream f(file, std::ios::binary);
        if (!f) { r.why = "can't write " + file.generic_u8string(); return r; }
        f << text;
    }
    r.path = file.generic_u8string();
    std::string err;
    if (!okay::vs::LoadCustomNodeScript(r.path, &err)) { r.why = "the new script did not load: " + err; return r; }
    r.type = std::string(okay::vs::CustomNodePrefix()) + fn;
    okay::vs::VsNodeInfo info;
    if (!okay::vs::CustomVsNodeInfo(r.type, info)) { r.why = "the new node was not registered"; return r; }
    r.line = info.line;
    // The block becomes the new node: same id, so every wire and typed value stays; the setting moved into the script (@base).
    n->type = r.type;
    n->arg.clear();
    g.dirty = true;
    r.ok = true;
    return r;
}

} // namespace okay::editor::vsed
