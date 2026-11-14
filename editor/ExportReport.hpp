// ExportReport: the plain-English lines the editor shows after a Web / Android /
// Server export, so the export says what it did NOT do. No ImGui, no engine
// includes: it can be unit-tested (tests/test_export_report.cpp).
//
// It collects three things:
//   1. assets the scene points to that were not found (names, and what pointed at them),
//   2. Build Settings that this target ignores (Layout, Strip, Compress, build number),
//   3. tool checks: is `emcc` on PATH (Web), are ANDROID_NDK_ROOT / ANDROID_HOME set (Android).
// Each becomes a Line with a severity (Error, Warning, Note). The environment is read
// through an injectable Env, so tests do not depend on the machine.
#pragma once
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace okay::editor::exportreport {

enum class Severity { Note = 0, Warning = 1, Error = 2 };

struct Line {
    Severity severity = Severity::Note;
    std::string text;
};

enum class Target { Web = 1, Android = 2, Server = 3 };   // same numbers as the editor's platform picker (0 is Desktop)

struct MissingAsset {
    std::string name;            // the path the scene stores
    std::string referencedBy;    // what pointed at it ("the scene", an object name, ...); may be empty
};

// What the Build Settings hold. Layout 0 = Folder, strip/compress default on, build number default 1.
struct Settings {
    int  layout = 0;
    bool stripAssets = true;
    bool compress = true;
    int  buildNumber = 1;
};

// Reads the machine. Tests replace both functions.
struct Env {
    std::function<std::string(const std::string&)> getVar;     // "" when unset
    std::function<bool(const std::string&)> fileExists;        // regular file at this path
};

inline Env SystemEnv() {
    Env e;
    e.getVar = [](const std::string& n) { const char* v = std::getenv(n.c_str()); return std::string(v ? v : ""); };
    e.fileExists = [](const std::string& p) { std::error_code ec; return std::filesystem::is_regular_file(p, ec); };
    return e;
}

// True when `tool` (or tool.exe / tool.bat / tool.cmd, for Windows) is in a PATH folder.
inline bool OnPath(const std::string& tool, const Env& env) {
    if (!env.getVar || !env.fileExists) return false;
    const std::string path = env.getVar("PATH");
    if (path.empty()) return false;
    static const char* const exts[] = {"", ".exe", ".bat", ".cmd"};
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = path.find_first_of(";:", i);
        // A Windows drive letter ("C:\x") is not a separator: keep "C:" together with what follows.
        if (j != std::string::npos && path[j] == ':' && j == i + 1 && j + 1 < path.size() && (path[j + 1] == '\\' || path[j + 1] == '/')) {
            j = path.find_first_of(";:", j + 1);
        }
        if (j == std::string::npos) j = path.size();
        std::string dir = path.substr(i, j - i);
        if (!dir.empty()) {
            for (const char* x : exts) {
                std::string full = dir + ((dir.back() == '/' || dir.back() == '\\') ? "" : "/") + tool + x;
                if (env.fileExists(full)) return true;
            }
        }
        i = j + 1;
    }
    return false;
}

inline const char* TargetName(Target t) {
    switch (t) { case Target::Web: return "Web"; case Target::Android: return "Android"; default: return "Dedicated Server"; }
}

inline const char* SeverityName(Severity s) {
    switch (s) { case Severity::Error: return "Error"; case Severity::Warning: return "Warning"; default: return "Note"; }
}

constexpr size_t kMaxMissingListed = 20;

inline std::vector<Line> MakeReport(Target target, const std::vector<MissingAsset>& missing,
                                    const Settings& s, const Env& env) {
    std::vector<Line> out;
    auto add = [&](Severity sv, std::string t) { out.push_back({sv, std::move(t)}); };

    // ---- 1. Missing assets: listed, at most 20, then "and N more". ----
    if (!missing.empty()) {
        add(Severity::Warning, std::to_string(missing.size()) + " asset" + (missing.size() == 1 ? " is" : "s are") +
            " missing and were NOT copied into the export:");
        for (size_t i = 0; i < missing.size() && i < kMaxMissingListed; ++i) {
            std::string t = "  " + missing[i].name;
            if (!missing[i].referencedBy.empty()) t += " (used by " + missing[i].referencedBy + ")";
            add(Severity::Warning, t);
        }
        if (missing.size() > kMaxMissingListed)
            add(Severity::Warning, "  and " + std::to_string(missing.size() - kMaxMissingListed) + " more");
    }

    // ---- 2. Settings this target ignores. ----
    std::vector<std::string> ignored;
    if (s.layout != 0) ignored.push_back(s.layout == 1 ? "Layout (Compact)" : "Layout (Zip)");
    if (!s.stripAssets) ignored.push_back("Strip unused assets (off)");
    if (!s.compress) ignored.push_back("Compress (off)");
    if (s.buildNumber != 1) ignored.push_back("Build number (" + std::to_string(s.buildNumber) + ")");
    if (!ignored.empty()) {
        std::string t = std::string(TargetName(target)) + " export ignores these Build Settings: ";
        for (size_t i = 0; i < ignored.size(); ++i) t += (i ? ", " : "") + ignored[i];
        t += ". Only Desktop builds use them.";
        add(Severity::Warning, t);
    }

    // ---- 3. Tool checks and what the export is. ----
    if (target == Target::Web) {
        if (OnPath("emcc", env)) {
            add(Severity::Note, "Emscripten (emcc) was found on PATH. The editor does not run it: run build-web.sh yourself.");
        } else {
            std::string t = "Emscripten (emcc) was not found on PATH. Install emsdk and run 'source emsdk_env.sh' before build-web.sh.";
            if (!env.getVar || env.getVar("EMSDK").empty()) t += " The EMSDK variable is not set either.";
            add(Severity::Warning, t);
        }
        add(Severity::Note, "The web player uses the software renderer, and networking is off in the web build.");
        add(Severity::Note, "This export wrote the game data, build-web.sh and a README. No .wasm was built.");
    } else if (target == Target::Android) {
        const bool ndk = env.getVar && !env.getVar("ANDROID_NDK_ROOT").empty();
        const bool sdk = env.getVar && !env.getVar("ANDROID_HOME").empty();
        if (!ndk) add(Severity::Warning, "ANDROID_NDK_ROOT is not set: the Android NDK was not found.");
        if (!sdk) add(Severity::Warning, "ANDROID_HOME is not set: the Android SDK was not found.");
        if (ndk && sdk) add(Severity::Note, "ANDROID_NDK_ROOT and ANDROID_HOME are set. The editor does not use them yet.");
        add(Severity::Note, "This export wrote assets and a README only. There is no APK and no Gradle project yet.");
    }
    return out;
}

inline bool HasErrors(const std::vector<Line>& l) {
    for (const Line& x : l) if (x.severity == Severity::Error) return true;
    return false;
}

// One block of text: "[Warning] ..." per line (for the console / README).
inline std::string ToText(const std::vector<Line>& l) {
    std::string r;
    for (const Line& x : l) {
        if (!r.empty()) r += "\n";
        r += std::string("[") + SeverityName(x.severity) + "] " + x.text;
    }
    return r;
}

}  // namespace okay::editor::exportreport
