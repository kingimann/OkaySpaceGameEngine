#pragma once
// Where the editor program lives. File > Examples looks for the shipped example scenes (the "examples" folder) next
// to the program as well as in the current folder, because the editor is often started from a project folder and
// not from its install folder. main() sets this once at startup. Written from our own design; no other code used.
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace okay::editor {
inline std::string& ExamplesExeDir() { static std::string d; return d; }

// Every .okayscene in examples/<sub>, found from the current folder (and its parents) first, then from the
// program's folder (and its parents). Sorted. Empty when the folder is not there.
inline std::vector<std::string> ExampleScenes(const std::string& sub) {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots;
    auto walk = [&](fs::path d) {
        for (int i = 0; i < 5 && !d.empty(); ++i) {
            roots.push_back(d / "examples" / sub);
            if (d == d.parent_path()) break;
            d = d.parent_path();
        }
    };
    std::error_code ec;
    walk(fs::current_path(ec));
    if (!ExamplesExeDir().empty()) walk(fs::path(ExamplesExeDir()));
    std::vector<std::string> out;
    for (const fs::path& root : roots) {
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& f : fs::directory_iterator(root, ec)) {
            if (ec) break;
            if (f.path().extension() == ".okayscene") out.push_back(f.path().lexically_normal().string());
        }
        if (!out.empty()) break;
    }
    std::sort(out.begin(), out.end());
    return out;
}
}
