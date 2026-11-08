#pragma once
#include <string>

namespace okay {

/// The script a new, empty script file starts with, per language: the two
/// functions every script wants, empty, with a line saying when each runs. The
/// same shape as an empty component script in a C#-style engine. It lives here (not in the editor)
/// so a test can load it into the real script engine.
///
/// OkayScript has no class or base type: the file's fields and functions ARE the
/// component, and the engine calls Start and Update by name. Lua and C# use the
/// shapes their backends expect.
inline const char* StarterScriptSource(const std::string& lang) {
    if (lang == "lua")
        return "-- Runs once, before the first frame.\n"
               "function start()\n"
               "end\n"
               "\n"
               "-- Runs every frame; dt is the seconds since the last one.\n"
               "function update(dt)\n"
               "end\n";
    if (lang == "csharp")
        return "class Script {\n"
               "    // Runs once, before the first frame.\n"
               "    void Start() {\n"
               "    }\n"
               "\n"
               "    // Runs every frame; dt is the seconds since the last one.\n"
               "    void Update(float dt) {\n"
               "    }\n"
               "}\n";
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
