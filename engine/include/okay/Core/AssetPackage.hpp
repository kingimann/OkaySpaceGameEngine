#pragma once
// Asset packages (docs/engine/asset_packages.md): the engine's one container for a game's files. An exported game ships
// its data as a base package (game.okaypkg) and, later, patch packages that replace, add or remove single assets, listed
// in order in packages.manifest. Everything here was designed for this engine from first principles (no other engine's
// package layout was studied or imitated): fixed-size little-endian records, a table of contents sorted by a 64-bit id
// so a lookup is a binary search, every region bounded and contiguous so a reader can check the whole layout before it
// trusts any of it, and two hashes in a trailer (one over the metadata, one over the data).
//
// File layout (version 1):
//   header    96 bytes (kHeaderBytes)       magic "OKAYPKG\0", version, sizes and offsets of the regions below
//   data      entry bytes, each aligned to `alignment` from the start of the file; stored or raw DEFLATE (RFC 1951)
//   toc       entryCount records of 64 bytes (kEntryBytes), sorted by id, no id twice
//   names     the entries' names (UTF-8, '/' separators, no terminators); empty when names were stripped
//   deps      the dependency lists: 64-bit ids, 8 bytes each
//   trailer   32 bytes: magic "OKPKEND\0", the file size, Hash64 of header+toc+names+deps, Hash64 of the data region
//
// Ids: IdOf(name) = Hash64(the name in lower case with '/' separators, kIdSeed); 0 is never an id. Names compare without
// letter case, as Windows does, so two files whose names differ only in case are one id: the writer refuses them, and
// two different names with one id (a 64-bit collision) are refused too, with both names in the message.
//
// Hash64 is a non-cryptographic 64-bit hash written for this engine (four multiply-xorshift lanes over 32-byte stripes,
// a length step and a final avalanche; constants are the fractional parts of the golden ratio and of the square roots
// of 2, 3, 5 and 7). It finds damage, not tampering: a package is checked for being intact, not for who made it.
//
// Reading untrusted packages (a downloaded game or patch): every header field, offset, size and count is checked
// against the file's real size and the Limits before anything is allocated or read; a damaged package is refused
// with a plain-words reason and never crashes the reader (tests/test_asset_package.cpp fuzzes it under ASan/UBSan).
//
// No <mutex>/<thread> here (CLAUDE.md 21a): the reader's file handle is guarded inside AssetPackage.cpp.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace okay {
class JobSystem;

namespace assetpkg {

using AssetId = std::uint64_t;

constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 96;
constexpr std::size_t kEntryBytes = 64;
constexpr std::size_t kTrailerBytes = 32;
constexpr std::uint64_t kIdSeed = 0x4F4B4159494430ull;   // "OKAYID0": ids and content hashes never share a seed

enum class Method : std::uint8_t { Stored = 0, Deflate = 1, Removed = 2 };
enum PackageFlags : std::uint32_t { kHasNames = 1u, kIsPatch = 2u };

// ---- Hash64 ------------------------------------------------------------------------------------------------

/// The engine's 64-bit non-cryptographic hash (see the top of this file). Same bytes and seed, same value, on every
/// machine (little-endian reads are done byte by byte).
/// Bytes take `const std::uint8_t*` (not `const void*`), so a string literal can never pick this overload by mistake.
std::uint64_t Hash64(const std::uint8_t* data, std::size_t size, std::uint64_t seed = 0);
inline std::uint64_t Hash64(std::string_view s, std::uint64_t seed = 0) {
    return Hash64(reinterpret_cast<const std::uint8_t*>(s.data()), s.size(), seed);
}

/// Hash64 fed in pieces: Update any number of times, then Final. Gives exactly Hash64 of all the pieces joined.
class Hasher {
public:
    explicit Hasher(std::uint64_t seed = 0);
    void Update(const void* data, std::size_t size);
    std::uint64_t Final() const;
private:
    std::uint64_t m_lane[4];
    std::uint64_t m_seed;
    std::uint64_t m_total = 0;
    unsigned char m_buf[32];
    std::size_t m_used = 0;
};

/// "Assets\\Foo//bar.png" -> "Assets/Foo/bar.png": '\' becomes '/', repeated '/' and "./" parts go, no leading '/'.
std::string NormalizeName(std::string_view name);
/// The id of a name: Hash64 of NormalizeName(name) in ASCII lower case, seeded with kIdSeed (never 0).
AssetId IdOf(std::string_view name);
/// Lower-case 16-digit hex of a 64-bit value (ids and hashes in messages and the manifest).
std::string Hex64(std::uint64_t v);
/// True when `name` may be a package name: NormalizeName(name) == name, at most 1024 bytes, and it passes
/// okay::pathsafety's Portable rules (no "..", no drive letter, no device name ...). `why` says what is wrong.
bool IsValidName(std::string_view name, std::string* why = nullptr);

// ---- Limits for reading untrusted packages ----------------------------------------------------------------

struct Limits {
    std::uint32_t maxEntries = 1u << 20;               ///< entries in one package
    std::uint64_t maxNamesBytes = 64ull << 20;         ///< the whole name table
    std::uint64_t maxDepsBytes = 64ull << 20;          ///< the whole dependency table
    std::uint32_t maxDepsPerEntry = 1u << 16;
    std::uint64_t maxEntryBytes = 4ull << 30;          ///< one entry, unpacked
};

// ---- Writing ----------------------------------------------------------------------------------------------

struct WriteOptions {
    bool keepNames = true;          ///< the name table (readable names in the editor and in tools; a shipped game may strip it)
    bool compress = true;           ///< try DEFLATE per entry; kept only when it saves at least 1/32 of the bytes
    std::uint32_t alignment = 16;   ///< every entry's data starts on this boundary (power of two, 1 to 65536)
    std::uint64_t baseStackHash = 0;///< a patch: the PackageStack::StackHash() of the packages it applies to (0 = a base package)
    JobSystem* jobs = nullptr;      ///< compress on these workers (null = on the calling thread); the output is the same either way
    std::string cacheDir;           ///< compressed copies kept by content hash (the editor's Library/PackageCache): unchanged assets are not compressed again
    bool trustCache = true;         ///< false: compress everything again and refresh the cache (after a verify found a bad cached copy)
};

struct WriteStats {
    std::size_t entries = 0, removed = 0, compressed = 0, fromCache = 0;
    std::uint64_t bytesIn = 0;      ///< the entries' sizes before compression
    std::uint64_t bytesOut = 0;     ///< the package file's size
    std::uint64_t metaHash = 0;     ///< the trailer's metadata hash (what a patch's baseStackHash chains from)
    double seconds = 0.0;
};

/// Collects entries, then writes one package. Entries are read (from memory or from their files) and compressed only
/// while Write runs, a batch at a time, so a large game is never held in memory all at once. The same entries give a
/// byte-identical package every time (sorted by id, no dates, zero padding).
class PackageWriter {
public:
    /// Add an entry from bytes in memory. False (and `why`) for an invalid name, or a name whose id another name already
    /// has: two names differing only in letter case, or a real 64-bit collision; the message names both.
    bool Add(std::string_view name, std::vector<std::uint8_t> bytes, std::vector<std::string> dependencies = {},
             std::uint8_t kind = 0, std::string* why = nullptr);
    /// Add an entry whose bytes are read from `file` when the package is written.
    bool AddFile(std::string_view name, std::string file, std::vector<std::string> dependencies = {},
                 std::uint8_t kind = 0, std::string* why = nullptr);
    /// A patch only: `name` is removed from the game (it hides the entry of a package below).
    bool AddRemoval(std::string_view name, std::string* why = nullptr);
    std::size_t Count() const { return m_items.size(); }
    bool Has(std::string_view name) const;

    /// Write the package to `path` (through "<path>.part", renamed over `path` only when complete).
    bool Write(const std::string& path, const WriteOptions& options = {}, WriteStats* stats = nullptr,
               std::string* why = nullptr) const;
    /// The same package as bytes in memory.
    bool WriteToMemory(std::vector<std::uint8_t>& out, const WriteOptions& options = {}, WriteStats* stats = nullptr,
                       std::string* why = nullptr) const;

    struct Item {
        AssetId id = 0;
        std::string name;
        std::string file;                       ///< empty: `bytes` holds the data
        std::vector<std::uint8_t> bytes;
        std::vector<std::string> deps;
        std::uint8_t kind = 0;
        bool removal = false;
    };
private:
    bool AddItem(Item item, std::string* why);
    std::vector<Item> m_items;                 ///< kept sorted by id
};

// ---- Reading ----------------------------------------------------------------------------------------------

struct Header {
    std::uint16_t version = 0;
    std::uint32_t flags = 0, entryCount = 0, alignment = 0;
    std::uint64_t dataOffset = 0, dataBytes = 0, tocOffset = 0, namesOffset = 0, namesBytes = 0, depsOffset = 0, depsBytes = 0;
    std::uint64_t baseStackHash = 0;
    std::uint64_t fileSize = 0, metaHash = 0, dataHash = 0;   ///< from the trailer
};

struct Entry {
    AssetId id = 0;
    std::uint64_t offset = 0, storedBytes = 0, size = 0, contentHash = 0;
    std::uint32_t nameOffset = 0, nameBytes = 0, depsFirst = 0, depsCount = 0;
    Method method = Method::Stored;
    std::uint8_t kind = 0;
};

/// One opened package. Open checks the whole layout and the metadata hash; entry data is checked (size and content
/// hash) every time it is read, and VerifyAll checks every byte. Thread-safe for reading.
class Package {
public:
    ~Package();
    Package(const Package&) = delete;
    Package& operator=(const Package&) = delete;

    /// Open a package file. Null and a plain-words `why` when the file is missing, not a package, from a newer engine,
    /// or damaged in any checked way.
    static std::shared_ptr<Package> OpenFile(const std::string& path, std::string& why, const Limits& limits = {});
    /// Open a package held in memory (the bytes are kept).
    static std::shared_ptr<Package> OpenMemory(std::vector<std::uint8_t> bytes, std::string& why, const Limits& limits = {});

    const Header& Info() const { return m_header; }
    bool IsPatch() const { return (m_header.flags & kIsPatch) != 0; }
    bool HasNames() const { return (m_header.flags & kHasNames) != 0; }
    const std::vector<Entry>& Entries() const { return m_entries; }
    const Entry* Find(AssetId id) const;
    const Entry* Find(std::string_view name) const { return Find(IdOf(name)); }
    /// The entry's name, or "" when the package has no names.
    std::string_view NameOf(const Entry& e) const;
    std::vector<AssetId> DependenciesOf(const Entry& e) const;
    /// The entry's bytes, unpacked and checked against its size and content hash.
    bool Read(const Entry& e, std::vector<std::uint8_t>& out, std::string& why) const;
    /// Read every entry and the whole data region and check them against their hashes (slow on a big package).
    bool VerifyAll(std::string& why) const;
    const std::string& Path() const { return m_path; }

private:
    Package() = default;
    bool Load(std::string& why, const Limits& limits);
    bool ReadAt(std::uint64_t offset, void* dst, std::size_t size, std::string& why) const;
    struct Source;
    std::unique_ptr<Source> m_src;
    std::string m_path;
    Header m_header;
    std::vector<Entry> m_entries;
    std::string m_names;
    std::vector<AssetId> m_deps;
    Limits m_limits;
};

// ---- Mounting a base package and its patches ------------------------------------------------------------------

/// A base package and the patches on top of it, in order. A later package replaces an earlier one's entry with the
/// same id, and a patch's removal hides it. Each patch must have been made for exactly the packages below it
/// (its baseStackHash), so a patch for another version of the game is refused instead of mixing two versions.
class PackageStack {
public:
    bool Mount(std::shared_ptr<const Package> package, std::string& why);
    std::size_t Layers() const { return m_layers.size(); }
    const Package& Layer(std::size_t i) const { return *m_layers[i]; }
    /// Base: its metaHash; each patch: Hash64 of (the hash so far, the patch's metaHash).
    std::uint64_t StackHash() const { return m_stackHash; }

    struct Hit { const Package* package = nullptr; const Entry* entry = nullptr; std::size_t layer = 0; };
    /// The visible entry for `id` (top layer first); false when no layer has it or a patch removed it.
    bool Find(AssetId id, Hit& out) const;
    bool Contains(std::string_view name) const { Hit h; return Find(IdOf(name), h); }
    bool Read(AssetId id, std::vector<std::uint8_t>& out, std::string& why) const;
    bool Read(std::string_view name, std::vector<std::uint8_t>& out, std::string& why) const { return Read(IdOf(name), out, why); }
    /// The name of a visible entry (from whichever layer has names), or "".
    std::string NameOf(AssetId id) const;
    /// Every visible entry once, sorted by id.
    std::vector<Hit> Visible() const;
    /// `root` and everything it needs, dependencies first (each id once; cycles are fine; ids no layer has are left out).
    std::vector<AssetId> Closure(AssetId root) const;
    /// Read on `jobs` (or the engine's jobs) and call `done` on the worker thread with the bytes or the reason.
    void ReadAsync(AssetId id, std::function<void(std::vector<std::uint8_t>&& bytes, const std::string& why)> done,
                   JobSystem* jobs = nullptr) const;

private:
    std::vector<std::shared_ptr<const Package>> m_layers;
    std::uint64_t m_stackHash = 0;
};

// ---- packages.manifest: which packages a game has, so an update fetches only the ones that changed ------------

struct ManifestItem { std::string file; std::uint64_t size = 0, hash = 0; };
/// "okaypkg-manifest 1" then one line per package in mount order: file name, size, Hash64 of the whole file (hex).
std::string WriteManifest(const std::vector<ManifestItem>& items);
/// Strict: any line that does not fit is refused (`why`). File names must be plain names ending in ".okaypkg".
bool ParseManifest(std::string_view text, std::vector<ManifestItem>& out, std::string& why);
/// The items of `wanted` that `have` lacks or has with another size or hash: what an update downloads.
std::vector<ManifestItem> ChangedPackages(const std::vector<ManifestItem>& have, const std::vector<ManifestItem>& wanted);
/// Hash64 (seed 0) of a whole file, read in pieces.
bool HashFile(const std::string& path, std::uint64_t& hash, std::uint64_t& size, std::string& why);

// ---- Patches -----------------------------------------------------------------------------------------------

struct PatchSource { std::string name, file; std::vector<std::string> dependencies; std::uint8_t kind = 0; };
/// Fill `writer` with what turns `current` into `wanted`: every entry that is new or whose bytes differ, and a removal
/// for every visible entry that `wanted` no longer has. Unchanged files are hashed, not copied. Write it with
/// WriteOptions::baseStackHash = current.StackHash(). `changed` counts the entries added (not the removals).
bool MakePatch(const PackageStack& current, const std::vector<PatchSource>& wanted, PackageWriter& writer,
               std::size_t* changed, std::string& why);

// ---- A game's data folder -------------------------------------------------------------------------------------

/// The packages of a game's data folder: those packages.manifest lists, in order, else game.okaypkg alone. False with
/// `why` when a listed package is missing, has another size than the manifest says, or does not open; true with an
/// empty stack when the folder has no packages (a game exported with loose files).
bool MountFolder(const std::string& dataDir, PackageStack& out, std::string& why, const Limits& limits = {});

struct MaterializeStats { std::size_t written = 0, kept = 0, removed = 0, loose = 0; double seconds = 0.0; };
/// Make `viewDir` a folder of the stack's visible files (for the parts of the engine that open files by path): an entry
/// is written only when the folder does not already hold that version of it (a state file remembers what was written,
/// with each file's size and time, so a later start with the same packages writes nothing). Files of earlier versions
/// that are no longer in the stack are deleted. Loose regular files directly in `dataDir` (not packages, not the
/// manifest) are copied too, unless an entry has the same name. Needs a package with names. Every write goes through
/// "<file>.part" and every name is checked with okay::pathsafety, so nothing is written outside `viewDir`.
bool Materialize(const PackageStack& stack, const std::string& dataDir, const std::string& viewDir,
                 MaterializeStats* stats, std::string& why);

/// What VerifyView found: names are package names ('/'-separated, relative to the view).
struct ViewCheck {
    std::size_t checked = 0, restored = 0;
    std::vector<std::string> changed;      ///< a file whose bytes are not the package's
    std::vector<std::string> missing;      ///< a file the packages have and the view lacks
    std::vector<std::string> unexpected;   ///< a program file (.exe, .dll, .so ...) in the view that no package has
    bool Clean() const { return changed.empty() && missing.empty() && unexpected.empty(); }
    /// One plain line per finding, as anti-cheat reports a changed shipped file.
    std::vector<std::string> Problems() const;
};
/// Anti-cheat's check of the view (docs/engine/asset_packages.md, "Anti-cheat"): every visible entry's file in `viewDir`
/// is compared byte for byte with the entry read from its package (the package file itself is in the anti-cheat list
/// and checked with SHA-256 first; the per-entry Hash64 is not trusted against a deliberate forgery, so bytes are
/// compared). With `restore`, a changed or missing file is written again from the package and recorded in the view's
/// state, so the next start keeps it. False (and `why`) only when the check itself cannot run.
bool VerifyView(const PackageStack& stack, const std::string& viewDir, bool restore, ViewCheck& out, std::string& why);

// ---- Packing an exported game's data folder (engine/src/Core/AssetPackageBuild.cpp) -----------------------------

struct PackFolderOptions {
    /// Top-level names in the data folder (files or folders) that stay loose: StreamingAssets (opened by name at run
    /// time), build.json (read by --build-info), manifest.json (the anti-cheat file list). Packages and the manifest
    /// are never packed.
    std::vector<std::string> keepLoose = {"StreamingAssets", "build.json", "manifest.json"};
    WriteOptions write;
    bool dependencies = true;   ///< record what each asset uses (the asset database's reference scanners)
    bool removeLoose = true;    ///< delete the packed loose files once the package is written and verified
};

struct PackFolderResult {
    WriteStats stats;
    std::vector<std::string> packed;       ///< package names of the files packed, sorted
    std::size_t dependencyLinks = 0;       ///< dependencies recorded
    std::string packagePath, manifestPath;
};

/// Pack every file of an exported game's data folder (except `keepLoose`) into `<dataDir>/<packageFile>` and write
/// `<dataDir>/packages.manifest` listing it. The package is read back and fully verified before any loose file is
/// deleted; if the cache gave a bad compressed copy the package is written again without the cache. False (and `why`,
/// the loose files untouched) on any failure.
bool PackFolder(const std::string& dataDir, const std::string& packageFile, const PackFolderOptions& options,
                PackFolderResult& result, std::string& why);

} // namespace assetpkg
} // namespace okay
