#pragma once
// The Project panel's logic, with no window code in it (so tests can run it without ImGui):
//   * a folder model: list a folder or search the whole project, filter by type, sort (natural order),
//   * safe file operations inside the Assets folder: create, rename, move, copy, duplicate, delete to a
//     trash folder, and undo for each. Every operation either completes or puts everything back,
//   * rename and move keep references: a scene, prefab, material or other text asset that points at the
//     moved file (or at a file inside a moved folder) is rewritten, the .okaymeta (GUID) moves with the file,
//   * "what uses this asset" for the delete confirmation and Find References,
//   * the copy / cut / paste clipboard.
// It sits on okay::AssetDatabase (GUIDs, dependency graph). See docs/project_panel.md.
// Failure injection: SetFailureHook makes any file step fail, so tests can check nothing is lost.
#include "okay/Core/AssetDatabase.hpp"

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
};

/// What the search box means: plain words (all must be in the name) plus t:model, ext:png, folder.
struct Query {
    std::vector<std::string> words;     // lower case
    Filter filter = Filter::All;        // from "t:<type>"
    bool foldersOnly = false;           // "t:folder"
    std::string ext;                    // ".png" from "ext:png"
};
Query ParseQuery(const std::string& text);

struct ListOptions {
    std::string text;                   ///< the search box
    Filter filter = Filter::All;        ///< the type chips (combined with the text)
    SortKey sort = SortKey::Name;
    bool descending = false;
    bool recursive = false;             ///< search below the folder (true when searching in this folder or the whole project)
    std::size_t limit = 4000;           ///< stop after this many entries
};
/// The folder's entries (hidden files, .okaymeta files and temp files skipped), folders first. With `text`
/// or `recursive`, everything below `dir` that matches.
std::vector<Entry> ListFolder(const std::string& dir, const ListOptions& options);
/// Sort in place the way ListFolder does (folders first).
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

    /// Index (or re-index) the assets: GUIDs, meta files, the dependency graph. Cheap when nothing changed.
    /// Returns false when the folder cannot be scanned.
    bool Sync();
    /// Call every frame with a millisecond clock. Re-indexes when due: the next poll waits at least 2 seconds
    /// and 30 times as long as the last scan took, so polling stays a few percent of one core at worst. Does
    /// nothing until the first Sync (the first scan can take a while on a big project and runs only when an
    /// operation or the info pane needs the database). Returns true when the scan found changes.
    bool Poll(std::uint64_t nowMs);
    bool Indexed() const { return m_indexed; }
    /// How many assets use this one (0 when unknown). Cheap: no scan.
    int UsersOf(const std::string& path);

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
    /// What an asset uses, as absolute paths.
    std::vector<std::string> DependenciesOf(const std::string& path);
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
    std::function<bool(const char*, const std::string&)> m_fail;
    std::vector<UndoEntry> m_undo;

    friend struct Tx;
    Result MoveCore(const std::vector<std::pair<std::string, std::string>>& items, const std::string& label, bool pushUndo);
    Result CopyCore(const std::vector<std::string>& sources, const std::vector<std::string>& dests, const std::string& label);
    Result TrashCore(const std::vector<std::string>& paths, bool pushUndo);
    std::string CheckInside(const std::string& path, bool allowRoot) const;
    void PushUndo(const std::string& label, std::function<Result()> run);
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
