#pragma once
// Asset thumbnails for an asset browser: a cache keyed by GUID + content hash with an LRU
// in-memory layer and an optional folder of PNGs on disk, fed by pluggable generators
// (`render(path) -> Image`). PNG/JPG and the other image types get a real thumbnail
// (box-filter downscale); everything else gets a placeholder tile. No UI here.
// Not thread-safe. See docs/asset_database.md.
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Graphics/Image.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// Shrinks `src` so its longest side is at most `maxSide` (area-weighted box filter, colour
/// averaged by alpha so transparent edges do not darken). Never enlarges: a small image is
/// returned as it is. An invalid source gives an empty image.
Image BoxDownscale(const Image& src, int maxSide);

/// A placeholder tile for assets without a thumbnail: a colour per kind, a border and the
/// extension (upper case, up to 4 letters) in the 8x8 font. `error` tints it red.
Image MakePlaceholderThumbnail(AssetType kind, const std::string& label, int size, bool error = false);

class TtfFont;

/// A tile that shows a font: a dark card with a large "Aa" and a line of sample text, drawn with
/// our own font code (TtfFont) straight into the pixels, so it needs no window or GPU. A font
/// with no Latin letters (a CJK-only face) shows the first of a few probe samples it can draw.
/// An invalid font, or one that can draw none of them, gives an empty Image.
Image RenderFontThumbnail(const TtfFont& font, int size);
/// The same from a file; a missing, unreadable, over 32 MB or non-font file gives an empty Image
/// and sets `error`.
Image RenderFontThumbnailFile(const std::string& absolutePath, int size, std::string* error = nullptr);

/// A generated tile for a data asset (okaydata, okayitems, okayloc, json, txt, bin...): a page
/// whose rows are as long as the first lines of the file (text), or a grid of cells shaded by the
/// first bytes (binary), with the extension below. The same file always gives the same tile.
/// A missing file gives an empty Image and sets `error`.
Image MakeDataThumbnail(const std::string& absolutePath, int size, std::string* error = nullptr);

/// Turns an asset file into a picture. `Render` returns an invalid (empty) Image when it
/// can not; it may return any size, the cache scales it down.
class ThumbnailGenerator {
public:
    virtual ~ThumbnailGenerator() = default;
    virtual bool Handles(const std::string& extension) const = 0;   // lower case, no dot
    virtual Image Render(const std::string& absolutePath, std::string* error) = 0;
};

/// png jpg jpeg bmp tga gif psd hdr, through okay::Image::Load.
class ImageThumbnailGenerator final : public ThumbnailGenerator {
public:
    bool Handles(const std::string& extension) const override;
    Image Render(const std::string& absolutePath, std::string* error) override;
};

/// wav ogg mp3 flac: the whole clip drawn as a min/max waveform (decoded through AudioClip::Load). A file over 64 MB is
/// not decoded just for a picture; it keeps the placeholder.
class WaveformThumbnailGenerator final : public ThumbnailGenerator {
public:
    bool Handles(const std::string& extension) const override;
    Image Render(const std::string& absolutePath, std::string* error) override;
};

/// ttf otf: RenderFontThumbnailFile at `size` pixels.
class FontThumbnailGenerator final : public ThumbnailGenerator {
public:
    explicit FontThumbnailGenerator(int size = 128) : m_size(size) {}
    bool Handles(const std::string& extension) const override;
    Image Render(const std::string& absolutePath, std::string* error) override;
private:
    int m_size;
};

/// The data extensions (AssetType::Data): MakeDataThumbnail at `size` pixels.
class DataThumbnailGenerator final : public ThumbnailGenerator {
public:
    explicit DataThumbnailGenerator(int size = 128) : m_size(size) {}
    bool Handles(const std::string& extension) const override;
    Image Render(const std::string& absolutePath, std::string* error) override;
private:
    int m_size;
};

class ThumbnailCache {
public:
    struct Options {
        int size = 128;                        ///< longest side of a thumbnail, pixels
        std::string diskDir;                   ///< PNG cache folder; empty = memory only
        std::size_t maxEntries = 512;          ///< LRU limits (memory layer)
        std::size_t maxBytes = 64u << 20;
        std::string tag = "1";                 ///< bump to invalidate every disk entry (generator changed)
        bool imageGenerator = true;            ///< register ImageThumbnailGenerator
        bool audioGenerator = true;            ///< register WaveformThumbnailGenerator
        bool fontGenerator = true;             ///< register FontThumbnailGenerator (a sample string in the font)
        bool dataGenerator = true;             ///< register DataThumbnailGenerator (a generated page tile)
    };
    struct Stats {
        std::size_t memoryHits = 0, diskHits = 0, generated = 0, placeholders = 0, failures = 0, evictions = 0, diskWrites = 0;
    };
    enum class Source { Memory, Disk, Generated, Placeholder };
    struct Thumb {
        std::shared_ptr<const Image> image;    ///< never null; stays valid after eviction
        Source source = Source::Placeholder;
        bool placeholder = false;              ///< not a picture of the asset (no generator, or it failed)
        bool failed = false;                   ///< a generator was tried and could not render it
    };

    ThumbnailCache() : ThumbnailCache(Options()) {}
    explicit ThumbnailCache(Options options);

    /// Generators are asked newest first, so a custom one can override the built-in.
    void AddGenerator(std::shared_ptr<ThumbnailGenerator> generator);
    /// Convenience: `render(absolutePath) -> Image` for files with these extensions.
    void AddGenerator(const std::vector<std::string>& extensions, std::function<Image(const std::string& path)> render);

    /// The thumbnail for an asset at `absolutePath` whose content hash is `contentHash`.
    /// A changed file has a new hash, so the old entry is never served.
    Thumb Get(const Guid& guid, std::uint64_t contentHash, const std::string& absolutePath, AssetType kind);
    /// Same, reading path, hash and kind from the database. A placeholder for an unknown GUID.
    Thumb Get(const AssetDatabase& db, const Guid& guid);

    bool InMemory(const Guid& guid, std::uint64_t contentHash) const;
    bool OnDisk(const Guid& guid, std::uint64_t contentHash) const;
    std::string DiskPathFor(const Guid& guid, std::uint64_t contentHash) const;

    /// Forget one asset (memory and disk), e.g. after it was deleted.
    void Invalidate(const Guid& guid);
    void Clear();            ///< memory layer only
    void ClearDisk();        ///< delete every file in the cache folder
    std::size_t MemoryCount() const { return m_lru.size(); }
    std::size_t MemoryBytes() const { return m_bytes; }
    const Stats& GetStats() const { return m_stats; }
    void ResetStats() { m_stats = Stats(); }
    const Options& GetOptions() const { return m_opt; }

private:
    struct Key {
        Guid guid;
        std::uint64_t hash = 0;
        bool operator<(const Key& o) const { return guid != o.guid ? guid < o.guid : hash < o.hash; }
    };
    struct Entry { Key key; std::shared_ptr<const Image> image; bool placeholder = false, failed = false; std::size_t bytes = 0; };
    using List = std::list<Entry>;

    Thumb Insert(const Key& key, std::shared_ptr<const Image> image, bool placeholder, bool failed, Source source);
    std::shared_ptr<const Image> Placeholder(AssetType kind, const std::string& ext, bool error);
    void Evict();
    void RemoveStaleOnDisk(const Guid& guid, const std::string& keepFile);

    Options m_opt;
    std::vector<std::shared_ptr<ThumbnailGenerator>> m_generators;
    List m_lru;                                // front = most recently used
    std::map<Key, List::iterator> m_index;
    std::size_t m_bytes = 0;
    std::map<std::string, std::shared_ptr<const Image>> m_placeholders;
    Stats m_stats;
};

} // namespace okay
