#pragma once
// ---------------------------------------------------------------------------
// EXPERIMENTAL Xbox One Developer Mode export: lays down the package folder.
//
//   <out>/package/AppxManifest.xml     the manifest (XboxManifest.hpp)
//   <out>/package/Assets/*.png         tile and logo images from the project icon (XboxAssets.hpp)
//   <out>/package/Data/                the game's data (the editor's existing export writes it here)
//   <out>/README_XBOX.txt              what to run next and what is unverified
//   <out>/PLACEHOLDER_ASSETS.txt       only when the images are generated stand-ins
//   <out>/xbox-export.json             the settings used (small, plain)
//
// It does NOT build the UWP app, pack or sign: that is scripts/build-xbox-uwp.ps1 on Windows.
// The UWP app shell (platform/xbox_uwp/app) does not load the game's data yet: it draws a test
// triangle and reads a gamepad. Say so wherever this is shown.
//
// UNVERIFIED on Windows and on an Xbox. Written from Microsoft's public documentation.
// ---------------------------------------------------------------------------
#include "XboxAssets.hpp"
#include "XboxManifest.hpp"
#include <fstream>
#include <string>
#include <vector>

namespace okay::xbox {

// Shown in the editor next to the Xbox export and written into README_XBOX.txt. Keep it honest.
inline constexpr const char* kLimitationsText =
    "This makes a Universal Windows Platform (UWP) package for an Xbox One in Developer Mode, using only Microsoft's public "
    "Windows SDK and tools. It is NOT a native console game: it does not use the Xbox GDK, it cannot be published, and it is "
    "limited by the UWP sandbox (no desktop-only Windows APIs, memory and CPU budgets for a Developer Mode app, Direct3D 11 "
    "only). The Xbox app shell in this version does not run your game yet: it opens a window, draws a test triangle and reads a "
    "gamepad, to prove the packaging steps. Nothing here has been built on Windows or run on a console, so compatibility with "
    "any console is unknown. The package is made unsigned by our own packer and signed on your PC with a test certificate.";

struct ExportInputs {
    std::string gameName = "My Game";
    std::string company;
    std::string version = "1.0.0";
    std::string bundleId = "com.okayspace.game";
    std::string iconPath;
};

struct ExportResult {
    bool ok = false;
    std::string message;                 // one plain sentence for the editor
    std::vector<std::string> problems;   // why it failed
    std::vector<std::string> warnings;
    bool placeholderAssets = false;
};

inline ManifestOptions ManifestFrom(const ExportInputs& in) {
    ManifestOptions m;
    m.identityName = IdentityNameFrom(in.bundleId);
    m.publisher = PublisherFrom(in.company);
    m.version = in.version.empty() ? "1.0.0" : in.version;
    std::string dn = in.gameName.empty() ? "OkaySpace Game" : in.gameName;
    if (dn.size() > 256) dn.resize(256);
    m.displayName = dn;
    m.publisherDisplayName = in.company.empty() ? "OkaySpace Developer" : in.company;
    if (m.publisherDisplayName.size() > 256) m.publisherDisplayName.resize(256);
    m.description = dn;
    return m;
}

inline bool WriteTextFile(const std::filesystem::path& p, const std::string& body) {
    std::ofstream f(p.string(), std::ios::binary | std::ios::trunc);
    f << body;
    return static_cast<bool>(f);
}

inline std::string ReadmeText(const ExportInputs& in, const ManifestOptions& m, bool placeholder) {
    std::string t;
    t += "OkaySpace - Xbox One (EXPERIMENTAL, Developer Mode) export of '" + in.gameName + "'\n";
    t += "====================================================================\n\n";
    t += "STATUS: UNVERIFIED. This was written without a Windows PC, Visual Studio, the Windows SDK or an Xbox.\n"
         "No package from this folder has been built, installed or run. Nothing here claims it works on hardware.\n\n";
    t += "What this is NOT: it is not a native Xbox console game, it does not use the Xbox GDK, and it cannot be\n"
         "published. It is a Universal Windows Platform (UWP) app for a console in Developer Mode.\n\n";
    t += "What is in this folder\n"
         "  package/AppxManifest.xml   package identity: " + m.identityName + "  version " + NormalizeVersion(m.version) + "  publisher " + m.publisher + "\n"
         "  package/Assets/            tile and logo images" + std::string(placeholder ? " (GENERATED PLACEHOLDERS, see PLACEHOLDER_ASSETS.txt)" : " (made from the project icon)") + "\n"
         "  package/Data/              your game's scene and assets\n\n";
    t += "IMPORTANT: the Xbox app shell in this version does not load Data/ yet. It opens a window, draws a test\n"
         "triangle with Direct3D 11 and reads a gamepad. It proves the packaging pipeline, not your game.\n\n";
    t += "Next steps, on a Windows PC with Visual Studio 2022 (see docs/xbox_uwp.md in the OkaySpace source):\n"
         "  1. powershell -File scripts\\xbox-check-sdk.ps1            (checks the tools)\n"
         "  2. powershell -File scripts\\build-xbox-uwp.ps1 -SourceRoot <OkaySpace source> -ExportDir <this folder>\n"
         "     (builds the UWP app, copies it into package/, packs with MakeAppx, makes a test certificate, signs)\n"
         "  3. Put the console in Developer Mode and install the .appx from the Windows Device Portal\n"
         "     (steps in docs/xbox_uwp.md, written from Microsoft's documentation and unverified).\n";
    return t;
}

// Writes the manifest, assets and notes. The game data is already in <outDir>/package/Data (the editor's export put it there).
inline ExportResult WritePackage(const std::string& outDir, const ExportInputs& in) {
    ExportResult r;
    namespace fs = std::filesystem;
    const ManifestOptions m = ManifestFrom(in);
    r.problems = ValidateManifest(m, &r.warnings);
    if (!r.problems.empty()) { r.message = "Xbox export stopped: " + r.problems.front(); return r; }
    const fs::path dir = fs::u8path(outDir), pkg = dir / "package";
    std::error_code ec;
    fs::create_directories(pkg, ec);
    if (ec) { r.problems.push_back("Could not create " + pkg.u8string() + ": " + ec.message()); r.message = r.problems.front(); return r; }
    if (!WriteTextFile(pkg / "AppxManifest.xml", GenerateManifest(m))) { r.problems.push_back("Could not write AppxManifest.xml"); r.message = r.problems.front(); return r; }
    const AssetResult a = WriteAssets((pkg / "Assets").u8string(), in.iconPath, m.backgroundColor);
    if (!a.ok) { r.problems.push_back(a.error); r.message = a.error; return r; }
    r.placeholderAssets = a.placeholder;
    if (a.placeholder) {
        WriteTextFile(dir / "PLACEHOLDER_ASSETS.txt",
            "The images in package/Assets were GENERATED by OkaySpace because the project has no usable icon.\n"
            "They are placeholders (a ring and a diamond), not final artwork. Set an icon in Build Settings > Player\n"
            "and export again before showing this package to anyone.\n");
        r.warnings.push_back("The tile and logo images are generated placeholders (no project icon): see PLACEHOLDER_ASSETS.txt.");
    }
    WriteTextFile(dir / "README_XBOX.txt", ReadmeText(in, m, a.placeholder));
    WriteTextFile(dir / "xbox-export.json",
        "{\n  \"experimental\": true,\n  \"verified_on_windows\": false,\n  \"verified_on_xbox\": false,\n"
        "  \"identity\": \"" + XmlEscape(m.identityName) + "\",\n  \"version\": \"" + NormalizeVersion(m.version) + "\",\n"
        "  \"publisher\": \"" + XmlEscape(m.publisher) + "\",\n  \"executable\": \"" + m.executable + "\",\n"
        "  \"placeholder_assets\": " + (a.placeholder ? "true" : "false") + ",\n"
        "  \"app_shell_loads_game_data\": false\n}\n");
    r.ok = true;
    r.message = "Exported an UNVERIFIED Xbox UWP package folder to " + pkg.u8string() +
                ". It still has to be built and packed on Windows (see README_XBOX.txt); the app shell does not run your game yet.";
    return r;
}

} // namespace okay::xbox
