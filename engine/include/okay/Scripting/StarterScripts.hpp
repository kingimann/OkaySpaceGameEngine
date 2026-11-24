#pragma once
#include <string>

namespace okay {

/// The script a new, empty script file starts with, (OkayScript for every language name): the two
/// functions every script wants, empty, with a line saying when each runs. The
/// same shape as an empty component script in a C#-style engine. It lives here (not in the editor)
/// so a test can load it into the real script engine.
///
/// OkayScript has no class or base type: the file's fields and functions ARE the
/// component, and the engine calls Start and Update by name.
inline const char* StarterScriptSource(const std::string& lang) {
    // Lua (our own interpreter, okay::lua): a chunk returning a table; its fields are
    // the Inspector settings and the engine calls Start and Update(dt) by name.
    if (lang == "lua")
        return "local Script = {\n"
               "    speed = 1,   -- settings show in the Inspector\n"
               "}\n"
               "\n"
               "-- Runs once, before the first frame.\n"
               "function Script:Start()\n"
               "end\n"
               "\n"
               "-- Runs every frame; dt is the time since the last frame.\n"
               "function Script:Update(dt)\n"
               "end\n"
               "\n"
               "return Script\n";
    return "// Runs once, before the first frame.\n"
           "void Start()\n"
           "{\n"
           "}\n"
           "\n"
           "// Runs every frame.\n"
           "void Update()\n"
           "{\n"
           "}\n";
}

} // namespace okay
