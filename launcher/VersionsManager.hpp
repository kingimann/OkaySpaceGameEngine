#pragma once
// The launcher's installed-versions store and everything around it: which versions exist (versions.json), where they
// download from, installing one next to the running version, removing and pruning, keeping the current editor when the
// launcher updates, the Latest / LTS channel, and what a project card and "Open with" need to know.
//
// No SDL, ImGui or network code lives here: downloads go through an injected function, so the tests run on a temp
// folder with local files (tests/test_launcher_versions.cpp). The window is launcher/VersionsUI.cpp; the real download
// and the hand-off file are launcher/VersionsNet.cpp. See docs/launcher_versions.md.
//
//   <launcher dir>/Versions/<version>/Tools/OkayEngine.exe, OkaySpacePlayer.exe, SDL2.dll     a complete copy of one release
//   <launcher dir>/Versions/<version>/install.txt                                              how it got there (kept / installed)
//   <launcher dir>/Versions/.partial-*  and  .removing-*                                       scratch folders, never listed
//
// Rules the code keeps (and the tests check):
//  * A version folder appears whole or not at all: files are downloaded, checked and assembled in a scratch folder next
//    to it, and that folder is renamed into place as the very last step. A failed, cancelled or full-disk install removes
//    its scratch folder and leaves every other version alone.
//  * A download is only ever run (moved into place) after its size and SHA-256 match versions.json. Without a checksum
//    (no index) the size must match the server's and the person must confirm.
//  * Nothing here touches a project folder, and nothing writes outside <launcher dir>/Versions.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "ProjectVersion.hpp"

namespace okayversions {
namespace fs = std::filesystem;

constexpr std::uint64_t kMiB = 1024ull * 1024ull;
constexpr std::uint64_t kMaxIndexBytes = 4 * kMiB;            ///< a bigger versions.json is not read
constexpr std::uint64_t kMaxFileBytes = 4096 * kMiB;          ///< no single release file is this big
constexpr std::size_t kMaxIndexVersions = 500;                ///< entries read from the index
constexpr std::uint64_t kSpaceReserve = 256 * kMiB;           ///< free space that must stay free after a copy ("nearly full")
constexpr int kMaxKeepPrevious = 9;
constexpr int kDefaultKeepPrevious = 1;

// ---- the files of a release ----------------------------------------------------------------------------------------------

/// The three files that make a complete copy of a release's Tools folder.
enum class Asset { Editor, Player, Sdl };
const char* IndexName(Asset a);          ///< the name in versions.json and on the server ("OkayEngine.exe")
std::string DiskName(Asset a);           ///< the name inside Versions/<v>/Tools (the editor uses okayver::EditorExeName())
/// Windows needs SDL2.dll beside every exe; other platforms use the system library.
constexpr bool kRequireSdl =
#if defined(_WIN32)
    true;
#else
    false;
#endif

// ---- versions.json -------------------------------------------------------------------------------------------------------

struct FileInfo {
    std::uint64_t size = 0;
    std::string sha256;                  ///< 64 lowercase hex digits, "" when the index has none
};
struct Entry {
    std::string version;                 ///< normalized ("2.850.1")
    std::string channel;                 ///< "latest", "lts" or ""
    std::string date;
    bool inBucket = false;               ///< downloadable from the releases bucket (otherwise only from GitHub)
    std::map<std::string, FileInfo> files;   ///< by index name
};
struct Index {
    bool ok = false;                     ///< a usable list (schema known, versions read)
    bool fallback = false;               ///< built from VERSION.txt / LTS.txt: no checksums
    std::string error;                   ///< why `ok` is false (plain words)
    std::string note;                    ///< a plain sentence about how complete the list is ("" = nothing to say)
    int schema = 0;
    std::string latest, lts;             ///< the two channel heads, normalized ("" = none)
    std::string archive;                 ///< "https://.../download/v{version}/{file}" ("" = none)
    std::string bucketPath = "v{version}/{file}";
    std::vector<Entry> versions;         ///< newest first
    const Entry* Find(const std::string& version) const;
};

/// Read versions.json. Tolerant of what it does not know (extra fields, an entry that is unusable is skipped), strict about
/// what it relies on: the schema must be exactly 1, versions are canonical, sizes are whole numbers under 4 GiB, checksums
/// are 64 hex digits, template text has no way out of its folder. Never throws; garbage gives `ok = false` and a reason.
Index ParseIndex(std::string_view text);
/// An index made from the two small text files when versions.json is not available: the heads only, no checksums.
/// `archiveTemplate` is the GitHub address pattern ("" = none); `bucketAvailable` says whether the bucket can be used.
Index FallbackIndex(const std::string& latestTxt, const std::string& ltsTxt, const std::string& archiveTemplate, bool bucketAvailable);
/// The version on the first line of VERSION.txt / LTS.txt (digits and dots only), normalized; "" when it is not one.
std::string ParseVersionTxt(std::string_view text);
/// "v{version}/{file}" with the two names replaced.
std::string ExpandTemplate(const std::string& tmpl, const std::string& version, const std::string& file);
/// https only (plain http only for the local test server on 127.0.0.1 / localhost), plain URL characters, no spaces or quotes.
bool IsAcceptableUrl(const std::string& url);
/// A file name that may be joined to a folder: letters, digits, '.', '_' and '-', no dots-only name.
bool IsSafeFileName(const std::string& name);

using FetchTextFn = std::function<bool(const std::string& url, std::string& out)>;   ///< the body of a small text file; false = unreachable
struct Sources {
    std::string bucketBase;                       ///< "https://.../releases/" ("" = no releases bucket is configured)
    std::string archiveTemplate;                  ///< GitHub address pattern ("https://.../download/v{version}/{file}"), "" = none
    std::vector<std::string> versionTxtUrls;      ///< other places a VERSION.txt lives (GitHub); the bucket's own is added
};
/// Find out which versions exist: <bucket>/versions.json; its channel heads are then checked against <bucket>/VERSION.txt and
/// LTS.txt (the index is written last and can lag: a newer head there is added, without checksums). When there is no usable
/// index the heads of VERSION.txt (every place) and LTS.txt make a FallbackIndex, with `note` saying why. `ok` is false only when
/// nothing at all could be read.
Index LoadIndex(const Sources& sources, const FetchTextFn& fetch);

/// The version a launch with `--versions <requested>` should highlight: the exact version when the index lists it, else the
/// newest listed version of the same line (the LTS head when that is of the line), else "".
std::string PickHighlight(const Index& index, const std::string& requested);

// ---- where to download from, and what to check ---------------------------------------------------------------------------

struct PlanFile {
    Asset asset = Asset::Editor;
    std::string indexName, diskName;
    std::vector<std::string> urls;       ///< tried in order (the bucket first, then GitHub)
    std::uint64_t size = 0;              ///< expected size; 0 = not known yet (see FillUnknownSizes)
    std::string sha256;                  ///< expected checksum; "" = the index has none
    bool verified = false;               ///< the checksum is known (or the file is the launcher's own SDL2.dll)
    fs::path localCopy;                  ///< when set: copied from here instead of downloaded (the launcher's own SDL2.dll)
};
struct InstallPlan {
    bool ok = false;
    std::string error;
    std::string version;
    std::vector<PlanFile> files;
    bool needsConfirmation = false;      ///< some file has no checksum: the person must agree
    std::uint64_t totalBytes() const;    ///< the known sizes added up
};
/// Work out what installing `version` takes. `bucketBase` is "https://.../releases/" ("" = no bucket); `localSdl` the
/// launcher's own SDL2.dll, used when the index lists none. A version that is not in the index but is one of its heads
/// is planned without checksums. Errors (no such version, nowhere to download from) come back as `ok = false`.
InstallPlan PlanInstall(const Index& index, const std::string& version, const std::string& bucketBase, const fs::path& localSdl,
                        bool requireSdl = kRequireSdl);
/// For every downloaded file without a size, ask the server (`headSize(url)`, 0 = unknown). Returns false when an
/// unverified file's size could not be found out (such a file will be refused).
bool FillUnknownSizes(InstallPlan& plan, const std::function<std::uint64_t(const std::string& url)>& headSize);

// ---- doing it --------------------------------------------------------------------------------------------------------------

enum class DownloadStatus { Ok, Failed, Cancelled, DiskFull };
struct DownloadResult {
    DownloadStatus status = DownloadStatus::Failed;
    std::string error;
};
/// Download `url` to `dest` (a file in a scratch folder). `onBytes` is told how many bytes are there so far; `cancelled()`
/// is polled and a true answer must stop the download soon (status Cancelled; the partial file may stay, the caller removes
/// it). `maxBytes` is a hard limit (a bigger body is a failure).
using DownloadFn = std::function<DownloadResult(const std::string& url, const fs::path& dest, std::uint64_t maxBytes,
                                                const std::function<void(std::uint64_t)>& onBytes,
                                                const std::function<bool()>& cancelled)>;

struct Progress {
    std::string file;                    ///< the file being fetched
    int fileIndex = 0, fileCount = 0;
    std::uint64_t fileBytes = 0, fileTotal = 0;      ///< this file (total 0 = unknown)
    std::uint64_t doneBytes = 0, totalBytes = 0;     ///< all files so far
};
struct IO {
    DownloadFn download;
    std::function<bool()> cancelled;                                   ///< polled between and during downloads
    std::function<void(const Progress&)> progress;
    std::function<std::uintmax_t(const fs::path&, std::error_code&)> availableSpace;   ///< default: fs::space
    std::function<bool(const std::string& step)> fail;                 ///< tests only: true = this step fails (as a full disk would)
};
struct InstallOptions {
    bool confirmedUnverified = false;    ///< the person agreed to files that have no checksum
    bool markKept = false;               ///< install.txt says "kept" (used by KeepCurrent) rather than "installed"
};

enum class Outcome { Installed, AlreadyInstalled, Removed, Cancelled, NeedsConfirmation, NotEnoughSpace, Failed, Refused, Skipped };
const char* OutcomeName(Outcome o);
struct Result {
    Outcome outcome = Outcome::Failed;
    std::string message;                 ///< plain words for the person (and the toast)
    fs::path path;                       ///< Versions/<v> when installed
    bool ok() const { return outcome == Outcome::Installed || outcome == Outcome::AlreadyInstalled || outcome == Outcome::Removed; }
};

/// Install the planned version into <launcherDir>/Versions/<version>/Tools, whole or not at all (see the rules above).
Result Install(const fs::path& launcherDir, const InstallPlan& plan, const InstallOptions& options, const IO& io);
/// The size and SHA-256 of `path` against what is expected (a 0 size or "" checksum is not checked). "" = fine, else why not.
std::string VerifyFile(const fs::path& path, std::uint64_t size, const std::string& sha256);

// ---- the store -------------------------------------------------------------------------------------------------------------

struct Installed {
    std::string version;                 ///< normalized
    fs::path dir;                        ///< <launcher dir>/Versions/<version>
    std::uint64_t bytes = 0;
    std::string source;                  ///< "kept" (copied when the launcher updated), "installed" (by the person) or "" (put there by hand)
    std::string date;
};
/// Every version with an editor under Versions/, newest first, with its size and where it came from.
std::vector<Installed> ListInstalled(const fs::path& launcherDir);
/// Bytes under <launcher dir>/Versions (scratch folders included).
std::uint64_t DiskUsage(const fs::path& launcherDir);
/// The sum of every file under `dir` (0 when it is not there); stops counting after 200000 entries.
std::uint64_t DirectorySize(const fs::path& dir);
/// Remove scratch folders (.partial-*, .removing-*) left by a crash or a failed delete. Returns how many were removed.
int CleanStale(const fs::path& launcherDir);

struct RemoveGuards {
    std::string running;                 ///< the version of the editor/launcher in use (the one in Tools/): never removed
    std::string launcher;                ///< the version this launcher was built as: never removed
    std::vector<std::pair<std::string, std::string>> keep;   ///< other versions that must stay, with the plain reason ("it is your default editor")
    std::function<bool(const std::string& version)> inUse;   ///< true when an editor of that version is open
};
/// Remove one installed version (the confirmation is the caller's). The folder is renamed away first, so the version
/// disappears at once or not at all; what could not be deleted after that is a scratch folder CleanStale retries.
Result Remove(const fs::path& launcherDir, const std::string& version, const RemoveGuards& guards, const IO& io = {});

// ---- keeping the current editor when the launcher updates -----------------------------------------------------------------

/// Where the running install's files are (Tools/ first, then beside the launcher).
struct CurrentInstall {
    fs::path editor, player, sdl;
};
CurrentInstall FindCurrentInstall(const fs::path& launcherDir);
/// Copy the current Tools files into Versions/<currentVersion>/Tools (all or nothing) before an update replaces them.
/// Skipped, with a reason, when: keepPrevious is 0, the version is not a version (a development build), it is already
/// there, the install is incomplete, or free space would drop under kSpaceReserve. A failure never blocks the update.
/// `Outcome::Installed` means a new copy was made.
Result KeepCurrent(const fs::path& launcherDir, const std::string& currentVersion, const CurrentInstall& cur, int keepPrevious,
                   bool requireSdl, const IO& io = {});

/// Which of the installed versions to remove so only `keepPrevious` automatically kept copies remain. Copies the person
/// installed (or put there by hand) are never pruned; neither is the newest installed version of the LTS line (`ltsVersion`
/// may be a version or a line), nor anything in `keep`. Newest first.
std::vector<std::string> PlanPrune(const std::vector<Installed>& installed, int keepPrevious, const std::string& ltsVersion,
                                   const std::vector<std::string>& keep);
struct PruneResult {
    std::vector<std::string> removed, failed;
};
PruneResult Prune(const fs::path& launcherDir, int keepPrevious, const std::string& ltsVersion, const RemoveGuards& guards, const IO& io = {});
/// Delete a version that KeepCurrent just made, without the guards (the update it was made for failed, so the copy only
/// duplicates the running install).
void DiscardKept(const fs::path& launcherDir, const std::string& version);

// ---- the channel -----------------------------------------------------------------------------------------------------------

enum class Channel { Latest, Lts };
const char* ChannelName(Channel c);                  ///< "latest" / "lts" (what launcher.cfg stores)
Channel ParseChannel(std::string_view text);         ///< anything but "lts" is Latest
struct ChannelDecision {
    enum Kind {
        None,                 ///< nothing to do (up to date, or the long-term-support copy is already installed)
        UpdateInPlace,        ///< the launcher updates itself to `version` (a normal update)
        InstallSideBySide,    ///< the launcher is newer than the LTS line: install `version` next to it (never a downgrade)
        UpdateSideBySide,     ///< a newer patch of the installed LTS copy
        NoInfo,               ///< the channel's head is not known (offline, or no LTS has been promoted yet)
    } kind = None;
    std::string version;
    std::string note;         ///< one plain sentence
};
/// What the update check should do. Latest: update when the head is newer (today's behaviour). LTS: only the LTS line's
/// patches are offered; a launcher that is newer than that line is never changed in place, the LTS editor is installed
/// beside it instead. `installed` are the versions under Versions/.
ChannelDecision DecideChannel(Channel channel, const std::string& local, const std::string& latestHead, const std::string& ltsHead,
                              const std::vector<std::string>& installed);

struct Settings {
    Channel channel = Channel::Latest;
    int keepPrevious = kDefaultKeepPrevious;
    std::string defaultEditor;           ///< a version under Versions/ used for projects with no stamp ("" = this launcher's editor)
    std::string ltsLine;                 ///< the LTS line last seen ("2.850"): lets pruning protect it while offline
};
Settings& GlobalSettings();
/// launcher.cfg lines: update_channel, keep_previous, default_editor, lts_line. True when the key was one of ours.
bool ApplySettingLine(Settings& s, const std::string& key, const std::string& value);
void WriteSettings(const Settings& s, std::ostream& out);

// ---- projects and "Open with" ----------------------------------------------------------------------------------------------

struct ProjectBadge {
    std::string version;       ///< "v2.850.1", or "unknown" when the project has no version recorded
    std::string text;          ///< "older: v2.850.x", "newer" or "" (the same line, or nothing to compare)
    enum Tone { None, Older, Newer } tone = None;
    std::string tip;           ///< a plain sentence for the tooltip ("" when there is no badge)
};
/// What a project card says. `currentVersion` is the editor that a plain Open starts.
ProjectBadge BadgeFor(const std::string& projectVersion, const std::string& currentVersion);

struct EditorChoice {
    std::string version;
    bool sameLine = false;     ///< of the project's line
};
/// The installed editors a project can be opened with, the project's line first, each group newest first. The editor that
/// a plain Open starts (`currentVersion`) is not repeated.
std::vector<EditorChoice> OpenWithChoices(const std::string& projectVersion, const std::vector<std::string>& installed,
                                          const std::string& currentVersion);
/// The arguments (after the program) that open `scene` with a kept editor: --launcher <pid> --open <scene> --keep-version.
std::vector<std::string> OpenWithArgs(unsigned long launcherPid, const std::string& scene);
/// One argument as a command line wants it (spaces, quotes, `$`, unicode survive): the Windows form (MSVCRT rules, double
/// quotes) and the POSIX form (single quotes). QuoteArg is the one of the platform being built for.
std::string QuoteArgWindows(const std::string& arg);
std::string QuoteArgPosix(const std::string& arg);
std::string QuoteArg(const std::string& arg);
std::string JoinArgs(const std::vector<std::string>& args);
/// What a plain Open starts: this launcher's editor, or the default editor (a version) for a project with no stamp.
struct OpenTarget {
    std::string version;       ///< "" = the current editor, otherwise an installed version under Versions/
    bool keepFlag = false;     ///< pass --keep-version
};
OpenTarget ChooseEditorForOpen(const std::string& projectVersion, const std::string& currentVersion,
                               const std::vector<std::string>& installed, const std::string& defaultEditor);
/// Does the editor that is open have to close before `targetVersion` can start? Editors share one single-instance lock, so a
/// second one only hands its project to the first. `runningVersion` is the version of the open editor ("" = not known: taken
/// to be `currentVersion`).
bool MustCloseEditorFirst(bool editorRunning, const std::string& runningVersion, const std::string& currentVersion,
                          const std::string& targetVersion);

// ---- the launcher's own command line and hand-off --------------------------------------------------------------------------

struct LauncherArgs {
    bool versions = false;     ///< --versions [<version>]: open the Versions page
    std::string version;       ///< the version to highlight (normalized), "" = none
};
LauncherArgs ParseLauncherArgs(const std::vector<std::string>& args);
/// A second launcher cannot open a page in the first, so it leaves a note in this file and exits; the first one polls it.
fs::path HandoffFile();
bool WriteHandoff(const fs::path& file, const LauncherArgs& args, long long nowSeconds);
/// Read and delete the note. Notes older than two minutes (a crash left them) are deleted and ignored.
bool TakeHandoff(const fs::path& file, long long nowSeconds, LauncherArgs* out);

// ---- words -----------------------------------------------------------------------------------------------------------------

std::string HumanSize(std::uint64_t bytes);              ///< "41.2 MB", "2.1 GB", "512 KB"
/// "v2.850.x is the long-term-support version: it only gets fixes."
std::string LtsSentence(const std::string& version);

}  // namespace okayversions
