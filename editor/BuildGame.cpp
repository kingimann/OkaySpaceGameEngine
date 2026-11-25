// Build Game: export the current scene as a standalone runnable game, the Web / Android / Xbox data exports and the
// dedicated server folder (BuildGame.hpp). Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 1):
// only the includes, the namespace lines and two default arguments (now in the header) differ.
#include "EditorCommon.hpp"
#include "BuildGame.hpp"
#include "MainShared.hpp"            // g_project, g_xboxExport* (main.cpp)
#include "Updater.hpp"               // updater::SelfPath / Download / RawUrl / FetchVerified
#include "AnimatorPreview.hpp"       // apreview::Stop: a build ships the edited scene, not the running one
#include "AntiCheatIntegrity.hpp"    // Data/manifest.json (opt-in anti-cheat)
#include "BuildSettingsModel.hpp"    // bsm::ExtraFrom, the pre / post build commands, the exclude list
#include "ConsolePanel.hpp"          // ConsoleLog
#include "LocalizationWindow.hpp"    // CopyLocalizationFiles: strings.okayloc and its fonts go into the build
#include "ModulesModel.hpp"          // okaymod::Selection / UsedButOff / UsageInScene: Build warns about modules in use but off
#include "okay/Core/BuildPipeline.hpp"
#include "WebRun.hpp"                // the web game folder's sizes (Build Settings > Web)
#include "okay/Input/InputRuntime.hpp"   // kProjectAssetRef: the project's input asset ships with the game
#if defined(OKAY_XBOX_UWP)               // EXPERIMENTAL Xbox One Developer Mode (UWP) export: platform/xbox_uwp, docs/platform/xbox_uwp.md
#include "XboxExport.hpp"
#include "okay/Core/FileCopy.hpp"   // CopyFileOver: copy_file with overwrite_existing fails over an existing file on Windows (MinGW)
#endif

namespace oked {

// ---- Build Game: export the current scene as a standalone runnable game ----
// A shipped game is just <Game>.exe (a copy of the player runtime) + a
// game.okayscene sitting beside it. Double-clicking the exe runs the scene.
namespace builder {
namespace fs = std::filesystem;

#if defined(_WIN32)
const char* kPlayerExe = "OkaySpacePlayer.exe";
#else
const char* kPlayerExe = "okay-player";
#endif

// Locate the player runtime. Searches next to the editor, then a few sibling
// locations (a launcher layout, a build tree), so it works whether the editor
// was launched from the dist folder or via the launcher.
std::string FindPlayer() {
    fs::path self = updater::SelfPath();
    if (self.empty()) return {};
    fs::path dir = self.parent_path();
#if defined(_WIN32)
    const char* names[] = {"OkaySpacePlayer.exe", "okay-player.exe"};
#else
    const char* names[] = {"okay-player", "OkaySpacePlayer"};
#endif
    std::vector<fs::path> roots = {dir, dir / "bin", dir.parent_path(),
                                   dir / "runtime"};
    for (const fs::path& root : roots)
        for (const char* n : names) {
            std::error_code ec;
            if (fs::exists(root / n, ec)) return (root / n).string();
        }
    return {};
}

// Ensure the player runtime is available, downloading it next to the editor
// from the releases bucket if it isn't found locally. Returns its path, or "" on failure.
// This is what lets a freshly-downloaded editor build a runnable game with no
// manual setup ("when building, generate everything it needs").
std::string EnsurePlayer(std::string* note) {
    std::string p = FindPlayer();
    if (!p.empty()) return p;
    fs::path self = updater::SelfPath();
    if (self.empty()) return {};
    fs::path dest = self.parent_path() / kPlayerExe;
    if (note) *note = "Fetching the player runtime (checked against the published checksum)...";
    std::string reason;
    if (!updater::FetchVerified(OKAY_ENGINE_VERSION, "OkaySpacePlayer.exe", dest, reason)) {
        if (note) *note = "Couldn't get the player runtime: " + reason;
        return {};
    }
    std::error_code ec;
    {   // FetchVerified staged and checked <dest>.new; only now does it become the player
        fs::path staged = dest; staged += ".new";
        fs::remove(dest, ec);
        fs::rename(staged, dest, ec);
    }
    if (ec || !fs::exists(dest, ec) || fs::file_size(dest, ec) < 100000) {
        fs::remove(dest, ec);
        if (note) *note = "Couldn't install the player runtime next to the editor.";
        return {};
    }
#if !defined(_WIN32)
    fs::permissions(dest, fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    if (note) *note = "Downloaded the player runtime to " + dest.string() + ".";
    return dest.string();
}


// Obfuscate a just-written text file in place (scene/config) so it can't be read
// or edited in a text editor; the player decodes it transparently at load.
inline void PackFileInPlace(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return;
    std::ifstream in(p.string(), std::ios::binary);
    std::stringstream ss; ss << in.rdbuf(); in.close();
    std::string raw = ss.str();
    if (okay::DataPack::IsPacked(raw)) return;        // already packed
    std::ofstream out(p.string(), std::ios::binary | std::ios::trunc);
    out << okay::DataPack::Pack(raw);
}

// Sanitize an asset's relative path so the build can't write outside its Data
// folder: drop any leading drive/root and reject "." / ".." components (path
// traversal). Returns a clean relative path; empty if nothing usable remains.
inline fs::path SafeRelative(const fs::path& rel) {
    fs::path out;
    for (const auto& part : rel) {
        std::string s = part.string();
        if (s.empty() || s == "." || s == ".." || s == "/" || s == "\\") continue;
        if (part.has_root_name() || part.has_root_directory()) continue;   // drive letters, leading slash
        out /= part;
    }
    return out;
}


// Build the game into outDir (docs/release/build_pipeline.md). Returns a human-readable status string; if `report`
// is non-null it is filled with the full build report (files by size and type, what was left out and why,
// warnings, time). With opt.dryRun nothing is written to outDir: the build runs in a scratch folder that is
// deleted again, so the report is exact without leaving files behind.
std::string Build(EditorState& ed, const std::string& outDir,
                  const std::string& gameName, const Options& opt, BuildReport* report) {
    namespace bp = okay::buildpipe;
    const auto t0 = std::chrono::steady_clock::now();
    BuildReport localRep;
    BuildReport& R = report ? *report : localRep;
    R = BuildReport{};
    // A build ships the scene you edited. While Play runs, the scene in memory is the running game (moved,
    // spawned, scripted), so stop Play first: Stop restores the edited scene.
    if (ed.isPlaying()) { okay::editor::apreview::Stop(ed.scene()); ed.Stop(); }
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::path(outDir), ec);
    if (dir.empty()) dir = fs::path(outDir);

    // Safety: refuse to scatter a build across a dangerous folder (a filesystem
    // root, the user's home, or the project folder itself), where Clean could
    // delete the wrong things and the output would mix with source files.
    {
        fs::path home;
        if (const char* h = std::getenv("HOME")) home = h;
        else if (const char* up = std::getenv("USERPROFILE")) home = up;
        fs::path proj = ed.projectDir().empty() ? fs::path() : fs::weakly_canonical(fs::path(ed.projectDir()), ec);
        if (dir == dir.root_path()) return "Refusing to build into a filesystem root. Pick a dedicated build folder.";
        if (!home.empty() && fs::weakly_canonical(home, ec) == dir)
            return "Refusing to build into your home folder. Pick a dedicated build folder.";
        if (!proj.empty() && (dir == proj || dir == proj / "Assets"))
            return "Refusing to build into the project/Assets folder. Pick a separate build folder so game files stay clean.";
    }

    // Build Settings > Advanced: the command to run BEFORE the build. Only when "Allow build commands" is on, never in a
    // dry run. A command that fails (a non-zero exit code) stops the build before anything is written.
    oked::bsm::CommandVars cmdVars;
    cmdVars.output = dir.string(); cmdVars.name = gameName; cmdVars.version = opt.version; cmdVars.build = std::to_string(opt.buildNumber); cmdVars.project = ed.projectDir();
    {
        const std::string pre = oked::bsm::CommandToRun(opt.preCommand, opt.allowCommands, opt.dryRun, cmdVars);
        if (!pre.empty()) {
            const int rc = oked::bsm::RunCommandLine(pre, ed.projectDir());
            if (rc != 0) return "The command before the build failed (exit code " + std::to_string(rc) + "): " + pre + ". Nothing was built.";
        }
    }

    // The layout: <Name>.exe + <Name>_Data/ (Folder), exe + Data/ (Compact), or the Folder layout in one zip.
#if defined(_WIN32)
    const bool windowsHost = true;
#else
    const bool windowsHost = false;
#endif
    const std::string utcNow = bp::UtcNowIso();
    bp::LayoutInput li;
    li.gameName = gameName;
    li.layout = (bp::Layout)std::max(0, std::min(2, opt.layout));
    li.compactDataFolder = opt.dataFolder;
    li.windows = windowsHost;
    li.antiCheat = opt.antiCheat != 0;
    li.archivePattern = opt.archiveName;
    li.tokens = {bp::FileSafeName(gameName, "Game"), opt.version, windowsHost ? "windows" : "linux", utcNow.substr(0, 10), opt.buildNumber};
    const bp::LayoutPlan plan = bp::PlanLayout(li);
    const bool zipOnly = plan.makeZip && !plan.keepFolder;
    const bool wantZip = plan.makeZip || opt.zipAlso;

    // Where the files go: the output folder; a scratch folder for a dry run; a staging folder inside the
    // output folder when only the zip is kept.
    fs::path root = dir, scratch;
    if (opt.dryRun) {
        scratch = fs::temp_directory_path(ec) / ("okay-dryrun-" + bp::HashHex(bp::HashText(dir.string() + utcNow + std::to_string(t0.time_since_epoch().count()))));
        root = scratch;
    } else if (zipOnly) {
        root = dir / (".stage-" + plan.safeName);
    }
    struct Cleanup { fs::path p; ~Cleanup() { if (!p.empty()) { std::error_code e; fs::remove_all(p, e); } } } cleanup{scratch};
    fs::create_directories(opt.dryRun ? root : dir, ec);
    if (ec) return "Couldn't create folder: " + ec.message();
    if (zipOnly && !opt.dryRun) { fs::remove_all(root, ec); fs::create_directories(root, ec); }

    // Clean stale build artifacts so old builds never leave junk behind. We only
    // remove files THIS builder writes (the managed data folders + the runtime/
    // config at the root), never the user's unrelated files.
    if (opt.cleanOutput && !opt.dryRun) {
        std::error_code ce;
        fs::remove_all(dir / "Data", ce);
        fs::remove_all(dir / (plan.safeName + "_Data"), ce);
        for (const char* f : {"game.okayscene", "game.okayconfig", "game.okayprefs"})
            fs::remove(dir / f, ce);
        // Drop any *.okayscene left at the root by an older flat-layout build.
        for (auto& e : fs::directory_iterator(dir, ce))
            if (e.is_regular_file() && e.path().extension() == ".okayscene")
                fs::remove(e.path(), ce);
        if (plan.makeZip) fs::remove(dir / plan.zipName, ce);
    }

    const fs::path data = plan.dataDir.empty() ? root : root / plan.dataDir;
    fs::create_directories(data, ec);
    if (ec) return "Couldn't create folder: " + ec.message();

    std::map<std::string, std::string> why;                 // path in the build (relative to root) -> why it is there
    auto relRoot = [&](const fs::path& p) { std::error_code e; return fs::relative(p, root, e).generic_string(); };
    std::vector<std::pair<std::string, std::uintmax_t>> virtualFiles;   // dry run: files that are counted but not copied

    // 1) Scenes In Build, in order: the first is the startup scene (written as game.okayscene); the open
    // scene is written from memory so unsaved edits ship. With no list, the open scene alone is the game
    // (plus, optionally, every project scene). Every scene is also read for the assets it uses.
    struct SceneEntry { fs::path src; std::string fn; bool isOpen = false; std::unique_ptr<Scene> loaded; };
    std::vector<SceneEntry> built;
    std::vector<std::string> sceneFiles;                    // filenames placed in the data folder
    auto uniqueName = [&](const fs::path& src, bool first) {
        std::string fn = first ? std::string("game.okayscene") : src.filename().string();
        for (int n = 2; std::find(sceneFiles.begin(), sceneFiles.end(), fn) != sceneFiles.end(); ++n)
            fn = src.stem().string() + "_" + std::to_string(n) + ".okayscene";   // two scenes, same name
        return fn;
    };
    std::error_code pe;
    if (!opt.sceneList.empty()) {
        for (std::size_t i = 0; i < opt.sceneList.size(); ++i) {
            SceneEntry se;
            se.src = fs::path(opt.sceneList[i]);
            se.fn = uniqueName(se.src, i == 0);
            se.isOpen = !ed.path().empty() && fs::exists(se.src, pe) && fs::equivalent(se.src, fs::path(ed.path()), pe);
            sceneFiles.push_back(se.fn);
            built.push_back(std::move(se));
        }
    } else {
        SceneEntry se;
        se.src = fs::path(ed.path());
        se.fn = "game.okayscene"; se.isOpen = true;
        sceneFiles.push_back(se.fn);
        built.push_back(std::move(se));
        if (opt.includeAllProjectScenes && !ed.projectDir().empty()) {
            fs::path assets = fs::path(ed.projectDir()) / "Assets";
            if (fs::exists(assets, ec))
                for (auto& e : fs::recursive_directory_iterator(assets, ec)) {
                    if (!e.is_regular_file() || e.path().extension() != ".okayscene") continue;
                    if (e.path().filename() == "game.okayscene") continue;
                    SceneEntry xe;
                    xe.src = e.path(); xe.fn = e.path().filename().string();
                    sceneFiles.push_back(xe.fn);
                    built.push_back(std::move(xe));
                }
        }
    }
    int sceneCount = 0;
    std::vector<bp::AssetRef> roots;
    std::vector<std::string> sceneSources;   // the scene files, and files embedded in them (accounted for, not shipped on their own)
    for (SceneEntry& se : built) {
        const std::string label = se.src.empty() ? std::string("the open scene") : "scene " + se.src.stem().string();
        if (se.isOpen) {
            if (!SceneSerializer::SaveToFile(ed.scene(), (data / se.fn).string())) return "Failed to write " + se.fn + ".";
            for (const std::string& a : SceneSerializer::CollectAssetPaths(ed.scene())) roots.push_back({a, se.src.string()});
            // Its visual-script graphs are embedded in the scene file just written: they need not ship.
            for (const std::string& g : bp::ScriptRefsInSceneText(SceneSerializer::Serialize(ed.scene()))) {
                bp::WalkSettings gs; gs.projectDir = ed.projectDir();
                const std::string gsrc = bp::ResolveAssetFile(g, se.src.parent_path().string(), gs);
                if (!gsrc.empty()) sceneSources.push_back(gsrc);
            }
        } else {
            okay::CopyFileOver(se.src, data / se.fn, pe);
            if (pe) return "Couldn't copy the scene " + se.src.string() + " (" + pe.message() + ").";
            { std::ifstream sf(se.src, std::ios::binary); std::ostringstream sb; sb << sf.rdbuf();   // a scene copied as a file still reads its graphs from their files
              for (const std::string& g : bp::ScriptRefsInSceneText(okay::DataPack::Unpack(sb.str()))) roots.push_back({g, se.src.string()}); }
            se.loaded = std::make_unique<Scene>("build");
            std::string lerr;
            if (SceneSerializer::LoadFromFile(*se.loaded, se.src.string(), &lerr))
                for (const std::string& a : SceneSerializer::CollectAssetPaths(*se.loaded)) roots.push_back({a, se.src.string()});
            else R.r.Warn("Couldn't read " + se.src.string() + " to list its assets (" + lerr + "): its assets are not shipped.");
        }
        if (!se.src.empty()) sceneSources.push_back(se.src.string());
        why[relRoot(data / se.fn)] = label + (sceneCount == 0 ? " (starts the game)" : "");
        ++sceneCount;
    }

    // 2) The dependency walk: the assets those scenes use, and what those assets use in turn.
    bp::WalkSettings ws;
    ws.projectDir = ed.projectDir();
    // The project's input asset ships even when no scene names it: the game loads it at start-up (InputRuntime::UseProjectAsset).
    if (!ed.projectDir().empty() && !bp::ResolveAssetFile(okay::InputRuntime::kProjectAssetRef, "", ws).empty()) roots.push_back({okay::InputRuntime::kProjectAssetRef, ""});
    for (const SceneEntry& se : built) if (!se.src.empty()) ws.searchDirs.push_back(se.src.parent_path().string());
    bp::WalkResult walk = bp::WalkAssets(roots, ws);
    std::vector<std::string> shippedSources;
    std::uintmax_t unusedShipped = 0; int unusedShippedCount = 0;
    if (!opt.stripAssets && !ed.projectDir().empty()) {
        std::set<std::string> have;
        for (const bp::ShippedAsset& a : walk.assets) { std::error_code e; have.insert(fs::weakly_canonical(fs::path(a.src), e).generic_string()); }
        std::set<std::string> sceneKeys;
        for (const std::string& s : sceneSources) { std::error_code e; sceneKeys.insert(fs::weakly_canonical(fs::path(s), e).generic_string()); }
        for (const bp::ProjectAsset& pa : bp::ListProjectAssets(ed.projectDir())) {
            std::error_code e;
            const std::string key = fs::weakly_canonical(fs::path(pa.src), e).generic_string();
            if (pa.editorOnly || pa.scene || have.count(key) || sceneKeys.count(key)) continue;
            bp::ShippedAsset a;
            a.ref = "Assets/" + pa.rel; a.dest = a.ref; a.src = pa.src; a.bytes = pa.bytes; a.reason = "asset stripping is off: every project asset ships";
            walk.assets.push_back(a);
            unusedShipped += pa.bytes; ++unusedShippedCount;
        }
    }
    if (!opt.excludeList.empty()) {   // Build Settings > Output: files the owner left out. A file a scene uses is warned about, not silently dropped.
        std::vector<bp::ShippedAsset> keep;
        for (const bp::ShippedAsset& a : walk.assets) {
            if (!oked::bsm::IsExcluded(a.dest, opt.excludeList) && !oked::bsm::IsExcluded(a.ref, opt.excludeList)) { keep.push_back(a); continue; }
            if (a.reason.rfind("asset stripping is off", 0) != 0) R.r.Warn("'" + a.dest + "' is left out of the build but a scene uses it: the game will not find it.");
        }
        walk.assets.swap(keep);
    }
    int copied = 0;
    std::uintmax_t assetBytes = 0;
    for (const bp::ShippedAsset& a : walk.assets) {
        const fs::path dst = data / fs::u8path(a.dest);
        std::error_code aec;
        if (dst.has_parent_path()) fs::create_directories(dst.parent_path(), aec);
        okay::CopyFileOver(fs::path(a.src), dst, aec);
        if (aec) { R.r.Warn("Couldn't copy " + a.src + " (" + aec.message() + ")."); continue; }
        ++copied; assetBytes += a.bytes;
        shippedSources.push_back(a.src);
        why[relRoot(dst)] = a.reason;
        fs::path ms = fs::path(a.src); ms += ".okaymeta";   // its import settings (texture filter / wrap / mipmaps / largest side, model normals ...) travel with it
        std::error_code me;
        if (fs::is_regular_file(ms, me)) {
            fs::path md = dst; md += ".okaymeta";
            okay::CopyFileOver(ms, md, me);
            if (!me) why[relRoot(md)] = "import settings of " + a.dest;
        }
        {   // a picture set to `compress`: its BC copy ships beside it, so the game never encodes it at run time
            std::string terr;
            if (okay::gputex::ShipCompressedCopy(a.src, dst.string(), &terr))
                why[relRoot(fs::u8path(okay::gputex::CompressedCachePath(dst.string())))] = "compressed GPU copy of " + a.dest;
            else if (!terr.empty())
                R.r.Warn("Couldn't make the compressed copy of " + a.dest + " (" + terr + "): the game makes it when it first draws it.");
        }
    }
    for (const bp::MissingAsset& m : walk.missing) {
        ++R.r.missingAssets;
        R.r.Warn("Missing asset '" + m.ref + "' (used by " + (m.referrer.empty() ? std::string("a scene") : fs::path(m.referrer).filename().string()) + "): not found, so it is not shipped.");
    }
    for (const std::string& a : walk.absolute)
        R.r.Warn("'" + a + "' is an absolute path: it is copied by file name, but the game still looks for the original path. Use a project-relative path.");
    for (const std::string& n : plan.notes) R.r.Warn(n);
    if (unusedShippedCount)
        R.r.Warn("Asset stripping is off: " + std::to_string(unusedShippedCount) + " project file(s) the scenes do not use (" + bp::HumanBytes(unusedShipped) + ") are shipped.");

    // 1b) Write game.okayconfig (window + scene list) read by the player.
    std::string settingsText;
    {
        std::ostringstream cf;
        cf << "title=" << gameName << "\n";
        cf << "company=" << opt.company << "\n";
        cf << "version=" << opt.version << "\n";
        cf << "build_number=" << opt.buildNumber << "\n";
        cf << "width=" << opt.width << "\n";
        cf << "height=" << opt.height << "\n";
        cf << "fullscreen=" << (opt.fullscreen ? 1 : 0) << "\n";
        cf << "borderless=" << (opt.borderless ? 1 : 0) << "\n";
        cf << "resizable=" << (opt.resizable ? 1 : 0) << "\n";
        cf << "vsync=" << (opt.vsync ? 1 : 0) << "\n";
        cf << "smooth_dt=" << (opt.smoothDt ? 1 : 0) << "\n";
        cf << "cursor=" << (opt.hideCursor ? 0 : 1) << "\n";
        cf << "fps_cap=" << opt.fpsCap << "\n";
        cf << "quit_on_escape=" << (opt.quitOnEscape ? 1 : 0) << "\n";
        cf << "volume=" << opt.masterVolume << "\n";
        cf << "show_fps=" << (opt.showFps ? 1 : 0) << "\n";
        cf << "development=" << (opt.developmentBuild ? 1 : 0) << "\n";
        cf << "physics_hz=" << g_project.physicsHz << "\n";
        cf << "physics_smooth=" << (g_project.smoothPhysics ? 1 : 0) << "\n";
        cf << "lock_cursor=" << (opt.lockCursor ? 1 : 0) << "\n";
        cf << "cursor_lock=" << (opt.cursorLockMode == 2 ? "confined" : opt.cursorLockMode == 1 ? "locked" : "none") << "\n";
        cf << "perpixel=" << (opt.perPixelLighting ? 1 : 0) << "\n";
        cf << "shadows=" << (opt.shadows ? 1 : 0) << "\n";
        cf << "shadow_distance=" << opt.shadowDistance << "\n";
        cf << "shadow_softness=" << opt.shadowSoftness << "\n";
        cf << "shadow_cascades=" << opt.shadowCascades << "\n";
        cf << "shadow_resolution=" << opt.shadowResolution << "\n";
        cf << "bloom=" << (opt.bloom ? 1 : 0) << "\n";
        cf << "ssao=" << (opt.ssao ? 1 : 0) << "\n";
        cf << "fxaa=" << (opt.fxaa ? 1 : 0) << "\n";
        cf << "antialias=" << opt.antialias << "\n";
        cf << "gpu=" << (opt.gpuRenderer ? 1 : 0) << "\n";
        cf << "d3d12=" << (opt.preferD3D12 ? 1 : 0) << "\n";
        // Player presentation.
        if (!opt.iconPath.empty()) cf << "icon=" << fs::path(opt.iconPath).filename().string() << "\n";
        cf << "bundle_id=" << opt.bundleId << "\n";
        cf << "run_background=" << (opt.runInBackground ? 1 : 0) << "\n";
        cf << "mute_unfocused=" << (opt.muteOnFocusLoss ? 1 : 0) << "\n";
        cf << "start_maximized=" << (opt.startMaximized ? 1 : 0) << "\n";
        cf << "min_width=" << opt.minWidth << "\n";
        cf << "min_height=" << opt.minHeight << "\n";
        cf << "save_userdir=" << (opt.saveToUserDir ? 1 : 0) << "\n";
        cf << "fullscreen_toggle=" << (opt.fullscreenToggle ? 1 : 0) << "\n";
        if (!opt.splashImage.empty()) {
            cf << "splash=" << fs::path(opt.splashImage).filename().string() << "\n";
            cf << "splash_time=" << opt.splashTime << "\n";
            cf << "splash_bg=" << (int)(opt.splashBg[0] * 255) << "," << (int)(opt.splashBg[1] * 255)
               << "," << (int)(opt.splashBg[2] * 255) << "\n";
        }
        // Boot splash (branded loading frame shown immediately on launch, before
        // the scene/renderer are up) — the project's custom loading screen.
        if (!opt.bootLogo.empty())
            cf << "boot_logo=" << fs::path(opt.bootLogo).filename().string() << "\n";
        cf << "boot_bg_top=" << opt.bootBgTop[0] << " " << opt.bootBgTop[1] << " " << opt.bootBgTop[2] << "\n";
        cf << "boot_bg_bot=" << opt.bootBgBot[0] << " " << opt.bootBgBot[1] << " " << opt.bootBgBot[2] << "\n";
        if (opt.bootAccentOverride)
            cf << "boot_accent=" << opt.bootAccent[0] << " " << opt.bootAccent[1] << " " << opt.bootAccent[2] << "\n";
        cf << "boot_bar=" << (opt.bootBar ? 1 : 0) << "\n";
        cf << "startup=game.okayscene\n";
        cf << okay::buildpipe::WriteExtraConfig(opt.extra);   // nothing at the defaults, so old projects build the same config
        for (const std::string& s : sceneFiles) cf << "scene=" << s << "\n";
        if (opt.antiCheat) cf << "anticheat=" << okayac::ModeName((okayac::Mode)opt.antiCheat) << "\n";
        settingsText = cf.str();
        std::ofstream out((data / "game.okayconfig").string());
        out << settingsText;
        why[relRoot(data / "game.okayconfig")] = "window, graphics and scene list settings";
    }

    // Optional: obfuscate the shipped scenes + config so they can't be read or
    // edited in a text editor (the player decodes them transparently). Deters
    // casual tampering; not strong DRM (the key ships in the binary).
    if (opt.encryptData) {
        PackFileInPlace(data / "game.okayconfig");
        for (const std::string& s : sceneFiles) PackFileInPlace(data / s);
    }

    // 3) Copy the player runtime, renamed to <Game>.exe. If it isn't beside the
    // editor, fetch it from the releases bucket so the build is self-contained.
    std::string fetchNote;
    std::string player = EnsurePlayer(&fetchNote);
    const std::string exeName = plan.exeName;
    if (player.empty()) {
        return "Wrote the scenes to " + dir.string() +
               ", but couldn't find or download the player runtime. Check your "
               "internet connection, or place OkaySpacePlayer next to the editor.";
    }
    // Copy over an existing target with okay::CopyFileOver (a plain copy_file with overwrite_existing reports
    // "file exists" on Windows with MinGW). Copying a file onto itself is an error, so that case is skipped.
    auto copyOver = [](const fs::path& src, const fs::path& dst, std::error_code& e) -> bool {
        e.clear();
        std::error_code e0;
        if (fs::exists(dst, e0) && fs::equivalent(src, dst, e0)) return true;  // same file → nothing to do
        return okay::CopyFileOver(src, dst, e);
    };
    auto putRuntime = [&](const fs::path& src, const std::string& name, const std::string& reason, std::error_code& e) -> bool {
        if (opt.dryRun) {
            std::error_code se; virtualFiles.emplace_back(name, fs::file_size(src, se)); why[name] = reason; e.clear(); return true;
        }
        why[name] = reason;
        return copyOver(src, root / name, e);
    };
    if (!putRuntime(player, exeName, "the player runtime", ec))
        return "Wrote the scenes but couldn't copy the player: " + ec.message();
#if !defined(_WIN32)
    if (!opt.dryRun) fs::permissions(root / exeName, fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec, fs::perm_options::add, ec);
#endif

    // Bundle the runtime libraries the game needs beside it — most importantly
    // SDL2.dll on Windows, without which the game won't launch. Look next to the
    // player, then next to the editor (and common subfolders). Nothing else is copied: no editor files,
    // no tools, no symbols.
    {
        fs::path pdir = fs::path(player).parent_path();
        fs::path edir = updater::SelfPath().empty() ? fs::path() : fs::path(updater::SelfPath()).parent_path();
        std::vector<fs::path> roots2 = {pdir, edir, edir / "bin", edir.parent_path(), pdir.parent_path()};
#if defined(_WIN32)
        const char* libs[] = {"SDL2.dll", "libwinpthread-1.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll"};
#else
        const char* libs[] = {"libSDL2-2.0.so.0"};
#endif
        for (const char* lib : libs) {
            for (const fs::path& r2 : roots2) {
                if (r2.empty()) continue;
                std::error_code fe;
                fs::path src = r2 / lib;
                if (fs::exists(src, fe)) { std::error_code ce; putRuntime(src, lib, "runtime library the player needs", ce); break; }
            }
        }
    }

    // Window icon, startup splash and boot logo: beside the config in the data folder.
    auto putPresentation = [&](const std::string& path, const char* what) {
        if (path.empty()) return;
        std::error_code ie; fs::path isrc(path);
        if (fs::exists(isrc, ie) && fs::is_regular_file(isrc, ie)) {
            okay::CopyFileOver(isrc, data / isrc.filename(), ie);
            if (!ie) { why[relRoot(data / isrc.filename())] = what; shippedSources.push_back(path); }
        } else R.r.Warn(std::string("The ") + what + " file " + path + " was not found.");
    };
    putPresentation(opt.iconPath, "window icon");
    putPresentation(opt.splashImage, "startup splash");
    putPresentation(opt.bootLogo, "boot logo");
    CopyLocalizationFiles(ed, data.u8string());   // strings.okayloc and its languages' fonts

    // StreamingAssets/: the project's StreamingAssets folder, copied as it is (no stripping, no renaming), for
    // files the game opens by name at run time (Paths::StreamingAssetsDir).
    int streaming = 0;
    for (const auto& sf : bp::ListStreamingAssets(ed.projectDir())) {
        const fs::path dst = data / "StreamingAssets" / fs::u8path(sf.first);
        std::error_code se;
        fs::create_directories(dst.parent_path(), se);
        okay::CopyFileOver(fs::path(sf.second), dst, se);
        if (se) { R.r.Warn("Couldn't copy StreamingAssets/" + sf.first + " (" + se.message() + ")."); continue; }
        why[relRoot(dst)] = "StreamingAssets (copied as it is)"; ++streaming;
    }

    // Data/manifest.json: every shipped file's SHA-256, signed when a key is set up
    // (shared/AntiCheatIntegrity.hpp). Only written (and only needed) when anti-cheat is on.
    okayac::BuildManifestResult integrity;
    if (opt.antiCheat) {
        integrity = okayac::WriteBuildManifest(root, gameName, opt.version, okayac::LoadSigningKey(), (okayac::Mode)opt.antiCheat);   // the mode is signed too
        if (!integrity.message.empty()) R.r.Warn(integrity.message);
        why["Data/manifest.json"] = "anti-cheat file hashes";
    }

    // <data>/build.json: what this build is. The player reads it (--build-info, the log, the development overlay).
    {
        std::uintmax_t soFar = 0;
        { std::error_code te;
          for (auto& e : fs::recursive_directory_iterator(root, te)) if (e.is_regular_file(te)) { std::error_code se; soFar += fs::file_size(e.path(), se); }
          for (const auto& v : virtualFiles) soFar += v.second; }
        bp::BuildManifest bm;
        bm.game = gameName; bm.company = opt.company; bm.bundleId = opt.bundleId; bm.version = opt.version; bm.buildNumber = opt.buildNumber;
        bm.engineVersion = okay::BuildInfo::Version(); bm.dateUtc = utcNow; bm.platform = windowsHost ? "windows" : "linux";
        bm.layout = bp::LayoutName(plan.effective); bm.development = opt.developmentBuild; bm.stripped = opt.stripAssets;
        bm.compressed = wantZip && opt.compress;
        bm.scenes = sceneFiles;
        bm.assetCount = copied;
        bm.totalBytes = soFar;
        bm.settingsHash = bp::HashHex(bp::HashText(settingsText + "|" + bp::LayoutName(plan.effective) + (opt.stripAssets ? "|strip" : "|all") + (opt.encryptData ? "|enc" : "")));
        std::ofstream bf((data / "build.json").string(), std::ios::binary);
        bf << bp::ManifestToJson(bm);
        why[relRoot(data / "build.json")] = "build info";
    }

    // The report: every file in the build, then what was left out and why.
    bp::Report& rp = R.r;
    rp.dryRun = opt.dryRun;
    rp.layout = bp::LayoutName(plan.effective);
    rp.outputDir = dir.string();
    {
        std::error_code te;
        for (auto& e : fs::recursive_directory_iterator(root, te)) {
            if (!e.is_regular_file(te)) continue;
            std::error_code se;
            const std::string rel = relRoot(e.path());
            auto w = why.find(rel);
            rp.AddFile(rel, fs::file_size(e.path(), se), w == why.end() ? std::string() : w->second);
        }
        for (const auto& v : virtualFiles) rp.AddFile(v.first, v.second, why[v.first]);
    }
    if (opt.stripAssets && !ed.projectDir().empty()) {
        std::set<std::pair<std::string, std::uintmax_t>> present;
        for (const bp::ReportFile& f : rp.files) present.insert({fs::path(f.path).filename().string(), f.bytes});
        for (const bp::ExcludedFile& x : bp::FindUnshipped(ed.projectDir(), shippedSources, sceneSources)) {
            const std::string fn = fs::path(x.path).filename().string();
            if (present.count({fn, x.bytes}) || fn == "strings.okayloc") continue;   // shipped another way (icon, fonts, string table)
            rp.AddExcluded(x.path, x.bytes, x.reason);
            if (x.bytes >= (1u << 20) && x.reason.rfind("not used", 0) == 0)
                rp.Warn(x.path + " (" + bp::HumanBytes(x.bytes) + ") is not used by any scene in the build, so it was left out. Reference it from a scene, or turn off asset stripping, if the game needs it.");
        }
    }
    rp.Finish();
    {   // the engine modules in the player, with their sizes from module_sizes.txt beside it (docs/engine/modular_engine.md)
        const std::string pl = FindPlayer();
        rp.AddModules(pl.empty() ? std::string() : (fs::path(pl).parent_path() / "module_sizes.txt").string(), g_project.modules);
        okaymod::Selection sel(okay::ModuleRegistry::Get(), g_project.modules);   // Project Settings > Modules
        for (const auto& u : okaymod::UsedButOff(sel, okaymod::UsageInScene(okay::ModuleRegistry::Get(), ed.scene()))) {
            const auto* mi = okay::ModuleRegistry::Get().Find(u.first);
            rp.Warn("The scene uses " + std::to_string(u.second) + " object(s) of the module " + (mi ? mi->manifest.title : u.first) +
                    ", which is off in Project Settings > Modules. The game still contains it in this version.");
        }
    }
    rp.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // 4) The archive: a zip of the finished build (Zip layout keeps only the zip; "also zip" keeps both).
    std::string zipNote;
    if (wantZip && !opt.dryRun) {
        fs::path zipPath = plan.makeZip ? dir / plan.zipName
                                         : dir.parent_path() / (bp::ExpandPattern(opt.archiveName, li.tokens) + ".zip");
        bp::ZipStats zs; std::string zerr;
        const auto z0 = std::chrono::steady_clock::now();
        if (!bp::WriteZip(zipPath.string(), root.string(), opt.compress, &zs, &zerr)) {
            rp.Warn("Couldn't create the archive: " + zerr);
        } else {
            std::error_code se;
            rp.archivePath = zipPath.string();
            rp.archiveBytes = fs::file_size(zipPath, se);
            rp.archiveCompressed = opt.compress;
            zipNote = "  Archive " + zipPath.filename().string() + " " + bp::HumanBytes(rp.archiveBytes) + (opt.compress ? " (compressed from " : " (stored, ") + bp::HumanBytes(zs.stored) + (opt.compress ? ")" : ")");
        }
        rp.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - z0).count();
    }
    if (zipOnly && !opt.dryRun) { std::error_code se; fs::remove_all(root, se); }

    // The same report as a file beside the project's other editor state (never inside the game).
    if (!ed.projectDir().empty()) {
        std::error_code we;
        fs::create_directories(fs::path(ed.projectDir()) / ".okay", we);
        std::ofstream rf((fs::path(ed.projectDir()) / ".okay" / "build_report.txt").string(), std::ios::binary);
        rf << rp.ToText();
    }

    // The older size table (and the server export) read these.
    R.total = rp.total; R.missing = rp.missingAssets; R.valid = true; R.ok = true;
    for (const bp::ReportFile& f : rp.files) {
        int cat = f.kind == bp::FileKind::Executable ? CAT_EXE : f.kind == bp::FileKind::Library ? CAT_LIB
                : (f.kind == bp::FileKind::Scene || f.kind == bp::FileKind::Config) ? CAT_DATA : CAT_ASSET;
        R.files.push_back({f.path, f.bytes, cat});
        R.catBytes[cat] += f.bytes;
    }

    auto human = [](std::uintmax_t b) { return bp::HumanBytes(b); };
    std::string msg;
    if (opt.dryRun) {
        msg = "Dry run for '" + gameName + "' (" + std::string(bp::LayoutName(plan.effective)) + " layout): " + std::to_string(rp.files.size()) + " files, " + human(rp.total) +
              ", " + std::to_string(rp.excluded.size()) + " left out. Nothing was written to " + dir.string() + ".";
    } else {
        const std::string where = plan.makeZip && !plan.keepFolder ? (dir / plan.zipName).string() : dir.string();
        msg = "Built '" + gameName + "' to " + where;
        if (!(plan.makeZip && !plan.keepFolder)) msg += " — run " + exeName + " to play";
        msg += ".  [" + human(rp.total) + " total";
        if (!plan.dataDir.empty() && !(plan.makeZip && !plan.keepFolder)) msg += ", game data in " + plan.dataDir + "/";
        msg += "]";
        if (!fetchNote.empty() && fetchNote.rfind("Downloaded", 0) == 0) msg += "  (runtime fetched from the download server)";
        if (sceneCount > 1) msg += "  (" + std::to_string(sceneCount) + " scenes)";
        if (copied) msg += "  (" + std::to_string(copied) + " asset(s), " + human(assetBytes) + ")";
        if (streaming) msg += "  (" + std::to_string(streaming) + " StreamingAssets file(s))";
        msg += zipNote;
    }
    if (!rp.excluded.empty()) msg += "  " + std::to_string(rp.excluded.size()) + " unused project file(s) left out (" + human(rp.excludedBytes) + ").";
    if (R.missing) msg += "  WARNING: " + std::to_string(R.missing) + " referenced asset(s) not found and not copied.";
    if (!integrity.message.empty()) msg += "  (" + integrity.message + ")";
    {   // Build Settings > Advanced: the command to run AFTER a good build (it cannot undo the build, so a failure is only reported)
        const std::string post = oked::bsm::CommandToRun(opt.postCommand, opt.allowCommands, opt.dryRun, cmdVars);
        if (!post.empty()) {
            const int rc = oked::bsm::RunCommandLine(post, ed.projectDir());
            msg += rc == 0 ? "  (ran your command after the build)" : "  WARNING: your command after the build failed (exit code " + std::to_string(rc) + ").";
        }
    }
    return msg;
}

// The Build Settings as the builder's options (the Build Settings window and `--build-game` both use this).
Options OptionsFromSettings(const BuildSettings& g, const ProjectSettings& pr) {
    Options o;
    o.company = g.company; o.version = g.version;
    o.width = g.width; o.height = g.height;
    o.fullscreen = g.fullscreen; o.borderless = g.borderless;
    o.resizable = g.resizable; o.vsync = g.vsync; o.smoothDt = g.smoothDt;
    o.hideCursor = g.hideCursor; o.fpsCap = g.fpsCap;
    o.quitOnEscape = g.quitOnEscape; o.masterVolume = g.masterVolume; o.showFps = g.showFps;
    o.includeAllProjectScenes = g.includeAllProjectScenes;
    o.developmentBuild = g.developmentBuild;
    o.lockCursor = g.lockCursor; o.cursorLockMode = g.cursorLockMode; o.perPixelLighting = g.perPixelLighting;
    o.shadows = g.shadows; o.bloom = g.bloom; o.ssao = g.ssao;
    o.shadowDistance = g.shadowDistance; o.shadowSoftness = g.shadowSoftness;
    o.shadowCascades = g.shadowCascades; o.shadowResolution = g.shadowResolution;
    o.fxaa = g.fxaa; o.antialias = g.antialias;
    o.gpuRenderer = g.gpuRenderer;
    o.preferD3D12 = g.preferD3D12;
    o.cleanOutput = g.cleanOutput; o.dataFolder = g.dataFolder;
    o.encryptData = g.encryptData;
    o.antiCheat = g.antiCheat;
    o.iconPath = g.icon; o.bundleId = g.bundleId;
    o.runInBackground = g.runInBackground; o.muteOnFocusLoss = g.muteOnFocusLoss;
    o.startMaximized = g.startMaximized;
    o.minWidth = g.minWidth; o.minHeight = g.minHeight;
    o.saveToUserDir = g.saveToUserDir;
    o.fullscreenToggle = g.fullscreenToggle;
    for (const BuildScene& sc : g.scenes) if (sc.on) o.sceneList.push_back(sc.path);
    o.splashImage = g.splash; o.splashTime = g.splashTime;
    o.splashBg[0] = g.splashBg[0]; o.splashBg[1] = g.splashBg[1]; o.splashBg[2] = g.splashBg[2];
    // Carry the project's custom loading screen into the shipped game.
    o.bootLogo = pr.splashLogo;
    for (int i = 0; i < 3; ++i) { o.bootBgTop[i] = pr.splashBgTop[i]; o.bootBgBot[i] = pr.splashBgBot[i]; o.bootAccent[i] = pr.splashAccent[i]; }
    o.bootAccentOverride = pr.splashAccentOverride; o.bootBar = pr.splashShowBar;
    o.layout = g.layout; o.stripAssets = g.stripAssets; o.compress = g.compress; o.zipAlso = g.zipAfter && g.layout != 2;
    o.buildNumber = g.buildNumber; o.archiveName = g.archiveName;
    o.extra = oked::bsm::ExtraFrom(g); o.excludeList = g.excludeList;
    o.allowCommands = g.allowBuildCommands; o.preCommand = g.preBuildCommand; o.postCommand = g.postBuildCommand;
    return o;
}

// Export the platform-independent game DATA (scene + config + assets) plus a
// ready-to-build project for a non-desktop target. The editor can't run Emscripten
// (web) or the Android NDK itself, so it lays down everything those toolchains need
// and a build script / README with the exact commands. `target`: 1 = Web (WASM +
// WebGL via Emscripten), 2 = Android (SDL2 + NDK).
std::string ExportForPlatform(EditorState& ed, const std::string& outDir,
                              const std::string& gameName, const Options& opt, int target) {
    if (ed.isPlaying()) { okay::editor::apreview::Stop(ed.scene()); ed.Stop(); }   // export the edited scene, not the running copy
    std::error_code ec;
    fs::path dir = fs::path(outDir);
    if (dir.empty()) return "Pick an output folder.";
    fs::create_directories(dir, ec);
    if (ec) return "Couldn't create folder: " + ec.message();

    // Web preloads a 'game/' folder; Android expects app/src/main/assets — either
    // way it's just our scene + config + assets.
    fs::path data = dir / (target == 2 ? "assets" : target == 4 ? fs::path("package") / "Data" : fs::path("game"));
    fs::create_directories(data, ec);
    if (!SceneSerializer::SaveToFile(ed.scene(), (data / "game.okayscene").string()))
        return "Failed to write game.okayscene.";
    { std::ofstream cf((data / "game.okayconfig").string());
      cf << "title=" << gameName << "\ncompany=" << opt.company << "\nversion=" << opt.version
         << "\nwidth=" << opt.width << "\nheight=" << opt.height << "\nstartup=game.okayscene\n"; }

    int copied = 0, missing = 0;
    std::set<std::string> seen;
    for (const std::string& asset : SceneSerializer::CollectAssetPaths(ed.scene())) {
        if (asset.empty() || !seen.insert(asset).second) continue;
        fs::path src(asset); std::error_code aec;
        if (!fs::exists(src, aec) || !fs::is_regular_file(src, aec)) { ++missing; continue; }
        fs::path rel = src.is_absolute() ? fs::path(src.filename()) : SafeRelative(src);
        if (rel.empty()) rel = src.filename();
        fs::path dst = data / rel;
        if (dst.has_parent_path()) fs::create_directories(dst.parent_path(), aec);
        okay::CopyFileOver(src, dst, aec);
        if (!aec) ++copied;
        { std::error_code me; fs::path ms = src; ms += ".okaymeta";
          if (!aec && fs::is_regular_file(ms, me)) { fs::path md = dst; md += ".okaymeta"; okay::CopyFileOver(ms, md, me); } }
    }
    CopyLocalizationFiles(ed, data.u8string());   // strings.okayloc and its languages' fonts

#if defined(OKAY_XBOX_UWP)
    if (target == 4) {   // ---- Xbox One Developer Mode (UWP), EXPERIMENTAL and UNVERIFIED: manifest, assets, notes beside package/Data ----
        okay::xbox::ExportInputs xi;
        xi.gameName = gameName; xi.company = opt.company; xi.version = opt.version; xi.bundleId = opt.bundleId; xi.iconPath = opt.iconPath;
        const okay::xbox::ExportResult xr = okay::xbox::WritePackage(dir.string(), xi);
        g_xboxExportOk = xr.ok; g_xboxExportWarnings = xr.warnings;
        std::string msg = xr.message;
        for (const std::string& w : xr.warnings) msg += " Warning: " + w;
        return msg;
    }
#endif
    if (target == 1) {   // ---- Web (Emscripten / WebGL) ----
        // One build script for every game: the engine's scripts/build-web.sh builds the player (once; later games reuse
        // it) and packs game/ beside it into web/ (index.html, okay-player.js, okay-player.wasm.gz, game.okaypack.gz)
        // plus a zip with index.html at its root for itch.io-style hosts. Build Settings > Web > Run in browser serves web/.
        const std::string zipName = okay::buildpipe::FileSafeName(gameName, "Game") + "-web.zip";
        std::ofstream sh((dir / "build-web.sh").string());
        sh << "#!/usr/bin/env bash\n"
              "# Build this game for the web (WebAssembly + WebGL) with Emscripten.\n"
              "# Prereqs: install emsdk (https://emscripten.org), 'source emsdk_env.sh',\n"
              "# and set OKAY_SRC to your OkaySpace engine source checkout.\n"
              "set -euo pipefail\n"
              "cd \"$(dirname \"$0\")\"\n"
              ": \"${OKAY_SRC:?Set OKAY_SRC to the OkaySpace engine source folder}\"\n"
              "bash \"$OKAY_SRC/scripts/build-web.sh\" --game \"$PWD/game\" --out \"$PWD/web\" --zip \"$PWD/" << zipName << "\" \"$@\"\n";
        std::ofstream rd((dir / "README.txt").string());
        rd << "OkaySpace - Web (WebGL / WebAssembly) export of '" << gameName << "'\n"
              "======================================================\n\n"
              "game/         your scene + assets (game.okayscene, game.okayconfig, ...)\n"
              "build-web.sh  builds the browser version with Emscripten into web/ and " << zipName << "\n\n"
              "Steps:\n"
              "  1. Install the Emscripten SDK, then: source /path/to/emsdk/emsdk_env.sh\n"
              "  2. export OKAY_SRC=/path/to/OkaySpace   (the engine source checkout)\n"
              "  3. ./build-web.sh            (add --fit aspect to keep the game's shape, --no-compress for hosts that compress)\n"
              "  4. Build Settings > Web > Run in browser, or upload " << zipName << " (index.html is at its root).\n\n"
              "web/ holds index.html, okay-player.js, okay-player.wasm.gz (the program) and game.okaypack.gz (your files).\n"
              "Saves are kept in the browser (IndexedDB) and survive a reload.\n";
        const oked::webrun::Sizes ws = oked::webrun::Measure((dir / "web").u8string());
        std::string msg = "Exported Web (WebGL) project to " + dir.string() + " (" + std::to_string(copied) + " asset(s)).";
        if (ws.built) msg += " The web build in web/ is from before this export: run build-web.sh again to update it (" + oked::webrun::SizeLine(ws) + ").";
        else msg += " Run build-web.sh with Emscripten to make the browser version in web/.";
        return msg;
    }

    // target == 2: ---- Android (SDL2 + NDK) ----
    std::ofstream rd((dir / "README.txt").string());
    rd << "OkaySpace - Android export of '" << gameName << "'\n"
          "==============================================\n\n"
          "assets/   your scene + assets, ready to drop into an Android app's\n"
          "          app/src/main/assets/ folder.\n\n"
          "Building an APK needs Android Studio + the NDK and SDL2's Android project\n"
          "(the OkaySpace player is C++ on SDL2, which has an official Android port):\n"
          "  1. Start from SDL2's androidproject template (SDL/build-scripts).\n"
          "  2. Add the OkaySpace engine + player sources as the app's native code;\n"
          "     they build against SDL2's Android port.\n"
          "  3. Copy this 'assets/' folder into app/src/main/assets/.\n"
          "  4. Build/run from Android Studio (Gradle + NDK).\n\n"
          "Config: identifier " << opt.bundleId << ", version " << opt.version << ".\n";
    return "Exported Android data + guide to " + dir.string() + " (" + std::to_string(copied) +
           " asset(s)). See README.txt - an APK needs Android Studio + the NDK.";
}
// ---- Dedicated server export ---------------------------------------------------
// A ready-to-run server folder: the game's data + the player runtime for the
// target OS (it runs headless with --server), a server.cfg with the chosen
// settings, start scripts, and for Linux everything to put it online — an
// install script (service + firewall), deploy.sh/.bat (copy over SSH and
// install in one command), a Dockerfile/compose file and a README.

// "Star Arena!" -> "star-arena" (file names, service names, Docker images).
inline std::string ServerSlug(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c)) o += (char)std::tolower(c);
        else if (!o.empty() && o.back() != '-') o += '-';
    }
    while (!o.empty() && o.back() == '-') o.pop_back();
    return o.empty() ? std::string("game") : o;
}
// Scripts must keep Unix line endings even when written on Windows.
inline bool WriteFileExact(const fs::path& p, const std::string& body) {
    std::ofstream f(p.string(), std::ios::binary | std::ios::trunc);
    f << body;
    return (bool)f;
}
inline std::string CfgQuote(const std::string& v) {
    std::string o = "\"";
    for (char c : v) if (c != '"' && c != '\n' && c != '\r') o += c;
    return o + "\"";
}

std::string BuildServer(EditorState& ed, const std::string& outDir, const std::string& gameName,
                        Options opt, const ServerOptions& so, BuildReport* report) {
    // A server draws nothing: skip the presentation extras.
    opt.gpuRenderer = false; opt.iconPath.clear(); opt.splashImage.clear(); opt.bootLogo.clear();
    opt.layout = 1; opt.zipAlso = false; opt.dryRun = false;   // the start scripts and Dockerfile expect the exe with a Data/ folder
    BuildReport local;
    BuildReport* rep = report ? report : &local;
    const std::string built = Build(ed, outDir, gameName, opt, rep);
    if (!rep->valid) return built;   // Build() said what went wrong

    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::path(outDir), ec);
    if (dir.empty()) dir = fs::path(outDir);
    const bool linuxTarget = so.target == 0;
#if defined(_WIN32)
    const bool hostLinux = false;
    const fs::path hostExe = dir / (okay::buildpipe::FileSafeName(gameName, "Game") + ".exe");
#else
    const bool hostLinux = true;
    const fs::path hostExe = dir / okay::buildpipe::FileSafeName(gameName, "Game");
#endif
    const std::string slug = ServerSlug(gameName);
    const std::string binName = linuxTarget ? slug + "-server" : gameName + "-Server.exe";
    const fs::path bin = dir / binName;
    const std::string serverName = so.name.empty() ? gameName + " Server" : so.name;

    // 1) The runtime for the target OS.
    std::string fetched;
    if (linuxTarget == hostLinux) {
        fs::remove(bin, ec);
        fs::rename(hostExe, bin, ec);
        if (ec) return "Built the game data, but couldn't rename the runtime to " + binName + " (" + ec.message() + ").";
    } else {
        fs::remove(hostExe, ec);
        for (const char* lib : {"SDL2.dll", "libwinpthread-1.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libSDL2-2.0.so.0"})
            fs::remove(dir / lib, ec);
        auto fetch = [&](const char* asset, const fs::path& dest, std::size_t minBytes) {
            const std::string url = updater::RawUrl("latest", asset);   // the newest release in the releases bucket (never GitHub)
            if (url.empty() || !updater::Download(url, dest.string())) return false;
            std::error_code fe;
            if (!fs::exists(dest, fe) || fs::file_size(dest, fe) < minBytes) { fs::remove(dest, fe); return false; }
            return true;
        };
        if (linuxTarget) {
            if (!fetch("okay-server-linux-x64", bin, 100000))
                return "Built the game data to " + dir.string() + ", but couldn't download the Linux server runtime "
                       "(okay-server-linux-x64 from the latest release). Check your connection, or export a Windows server.";
            fetched = "the Linux server runtime";
        } else {
            if (!fetch("OkaySpacePlayer.exe", bin, 100000) || !fetch("SDL2.dll", dir / "SDL2.dll", 10000))
                return "Built the game data to " + dir.string() + ", but couldn't download the Windows runtime.";
            fetched = "the Windows runtime";
        }
    }
#if !defined(_WIN32)
    fs::permissions(bin, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif

    // 2) server.cfg: what the server reads at start (flags/env still override).
    {
        std::string c;
        c += "# " + gameName + " dedicated server settings.\n";
        c += "# Edit and restart the server to apply. Command-line flags override these.\n\n";
        c += "name = " + CfgQuote(serverName) + "\n";
        c += "port = " + std::to_string(so.port) + "\n";
        c += "max_players = " + std::to_string(so.maxPlayers) + "\n";
        c += "tick = " + std::to_string(so.tick) + "\n";
        c += "password = " + CfgQuote(so.password) + "\n";
        c += "# Only players signed in to OkaySpace may join.\n";
        c += std::string("auth = ") + (so.auth ? "1" : "0") + "\n";
        c += "encryption = 1\n";
        c += "status = 60\n\n";
        c += "# List this server in the launcher and in-game server browsers.\n";
        c += "# Needs the machine signed in once:  ./" + binName + " --login YOUR_NAME\n";
        c += std::string("advertise = ") + (so.advertise ? "1" : "0") + "\n";
        c += "public_addr = " + CfgQuote(so.publicAddr) + "\n";
        c += "region = " + CfgQuote(so.region) + "\n\n";
        c += "# Anti-cheat basics (0 = off).\n";
        char sp[32]; std::snprintf(sp, sizeof(sp), "%g", so.maxSpeed);
        c += std::string("max_speed = ") + sp + "\n";
        c += "rate_limit = " + std::to_string(so.rateLimit) + "\n";
        c += "kick_after = " + std::to_string(so.kickAfter) + "\n";
        if (!WriteFileExact(dir / "server.cfg", c)) return "Couldn't write server.cfg in " + dir.string() + ".";
    }

    // 3) Start scripts.
    const std::string portS = std::to_string(so.port);
    WriteFileExact(dir / "start-server.bat",
        "@echo off\r\nrem Start the " + gameName + " server (settings in server.cfg).\r\n"
        "cd /d \"%~dp0\"\r\n" + (linuxTarget ? "echo This server is built for Linux: copy the folder to a Linux machine, or use deploy.bat.\r\npause\r\n"
                                             : "\"" + binName + "\" --server %*\r\npause\r\n"));
    if (linuxTarget) {
        WriteFileExact(dir / "start-server.sh",
            "#!/bin/sh\n# Start the " + gameName + " server in this terminal (settings in server.cfg).\n"
            "cd \"$(dirname \"$0\")\" && exec ./" + binName + " --server \"$@\"\n");

        // install-server.sh: run ON the server (deploy.* does it for you).
        WriteFileExact(dir / "install-server.sh",
            "#!/usr/bin/env bash\n"
            "# Install this folder as a service that starts on boot and restarts if it stops.\n"
            "#   sudo bash install-server.sh        (from inside the folder, on the server)\n"
            "set -euo pipefail\n"
            "APP=" + slug + "-server\n"
            "DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
            "PORT=$(sed -n 's/^port *= *\\([0-9]*\\).*/\\1/p' \"$DIR/server.cfg\" | head -n1)\n"
            "PORT=${PORT:-" + portS + "}\n"
            "SUDO=; [ \"$(id -u)\" -ne 0 ] && SUDO=sudo\n"
            "if command -v apt-get >/dev/null; then\n"
            "  $SUDO apt-get update -qq && $SUDO apt-get install -y -qq libsdl2-2.0-0 libsodium23 >/dev/null || true\n"
            "elif command -v dnf >/dev/null; then\n"
            "  $SUDO dnf install -y -q SDL2 libsodium >/dev/null || true\n"
            "fi\n"
            "id okayspace >/dev/null 2>&1 || $SUDO useradd --system --home-dir \"$DIR\" --shell /usr/sbin/nologin okayspace\n"
            "chmod +x \"$DIR/" + binName + "\" \"$DIR\"/*.sh\n"
            "$SUDO chown -R okayspace \"$DIR\"\n"
            "$SUDO tee /etc/systemd/system/$APP.service >/dev/null <<EOF\n"
            "[Unit]\nDescription=" + gameName + " dedicated server\nAfter=network-online.target\nWants=network-online.target\n\n"
            "[Service]\nUser=okayspace\nWorkingDirectory=$DIR\nEnvironment=HOME=$DIR\n"
            "ExecStart=$DIR/" + binName + " --server\nRestart=always\nRestartSec=3\n\n"
            "[Install]\nWantedBy=multi-user.target\nEOF\n"
            "$SUDO systemctl daemon-reload\n"
            "$SUDO systemctl enable $APP >/dev/null\n"
            "$SUDO systemctl restart $APP\n"
            "if command -v ufw >/dev/null && $SUDO ufw status | grep -q active; then $SUDO ufw allow \"$PORT\"/udp >/dev/null; fi\n"
            "if command -v firewall-cmd >/dev/null && $SUDO firewall-cmd --state >/dev/null 2>&1; then\n"
            "  $SUDO firewall-cmd --permanent --add-port=\"$PORT\"/udp >/dev/null && $SUDO firewall-cmd --reload >/dev/null; fi\n"
            "sleep 1\n"
            "$SUDO systemctl --no-pager --lines=8 status $APP || true\n"
            "echo\n"
            "echo \"Running as the service '$APP' on UDP port $PORT.\"\n"
            "echo \"  logs:    journalctl -u $APP -f\"\n"
            "echo \"  restart: sudo systemctl restart $APP   (after editing server.cfg)\"\n"
            "echo \"  list it: sudo -u okayspace env HOME=$DIR $DIR/" + binName + " --login YOUR_NAME   (then set advertise = 1)\"\n");

        // deploy.sh / deploy.bat: run on YOUR machine — copy over SSH, then install.
        const std::string dest = "/opt/" + slug + "-server";
        WriteFileExact(dir / "deploy.sh",
            "#!/usr/bin/env bash\n"
            "# Put this server online on a Linux machine you can SSH into (a VPS):\n"
            "#   ./deploy.sh user@your-server [ssh-port]\n"
            "# Copies this folder to " + dest + " and installs it as a service.\n"
            "set -euo pipefail\n"
            "HOST=\"${1:?usage: ./deploy.sh user@host [ssh-port]}\"\nSSHPORT=\"${2:-22}\"\n"
            "cd \"$(dirname \"$0\")\"\n"
            "echo \"Copying to $HOST:" + dest + " ...\"\n"
            "ssh -t -p \"$SSHPORT\" \"$HOST\" 'sudo mkdir -p " + dest + " && sudo chown \"$(id -un)\" " + dest + "'\n"
            "tar czf - --exclude=deploy.sh --exclude=deploy.bat . | ssh -p \"$SSHPORT\" \"$HOST\" 'tar xzf - -C " + dest + "'\n"
            "ssh -t -p \"$SSHPORT\" \"$HOST\" 'sudo bash " + dest + "/install-server.sh'\n");
        WriteFileExact(dir / "deploy.bat",
            "@echo off\r\n"
            "rem Put this server online on a Linux machine you can SSH into (a VPS):\r\n"
            "rem   deploy.bat user@your-server [ssh-port]\r\n"
            "rem Uses the ssh and tar built into Windows 10/11.\r\n"
            "setlocal\r\n"
            "if \"%~1\"==\"\" (echo Usage: deploy.bat user@host [ssh-port] & exit /b 1)\r\n"
            "set HOST=%~1\r\nset SSHPORT=%~2\r\nif \"%SSHPORT%\"==\"\" set SSHPORT=22\r\n"
            "cd /d \"%~dp0\"\r\n"
            "echo Copying to %HOST%:" + dest + " ...\r\n"
            "ssh -t -p %SSHPORT% %HOST% \"sudo mkdir -p " + dest + " && sudo chown $(id -un) " + dest + "\" || exit /b 1\r\n"
            "tar czf - --exclude=deploy.sh --exclude=deploy.bat . | ssh -p %SSHPORT% %HOST% \"tar xzf - -C " + dest + "\" || exit /b 1\r\n"
            "ssh -t -p %SSHPORT% %HOST% \"sudo bash " + dest + "/install-server.sh\"\r\n"
            "pause\r\n");

        // Docker: for hosts that run containers (and Fly.io / Railway / etc.).
        WriteFileExact(dir / "Dockerfile",
            "# " + gameName + " dedicated server.\n"
            "#   docker build -t " + slug + "-server .\n"
            "#   docker run -d -p " + portS + ":" + portS + "/udp --restart unless-stopped " + slug + "-server\n"
            "FROM ubuntu:22.04\n"
            "RUN apt-get update && apt-get install -y --no-install-recommends libsdl2-2.0-0 libsodium23 ca-certificates \\\n"
            "    && rm -rf /var/lib/apt/lists/* && useradd --system --create-home okayspace\n"
            "WORKDIR /srv/game\n"
            "COPY . .\n"
            "RUN chmod +x " + binName + " && chown -R okayspace /srv/game\n"
            "USER okayspace\n"
            "EXPOSE " + portS + "/udp\n"
            "CMD [\"./" + binName + "\", \"--server\"]\n");
        WriteFileExact(dir / "docker-compose.yml",
            "# docker compose up -d   (settings in server.cfg, or OKAY_SERVER_* variables below)\n"
            "services:\n"
            "  server:\n"
            "    build: .\n"
            "    restart: unless-stopped\n"
            "    ports:\n"
            "      - \"" + portS + ":" + portS + "/udp\"\n"
            "    # environment:\n"
            "    #   OKAY_SERVER_NAME: \"" + serverName + "\"\n"
            "    #   OKAY_SERVER_MAX_PLAYERS: \"" + std::to_string(so.maxPlayers) + "\"\n");
        WriteFileExact(dir / ".dockerignore", "deploy.sh\ndeploy.bat\n*.zip\n");
    }

    // 4) README.
    {
        std::string r;
        r += gameName + " - dedicated server\n";
        r += std::string(gameName.size() + 19, '=') + "\n\n";
        r += "This folder is a complete game server: no window, no graphics. Players connect to it\n";
        r += "over UDP port " + portS + ". Settings live in server.cfg (restart after editing).\n\n";
        if (linuxTarget) {
            r += "PUT IT ONLINE (a Linux VPS: Hetzner, DigitalOcean, OVH, ...)\n";
            r += "  From this PC, in this folder:\n";
            r += "    Windows:  deploy.bat user@your-server\n";
            r += "    Mac/Linux: ./deploy.sh user@your-server\n";
            r += "  It copies the folder to /opt/" + slug + "-server, installs what it needs, opens the\n";
            r += "  firewall port and runs it as a service that restarts on its own and starts on boot.\n\n";
            r += "  By hand instead: copy the folder to the server and run  sudo bash install-server.sh\n";
            r += "  Just to try it:  ./start-server.sh\n\n";
            r += "DOCKER\n  docker compose up -d   (or see the Dockerfile)\n\n";
            r += "MANAGE IT (on the server)\n";
            r += "  logs:     journalctl -u " + slug + "-server -f\n";
            r += "  restart:  sudo systemctl restart " + slug + "-server\n";
            r += "  stop:     sudo systemctl stop " + slug + "-server\n\n";
        } else {
            r += "RUN IT\n  Double-click start-server.bat (or run: " + binName + " --server).\n";
            r += "  Allow " + binName + " through Windows Firewall (UDP " + portS + ") when asked.\n\n";
        }
        r += "LIST IT IN THE SERVER BROWSERS\n";
        r += "  1. Sign this machine in to OkaySpace once:\n";
        r += linuxTarget ? "       sudo -u okayspace env HOME=/opt/" + slug + "-server /opt/" + slug + "-server/" + binName + " --login YOUR_NAME\n"
                         : "       " + binName + " --login YOUR_NAME\n";
        r += "  2. In server.cfg set  advertise = 1  and  public_addr = your server's IP or hostname.\n";
        r += "  3. Restart the server. It shows up in the launcher's Multiplayer page and the game's\n";
        r += "     \"Find a server\" window within a minute.\n\n";
        r += "PLAYERS CAN ALSO JOIN BY ADDRESS\n";
        r += "  Connect to server... (launcher) or Address (in game):  your-server:" + portS + "\n\n";
        WriteFileExact(dir / "README.txt", r);
    }

#if !defined(_WIN32)
    for (const char* sh : {"start-server.sh", "install-server.sh", "deploy.sh"})
        fs::permissions(dir / sh, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif

    // 5) Re-tally the folder for the result popup.
    *rep = BuildReport{};
    for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::error_code se; const std::uintmax_t sz = fs::file_size(e.path(), se);
        std::string ext = e.path().extension().string();
        for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
        const std::string fn = e.path().filename().string();
        int cat = CAT_ASSET;
        if (fn == binName) cat = CAT_EXE;
        else if (ext == ".dll" || ext == ".so" || fn.rfind("libSDL2", 0) == 0) cat = CAT_LIB;
        else if (ext == ".okayscene" || ext == ".okayconfig" || fn == "server.cfg") cat = CAT_DATA;
        rep->files.push_back({fs::relative(e.path(), dir, se).generic_string(), sz, cat});
        rep->catBytes[cat] += sz; rep->total += sz;
    }
    std::sort(rep->files.begin(), rep->files.end(), [](const ReportEntry& a, const ReportEntry& b) { return a.bytes > b.bytes; });
    rep->valid = true;

    std::string msg = "Built the " + std::string(linuxTarget ? "Linux" : "Windows") + " dedicated server for '" + gameName +
                      "' to " + dir.string() + "  [" + HumanBytes(rep->total) + "]. ";
    msg += linuxTarget ? "Put it online with deploy.bat user@your-server (or ./deploy.sh), or docker compose up. "
                       : "Run start-server.bat. ";
    msg += "Settings are in server.cfg; README.txt has the steps.";
    if (!fetched.empty()) msg += "  (" + fetched + " was downloaded from the latest release)";
    if (so.advertise && so.publicAddr.empty()) msg += "  NOTE: set public_addr in server.cfg so the listing points players at the right address.";
    return msg;
}

} // namespace builder

} // namespace oked
