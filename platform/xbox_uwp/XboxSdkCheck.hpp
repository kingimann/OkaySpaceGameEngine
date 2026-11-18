#pragma once
// ---------------------------------------------------------------------------
// EXPERIMENTAL Xbox One Developer Mode export: "is this PC able to build a UWP package?"
//
// Pure decision logic. It reads the disk only through the two callbacks in FsView, so tests
// pass a fake directory tree and the same code runs on Linux. The real disk and the vswhere
// call live in XboxSdkProbe.hpp.
//
// Written from Microsoft's public documentation: the Visual Studio Locator (vswhere) page,
// the "Windows Kits\10" folder layout (Include, Lib, bin, UnionMetadata per SDK version), the
// MSVC toolset folder layout (VC\Tools\MSVC\<version>\lib\x64\store holds the Universal
// Windows Platform libraries), and the MakeAppx and SignTool tool pages.
//
// UNVERIFIED: the folder names below are from documentation and memory. Nothing here has run
// on a Windows PC with Visual Studio installed. If a name is wrong the export reports a tool
// as missing when it is there; the PowerShell twin (scripts/xbox-check-sdk.ps1) uses the same
// names, so run it on Windows and compare (docs/xbox_uwp.md).
// ---------------------------------------------------------------------------
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace okay::xbox {

// The disk, as the check sees it. Paths use '/' (the real probe converts). `list` returns the
// entry names of a folder, or an empty vector if the folder does not exist.
struct FsView {
    std::function<std::vector<std::string>(const std::string& dir)> list;
    bool Exists(const std::string& path) const {   // a path exists if its parent lists its last part
        const std::size_t cut = path.find_last_of('/');
        if (cut == std::string::npos || !list) return false;
        const std::string parent = path.substr(0, cut), leaf = path.substr(cut + 1);
        for (const std::string& n : list(parent))
            if (EqualNoCase(n, leaf)) return true;
        return false;
    }
    static bool EqualNoCase(const std::string& a, const std::string& b) {   // Windows file names ignore case
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
            if (x != y) return false;
        }
        return true;
    }
};

struct SdkInputs {
    FsView fs;
    std::string programFilesX86 = "C:/Program Files (x86)";
    bool vswhereFound = false;                     // vswhere.exe exists at its fixed install path
    std::vector<std::string> vswhereInstallPaths;  // its output lines: the "installationPath" of each VS 2022 (version [17.0,18.0))
};

enum class Need { VisualStudio, UwpToolset, WindowsSdk, UwpHeadersLibs, MakeAppx, SignTool };

struct SdkItem {
    Need need;
    std::string name;     // short label shown in the editor
    bool found = false;
    bool required = true; // MakeAppx is not: our own packer (XboxAppx.hpp) replaces it
    std::string detail;   // what was found (a path or version); empty if missing
    std::string install;  // the official install step, shown when missing
};

struct SdkReport {
    std::vector<SdkItem> items;
    bool available = false;        // every item found
    std::string visualStudioPath;  // chosen install (the newest listed)
    std::string toolsetVersion;    // e.g. 14.38.33130 (newest MSVC folder with the UWP "store" libraries)
    std::string sdkVersion;        // e.g. 10.0.19041.0
    std::string sdkRoot;           // e.g. C:/Program Files (x86)/Windows Kits/10
    std::string makeAppxPath, signToolPath;
    std::string message;           // one plain paragraph for the editor
};

// "10.0.19041.0" -> {10,0,19041,0}. A name that is not four or more numbers gives an empty vector.
inline std::vector<long> ParseDottedVersion(const std::string& s) {
    std::vector<long> out;
    std::size_t i = 0;
    if (s.empty()) return out;
    while (i <= s.size()) {
        std::size_t j = i;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
        if (j == i || j - i > 9) return {};
        out.push_back(std::strtol(s.substr(i, j - i).c_str(), nullptr, 10));
        if (j == s.size()) break;
        if (s[j] != '.') return {};
        i = j + 1;
    }
    return out;
}
inline bool VersionLess(const std::vector<long>& a, const std::vector<long>& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

// The oldest SDK this export asks for. 10.0.17763 is Windows 10 version 1809, the first with a UWP
// minimum this feature uses. A choice, not a proven requirement (see docs/xbox_uwp.md).
inline std::vector<long> MinSdkVersion() { return {10, 0, 17763, 0}; }

inline SdkReport EvaluateSdk(const SdkInputs& in) {
    SdkReport r;
    r.items.reserve(8);   // add() hands out pointers into this vector: it must never reallocate
    auto add = [&](Need n, const char* name, const char* install) {
        SdkItem it; it.need = n; it.name = name; it.install = install; r.items.push_back(it); return &r.items.back();
    };
    // 1. Visual Studio 2022
    SdkItem* vs = add(Need::VisualStudio, "Visual Studio 2022",
        "Install Visual Studio 2022 (the free Community edition is enough) from the Visual Studio downloads page on visualstudio.microsoft.com.");
    // 2. UWP C++ toolset
    SdkItem* ts = add(Need::UwpToolset, "C++ Universal Windows Platform tools",
        "Run the Visual Studio Installer, choose Modify, and tick the workload \"Universal Windows Platform development\" "
        "and, under its optional parts, \"C++ (v143) Universal Windows Platform tools\".");
    // 3-4. Windows SDK
    SdkItem* sdk = add(Need::WindowsSdk, "Windows 10/11 SDK (17763 or newer)",
        "In the Visual Studio Installer tick a \"Windows 10 SDK\" or \"Windows 11 SDK\" (10.0.17763.0 or newer) under the Universal Windows Platform workload, "
        "or install the standalone Windows SDK from the Windows SDK page on developer.microsoft.com.");
    SdkItem* hl = add(Need::UwpHeadersLibs, "Windows SDK UWP headers and libraries",
        "Same step as the Windows SDK: its UWP part (C++/WinRT headers, WindowsApp.lib, d3d11.lib) installs with the SDK and the Universal Windows Platform workload.");
    SdkItem* mk = add(Need::MakeAppx, "MakeAppx.exe",
        "OPTIONAL: our own packer (okay-appx) makes the package, so MakeAppx is not needed. It is only used to cross-check our package. It is part of the Windows SDK (\"Windows SDK Signing Tools for Desktop Apps\").");
    mk->required = false;
    SdkItem* st = add(Need::SignTool, "SignTool.exe",
        "SignTool.exe is part of the Windows SDK (\"Windows SDK Signing Tools for Desktop Apps\"). Install the Windows SDK as above.");

    // --- Visual Studio: the vswhere lines, newest-listed first; keep one that really has VC\Tools\MSVC ---
    if (in.vswhereFound) {
        for (const std::string& raw : in.vswhereInstallPaths) {
            std::string p = raw;
            while (!p.empty() && (p.back() == '\r' || p.back() == '\n' || p.back() == ' ')) p.pop_back();
            for (char& c : p) if (c == '\\') c = '/';
            if (p.empty()) continue;
            if (!in.fs.list || in.fs.list(p + "/VC/Tools/MSVC").empty()) continue;
            r.visualStudioPath = p;
            vs->found = true; vs->detail = p;
            break;
        }
        if (!vs->found) vs->detail = "";
    }
    if (!in.vswhereFound) vs->install = "vswhere.exe was not found (it installs with Visual Studio 2017 or newer, into Program Files (x86)\\Microsoft Visual Studio\\Installer). " + vs->install;

    // --- UWP toolset: newest MSVC folder that has lib/x64/store ---
    if (vs->found) {
        std::vector<std::string> names = in.fs.list(r.visualStudioPath + "/VC/Tools/MSVC");
        std::vector<long> best;
        for (const std::string& n : names) {
            std::vector<long> v = ParseDottedVersion(n);
            if (v.empty()) continue;
            if (!in.fs.Exists(r.visualStudioPath + "/VC/Tools/MSVC/" + n + "/lib/x64/store")) continue;
            if (best.empty() || VersionLess(best, v)) { best = v; r.toolsetVersion = n; }
        }
        if (!r.toolsetVersion.empty()) { ts->found = true; ts->detail = "MSVC " + r.toolsetVersion; }
    }

    // --- Windows SDK: newest version >= minimum with Include/<v>/winrt and Lib/<v>/um/x64 ---
    const std::string kits = in.programFilesX86 + "/Windows Kits/10";
    {
        std::vector<long> bestAny, bestHl;
        std::string verAny, verHl;
        for (const std::string& n : in.fs.list ? in.fs.list(kits + "/Include") : std::vector<std::string>()) {
            std::vector<long> v = ParseDottedVersion(n);
            if (v.size() < 4 || VersionLess(v, MinSdkVersion())) continue;
            const std::string inc = kits + "/Include/" + n;
            if (bestAny.empty() || VersionLess(bestAny, v)) { bestAny = v; verAny = n; }
            const bool headers = in.fs.Exists(inc + "/winrt") && in.fs.Exists(inc + "/cppwinrt") && in.fs.Exists(inc + "/um") && in.fs.Exists(inc + "/ucrt");
            const bool libs = in.fs.Exists(kits + "/Lib/" + n + "/um/x64/WindowsApp.lib") && in.fs.Exists(kits + "/Lib/" + n + "/um/x64/d3d11.lib");
            if (headers && libs && (bestHl.empty() || VersionLess(bestHl, v))) { bestHl = v; verHl = n; }
        }
        if (!verAny.empty()) { sdk->found = true; sdk->detail = verAny; }
        if (!verHl.empty()) { hl->found = true; hl->detail = verHl; r.sdkVersion = verHl; r.sdkRoot = kits; }
        else if (!verAny.empty()) { hl->detail = "SDK " + verAny + " is installed but lacks the UWP headers or libraries (winrt, cppwinrt, WindowsApp.lib, d3d11.lib)"; }
    }

    // --- Tools: in the chosen SDK's bin folder (older layout: bin/x64) ---
    auto findTool = [&](const char* exe, std::string* path) {
        std::vector<std::string> dirs;
        if (!r.sdkVersion.empty()) dirs.push_back(kits + "/bin/" + r.sdkVersion + "/x64");
        dirs.push_back(kits + "/bin/x64");
        for (const std::string& d : dirs)
            if (in.fs.Exists(d + "/" + exe)) { *path = d + "/" + exe; return true; }
        return false;
    };
    if (findTool("makeappx.exe", &r.makeAppxPath)) { mk->found = true; mk->detail = r.makeAppxPath; }
    if (findTool("signtool.exe", &r.signToolPath)) { st->found = true; st->detail = r.signToolPath; }

    r.available = true;
    std::string missing;
    for (const SdkItem& it : r.items)
        if (!it.found && it.required) { r.available = false; missing += (missing.empty() ? "" : "; ") + it.name; }
    if (r.available)
        r.message = "All required tools were found. This only means the files exist: no Xbox package has been built or run from this editor yet (experimental, unverified).";
    else
        r.message = "Xbox export is unavailable on this PC. Missing: " + missing + ". Install them (steps below) and press Check again.";
    return r;
}

} // namespace okay::xbox
