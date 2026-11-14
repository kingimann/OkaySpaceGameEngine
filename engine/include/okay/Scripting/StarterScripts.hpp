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
inline const char* StarterScriptSource(const std::string& /*lang*/) {
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
