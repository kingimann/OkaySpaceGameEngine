#pragma once
// Actions list -> OkayScript. The generated start() calls actions_load with the
// list's own text, so every step and trigger runs on an Actions component the
// same way the original list does. A step is never left as a comment.
#include "okay/Components/ActionList.hpp"
#include <string>

namespace okay::editor {

inline std::string ActionScriptString(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '\\' || c == '"') { o += '\\'; o += static_cast<char>(c); }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else o += static_cast<char>(c);
    }
    o += '"';
    return o;
}

// The whole list, including every instruction, condition, and trigger.
inline std::string ActionListToCode(const okay::ActionList& al) {
    return std::string("// Generated from the Actions list. actions_load runs every step on this object,\n"
                        "// the same way the Actions component does.\n\n"
                        "start() {\n"
                        "    actions_load(") + ActionScriptString(al.ToText()) + ")\n"
                        "}\n";
}

} // namespace okay::editor
