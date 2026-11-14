#pragma once
// World chunks: one cell of a big scene, stored as text (docs/foundations/world_streaming.md).
//
// A chunk is a list of top-level object TREES (an object and everything below it; a tree never
// spans two chunks). Each tree is the engine's own prefab text (SceneSerializer::SerializeObject) plus
// the list of persistent ids (guids) of its objects, because the scene reader gives a merged object a
// fresh guid and streaming needs the same id after an unload and load.
//
// File layout (all text, '\n' line ends):
//     okaychunk 1
//     cell 3 -2 0            (or "cell persistent" for the always-loaded set)
//     roots 2
//     sum 00ab12cd34ef5678   (FNV-1a 64 of the cell line, the roots line (both with their '\n') and
//                             everything after this line, so a damaged coordinate is caught too)
//     root <guidCount> <textBytes>
//     <guid> <guid> ...
//     <textBytes bytes of object text>
//     root ...
// Every size is checked against what is left in the file, so a damaged or hostile file fails with a
// message and never reads out of bounds, loops or allocates more than the file itself holds.
#include "okay/Scene/WorldGrid.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace okay {
class Scene;
class GameObject;
}

namespace okay::worldpart {

constexpr int kChunkFormatVersion = 1;
/// A chunk text larger than this is refused (256 MiB).
constexpr std::size_t kMaxChunkBytes = 256u * 1024u * 1024u;
/// The name of the always-loaded chunk in a store.
constexpr const char* kPersistentChunkName = "persistent";
/// The tag that keeps a top-level object (or anything below it) out of the cells.
constexpr const char* kAlwaysLoadedTag = "AlwaysLoaded";

std::uint64_t Fnv1a64(const std::string& data);

/// One object tree.
struct ChunkRoot {
    std::vector<std::string> guids;   ///< guid of each written object, in tree (depth-first) order
    std::string text;                 ///< SceneSerializer::SerializeObject text
};

struct Chunk {
    bool persistent = false;          ///< the always-loaded set (cell is unused)
    CellCoord cell;
    std::vector<ChunkRoot> roots;
    /// "persistent" or the WorldGrid::CellName of `cell`.
    std::string Name() const;
};

std::string EncodeChunk(const Chunk& chunk);
/// Parse and verify a chunk. False with `error` on anything wrong (bad header, version, checksum, a
/// size that does not fit, a bad or repeated guid, text left over). `out` is left empty on failure.
bool DecodeChunk(const std::string& text, Chunk& out, std::string* error = nullptr);

/// Write one live object tree as a chunk root (its objects must have their guids). False with `error`
/// when the tree cannot be written (it is destroyed, or the text and the tree disagree).
bool CaptureRoot(const GameObject& root, ChunkRoot& out, std::string* error = nullptr);

enum class ChunkApply {
    Ok,       ///< every tree was created, with the chunk's guids
    Busy,     ///< an object with one of the guids is still in the scene waiting to be destroyed: try next frame
    Failed,   ///< the chunk is bad or its guids already exist; the scene is as it was
};
/// Create the chunk's trees in `scene` (all or nothing) with their saved guids. `rootGuids` receives
/// each created root's guid, in chunk order. Objects Awake / Start on the scene's next update, as
/// with any Instantiate.
ChunkApply InstantiateChunk(Scene& scene, const Chunk& chunk, std::vector<std::string>* rootGuids = nullptr,
                            std::string* error = nullptr);

struct SplitOptions {
    /// An object with this tag, or with it anywhere below, stays in the always-loaded chunk.
    std::string alwaysLoadedTag = kAlwaysLoadedTag;
};

struct SplitResult {
    /// Chunk name (a CellName, or kPersistentChunkName) -> encoded chunk text. The persistent chunk is
    /// present only when it has objects.
    std::map<std::string, std::string> chunks;
    std::vector<CellCoord> cells;     ///< the cells that have a chunk, sorted
    int cellRoots = 0;                ///< top-level trees put in cells
    int persistentRoots = 0;          ///< top-level trees kept always loaded
    int outsideWorld = 0;             ///< of those, because the position is not a finite in-range number
    int skippedRoots = 0;             ///< trees the scene writer does not save (transient, generated): left out
};

/// Cut a scene into chunks: each top-level object goes to the cell holding its origin (world position),
/// objects marked always-loaded, carrying a StreamingSource, or with a position outside the world go
/// to the persistent chunk. Scene settings (name, gravity, fog, lighting) are not chunk data. False
/// with `error` for an invalid grid. The scene is not changed.
bool SplitScene(const Scene& scene, const WorldGrid& grid, const SplitOptions& options, SplitResult& out,
                std::string* error = nullptr);

/// Chunk storage. The streamer reads from it and never writes to it (changes go to an in-memory
/// overlay that a save keeps), so authored chunk files stay as authored.
class IChunkStore {
public:
    virtual ~IChunkStore() = default;
    /// The chunk text called `name`; false if there is none or it cannot be read.
    virtual bool Read(const std::string& name, std::string& out) const = 0;
    virtual bool Write(const std::string& name, const std::string& text) = 0;
    /// Every stored name, sorted.
    virtual std::vector<std::string> List() const = 0;
};

class MemoryChunkStore : public IChunkStore {
public:
    bool Read(const std::string& name, std::string& out) const override;
    bool Write(const std::string& name, const std::string& text) override;
    std::vector<std::string> List() const override;
    /// Put every chunk of a split into the store.
    void Add(const SplitResult& split);
private:
    std::map<std::string, std::string> m_chunks;
};

/// Chunks as "<dir>/<name>.okaychunk" files. Names may only hold a-z, 0-9, '_' and '-' (anything else
/// is refused, so a name can never leave the folder). A write goes to a temporary file first.
class DirectoryChunkStore : public IChunkStore {
public:
    explicit DirectoryChunkStore(std::string dir) : m_dir(std::move(dir)) {}
    bool Read(const std::string& name, std::string& out) const override;
    bool Write(const std::string& name, const std::string& text) override;
    std::vector<std::string> List() const override;
    static bool ValidName(const std::string& name);
private:
    std::string m_dir;
};

} // namespace okay::worldpart
