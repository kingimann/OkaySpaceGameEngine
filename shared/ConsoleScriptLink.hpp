#pragma once
// Which script, and which line, a Console entry points to: the entry's own source when it is a script or an asset, else a
// `.okay`, `.lua` or `.cs` file name found in the text, and a "line N" after it. The Console panel offers "Open Script" on
// exactly the entries this accepts. Shared by the Dear ImGui panel (editor/ConsolePanel.cpp) and its OkayUI port
// (okayui/screens/OkayUIScreens.cpp) so both show the button on the same lines.
#include "okay/Debug/ConsoleModel.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace okayconsole {

/// Fills `path` and `line` (0 = none) and returns true when the entry points to a script file; false (both cleared) otherwise.
inline bool ScriptLink(const okay::ConsoleEntry& e, std::string& path, int& line) {
    path.clear();
    line = e.source.line;
    if (e.source.kind == okay::ConsoleSource::Kind::Script || e.source.kind == okay::ConsoleSource::Kind::Asset)
        path = e.source.path;
    if (path.empty()) {
        for (const char* ex : {".okay", ".lua", ".cs"}) {
            std::size_t p = e.message.find(ex);
            if (p == std::string::npos) continue;
            std::size_t start = e.message.find_last_of(" \t'\"(,;", p);
            start = start == std::string::npos ? 0 : start + 1;
            path = e.message.substr(start, p + std::strlen(ex) - start);
            break;
        }
    }
    if (line <= 0)
        if (std::size_t lp = e.message.find("line "); lp != std::string::npos) line = std::atoi(e.message.c_str() + lp + 5);
    bool scriptish = false;
    for (const char* ex : {".okay", ".lua", ".cs"})
        if (path.size() >= std::strlen(ex) && path.compare(path.size() - std::strlen(ex), std::strlen(ex), ex) == 0) scriptish = true;
    if (!scriptish || path.empty()) { path.clear(); line = 0; return false; }
    return true;
}

} // namespace okayconsole
