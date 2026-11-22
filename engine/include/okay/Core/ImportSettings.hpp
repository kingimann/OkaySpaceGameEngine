#pragma once
// Asset types (decided by file extension) and the typed per-asset import settings that
// live in an asset's `.okaymeta` file. Settings are versioned: a file written by an older
// layout is migrated on load, and a file written by a newer one is read best-effort and
// never rewritten. See docs/engine/asset_database.md.
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace okay {

enum class AssetType {
    Unknown,        ///< not decided (a default-constructed value)
    Texture,        ///< png jpg jpeg bmp tga gif psd hdr
    Model,          ///< glb gltf fbx obj dae
    Audio,          ///< wav ogg mp3 flac
    Mesh,           ///< okaymesh
    Material,       ///< okaymat
    Scene,          ///< okayscene
    Prefab,         ///< okayprefab
    Animation,      ///< okayanim okayclip bvh
    Dialogue,       ///< okaydialog
    Quest,          ///< okayquest
    BehaviorTree,   ///< okaybt
    VisualScript,   ///< okayvs
    Sequence,       ///< okayseq
    Script,         ///< cs lua
    Data,           ///< okaydata okayitems okayloot okayweapon okayrecipes okayloc okaynav json txt bin
    Font,           ///< ttf otf
    Other           ///< any extension not listed above
};

/// Lower-case name of a kind ("texture", "model", ...), as written in the meta file.
const char* AssetTypeName(AssetType kind);
/// The inverse of AssetTypeName (case-insensitive); Unknown when `name` is not a kind.
AssetType AssetTypeFromName(const std::string& name);
/// The kind for a file extension, with or without the dot, in any case. Other when unlisted.
AssetType AssetTypeFromExtension(const std::string& extension);
/// The kind for a path (looks at the part after the last '.' of the file name).
AssetType AssetTypeFromPath(const std::string& path);
/// Lower-case extensions (no dot) a kind covers, in a stable order. Empty for Unknown/Other.
std::vector<std::string> ExtensionsOfType(AssetType kind);
/// Lower-case extension of a path without the dot ("" when the file name has none).
std::string AssetExtension(const std::string& path);
/// "Assets/Art/hero.png" -> "hero.png" (the part after the last '/' or '\\').
std::string AssetFileName(const std::string& path);

/// Texture import settings. `maxSize` is the longest side in pixels (0 = no limit).
struct TextureImportSettings {
    enum class Filter { Point, Bilinear, Trilinear };
    enum class Wrap { Repeat, Clamp, Mirror };
    bool   sRGB = true;                    ///< the pixels are colour (gamma) data, not linear data like normal maps
    Filter filter = Filter::Trilinear;   // what a picture with no settings file renders with
    Wrap   wrap = Wrap::Repeat;
    int    maxSize = 0;                    ///< 0 (no limit: what a texture with no meta loads at), or 32..16384
    bool   mipmaps = true;
    bool   compress = false;               ///< ask the renderer for a compressed GPU format
};

/// Model import settings.
struct ModelImportSettings {
    enum class Normals { Import, Calculate, Flat };
    float   scale = 1.0f;                  ///< uniform scale applied on import (0.0001..10000)
    Normals normals = Normals::Import;     ///< use the file's, compute smooth ones, or flat ones
    bool    importAnimations = true;
    float   smoothingAngle = 60.0f;        ///< degrees, for Normals::Calculate (0..180)
    /// Automatic LOD: when on, every (unskinned) mesh of the model gets a LOD Group when it is brought into a
    /// scene, with `lodLevels` levels (2..6), each keeping `lodReduction` (0.05..0.95) of the triangles of the one
    /// before, switched at the screen sizes in `lodScreen` (fraction of the view height where level i is left;
    /// missing entries halve the one before, starting at 0.5). The keys are only written while this is on.
    bool    generateLods = false;
    int     lodLevels = 3;
    float   lodReduction = 0.5f;
    std::vector<float> lodScreen;
};

/// Audio import settings.
struct AudioImportSettings {
    /// Decode ("decode", the default): automatic, the whole file in memory unless it plays for 30 seconds or more
    /// (AudioStreamThresholdSeconds, Audio/AudioLoad.hpp), then read while it plays. Stream: always read while it
    /// plays. Memory ("memory"): always the whole file, however long.
    enum class LoadMode { Decode, Stream, Memory };
    LoadMode loadMode = LoadMode::Decode;
    bool     forceMono = false;
    float    quality = 0.7f;               ///< kept so older files still round-trip; audio is not re-encoded, so this is not applied
};

/// All import settings of one asset. Only the block that matches `kind` is stored; types
/// without importer options (scenes, prefabs, ...) store none.
class ImportSettings {
public:
    /// The layout version this build writes, per kind (0 = the kind has no import settings).
    static int CurrentVersion(AssetType kind);
    static bool HasSettings(AssetType kind) { return CurrentVersion(kind) > 0; }

    AssetType kind = AssetType::Unknown;
    int version = 0;                       ///< 0 until the settings were created, loaded or migrated
    TextureImportSettings texture;
    ModelImportSettings model;
    AudioImportSettings audio;
    /// Keys this build does not know (from a newer or plugin-written layout), kept so a
    /// round trip loses nothing.
    std::map<std::string, std::string> extra;
    /// Read from a layout newer than this build knows: values are best effort and the
    /// database will not rewrite the file.
    bool fromNewerVersion = false;

    using Pairs = std::vector<std::pair<std::string, std::string>>;

    /// Defaults for a kind, at the current version.
    static ImportSettings Defaults(AssetType kind);
    /// The settings as key/value text, current layout, stable order, `extra` last.
    Pairs ToPairs() const;
    /// Build settings from stored key/value pairs written at `version`: older layouts are
    /// migrated, missing keys take defaults, bad values take defaults (and add a note),
    /// ranges are clamped. `notes` receives one line per problem or migration step.
    static ImportSettings FromPairs(AssetType kind, int version, const Pairs& pairs,
                                    std::vector<std::string>* notes = nullptr);
    /// Upgrade stored pairs from `fromVersion` to the current layout of `kind`.
    static Pairs Migrate(AssetType kind, int fromVersion, Pairs pairs, std::vector<std::string>* notes = nullptr);

    /// Pull every value into its legal range.
    void Clamp();
    /// Same kind and same stored values (the version and notes are not compared).
    bool SameAs(const ImportSettings& other) const;
    /// One line for logs: "texture v2 srgb=1 filter=bilinear ...".
    std::string Describe() const;
    /// A stable text of the stored values: equal fingerprints mean no reimport is needed.
    std::string Fingerprint() const;
};

} // namespace okay
