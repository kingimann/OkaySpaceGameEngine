#pragma once
// Where the editor program lives. File > Examples looks for the shipped example scenes (the "examples" folder) next
// to the program as well as in the current folder, because the editor is often started from a project folder and
// not from its install folder. main() sets this once at startup. Written from our own design; no other code used.
#include <string>

namespace okay::editor {
inline std::string& ExamplesExeDir() { static std::string d; return d; }
}
