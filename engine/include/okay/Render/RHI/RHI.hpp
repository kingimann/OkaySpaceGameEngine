#pragma once
// ---------------------------------------------------------------------------
// RHI: the engine's rendering hardware interface. One small set of calls that every
// drawing backend implements (OpenGL, Software and Direct3D 11 today; Direct3D 12,
// Vulkan and WebGPU in later rounds), so a rendering feature is written ONCE on top of
// it instead of once per renderer. Design, mapping per backend and the migration
// order: docs/rendering/rhi.md.
//
//   Device       creates and destroys every resource, reports its capabilities
//                (Caps), runs command lists (Submit) and reads textures back.
//   SwapChain    the picture shown in a window (BackBuffer + Present).
//   Buffer       vertex, index or uniform bytes.
//   Texture      a 2D picture: sampled, drawn into (render target) or a depth buffer.
//   Sampler      how a texture is read (filter, wrap).
//   Shader       one program for every backend: GLSL text for OpenGL, HLSL for
//                Direct3D 11, C++ functions for Software, one uniform block layout and named
//                texture slots. OkayShader surfaces become RHI shaders through
//                ShaderFromSurface (RHI/SurfaceShader.hpp).
//   Pipeline     shader + vertex layout + raster, blend and depth state.
//   CommandList  recorded once (passes, binds, draws), then submitted. It tracks
//                texture states and records the transitions explicit APIs need.
//
// Conventions (the OpenGL ones, so the OpenGL backend needs no conversion):
//   * Clip space: x, y and z from -w to w; depth = z/w * 0.5 + 0.5 (0 near, 1 far).
//   * Window y goes up: NDC y = -1 is texture row 0, and row 0 is the first row of
//     data uploaded and read back. Texture v = 0 is row 0.
//   * Front faces are counter-clockwise in window space (y up).
//   * RGBA8 data is 4 bytes per pixel, r g b a; float formats take 4 floats per pixel.
//
// Written by us from the OpenGL 2.1 / 3.3 specifications (rasterization, blending,
// framebuffer objects), the Direct3D 11/12 and Vulkan documentation (resource states,
// barriers, frames in flight) and general renderer design. No code from any other
// engine or graphics library.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay {
namespace rhi {

enum class Backend : std::uint8_t { Software, OpenGL, D3D11, D3D12, Vulkan, WebGPU };
const char* BackendName(Backend b);

// ---- Handles --------------------------------------------------------------------
// Ids are never reused by a device, so a destroyed handle is caught, not aliased.
template <class Tag> struct Handle {
    std::uint32_t id = 0;
    explicit operator bool() const { return id != 0; }
    bool operator==(const Handle& o) const { return id == o.id; }
    bool operator!=(const Handle& o) const { return id != o.id; }
};
struct BufferTag {}; struct TextureTag {}; struct SamplerTag {}; struct ShaderTag {}; struct PipelineTag {}; struct SwapChainTag {};
using BufferHandle    = Handle<BufferTag>;
using TextureHandle   = Handle<TextureTag>;
using SamplerHandle   = Handle<SamplerTag>;
using ShaderHandle    = Handle<ShaderTag>;
using PipelineHandle  = Handle<PipelineTag>;
using SwapChainHandle = Handle<SwapChainTag>;

// ---- Resource descriptions --------------------------------------------------------
enum class Format : std::uint8_t { RGBA8, RGBA16F, RGBA32F, Depth };   // Depth: 24-bit or float, backend's choice
bool IsDepth(Format f);
int  BytesPerPixel(Format f);   ///< of the CPU data the Device takes and gives back (RGBA8: 4, float formats: 16)

enum class BufferUsage : std::uint8_t { Vertex, Index };
struct BufferDesc {
    BufferUsage usage = BufferUsage::Vertex;
    std::size_t size = 0;
    const void* initialData = nullptr;   ///< may be null (contents then undefined)
    bool dynamic = false;                ///< updated often (UpdateBuffer each frame)
    const char* debugName = "";
};

struct TextureDesc {
    int width = 0, height = 0;
    Format format = Format::RGBA8;
    bool renderTarget = false;           ///< can be a render pass attachment
    bool sampled = true;                 ///< can be bound with SetTexture
    const void* initialData = nullptr;   ///< width*height pixels, row 0 first (see BytesPerPixel)
    const char* debugName = "";
};

enum class Filter : std::uint8_t { Nearest, Linear };
enum class Wrap : std::uint8_t { Clamp, Repeat };
/// No mip levels yet, so minification and magnification read the same level; the Software
/// backend uses magFilter for both (OpenGL picks by its own scale rule; set both the same).
struct SamplerDesc {
    Filter minFilter = Filter::Linear, magFilter = Filter::Linear;
    Wrap wrapU = Wrap::Clamp, wrapV = Wrap::Clamp;
};

// ---- Shaders ----------------------------------------------------------------------
enum class UniformType : std::uint8_t { Float, Vec2, Vec3, Vec4, Mat4, Int };
int UniformTypeSize(UniformType t);   ///< bytes
/// One field of a shader's uniform block. Offsets follow the 16-byte rule of HLSL
/// constant buffers (a field may not cross a 16-byte boundary; a Mat4 starts on one),
/// so the same block becomes a D3D cbuffer later. Mat4 is column-major (okay::Mat4).
struct UniformField {
    std::string name;   ///< the GLSL uniform name
    UniformType type = UniformType::Float;
    std::uint32_t offset = 0;
};

/// What a CPU (Software backend) fragment function sees.
struct FragmentContext {
    const void* uniforms = nullptr;   ///< the bound uniform block
    const void* userData = nullptr;   ///< ShaderDesc::cpuUserData
    float fragCoord[2] = {0, 0};      ///< window position of the pixel centre (x + 0.5, y + 0.5)
    /// Read texture slot `slot` at (u, v) with its bound sampler, like GLSL texture2D.
    void Sample(int slot, float u, float v, float rgba[4]) const { sampleFn(sampleCtx, slot, u, v, rgba); }
    void (*sampleFn)(const void*, int, float, float, float*) = nullptr;
    const void* sampleCtx = nullptr;
};
/// Vertex stage on the CPU: attributes (each widened to 4 floats, missing ones
/// (0,0,0,1) as in OpenGL) -> clip position and `varyings` floats.
using CpuVertexFn = void (*)(const float (*attr)[4], const void* uniforms, const void* userData,
                             float clipPos[4], float* varyings);
/// Fragment stage on the CPU: interpolated varyings -> colour. Return false to discard.
using CpuFragmentFn = bool (*)(const float* varyings, const FragmentContext& ctx, float outColor[4]);

constexpr int kMaxAttributes = 8;
constexpr int kMaxVaryings = 32;
constexpr int kMaxTextureSlots = 8;
constexpr std::uint32_t kMaxUniformBytes = 4096;

struct ShaderDesc {
    const char* debugName = "";
    std::vector<std::string> attributes;    ///< vertex attribute location i -> its GLSL name
    std::vector<UniformField> uniforms;     ///< the one uniform block
    std::uint32_t uniformBytes = 0;         ///< size of the block (0: no uniforms)
    std::vector<std::string> textures;      ///< texture slot i -> its GLSL sampler name
    // OpenGL: GLSL 1.20 text, compiled like the OpenGL renderer's own (the hosts make a compatibility context).
    std::string glslVertex, glslFragment;
    // Direct3D 11: HLSL (shader model 4.0) text, entry points VSMain and PSMain. Rules (docs/rendering/rhi.md):
    // vertex attribute location i is the input semantic TEXCOORDi; the uniform block is a cbuffer at b0
    // (the same byte layout as `uniforms`); texture slot i is t i with its sampler at s i; the vertex
    // stage returns rhiClip(clipPos) (defined by the backend: it maps the RHI's OpenGL-style clip
    // space to Direct3D's) as its SV_Position. Compiled with D3DCompile when the shader is made.
    std::string hlslVertex, hlslFragment;
    // Software: the same program as C++ functions.
    CpuVertexFn cpuVertex = nullptr;
    CpuFragmentFn cpuFragment = nullptr;
    int cpuVaryings = 0;                    ///< floats passed from vertex to fragment (<= kMaxVaryings)
    const void* cpuUserData = nullptr;      ///< handed to both functions; must outlive the shader
    std::shared_ptr<const void> keepAlive;  ///< owner of cpuUserData, if any (kept with the shader)
};
/// Checks a description for mistakes every backend would make (layout overlaps,
/// 16-byte crossings, sizes). Empty string when fine.
std::string ValidateShaderDesc(const ShaderDesc& d);
/// Checks the HLSL of a description against the Direct3D 11 rules above (entry points, rhiClip,
/// TEXCOORDn inputs, cbuffer b0, t/s registers, every uniform named). A text check, so it runs on
/// every platform; the Direct3D 11 backend runs it before D3DCompile. Empty string when fine.
std::string ValidateHlslDesc(const ShaderDesc& d);

// ---- Pipelines --------------------------------------------------------------------
enum class VertexFormat : std::uint8_t { Float1, Float2, Float3, Float4, UNorm8x4 };
struct VertexAttribute {
    int location = 0;
    VertexFormat format = VertexFormat::Float3;
    std::uint32_t offset = 0;
};
struct VertexLayout {
    std::uint32_t stride = 0;
    std::vector<VertexAttribute> attributes;
};

enum class BlendFactor : std::uint8_t {
    Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor,
    SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha
};
struct BlendState {
    bool enable = false;
    BlendFactor srcColor = BlendFactor::One, dstColor = BlendFactor::Zero;
    BlendFactor srcAlpha = BlendFactor::One, dstAlpha = BlendFactor::Zero;   // operation: add
    std::uint8_t writeMask = 0xF;   ///< bit 0 r, 1 g, 2 b, 3 a
    static BlendState Opaque() { return {}; }
    static BlendState Additive() { BlendState b; b.enable = true; b.dstColor = b.dstAlpha = BlendFactor::One; return b; }
    static BlendState Alpha() {
        BlendState b; b.enable = true; b.srcColor = b.srcAlpha = BlendFactor::SrcAlpha;
        b.dstColor = b.dstAlpha = BlendFactor::OneMinusSrcAlpha; return b;
    }
};
enum class CompareOp : std::uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
struct DepthState {
    bool test = false, write = false;
    CompareOp compare = CompareOp::LessEqual;
};
enum class CullMode : std::uint8_t { None, Back, Front };
enum class Topology : std::uint8_t { Triangles };   // lines and strips: round 2

struct PipelineDesc {
    ShaderHandle shader;
    VertexLayout layout;
    Topology topology = Topology::Triangles;
    CullMode cull = CullMode::None;
    BlendState blend;
    DepthState depth;
    const char* debugName = "";
};
/// Checks a pipeline's vertex layout (locations in range and not repeated, each attribute inside
/// the stride). Empty string when fine. Every backend's CreatePipeline runs it.
std::string ValidatePipelineDesc(const PipelineDesc& d);

// ---- Render passes ------------------------------------------------------------------
enum class LoadOp : std::uint8_t { Load, Clear, DontCare };
struct RenderPassDesc {
    TextureHandle color;   ///< a render-target texture or a swap chain's BackBuffer(); may be empty (depth only)
    TextureHandle depth;   ///< a Depth texture, or empty
    LoadOp colorLoad = LoadOp::Load, depthLoad = LoadOp::Clear;
    float clearColor[4] = {0, 0, 0, 0};
    float clearDepth = 1.0f;
};

/// The state a texture is in, for the backends that need barriers between uses
/// (Direct3D 12, Vulkan). OpenGL and Software ignore the transitions.
enum class ResourceState : std::uint8_t { Unknown, RenderTarget, DepthWrite, ShaderRead, CopySource, Present };

// ---- Command lists ------------------------------------------------------------------
enum class IndexType : std::uint8_t { U16, U32 };

class CommandList {
public:
    enum class Op : std::uint8_t {
        BeginPass, EndPass, Viewport, Pipeline, VertexBuffer, IndexBuffer, Texture, Uniforms, Draw, DrawIndexed, Transition
    };
    struct Command {
        Op op;
        std::uint32_t a = 0, b = 0, c = 0, d = 0;   ///< meaning depends on op (see CommandList.cpp)
    };

    /// Everything set inside a pass (pipeline, buffers, textures, uniforms, viewport) ends with it.
    void BeginRenderPass(const RenderPassDesc& pass);
    void EndRenderPass();
    /// Pixels, origin bottom-left. BeginRenderPass sets it to the whole attachment.
    void SetViewport(int x, int y, int w, int h);
    void SetPipeline(PipelineHandle p);
    void SetVertexBuffer(BufferHandle b, std::uint32_t offset = 0);
    void SetIndexBuffer(BufferHandle b, IndexType type, std::uint32_t offset = 0);
    void SetTexture(int slot, TextureHandle t, SamplerHandle s);
    /// Copies `bytes` into the list (the frame's uniform memory); the pointer need not live on.
    void SetUniforms(const void* data, std::uint32_t bytes);
    void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex = 0);
    void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex = 0, std::int32_t baseVertex = 0);

    void Reset();
    /// The first recording mistake (draw outside a pass, no pipeline, a texture read while
    /// it is the pass's own attachment...), or "". Submit refuses a list with an error.
    const std::string& Error() const { return m_error; }
    const std::vector<Command>& Commands() const { return m_cmds; }
    const RenderPassDesc& Pass(std::uint32_t i) const { return m_passes[i]; }
    const std::uint8_t* UniformData(std::uint32_t offset) const { return m_arena.data() + offset; }
    std::size_t UniformBytesUsed() const { return m_arena.size(); }
    /// The transitions recorded so far (texture id, new state), in order.
    std::vector<std::pair<std::uint32_t, ResourceState>> Transitions() const;

private:
    void Fail(const char* what);
    void Track(TextureHandle t, ResourceState s);
    std::vector<Command> m_cmds;
    std::vector<RenderPassDesc> m_passes;
    std::vector<std::uint8_t> m_arena;   // uniform data, 16-byte aligned blocks
    std::vector<std::pair<std::uint32_t, ResourceState>> m_states;   // per texture, in this list
    std::string m_error;
    bool m_inPass = false, m_havePipeline = false;
    RenderPassDesc m_cur;
};

// ---- Capabilities -------------------------------------------------------------------
struct Caps {
    Backend backend = Backend::Software;
    std::string deviceName;            ///< "Software", or the GL_RENDERER string
    int maxTextureSize = 0;
    int maxTextureSlots = kMaxTextureSlots;
    bool floatRenderTargets = false;   ///< RGBA16F / RGBA32F can be render targets
    bool depthTextures = false;        ///< Depth textures can be attachments
    bool indexU32 = true;
    bool drawBaseVertex = true;        ///< DrawIndexed takes a baseVertex other than 0
    bool needsBarriers = false;        ///< the backend acts on Transition commands
    int framesInFlight = 1;            ///< frames the CPU may run ahead of the device (deferred destruction)
};

struct Stats {
    std::uint64_t submits = 0, draws = 0, passes = 0, transitions = 0, pipelineBinds = 0;
    std::uint64_t trianglesIn = 0;     ///< triangles given to draws
};

// ---- Device ------------------------------------------------------------------------
class Device {
public:
    virtual ~Device() = default;
    virtual const Caps& GetCaps() const = 0;
    Backend GetBackend() const { return GetCaps().backend; }

    virtual BufferHandle   CreateBuffer(const BufferDesc& d) = 0;
    virtual TextureHandle  CreateTexture(const TextureDesc& d) = 0;
    virtual SamplerHandle  CreateSampler(const SamplerDesc& d) = 0;
    virtual ShaderHandle   CreateShader(const ShaderDesc& d) = 0;      ///< empty handle + LastError() on failure
    virtual PipelineHandle CreatePipeline(const PipelineDesc& d) = 0;
    virtual SwapChainHandle CreateSwapChain(int width, int height) = 0;

    /// Destroy: freed at once, or after framesInFlight EndFrame() calls on backends that run ahead.
    void Destroy(BufferHandle h)    { Defer(Kind::Buffer, h.id); }
    void Destroy(TextureHandle h)   { Defer(Kind::Texture, h.id); }
    void Destroy(SamplerHandle h)   { Defer(Kind::Sampler, h.id); }
    void Destroy(ShaderHandle h)    { Defer(Kind::Shader, h.id); }
    void Destroy(PipelineHandle h)  { Defer(Kind::Pipeline, h.id); }
    void Destroy(SwapChainHandle h) { Defer(Kind::SwapChain, h.id); }

    virtual bool UpdateBuffer(BufferHandle b, std::size_t offset, const void* data, std::size_t bytes) = 0;
    virtual bool UpdateTexture(TextureHandle t, const void* pixels) = 0;   ///< whole level 0, row 0 first
    /// Read a colour texture back: RGBA8 as 4 bytes per pixel, float formats as 4 floats. Row 0 first.
    virtual bool ReadTexture(TextureHandle t, std::vector<std::uint8_t>& out) = 0;
    virtual bool TextureSize(TextureHandle t, int& w, int& h) const = 0;

    virtual TextureHandle BackBuffer(SwapChainHandle s) = 0;
    virtual bool ResizeSwapChain(SwapChainHandle s, int width, int height) = 0;
    virtual bool Present(SwapChainHandle s) = 0;

    /// Run a recorded list. False (and nothing drawn) when the list has an error or names
    /// a destroyed resource; LastError() says why.
    virtual bool Submit(const CommandList& list) = 0;

    /// Frame bracket: EndFrame frees deferred resources whose frames are done.
    void BeginFrame() {}
    void EndFrame();
    std::uint64_t FrameIndex() const { return m_frame; }

    /// Resources alive on the device (deferred ones included), for leak checks.
    virtual std::size_t LiveResources() const = 0;
    const Stats& GetStats() const { return m_stats; }
    const std::string& LastError() const { return m_error; }

protected:
    enum class Kind : std::uint8_t { Buffer, Texture, Sampler, Shader, Pipeline, SwapChain };
    virtual void Release(Kind k, std::uint32_t id) = 0;
    std::uint32_t NextId() { return ++m_nextId; }
    void SetError(const std::string& e) { m_error = e; }
    /// CreatePipeline's common checks (shader known to the backend, ValidatePipelineDesc); sets the error.
    bool CheckPipeline(const PipelineDesc& d, bool shaderKnown);
    void ReleaseAllDeferred();
    Stats m_stats;
private:
    void Defer(Kind k, std::uint32_t id);
    struct Pending { Kind kind; std::uint32_t id; std::uint64_t frame; };
    std::vector<Pending> m_pending;
    std::uint64_t m_frame = 0;
    std::uint32_t m_nextId = 0;
    std::string m_error;
};

/// A CPU device: rasterizes the command lists itself. Always available; the reference
/// the other backends are tested against.
std::unique_ptr<Device> CreateSoftwareDevice();

struct GLDeviceOptions {
    /// Called by Present() for a swap chain (swap the window's buffers). May be empty.
    std::function<void()> swapBuffers;
};
/// An OpenGL device on the CURRENT context. Needs GLRenderer::LoadGL() to have
/// succeeded (the RHI shares the renderer's loaded GL entry points, no second loader).
/// Null when GL is not loaded.
std::unique_ptr<Device> CreateOpenGLDevice(const GLDeviceOptions& opt = {});
/// OpenGL only: use an existing GL texture (made by other code) as an RHI texture. The
/// RHI never deletes it. Empty handle on other backends.
TextureHandle WrapGLTexture(Device& dev, unsigned int glTexture, const TextureDesc& d);
/// OpenGL only: the context is gone (or not current) and the device is being thrown away;
/// its destructor then makes no GL calls.
void AbandonGLDevice(Device& dev);

/// A Direct3D 11 device on an EXISTING ID3D11Device (passed as void* so this header needs no
/// Windows headers): the D3D11 renderer's own device and immediate context, no second device.
/// The RHI keeps a reference to both. Submit leaves the context's state as it found it.
/// Null on other platforms or when `d3d11Device` is null. Swap chains are off-screen
/// textures for now (like Software): the host reads or copies BackBuffer().
std::unique_ptr<Device> CreateD3D11Device(void* d3d11Device);
/// Direct3D 11 only: use an existing ID3D11Texture2D (void*) as an RHI texture. The RHI makes
/// its own views and holds a reference while the handle lives. Empty handle on other backends.
TextureHandle WrapD3D11Texture(Device& dev, void* d3d11Texture2D, const TextureDesc& d);
/// The text the Direct3D 11 backend puts before every HLSL stage (defines rhiClip).
extern const char* const kHlslPrelude;

} // namespace rhi
} // namespace okay
