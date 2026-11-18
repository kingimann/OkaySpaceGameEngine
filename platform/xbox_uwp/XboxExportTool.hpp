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
    bool withScene = false;   // --with-scene: also write package\Data\game.okayconfig and a tiny scene (a cube and a camera) for engine mode
};

/// The tiny scene (a camera, a lit-by-default cube and a UI panel with a label) in the engine's text scene format (`okayscene 3`), the same field layout the engine's
/// serializer writes (taken from the example scenes in examples/: transform = position(3) rotation quaternion(4) scale(3); mesh "Cube" r g b a ...).
/// The camera stands at +z and looks down its own -Z at the origin. UNVERIFIED against the engine on Windows; the format is checked on Linux
/// only against the example scenes' lines, not by loading it.
inline std::string TinySceneText() {
    return "okayscene 3\n"
           "name \"Xbox test scene\"\n"
           "gravity 0 -9.81\n"
           "gravity3 0 -9.81 0\n"
           "rendersettings 1 0.27450982 0.47058824 0.78431374 0.5882353 0.7254902 0.88235295 0.47058824 0.47058824 0.50980395 0.15\n"
           "fog 0 0.5882353 0.7254902 0.88235295 20 90\n"
           "tonemap 1\n"
           "gameobject 0 \"Main Camera\"\n"
           "  guid \"5a1c0d2e7b3f4a6c8e9d0b1a2c3d4e5f\"\n"
           "  active 1\n"
           "  parent -1\n"
           "  transform 0 1 6 0 0 0 1 1 1 1\n"
           "  camera 0 5 60 0.47058824 0.65882355 0.8392157 1 1 1 0 0.3 4000 0 0 0 1 1 -1 0 50 24 1 0 0 0 0 32 0.3 2 18 1.5\n"
           "end\n"
           "gameobject 1 \"Cube\"\n"
           "  guid \"7c3e2f4a9d5b6c8e0a1f2d3c4e5f6a71\"\n"
           "  active 1\n"
           "  parent -1\n"
           "  transform 0 0 0 0 0.2588190 0 0.9659258 1 1 1\n"
           "  mesh \"Cube\" 0.85 0.62 0.25 1 0 \"\" 0\n"
           "end\n"
           // a screen-space UI panel with a text label (objects 7 and 8 of examples/ui_text/ui_text.okayscene, moved to the screen corner): the UI layer test
           "gameobject 2 \"UI Panel\"\n"
           "  guid \"11111111111111111111111111111111\"\n"
           "  parent -1\n"
           "  transform 0 0 0 0 0 0 1 1 1 1\n"
           "  uipanel 40 40 360 110 0.0627451 0.07058824 0.09411765 0.74509805 0 12 1 1 1 1 0.08627451 0 0.039215688 0.047058824 0.07058824 0.78431374 0 0 0 0 0.47058824 6 6 1 0 0 0 0 0.47058824 0.7058824 1 0.7058824 0 1 1 1 0.15686275 15\n"
           "end\n"
           "gameobject 3 \"UI Label\"\n"
           "  guid \"22222222222222222222222222222222\"\n"
           "  parent 2\n"
           "  transform 0 0 0 0 0 0 1 1 1 1\n"
           "  text \"Hello Xbox (UI layer)\" 0.9098039 0.92156863 0.9411765 1 2.5 1 12 6 0 0 0 0 0 0.78431374 1 1 0 0 0 0 0 0.9019608 1 124 30 1 0 0 0 0 0.54901963 0 0 0 0 0 0 0.47058824 0.627451 1 1 -1 0 0 \"\" 1 0.6 2.5\n"
           "end\n";
}
inline std::string TinyConfigText(const std::string& title) {
    return "title=" + title + "\nwidth=1280\nheight=720\nstartup=game.okayscene\nscene=game.okayscene\nvsync=1\n";
}

/// Arguments: --out <dir> (required), --name, --company, --publisher-name (alias of --company), --version, --bundle-id, --icon <png>.
/// Defaults: name "OkaySpace Test", company "OkaySpaceTest" (publisher CN=OkaySpaceTest), version 1.0.0.0, bundle id com.okayspace.test.
inline bool ParseExportArgs(const std::vector<std::string>& args, ExportToolOptions& o, std::string& err) {
    o = ExportToolOptions();
    o.in.gameName = "OkaySpace Test"; o.in.company = "OkaySpaceTest"; o.in.version = "1.0.0.0"; o.in.bundleId = "com.okayspace.test";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--help" || a == "-h") { o.help = true; continue; }
        if (a == "--with-scene") { o.withScene = true; continue; }
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
    return "okay-xbox-export --out <folder> [--name <text>] [--company <text>] [--version 1.0.0.0] [--bundle-id com.x.y] [--icon <png>] [--with-scene]\n"
           "  --with-scene: also write package\\Data\\game.okayconfig and a tiny scene (cube + camera) for engine mode.\n"
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
    if (o.withScene) {
        WriteTextFile(data / "game.okayconfig", TinyConfigText(o.in.gameName));
        WriteTextFile(data / "game.okayscene", TinySceneText());
        WriteTextFile(data / "README.txt", "Test export with a tiny scene (a cube and a camera). Engine mode runs it; the default triangle shell ignores Data.\n");
    } else {
        WriteTextFile(data / "README.txt", "Test export: no game data. The app shell shows its triangle test (no game.okayconfig here).\n");
    }
    return true;
}

} // namespace okay::xbox
