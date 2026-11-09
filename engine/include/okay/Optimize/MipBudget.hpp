#pragma once
// ---------------------------------------------------------------------------
// Texture mip streaming budget (feature map 27.4).
//
// PlanMips() picks, per texture, which mip level to keep resident:
//   1. the ideal mip from how big the texture appears on screen
//      (texels per pixel = 1 at that mip), with a bias and hysteresis;
//   2. if the total memory is over the budget, textures are demoted one mip at
//      a time, always the one that loses the least visible detail per byte
//      saved, until everything fits (or nothing can be demoted any more).
//
// A resident texture keeps its whole chain from the chosen mip down, so the
// bytes of mip m are the sum of mips m..last. Pure arithmetic, no GPU.
// ---------------------------------------------------------------------------
#include <vector>

namespace okay::optimize {

struct MipTexture {
    int   id = 0;
    int   width = 1, height = 1;
    int   mipCount = 0;             ///< 0 = full chain (down to 1x1)
    float bytesPerPixel = 4.0f;     ///< 4 = RGBA8, 0.5 = BC1, 1 = BC3 / BC7
    float screenPixels = 0.0f;      ///< largest on-screen size (pixels, longest side) of anything using it; 0 = not visible
    float uvScale = 1.0f;           ///< tiling: how many times the texture repeats across that size
    float importance = 1.0f;        ///< weight when the budget forces demotions
    int   previousMip = -1;         ///< last plan's choice, for hysteresis (-1 = none)
    bool  pinned = false;           ///< always the full resolution mip 0 and never demoted (UI, hero assets)
};

struct MipOptions {
    float budgetBytes = 0.0f;       ///< 0 = unlimited
    float mipBias = 0.0f;           ///< > 0 blurrier / cheaper, < 0 sharper
    int   minDimension = 4;         ///< never go below this size on the longest side (the resident floor)
    float hysteresis = 0.25f;       ///< fraction of a mip level a texture must move before its mip changes
};

struct MipChoice {
    int   id = 0;
    int   desiredMip = 0;           ///< what the screen size asked for
    int   mip = 0;                  ///< what the budget allows
    int   width = 1, height = 1;    ///< size at `mip`
    float bytes = 0.0f;             ///< resident bytes at `mip` (chain below included)
    float fullBytes = 0.0f;         ///< resident bytes at mip 0
    bool  demotedForBudget = false;
};

struct MipPlan {
    std::vector<MipChoice> choices; ///< same order as the input
    float totalBytes = 0.0f;
    float desiredBytes = 0.0f;      ///< what everything would use at its desired mip
    float fullBytes = 0.0f;         ///< what everything would use at mip 0
    float budgetBytes = 0.0f;
    int   demotions = 0;
    bool  overBudget = false;       ///< nothing left to demote and still above the budget
};

/// Number of mips in a full chain for a texture of this size.
int FullMipCount(int width, int height);
/// Resident bytes from `fromMip` down to the last mip.
float MipChainBytes(int width, int height, float bytesPerPixel, int fromMip, int mipCount);
/// The mip the screen size asks for (before budgeting), clamped to what the texture has.
int DesiredMip(const MipTexture& t, const MipOptions& opt = MipOptions());

MipPlan PlanMips(const std::vector<MipTexture>& textures, const MipOptions& opt = MipOptions());

} // namespace okay::optimize
