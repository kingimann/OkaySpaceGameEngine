#pragma once
// Command-line front end of the Xbox export, pure parsing + one function that writes a minimal TEST export folder
// (package\AppxManifest.xml, package\Assets\*.png placeholders, package\Data\README.txt, README_XBOX.txt, xbox-export.json,
// PLACEHOLDER_ASSETS.txt) with the same code the editor uses (XboxExport.hpp). For people with no editor that has the Xbox tab.
// UNVERIFIED on Windows; the parsing and the folder layout are tested on Linux.
#include "XboxExport.hpp"
#include <string>
#include <vector>

namespace okay::xbox {

struct ExportToolOptions {
    std::string outDir;
    ExportInputs in;
    bool help = false;
};

/// Arguments: --out <dir> (required), --name, --company, --publisher-name (alias of --company), --version, --bundle-id, --icon <png>.
/// Defaults: name "OkaySpace Test", company "OkaySpaceTest" (publisher CN=OkaySpaceTest), version 1.0.0.0, bundle id com.okayspace.test.
inline bool ParseExportArgs(const std::vector<std::string>& args, ExportToolOptions& o, std::string& err) {
    o = ExportToolOptions();
    o.in.gameName = "OkaySpace Test"; o.in.company = "OkaySpaceTest"; o.in.version = "1.0.0.0"; o.in.bundleId = "com.okayspace.test";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--help" || a == "-h") { o.help = true; continue; }
        auto val = [&](std::string& dst) {
            if (i + 1 >= args.size() || (args[i + 1].size() > 1 && args[i + 1][0] == '-' && args[i + 1][1] == '-')) { err = "missing value after " + a; return false; }
            dst = args[++i]; return true;
        };
        if (a == "--out") { if (!val(o.outDir)) return false; }
        else if (a == "--name") { if (!val(o.in.gameName)) return false; }
        else if (a == "--company" || a == "--publisher-name") { if (!val(o.in.company)) return false; }
        else if (a == "--version") { if (!val(o.in.version)) return false; }
        else if (a == "--bundle-id") { if (!val(o.in.bundleId)) return false; }
        else if (a == "--icon") { if (!val(o.in.iconPath)) return false; }
        else { err = "unknown argument " + a; return false; }
    }
    if (!o.help && o.outDir.empty()) { err = "--out <folder> is required"; return false; }
    return true;
}

inline const char* ExportToolUsage() {
    return "okay-xbox-export --out <folder> [--name <text>] [--company <text>] [--version 1.0.0.0] [--bundle-id com.x.y] [--icon <png>]\n"
           "  writes a minimal TEST export (package\\AppxManifest.xml, placeholder tile images, README) for scripts\\build-xbox-uwp.ps1.\n"
           "  defaults: name \"OkaySpace Test\", company OkaySpaceTest (publisher CN=OkaySpaceTest), version 1.0.0.0\n";
}

/// Writes the export. Returns true on success; `message` is a plain sentence either way.
inline bool WriteTestExport(const ExportToolOptions& o, std::string& message) {
    namespace fs = std::filesystem;
    const ExportResult r = WritePackage(o.outDir, o.in);
    message = r.message;
    if (!r.ok) return false;
    std::error_code ec;
    const fs::path data = fs::u8path(o.outDir) / "package" / "Data";
    fs::create_directories(data, ec);
    WriteTextFile(data / "README.txt", "Test export: no game data. The app shell shows its triangle test (no game.okayconfig here).\n");
    return true;
}

} // namespace okay::xbox
