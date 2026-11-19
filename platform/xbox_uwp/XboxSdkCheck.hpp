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

/// One Visual Studio install as vswhere reports it (any version: 2017, 2019, 2022 and newer).
struct VsInstall {
    std::string path;          // installationPath
    std::string version;       // installationVersion, e.g. "17.9.34728.123" or "18.0.1234.5" ("" = not reported)
    std::string displayName;   // e.g. "Visual Studio Community 2022" ("" = not reported)
    bool vcComponent = false;  // vswhere -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 listed it (informational; the folders decide)
};

struct SdkInputs {
    FsView fs;
    std::string programFilesX86 = "C:/Program Files (x86)";
    bool vswhereFound = false;                     // vswhere.exe exists at its fixed install path
    std::vector<VsInstall> installs;               // vswhere -products * -sort (newest first as vswhere lists them)
    std::vector<std::string> vswhereInstallPaths;  // older input form: just the paths (used only when `installs` is empty)
    std::string probeNote;                         // set when the probe itself had a problem (e.g. "vswhere did not answer within 5 seconds"); shown in the message
};

// Component ids, from Microsoft's "Visual Studio component directory" as remembered: UNVERIFIED, so they are only shown to the person and
// passed to vswhere as an informational query. The decision is made from the folders (lib/x64/store), which is what the build needs.
inline const char* kVcToolsComponentId() { return "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"; }
inline std::vector<std::string> UwpComponentIds() {
    return {"Microsoft.VisualStudio.ComponentGroup.UWP.VC.BuildTools",   // 2022: C++ (v143) Universal Windows Platform tools
            "Microsoft.VisualStudio.ComponentGroup.UWP.VC",              // 2019 / 2022 workload part
            "Microsoft.VisualStudio.Component.UWP.VC.v143",              // guess for the individual component (UNVERIFIED)
            "Microsoft.VisualStudio.Component.UWP.VC"};                  // guess for newer installs (UNVERIFIED)
}

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
    std::string visualStudioPath;  // chosen install
    std::string visualStudioVersion;   // its installationVersion ("" if unknown)
    std::string visualStudioName;      // its display name ("" if unknown)
    int visualStudioMajor = 0;         // 17 for 2022, 18 for the next one; 0 = unknown
    std::string generator;             // CMake generator for that major ("" = let CMake choose)
    std::string platformToolset;       // "v143" / "v145" derived from toolsetVersion ("" = unknown: the plan then passes only version=)
    std::string toolsetVersion;    // e.g. 14.38.33130 (newest MSVC folder with the UWP "store" libraries)
    std::string sdkVersion;        // e.g. 10.0.19041.0
    std::string sdkRoot;           // e.g. C:/Program Files (x86)/Windows Kits/10
    std::string makeAppxPath, signToolPath;
    std::string message;           // one plain paragraph for the editor
    bool checking = false;         // true while the probe still runs on its worker thread (XboxAsyncProbe.hpp): items are empty, nothing is decided yet
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
/// "17.9.34728.123", "18.0", "17" -> numbers (one to four parts); anything else gives an empty vector. Numeric, never textual.
inline std::vector<long> ParseDottedVersionLoose(const std::string& s) {
    std::vector<long> out;
    if (s.empty()) return out;
    std::size_t i = 0;
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

inline int MajorOf(const std::string& version) { const std::vector<long> v = ParseDottedVersionLoose(version); return v.empty() ? 0 : (int)v[0]; }

/// The CMake generator name for a Visual Studio major version. Table from CMake's generator list as remembered (UNVERIFIED for 18):
/// 15 = 2017, 16 = 2019, 17 = 2022, 18 = 2026. Unknown: "" (the plan then omits -G and CMake picks its default).
inline std::string GeneratorForMajor(int major) {
    switch (major) {
        case 15: return "Visual Studio 15 2017";
        case 16: return "Visual Studio 16 2019";
        case 17: return "Visual Studio 17 2022";
        case 18: return "Visual Studio 18 2026";
        default: return "";
    }
}

/// The MSBuild platform toolset name for an MSVC folder version, because a newer Visual Studio's DEFAULT toolset may differ from the one that has the
/// UWP libraries (Visual Studio 2026 defaults to v145 while a v143 UWP component is MSVC 14.4x). From Microsoft's published mapping as remembered
/// (UNVERIFIED): 14.30-14.49 = v143, 14.50-14.59 = v145. Anything else: "" (omit the name; pass only version=).
inline std::string PlatformToolsetFor(const std::string& msvcFolderVersion) {
    const std::vector<long> v = ParseDottedVersionLoose(msvcFolderVersion);
    if (v.size() < 2 || v[0] != 14) return "";
    if (v[1] >= 30 && v[1] <= 49) return "v143";
    if (v[1] >= 50 && v[1] <= 59) return "v145";
    return "";
}

// The oldest SDK this export asks for. 10.0.17763 is Windows 10 version 1809, the first with a UWP
// minimum this feature uses. A choice, not a proven requirement (see docs/xbox_uwp.md).
inline std::vector<long> MinSdkVersion() { return {10, 0, 17763, 0}; }

/// How to install the UWP C++ tools. 2022 (and older) has the workload "Universal Windows Platform development"; newer installs have no such
/// workload, only the individual component "C++ Universal Windows Platform support for ... build tools" (Individual components tab).
inline std::string UwpInstallText(int major) {
    const std::string w = "Visual Studio Installer > Modify > the workload \"Universal Windows Platform development\" (with \"C++ (v143) Universal Windows Platform tools\" ticked under its optional parts)";
    const std::string c = "Visual Studio Installer > Modify > Individual components > \"C++ Universal Windows Platform support for ... build tools\" (pick the one for your newest MSVC build tools)";
    if (major >= 18) return "Open the " + c + ". The workload " + "\"Universal Windows Platform development\" does not exist there.";
    if (major > 0) return "Open the " + w + ".";
    return "Open the " + w + ", or on a newer Visual Studio the " + c + ".";
}

inline SdkReport EvaluateSdk(const SdkInputs& in) {
    SdkReport r;
    r.items.reserve(8);   // add() hands out pointers into this vector: it must never reallocate
    auto add = [&](Need n, const char* name, const std::string& install) {
        SdkItem it; it.need = n; it.name = name; it.install = install; r.items.push_back(it); return &r.items.back();
    };
    // 1. Visual Studio 2022 or newer
    SdkItem* vs = add(Need::VisualStudio, "Visual Studio 2022 or newer",
        "Install Visual Studio 2022 or newer (the free Community edition is enough) from the Visual Studio downloads page on visualstudio.microsoft.com.");
    // 2. UWP C++ toolset
    SdkItem* ts = add(Need::UwpToolset, "C++ Universal Windows Platform tools",
        UwpInstallText(0));
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

    // --- Visual Studio: every install vswhere listed. Keep the ones that really have VC\Tools\MSVC, then prefer one that also has a UWP
    //     toolset (an MSVC folder with lib/x64/store), the newest by NUMERIC installationVersion, else the first listed. ---
    std::vector<VsInstall> installs = in.installs;
    if (installs.empty()) for (const std::string& raw : in.vswhereInstallPaths) { VsInstall v; v.path = raw; installs.push_back(v); }
    struct Cand { VsInstall vs; std::string toolset; std::vector<long> ver; std::size_t order; bool hasUwp; };
    std::vector<Cand> cands;
    if (in.vswhereFound) {
        std::size_t order = 0;
        for (VsInstall v : installs) {
            std::string& p = v.path;
            while (!p.empty() && (p.back() == '\r' || p.back() == '\n' || p.back() == ' ')) p.pop_back();
            for (char& c : p) if (c == '\\') c = '/';
            ++order;
            if (p.empty() || !in.fs.list) continue;
            const std::vector<std::string> names = in.fs.list(p + "/VC/Tools/MSVC");
            if (names.empty()) continue;
            Cand c; c.vs = v; c.order = order; c.ver = ParseDottedVersionLoose(v.version); c.hasUwp = false;
            std::vector<long> best;
            for (const std::string& n : names) {   // any toolset folder name: numeric ordering, nothing hard-coded (14.38, 14.44, 14.5x ...)
                const std::vector<long> tv = ParseDottedVersion(n);
                if (tv.empty()) continue;
                if (!in.fs.Exists(p + "/VC/Tools/MSVC/" + n + "/lib/x64/store")) continue;   // an ARM64-only UWP install has lib/arm64/store, not this
                if (best.empty() || VersionLess(best, tv)) { best = tv; c.toolset = n; c.hasUwp = true; }
            }
            cands.push_back(c);
        }
    }
    const Cand* pick = nullptr;
    for (const Cand& c : cands) {
        if (!pick) { pick = &c; continue; }
        if (c.hasUwp != pick->hasUwp) { if (c.hasUwp) pick = &c; continue; }
        if (VersionLess(pick->ver, c.ver)) pick = &c;   // newer installationVersion wins; equal or unknown keeps vswhere's order
    }
    if (pick) {
        r.visualStudioPath = pick->vs.path; r.visualStudioVersion = pick->vs.version; r.visualStudioName = pick->vs.displayName;
        r.visualStudioMajor = MajorOf(pick->vs.version); r.generator = GeneratorForMajor(r.visualStudioMajor);
        vs->found = true; vs->detail = pick->vs.displayName.empty() ? pick->vs.path : pick->vs.displayName + " " + pick->vs.version + " (" + pick->vs.path + ")";
        if (pick->hasUwp) { r.toolsetVersion = pick->toolset; r.platformToolset = PlatformToolsetFor(pick->toolset); ts->found = true; ts->detail = "MSVC " + r.toolsetVersion; }
        else ts->detail = "no MSVC toolset with x64 UWP libraries (lib/x64/store) in " + pick->vs.path;
        ts->install = UwpInstallText(r.visualStudioMajor);
    }
    if (!in.vswhereFound) vs->install = "vswhere.exe was not found (it installs with Visual Studio 2017 or newer, into Program Files (x86)\\Microsoft Visual Studio\\Installer). " + vs->install;

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
    if (!in.probeNote.empty()) r.message += " Note: " + in.probeNote + ".";
    return r;
}

} // namespace okay::xbox
