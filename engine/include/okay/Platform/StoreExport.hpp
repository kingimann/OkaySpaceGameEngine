#pragma once
// Store upload files for a built game (Build Settings > Stores): everything a developer needs next to the game
// build to upload it to Steam (SteamPipe: app_build / depot_build VDF files and a steamcmd script) or to the Epic
// Games Store (a BuildPatchTool command script).
//
// Written from Valve's SteamPipe documentation (the VDF keys) and Epic's BuildPatchTool documentation (the
// command-line parameters). We never ship or download either SDK: steam_api64.dll and the EOS SDK DLL are only
// copied from the folder the developer points at. No secret is ever written into these files: steamcmd asks for
// the password itself, and BuildPatchTool reads its client secret from an environment variable by name.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "okay/Platform/PlatformServices.hpp"

namespace okay::stores {

/// The folder inside the build where the upload files go. It is left out of every upload.
inline const char* UploadFolderName() { return "StoreUpload"; }

struct SteamExport {
    std::uint32_t appId = 0;          // the game's own Steam app id (Steamworks > App Admin)
    std::uint32_t depotId = 0;        // its Windows depot id (usually appId + 1)
    std::string   sdkPath;            // the Steamworks SDK folder the developer downloaded ("" = none)
    std::string   branch;             // set the build live on this branch after upload ("" = upload only)
    std::string   description;        // shown on the Steamworks Builds page ("" = "<Game> <version> build <n>")
};

struct EpicExport {
    std::string productId, sandboxId, deploymentId, clientId;   // the game's EOS ids (runtime)
    std::string organizationId, artifactId;                     // BuildPatchTool upload target
    std::string bptClientId;                                    // BuildPatchTool's own client id
    std::string bptPath;                                        // BuildPatchTool.exe ("" = look on PATH)
    std::string sdkPath;                                        // the EOS SDK folder ("" = none)
};

/// Environment variable names (never their values) used for the two Epic secrets.
inline const char* EosClientSecretEnv() { return "OKAY_EOS_CLIENT_SECRET"; }     // read by the editor at build time
inline const char* BptClientSecretEnv() { return "EPIC_BPT_CLIENT_SECRET"; }     // read by BuildPatchTool at upload time

struct ExportInput {
    StoreKind   store = StoreKind::None;
    std::string buildDir;      // the game build's folder (where the exe is)
    std::string dataDir;       // the game's data folder (where game.okayconfig is)
    std::string exeName;       // "MyGame.exe"
    std::string gameName;
    std::string version = "1.0.0";
    int         buildNumber = 1;
    bool        developmentBuild = false;
    SteamExport steam;
    EpicExport  epic;
    /// How to read an environment variable (tests replace it). Default: std::getenv.
    std::function<std::string(const char*)> getEnv;
};

struct ExportResult {
    bool ok = true;
    std::vector<std::string> written;    // paths relative to buildDir, "/" separated
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// Problems with the settings, as plain sentences ("" list = fine). Errors stop the store files being written.
std::vector<std::string> Validate(const ExportInput& in, std::vector<std::string>* warnings = nullptr);

/// The game.okayconfig store lines for these settings (no secret).
StoreSettings SettingsFor(const ExportInput& in);

// ---- the text of each file (pure, tested) ---------------------------------------------------------------------
std::string SteamAppBuildVdf(const ExportInput& in);
std::string SteamDepotBuildVdf(const ExportInput& in);
std::string SteamUploadBat(const ExportInput& in);
std::string SteamUploadSh(const ExportInput& in);
std::string EpicUploadBat(const ExportInput& in);
std::string EpicIgnoreList(const ExportInput& in);
std::string ReadmeText(const ExportInput& in);
/// A BuildPatchTool build version: letters, digits, '.', '-', '_' and '+' only.
std::string EpicBuildVersion(const std::string& version, int buildNumber);

/// Write the store files into the build: steam_appid.txt (development builds only, removed otherwise), the SDK
/// runtime DLL when the SDK path is set, the upload folder, and the Epic runtime secret (packed) when
/// OKAY_EOS_CLIENT_SECRET is set. Does nothing for StoreKind::None except remove a stale steam_appid.txt.
ExportResult WriteStoreFiles(const ExportInput& in);

/// The name of the packed file in the data folder that holds the Epic client secret.
inline const char* EpicSecretFileName() { return "epic.okaysecret"; }
/// Read that file back (the player does this). "" if missing or damaged.
std::string ReadEpicSecretFile(const std::string& dataDir);

}  // namespace okay::stores
