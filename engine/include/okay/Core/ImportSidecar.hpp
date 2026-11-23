#pragma once
// Reads the import settings the editor stores next to an asset (`<asset>.okaymeta`) for the few importers that use
// them. No asset database needed (ImportSidecar.cpp): a missing or unreadable meta means "no settings".
//   * texture `maxSize`: the longest side a texture is loaded at (0 = no limit). Used by the software renderer's
//     texture cache, so the editor and a built game agree.
//   * texture `filter` / `wrap` / `mipmaps`: how the software renderer and the OpenGL renderer sample the picture.
//   * model `scale`: multiplies the size of a model added to a scene from the editor.
//   * model `normals` / `smoothingAngle` / `importAnimations`: applied when a model is brought into a scene
//     (see ModelImport.cpp, which reads them through ReadModelImportOptions below).
// * texture `compress` (with `sRGB`, `mipmaps`, `maxSize`): the GPU renderers upload a block-compressed copy kept in the
//   import cache (okay/Render/GpuTexture.hpp, gputex::Source). Editor: Import Settings > "Compress on the GPU".
// Audio `loadMode` and `forceMono` are read by the audio loader (quality is kept for old files and not applied).
//
// One reader: the sidecar is read the way the asset database reads it, through ImportSettings::FromPairs (values
// unescaped, older layouts migrated by the same steps, so v1's `maxsize`, `mips`, `nearest`, `linear`, `stream`
// and `mono` are understood without a second copy of those renames here). The result is cached per file and
// re-read when the file's time or size changes, so the hot loaders pay one file-status call, not a parse.
#include "okay/Graphics/Image.hpp"
#include "okay/Core/ImportSettings.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>   // kept for files that relied on this header for it
#include <sstream>
#include <string>

namespace okay {

/// Area-weighted shrink to a longest side of at most `maxSide` (defined in ThumbnailCache.cpp, linked with the engine).
Image BoxDownscale(const Image& src, int maxSide);

/// What `<assetPath>.okaymeta` says for one importer kind.
struct ImportSidecar {
    bool found = false;              ///< the file has an `importer <kind>` block
    ImportSettings settings;         ///< typed, migrated and clamped (the kind's defaults when not found)
    ImportSettings::Pairs pairs;     ///< the block's `set` lines after migration, unescaped, before any range clamp
};

/// The sidecar of `assetPath` for `kind` (Texture, Model or Audio), from a small per-file cache (ImportSidecar.cpp).
/// Safe to call from loader threads.
ImportSidecar ReadImportSidecar(const std::string& assetPath, AssetType kind);
/// Forget every cached sidecar (tests, or a tool that rewrote metas within one file-time tick).
void ClearImportSidecarCache();

/// The value of `set <key> <value>` in `<assetPath>.okaymeta` for an asset of type `importer` ("texture", "model",
/// "audio"), after migration to the current layout and unescaping; "" when absent. Keys match in any case.
std::string ReadImportSetting(const std::string& assetPath, const char* importer, const char* key);

/// Texture `maxSize` from the sidecar: 0 when there is none, or it is 0 (no limit). Values below 32 are ignored.
inline int TextureImportMaxSize(const std::string& imagePath) {
    const std::string v = ReadImportSetting(imagePath, "texture", "maxSize");
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
/// (smooth, mipmapped, repeating). Older layouts' words ("nearest", "linear", "mips") are migrated by ImportSettings.
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
    const ImportSidecar sc = ReadImportSidecar(imagePath, AssetType::Texture);
    if (!sc.found) return t;
    const TextureImportSettings& s = sc.settings.texture;
    t.filter = s.filter == TextureImportSettings::Filter::Point ? TextureSampling::Point
             : s.filter == TextureImportSettings::Filter::Bilinear ? TextureSampling::Bilinear : TextureSampling::Trilinear;
    t.wrap = s.wrap == TextureImportSettings::Wrap::Clamp ? TextureSampling::Clamp
           : s.wrap == TextureImportSettings::Wrap::Mirror ? TextureSampling::Mirror : TextureSampling::Repeat;
    t.mipmaps = s.mipmaps;
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
    const ImportSidecar sc = ReadImportSidecar(modelPath, AssetType::Model);
    if (!sc.found) return o;
    const ModelImportSettings& m = sc.settings.model;
    o.normals = m.normals == ModelImportSettings::Normals::Calculate ? ModelImportOptions::Calculate
              : m.normals == ModelImportSettings::Normals::Flat ? ModelImportOptions::Flat : ModelImportOptions::Import;
    const std::string a = ReadImportSetting(modelPath, "model", "smoothingAngle");   // out of range: ignored, as before
    if (!a.empty()) { const float v = static_cast<float>(std::atof(a.c_str())); if (v >= 0.0f && v <= 180.0f) o.smoothingAngle = v; }
    o.importAnimations = m.importAnimations;
    return o;
}

/// What an audio file's sidecar asks for when the file is loaded.
/// `stream` is loadMode stream (or the older `stream 1` key). `forceMono` averages every channel into one
/// (or the older `mono 1` key). `quality` is not here: clips are not re-encoded, so that number changes nothing.
struct AudioImportOptions {
    bool stream = false;
    bool forceMono = false;
};

inline std::string ImportLower(std::string v) {
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}

inline bool ImportFlagOn(const std::string& v) {
    const std::string s = ImportLower(v);
    return s == "1" || s == "true" || s == "on" || s == "yes";
}

inline AudioImportOptions ReadAudioImportOptions(const std::string& audioPath) {
    AudioImportOptions o;
    const ImportSidecar sc = ReadImportSidecar(audioPath, AssetType::Audio);
    if (!sc.found) return o;
    o.stream = sc.settings.audio.loadMode == AudioImportSettings::LoadMode::Stream;
    o.forceMono = sc.settings.audio.forceMono;
    return o;
}

/// Apply the texture's `maxSize` to a freshly loaded image (never enlarges).
inline void ApplyTextureImportSettings(const std::string& imagePath, Image& img) {
    if (!img.Valid()) return;
    const int ms = TextureImportMaxSize(imagePath);
    if (ms > 0 && (img.Width() > ms || img.Height() > ms)) img = BoxDownscale(img, ms);
}

} // namespace okay
