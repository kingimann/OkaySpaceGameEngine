#pragma once
// Turns the live particles of a scene into a flat, sorted list of screen shapes (quads and small polygons) that any
// renderer can draw: the editor (ImGui), the player (its 2D painter) and the headless test raster below. One
// implementation of the camera facing, streaks, flat / upright billboards, picture-sheet frames, ribbons, cubes,
// sorting, soft edges, lighting and the culling / LOD feed. Written from general projective-geometry and
// painter's-algorithm techniques; no other engine's code.
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Math/Mat4.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay {

class Scene;
class GameObject;

/// The camera and the screen rectangle the 3D picture covers.
struct ParticleView {
    Mat4  vp;                       // view-projection (clip = vp * (world, 1))
    Vec3  camPos{0, 0, 0};
    float x0 = 0.0f, y0 = 0.0f;     // top-left of the picture on screen, pixels
    float w = 1.0f, h = 1.0f;       // its size, pixels
    float maxRadius = 400.0f;       // a particle is never drawn bigger than this many pixels
    bool  useDepth = true;          // hide / soften behind solid geometry when a depth picture exists
    bool  frustumCull = true;       // skip systems wholly outside the picture (and tell them: SetCulled)
};

/// One thing to draw. Kind Quad: centre (sx, sy) with half-axes (axx, axy) and (ayx, ayy): the corners are
/// centre -/+ axis1 -/+ axis2 and the picture maps u0..u1 along axis 1 and v0..v1 along axis 2. Kind Poly: `npts`
/// corners in px / py, one flat colour.
struct ParticleDrawItem {
    enum Kind : std::uint8_t { Quad, Poly };
    Kind kind = Quad;
    ParticleSystem::BlendMode blend = ParticleSystem::BlendMode::Alpha;
    std::uint8_t npts = 0;
    bool  streak = false;           // a stretched quad (an untextured one is drawn as a bar, not a puff)
    float key = 0.0f;               // draw order: larger = farther = drawn first
    float sx = 0, sy = 0;
    float axx = 0, axy = 0, ayx = 0, ayy = 0;
    float rad = 1.0f;               // radius of the round puff, pixels
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    float px[4] = {0, 0, 0, 0}, py[4] = {0, 0, 0, 0};
    Color color;                    // straight (not premultiplied) alpha
    const std::string* texture = nullptr;   // empty / null = a soft round puff
};

struct ParticleDrawStats {
    int systems = 0;                // systems with live particles
    int culled = 0;                 // of those, skipped as off screen
    int particles = 0;              // particles turned into shapes
    int items = 0;                  // shapes produced (quads, ribbon pieces, cube faces)
    int ribbonPieces = 0;
};

/// Build the draw list for every Particle System in `scene`. `visible` (optional) can hide an object (a hidden
/// canvas, a camera culling mask). The list is sorted far to near (a stable sort, so equal keys keep their order).
void BuildParticleDraw(Scene& scene, const ParticleView& view, std::vector<ParticleDrawItem>& out,
                       ParticleDrawStats* stats = nullptr,
                       const std::function<bool(const GameObject*)>& visible = {});

/// A small CPU rasteriser for the list (headless renders, tests, thumbnails). `rgba` is w*h*4 bytes, cleared by the
/// caller; textured items are drawn as the soft puff (no image files are read). Additive and Premultiplied blend
/// add the colour; Alpha blends over.
void RasterizeParticleDraw(const std::vector<ParticleDrawItem>& items, int w, int h, std::uint8_t* rgba);

} // namespace okay
