#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Graphics/Image.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

/// One paintable ground layer of a Terrain (grass, dirt, rock...): a picture tiled across
/// the terrain and a tint, blended with the other layers by a painted weight map.
struct TerrainLayer {
    std::string name = "Layer";
    std::string texture;           ///< picture file (empty = just the tint colour)
    Color       tint{1, 1, 1, 1};  ///< multiplies the picture (or is the colour when there is none)
    float       tiling = 8.0f;     ///< how many times the picture repeats across the whole terrain
};

/// A heightmap terrain like Terrain: a square grid of height samples you
/// sculpt with brushes (raise/lower/smooth/flatten) or generate (random / hills),
/// rendered as a mesh built from the heights. It lives next to a MeshRenderer and
/// (re)builds that renderer's mesh whenever it changes — so the existing 3D
/// pipeline (editor view + player) draws it for free.
class Terrain : public Component {
public:
    int   resolution = 64;     // cells per side; (resolution+1)^2 vertices (higher = smoother)
    float size = 50.0f;        // world width & depth (centered on the object)
    Color color = Color::FromBytes(96, 140, 80);
    std::vector<float> heights; // row-major, Dim()*Dim()

    // ---- Auto-coloring "layers": each face is tinted
    //      by its elevation and steepness — water/sand at the bottom, grass in
    //      the middle, rock on steep slopes, snow on the peaks. ----
    bool  autoColor  = true;
    // ---- Texturing (leverages the material pipeline: triplanar + normal maps) ----
    /// Ground texture tiled across the surface (grass/rock/dirt PNG). Empty = flat
    /// auto-colours only. When `autoColor` is also on, the texture is TINTED by the
    /// elevation/slope colours, so one grass texture reads green on flats and grey on
    /// cliffs. Build Game bundles the file.
    std::string texture;
    float       textureTiling = 12.0f;  ///< how many times the texture repeats across the terrain
    /// Triplanar projection: texture by world axes so steep cliffs don't smear (the
    /// classic terrain-texture fix). Uses `textureTiling` as world-space scale.
    bool        triplanarTex = true;
    /// Optional normal/bump map for fine surface detail (pebbles, grain) under lighting.
    std::string normalMap;
    float       normalStrength = 1.0f;

    Color waterColor = Color::FromBytes(54, 110, 158);
    Color sandColor  = Color::FromBytes(205, 195, 145);
    Color grassColor = Color::FromBytes(96, 150, 80);
    Color rockColor  = Color::FromBytes(122, 112, 102);
    Color snowColor  = Color::FromBytes(236, 240, 248);
    float waterLevel = 0.0f;    // world-Y at/below which terrain reads as shore/water
    float snowLevel  = 12.0f;   // world-Y above which terrain reads as snow
    float rockSlope  = 0.55f;   // face steepness (0 flat .. 1 vertical) -> rock

    // ---- Texture layers (splat painting) --------------------------------
    // Up to kMaxLayers ground layers, each with its own picture, blended by a
    // weight map painted per height-grid vertex (layer 0 is the base: it starts
    // at full weight everywhere). With one or more layers the terrain is drawn
    // from ONE baked ground picture (`splatResolution` pixels a side) that the
    // layers are composited into, so every renderer shows it without a special
    // shader; the single Ground Texture and the auto-colours are not used while
    // layers exist. The weights are saved with the terrain (`terrainlayers`).
    static constexpr int kMaxLayers = 4;
    std::vector<TerrainLayer> layers;
    int splatResolution = 512;           ///< size of the baked ground picture (64..4096)

    /// Add a layer (at most kMaxLayers). The first is the base and covers everything.
    /// Returns its index, or -1 when full.
    int  AddLayer(const TerrainLayer& l);
    /// Remove layer `i`; its weight goes to the layers that stay. Removing the last one
    /// returns the terrain to its single texture / auto-colour look.
    bool RemoveLayer(int i);
    /// Weight of layer `i` at height-grid vertex (x, z), 0..1 (0 when out of range).
    float LayerWeight(int i, int x, int z) const;
    /// Weight of layer `i` at a local XZ point, bilinear.
    float LayerWeightAt(int i, float localX, float localZ) const;
    /// Paint layer `i` within `radius`: its weight moves toward 1 (the others shrink to
    /// make room) by `amount` (0..1) at the brush centre, falling off like the sculpt
    /// brushes. A negative `amount` takes the layer away again (the base layer gets it).
    /// Re-bakes only the touched part of the ground picture; call PublishSplat()
    /// when a stroke ends (or now and then while it goes on).
    void PaintLayerAt(int i, float localX, float localZ, float radius, float amount, float hardness = 0.4f);
    /// Give layer `i` full weight everywhere.
    void FillLayer(int i);
    /// A layer's picture, tint or tiling (or splatResolution) changed: the next Apply() re-bakes.
    void MarkSplatDirty() { splatSized_ = 0; }
    /// Weights for the current layers: the base at full weight, the others at 0 (the scene loader uses it).
    void ResetSplatPublic() { ResetSplat(); }
    /// Rebuild the whole ground picture from the layers and weights.
    void BakeSplat();
    /// Tell the renderers the ground picture changed (they re-read it).
    void PublishSplat();
    /// Name the baked picture is registered under (empty without layers).
    const std::string& SplatName() const { return splatName_; }
    /// The baked picture (null without layers or before the first bake).
    const Image* SplatImage() const;
    /// The layers' weights as text (run-length) and back; what the scene file stores.
    std::string EncodeSplat() const;
    bool DecodeSplat(const std::string& text);
    std::vector<std::uint8_t> splat;     ///< layer-major weights: splat[i * Dim()^2 + z * Dim() + x]

    Terrain();

    int   Dim() const { return resolution + 1; }
    float CellSize() const { return resolution > 0 ? size / resolution : size; }

    /// Resize the grid to (res+1)^2, preserving nothing (all flat at 0).
    void  Resize(int res);
    float GetHeight(int x, int z) const;
    void  SetHeight(int x, int z, float h);

    // ---- Sculpting -----------------------------------------------------
    void Flatten(float h = 0.0f);
    void Smooth();
    void Randomize(float amount, unsigned seed = 12345u);
    void Hills(int count, float maxHeight, unsigned seed = 1u);

    /// Procedurally generate a whole landscape from fractal (Perlin-like) noise.
    /// type: 0 Mountains, 1 Hills, 2 Plains, 3 Plateau (mesas), 4 Islands,
    ///       5 Ridged Mountains (sharp ridgelines), 6 Canyons (carved channels).
    /// Types 5/6 use ridged noise + domain warping for natural, organic shapes.
    /// Replaces the current heights.
    void Generate(int type, float amplitude, float frequency, int octaves, unsigned seed);

    // ---- Erosion (geological realism) ----------------------------------
    /// Hydraulic erosion: simulate `droplets` rain drops that flow downhill,
    /// picking up soil on steep ground and dropping it in flats — the standard
    /// technique that carves river valleys, gullies and sediment fans, turning
    /// blobby noise into believable landscape. `strength` scales how aggressively
    /// material is moved (0..~1). Operates in place on the heightmap.
    void Erode(int droplets, float strength = 0.3f, unsigned seed = 1337u);

    /// Thermal erosion: material on slopes steeper than `talus` (height step per
    /// cell) slumps to the lower neighbours, forming talus piles and softening
    /// cliffs into scree slopes. `iterations` passes, `strength` in [0,1].
    void ThermalErode(int iterations, float talus, float strength = 0.5f);

    /// Terrace the terrain into `steps` flat elevation bands (rice-paddy / mesa
    /// look): quantize every height to the nearest of `steps` levels between the
    /// lowest and highest point. `steps` < 2 is a no-op.
    void Terrace(int steps);

    /// Steepness of the surface at a local XZ point, in DEGREES (0 = flat ground,
    /// 90 = a vertical cliff) — from the surface normal. Handy for placing rocks on
    /// cliffs, blocking the player on steep ground, or gating buildable area.
    float SlopeAt(float localX, float localZ) const;

    /// Linearly remap every height so the lowest point becomes `lowY` and the highest
    /// `highY` — fit a noisy heightmap into a desired elevation range (sea level to
    /// peak). A flat map is set to `lowY`.
    void Normalize(float lowY, float highY);

    /// Flip the terrain about its mid elevation: peaks become valleys and vice-versa
    /// (the overall height range is preserved). Great for carving a lake/cave-bed from
    /// a mountain, or just exploring a generated shape's inverse.
    void Invert();

    // ---- Heightmap I/O (interop with World Machine / Gaea / Photoshop) --
    /// Lowest and highest height in the map (for normalizing exports / UI).
    void HeightRange(float& lo, float& hi) const;
    /// Write the heightmap to a grayscale PNG, normalized so the lowest point is
    /// black and the highest is white (the standard heightmap convention).
    bool ExportHeightmap(const std::string& path) const;
    /// Replace the heights from a grayscale PNG: black -> `lowY`, white -> `highY`.
    /// Bilinearly resampled to the current resolution, so any image size works.
    bool ImportHeightmap(const std::string& path, float lowY, float highY);

    /// Brush falloff shape. `hardness` in [0,1] sets the radius of the full-strength
    /// core (Photoshop-style): 0 = soft smoothstep from the centre, 1 = hard edge.
    /// Used by all the brushes below for a natural, pressure-like feel.
    static float BrushWeight(float dist, float radius, float hardness);

    /// Raise (or lower, with a negative delta) heights within `radius` of a point
    /// in the terrain's local XZ space, with a soft falloff — the sculpt brush.
    void RaiseAt(float localX, float localZ, float radius, float delta, float hardness = 0.4f);
    /// Brush that relaxes (averages) heights within `radius` toward their
    /// neighborhood — the Smooth brush. `amount` in [0,1] per application.
    void SmoothAt(float localX, float localZ, float radius, float amount, float hardness = 0.4f);
    /// Brush that pulls heights within `radius` toward `target` — the Flatten /
    /// Set-Height brush. `amount` in [0,1] per application.
    void FlattenAt(float localX, float localZ, float radius, float target, float amount, float hardness = 0.4f);
    /// Brush that adds fractal noise detail within `radius` — roughen up flats with
    /// bumps/grain. `amount` scales the bump height; `seed` varies the pattern.
    void NoiseAt(float localX, float localZ, float radius, float amount, unsigned seed, float hardness = 0.4f);
    /// Brush that runs a localized hydraulic erosion pass within `radius` — carve a
    /// gully / weather a patch right under the cursor. `amount` scales the effect.
    void ErodeAt(float localX, float localZ, float radius, float amount);

    /// How far below the lowest height (sampled on the first brush stroke after a
    /// generate, flatten, import, or other whole-map edit) a dig may carve.
    /// A held dig stops at that floor instead of lowering the heightmap without
    /// limit and dropping the player out of the world. Negative disables it.
    /// Not stored in the scene file — the default applies to loaded terrains.
    float digDepth = 48.0f;

    /// Surface normal at a local XZ point, from the height gradient (bilinear).
    /// Used by physics for slope-aware terrain collision and by tools.
    Vec3 NormalAt(float localX, float localZ) const;
    /// Sample the terrain height at a point in local XZ (bilinear) — handy for the
    /// Flatten brush to pick up the height under the cursor.
    float SampleHeight(float localX, float localZ) const;

    /// Cast a world-space ray (origin, unit direction) at the height field: the first point where it
    /// passes below the surface within `maxDistance`. Works on a hill (unlike a flat plane), and on a
    /// moved terrain. Returns false when the ray misses or never comes down to the surface.
    bool Raycast(const Vec3& origin, const Vec3& dir, float maxDistance, Vec3& hitWorld) const;

    // ---- Rendering -----------------------------------------------------
    /// Build a Mesh (vertices + triangles + UVs) from the heightmap.
    Mesh BuildMesh() const;
    /// Build the mesh into the sibling MeshRenderer (adds one if missing) and
    /// push the terrain color. Call after any edit.
    void Apply();

    void Start() override { Apply(); }

private:
    std::string splatName_;
    int  splatSized_ = 0;                ///< resolution the baked picture was made at
    void ResetSplat();                   ///< weights for the current layers: base = full
    void BakeRect(int x0, int y0, int x1, int y1);
    float floorY_ = 0.0f;
    bool  floorCaptured_ = false;
    /// Drop the remembered floor so the next brush samples the map as it is now.
    void ForgetFloor();
    /// Remember a floor from the current lowest height, once per edit session.
    void CaptureFloor();
    void ClampFloor(float& h) const;
};

} // namespace okay
