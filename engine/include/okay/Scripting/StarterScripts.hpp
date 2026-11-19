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
    // Lua: the two global functions the engine calls, empty (okay::LuaScriptVM).
    if (lang == "lua")
        return "-- Runs once, before the first frame.\n"
               "function Start()\n"
               "end\n"
               "\n"
               "-- Runs every frame. dt is the seconds since the last frame.\n"
               "function Update(dt)\n"
               "end\n";
    // C# (real C# through the hosted .NET runtime, engine/src/CSharp): a class that inherits Behaviour.
    if (lang == "csharp" || lang == "cs")
        return "using OkayScripting;\n"
               "\n"
               "public class NewScript : Behaviour\n"
               "{\n"
               "    public float speed = 1f;   // public fields show in the Inspector\n"
               "\n"
               "    // Runs once, before the first frame.\n"
               "    void Start()\n"
               "    {\n"
               "    }\n"
               "\n"
               "    // Runs every frame.\n"
               "    void Update()\n"
               "    {\n"
               "        transform.Translate(Vector3.right * speed * Time.deltaTime);\n"
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
