#pragma once
// OkayUI render interface: OkayUI's draw data on screen WITHOUT any Dear ImGui backend code.
//
// OWN CODE, OFF BY DEFAULT, wired nowhere (the editor and launcher still draw through Dear ImGui). See
// docs/own_ui_toolkit.md, "OkayUI render interface".
//
// Three layers:
//
//   OkayUI::GetDrawData()          what OkayUI produces after EndFrameData: vertices, 32-bit indices and
//                                  commands (spans), each span = a run of indices with one clip rectangle and
//                                  one texture key, in draw order. Window stacking has already reordered them.
//   render::Validate / Sanitize    the contract checker: refuses or repairs hostile or broken data.
//   render::Renderer               the one front end every host uses: validates, keeps the textures (the glyph
//                                  atlas, fallback-font atlases, the host's own images) on the backend, turns
//                                  spans into framebuffer-pixel spans for a DPI scale, calls the backend.
//   render::Backend                what a backend implements (software, okay::platform::Painter, OpenGL,
//                                  Direct3D 11): create / update / destroy a texture, begin a frame, draw
//                                  spans, end a frame. Backends see only data that passed the validator.
//
// ---- The contract (what a backend may rely on, what a host must provide) -------------------------------
//   Coordinates   vertex positions and clip rectangles are in LOGICAL pixels, origin at the top left, y down.
//                 With a DPI scale s the framebuffer is s times larger: framebuffer pixel = logical * s.
//                 OkayUI knows nothing of DPI: text is baked at fixed sizes, so s > 1 draws it magnified
//                 (linearly filtered), not re-rasterised.
//   Vertices      render::Vertex = OkayUI::DrawVertex: float x, y; uint8 r, g, b, a (straight alpha, NOT
//                 premultiplied); float u, v. 20 bytes. u, v are in 0..1 over the texture, v = 0 is the TOP row
//                 of the RGBA8 data (first row in memory).
//   Colour        the fragment colour is vertex colour times texel (texel = white when untextured), then
//                 blended source-over with straight alpha: rgb = src.rgb * src.a + dst.rgb * (1 - src.a),
//                 alpha = src.a + dst.a * (1 - src.a).
//   Triangles     indices are 32-bit, three per triangle, both windings are drawn (no culling). Pixel centres
//                 are at +0.5; a pixel is inside a triangle by the top-left fill rule (software and Painter)
//                 or by the GPU's own rule (OpenGL, Direct3D 11: the same rule, to the hardware's precision).
//   Spans         span order IS draw order. OkayUI emits spans with ascending, adjacent index ranges that tile
//                 [0, indexCount); a backend must not depend on that, the validator only reports it. Each span has
//                 a clip rectangle (or none) and a texture key (null = untextured). The clip is a scissor
//                 rectangle: a pixel is drawn when x0 <= x < x1 and y0 <= y < y1. The renderer converts it to
//                 framebuffer pixels (floor of the low edge, ceil of the high edge, clamped to the target) and
//                 drops spans whose rectangle is empty, so a backend always gets a non-empty, in-range scissor.
//   Textures      RGBA8, top row first, straight alpha. The key in DrawCmd::texture is an opaque pointer:
//                 DrawData::atlasTexture = the launcher look's glyph atlas (white texels, coverage in alpha),
//                 OkayUI::FallbackFontOf(key) != null = a fallback font's atlas, anything else = a host image the
//                 host registered with Renderer::SetImage under the same key (the pointer passed to
//                 OkayUI::Image / DrawTexture; any unique pointer will do for a host that is not on SDL). A span
//                 whose key is none of these is skipped and counted in Stats::unresolvedSpans. The atlas and
//                 fallback fonts are filtered linearly, a host image as it asked.
//   Frames        Renderer::Render = Backend::BeginFrame, Backend::Draw, Backend::EndFrame, once per OkayUI frame
//                 after OkayUI::EndFrameData. The backend does not clear the target (the host does) and does not
//                 present (the host does).
//   Threads       none of this is thread safe; call it from the thread that made the GPU context current.
//
// Everything the renderer touches is checked first (see Validate). A frame that cannot be drawn safely is
// refused as a whole and nothing is drawn.
#include "okay/UI/OkayUI.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace OkayUI {
namespace render {

using Vertex = OkayUI::DrawVertex;
static_assert(sizeof(Vertex) == 20, "the draw vertex is 20 bytes: position, RGBA8, uv");

// ---- limits ------------------------------------------------------------------------------------------------
constexpr int kMaxVertices   = 4 * 1024 * 1024;   // far above OkayUI's own batch (262,144 per layer)
constexpr int kMaxIndices    = 12 * 1024 * 1024;
constexpr int kMaxSpans      = 65536;
constexpr int kMaxTarget     = 16384;             // framebuffer width or height
constexpr int kMaxTexture    = 16384;
constexpr float kMaxCoord    = 1.0e6f;            // |x|, |y| beyond this is treated as broken
constexpr float kMinDpi      = 0.25f, kMaxDpi = 16.0f;

// ---- the validator -----------------------------------------------------------------------------------------
enum class Problem {
    None = 0,
    // fatal: the frame is refused, nothing is drawn
    NullPointer,         // a pointer is null while its count says there is data
    BadStride,           // vertexStride != sizeof(Vertex)
    NegativeCount,       // a count below zero
    TooMany,             // a count above the limits
    BadTarget,           // framebuffer size or DPI outside the supported range
    // repairable: dropped by Sanitize (or fatal in strict mode)
    IndexCountNotTriangles,   // indexCount % 3 != 0 (the tail is not drawn)
    IndexOutOfRange,          // an index < 0 or >= vertexCount (that triangle is dropped)
    BadVertex,                // NaN, infinity or |coordinate| > kMaxCoord in a position (the triangle is dropped); NaN uv is a warning only
    SpanOutOfRange,           // a span reaching outside the index buffer, or with a negative offset or count (the span is dropped / clamped)
    SpanNotTriangles,         // a span whose count is not a multiple of 3 (the tail is not drawn)
    BadClip,                  // a clip rectangle with a negative size or overflowing (the span is dropped)
    // contract warnings: legal for a backend, but OkayUI never produces them
    SpansNotAscending,        // span index ranges that go backwards or overlap
    UncoveredIndices,         // indices that no span draws
    UnresolvedTexture,        // a span with a texture key the renderer does not know (skipped)
    Dropped,                  // DrawData::dropped > 0: OkayUI itself ran out of batch space
};
const char* ProblemName(Problem p);
inline bool IsFatal(Problem p) { return p >= Problem::NullPointer && p <= Problem::BadTarget; }

struct Report {
    Problem     first = Problem::None;       // the first problem found (by severity order below), None = clean
    std::string message;                     // one line: what, where
    // counts (0 = absent)
    int fatal = 0;                           // number of fatal problems (0 or 1: the check stops at the first)
    int badIndices = 0, badVertices = 0, badSpans = 0, badClips = 0, nanUv = 0;
    int unresolvedSpans = 0, uncoveredIndices = 0, spansOutOfOrder = 0, droppedByOkayUI = 0;
    bool trailingIndices = false;            // indexCount % 3 != 0
    bool ok() const { return first == Problem::None; }
    bool refused() const { return fatal > 0; }
    /// true when something repairable was found, i.e. Sanitize would change the data
    bool needsRepair() const { return badIndices || badVertices || nanUv || badSpans || badClips || trailingIndices; }
};

struct ValidateOptions {
    bool checkIndices  = true;    // O(indexCount): every index in range
    bool checkVertices = true;    // O(vertexCount): finite, bounded positions
    /// A texture key the validator should accept as resolvable (called per span; null = accept every key).
    bool (*resolvable)(const void* key, void* user) = nullptr;
    void* resolvableUser = nullptr;
};

/// Check `d` against the contract, for a framebuffer of fbW x fbH pixels at `dpi`. Never reads outside the
/// arrays the counts describe, never crashes on hostile data (the first thing it checks is that the counts
/// and pointers agree). O(vertices + indices + spans).
Report Validate(const DrawData& d, int fbW, int fbH, float dpi, const ValidateOptions& o = ValidateOptions());

/// What a backend draws: repaired copies of the arrays. Spans are in framebuffer pixels and already clipped.
struct Span {
    int          idxOffset = 0, idxCount = 0;      // into the index array, count a multiple of 3, offset + count <= indexCount
    bool         clipOn = false;
    int          x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // scissor in framebuffer pixels, x0 < x1, y0 < y1, inside the target (only when clipOn)
    std::uintptr_t tex = 0;                        // the backend's texture id (0 = untextured)
};

// ---- the backend interface ----------------------------------------------------------------------------------
using TexId = std::uintptr_t;     // 0 = none

struct Caps {
    bool scissor = true;          // clips per span (all of ours do; a backend that cannot says false and is not used)
    bool linearFilter = true;
    int  maxTexture = 4096;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual const char* Name() const = 0;
    virtual Caps Capabilities() const { return Caps(); }

    /// RGBA8, top row first, `rgba` may be null (transparent black). Returns 0 on failure.
    virtual TexId CreateTexture(int w, int h, bool linear, const std::uint8_t* rgba) = 0;
    /// Replace the pixels; the size may differ from the old one. False on failure (the texture is then unusable).
    virtual bool  UpdateTexture(TexId t, int w, int h, const std::uint8_t* rgba) = 0;
    virtual void  DestroyTexture(TexId t) = 0;

    /// Start a frame on a target of fbW x fbH pixels; logical coordinates are multiplied by `dpi`.
    virtual bool BeginFrame(int fbW, int fbH, float dpi) = 0;
    /// Upload the geometry (indices are valid for the vertices, spans are valid for the indices) and draw the
    /// spans in order. Called once per frame between BeginFrame and EndFrame.
    virtual void Draw(const Vertex* vtx, int vertexCount, const std::int32_t* idx, int indexCount, const Span* spans, int spanCount) = 0;
    virtual void EndFrame() = 0;

    /// The target as RGBA8, top row first (tests, screenshots). False when the backend cannot.
    virtual bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) { (void)rgba; (void)w; (void)h; return false; }
};

// ---- the front end ------------------------------------------------------------------------------------------
struct Stats {
    int spansDrawn = 0, spansSkipped = 0, unresolvedSpans = 0;
    int trianglesDrawn = 0, trianglesDropped = 0;
    int texturesUploaded = 0;       // atlas / fallback / image uploads this frame
    bool refused = false;           // the frame was not drawn
    bool repaired = false;          // Sanitize changed the data
    Problem firstProblem = Problem::None;
    std::string message;
};

struct Options {
    /// Strict: any problem the validator reports (not only fatal ones) refuses the frame.
    bool strict = false;
    /// The glyph atlases are sampled linearly (what the GPU backends always did). false = nearest, which is what
    /// the software draw-list rasteriser and its committed golden images use.
    bool atlasLinear = true;
    bool checkIndices = true, checkVertices = true;
};

struct FrameInfo {
    int   fbWidth = 0, fbHeight = 0;     // framebuffer pixels
    float dpi = 1.0f;                    // framebuffer pixels per logical pixel
};

class Renderer {
public:
    explicit Renderer(Backend& b, const Options& o = Options()) : m_b(b), m_o(o) {}
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// A host image under `key` (the pointer the host gave OkayUI::Image / DrawTexture / DrawList::AddImage).
    /// Replaces the image under that key. False on a bad size (0 or above kMaxTexture) or a null key.
    bool SetImage(const void* key, int w, int h, const std::uint8_t* rgba, bool linear = true);
    void RemoveImage(const void* key);
    bool HasImage(const void* key) const { return m_images.count(key) != 0; }

    /// Draw one finished OkayUI frame (`OkayUI::GetDrawData()` after EndFrameData). Does not clear and does not present.
    Stats Render(const DrawData& d, const FrameInfo& f);

    /// Forget the uploaded glyph atlas (call after the atlas content changed in place; it is uploaded again next frame).
    void InvalidateAtlas() { m_atlasKey = nullptr; }
    /// Free everything on the backend (call with the backend's context current).
    void ReleaseAll();

    Backend& GetBackend() { return m_b; }
    const Options& GetOptions() const { return m_o; }
    void SetOptions(const Options& o) { m_o = o; }

private:
    struct Img { TexId id = 0; int w = 0, h = 0; };
    struct FontTex { TexId id = 0; int w = 0, h = 0; std::uint32_t gen = ~0u; };
    Backend& m_b;
    Options m_o;
    std::unordered_map<const void*, Img> m_images;
    TexId m_atlas = 0; const void* m_atlasKey = nullptr; int m_atlasW = 0, m_atlasH = 0;
    std::unordered_map<const void*, FontTex> m_fonts;
    // scratch, kept between frames
    std::vector<Span> m_spans;
    std::vector<std::int32_t> m_idx;
    std::vector<Vertex> m_vtx;
};

} // namespace render
} // namespace OkayUI
