#pragma once
// The asset database: the data layer an asset browser sits on.
//   * Every asset file `foo.png` has a sidecar `foo.png.okaymeta` with a persistent GUID,
//     the import settings and labels. Scan() creates missing metas (never touching the
//     asset), follows moved and renamed files, and keeps GUID <-> path lookups valid.
//   * A dependency graph built by registered text extractors (scenes, prefabs, materials,
//     clips, dialogue, quests, behavior trees, ...): DependenciesOf, UsedBy, FindUnused,
//     FindMissing, BuildClosure.
//   * Per-asset import settings (okay/Core/ImportSettings.hpp) with a Reimport signal.
//   * A search / filter / sort / page query for an asset browser.
// Not thread-safe: use one database from one thread. See docs/asset_database.md.
#include "okay/Core/ImportSettings.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class AssetManager;

/// A 128-bit asset identifier, written as 32 lower-case hex digits.
struct Guid {
    std::uint64_t hi = 0, lo = 0;
    bool IsNull() const { return hi == 0 && lo == 0; }
    explicit operator bool() const { return !IsNull(); }
    bool operator==(const Guid& o) const { return hi == o.hi && lo == o.lo; }
    bool operator!=(const Guid& o) const { return !(*this == o); }
    bool operator<(const Guid& o) const { return hi != o.hi ? hi < o.hi : lo < o.lo; }
    std::string ToString() const;
    /// Accepts 32 hex digits (any case), optionally dashed 8-4-4-4-12 and/or in braces.
    static bool Parse(const std::string& text, Guid& out);
    /// A fresh random GUID (never null).
    static Guid Generate();
};

struct GuidHash {
    std::size_t operator()(const Guid& g) const { return static_cast<std::size_t>(g.hi * 0x9E3779B97F4A7C15ull ^ (g.lo + 0x7F4A7C15ull)); }
};

/// One indexed asset file.
struct AssetRecord {
    Guid guid;
    std::string path;                  ///< relative to the scanned root, '/' separators
    AssetType kind = AssetType::Other;
    std::uint64_t hash = 0;            ///< FNV-1a of the content (AssetDatabase::HashFile)
    std::int64_t size = 0;             ///< bytes
    std::int64_t modified = 0;         ///< file modification time, seconds since the Unix epoch
    std::vector<std::string> labels;
    ImportSettings settings;
    int metaVersion = 0;
    bool metaReadOnly = false;         ///< the meta was written by a newer layout: never rewritten

    std::string Name() const { return AssetFileName(path); }
    std::string Extension() const { return AssetExtension(path); }
    bool HasLabel(const std::string& label) const;   // case-insensitive
};

/// What a Reimport signal says changed.
struct ReimportEvent {
    enum class Reason { Added, SourceChanged, SettingsChanged };
    Guid guid;
    std::string path;
    Reason reason = Reason::SourceChanged;
    const char* ReasonName() const;
};

/// A path found inside a text asset, with where it sits so it can be rewritten.
struct FoundRef {
    std::string path;                  ///< as written in the file (escapes removed)
    std::size_t begin = std::string::npos, end = std::string::npos;   ///< span of the raw text in the file
    bool quoted = false;               ///< the span is inside a "quoted" string (escape on rewrite)
};
/// Finds the asset paths in one text asset. `assetPath` is the asset's own project path.
using RefExtractor = std::function<void(const std::string& assetPath, const std::string& text, std::vector<FoundRef>& out)>;

/// Built-in scanners, usable on their own.
/// Scene/prefab text: every quoted token that names a known asset type, plus the paths on
/// `prefab`, `srcscene`, `netprefab` and `visualscriptpath` lines; `gameobject`, `name` and
/// `tag` lines are skipped (a name is not a path).
std::vector<FoundRef> ExtractSceneRefs(const std::string& text);
/// `.okaymat`: the `texture <path>` line ("-" means none).
std::vector<FoundRef> ExtractMaterialRefs(const std::string& text);
/// Generic text scanner (clips, dialogue, quests, behavior trees, sequences, glTF, ...):
/// quoted strings, `key=value` values and bare words that end in a known asset extension.
std::vector<FoundRef> ExtractTextRefs(const std::string& text);

struct ScanOptions {
    bool createMetas = true;           ///< write a `.okaymeta` for assets that have none
    bool detectMoves = true;           ///< follow moved / renamed files by GUID, hash and name
    bool matchByName = true;           ///< last resort: one orphan meta + one new file with the same name and type
    bool verifyHashes = true;          ///< hash every file on a first scan (false: trust a meta's hash when the size matches)
    bool storeHashInMeta = true;       ///< keep the content hash in the meta so a lost meta can be matched later
    bool upgradeMetas = true;          ///< rewrite metas written by an older layout
    bool buildDependencies = true;     ///< (re)read text assets that changed and rebuild the graph
    bool deleteOrphanMetas = false;    ///< remove metas whose asset is gone and was not matched to a new file
    bool logWarnings = true;           ///< also send warnings to the engine log
    std::vector<std::string> ignoreDirs;   ///< folder names to skip anywhere (folders starting with '.' are always skipped)
    std::vector<std::string> ignoreExtensions = {"okaymeta", "okayasset", "part", "tmp", "bak", "swp", "corrupt"};
    std::size_t fullHashLimit = 512u << 20;   ///< files above this are hashed from their head, tail and size
};

struct ScanReport {
    struct Move { Guid guid; std::string from, to; std::string how; };   ///< how: "guid", "hash", "name", "restored"
    struct Duplicate { std::string path; Guid original, replacement; };
    int scanned = 0;                   ///< asset files found
    int created = 0;                   ///< metas written for new assets
    int upgraded = 0;                  ///< metas rewritten from an older layout
    int unmetaed = 0;                  ///< assets left without a meta (createMetas off)
    std::vector<Move> moved;
    std::vector<Duplicate> duplicates; ///< copied metas: the copy got a new GUID
    std::vector<std::string> orphanMetas;   ///< metas whose asset is gone (project paths of the assets)
    std::vector<std::string> removed;  ///< assets that were indexed and are gone
    std::vector<ReimportEvent> reimports;
    std::vector<std::string> warnings;
    std::string ToString() const;
};

struct MissingRef { Guid referrer; std::string referrerPath; std::string reference; };
/// A reference that still points at an old path of a moved asset.
struct StaleRef { Guid referrer; std::string referrerPath; std::string reference; Guid target; std::string targetPath; };

/// Result of Resolve().
struct AssetResolution {
    Guid guid;                         ///< null when nothing matched
    std::string path;                  ///< the asset's current project path
    bool viaRedirect = false;          ///< matched an old path of a moved asset
    explicit operator bool() const { return !guid.IsNull(); }
};

/// Search query for an asset browser. Every filter that is set must match.
struct AssetQuery {
    enum class Sort { Name, Path, Type, Modified, Size };
    std::string text;                      ///< substring of the file name (case-insensitive); empty = all
    bool matchFolders = false;             ///< `text` may also match the folder part of the path
    std::vector<AssetType> types;          ///< any of these types
    std::vector<std::string> extensions;   ///< any of these extensions (no dot)
    std::vector<std::string> labels;       ///< labels, case-insensitive
    bool anyLabel = false;                 ///< match any of `labels` (default: all of them)
    std::string folder;                    ///< only under this folder (project path); empty = everywhere
    bool recursive = true;                 ///< with `folder`: include sub-folders
    std::int64_t modifiedAfter = 0;        ///< modified at or after this time (seconds since epoch); 0 = any
    std::int64_t modifiedWithinSeconds = 0;///< modified within this long before `now`; 0 = any
    std::int64_t now = 0;                  ///< "now" for the line above (0 = the clock); lets tests be exact
    Guid referencing;                      ///< only assets that reference this one (its users)
    Guid referencedBy;                     ///< only assets this one references (its dependencies)
    bool transitive = false;               ///< make the two lines above follow the whole graph
    bool unusedOnly = false;               ///< only assets nothing references
    bool withMissingRefsOnly = false;      ///< only assets that have a reference to a missing file
    Sort sort = Sort::Name;
    bool descending = false;
    std::size_t pageSize = 0;              ///< 0 = everything on one page
    std::size_t page = 0;                  ///< 0-based
};

struct AssetPage {
    std::vector<const AssetRecord*> items; ///< valid until the next Scan / mutation
    std::size_t total = 0;                 ///< matches over all pages
    std::size_t page = 0, pageSize = 0, pageCount = 1;
};

class AssetDatabase {
public:
    AssetDatabase();
    ~AssetDatabase();
    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    /// The meta file layout this build writes (`okaymeta <n>` on the first line).
    static constexpr int kMetaVersion = 1;
    static const char* MetaExtension() { return ".okaymeta"; }
    static std::string MetaPathFor(const std::string& assetPath) { return assetPath + MetaExtension(); }

    // --- Scan -------------------------------------------------------------------
    /// Index every asset under `root`: read metas, create the missing ones, detect moves,
    /// duplicates and orphans, refresh hashes and the dependency graph. Calling it again
    /// is incremental and keeps the lookups valid across moves. A different root starts over.
    ScanReport Scan(const std::string& root, const ScanOptions& options = {});
    ScanReport Rescan();                   ///< Scan(Root(), the last options)
    const std::string& Root() const { return m_root; }
    /// Forget everything (records, redirects, vanished assets, graph). Metas are untouched.
    void Clear();

    // --- Lookup -----------------------------------------------------------------
    std::size_t Count() const { return m_slots.size(); }
    std::string GuidToPath(const Guid& guid) const;          ///< "" when unknown
    Guid PathToGuid(const std::string& path) const;          ///< null when unknown; exact (normalised) path only
    /// Accepts a GUID in text form or a path: a project path, an absolute path inside the
    /// root, a path with a leading "./" or "../", or an old path of an asset that moved.
    AssetResolution Resolve(const std::string& guidOrPath) const;
    const AssetRecord* Find(const Guid& guid) const;
    const AssetRecord* Find(const std::string& guidOrPath) const;
    /// Every record, sorted by path.
    std::vector<const AssetRecord*> All() const;
    /// Root + project path; "" for an unknown GUID.
    std::string AbsolutePath(const Guid& guid) const;
    /// A path (absolute, or relative to the root) as a normalised project path; "" if it is outside the root.
    std::string ToProjectPath(const std::string& path) const;
    /// GUIDs whose meta is on disk but whose asset file is gone (from the last Scan).
    std::vector<Guid> OrphanGuids() const;

    // --- Changes ------------------------------------------------------------------
    /// Move or rename an asset and its meta together. The old path keeps resolving.
    bool MoveAsset(const std::string& guidOrPath, const std::string& newProjectPath, std::string* error = nullptr);
    /// Rehash one file after it changed on disk (or index it if it is new). Raises
    /// SourceChanged / Added. Returns the guid (null if the file is not an asset).
    Guid RefreshFile(const std::string& path);
    /// Rewrite text references that still use the old path of a moved asset. Returns how
    /// many references were rewritten; `changedFiles` gets the project paths of the files.
    int FixStaleReferences(std::vector<std::string>* changedFiles = nullptr);
    /// Remove the metas of orphaned assets from disk. Returns how many.
    int RemoveOrphanMetas();

    // --- Labels -------------------------------------------------------------------
    bool SetLabels(const Guid& guid, const std::vector<std::string>& labels, std::string* error = nullptr);
    bool AddLabel(const Guid& guid, const std::string& label, std::string* error = nullptr);
    bool RemoveLabel(const Guid& guid, const std::string& label, std::string* error = nullptr);
    /// Every label in use, sorted, with how many assets carry it.
    std::vector<std::pair<std::string, std::size_t>> AllLabels() const;

    // --- Import settings and reimport ---------------------------------------------
    const ImportSettings* GetSettings(const Guid& guid) const;
    /// Store new settings in the meta. A change raises a SettingsChanged reimport; the
    /// same values change nothing. False (and `error`) for an unknown asset, a settings
    /// block of the wrong kind, or a meta that can not be written.
    bool SetSettings(const Guid& guid, const ImportSettings& settings, std::string* error = nullptr);
    /// Called, after the change was stored, for every reimport signal.
    void SetReimportCallback(std::function<void(const ReimportEvent&)> callback) { m_onReimport = std::move(callback); }
    /// Signals queued since the last call (also delivered to the callback).
    std::vector<ReimportEvent> TakeReimports();
    /// Let an AssetManager's hot reload feed the database: when it reloads a file in place
    /// the database rehashes it (a SourceChanged signal) and rebuilds that file's references.
    /// Replaces the manager's reload callback; `chain` is called afterwards.
    void AttachHotReload(AssetManager& manager,
                         std::function<void(const std::string& key, const std::string& typeName)> chain = {});
    /// The function AttachHotReload installs, to hook up by hand.
    std::function<void(const std::string&, const std::string&)> ReloadHook();

    // --- Dependencies ---------------------------------------------------------------
    /// Add an extractor for files with these extensions (no dot, any case). Several
    /// extractors may cover one extension; their results are combined. The built-in ones
    /// are registered by the constructor. Takes effect at the next Scan / RebuildDependencies.
    void RegisterExtractor(const std::string& name, const std::vector<std::string>& extensions, RefExtractor extractor);
    bool RemoveExtractor(const std::string& name);
    std::vector<std::string> ExtractorNames() const;
    /// Re-read every text asset that has an extractor and rebuild the graph (Scan does the
    /// changed ones itself).
    void RebuildDependencies();
    /// The assets `guid` references (unique, in order of first appearance; itself excluded).
    std::vector<Guid> DependenciesOf(const Guid& guid, bool recursive = false) const;
    /// The assets that reference `guid`, sorted by path.
    std::vector<Guid> UsedBy(const Guid& guid, bool recursive = false) const;
    /// `roots` plus everything they reach, breadth first, each once (cycles are fine).
    /// For a build or export: ship exactly these.
    std::vector<Guid> BuildClosure(const std::vector<Guid>& roots) const;
    std::vector<std::string> BuildClosurePaths(const std::vector<std::string>& rootsGuidOrPath) const;
    /// Assets not reachable from `roots` (the roots themselves never count). Empty roots
    /// means every scene. Sorted by path.
    std::vector<Guid> FindUnused(const std::vector<Guid>& roots) const;
    /// Assets nothing references at all (scenes and other top-level files included).
    std::vector<Guid> FindUnreferenced() const;
    /// References to files that exist neither in the index nor on disk.
    std::vector<MissingRef> FindMissing() const;
    /// References that only resolve through an old path of a moved asset.
    std::vector<StaleRef> FindStaleReferences() const;

    // --- Search ---------------------------------------------------------------------
    AssetPage Search(const AssetQuery& query) const;
    /// Count of assets per kind (all assets, or only those matching `query` ignoring paging).
    std::map<AssetType, std::size_t> CountByType() const;
    std::map<AssetType, std::size_t> CountByType(const AssetQuery& query) const;

    // --- Utilities --------------------------------------------------------------------
    /// FNV-1a 64 of a file's bytes (head + tail + size for files above `fullHashLimit`).
    /// 0 when the file can not be read. `size` receives the byte count.
    static std::uint64_t HashFile(const std::string& absolutePath, std::int64_t* size = nullptr,
                                  std::size_t fullHashLimit = 512u << 20);
    /// Replace how new GUIDs are made (tests; the default is random).
    void SetGuidGenerator(std::function<Guid()> generator) { m_guidGen = std::move(generator); }

private:
    struct Edge {
        FoundRef ref;
        Guid target;                       // null = unresolved
        bool viaRedirect = false;          // matched an old path of a moved asset
        bool missing = false;              // unresolved and no such file on disk
        int style = 0;                     // 0: relative to the root, 1: relative to the referrer
        std::string prefix;                // leading path stripped from the reference to match (style 0)
    };
    struct Slot {
        AssetRecord rec;
        std::int64_t stamp = 0;            // raw file time ticks at the last look
        std::vector<std::string> unknownMetaLines;   // lines a newer / other writer added: kept verbatim
        std::string settingsPrint;         // settings fingerprint at the last look
        std::vector<Edge> edges;
        std::uint64_t depsHash = 0;        // content hash the edges were extracted from
        std::uint64_t depsGeneration = 0;  // extractor set the edges were extracted with
        bool depsPacked = false;           // edges came from a packed file: spans can not be rewritten
    };
    struct ExtractorRec { std::string name; std::vector<std::string> extensions; RefExtractor fn; };
    struct Vanished { AssetRecord rec; std::string settingsPrint; std::vector<std::string> unknownMetaLines; };
    struct RefMatch { Guid guid; bool redirect = false; int style = 0; std::string prefix; };

    std::string m_root;
    ScanOptions m_options;
    std::map<Guid, Slot> m_slots;
    std::unordered_map<std::string, Guid> m_pathIndex;          // key(path) -> guid
    std::unordered_map<std::string, Guid> m_redirects;          // key(old path) -> guid
    std::map<Guid, Vanished> m_vanished;                        // indexed once, file gone (for hash matching)
    std::map<Guid, std::string> m_orphans;                      // meta on disk, no asset: guid -> asset project path
    std::vector<ExtractorRec> m_extractors;
    std::uint64_t m_depsGeneration = 1;
    std::vector<ReimportEvent> m_pending;                       // since the last TakeReimports
    std::vector<ReimportEvent> m_fresh;                         // not yet delivered to the callback
    std::function<void(const ReimportEvent&)> m_onReimport;
    std::function<Guid()> m_guidGen;
    mutable bool m_reverseValid = false;                        // lazy "used by" map
    mutable std::unordered_map<Guid, std::vector<Guid>, GuidHash> m_reverse;

    void RegisterDefaultExtractors();
    void RebuildIndexes();
    void ResolveAllEdges();
    void ResolveEdgesOf(Slot& slot);
    RefMatch ResolveRaw(const std::string& raw, const std::string& fromPath) const;
    bool ExistsOnDisk(const std::string& raw, const std::string& fromPath) const;
    void ExtractDependencies(Slot& slot, bool force);
    void BuildReverse() const;
    void Raise(const ReimportEvent& ev, std::vector<ReimportEvent>* report = nullptr);
    void Deliver();
    Guid NewGuid() const;
    bool WriteMeta(Slot& slot, std::string* error);
    std::string KeyOf(const std::string& projectPath) const;
    Slot* SlotOf(const Guid& guid);
    const Slot* SlotOf(const Guid& guid) const;
};

} // namespace okay
