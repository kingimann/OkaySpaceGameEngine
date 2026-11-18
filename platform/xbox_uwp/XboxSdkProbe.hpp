#pragma once
// The real disk and the vswhere call for XboxSdkCheck.hpp. UNVERIFIED: never run on Windows.
// Off Windows it reports "vswhere not found", so Xbox export is simply unavailable there.
#include "XboxSdkCheck.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace okay::xbox {

inline FsView RealFs() {
    FsView v;
    v.list = [](const std::string& dir) {
        std::vector<std::string> out;
        std::error_code ec;
        std::filesystem::directory_iterator it(std::filesystem::u8path(dir), ec), end;
        for (; !ec && it != end; it.increment(ec)) out.push_back(it->path().filename().u8string());
        return out;
    };
    return v;
}

inline SdkInputs ProbeThisPc() {
    SdkInputs in;
    in.fs = RealFs();
#if defined(_WIN32)
    const char* pf = std::getenv("ProgramFiles(x86)");
    if (!pf || !*pf) pf = std::getenv("ProgramFiles");
    std::string base = pf && *pf ? pf : "C:/Program Files (x86)";
    for (char& c : base) if (c == '\\') c = '/';
    in.programFilesX86 = base;
    const std::string vswhere = base + "/Microsoft Visual Studio/Installer/vswhere.exe";
    std::error_code ec;
    in.vswhereFound = std::filesystem::exists(std::filesystem::u8path(vswhere), ec);
    if (in.vswhereFound) {
        // Fixed program, fixed arguments, nothing from the user: no injection surface. Every install of any version (2017, 2019, 2022 and
        // newer), same -sort for each call so the lists line up. `-requires` limits the last call to installs with the MSVC x64/x86 tools.
        auto run = [&](const std::string& args) {
            std::vector<std::string> lines;
            std::string cmd = "\"\"" + vswhere + "\" " + args + "\"";
            for (char& c : cmd) if (c == '/') c = '\\';
            if (FILE* p = _popen(cmd.c_str(), "r")) {
                char buf[1024];
                while (std::fgets(buf, sizeof buf, p)) { std::string l = buf; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back(); if (!l.empty()) lines.push_back(l); }
                _pclose(p);
            }
            return lines;
        };
        const std::vector<std::string> paths = run("-products * -prerelease -sort -property installationPath");
        const std::vector<std::string> vers = run("-products * -prerelease -sort -property installationVersion");
        const std::vector<std::string> names = run("-products * -prerelease -sort -property displayName");
        const std::vector<std::string> withVc = run(std::string("-products * -prerelease -sort -requires ") + kVcToolsComponentId() + " -property installationPath");
        for (std::size_t i = 0; i < paths.size(); ++i) {
            VsInstall v; v.path = paths[i];
            if (vers.size() == paths.size()) v.version = vers[i];   // only trusted when the lists line up
            if (names.size() == paths.size()) v.displayName = names[i];
            for (const std::string& w : withVc) if (w == v.path) v.vcComponent = true;
            in.installs.push_back(v);
        }
    }
#endif
    return in;
}

// The check runs vswhere and walks folders, so the editor keeps the answer and asks again only when told to
// (the "Check again" button). Single-threaded (the editor's UI thread): no lock.
inline const SdkReport& CachedSdkReport(bool refresh = false) {
    static SdkReport report;
    static bool have = false;
    if (refresh || !have) { report = EvaluateSdk(ProbeThisPc()); have = true; }
    return report;
}

} // namespace okay::xbox
