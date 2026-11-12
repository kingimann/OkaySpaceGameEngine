#pragma once
// The Project panel's logic, with no window code in it (so tests can run it without ImGui):
//   * a folder model read from the asset database (no disk walk of its own): list a folder or search the whole
//     project by name, type, label, "used by" and "depends on", filter by type, sort (natural order),
//   * asset info (type, size, GUID, dependency counts, broken references) and thumbnails from the database's
//     ThumbnailCache, made lazily within a per-frame time budget,
//   * safe file operations inside the Assets folder: create, rename, move, copy, duplicate, delete to a
//     trash folder, and undo for each. Every operation either completes or puts everything back,
//   * rename and move keep references: a scene, prefab, material or other text asset that points at the
//     moved file (or at a file inside a moved folder) is rewritten, the .okaymeta (GUID) moves with the file,
//   * "what uses this asset" for the delete confirmation and Find References,
//   * the copy / cut / paste clipboard.
// It sits on okay::AssetDatabase (GUIDs, dependency graph). See docs/project_panel.md.
// Failure injection: SetFailureHook makes any file step fail, so tests can check nothing is lost.
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/ThumbnailCache.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace okaypb {

// ---- names and paths --------------------------------------------------------------------------

/// "" when `name` is fine for a file or folder name; otherwise a short plain-English reason.
std::string ValidateName(const std::string& name);
/// True when `path` is `root` or inside it (lexical, no symlink escape). Both may be relative or absolute.
bool IsInside(const std::string& root, const std::string& path);
/// "Name", "Name 2", "Name 3"... that does not exist in `dir`. `ext` includes the dot (or is empty).
std::string UniqueName(const std::string& dir, const std::string& stem, const std::string& ext, const char* sep = " ");
/// Case-insensitive, digits compared as numbers ("tex2" before "tex10"). Strict weak order.
bool NaturalLess(const std::string& a, const std::string& b);
/// Forward-slash form of a path.
std::string Slash(const std::string& path);

// ---- the folder model -------------------------------------------------------------------------

enum class Filter { All, Scripts, Images, Scenes, Materials, Prefabs, Data, Audio, Models, Folders };
enum class SortKey { Name, Type, Size, Modified };

/// Extension (with dot, lower case) belongs to the type filter. Filter::All and Folders: true.
bool FilterMatches(Filter f, const std::string& extension);
/// Plain-English type name for an extension or folder ("Texture", "Scene", "Folder", ...).
std::string TypeLabel(const std::string& extension, bool isDir);
/// A short human size ("12 KB").
std::string HumanSize(std::uint64_t bytes);

struct Entry {
    std::string path;          ///< absolute, '/' separators
    std::string name;          ///< file name with extension
    std::string ext;           ///< lower case with dot, "" for folders
    bool isDir = false;
    std::uint64_t size = 0;
    std::int64_t modified = 0; ///< seconds since the epoch (file clock), 0 when unknown
    okay::Guid guid;           ///< the database's GUID (null for folders)
    okay::AssetType kind = okay::AssetType::Other;
    bool hasMeta = false;      ///< a .okaymeta file exists (the GUID is kept on disk); false: the GUID lasts for this session
    int missingRefs = 0;       ///< references from this asset to files that do not exist (the warning badge)
};

/// What the search box means: plain words (each must be in the name, a label, or be the asset's type word) plus
/// t:model, ext:png, l:label, is:unused, is:broken, uses:<asset> (assets that use it) and deps:<asset> (assets it uses).
struct Query {
    std::vector<std::string> words;     // lower case
    Filter filter = Filter::All;        // from "t:<type>"
    bool foldersOnly = false;           // "t:folder"
    std::string ext;                    // ".png" from "ext:png"
    std::vector<std::string> labels;    // from "l:red": every one must be a label of the asset (database labels)
    bool unused = false;                // "is:unused": nothing references the asset
    bool broken = false;                // "is:broken": it references a file that is missing
    std::string usedBy;                 // "uses:hero.png": the assets that use hero.png (a name or a project path)
    std::string dependsOn;              // "deps:main.okayscene": the assets main.okayscene uses
};
Query ParseQuery(const std::string& text);

struct ListOptions {
    std::string text;                   ///< the search box
    Filter filter = Filter::All;        ///< the type chips (combined with the text)
    SortKey sort = SortKey::Name;
    bool descending = false;
    bool recursive = false;             ///< search below the folder (true when searching in this folder or the whole project)
    std::size_t limit = 4000;           ///< stop after this many entries
    std::int64_t modifiedWithinSeconds = 0;   ///< "recently changed" (0 = any)
    std::int64_t now = 0;               ///< the clock for the line above (0 = now), so tests are exact
    bool unusedOnly = false;            ///< assets nothing references (the Unused smart filter)
};
/// Sort in place the way Browser::List does (folders first).
void SortEntries(std::vector<Entry>& entries, SortKey key, bool descending);
/// True for names the panel never shows (starting with ".", .okaymeta, .tmp, .part, .bak, .swp).
bool IsHiddenName(const std::string& fileName);

// ---- references -------------------------------------------------------------------------------

/// Old absolute path -> new absolute path. A folder entry moves everything below it too.
using PathMap = std::vector<std::pair<std::string, std::string>>;

/// Rewrites the asset paths in one text asset so they follow the moves in `map`. `referrerOld` is where
/// the file was, `referrerNew` where it will be. A reference keeps its style: absolute stays absolute,
/// "Assets/x.png" stays project-relative, "../x.png" stays relative to the file. Returns the new text;
/// `count` gets how many references changed. `assetsRoot` is the Assets folder.
std::string RewriteReferences(const std::string& text, const std::string& referrerOld, const std::string& referrerNew,
                              const PathMap& map, const std::string& assetsRoot, int* count = nullptr);

struct Referrer {
    std::string path;          ///< absolute path of the asset that uses the target
    std::string target;        ///< absolute path of the asset (inside the set) it uses
};

// ---- operations -------------------------------------------------------------------------------

struct Result {
    bool ok = false;
    std::string error;                 ///< plain English, set when !ok
    std::vector<std::string> paths;    ///< the new paths (created, renamed, moved, copied) in order
    PathMap moved;                     ///< old -> new for rename and move (editor: update labels, the open scene)
    int referencesRewritten = 0;
    int filesRewritten = 0;
    std::string note;                  ///< something to tell the user even on success ("2 files were skipped ...")
};

class Browser {
public:
    /// `assetsRoot` is the project's Assets folder (made if missing is NOT done: it must exist).
    explicit Browser(std::string assetsRoot);
    ~Browser();
    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;

    const std::string& Root() const { return m_root; }
    std::string ProjectDir() const;
    okay::AssetDatabase& Db() { return m_db; }

    /// Index (or re-index) the assets: GUIDs, the dependency graph. Nothing is written (an asset gets a .okaymeta only
    /// when it is given labels or settings). Incremental: files whose size and time did not change are not read again.
    /// Returns false when the folder cannot be scanned.
    bool Sync();
    /// Call every frame with a millisecond clock. Re-indexes when due: the next poll waits at least 2 seconds
    /// and 30 times as long as the last scan took, so polling stays a few percent of one core at worst. Does
    /// nothing until the first Sync (the first scan can take a while on a big project and runs only when an
    /// operation or the info pane needs the database). Returns true when the scan found changes.
    bool Poll(std::uint64_t nowMs);
    bool Indexed() const { return m_indexed; }
    /// How many assets use this one (0 when none), straight from the database's graph. Cheap: no scan.
    int UsersOf(const std::string& path);
    /// Goes up whenever a scan found a change (a file or folder created, changed, renamed, moved or deleted) and
    /// after every operation of this class. A listing that was made at the same revision is still right.
    std::uint64_t Revision() const { return m_revision; }
    /// How long the last scan took, in milliseconds (the cost of a refresh), and how many assets it indexed.
    double LastScanMs() const { return m_scanMs; }
    int AssetCount() const { return static_cast<int>(m_db.Count()); }

    /// The folder's contents (or, with a search or `recursive`, everything below it) as the database sees them:
    /// no disk walk. `dir` is absolute and inside the Assets folder. Folders first, then the sort order.
    std::vector<Entry> List(const std::string& dir, const ListOptions& options);
    /// Make the meta file (the asset's GUID) for one asset if it has none, and return its GUID. Opening a project
    /// writes no metas: they appear only for assets that are given settings or labels. The GUID the asset already
    /// had in this session is the one written.
    okay::Guid EnsureMeta(const std::string& path);

    /// The folders directly inside `absDir` (from the database's folder list, no disk walk), sorted by name, with how
    /// many files and folders each holds.
    struct FolderRow { std::string path; bool hasKids = false; int items = 0; };
    std::vector<FolderRow> SubFolders(const std::string& absDir);

    /// Counts for the smart filters: assets per type filter (index = Filter) and changed in the last week.
    struct Counts { int byFilter[10] = {0}; int recent = 0; int total = 0; };
    const Counts& CountsNow(std::int64_t now = 0);

    /// What the info strip shows about one asset.
    struct AssetInfo {
        bool indexed = false;
        okay::AssetType kind = okay::AssetType::Other;
        std::string typeName;               ///< "Texture", "Prefab", ...
        std::uint64_t size = 0;
        okay::Guid guid;
        std::string guidShort;              ///< the first 8 hex digits
        bool hasMeta = false;
        std::uint64_t hash = 0;
        int uses = 0;                       ///< assets this one references (direct)
        int usedBy = 0;                     ///< assets that reference it (direct)
        int usesAll = 0;                    ///< everything it needs, through the whole graph
        std::vector<std::string> missing;   ///< references written in the file that point at nothing
        std::vector<std::string> labels;
    };
    AssetInfo Info(const std::string& path);
    /// References that point at nothing, and references that only work through the old path of an asset that was moved
    /// outside the editor (the database remembers the move). Both are read from the dependency graph.
    struct MissingRefRow { std::string referrer; std::string reference; };   ///< referrer: absolute path; reference: as written in it
    struct StaleRefRow { std::string referrer; std::string reference; std::string target; };   ///< target: where the asset is now (absolute)
    std::vector<MissingRefRow> MissingRefs();
    std::vector<StaleRefRow> StaleRefs();
    /// Missing plus stale, cheap (cached per revision): the number the panel shows next to "Broken References".
    int ReferenceProblemCount();
    /// Rewrite the stale references so they name the asset's current path (only the path text changes; written
    /// atomically). Undo-able. `r.referencesRewritten` and `r.filesRewritten` say what changed; `r.paths` the files.
    Result FixStaleRefs();
    /// Labels (tags) are kept in the asset's .okaymeta; adding one writes the meta if there is none.
    bool AddLabel(const std::string& path, const std::string& label, std::string* error = nullptr);
    bool RemoveLabel(const std::string& path, const std::string& label, std::string* error = nullptr);
    /// Everything `path` needs (absolute paths), direct or through the whole graph.
    std::vector<std::string> DependenciesOf(const std::string& path, bool recursive);
    /// The assets that use `path`, direct or through the whole graph.
    std::vector<std::string> UsersOfList(const std::string& path, bool recursive);
    /// Import settings of a texture or model (defaults when the asset has no meta). False for other kinds.
    bool GetImportSettings(const std::string& path, okay::ImportSettings& out);
    /// Store settings. Nothing is written when the asset has no meta and the settings are the defaults.
    bool SetImportSettings(const std::string& path, const okay::ImportSettings& settings, std::string* error = nullptr);
    /// Read a file again after it changed outside: forgets what was learned about it.
    bool Reimport(const std::string& path);
    // ---- thumbnails: the database's ThumbnailCache, keyed by GUID and content hash --------------------------
    /// The cache key of a file's thumbnail: the asset's GUID (a stable stand-in made from the path while it has no
    /// meta) and its content hash from the database; for a prefab, scene or material the hashes of everything it
    /// uses are mixed in, so the picture is made again when a texture or mesh it shows changes.
    void ThumbKey(const std::string& path, okay::Guid& guid, std::uint64_t& hash);
    struct ThumbResult {
        std::shared_ptr<const okay::Image> image;   ///< null when it is not ready (draw the type icon)
        bool ready = false;                         ///< a real picture of the asset (memory, disk or just made)
        bool placeholder = false;                   ///< there is no picture for this type, or it could not be made
        bool failed = false;
        std::string key;                            ///< "<guid>:<hash>": changes with the content
    };
    /// Start a frame: up to `budgetMs` of thumbnail making is allowed until the next call (the first one of a frame is
    /// always allowed, so progress never stalls). Pictures already in memory or on disk are free.
    void ThumbFrame(double budgetMs);
    /// The picture for an asset. A missing one is made now when the frame budget allows, else `ready` is false and
    /// the caller shows an icon and asks again next frame.
    ThumbResult Thumb(const std::string& path);
    /// For prefabs, scenes, models and materials (and anything else): `render(absolutePath)` returns an Image, or an
    /// empty one when it cannot. Takes effect for pictures not yet made.
    void AddThumbGenerator(const std::vector<std::string>& extensions, std::function<okay::Image(const std::string&)> render);
    /// Forget a file's picture (memory and disk) so it is made again (Reimport, import settings changed).
    void ForgetThumb(const std::string& path);
    /// The cache (memory layer, disk folder <project>/.okay/thumbs): for tests and the stats. Made on first use.
    okay::ThumbnailCache& Thumbs();
    /// Delete cached pictures of assets that no longer exist.
    int PruneThumbs();
    /// Copy a file or folder from anywhere (an OS drop, the Import button) into `destDir`, under a free name.
    /// All or nothing; undo-able.
    Result ImportFile(const std::string& source, const std::string& destDir);

    /// Test hook: called before each file step with the step's name ("rename", "copy", "remove", "mkdir", "write")
    /// and the path; return true to make that step fail.
    void SetFailureHook(std::function<bool(const char* op, const std::string& path)> hook) { m_fail = std::move(hook); }

    Result CreateFolder(const std::string& dir, const std::string& name);
    /// A new file with `content`, written atomically. Fails if it already exists.
    Result MakeFile(const std::string& dir, const std::string& fileName, const std::string& content);
    /// Rename in place. Files keep their extension: if `newName` has none, the old one is added; a different
    /// extension is refused (it would change what the asset is).
    Result Rename(const std::string& path, const std::string& newName);
    /// Move into a folder (names that clash get "2", "3"...). Paths must be inside Assets.
    Result Move(const std::vector<std::string>& paths, const std::string& destDir);
    /// Copy into a folder (clashing names get "2", "3"...). Folders are copied with everything in them.
    Result Copy(const std::vector<std::string>& paths, const std::string& destDir);
    /// A copy next to the original named "<name> copy".
    Result Duplicate(const std::string& path);
    /// Delete: moves to <project>/.okay/trash so Undo brings them back.
    Result Trash(const std::vector<std::string>& paths);

    bool CanUndo() const { return !m_undo.empty(); }
    std::string UndoLabel() const { return m_undo.empty() ? std::string() : m_undo.back().label; }
    Result Undo();

    /// Assets (outside `paths`) that use any of `paths` or anything inside a folder in `paths`.
    std::vector<Referrer> ReferencesTo(const std::vector<std::string>& paths);
    /// What an asset uses, as absolute paths (direct).
    std::vector<std::string> DependenciesOf(const std::string& path) { return DependenciesOf(path, false); }
    /// Trash entries kept (folders under .okay/trash).
    int TrashCount() const;
    /// Remove everything in the trash for good. Returns false if something could not be removed.
    bool EmptyTrash();

private:
    struct UndoEntry { std::string label; std::function<Result()> run; };
    std::string m_root;
    okay::AssetDatabase m_db;
    bool m_indexed = false;
    std::uint64_t m_nextPoll = 0;
    std::uint64_t m_revision = 0;
    std::uint64_t m_problemRev = ~0ull; int m_problemCount = 0;
    struct FolderStat { int files = 0; std::vector<std::string> kids; };
    std::map<std::string, FolderStat> m_folderStats;                // parent project path -> contents, per revision
    std::uint64_t m_folderStatsRev = ~0ull;
    bool m_prunePending = false;
    std::vector<okay::Guid> GuidsUnder(const std::string& abs);   // the asset at a path, or every asset inside a folder
    double m_scanMs = 0;
    bool DoScan();                                                  // one incremental database scan (false: the folder cannot be read); raises m_revision on a change
    bool m_scanChanged = false;                                     // the last scan found a change
    std::map<okay::Guid, std::vector<std::string>> m_missing;       // asset -> references that point at nothing (rebuilt per revision)
    std::uint64_t m_missingRev = ~0ull;
    void EnsureMissing();
    Counts m_counts; std::uint64_t m_countsRev = ~0ull; std::int64_t m_countsNow = -1;
    struct ThumbKeyEntry { std::uint64_t rev = ~0ull; okay::Guid guid; std::uint64_t hash = 0; };
    std::map<std::string, ThumbKeyEntry> m_thumbKeys;               // path -> key at a revision
    std::unique_ptr<okay::ThumbnailCache> m_thumbs;
    std::vector<std::pair<std::vector<std::string>, std::function<okay::Image(const std::string&)>>> m_thumbGens;
    double m_thumbBudgetMs = 0, m_thumbSpentMs = 0;
    std::string AbsOf(const std::string& projectPath) const { return m_root + "/" + projectPath; }
    okay::Guid ThumbGuidOf(const okay::AssetRecord& rec) const;
    std::function<bool(const char*, const std::string&)> m_fail;
    std::vector<UndoEntry> m_undo;

    friend struct Tx;
    Result MoveCore(const std::vector<std::pair<std::string, std::string>>& items, const std::string& label, bool pushUndo);
    Result CopyCore(const std::vector<std::string>& sources, const std::vector<std::string>& dests, const std::string& label);
    Result TrashCore(const std::vector<std::string>& paths, bool pushUndo);
    std::string CheckInside(const std::string& path, bool allowRoot) const;
    void PushUndo(const std::string& label, std::function<Result()> run);
};

// ---- favourites -------------------------------------------------------------------------------

/// Where `abs` ends up after the moves in `map` (a folder entry moves what is inside it); "" when it does not move.
std::string RemapPath(const PathMap& map, const std::string& abs);

/// Follow renames and moves in a list of absolute paths (the "Scenes In Build" list). Returns true when one changed.
bool RemapPaths(const PathMap& map, std::vector<std::string>& paths);

/// Starred files and folders, kept in the project (<project>/.okay/favourites.txt, one path relative to Assets per
/// line) so they travel with it. Missing items are dropped when listed; moves and renames are followed.
class Favourites {
public:
    /// Read the list for the project whose Assets folder is `assetsRoot` (a missing or damaged file is an empty list).
    void Load(const std::string& assetsRoot);
    bool Has(const std::string& abs) const;
    /// Add or remove. Returns false (and changes nothing) when the item is outside Assets or the list cannot be saved.
    bool Toggle(const std::string& abs, std::string* error = nullptr);
    /// Favourites that still exist, as absolute paths, in the order they were added.
    std::vector<std::string> List() const;
    /// Follow renames and moves; saves when something changed.
    void Remap(const PathMap& map);
    const std::string& Root() const { return m_root; }
    /// Test hook for the save step.
    void SetFailureHook(std::function<bool()> hook) { m_fail = std::move(hook); }
private:
    bool Save(std::string* error);
    std::string m_root;
    std::vector<std::string> m_items;   // relative to Assets, '/' separated
    std::function<bool()> m_fail;
};

// ---- clipboard --------------------------------------------------------------------------------

struct Clipboard {
    std::vector<std::string> items;
    bool cut = false;
    bool Empty() const { return items.empty(); }
    void Set(std::vector<std::string> paths, bool isCut) { items = std::move(paths); cut = isCut; }
    void Clear() { items.clear(); cut = false; }
    /// Paste into `destDir`: a copy, or a move for a cut (the clipboard is emptied after a successful cut).
    /// Items that no longer exist are skipped and named in `note`.
    Result Paste(Browser& browser, const std::string& destDir);
};

} // namespace okaypb
