#pragma once
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include <string>
#include <sstream>
#include <fstream>

namespace okay {

/// A reusable surface material — the look (albedo, emissive, specular, texture,
/// tiling, unlit, double-sided) packaged on its own so it can be saved as a
/// `.okaymat` asset and applied to many MeshRenderers, exactly like a Unity
/// Material. The MeshRenderer still owns the live fields the renderer reads;
/// a Material is the shareable preset you copy into them.
struct Material {
    Color       color    = Color::White;
    Color       emissive = Color::Black;
    float       specular = 0.0f;
    float       shininess = 16.0f;
    bool        unlit = false;
    bool        doubleSided = false;
    std::string texture;
    Vec2        tiling{1.0f, 1.0f};

    /// Shading model (Unity's shader selector) + cel-band count for the Toon shader.
    MeshRenderer::Shader shader = MeshRenderer::Shader::Standard;
    int                  toonBands = 3;

    /// Per-material rim (Fresnel) backlight and inverted-hull silhouette outline.
    float rimStrength = 0.0f, rimPower = 3.0f;
    Color rimColor = Color::White;
    bool  outline = false;
    Color outlineColor = Color::Black;
    float outlineWidth = 0.03f;

    /// Scrolling-UV animation speed and triplanar (seamless world-space) mapping.
    Vec2  uvScroll{0.0f, 0.0f};
    bool  triplanar = false;

    /// Copy the surface fields from a renderer into a Material preset.
    static Material FromRenderer(const MeshRenderer& mr) {
        Material m;
        m.color = mr.color; m.emissive = mr.emissive;
        m.specular = mr.specular; m.shininess = mr.shininess;
        m.unlit = mr.unlit; m.doubleSided = mr.doubleSided;
        m.texture = mr.texture; m.tiling = mr.tiling;
        m.shader = mr.shader; m.toonBands = mr.toonBands;
        m.rimStrength = mr.rimStrength; m.rimPower = mr.rimPower; m.rimColor = mr.rimColor;
        m.outline = mr.outline; m.outlineColor = mr.outlineColor; m.outlineWidth = mr.outlineWidth;
        m.uvScroll = mr.uvScroll; m.triplanar = mr.triplanar;
        return m;
    }
    /// Stamp this material's look onto a renderer (mesh/geometry untouched).
    void ApplyTo(MeshRenderer& mr) const {
        mr.color = color; mr.emissive = emissive;
        mr.specular = specular; mr.shininess = shininess;
        mr.unlit = unlit; mr.doubleSided = doubleSided;
        mr.texture = texture; mr.tiling = tiling;
        mr.shader = shader; mr.toonBands = toonBands;
        mr.rimStrength = rimStrength; mr.rimPower = rimPower; mr.rimColor = rimColor;
        mr.outline = outline; mr.outlineColor = outlineColor; mr.outlineWidth = outlineWidth;
        mr.uvScroll = uvScroll; mr.triplanar = triplanar;
    }

    std::string ToText() const {
        std::ostringstream o;
        o << "okaymat 1\n"
          << "color " << color.r << " " << color.g << " " << color.b << " " << color.a << "\n"
          << "emissive " << emissive.r << " " << emissive.g << " " << emissive.b << "\n"
          << "specular " << specular << " " << shininess << "\n"
          << "flags " << (unlit ? 1 : 0) << " " << (doubleSided ? 1 : 0) << "\n"
          << "tiling " << tiling.x << " " << tiling.y << "\n"
          << "shader " << (int)shader << " " << toonBands << "\n"
          << "rim " << rimStrength << " " << rimPower << " "
          << rimColor.r << " " << rimColor.g << " " << rimColor.b << "\n"
          << "outline " << (outline ? 1 : 0) << " " << outlineWidth << " "
          << outlineColor.r << " " << outlineColor.g << " " << outlineColor.b << "\n"
          << "uvanim " << uvScroll.x << " " << uvScroll.y << " " << (triplanar ? 1 : 0) << "\n"
          << "texture " << (texture.empty() ? "-" : texture) << "\n";
        return o.str();
    }
    static Material FromText(const std::string& text) {
        Material m;
        std::istringstream in(text);
        std::string tok;
        while (in >> tok) {
            if (tok == "color")        in >> m.color.r >> m.color.g >> m.color.b >> m.color.a;
            else if (tok == "emissive")in >> m.emissive.r >> m.emissive.g >> m.emissive.b;
            else if (tok == "specular")in >> m.specular >> m.shininess;
            else if (tok == "flags")   { int u = 0, d = 0; in >> u >> d; m.unlit = u != 0; m.doubleSided = d != 0; }
            else if (tok == "tiling")  in >> m.tiling.x >> m.tiling.y;
            else if (tok == "shader")  { int s = 0; in >> s >> m.toonBands; m.shader = (MeshRenderer::Shader)s; }
            else if (tok == "rim")     in >> m.rimStrength >> m.rimPower >> m.rimColor.r >> m.rimColor.g >> m.rimColor.b;
            else if (tok == "outline") { int o = 0; in >> o >> m.outlineWidth >> m.outlineColor.r >> m.outlineColor.g >> m.outlineColor.b; m.outline = o != 0; }
            else if (tok == "uvanim")  { int tp = 0; in >> m.uvScroll.x >> m.uvScroll.y >> tp; m.triplanar = tp != 0; }
            else if (tok == "texture") { std::string t; in >> t; m.texture = (t == "-") ? "" : t; }
        }
        return m;
    }

    bool SaveToFile(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return false;
        f << ToText();
        return true;
    }
    static bool LoadFromFile(const std::string& path, Material& out) {
        std::ifstream f(path);
        if (!f) return false;
        std::stringstream ss; ss << f.rdbuf();
        out = FromText(ss.str());
        return true;
    }
};

} // namespace okay
