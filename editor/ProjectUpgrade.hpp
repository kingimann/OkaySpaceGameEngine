#pragma once
// Upgrading a project to the version of the editor that opens it, and the question that comes first. No ImGui and no
// SDL in here: the editor draws the windows (ProjectUpgradeUI.cpp) and the tests drive the same objects.
//
//   ProjectGate      the decision before a project is opened: Same / DevBuild open silently; Older, Unknown and Newer
//                    ask. Nothing in the project folder is read for writing, let alone written, until it is answered.
//   UpgradeJob       backup, check and rewrite, stamp: stepped a little each frame so the window stays alive, and all
//                    or nothing (see docs/project_upgrade.md)
//   ListBackups / RestoreBackup   the sibling "<Name> (backup v...)" folders, and putting the upgraded files back
//
// Nothing here prints or throws: failures come back as text for the UI to show. Main thread only: the check loads
// every scene through the engine's loader, which is not for worker threads.
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "../shared/ProjectVersion.hpp"

namespace okayup {
namespace fs = std::filesystem;

// ---- which folder is "the project" ------------------------------------------------------------------------------------

/// True when `dir` is a project folder: it has an Assets folder or a project.okayproj file in it. (A .okay folder alone
/// does not count: the editor leaves one next to any scene it opened, project or not.)
bool IsProjectRoot(const fs::path& dir);

/// What ProjectRootForScene needs from the machine, so the rules can be tried anywhere (tests pass their own).
struct RootRules {
    bool assetsAnyCase = false;   ///< an "Assets" folder the scene is inside may be spelled in any case (Windows)
    fs::path workingFolder;       ///< the editor's working folder: its project.okayproj is the old shared settings file
    fs::path home;                ///< the person's home folder: never a project
    fs::path ceiling;             ///< tests: the search goes no higher than this folder ("" = up to the drive root)
};
/// This machine's rules: any case on Windows, the current folder, HOME (USERPROFILE on Windows).
RootRules DefaultRootRules();
/// The project folder a scene belongs to. From the scene's folder upwards, the nearest folder that is clearly a project:
///   - it holds the scene through a folder called Assets (any case when `assetsAnyCase`): <project>/Assets/.../scene;
///   - or it holds a project.okayproj with an engine_version stamp (not the working folder: its file is the old shared one,
///     which a release build stamps too).
/// Only when no folder up the path is clearly a project: the scene's own folder when it holds project.okayproj or an Assets
/// folder (the old flat layout; not the working folder). A drive root and the home folder are never a project. Not evidence:
/// an unstamped project.okayproj above the scene (a copy of the shared file) and an Assets folder beside a folder above the
/// scene (the release folder holds Assets/ next to examples/). "" for a loose file.
std::string ProjectRootForScene(const std::string& scene, const RootRules& rules);
std::string ProjectRootForScene(const std::string& scene);   ///< with DefaultRootRules()
/// The project File > Project Upgrade works on: the project of `scene`, else `projectDir` when that is a project folder
/// (no scene saved yet). The window shows this folder and its Upgrade button upgrades this folder.
std::string UpgradeRootFor(const std::string& scene, const std::string& projectDir, const RootRules& rules);
/// The folder whose project.okayproj holds the settings of the project open in `projectDir`: `projectDir` when it is a project
/// folder (it has project.okayproj or an Assets folder), else "" (the editor's shared file). So the old shared file is only
/// ever copied into a real project, never into a scene's folder.
std::string SettingsFolderFor(const std::string& projectDir);
/// True for the root of a drive or a network share ("C:\", "/", "\\server\share").
bool IsDriveRoot(const fs::path& dir);
/// `a` and `b` are the same folder on disk, however each is spelled (a link, 8.3 name, another case on Windows, a trailing
/// separator). False when either is missing.
bool SameFolder(const fs::path& a, const fs::path& b);
/// A folder as a map key: absolute, normalized, no trailing separator, lower case on Windows.
std::string RootKey(const std::string& root);

// ---- the stamp in project.okayproj, as the editor writes it ---------------------------------------------------------------

/// True for a key project.okayproj's writer (ProjectState.hpp's WriteProjectSettings and ProjectLists) owns; every other
/// line of the file is somebody else's (a newer editor's setting, a note) and is carried over when the file is saved.
bool IsKnownProjectKey(const std::string& key);
/// The text to save as `path` (a project.okayproj): `generated`, the stamp for `running` (none for a development build)
/// and the lines of the file now on disk that the writer does not own.
std::string ProjectFileText(const std::string& path, const std::string& generated, const std::string& running);
/// Stamp the project's project.okayproj with `running` after a scene was saved: only the one line changes. Does nothing
/// for a development build or a project with no project.okayproj yet. False (with `error`) when the file can't be written.
bool StampProject(const std::string& projectDir, const std::string& running, std::string* error = nullptr);

// ---- the upgrade ----------------------------------------------------------------------------------------------------------

/// A hook for tests: called before each step that touches the disk with a stage name and what it is about; returning true
/// makes that step fail as if the disk had refused. Stages: scan, analyze, space, backup-copy, backup-verify,
/// backup-manifest, backup-rename, apply, stamp, restore.
using FailHook = std::function<bool(const char* stage, const std::string& what)>;

struct Options {
    std::string projectDir;
    std::string fromVersion;          ///< the version the project was written by ("" = unknown)
    std::string toVersion;            ///< the running editor's version
    bool migrate = true;              ///< false: only make the backup (a newer project opened as it is)
    std::string backupParent;         ///< where the backup folder goes (default: the folder the project is in)
    std::string legacySettings;       ///< text a project with no project.okayproj of its own starts from (the old shared file)
    std::vector<std::string> skipDirs;   ///< absolute folders to leave out of the backup (build output)
    std::uintmax_t spaceMargin = 16u * 1024u * 1024u;   ///< free space wanted beyond the copy itself
    /// Above this many files or bytes the folder is too big to be copied without asking: the scan stops early and the result
    /// says `tooLarge` (nothing made). A folder taken for a project by mistake (Documents, a drive) is caught here.
    std::uint64_t largeFiles = 50000, largeBytes = 8ull * 1024 * 1024 * 1024;
    bool allowLarge = false;          ///< the person said "copy it anyway"
    std::function<std::uintmax_t(const fs::path&, std::error_code&)> availableSpace;   ///< default: fs::space
    FailHook fail;
};

struct Result {
    bool ok = false;
    bool cancelled = false;
    std::string error;                ///< the first thing that went wrong, in plain words
    std::string restoreNote;          ///< what was done about it: "The project is exactly as it was." and the like
    bool rolledBack = false;          ///< every file the upgrade had already rewritten was put back
    bool tooLarge = false;            ///< stopped at the scan: more than Options::largeFiles / largeBytes (nothing was made)
    std::string backupDir;            ///< the finished backup ("" if none was made)
    int scenes = 0, prefabs = 0;      ///< checked
    int rewritten = 0;                ///< scene / prefab files that had to change
    std::uint64_t files = 0, bytes = 0;   ///< what the backup holds
    std::vector<std::string> changed; ///< project-relative paths written (scenes, prefabs, project.okayproj)
    std::vector<std::string> log;     ///< what the migration did, one line per step
    std::vector<std::string> warnings;
};

enum class Phase { Scanning, Checking, Space, Backup, Verify, Applying, Done, Failed };
const char* PhaseName(Phase p);

struct Progress {
    Phase phase = Phase::Scanning;
    std::uint64_t done = 0, total = 0;   ///< of the current phase (files; the scan has no total)
    double fraction = 0.0;               ///< of the whole job, 0..1
    std::string file;                    ///< the file being worked on
};

class UpgradeJob {
public:
    explicit UpgradeJob(Options opt);
    ~UpgradeJob();
    UpgradeJob(const UpgradeJob&) = delete;
    UpgradeJob& operator=(const UpgradeJob&) = delete;

    /// Work for about `budget`, then return. False once the job is finished (Done or Failed).
    bool Step(std::chrono::milliseconds budget);
    /// Run to the end (tests, tools).
    void Run();
    /// Stop at the next step. Only before the first file is rewritten: after that the upgrade finishes (it is short).
    void Cancel();
    bool Finished() const;
    Progress progress() const;
    const Result& result() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

/// "<Name> (backup v2.850.0)", or "<Name> (backup before v2.855.0)" when the old version is not known.
std::string BackupFolderName(const std::string& projectName, const std::string& fromVersion, const std::string& toVersion);

struct BackupInfo {
    fs::path dir;
    std::string name;                 ///< the folder's name
    std::string fromVersion, toVersion, made, mode;
    std::uint64_t files = 0, bytes = 0;
    std::vector<std::string> changed, created;   ///< project-relative: files the upgrade rewrote, files it made
    bool restorable = false;          ///< it has the manifest this editor writes
};
/// The backups of the project in `projectDir`: sibling folders named "<Name> (backup ...)", newest name last.
std::vector<BackupInfo> ListBackups(const std::string& projectDir);

struct RestoreResult {
    bool ok = false;
    std::string error;
    std::string note;
    int restored = 0, removed = 0;
    std::string sideCopyDir;          ///< where the files that were replaced went: <project>/.okay/backups/restore-N
};
/// Put the files the upgrade rewrote back from `backup`, and take away the ones it made. Everything about to be replaced
/// is first copied to <project>/.okay/backups/restore-N, and a failure puts those back, so a restore is all or nothing too.
RestoreResult RestoreBackup(const BackupInfo& backup, const std::string& projectDir, const FailHook& fail = {});

// ---- the question ---------------------------------------------------------------------------------------------------------

enum class PromptKind { None, Older, Unknown, Newer, NotInstalled, Working, Done, Failed, Large };
enum class Choice { Upgrade, Keep, Cancel, OpenAnyway, UpdateEditor, Back, Continue, Close };

struct Prompt {
    PromptKind kind = PromptKind::None;
    std::string root, scene, projectName;
    std::string projectVersion;       ///< normalized; "" when unknown
    std::string runningVersion;
    std::string keepTarget;           ///< Older: the installed editor "Keep" would start (the newest of the project's line), "" if none
    bool backupOnly = false;          ///< Working / Done / Failed: it is the backup before opening a newer project
    Progress progress;                ///< Working
    Result result;                    ///< Done / Failed / Large (how big the folder is, roughly: what was counted before stopping)
    std::string message;              ///< NotInstalled: the sentence to show
};

/// What the gate needs from the outside; tests fill in their own.
struct GateEnv {
    std::string running;                                          ///< this editor's version ("dev" for a development build)
    std::function<fs::path()> launcherDir;                        ///< where Versions/ is looked for ("" = no launcher around)
    std::vector<std::string> forwardArgs;                         ///< arguments passed on to a kept editor ("--launcher", "<pid>")
    std::function<std::string()> legacySettings;                  ///< the old shared project.okayproj, for a project without one
    std::function<std::vector<std::string>(const std::string& projectDir)> outputDirs;   ///< Build Settings output folders
    FailHook fail;                                                ///< tests only
    std::function<std::uintmax_t(const fs::path&, std::error_code&)> availableSpace;   ///< tests only
    std::optional<RootRules> rootRules;                           ///< tests only: how a scene's project is found (default: DefaultRootRules())
    std::uint64_t largeFiles = 0, largeBytes = 0;                 ///< tests only: the "too big to copy without asking" limits (0 = Options' default)
};

/// An editor of another version to start in place of this one (the "Keep" choice).
struct Launch {
    std::string exe;
    std::vector<std::string> args;
    std::string workDir;
};

class ProjectGate {
public:
    explicit ProjectGate(GateEnv env = {});

    /// The editor wants to open `scene`. `proceed` runs when the project may be opened: at once when there is nothing to
    /// ask, otherwise after the person's answer. `cancel` runs when it may not. A scene that is not part of a project, and a
    /// project that was already opened this session, never ask. One question at a time: a request that comes while a window
    /// is open (the launcher handing over another project) is held and handled when the window closes; a newer held request
    /// replaces an older one (whose `cancel` runs).
    void Open(const std::string& scene, std::function<void()> proceed, std::function<void()> cancel = {});
    /// File > Project Upgrade...: the person asks for the upgrade of UpgradeRootFor(scene, projectDir) (an open project, so
    /// there is no question): the backup and upgrade start at once, and `done` runs after the result window is dismissed with
    /// Continue. Does nothing while a window is open.
    void Upgrade(const std::string& scene, std::function<void()> done = {}, const std::string& projectDir = {});
    /// The project a scene belongs to, by this gate's rules (ProjectRootForScene).
    std::string RootOf(const std::string& scene) const;
    /// What Upgrade(scene, ..., projectDir) works on (UpgradeRootFor with this gate's rules).
    std::string UpgradeRootOf(const std::string& scene, const std::string& projectDir) const;
    /// True while an Open is held, waiting for the window on screen to close.
    bool HasHeldOpen() const { return held_.has_value(); }
    /// True while a question, the upgrade or its result is on screen.
    bool Busy() const { return prompt_.kind != PromptKind::None; }
    const Prompt& prompt() const { return prompt_; }
    /// A button of the current window.
    void Answer(Choice c);
    /// Once per frame: runs the upgrade for about `budget`.
    void Tick(std::chrono::milliseconds budget = std::chrono::milliseconds(8));

    /// A project that is open (made in this session, upgraded, or answered): it is not asked about again.
    void MarkCleared(const std::string& root);
    bool IsCleared(const std::string& root) const;
    /// `--keep-version`: the person chose this editor's version for the project that `scene` belongs to. `forVersion` is
    /// the version the flag was given for ("" = the version of whoever reads it); a flag for another version is ignored.
    /// An older or unversioned project is then opened without a question, for the rest of the session; a newer one still asks.
    void NoteKeepRequest(const std::string& scene, const std::string& forVersion = {});
    bool IsKept(const std::string& root) const;
    /// The safety net under EditorState::Load: may this scene be opened now? Same-version and development projects, and
    /// ones already answered, yes (and are remembered); everything else no, with the reason.
    bool MayLoad(const std::string& scene, std::string* why);

    /// What the answers asked the editor to do next (each is handed out once).
    bool TakeQuitRequest() { const bool r = quit_; quit_ = false; return r; }
    bool TakeUpdateRequest() { const bool r = update_; update_ = false; return r; }
    std::optional<Launch> TakeLaunch() { auto r = std::move(launch_); launch_.reset(); return r; }

    /// Changes whenever the cleared or kept projects change (so a label made from them can be cached).
    unsigned revision() const { return revision_; }

    const GateEnv& env() const { return env_; }
    void SetEnv(GateEnv env) { env_ = std::move(env); }

private:
    void Ask(PromptKind k, const std::string& root, const std::string& scene, const std::string& version);
    void StartJob(bool migrate, bool allowLarge = false);
    void RunHeld();
    void Finish(bool proceed);
    void KeepVersion();

    GateEnv env_;
    Prompt prompt_;
    std::function<void()> proceed_, cancel_;
    struct Held { std::string scene; std::function<void()> proceed, cancel; };
    std::optional<Held> held_;
    std::unique_ptr<UpgradeJob> job_;
    std::set<std::string> cleared_, kept_;
    bool quit_ = false, update_ = false;
    unsigned revision_ = 0;
    std::optional<Launch> launch_;
};

}  // namespace okayup
