#pragma once
#include <string>

namespace okay {

/// The script a new, empty script file starts with: the two functions every script
/// wants, empty, with a line saying when each runs. The same shape as an empty
/// component script in a C#-style engine. It lives here (not in the editor) so a
/// test can load it into the real script engine.
///
/// "csharp" / "cs": a C# class extending MonoBehaviour (run by our own typed
/// language, NextScriptVM). The editor renames the class after the file.
/// Any other name (OkayScript): no class or base type; the file's fields and
/// functions ARE the component, and the engine calls Start and Update by name.
inline const char* StarterScriptSource(const std::string& lang) {
    if (lang == "csharp" || lang == "cs")
        return "using UnityEngine;\n"
               "\n"
               "public class NewScript : MonoBehaviour\n"
               "{\n"
               "    // Runs once, before the first frame.\n"
               "    void Start()\n"
               "    {\n"
               "    }\n"
               "\n"
               "    // Runs every frame.\n"
               "    void Update()\n"
               "    {\n"
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
