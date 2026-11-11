#pragma once
// Reads the import settings the editor stores next to an asset (`<asset>.okaymeta`) for the few importers that use
// them. Header only, no asset database needed: a missing or unreadable meta means "no settings".
//   * texture `maxSize`: the longest side a texture is loaded at (0 = no limit). Used by the software renderer's
//     texture cache, so the editor and a built game agree.
//   * texture `filter` / `wrap` / `mipmaps`: how the software renderer and the OpenGL renderer sample the picture.
//   * model `scale`: multiplies the size of a model added to a scene from the editor.
//   * model `normals` / `smoothingAngle` / `importAnimations`: applied when a model is brought into a scene
//     (see ModelImport.cpp, which reads them through ReadModelImportOptions below).
// Other keys in the meta (compress, sRGB, audio options ...) are stored but no importer reads them yet, and the
// editor does not offer them.
#include "okay/Graphics/Image.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace okay {

/// Area-weighted shrink to a longest side of at most `maxSide` (defined in ThumbnailCache.cpp, linked with the engine).
Image BoxDownscale(const Image& src, int maxSide);

/// The value of `set <key> <value>` in `<assetPath>.okaymeta` for an asset of type `importer`, or "" when absent.
inline std::string ReadImportSetting(const std::string& assetPath, const char* importer, const char* key) {
    std::ifstream in(assetPath + ".okaymeta", std::ios::binary);
    if (!in) return {};
    std::string line;
    bool right = false;
    std::string want = std::string("set ") + key + " ";
    std::string found;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("importer ", 0) == 0) right = line.compare(9, std::string(importer).size(), importer) == 0;
        else if (right && line.compare(0, want.size(), want) == 0) found = line.substr(want.size());
    }
    return found;
}

/// Texture `maxSize` from the sidecar: 0 when there is none, or it is 0 (no limit). Values below 32 are ignored.
inline int TextureImportMaxSize(const std::string& imagePath) {
    std::string v = ReadImportSetting(imagePath, "texture", "maxSize");
    if (v.empty()) v = ReadImportSetting(imagePath, "texture", "maxsize");   // the older layout
    if (v.empty()) return 0;
    const int n = std::atoi(v.c_str());
    return n >= 32 && n <= 16384 ? n : 0;
}

/// Model `scale` from the sidecar: 1 when there is none or it is out of range.
inline float ModelImportScale(const std::string& modelPath) {
    const std::string v = ReadImportSetting(modelPath, "model", "scale");
    if (v.empty()) return 1.0f;
    const float s = static_cast<float>(std::atof(v.c_str()));
    return s >= 0.0001f && s <= 10000.0f ? s : 1.0f;
}

/// How a texture is sampled. A missing sidecar, or a missing key, gives the renderer's long-standing look
/// (smooth, mipmapped, repeating). Older layouts' words ("nearest", "linear", "mips") are understood too.
struct TextureSampling {
    enum Filter { Point = 0, Bilinear = 1, Trilinear = 2 };
    enum Wrap { Repeat = 0, Clamp = 1, Mirror = 2 };
    int  filter = Trilinear;
    int  wrap = Repeat;
    bool mipmaps = true;
};

inline TextureSampling TextureImportSampling(const std::string& imagePath) {
    TextureSampling t;
    if (imagePath.empty() || imagePath[0] == '@' || imagePath.rfind("proc:", 0) == 0) return t;
    auto low = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    const std::string f = low(ReadImportSetting(imagePath, "texture", "filter"));
    if (f == "point" || f == "nearest") t.filter = TextureSampling::Point;
    else if (f == "bilinear" || f == "linear") t.filter = TextureSampling::Bilinear;
    else if (f == "trilinear") t.filter = TextureSampling::Trilinear;
    const std::string w = low(ReadImportSetting(imagePath, "texture", "wrap"));
    if (w == "clamp") t.wrap = TextureSampling::Clamp;
    else if (w == "mirror") t.wrap = TextureSampling::Mirror;
    else if (w == "repeat") t.wrap = TextureSampling::Repeat;
    std::string m = ReadImportSetting(imagePath, "texture", "mipmaps");
    if (m.empty()) m = ReadImportSetting(imagePath, "texture", "mips");
    m = low(m);
    if (m == "0" || m == "false" || m == "off") t.mipmaps = false;
    return t;
}

/// What a model's sidecar asks for when it is brought into a scene.
struct ModelImportOptions {
    enum Normals { Import = 0, Calculate = 1, Flat = 2 };
    int   normals = Import;
    float smoothingAngle = 60.0f;   ///< degrees, for Calculate
    bool  importAnimations = true;
};

inline ModelImportOptions ReadModelImportOptions(const std::string& modelPath) {
    ModelImportOptions o;
    auto low = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    const std::string n = low(ReadImportSetting(modelPath, "model", "normals"));
    if (n == "calculate") o.normals = ModelImportOptions::Calculate;
    else if (n == "flat") o.normals = ModelImportOptions::Flat;
    const std::string a = ReadImportSetting(modelPath, "model", "smoothingAngle");
    if (!a.empty()) { const float v = static_cast<float>(std::atof(a.c_str())); if (v >= 0.0f && v <= 180.0f) o.smoothingAngle = v; }
    const std::string ia = low(ReadImportSetting(modelPath, "model", "importAnimations"));
    if (ia == "0" || ia == "false" || ia == "off") o.importAnimations = false;
    return o;
}

/// Apply the texture's `maxSize` to a freshly loaded image (never enlarges).
inline void ApplyTextureImportSettings(const std::string& imagePath, Image& img) {
    if (!img.Valid()) return;
    const int ms = TextureImportMaxSize(imagePath);
    if (ms > 0 && (img.Width() > ms || img.Height() > ms)) img = BoxDownscale(img, ms);
}

} // namespace okay
