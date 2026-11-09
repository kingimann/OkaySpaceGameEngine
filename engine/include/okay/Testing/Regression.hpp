#pragma once
// ---------------------------------------------------------------------------
// Regression helpers: catch behaviour or output that changed without anyone meaning it to.
//
//   Golden files      CompareTextToGolden / CompareBinaryToGolden: compare what a test
//                     produced with a stored file, with tolerances, and rewrite the file
//                     on purpose when the change is intended.
//   Scene digest      DigestScene / HashScene: a stable fingerprint of a scene's objects
//                     (rounded world poses, components), so a test can say "after 120 frames
//                     of play the world still looks like this".
//   Golden images     RenderSceneToImage + CheckImageGolden: draw a scene with the software
//                     renderer into memory and compare it with a stored PNG or PPM, per channel
//                     tolerance, with a diff image written when it fails.
//
// Updating goldens: set the environment variable OKAY_UPDATE_GOLDEN=1 (or pass update=true)
// and every check rewrites its golden file instead of comparing. Review the change in version
// control and commit it.
// ---------------------------------------------------------------------------
#include "okay/Graphics/Image.hpp"
#include "okay/Render/Color.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
class Scene;
class Camera;
}

namespace okay::testing {

/// True when OKAY_UPDATE_GOLDEN is set to anything but "" or "0".
bool GoldenUpdateRequested();

struct GoldenResult {
    bool ok = true;
    bool updated = false;       // the golden file was written; nothing was compared
    bool missing = false;       // there is no golden file (and we were not updating)
    std::size_t differences = 0;
    std::string message;        // what differed (the first few places); empty when ok
    explicit operator bool() const { return ok; }
};

// ---- Text -------------------------------------------------------------------

struct TextGoldenOptions {
    bool normalizeLineEndings = true;      // CRLF and CR count as LF
    bool trimTrailingWhitespace = true;    // spaces at the end of a line do not matter
    bool ignoreTrailingNewlines = true;    // nor do blank lines at the very end
    /// Numbers inside the text may differ by this much (absolute), or by this fraction
    /// of the larger one (relative); either is enough. 0 = exact.
    double numberTolerance = 0.0;
    double relativeTolerance = 0.0;
    bool update = false;                   // rewrite the golden (also when GoldenUpdateRequested())
    std::size_t maxReportedDiffs = 10;
};

/// Compare two texts line by line under the options (no files involved).
GoldenResult CompareText(const std::string& actual, const std::string& expected, const TextGoldenOptions& options = {});
/// Compare `actual` with the golden file at `goldenPath`. A missing file is a failure
/// (missing = true) unless updating, which writes it.
GoldenResult CompareTextToGolden(const std::string& actual, const std::string& goldenPath,
                                 const TextGoldenOptions& options = {});

// ---- Binary -----------------------------------------------------------------

struct BinaryGoldenOptions {
    /// Each byte may differ by this much...
    int byteTolerance = 0;
    /// ...and this many bytes may differ by more than that.
    std::size_t maxDifferingBytes = 0;
    /// Treat the data as little-endian 32-bit floats and compare them within
    /// `floatTolerance` (absolute) instead of byte by byte. Two NaNs match. A tail
    /// shorter than 4 bytes is compared exactly.
    bool asFloat32 = false;
    float floatTolerance = 0.0f;
    bool update = false;
    std::size_t maxReportedDiffs = 10;
};

GoldenResult CompareBinary(const std::vector<std::uint8_t>& actual, const std::vector<std::uint8_t>& expected,
                           const BinaryGoldenOptions& options = {});
GoldenResult CompareBinaryToGolden(const std::vector<std::uint8_t>& actual, const std::string& goldenPath,
                                   const BinaryGoldenOptions& options = {});

// ---- Scene digest -----------------------------------------------------------

struct SceneHashOptions {
    /// Positions, rotations and scales are rounded to this many decimals before hashing,
    /// so float noise below it does not change the hash.
    int decimals = 3;
    bool includeInactive = true;
    bool includeTransient = false;     // objects a component generated at run time
    bool includeComponents = true;     // the list of component types on each object
    bool includeLayerAndTag = true;
};

/// A fingerprint of a scene: one line per object (path, active, layer, tag, rounded world
/// position / rotation / scale, component types), sorted, and a 64-bit hash of those lines.
/// Two scenes with the same objects in the same state give the same digest wherever and
/// however they were built.
struct SceneDigest {
    std::uint64_t hash = 0;
    std::vector<std::string> lines;
    /// The hash as 16 lowercase hex digits.
    std::string HashHex() const;
    /// "scenehash <hex>\n" followed by the lines: the golden file format.
    std::string ToText() const;
};

SceneDigest DigestScene(const Scene& scene, const SceneHashOptions& options = {});
inline std::uint64_t HashScene(const Scene& scene, const SceneHashOptions& options = {}) {
    return DigestScene(scene, options).hash;
}
/// The lines that were removed ("- ...") and added ("+ ...") between two digests, at most
/// `maxLines` of them; empty when the digests are equal.
std::string DiffDigests(const SceneDigest& expected, const SceneDigest& actual, std::size_t maxLines = 20);
/// Compare a scene's digest with the golden file holding an earlier digest's ToText().
GoldenResult CheckSceneGolden(const Scene& scene, const std::string& goldenPath,
                              const SceneHashOptions& options = {}, bool update = false);

// ---- Images -----------------------------------------------------------------

struct ImageCompareOptions {
    /// A pixel matches when every channel is within this many levels (0..255).
    int channelTolerance = 0;
    /// This fraction of the pixels (0..1) may fail to match, or...
    double maxDifferingFraction = 0.0;
    /// ...this many pixels (the larger allowance wins).
    std::size_t maxDifferingPixels = 0;
    bool compareAlpha = false;
};

struct ImageCompareResult {
    bool match = false;
    bool sizeMismatch = false;
    std::size_t differingPixels = 0;
    int maxChannelDiff = 0;
    double meanChannelDiff = 0.0;     // over every compared channel of every pixel
    /// Same size as the larger input: pixels that differ beyond the tolerance are red
    /// (brighter = further off); the rest are a dimmed copy of the actual image.
    Image diff;
    std::string message;
};

ImageCompareResult CompareImages(const Image& actual, const Image& expected, const ImageCompareOptions& options = {});

/// Read / write an image by file extension: ".ppm" is binary PPM (P6; ASCII P3 can be read),
/// anything else goes through Image::Load / Image::SavePNG.
bool LoadImageFile(const std::string& path, Image& out, std::string* error = nullptr);
bool SaveImageFile(const Image& image, const std::string& path);

/// Compare `actual` with the stored image at `goldenPath`. When the images differ and
/// `diffPath` is not empty, the diff image is saved there. Updating writes the golden.
GoldenResult CheckImageGolden(const Image& actual, const std::string& goldenPath,
                              const ImageCompareOptions& options = {}, const std::string& diffPath = "",
                              bool update = false);

struct SceneRenderOptions {
    int width = 160;
    int height = 90;
    /// Draw at this multiple of the size and average down (1 = none).
    int supersample = 1;
    /// What shows where no mesh was drawn (the software 3D pass leaves those pixels
    /// transparent; sky and 2D sprites are painted by the player, not by it).
    Color background = Color::FromBytes(166, 206, 235);
    /// The camera to draw from; null = the scene's main camera, or else the first
    /// enabled one.
    Camera* camera = nullptr;
};

/// Draw the scene's 3D meshes with the engine's software renderer into `out` (RGBA, opaque).
/// Deterministic on one machine and build. Returns false (with `error`) when there is no
/// usable camera. Only MeshRenderers are drawn; sprites, UI, particles and the sky are
/// painted by the host (player/editor), not by the software 3D pass.
bool RenderSceneToImage(const Scene& scene, Image& out, const SceneRenderOptions& options = {},
                        std::string* error = nullptr);

} // namespace okay::testing
