#pragma once
// ---------------------------------------------------------------------------
// The glue the user-facing tools share (okay-texture, the editor's Import Settings box): load a picture, build its
// mip chain, optionally compress it, write a .dds beside it; pack pictures into an atlas. Uses okay::Image for
// loading and PNG saving; everything else is okay::tex.
// ---------------------------------------------------------------------------
#include "okay/Texture/Atlas.hpp"
#include "okay/Texture/Bc.hpp"
#include "okay/Texture/Dds.hpp"
#include "okay/Texture/Mip.hpp"
#include <string>
#include <vector>

namespace okay { class Image; }

namespace okay::tex {

struct TextureBuildOptions {
    bool mips = true;               ///< build the whole mip chain
    MipOptions mip;                 ///< filter, sRGB, alpha coverage, normal map ...
    bool compress = false;          ///< block compress every level
    bool autoFormat = true;         ///< BC1 for opaque pictures, BC3 when any alpha is below 255 (normal maps: BC5)
    BcFormat format = BcFormat::BC1;///< used when autoFormat is off
    BcEncodeOptions encode;
};

/// Chooses the format `compress` would use for this picture.
BcFormat ChooseBcFormat(const std::uint8_t* rgba, int w, int h, bool normalMap);

/// Build a DDS image in memory from RGBA8 pixels.
bool BuildDds(const std::uint8_t* rgba, int w, int h, const TextureBuildOptions& opt, DdsImage* out, std::string* error);

/// `<name>.png` -> `<name>.dds` (or `<name>.mips.dds` when `suffixMips` is set).
std::string DdsPathFor(const std::string& imagePath, bool compressed);

/// Load an image file, build, write the .dds. Returns the written path in `outPath` (also when it was given).
bool BuildDdsFile(const std::string& imagePath, std::string* outPath, const TextureBuildOptions& opt, std::string* error);

/// Write one PNG per mip level: `<prefix>_0.png`, `<prefix>_1.png` ... Returns the number written (0 on failure).
int WriteMipPngs(const std::string& imagePath, const std::string& prefix, const MipOptions& opt, std::string* error);

/// Load the pictures, pack them (names = file names without folder and extension), write `<sheetPng>` and
/// `<manifestJson>`. Returns false with a message when anything fails (a picture that cannot be loaded, no room).
bool BuildAtlasFiles(const std::vector<std::string>& imagePaths, const std::string& sheetPng, const std::string& manifestJson,
                     const AtlasOptions& opt, std::string* error);

} // namespace okay::tex
