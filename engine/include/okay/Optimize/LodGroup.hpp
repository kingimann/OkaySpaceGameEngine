#pragma once
// ---------------------------------------------------------------------------
// LOD groups (feature map 27.1): plain data plus a pure selector.
//
// A LodGroup lists detail levels from best to worst. Each level carries the
// screen-relative height (the fraction of the viewport height the object
// covers) at or below which the group leaves that level for the next one:
//
//   levels[0].screenHeight = 0.50   LOD0 while the object covers >= 50 %
//   levels[1].screenHeight = 0.25   LOD1 while it covers >= 25 %
//   levels[2]                       last level, used down to cullScreenHeight
//   cullScreenHeight       = 0.02   below 2 % the object is not drawn (0 = never)
//
// SelectLod() is a pure function of the group, the viewing distance and a
// small view description. Hysteresis and cross-fade are handled by passing the
// previously chosen level back in, so the caller keeps one int per object.
//
// A LODGroup component is deliberately not part of this change: components
// have to be registered with the scene serializer and the component factory,
// which means edits outside the Optimize folder. This data class is what such
// a component would hold.
// ---------------------------------------------------------------------------
#include "okay/Optimize/MeshSimplify.hpp"
#include <string>
#include <vector>

namespace okay::optimize {

struct LodLevel {
    float screenHeight = 0.0f;   ///< leave this level below this screen-relative height (unused on the last level)
    int   meshIndex = 0;         ///< caller's payload: which mesh / asset this level draws
    int   triangles = 0;         ///< informational
    std::string name;            ///< informational ("LOD0")
};

enum class LodFadeMode { None, CrossFade };

struct LodGroup {
    std::vector<LodLevel> levels;     ///< ordered best (largest threshold) to worst
    float size = 1.0f;                ///< world size of the object (bounding sphere diameter)
    float cullScreenHeight = 0.0f;    ///< below this the object is culled (0 = never culled)
    float minPixelHeight = 0.0f;      ///< also cull below this many pixels tall on screen (0 = off)
    LodFadeMode fadeMode = LodFadeMode::None;
    /// Width of the cross-fade band as a fraction of a level's threshold: a level starts
    /// blending toward the next one once the object is within (1 + fadeWidth) of its threshold.
    float fadeWidth = 0.2f;
    /// Hysteresis as a fraction of a threshold: going to a worse level needs the object to
    /// shrink below threshold * (1 - h), coming back needs it to grow past threshold * (1 + h).
    float hysteresis = 0.1f;

    int  LevelCount() const { return static_cast<int>(levels.size()); }
    /// True when thresholds are strictly decreasing and in (0, 1], and the cull height is below the last.
    bool Valid() const;
};

/// What the camera looks like to the selector.
struct LodView {
    float viewportHeightPx = 1080.0f;
    float fovYDeg = 60.0f;
    /// Pixels covered by a one-unit tall object at distance one.
    float FovHeightPx() const;
};

struct LodSelection {
    int   level = 0;            ///< chosen level, or -1 when culled
    int   state = 0;            ///< feed this back as `previousLevel` next frame (LevelCount() = culled)
    bool  culled = false;
    int   fadeTo = -1;          ///< level being faded toward (CrossFade), -1 when none
    float fade = 0.0f;          ///< 0..1 weight of fadeTo
    float screenHeight = 0.0f;  ///< the screen-relative height that was evaluated
};

/// Screen-relative height of the group at `distance`. `bias` > 1 makes objects look bigger
/// (higher detail for longer), < 1 smaller (cheaper LODs sooner).
float LodGroupScreenHeight(const LodGroup& g, float distance, const LodView& view, float bias = 1.0f);

/// Pick the level for an object at `distance`. Pure: pass the previous frame's
/// `LodSelection::state` (or -1 for none) to get hysteresis and a stable cross-fade.
LodSelection SelectLod(const LodGroup& g, float distance, const LodView& view,
                       float bias = 1.0f, int previousLevel = -1);

/// The same selection from an already known screen-relative height. `cullHeight` < 0 uses
/// the group's own cullScreenHeight.
LodSelection SelectLodByHeight(const LodGroup& g, float screenHeight, int previousLevel = -1, float cullHeight = -1.0f);

/// Distance at which a level is left for the next one (without hysteresis).
float LodSwitchDistance(const LodGroup& g, int level, const LodView& view, float bias = 1.0f);

/// Build a group from a generated chain. `cullScreenHeight` 0 means never cull.
LodGroup MakeLodGroup(const LodChain& chain, float cullScreenHeight = 0.0f);

} // namespace okay::optimize
