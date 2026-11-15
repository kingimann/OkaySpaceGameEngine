#pragma once
// Which scene the editor opens at startup, from its command line. Header-only and
// free of ImGui and SDL so a test can check it (tests/test_editor_launch_args.cpp).
//
// `--open <scene>` and `--open=<scene>` (the launcher) win. Otherwise the first bare
// argument ending in ".okayscene" is the scene: that is how Windows passes a file
// when it is opened with OkayEngine.exe or dropped on it, and how a person types
// `OkayEngine.exe examples\vehicle\vehicle_test.okayscene`. Before this a bare path was
// ignored and the editor opened an empty Untitled scene.
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace okay::editor {

/// Flags whose next argument is their value, so that value is never taken for a scene.
inline bool LaunchFlagTakesValue(const std::string& a) {
    static const char* const kFlags[] = {"--open", "--launcher", "--template", "--new-project",
                                         "--project-version", "--gpu"};
    for (const char* f : kFlags) if (a == f) return true;
    return false;
}

inline bool EndsWithSceneExtension(const std::string& a) {
    static const char kExt[] = ".okayscene";
    const std::size_t n = sizeof(kExt) - 1;
    if (a.size() <= n) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(a[a.size() - n + i])) != kExt[i]) return false;
    return true;
}

/// `args` are the arguments after the program name. Empty when no scene was asked for.
inline std::string LaunchSceneFromArgs(const std::vector<std::string>& args) {
    std::string bare;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--open") { if (i + 1 < args.size()) return args[i + 1]; continue; }
        if (a.rfind("--open=", 0) == 0) return a.substr(7);
        if (LaunchFlagTakesValue(a)) { ++i; continue; }
        if (!a.empty() && a[0] != '-' && bare.empty() && EndsWithSceneExtension(a)) bare = a;
    }
    return bare;
}

}  // namespace okay::editor
