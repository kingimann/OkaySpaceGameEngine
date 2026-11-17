#pragma once
// Where the Modeling window's "Export OBJ" writes: Assets/<object name>.obj, made safe as a file name, and never on top
// of a file that is already there (two objects called "Box" used to overwrite each other's Box.obj without a word).
// The next free name is <name>_2.obj, <name>_3.obj, ... Our own code.
#include <filesystem>
#include <string>

namespace okay::modeling {

/// A file name made from an object name: characters that are not allowed in a Windows file name become '_',
/// trailing dots and spaces go, and an empty result is "mesh".
inline std::string ExportFileStem(const std::string& objectName) {
    std::string base;
    for (char ch : objectName) {
        const unsigned char u = (unsigned char)ch;
        if (u < 32 || ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
            ch == '>' || ch == '|')
            base += '_';
        else
            base += ch;
    }
    while (!base.empty() && (base.back() == '.' || base.back() == ' ')) base.pop_back();
    if (base.empty()) base = "mesh";
    return base;
}

/// `dir`/<stem><ext>, or the first `dir`/<stem>_N<ext> (N = 2, 3, ...) that does not exist yet. With an .obj the
/// matching .mtl must be free too, so an export never replaces another export's materials.
inline std::string NextFreeExportPath(const std::filesystem::path& dir, const std::string& objectName,
                                      const std::string& ext = ".obj") {
    namespace fs = std::filesystem;
    const std::string stem = ExportFileStem(objectName);
    auto taken = [&](const std::string& s) {
        std::error_code ec;
        if (fs::exists(dir / (s + ext), ec)) return true;
        if (ext == ".obj" && fs::exists(dir / (s + ".mtl"), ec)) return true;
        return false;
    };
    std::string s = stem;
    for (int n = 2; taken(s) && n < 100000; ++n) s = stem + "_" + std::to_string(n);
    return (dir / (s + ext)).string();
}

} // namespace okay::modeling
