// Store upload files for a built game (okay/Platform/StoreExport.hpp).
//
// Written from Valve's SteamPipe documentation (app build: AppID, Desc, ContentRoot, BuildOutput, SetLive, Preview,
// Depots; depot build: DepotID, FileMapping LocalPath / DepotPath / Recursive, FileExclusion; running
// "steamcmd +login <user> +run_app_build <file> +quit") and Epic's BuildPatchTool documentation (-mode=UploadBinary
// with -OrganizationId, -ProductId, -ArtifactId, -ClientId, -ClientSecretEnvVar, -BuildRoot, -CloudDir,
// -BuildVersion, -AppLaunch, -AppArgs, -FileIgnoreList). Our own code; no sample code was used.
#include "okay/Platform/StoreExport.hpp"
#include "okay/Core/DataPack.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace okay::stores {

namespace fs = std::filesystem;

namespace {

std::string Env(const ExportInput& in, const char* name) {
    if (in.getEnv) return in.getEnv(name);
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// Text inside a VDF "..." value: no quotes, no line breaks.
std::string VdfText(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"') o.push_back('\'');
        else if (c == '\r' || c == '\n' || c == '\t') o.push_back(' ');
        else o.push_back(c);
    }
    return o;
}
// Text inside a batch-file "..." value: no quotes or line breaks, and % doubled so it is not read as a variable.
std::string BatText(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\r' || c == '\n') continue;
        if (c == '%') o += "%%"; else o.push_back(c);
    }
    return o;
}
// Text inside a shell '...' value.
std::string ShText(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '\'') o += "'\\''"; else if (c != '\r' && c != '\n') o.push_back(c); }
    return o;
}
std::string WinPath(std::string p) { for (char& c : p) if (c == '/') c = '\\'; return p; }
std::string UnixPath(std::string p) { for (char& c : p) if (c == '\\') c = '/'; return p; }

bool IdOk(const std::string& s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
bool BranchOk(const std::string& s) {
    if (s.size() > 64) return false;
    for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
    return true;
}

std::uint32_t DepotOf(const ExportInput& in) { return in.steam.depotId ? in.steam.depotId : in.steam.appId + 1; }

std::string Describe(const ExportInput& in) {
    if (!in.steam.description.empty()) return in.steam.description;
    return in.gameName + " " + in.version + " build " + std::to_string(in.buildNumber);
}

bool WriteText(const fs::path& p, const std::string& text, ExportResult& r, const fs::path& base) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { r.errors.push_back("Could not write " + p.filename().string() + "."); r.ok = false; return false; }
    f << text;
    f.close();
    r.written.push_back(fs::relative(p, base, ec).generic_string());
    return true;
}

// The SDK's runtime DLL: copied beside the game exe only when the developer's SDK folder holds it.
void CopySdkDll(const std::vector<fs::path>& candidates, const fs::path& buildDir, const std::string& dllName,
                const std::string& sdkPath, ExportResult& r) {
    std::error_code ec;
    for (const fs::path& c : candidates) {
        if (!fs::is_regular_file(c, ec)) continue;
        fs::remove(buildDir / dllName, ec);
        fs::copy_file(c, buildDir / dllName, fs::copy_options::overwrite_existing, ec);
        if (ec) { r.warnings.push_back("Could not copy " + dllName + " (" + ec.message() + ")."); return; }
        r.written.push_back(dllName);
        return;
    }
    r.warnings.push_back(dllName + " was not found in the SDK folder " + sdkPath + ", so it was not copied. Check the folder on the Stores page.");
}

}  // namespace

std::string EpicBuildVersion(const std::string& version, int buildNumber) {
    std::string v;
    for (char c : version) v.push_back((std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == '+') ? c : '_');
    if (v.empty()) v = "1.0.0";
    return v + "-b" + std::to_string(buildNumber < 0 ? 0 : buildNumber) + "-win64";
}

StoreSettings SettingsFor(const ExportInput& in) {
    StoreSettings s;
    s.store = in.store;
    if (in.store == StoreKind::Steam) {
        s.steamAppId = in.steam.appId;
        s.steamRelaunch = !in.developmentBuild;   // a shipped Steam game is started through the Steam client
    } else if (in.store == StoreKind::Epic) {
        s.epicProductId = in.epic.productId; s.epicSandboxId = in.epic.sandboxId;
        s.epicDeploymentId = in.epic.deploymentId; s.epicClientId = in.epic.clientId;
    }
    return s;
}

std::vector<std::string> Validate(const ExportInput& in, std::vector<std::string>* warnings) {
    std::vector<std::string> err, warn;
    if (in.store == StoreKind::Steam) {
        if (in.steam.appId == 0) err.push_back("Steam: type your game's app id (Steamworks > App Admin). 480 is only Valve's test app.");
        if (in.steam.appId != 0 && in.steam.depotId == 0)
            warn.push_back("Steam: no depot id, so " + std::to_string(in.steam.appId + 1) + " (the app id + 1, Steam's usual first depot) is used.");
        if (!BranchOk(in.steam.branch)) err.push_back("Steam: the branch name may only have letters, digits, '-', '_' and '.'.");
        else if (in.steam.branch == "default")
            warn.push_back("Steam: setting a build live on \"default\" gives it to every player at once. Steam may ask you to do that on the Builds page instead.");
        if (in.steam.sdkPath.empty())
            warn.push_back("Steam: no Steamworks SDK folder is set, so steam_api64.dll is not copied and the game runs with the Steam simulation. Download the SDK from Steamworks and set its folder (docs/release/steam_release.md).");
    } else if (in.store == StoreKind::Epic) {
        const std::pair<const char*, const std::string*> run[] = {{"product id", &in.epic.productId}, {"sandbox id", &in.epic.sandboxId},
                                                                   {"deployment id", &in.epic.deploymentId}, {"client id", &in.epic.clientId}};
        for (const auto& p : run) if (!IdOk(*p.second)) err.push_back(std::string("Epic: the ") + p.first + " is missing or has characters an id cannot have (Developer Portal > Product Settings).");
        const std::pair<const char*, const std::string*> up[] = {{"organization id", &in.epic.organizationId}, {"artifact id", &in.epic.artifactId},
                                                                  {"BuildPatchTool client id", &in.epic.bptClientId}};
        for (const auto& p : up) if (!IdOk(*p.second)) err.push_back(std::string("Epic: the ") + p.first + " for uploads is missing or has characters an id cannot have.");
        if (in.epic.sdkPath.empty())
            warn.push_back("Epic: no EOS SDK folder is set, so EOSSDK-Win64-Shipping.dll is not copied (docs/release/epic_release.md).");
    }
    if (in.store != StoreKind::None) {
        if (in.buildDir.empty() || in.exeName.empty()) err.push_back("The store files need a finished build folder and game exe.");
    }
    if (warnings) warnings->insert(warnings->end(), warn.begin(), warn.end());
    return err;
}

// ---- Steam ----------------------------------------------------------------------------------------------------
std::string SteamAppBuildVdf(const ExportInput& in) {
    const std::uint32_t depot = DepotOf(in);
    std::ostringstream o;
    o << "// SteamPipe app build for " << VdfText(in.gameName) << " (written by OkaySpace Build).\n"
      << "// Upload with upload.bat (or upload.sh) in this folder. The content is the game build two folders up;\n"
      << "// this StoreUpload folder and steam_appid.txt are left out by the depot file.\n"
      << "\"AppBuild\"\n{\n"
      << "\t\"AppID\" \"" << in.steam.appId << "\"\n"
      << "\t\"Desc\" \"" << VdfText(Describe(in)) << "\"\n"
      << "\t\"ContentRoot\" \"..\\..\\\"\n"
      << "\t\"BuildOutput\" \"output\\\"\n"
      << "\t// \"\" uploads only; set it live on the Steamworks Builds page. A branch name sets it live there.\n"
      << "\t\"SetLive\" \"" << VdfText(in.steam.branch) << "\"\n"
      << "\t\"Preview\" \"0\"\n"
      << "\t\"Depots\"\n\t{\n"
      << "\t\t\"" << depot << "\" \"depot_build_" << depot << ".vdf\"\n"
      << "\t}\n}\n";
    return o.str();
}

std::string SteamDepotBuildVdf(const ExportInput& in) {
    std::ostringstream o;
    o << "// SteamPipe depot build for " << VdfText(in.gameName) << ": every file of the game build.\n"
      << "\"DepotBuild\"\n{\n"
      << "\t\"DepotID\" \"" << DepotOf(in) << "\"\n"
      << "\t\"FileMapping\"\n\t{\n"
      << "\t\t\"LocalPath\" \"*\"\n"
      << "\t\t\"DepotPath\" \".\"\n"
      << "\t\t\"Recursive\" \"1\"\n"
      << "\t}\n"
      << "\t\"FileExclusion\" \"" << UploadFolderName() << "\\*\"\n"
      << "\t\"FileExclusion\" \"steam_appid.txt\"\n"
      << "\t\"FileExclusion\" \"*.pdb\"\n"
      << "}\n";
    return o.str();
}

std::string SteamUploadBat(const ExportInput& in) {
    std::ostringstream o;
    const std::string defaultCmd = in.steam.sdkPath.empty() ? std::string("steamcmd.exe")
                                                            : WinPath(in.steam.sdkPath) + "\\tools\\ContentBuilder\\builder\\steamcmd.exe";
    o << "@echo off\r\n"
      << "rem Upload " << BatText(in.gameName) << " " << BatText(in.version) << " to Steam (app " << in.steam.appId << ", depot " << DepotOf(in) << ").\r\n"
      << "rem Written by OkaySpace Build. steamcmd asks for your password and Steam Guard code itself;\r\n"
      << "rem nothing secret is stored in this file.\r\n"
      << "setlocal\r\n"
      << "if \"%STEAM_USER%\"==\"\" (\r\n"
      << "  echo Set STEAM_USER to your Steamworks build account first, for example:  set STEAM_USER=mybuilder\r\n"
      << "  exit /b 1\r\n"
      << ")\r\n"
      << "if \"%STEAMCMD%\"==\"\" set \"STEAMCMD=" << BatText(defaultCmd) << "\"\r\n"
      << "\"%STEAMCMD%\" +login %STEAM_USER% +run_app_build \"%~dp0app_build_" << in.steam.appId << ".vdf\" +quit\r\n"
      << "exit /b %ERRORLEVEL%\r\n";
    return o.str();
}

std::string SteamUploadSh(const ExportInput& in) {
    std::ostringstream o;
    const std::string defaultCmd = in.steam.sdkPath.empty() ? std::string("steamcmd")
                                                            : UnixPath(in.steam.sdkPath) + "/tools/ContentBuilder/builder_linux/steamcmd.sh";
    o << "#!/usr/bin/env bash\n"
      << "# Upload " << in.gameName << " " << in.version << " to Steam (app " << in.steam.appId << ", depot " << DepotOf(in) << ").\n"
      << "# Written by OkaySpace Build. steamcmd asks for your password and Steam Guard code itself.\n"
      << "set -euo pipefail\n"
      << "here=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
      << ": \"${STEAM_USER:?Set STEAM_USER to your Steamworks build account first}\"\n"
      << "STEAMCMD=\"${STEAMCMD:-'" << ShText(defaultCmd) << "'}\"\n"
      << "\"$STEAMCMD\" +login \"$STEAM_USER\" +run_app_build \"$here/app_build_" << in.steam.appId << ".vdf\" +quit\n";
    return o.str();
}

// ---- Epic -----------------------------------------------------------------------------------------------------
std::string EpicIgnoreList(const ExportInput&) {
    // BuildPatchTool reads one path per line, relative to the build root.
    std::ostringstream o;
    o << UploadFolderName() << "/README.txt\n"
      << UploadFolderName() << "/epic/upload.bat\n"
      << UploadFolderName() << "/epic/ignore.txt\n"
      << "steam_appid.txt\n";
    return o.str();
}

std::string EpicUploadBat(const ExportInput& in) {
    std::ostringstream o;
    const std::string bpt = in.epic.bptPath.empty() ? std::string("BuildPatchTool.exe") : WinPath(in.epic.bptPath);
    o << "@echo off\r\n"
      << "rem Upload " << BatText(in.gameName) << " " << BatText(in.version) << " to the Epic Games Store with BuildPatchTool.\r\n"
      << "rem Written by OkaySpace Build. The BuildPatchTool client secret is NOT in this file: set the environment\r\n"
      << "rem variable " << BptClientSecretEnv() << " first (Developer Portal > Product > BuildPatchTool credentials).\r\n"
      << "setlocal\r\n"
      << "if \"%" << BptClientSecretEnv() << "%\"==\"\" (\r\n"
      << "  echo Set " << BptClientSecretEnv() << " to your BuildPatchTool client secret first. It is read from the environment only.\r\n"
      << "  exit /b 1\r\n"
      << ")\r\n"
      << "if \"%BPT%\"==\"\" set \"BPT=" << BatText(bpt) << "\"\r\n"
      << "set \"CLOUD=%LOCALAPPDATA%\\OkaySpace\\BuildPatchTool\\" << BatText(in.epic.artifactId) << "\"\r\n"
      << "\"%BPT%\" -mode=UploadBinary"
      << " -OrganizationId=\"" << BatText(in.epic.organizationId) << "\""
      << " -ProductId=\"" << BatText(in.epic.productId) << "\""
      << " -ArtifactId=\"" << BatText(in.epic.artifactId) << "\""
      << " -ClientId=\"" << BatText(in.epic.bptClientId) << "\""
      << " -ClientSecretEnvVar=\"" << BptClientSecretEnv() << "\""
      << " -BuildRoot=\"%~dp0..\\..\""
      << " -CloudDir=\"%CLOUD%\""
      << " -BuildVersion=\"" << EpicBuildVersion(in.version, in.buildNumber) << "\""
      << " -AppLaunch=\"" << BatText(in.exeName) << "\""
      << " -AppArgs=\"\""
      << " -FileIgnoreList=\"%~dp0ignore.txt\"\r\n"
      << "exit /b %ERRORLEVEL%\r\n";
    return o.str();
}

std::string ReadmeText(const ExportInput& in) {
    std::ostringstream o;
    o << "Store upload files for " << in.gameName << " " << in.version << " (build " << in.buildNumber << ")\r\n"
      << "Written by OkaySpace Build. This folder is left out of every upload.\r\n\r\n";
    if (in.store == StoreKind::Steam) {
        o << "Steam (app " << in.steam.appId << ", depot " << DepotOf(in) << ")\r\n"
          << "  steam\\app_build_" << in.steam.appId << ".vdf    what to upload and where\r\n"
          << "  steam\\depot_build_" << DepotOf(in) << ".vdf  which files (the whole build except this folder)\r\n"
          << "  steam\\upload.bat                set STEAM_USER, then run it; steamcmd asks for the password\r\n"
          << "After the upload, open Steamworks > your app > SteamPipe > Builds and set the build live.\r\n"
          << (in.developmentBuild ? "This is a development build: steam_appid.txt lets it start without the Steam client launching it.\r\n"
                                  : "This is a shipping build: it has no steam_appid.txt, so Steam starts it.\r\n");
    } else if (in.store == StoreKind::Epic) {
        o << "Epic Games Store (artifact " << in.epic.artifactId << ")\r\n"
          << "  epic\\upload.bat   set " << BptClientSecretEnv() << " (never stored in a file), then run it\r\n"
          << "  epic\\ignore.txt   files BuildPatchTool leaves out\r\n"
          << "After the upload, label the binary for a sandbox in the Developer Portal (Artifacts and Binaries).\r\n"
          << "The game itself needs the EOS client secret: the editor reads it from " << EosClientSecretEnv() << " at build time.\r\n";
    }
    return o.str();
}

std::string ReadEpicSecretFile(const std::string& dataDir) {
    std::ifstream f(fs::path(dataDir) / EpicSecretFileName(), std::ios::binary);
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    bool ok = false;
    std::string body = DataPack::Unpack(ss.str(), &ok);
    if (!ok || body.rfind("client_secret=", 0) != 0) return {};
    body = body.substr(14);
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
    return body;
}

ExportResult WriteStoreFiles(const ExportInput& in) {
    ExportResult r;
    std::error_code ec;
    const fs::path build = fs::path(in.buildDir);
    const fs::path upload = build / UploadFolderName();
    // Files an earlier build for another store (or a development build) may have left.
    if (!build.empty()) {
        if (in.writeRuntime) {
            if (in.store != StoreKind::Steam || !in.developmentBuild) fs::remove(build / "steam_appid.txt", ec);
            if (in.store != StoreKind::Epic && !in.dataDir.empty()) fs::remove(fs::path(in.dataDir) / EpicSecretFileName(), ec);
        }
        if (in.writeUpload) fs::remove_all(upload, ec);
    }
    if (in.store == StoreKind::None) return r;

    r.errors = Validate(in, &r.warnings);
    if (!r.errors.empty()) { r.ok = false; return r; }

    if (in.store == StoreKind::Steam) {
        if (in.writeRuntime) {
            if (in.developmentBuild) WriteText(build / "steam_appid.txt", std::to_string(in.steam.appId) + "\n", r, build);
            if (!in.steam.sdkPath.empty()) {
                const fs::path sdk(in.steam.sdkPath);
                CopySdkDll({sdk / "redistributable_bin" / "win64" / "steam_api64.dll", sdk / "steam_api64.dll"}, build, "steam_api64.dll", in.steam.sdkPath, r);
            }
        }
        if (in.writeUpload) {
            const std::uint32_t depot = DepotOf(in);
            WriteText(upload / "steam" / ("app_build_" + std::to_string(in.steam.appId) + ".vdf"), SteamAppBuildVdf(in), r, build);
            WriteText(upload / "steam" / ("depot_build_" + std::to_string(depot) + ".vdf"), SteamDepotBuildVdf(in), r, build);
            WriteText(upload / "steam" / "upload.bat", SteamUploadBat(in), r, build);
            WriteText(upload / "steam" / "upload.sh", SteamUploadSh(in), r, build);
#if !defined(_WIN32)
            fs::permissions(upload / "steam" / "upload.sh", fs::perms::owner_exec | fs::perms::group_exec, fs::perm_options::add, ec);
#endif
        }
    } else if (in.store == StoreKind::Epic) {
        if (in.writeRuntime) {
            if (!in.epic.sdkPath.empty()) {
                const fs::path sdk(in.epic.sdkPath);
                CopySdkDll({sdk / "SDK" / "Bin" / "EOSSDK-Win64-Shipping.dll", sdk / "Bin" / "EOSSDK-Win64-Shipping.dll", sdk / "EOSSDK-Win64-Shipping.dll"},
                           build, "EOSSDK-Win64-Shipping.dll", in.epic.sdkPath, r);
            }
            // The EOS client secret: read from the environment now, written packed into the game only.
            const std::string secret = Env(in, EosClientSecretEnv());
            const fs::path secretFile = fs::path(in.dataDir.empty() ? in.buildDir : in.dataDir) / EpicSecretFileName();
            if (!secret.empty()) {
                std::ofstream f(secretFile, std::ios::binary | std::ios::trunc);
                f << DataPack::Pack("client_secret=" + secret + "\n");
                f.close();
                if (f) r.written.push_back(fs::relative(secretFile, build, ec).generic_string());
                else r.warnings.push_back("Could not write the Epic client secret file into the game.");
            } else {
                fs::remove(secretFile, ec);
                r.warnings.push_back(std::string("Epic: the environment variable ") + EosClientSecretEnv() +
                                     " is not set, so the game has no EOS client secret and cannot log in to Epic. Set it and build again.");
            }
        }
        if (in.writeUpload) {
            WriteText(upload / "epic" / "upload.bat", EpicUploadBat(in), r, build);
            WriteText(upload / "epic" / "ignore.txt", EpicIgnoreList(in), r, build);
        }
    }
    if (in.writeUpload) WriteText(upload / "README.txt", ReadmeText(in), r, build);
    return r;
}

}  // namespace okay::stores
