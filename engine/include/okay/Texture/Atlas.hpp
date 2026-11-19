#pragma once
// ---------------------------------------------------------------------------
// Rectangle atlas packer (okay::tex). Written from the MaxRects idea (a list of maximal free rectangles; place a
// rectangle with the "best short side fit" rule, cut every free rectangle it overlaps, drop free rectangles that
// lie inside another) as described in the public paper "A Thousand Ways to Pack the Bin" (J. Jylanki, 2010).
//
//  - `padding` empty pixels between neighbours, `bleed` pixels of edge colour repeated around each sprite
//    (keeps bilinear filtering and mip levels from pulling in a neighbour's colour).
//  - Optional 90 degree rotation, optional power-of-two sheet size, sheet grown from the smallest that fits.
//  - Deterministic: the same input always gives the same layout; ties are broken by name then by input index.
//  - A text manifest (JSON) with a fixed key order, so it diffs cleanly.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

namespace okay::tex {

struct AtlasItem { std::string name; int w = 0, h = 0; };

struct AtlasOptions {
    int  maxWidth = 4096, maxHeight = 4096;
    int  padding = 0;           ///< empty pixels between sprites (and their bleed)
    int  bleed = 0;             ///< repeated edge pixels around every sprite
    bool allowRotate = false;   ///< a sprite may be turned 90 degrees clockwise
    bool powerOfTwo = true;     ///< sheet width and height are powers of two
};

struct AtlasPlacement {
    int  item = 0;              ///< index into the input list
    std::string name;
    int  x = 0, y = 0;          ///< top-left of the sprite itself (bleed lies outside it)
    int  w = 0, h = 0;          ///< size as stored on the sheet (swapped when rotated)
    bool rotated = false;
};

struct AtlasResult {
    bool ok = false;            ///< every item was placed
    int  width = 0, height = 0; ///< sheet size
    std::vector<AtlasPlacement> placements;   ///< placed items, in input order
    std::vector<int> failed;    ///< input indices that did not fit or were invalid
    double occupancy = 0.0;     ///< sprite pixels / sheet pixels
};

AtlasResult PackAtlas(const std::vector<AtlasItem>& items, const AtlasOptions& opt = AtlasOptions());

struct AtlasImage { const std::uint8_t* rgba = nullptr; int w = 0, h = 0; };

/// Draw the sprites into a sheet (RGBA8, transparent where unused), with bleed. `images` is indexed like the packer's
/// input. Returns an empty vector when the result is empty or an image is missing / the wrong size.
std::vector<std::uint8_t> ComposeAtlasRGBA8(const AtlasResult& r, const std::vector<AtlasImage>& images, int bleed);

/// Stable JSON manifest: fixed key order, sprites sorted by name then input index, integers only.
std::string AtlasToJson(const AtlasResult& r, const std::string& imageName, const AtlasOptions& opt);

} // namespace okay::tex
