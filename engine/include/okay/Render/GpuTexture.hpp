#pragma once
// ---------------------------------------------------------------------------
// How a texture goes to the GPU when it comes as block-compressed data (okay::gputex). Pure logic, no GL calls, so the
// choice is tested headless; the OpenGL renderer does the calls.
//
//   * Where the data comes from (GpuTextureSource): a texture path ending in .dds is read as it is; a picture whose
//     import settings say `compress` gets a compressed copy, built once by our own encoder (okay::tex) and kept in the
//     import cache `<folder>/.okaycache/<file name>.dds`. Beside it, `<file name>.dds.src` records what it was made
//     from (the size and an FNV-1a 64 hash of the picture and of its .okaymeta); any change rebuilds it, whatever the
//     file times say. A game build writes the copy next to the shipped picture (ShipCompressedCopy), so a built game
//     does not encode at run time. Any other picture returns false and goes the ordinary RGBA8 way, as before.
//   * Whether the GPU takes it compressed (PlanGpuTexture): BC1/BC2/BC3 need S3TC (GL_EXT_texture_compression_s3tc),
//     BC4/BC5 need RGTC (core since OpenGL 3.0, or GL_ARB/EXT_texture_compression_rgtc). The top level must be a
//     multiple of 4 on both sides (older drivers refuse other sizes). Otherwise every level is decoded on the CPU
//     (okay::tex::DecodeBc) and uploaded as RGBA8, which looks the same.
// The GL enum numbers are from the extension specifications (EXT_texture_compression_s3tc, ARB_texture_compression_rgtc).
// ---------------------------------------------------------------------------
#include "okay/Texture/Dds.hpp"

#include <string>
#include <vector>

namespace okay::gputex {

struct Caps {
    bool s3tc = false;   ///< BC1, BC2, BC3
    bool rgtc = false;   ///< BC4, BC5
};

/// From the GL_VERSION string ("4.5 (Core Profile) Mesa 23.2", "OpenGL ES 3.0 ...") and the extension names.
Caps CapsFrom(const char* version, const std::vector<std::string>& extensions);

constexpr unsigned kGlRgba8 = 0x8058;
constexpr unsigned kGlCompressedRgbaS3tcDxt1 = 0x83F1;
constexpr unsigned kGlCompressedRgbaS3tcDxt3 = 0x83F2;
constexpr unsigned kGlCompressedRgbaS3tcDxt5 = 0x83F3;
constexpr unsigned kGlCompressedRedRgtc1 = 0x8DBB;
constexpr unsigned kGlCompressedRgRgtc2 = 0x8DBD;

/// The compressed GL internal format for a DDS format (0 for RGBA8). BC1 uses the RGBA variant so its transparent
/// texels stay transparent, as our CPU decoder gives them.
unsigned GlCompressedFormat(tex::DdsFormat f);
/// Whether the caps let this format go up compressed.
bool Supported(const Caps& caps, tex::DdsFormat f);

struct Level {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bytes;   ///< compressed blocks, or RGBA8 rows
};

struct Plan {
    bool compressed = false;           ///< upload with glCompressedTexImage2D
    unsigned glFormat = kGlRgba8;      ///< the compressed internal format, or GL_RGBA8
    tex::DdsFormat source = tex::DdsFormat::RGBA8;
    std::vector<Level> levels;         ///< level 0 first
    std::string note;                  ///< why it was decoded on the CPU ("" when compressed or plain RGBA8)
};

/// Decide and prepare the levels. False only for an empty or inconsistent image.
bool PlanUpload(tex::DdsImage img, const Caps& caps, Plan* out);

/// The import cache copy of a picture: `<folder>/.okaycache/<file name>.dds`.
std::string CompressedCachePath(const std::string& imagePath);

/// The compressed copy of a picture, from the cache when its record matches the picture and its .okaymeta, else built
/// (honouring the picture's maxSize, sRGB and mipmaps import settings) and written to the cache (a read-only folder
/// only skips the write). `rebuilt` says which happened.
bool LoadCompressedCopy(const std::string& imagePath, tex::DdsImage* out, std::string* error = nullptr, bool* rebuilt = nullptr);

/// For a game build: when the picture `sourceImage` asks for `compress`, write its compressed copy into the import
/// cache next to `shippedImage` (its copy in the build, already copied together with its .okaymeta), so the game loads
/// it instead of encoding at run time. True when a copy was written. False when the picture does not ask for one
/// (`error` left empty) or it could not be built or written (`error` says why).
bool ShipCompressedCopy(const std::string& sourceImage, const std::string& shippedImage, std::string* error = nullptr);

/// The DDS data a GPU renderer should upload for a texture name: the .dds file itself, or the compressed copy when the
/// picture's import settings say `compress`. False (and `out` untouched) for everything else, and for registered
/// ("@...") or procedural ("proc:...") pictures; `error` says why when a file was tried and failed.
bool Source(const std::string& name, tex::DdsImage* out, std::string* error = nullptr);

} // namespace okay::gputex
