#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
#include <string>

namespace okay {

/// Holds a 3D mesh to be drawn at the GameObject's Transform. Rendered solid
/// (flat-shaded, back-face-culled, depth-sorted) by both the editor and the
/// player; enable `wireframe` for an edges-only view.
class MeshRenderer : public Component {
public:
    Mesh  mesh;
    Color color = Color::White;        // base (albedo) color
    bool  enabled = true;      // when false the mesh is not drawn (e.g. the local
                               // player's own body in first person)
    bool  wireframe = false;   // solid by default (Unity-like); true = edges only
    /// Render both faces of each triangle (no back-face culling) — for planes,
    /// flags, foliage cards, and the insides of open meshes like Tube.
    bool  doubleSided = false;

    // ---- Material (honored by the software renderer) ------------------
    /// Self-illumination added on top of lighting (glowing screens, lava, neon).
    Color emissive = Color::Black;
    /// Blinn-Phong specular highlight: intensity [0,1] and tightness (exponent).
    float specular  = 0.0f;
    float shininess = 16.0f;
    /// Skip lighting entirely and draw the flat base color.
    bool  unlit = false;
    /// Optional texture image (PNG/JPG…). When set, the solid renderer maps it
    /// onto the mesh (planar/box projection) tinted by `color`. Build Game
    /// bundles the file alongside the exe.
    std::string texture;
    /// Texture repeat across the surface (UVs are multiplied by this).
    Vec2 tiling = {1.0f, 1.0f};

    /// Optional tangent-space normal map (PNG, RGB = XYZ in 0..1). Adds bumpy
    /// surface detail (per-pixel lighting only) without extra geometry. Build Game
    /// bundles the file alongside the exe.
    std::string normalMap;
    /// Strength of the normal map perturbation (0 = flat, 1 = full).
    float normalStrength = 1.0f;

    /// Environment reflectivity [0,1]: how much the surface mirrors the scene's
    /// sky gradient (per-pixel lighting only). 0 = matte, higher = glossy/metal.
    /// Fresnel-weighted, so edges always reflect a little more than face-on.
    float reflectivity = 0.0f;

    /// Optional specular/gloss map (grayscale PNG): its per-texel luminance scales
    /// the specular highlight and reflection, so one material can mix shiny and
    /// matte regions (per-pixel lighting only). Build Game bundles the file.
    std::string specularMap;

    /// Metalness [0,1]: metals lose their diffuse and tint both their specular
    /// highlight and their environment reflection by the albedo color (so gold
    /// reflects gold). 0 = dielectric (plastic/wood), 1 = pure metal.
    float metallic = 0.0f;

    /// Optional matcap (lit-sphere) image, sampled by the camera-space normal.
    /// This is the technique MakeHuman uses for soft, skin-like shading: it bakes
    /// the entire lighting response into a sphere image, so subtle surface relief
    /// (eye sockets, nose, lips on a face mesh) reads clearly even in a flat
    /// software renderer. The sampled value multiplies the (per-face) base color.
    /// Set to a registered in-memory image name (see RegisterTexture). Empty = off.
    std::string matcap;

    /// Optional .OBJ model file. When set, the scene loader replaces `mesh` with
    /// the loaded geometry (and Build Game bundles the file alongside the exe).
    std::string meshPath;

    MeshRenderer() : mesh(Mesh::Cube()) {}
    explicit MeshRenderer(Mesh m) : mesh(std::move(m)) {}
};

} // namespace okay
