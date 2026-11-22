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
// It does NOT build the UWP app, pack or sign: the editor's one-click build (XboxBuildPlan.hpp) or scripts/build-xbox-uwp.ps1 does, on Windows.
// The UWP app shell (platform/xbox_uwp/app) draws a test triangle by default. Only when built in "engine mode" (EXPERIMENTAL, never compiled)
// does it run the game in package/Data: 3D view, scripts, physics, pads and (EXPERIMENTAL) a software-painted 2D / UI layer. Say so wherever this is shown.
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
    "only). By default the Xbox app shell does not run your game: it opens a window, draws a test triangle and reads a "
    "gamepad, to prove the packaging steps. \"Engine mode\" (a tick box next to the build button, EXPERIMENTAL) also links the engine and runs your game's "
    "3D scene, scripts, physics and gamepad; the 2D / UI layer is software-painted over the 3D view (EXPERIMENTAL, never run), and the engine has never been compiled with Microsoft's UWP compiler. The triangle test package was built and run on a Windows 11 PC (2026-11-18) but nothing has run on a console, so compatibility with "
    "any console is unknown. The package is made unsigned by our own packer and signed on your PC with a test certificate.";

// Which of OUR OWN code stands in for third-party libraries in the Xbox UWP build, and the crypto statement (docs/platform/xbox_uwp.md). Written into README_XBOX.txt.
inline constexpr const char* kThirdPartyText =
    "Third-party code in this build: none is linked. SDL, Dear ImGui, Assimp, ufbx and libsodium are NOT part of the Xbox build; the engine's own code does "
    "their job (our own window/input/audio backend, software 2D painter, model readers, WAV/OGG/MP3/FLAC decoders, image and font code). "
    "CRYPTO: this build has no libsodium. Packet encryption is OFF by default. SHA-256, random bytes and Ed25519 checks use our own okay::crypto code, which is "
    "own crypto, not independently reviewed; for experiments only; do not ship. The build switch OKAY_XBOX_OWN_CRYPTO=ON (off by default) turns packet "
    "encryption on through the same unreviewed code; a release build refuses that switch (scripts/check_release_own_stack.sh). "
    "HTTP for downloads: Windows.Web.Http by default; OKAY_XBOX_HTTP=own uses our own HTTP client over Winsock for plain http:// only (no TLS of our own yet). Both UNVERIFIED.";

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
    t += "STATUS: PARTLY VERIFIED, NOT ON A CONSOLE. On 2026-11-18 the test package of this pipeline was built, packed, signed, unpacked by Microsoft's\n"
         "MakeAppx, installed and started on a Windows 11 PC (Visual Studio 2026, MSVC 14.52, Windows SDK 10.0.26100.0): it showed the triangle test.\n"
         "NOT verified: the gamepad, suspend/resume, an Xbox console, engine mode (running your game) and the editor's one-click button.\n"
         "Everything not listed as verified is UNVERIFIED. Nothing here claims it works on an Xbox.\n\n";
    t += std::string(kThirdPartyText) + "\n\n";
    t += "What this is NOT: it is not a native Xbox console game, it does not use the Xbox GDK, and it cannot be\n"
         "published. It is a Universal Windows Platform (UWP) app for a console in Developer Mode.\n\n";
    t += "What is in this folder\n"
         "  package/AppxManifest.xml   package identity: " + m.identityName + "  version " + NormalizeVersion(m.version) + "  publisher " + m.publisher + "\n"
         "  package/Assets/            tile and logo images" + std::string(placeholder ? " (GENERATED PLACEHOLDERS, see PLACEHOLDER_ASSETS.txt)" : " (made from the project icon)") + "\n"
         "  package/Data/              your game's scene and assets\n\n";
    t += "IMPORTANT: by default the Xbox app shell does not load Data/. It opens a window, draws a test triangle with\n"
         "Direct3D 11 and reads a gamepad (left stick moves it, A centres it, B exits). That proves the packaging pipeline, not your game.\n"
         "Tick 'Engine mode' before building to try the EXPERIMENTAL game run: it links the engine and plays package/Data (3D scene,\n"
         "scripts, physics, gamepad and keyboard). The 2D / UI layer is software-painted and blended over the 3D view (never run on a screen, so NOT drawn yet as far as anyone has seen). Nobody has compiled it with Microsoft's compiler,\n"
         "so it may fail to build; untick it to get the triangle back.\n\n";
    t += "Next steps, on a Windows PC (see docs/platform/xbox_uwp.md in the OkaySpace source):\n"
         "  1. Install Visual Studio once: 2022 with the 'Universal Windows Platform development' workload, or a newer Visual Studio with the\n"
         "     individual component 'C++ Universal Windows Platform support for ... build tools', plus a Windows 10/11 SDK. The editor checks this for you.\n"
         "  2. In the editor press the big button on File > Build Settings > Xbox (exp.). It checks the tools, exports, compiles\n"
         "     the UWP app and the shaders, packs with our own packer, signs with a test certificate (PowerShell + Microsoft's\n"
         "     SignTool) and, if you ticked it, installs on this PC. Accept the one administrator prompt (UAC) when it appears.\n"
         "     The log panel shows each step. (Advanced: scripts\\build-xbox-uwp.ps1 does the same from a command line.)\n"
         "  3. Put the console in Developer Mode and install the .appx from the Windows Device Portal\n"
         "     (steps in docs/platform/xbox_uwp.md, written from Microsoft's documentation and unverified).\n";
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
        "  \"app_shell_loads_game_data\": false,\n  \"engine_mode_available_experimental\": true\n}\n");
    r.ok = true;
    r.message = "Exported an UNVERIFIED Xbox UWP package folder to " + pkg.u8string() +
                ". It still has to be built and packed on Windows (see README_XBOX.txt); the app shell runs your game only in the EXPERIMENTAL engine mode (see README_XBOX.txt).";
    return r;
}

} // namespace okay::xbox
