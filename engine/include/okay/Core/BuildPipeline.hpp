#pragma once
// The pure logic of File > Build Settings, kept out of the editor so it can be tested and reused by
// the headless `OkayEngine --build-game`:
//   * layout planning   - which folders/files a build has (<Name>.exe + <Name>_Data/, compact Data/, zip)
//   * the dependency walk - which project files a build must ship (the assets the built scenes use,
//                         and what those files use in turn), and which project files it leaves out
//   * the build manifest - <Name>_Data/build.json: who built what, when, with which settings
//   * the build report   - every shipped file by size and type, excluded files with the reason,
//                         warnings, totals, time
//   * a zip writer (stored / deflate) and directory reader for the archive layout
// Nothing here touches the editor, SDL or the scene types: it works on paths and text.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace buildpipe {

// ---- layout ----------------------------------------------------------------------------------------

/// Folder:  <Name>.exe next to <Name>_Data/ (scenes, assets, StreamingAssets, build.json).
/// Compact: the older layout kept for existing projects: the exe with a Data/ folder (or everything beside the exe).
/// Zip:     the Folder layout packed into one <archive>.zip; the folder itself is not left behind.
enum class Layout { Folder = 0, Compact = 1, Zip = 2 };
const char* LayoutName(Layout l);                       ///< "folder", "compact", "zip"
bool ParseLayout(const std::string& text, Layout& out); ///< accepts the names above (any case)

/// A name made safe to use as a file or folder name on Windows and Linux (illegal characters become '_',
/// trailing dots/spaces dropped, reserved device names avoided). `fallback` is used when nothing is left.
std::string FileSafeName(const std::string& name, const std::string& fallback = "Game");

struct NameTokens {
    std::string name, version, platform, date;   // date: "2026-11-12"
    int build = 1;
};
/// Replace {name} {version} {build} {platform} {date} in `pattern`, then make the result file-safe.
/// An empty pattern gives "{name}-{version}". Unknown {tokens} are kept as written.
std::string ExpandPattern(const std::string& pattern, const NameTokens& t);

struct LayoutInput {
    std::string gameName;
    Layout layout = Layout::Folder;
    bool compactDataFolder = true;   ///< Compact only: Data/ subfolder (true) or everything beside the exe
    bool windows = true;             ///< the exe gets ".exe"
    bool antiCheat = false;          ///< anti-cheat verifies Data/manifest.json, so it needs the compact Data/ layout
    bool keepFolderWithZip = false;  ///< also leave the folder next to the zip
    std::string archivePattern;      ///< zip file name (without ".zip"); see ExpandPattern
    NameTokens tokens;               ///< values for the archive pattern
};
struct LayoutPlan {
    Layout requested = Layout::Folder, effective = Layout::Folder;
    std::string safeName;            ///< gameName made file-safe
    std::string exeName;             ///< "<safeName>.exe" / "<safeName>"
    std::string dataDir;             ///< relative to the output folder: "<safeName>_Data", "Data" or "" (beside the exe)
    bool makeZip = false;
    bool keepFolder = true;          ///< false: only the zip stays
    std::string zipName;             ///< "<expanded pattern>.zip" (when makeZip)
    std::vector<std::string> notes;  ///< why the effective layout differs from the requested one
};
LayoutPlan PlanLayout(const LayoutInput& in);

/// Where the player finds its data: <baseDir>/<exeStem>_Data, then the only *_Data folder holding a
/// game.okayconfig, then <baseDir>/Data, else <baseDir> itself. Returns a path ending in '/'.
std::string FindDataDir(const std::string& baseDir, const std::string& exeStem);

// ---- file kinds ------------------------------------------------------------------------------------

enum class FileKind { Executable, Library, Scene, Config, Texture, Audio, Model, Font, Script, Data, Streaming, Other };
constexpr int kFileKindCount = 12;
const char* KindName(FileKind k);                   ///< "Executable", "Scenes", ...
FileKind KindOf(const std::string& relPath);        ///< by extension (and a StreamingAssets/ prefix)
bool IsEditorOnlyFile(const std::string& relPath);  ///< backups, temp files, authoring sources (.psd, .blend ...)

// ---- the dependency walk ---------------------------------------------------------------------------

/// One file the build wants: `ref` is the path exactly as the scene (or another asset) writes it, which is
/// the path the running game will look up; `referrer` names who wrote it (a scene file, or an asset path).
struct AssetRef { std::string ref, referrer; };

struct WalkSettings {
    std::string projectDir;                 ///< the project folder ("" when there is none)
    std::vector<std::string> searchDirs;    ///< more folders a relative reference is tried in
    std::uintmax_t maxScanBytes = 8u << 20; ///< text assets above this are shipped but not read for references
};
struct ShippedAsset {
    std::string ref;     ///< as referenced
    std::string dest;    ///< where it lands, relative to the data folder (clean, '/' separators)
    std::string src;     ///< the file on disk it is copied from
    std::uintmax_t bytes = 0;
    std::string reason;  ///< "used by <referrer>"
};
struct MissingAsset { std::string ref, referrer; };
struct WalkResult {
    std::vector<ShippedAsset> assets;       ///< unique by source file, in discovery order
    std::vector<MissingAsset> missing;      ///< referenced but not found anywhere
    std::vector<std::string> absolute;      ///< references that are absolute paths: copied by file name, so the game will not find them
};

/// Find the file a reference points at: as given, then under the project folder, its Assets folder, the
/// referrer's folder and the extra search folders. "" when nothing matches. Never leaves the machine's
/// files unchecked for "..": a reference that climbs out through ".." is only accepted if it exists as written.
std::string ResolveAssetFile(const std::string& ref, const std::string& referrerDir, const WalkSettings& s);
/// The relative path `ref` is stored under inside the data folder (no drive, no "..", '/' separators);
/// an absolute reference keeps only its file name.
std::string DestinationFor(const std::string& ref);

/// Starting from `roots` (what the built scenes reference), follow text assets (prefabs, materials, clips,
/// dialogue, behavior trees, ...) for the files they use in turn. Each source file is listed once.
WalkResult WalkAssets(const std::vector<AssetRef>& roots, const WalkSettings& s);

/// The visual-script files (.okayvs) a scene's text points at (visualscriptpath lines). A scene saved from the
/// editor embeds those graphs, so they need not ship; a scene copied as a file still needs them.
std::vector<std::string> ScriptRefsInSceneText(const std::string& sceneText);

struct ProjectAsset { std::string rel; std::string src; std::uintmax_t bytes = 0; bool editorOnly = false; bool scene = false; };
/// Every file under <project>/Assets (not hidden folders, not .okaymeta sidecars), sorted by path.
std::vector<ProjectAsset> ListProjectAssets(const std::string& projectDir);
/// Every file under <project>/StreamingAssets as (relative path, source), sorted.
std::vector<std::pair<std::string, std::string>> ListStreamingAssets(const std::string& projectDir);

// ---- the manifest ----------------------------------------------------------------------------------

/// <Name>_Data/build.json
struct BuildManifest {
    std::string game, company, bundleId, version, engineVersion, dateUtc, platform, layout, settingsHash;
    int buildNumber = 1;
    bool development = false, stripped = true, compressed = false;
    std::vector<std::string> scenes;   ///< in build order; the first starts the game
    int assetCount = 0;
    std::uintmax_t totalBytes = 0;
};
std::string ManifestToJson(const BuildManifest& m);
bool ParseManifestJson(const std::string& text, BuildManifest& out, std::string* error = nullptr);
/// A short text for logs and `--build-info`: "Game 1.2.0 (build 7), engine 2.852.0, 2026-11-12, windows, 3 scenes".
std::string ManifestSummary(const BuildManifest& m);
std::uint64_t HashText(const std::string& text);   ///< FNV-1a 64
std::string HashHex(std::uint64_t h);              ///< 16 lower-case hex digits
std::string UtcNowIso();                           ///< "2026-11-12T09:30:00Z"

// ---- the report ------------------------------------------------------------------------------------

struct ReportFile { std::string path; std::uintmax_t bytes = 0; FileKind kind = FileKind::Other; std::string reason; };
struct ExcludedFile { std::string path; std::uintmax_t bytes = 0; std::string reason; };
struct Report {
    bool valid = false, dryRun = false;
    std::vector<ReportFile> files;           ///< sorted by size, largest first, after Finish()
    std::vector<ExcludedFile> excluded;
    std::vector<std::string> warnings;
    std::uintmax_t total = 0, excludedBytes = 0;
    std::array<std::uintmax_t, kFileKindCount> kindBytes{};
    std::array<int, kFileKindCount> kindCount{};
    std::string layout;                      ///< effective layout name
    std::string outputDir, archivePath;
    std::uintmax_t archiveBytes = 0;         ///< size of the zip (0 when none)
    bool archiveCompressed = false;
    double seconds = 0.0;
    int missingAssets = 0;

    void AddFile(const std::string& path, std::uintmax_t bytes, const std::string& reason = std::string());
    void AddExcluded(const std::string& path, std::uintmax_t bytes, const std::string& reason);
    void Warn(const std::string& text) { warnings.push_back(text); }
    /// Sort and total. Call once after the last AddFile.
    void Finish();
    /// (extension, bytes, count) rows, largest first; files without one are "(none)".
    struct TypeRow { std::string ext; std::uintmax_t bytes = 0; int count = 0; };
    std::vector<TypeRow> ByExtension() const;
    /// The plain-text report (what the editor writes to .okay/build_report.txt and the CLI prints).
    /// `maxRows` limits the file and excluded lists (0 = all).
    std::string ToText(std::size_t maxRows = 0) const;
};
std::string HumanBytes(std::uintmax_t b);   ///< "1.5 MB"

/// Project files that were not shipped, with the reason: not reached by the built scenes, a scene that is
/// not in the build, or an editor-only file. `shippedSources` and `sceneSources` are the source paths the
/// build used (any spelling: they are compared by canonical path).
std::vector<ExcludedFile> FindUnshipped(const std::string& projectDir, const std::vector<std::string>& shippedSources,
                                        const std::vector<std::string>& sceneSources);

// ---- zip -------------------------------------------------------------------------------------------

struct ZipStats { int files = 0; std::uintmax_t stored = 0, packed = 0; };
/// Pack every file under `rootDir` into `zipPath` (paths relative to rootDir, '/' separators, sorted, so the
/// same folder gives the same archive). `compress` deflates files that benefit (already-compressed types such
/// as .png .ogg .zip are stored). False with `error` set on failure; a partial file is removed.
bool WriteZip(const std::string& zipPath, const std::string& rootDir, bool compress, ZipStats* stats = nullptr, std::string* error = nullptr);
struct ZipEntry { std::string name; int method = 0; std::uint32_t crc = 0; std::uint64_t packed = 0, size = 0; };
/// The central directory of a zip (what `unzip -l` lists).
bool ReadZipDirectory(const std::string& zipPath, std::vector<ZipEntry>& out, std::string* error = nullptr);

} // namespace buildpipe
} // namespace okay
