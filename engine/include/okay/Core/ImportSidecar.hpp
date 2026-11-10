#pragma once
// Reads the import settings the editor stores next to an asset (`<asset>.okaymeta`) for the few importers that use
// them. Header only, no asset database needed: a missing or unreadable meta means "no settings".
//   * texture `maxSize`: the longest side a texture is loaded at (0 = no limit). Used by the software renderer's
//     texture cache, so the editor and a built game agree.
//   * model `scale`: multiplies the size of a model added to a scene from the editor.
// Other keys in the meta (filter, wrap, mipmaps, compress, sRGB, normals, audio options ...) are stored but no
// importer reads them yet, and the editor does not offer them.
#include "okay/Graphics/Image.hpp"

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

/// Apply the texture's `maxSize` to a freshly loaded image (never enlarges).
inline void ApplyTextureImportSettings(const std::string& imagePath, Image& img) {
    if (!img.Valid()) return;
    const int ms = TextureImportMaxSize(imagePath);
    if (ms > 0 && (img.Width() > ms || img.Height() > ms)) img = BoxDownscale(img, ms);
}

} // namespace okay
