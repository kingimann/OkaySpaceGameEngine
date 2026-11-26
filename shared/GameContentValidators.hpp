#pragma once
// The safety checks scripts and graphs that arrive as content go through BEFORE they become visible (ContentSync's `validate` hook).
// Docs: docs/release/content_delivery.md "Scripts and graphs". Uses the engine's existing parsers; adds no new interpreter and no new
// capability: a downloaded graph or script runs with exactly the powers any shipped one has (ScriptPathOk sandbox for files, the same
// node and builtin set), and nothing downloaded is ever loaded as native code.
//   .okayvs      okay::vs::ValidateGraphText (the graph checker the editor and Project diagnostics use): any ERROR (severity 0) refuses it,
//                a parse failure is an error too. Warnings are allowed.
//   .okayscript  okay::OkayScriptVM::Validate (parse / syntax check, does not run anything).
//   other kinds  accepted (no code in them); the manifest already refuses executable extensions.
#include <cctype>
#include <string>

#include "ContentSync.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/VisualScript/VsTypes.hpp"

namespace okaycontent {

inline std::string ValidateContentFile(const Entry& e, const std::string& path) {
    std::string ext;
    const std::size_t dot = e.path.rfind('.');
    if (dot != std::string::npos) for (std::size_t i = dot; i < e.path.size(); ++i) ext += static_cast<char>(std::tolower(static_cast<unsigned char>(e.path[i])));
    if (ext != ".okayvs" && ext != ".okayscript") return std::string();
    std::string text;
    if (!detail::ReadAll(detail::P(path), text, 4u << 20) || text.empty()) return "the file is empty, too large (over 4 MB) or unreadable";
    if (ext == ".okayvs") {
        // The graph loader quietly ignores lines it does not know, so garbage parses as an empty graph. A downloaded graph must be a graph:
        // every line is blank, a '#' comment, or starts with a directive NodeGraph::Parse knows, and there is at least one node.
        static const char* kDirectives[] = {"node", "exec", "data", "entry", "default", "var", "struct", "decl", "pos", "comment", "knot"};
        int nodes = 0, lineNo = 0;
        std::size_t at = 0;
        while (at <= text.size()) {
            std::size_t e = text.find('\n', at);
            if (e == std::string::npos) e = text.size();
            std::string line = text.substr(at, e - at);
            at = e + 1;
            ++lineNo;
            const std::size_t a = line.find_first_not_of(" \t\r");
            if (a == std::string::npos || line[a] == '#') continue;
            const std::string word = line.substr(a, line.find_first_of(" \t\r", a) - a);
            bool known = false;
            for (const char* d : kDirectives) if (word == d) known = true;
            if (!known) return "line " + std::to_string(lineNo) + " is not part of a graph";
            if (word == "node") ++nodes;
        }
        if (nodes == 0) return "the graph has no nodes";
        for (const okay::vs::VsIssue& is : okay::vs::ValidateGraphText(text, std::string()))
            if (is.severity == 0) return "the graph has an error: " + is.message;
        return std::string();
    }
    okay::OkayScriptVM vm;
    std::string err;
    if (!vm.Validate(text, &err)) return "the script does not parse: " + err;
    return std::string();
}

}  // namespace okaycontent
