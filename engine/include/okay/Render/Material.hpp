#pragma once
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Core/TolerantNumbers.hpp"
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>

namespace okay {

/// A reusable surface material — the look (albedo, emissive, specular, texture,
/// tiling, unlit, double-sided) packaged on its own so it can be saved as a
/// `.okaymat` asset and applied to many MeshRenderers, exactly like a shared
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

    /// Shading model + cel-band count for the Toon shader.
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
          << "texture " << TexturePathText(texture) << "\n";
        return o.str();
    }
    /// The texture path as the `texture` line writes it. "-" is no texture. A plain path is written as it is
    /// (what every version wrote, so older engines still read it); one with a space, a tab, a quote or a line
    /// break, or one that is literally "-", is put in double quotes with \\ \" \n \r \t escapes.
    static std::string TexturePathText(const std::string& path) {
        if (path.empty()) return "-";
        bool quote = path == "-" || path[0] == '"';
        for (char c : path) if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"') quote = true;
        if (!quote) return path;
        std::string o = "\"";
        for (char c : path) {
            if (c == '\\') o += "\\\\";
            else if (c == '"') o += "\\\"";
            else if (c == '\n') o += "\\n";
            else if (c == '\r') o += "\\r";
            else if (c == '\t') o += "\\t";
            else o += c;
        }
        return o + "\"";
    }
    /// Read back what TexturePathText wrote: `rest` is everything after "texture " on the line. A plain value
    /// is the whole rest of the line, so an older file that wrote a spaced path unquoted loads it whole too.
    static std::string TexturePathFromText(std::string rest) {
        while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t')) rest.pop_back();
        std::size_t i = 0;
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
        rest.erase(0, i);
        if (rest.empty() || rest == "-") return "";
        if (rest[0] != '"') return rest;
        std::string o;
        for (std::size_t k = 1; k < rest.size(); ++k) {
            const char c = rest[k];
            if (c == '"') break;                                   // the closing quote
            if (c == '\\' && k + 1 < rest.size()) {
                const char n = rest[++k];
                o += n == 'n' ? '\n' : n == 'r' ? '\r' : n == 't' ? '\t' : n;
            } else o += c;
        }
        return o;
    }

    /// Read a material. Each line is read on its own and each number on its own: a word that is not a number,
    /// or nan / inf, leaves just that field at its default, and everything else in the file still loads.
    /// Older files (one token per field, unquoted texture) load as before. `problems`, when given, counts the
    /// fields that were dropped.
    static Material FromText(const std::string& text, int* problems = nullptr) {
        Material m;
        int bad = 0;
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream ls(line);
            std::string key;
            if (!(ls >> key)) continue;
            if (key == "texture") {
                const std::size_t at = line.find("texture");
                m.texture = TexturePathFromText(line.substr(at + 7));
                continue;
            }
            std::vector<std::string> v;
            for (std::string t; ls >> t;) v.push_back(t);
            // Field i of this line as a finite float into `dst`; anything else keeps `dst` and is counted.
            auto f = [&](std::size_t i, float& dst) {
                float x = 0.0f;
                if (i < v.size() && ParseTolerantFloat(v[i], x) && std::isfinite(x)) dst = x;
                else ++bad;
            };
            auto n = [&](std::size_t i, int& dst) {
                float x = 0.0f;
                if (i < v.size() && ParseTolerantFloat(v[i], x) && std::isfinite(x) && x == std::floor(x) &&
                    x >= -1.0e6f && x <= 1.0e6f) dst = (int)x;
                else ++bad;
            };
            auto b = [&](std::size_t i, bool& dst) { int x = dst ? 1 : 0; n(i, x); dst = x != 0; };
            if (key == "color")         { f(0, m.color.r); f(1, m.color.g); f(2, m.color.b); f(3, m.color.a); }
            else if (key == "emissive") { f(0, m.emissive.r); f(1, m.emissive.g); f(2, m.emissive.b); }
            else if (key == "specular") { f(0, m.specular); f(1, m.shininess); }
            else if (key == "flags")    { b(0, m.unlit); b(1, m.doubleSided); }
            else if (key == "tiling")   { f(0, m.tiling.x); f(1, m.tiling.y); }
            else if (key == "shader") {
                int s = (int)m.shader;
                n(0, s);
                // An id this build does not know (a damaged file, or a newer shader) draws as Standard.
                if (s < 0 || s > (int)MeshRenderer::Shader::Velvet) { ++bad; s = (int)MeshRenderer::Shader::Standard; }
                m.shader = (MeshRenderer::Shader)s;
                n(1, m.toonBands);
            }
            else if (key == "rim")      { f(0, m.rimStrength); f(1, m.rimPower); f(2, m.rimColor.r); f(3, m.rimColor.g); f(4, m.rimColor.b); }
            else if (key == "outline")  { b(0, m.outline); f(1, m.outlineWidth); f(2, m.outlineColor.r); f(3, m.outlineColor.g); f(4, m.outlineColor.b); }
            else if (key == "uvanim")   { f(0, m.uvScroll.x); f(1, m.uvScroll.y); b(2, m.triplanar); }
            // "okaymat 1" and keys from a newer version are skipped.
        }
        if (problems) *problems = bad;
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
