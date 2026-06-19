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

    /// Copy the surface fields from a renderer into a Material preset.
    static Material FromRenderer(const MeshRenderer& mr) {
        Material m;
        m.color = mr.color; m.emissive = mr.emissive;
        m.specular = mr.specular; m.shininess = mr.shininess;
        m.unlit = mr.unlit; m.doubleSided = mr.doubleSided;
        m.texture = mr.texture; m.tiling = mr.tiling;
        return m;
    }
    /// Stamp this material's look onto a renderer (mesh/geometry untouched).
    void ApplyTo(MeshRenderer& mr) const {
        mr.color = color; mr.emissive = emissive;
        mr.specular = specular; mr.shininess = shininess;
        mr.unlit = unlit; mr.doubleSided = doubleSided;
        mr.texture = texture; mr.tiling = tiling;
    }

    std::string ToText() const {
        std::ostringstream o;
        o << "okaymat 1\n"
          << "color " << color.r << " " << color.g << " " << color.b << " " << color.a << "\n"
          << "emissive " << emissive.r << " " << emissive.g << " " << emissive.b << "\n"
          << "specular " << specular << " " << shininess << "\n"
          << "flags " << (unlit ? 1 : 0) << " " << (doubleSided ? 1 : 0) << "\n"
          << "tiling " << tiling.x << " " << tiling.y << "\n"
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
