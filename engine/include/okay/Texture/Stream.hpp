#pragma once
// ---------------------------------------------------------------------------
// Texture streaming planner (okay::tex). It extends optimize::PlanMips (Optimize/MipBudget.hpp, feature map 27.4):
// that function decides which mip each texture keeps resident from its on-screen size and a memory budget. This
// planner adds what a streaming system needs around it:
//   - on-screen size from camera distance: pixels = worldSize / (distance * 2 * tan(fov / 2)) * screenHeight,
//     taken as the maximum over every visible instance that uses the texture;
//   - remembering last frame's choice (the hysteresis input of PlanMips);
//   - the difference to last frame: which mips to load, which to drop, and how many bytes move;
//   - an optional per-frame upload cap: the most visible loads go first, the rest wait (the texture keeps its
//     previous, blurrier mip until a later frame). A texture with nothing resident is never held back (it would
//     pop in from nothing), so the cap limits sharpening, not first loads.
// Pure arithmetic, no GPU; the host (renderer) does the uploads.
// ---------------------------------------------------------------------------
#include "okay/Optimize/MipBudget.hpp"
#include <map>
#include <string>
#include <vector>

namespace okay::tex {

struct StreamCamera {
    float x = 0, y = 0, z = 0;
    float fovY = 1.0471976f;        ///< vertical field of view in radians (60 degrees)
    int   screenHeight = 1080;      ///< pixels
};

struct StreamTexture {
    int   id = 0;
    std::string name;
    int   width = 1, height = 1;
    int   mipCount = 0;             ///< 0 = full chain
    float bytesPerPixel = 4.0f;     ///< use BcBytesPerPixel() for compressed data
    float importance = 1.0f;
    bool  pinned = false;           ///< always mip 0 (UI, hero assets)
};

struct StreamInstance {
    int   textureId = 0;
    float x = 0, y = 0, z = 0;      ///< world position of the surface
    float worldSize = 1.0f;         ///< length of its longest side in world units
    float uvScale = 1.0f;           ///< times the texture repeats across that side
    bool  visible = true;           ///< false = culled by the host this frame
};

struct StreamOptions {
    float budgetBytes = 0.0f;       ///< 0 = unlimited resident memory
    float mipBias = 0.0f;
    int   minDimension = 4;
    float hysteresis = 0.25f;
    float uploadBytesPerFrame = 0.0f; ///< 0 = unlimited loads per plan
};

struct StreamRequest {
    int   id = 0;
    int   mip = 0;                  ///< base mip that should be resident after this plan
    int   desiredMip = 0;           ///< what the screen asked for (before budget and upload cap)
    int   previousMip = -1;         ///< resident before (-1 = nothing)
    float screenPixels = 0.0f;
    float loadBytes = 0.0f;         ///< bytes to upload (mips [mip, previousMip) or the whole chain)
    float evictBytes = 0.0f;        ///< bytes to drop (mips [previousMip, mip))
    bool  deferred = false;         ///< wanted a sharper mip but the upload cap held it back
};

struct StreamPlan {
    std::vector<StreamRequest> requests;      ///< same order as the textures passed in
    float residentBytes = 0.0f;
    float loadBytes = 0.0f, evictBytes = 0.0f;
    int   loads = 0, evictions = 0, deferred = 0;
    bool  overBudget = false;
};

/// On-screen size in pixels of something `worldSize` long at distance `dist` (0 when it cannot be seen).
float ProjectedPixels(float worldSize, float dist, float fovY, int screenHeight);

class StreamPlanner {
public:
    StreamPlan Plan(const StreamCamera& cam, const std::vector<StreamTexture>& textures,
                    const std::vector<StreamInstance>& instances, const StreamOptions& opt = StreamOptions());
    /// Forget what is resident (a level change, or a lost device).
    void Reset() { m_resident.clear(); }
    int ResidentMip(int id) const { auto it = m_resident.find(id); return it == m_resident.end() ? -1 : it->second; }
private:
    std::map<int, int> m_resident;
};

} // namespace okay::tex
