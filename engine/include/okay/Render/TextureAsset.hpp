#pragma once
// ---------------------------------------------------------------------------
// One texture asset for every renderer (okay::TextureAsset, okay::TextureHandle, okay::textures).
//
//  * A picture is decoded once, through AssetManager (Core/AssetManager.hpp), into a TextureAsset: the RGBA8 pixels
//    with the picture's own import settings applied (largest side), the settings themselves (TextureSettings, read
//    from `<picture>.okaymeta`), and a `serial` number that is new for every decode.
//  * A TextureHandle is a cheap, copyable, reference-counted pointer to it. The asset is freed when the last handle
//    goes (AssetManager only keeps weak references).
//  * The renderers (software, OpenGL, Direct3D 11, Direct3D 12) ask `textures::ForRender(name)`: the library keeps one
//    handle per name they draw with. Each renderer keeps only its own GPU object for it, and uploads again when the
//    asset's `serial` changes (a hot reload, a reimport, a painted terrain splat), instead of flushing every texture.
//  * Hot reload: `textures::Pump()` once a frame (the player and the editor do) re-reads a picture whose file changed on
//    disk, on AssetManager's worker thread, and moves the new pixels into the same asset, so handles stay valid.
//  * Memory: every live asset is counted in MemoryTracker::Global() under "Textures" (count and bytes, base level plus
//    a CPU mip chain when one was built). `textures::GetStats()` and `textures::Report()` list them; a budget
//    (OKAY_TEXTURE_BUDGET_MB, default 1024) logs a warning when crossed, and while over it the library lets go of the
//    textures no renderer has drawn for a while.
//  * Mip chains (`TextureAsset::Mips()`) are built by our own filters in okay::tex (Texture/Mip.hpp): box or Kaiser,
//    in linear light for colour pictures (sRGB), renormalised for normal maps, so a far-away checker keeps its
//    brightness instead of going dark.
// Main thread only, except the decode itself (the loader runs on AssetManager's worker for hot reload). This header
// pulls in no <mutex> / <thread> (MinGW win32 thread model, CLAUDE.md rule 21a); the AssetManager use is in the .cpp.
// ---------------------------------------------------------------------------
#include "okay/Graphics/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// Everything a picture's import settings say about how it is decoded, filtered and stored. Stored in the picture's
/// `.okaymeta` (`importer texture` block). The first six are ImportSettings' own texture keys; the rest are texture-only
/// keys kept in that block too (ImportSettings round-trips them as extra keys): `anisotropy`, `normalMap`, `mipFilter`,
/// `compression`.
struct TextureSettings {
    enum Filter { Point = 0, Bilinear = 1, Trilinear = 2 };
    enum Wrap { Repeat = 0, Clamp = 1, Mirror = 2 };
    enum MipFilter { MipBox = 0, MipKaiser = 1 };
    enum Compression { CompressAuto = 0, CompressBC1 = 1, CompressBC3 = 2, CompressBC4 = 3, CompressBC5 = 4 };

    bool sRGB = true;          ///< colour data (gamma); off for normal maps and data maps (roughness, masks)
    int  filter = Trilinear;
    int  wrap = Repeat;
    int  maxSize = 0;          ///< longest side in pixels; 0 = no limit
    bool mipmaps = true;
    bool compress = false;     ///< block-compressed GPU copy (GpuTexture.hpp)
    int  anisotropy = 8;       ///< 1 = off; 2..16 = anisotropic filtering level (applies with Trilinear)
    bool normalMap = false;    ///< RGB is a tangent-space normal: linear, mips renormalised, auto compression BC5
    int  mipFilter = MipBox;
    int  compression = CompressAuto;   ///< with `compress`: auto (BC1 opaque, BC3 alpha, BC5 normal maps) or one format

    /// Colour data the GPU should treat as sRGB (sRGB on and not a normal map).
    bool IsColour() const { return sRGB && !normalMap; }
    bool operator==(const TextureSettings& o) const;
    bool operator!=(const TextureSettings& o) const { return !(*this == o); }
};

/// The settings of a picture from its `.okaymeta` (one read of the file). Defaults for a missing file or key, and for
/// registered ("@...") and procedural ("proc:...") names. Values out of range are pulled into range.
TextureSettings ReadTextureSettings(const std::string& path);
/// The texture-only keys as `set` lines' key/value pairs ("anisotropy" "8", ...), for the editor to keep in an
/// ImportSettings' extra keys. ImportSettings' own keys (srgb, filter, wrap, maxSize, mipmaps, compress) are not listed.
std::vector<std::pair<std::string, std::string>> TextureExtraKeys(const TextureSettings& s);
/// Read the texture-only keys back from such pairs (unknown keys are ignored; missing ones keep `s`'s values).
void ApplyTextureExtraKeys(TextureSettings& s, const std::vector<std::pair<std::string, std::string>>& keys);
const char* TextureCompressionName(int compression);   // "auto", "bc1", ...
const char* TextureMipFilterName(int mipFilter);       // "box", "kaiser"

/// A mip chain for `base`, level 0 (a copy) first, down to 1x1, with the settings' filter (box or Kaiser), in linear
/// light for colour pictures and renormalised for normal maps. Just level 0 when mipmaps are off. For a GPU upload that
/// does not need to keep the chain; TextureAsset::Mips() keeps one.
std::vector<Image> BuildMipChain(const Image& base, const TextureSettings& settings);

/// One decoded picture, shared by every renderer.
class TextureAsset {
public:
    TextureAsset() = default;
    ~TextureAsset();
    TextureAsset(const TextureAsset&) = delete;
    TextureAsset& operator=(const TextureAsset&) = delete;
    /// Moves keep the memory count right: the target gives up its own count and takes the source's (a hot reload moves
    /// a fresh decode into the live asset).
    TextureAsset(TextureAsset&& o) noexcept;
    TextureAsset& operator=(TextureAsset&& o) noexcept;

    std::string path;             ///< the name it was loaded under (file path, "proc:...", "@...")
    Image image;                  ///< level 0, RGBA8, import `maxSize` applied; empty when the load failed
    TextureSettings settings;
    std::uint64_t serial = 0;     ///< new for every decode or change (never 0 once loaded)
    bool registered = false;      ///< made in memory (RegisterTexture), not read from a file

    bool Valid() const { return image.Valid(); }
    int  Width() const { return image.Width(); }
    int  Height() const { return image.Height(); }
    /// The mip chain, level 0 (a copy of `image`) first, down to 1x1, built on first use with our own filter (box or
    /// Kaiser, linear light for colour, renormalised normal maps). Just level 0 when `settings.mipmaps` is off.
    const std::vector<Image>& Mips() const;
    /// The same chain as a shared pointer, so a renderer can keep it while the asset rebuilds or reloads.
    std::shared_ptr<const std::vector<Image>> SharedMips() const;
    bool MipsBuilt() const { return m_mips && m_mipsSerial == serial; }
    int  MipLevels() const { return MipsBuilt() ? static_cast<int>(m_mips->size()) : 1; }
    /// CPU bytes held: the base level plus the mip chain when built.
    std::size_t Bytes() const;
    /// The pixels changed in place (a painted terrain splat): a new serial, the mip chain dropped, the count updated.
    void Touch();
    /// Count (or recount) this asset's bytes in MemoryTracker "Textures" and the library's list.
    void Track() const;

private:
    mutable std::shared_ptr<std::vector<Image>> m_mips;
    mutable std::uint64_t m_mipsSerial = 0;
    mutable std::int64_t m_tracked = -1;   ///< bytes counted in MemoryTracker; -1 = not counted
    void Untrack() const;
};

/// A counted reference to a TextureAsset. Copy it freely; the asset lives while any copy does.
class TextureHandle {
public:
    TextureHandle() = default;
    explicit TextureHandle(std::shared_ptr<TextureAsset> p) : m_p(std::move(p)) {}
    bool Valid() const { return m_p && m_p->Valid(); }
    explicit operator bool() const { return Valid(); }
    const TextureAsset* Get() const { return m_p.get(); }
    const TextureAsset* operator->() const { return m_p.get(); }
    const Image* GetImage() const { return Valid() ? &m_p->image : nullptr; }
    std::uint64_t Serial() const { return m_p ? m_p->serial : 0; }
    const std::string& Path() const { static const std::string e; return m_p ? m_p->path : e; }
    long UseCount() const { return m_p ? m_p.use_count() : 0; }
    void Reset() { m_p.reset(); }
    bool operator==(const TextureHandle& o) const { return m_p == o.m_p; }
    bool operator!=(const TextureHandle& o) const { return m_p != o.m_p; }
private:
    friend struct TextureLibraryAccess;
    std::shared_ptr<TextureAsset> m_p;
};

namespace textures {

/// Decode a texture from a file, a "proc:" name or a registered "@" name, or share the one already loaded. The handle
/// is empty (and `error` set) when it cannot be loaded; failures are not cached.
TextureHandle Load(const std::string& path, std::string* error = nullptr);
/// The asset already loaded under this name (by anyone), or an empty handle. Never loads.
TextureHandle Find(const std::string& path);
/// Decode the file again now, into the same asset (handles stay valid, the serial changes). For a changed import
/// setting or a reimport. False when nothing is loaded under that name or the new decode failed (the old pixels stay).
bool Reload(const std::string& path);

/// An in-memory picture under a name (conventionally "@..."), held by the library until Unregister. Registering the
/// same name again replaces the pixels in the same asset (new serial).
TextureHandle Register(const std::string& name, Image img);
void Unregister(const std::string& name);
/// The registered picture's pixels, to change in place; call TouchRegistered(name) afterwards so renderers upload them.
/// Creates an empty registration when there is none.
Image& RegisteredImage(const std::string& name);
void TouchRegistered(const std::string& name);

/// What the renderers draw with: the asset for a texture name, loaded on first use and held by the library, or null
/// when it cannot be loaded (that answer is remembered until the name is dropped, the file changes or InvalidateAll).
/// Main thread only. Cheap: one hash lookup.
const TextureAsset* ForRender(const std::string& name);
/// While a ReadOnlyScope is alive (the software renderer's parallel band render), ForRender only looks names up: it
/// loads nothing and changes nothing, so several threads may call it. A name not loaded yet gives null there.
bool ReadOnly();
struct ReadOnlyScope {
    ReadOnlyScope();
    ~ReadOnlyScope();
    ReadOnlyScope(const ReadOnlyScope&) = delete;
    ReadOnlyScope& operator=(const ReadOnlyScope&) = delete;
};
/// Let go of the library's own reference to a name (the asset lives on while other handles exist).
void Drop(const std::string& name);
/// Drop every file texture the library holds for the renderers, re-decode the ones still alive elsewhere, and keep
/// registered pictures. For "the import settings of some picture changed".
void InvalidateAll();
/// Bumped whenever the library lets go of names (Drop, InvalidateAll, a budget trim): renderers that cache by name
/// compare it to forget names cheaply.
unsigned Generation();

/// Once a frame on the main thread: hot reload (when on), the frame counter for idle tracking, the budget check and
/// the memory figures. Returns the number of textures reloaded this call.
int Pump();
/// Watch loaded picture files for changes (checked at most every `intervalSeconds`). Off by default; the editor turns
/// it on, a game with OKAY_TEXTURE_HOT_RELOAD=1.
void SetHotReload(bool enabled, double intervalSeconds = 0.5);
bool HotReload();

/// Background loading: ForRender starts the decode on AssetManager's worker and returns null (the renderers draw the
/// surface untextured) until it is done, instead of stopping the frame for a big picture. Off by default;
/// OKAY_TEXTURE_ASYNC=1 turns it on. Finished loads are picked up by ForRender (Pump runs the callbacks).
void SetAsyncLoading(bool enabled);
bool AsyncLoading();

struct Stats {
    std::size_t count = 0;          ///< live texture assets (all of them, not only the renderers')
    std::size_t bytes = 0;          ///< their CPU bytes (base levels plus built mip chains)
    std::size_t rendererHeld = 0;   ///< names the renderers' cache holds
    std::size_t loading = 0;        ///< background loads not finished yet
    std::size_t budgetBytes = 0;
    bool overBudget = false;
    std::uint64_t reloads = 0;      ///< hot reloads and Reload calls since start
    std::uint64_t trimmed = 0;      ///< textures let go of because the budget was exceeded
};
Stats GetStats();
struct Row {
    std::string path;
    int width = 0, height = 0, mipLevels = 1;
    std::size_t bytes = 0;
    long refs = 0;                  ///< handles alive, the library's own included
    bool registered = false;
    bool sRGB = true, normalMap = false, compress = false;
};
/// Every live texture, largest first.
std::vector<Row> Report();
/// One line for the F3 overlay: "textures 12  38.4 MB / 1024 MB" (+ "  OVER BUDGET").
std::string OverlayLine();

/// Budget in bytes (0 = no budget). Default from OKAY_TEXTURE_BUDGET_MB, else 1024 MB.
void SetBudgetBytes(std::size_t bytes);
std::size_t BudgetBytes();
/// While over budget, Pump lets go of textures the renderers have not drawn for this many Pump calls (default 300).
void SetIdleFrames(int frames);

/// Whether the renderers' cache holds this name now. A GPU renderer that sees Generation() change frees its own copies
/// of names no longer held (that is how a trimmed or dropped texture leaves video memory too).
bool IsHeld(const std::string& name);

/// For tests: forget everything the library holds (registered pictures too) and reset the counters.
void ResetForTests();

} // namespace textures
} // namespace okay
