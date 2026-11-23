// The OpenGL okay::platform::Painter (own code, off by default). See Painter.hpp.
//
// Written from the OpenGL 3.3 core and 3.0 specifications (Khronos) and the GLSL 1.30 / 3.30 language specifications, and from
// our own software painter (okayui/soft/OkayUI_SoftPainter.cpp), whose rules it follows: the same rounding from logical to device
// pixels, source-over blending with the destination alpha kept as a + d * (1 - a), "None" writing the colour as it is, nearest
// sampling at floor(u * w), the clip as a device rectangle. No other renderer's code was read or followed.
//
// How it draws: every primitive becomes triangles in device pixels on the CPU (rectangles and lines too, so their pixel coverage
// is the software painter's), appended to one vertex batch. A batch is drawn when the state it needs changes (texture, blend,
// clip, target) or before anything that must see the result (present, read-back, texture changes, a target switch). One shader:
// colour * texture (or colour alone). No GL header and no link-time GL: entry points come from Backend::GLProcAddress, enums are
// written out below, so the same file builds for GLX (Linux) and WGL (Windows) and costs nothing when it is never used.
#include "okay/Platform/Painter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#define OKAY_GLAPI __stdcall
#else
#define OKAY_GLAPI
#endif

namespace okay::platform {
namespace {

typedef unsigned int GLenum_; typedef unsigned int GLuint_; typedef int GLint_; typedef int GLsizei_;
typedef unsigned char GLboolean_; typedef float GLfloat_; typedef char GLchar_; typedef std::ptrdiff_t GLsizeiptr_;
typedef unsigned int GLbitfield_;

enum : unsigned {
    kTriangles = 0x0004, kUnsignedByte = 0x1401, kFloat = 0x1406,
    kCullFace = 0x0B44, kDepthTest = 0x0B71, kStencilTest = 0x0B90, kScissorTest = 0x0C11, kBlend = 0x0BE2,
    kUnpackAlignment = 0x0CF5, kUnpackRowLength = 0x0CF2, kPackAlignment = 0x0D05,
    kTexture2D = 0x0DE1, kMagFilter = 0x2800, kMinFilter = 0x2801, kWrapS = 0x2802, kWrapT = 0x2803,
    kNearest = 0x2600, kLinear = 0x2601, kClampToEdge = 0x812F, kRgba = 0x1908, kRgba8 = 0x8058,
    kTexture0 = 0x84C0, kSrcAlpha = 0x0302, kOneMinusSrcAlpha = 0x0303, kOne = 1, kFuncAdd = 0x8006,
    kArrayBuffer = 0x8892, kStreamDraw = 0x88E0, kColorBufferBit = 0x4000,
    kVertexShader = 0x8B31, kFragmentShader = 0x8B30, kCompileStatus = 0x8B81, kLinkStatus = 0x8B82,
    kFramebuffer = 0x8D40, kColorAttachment0 = 0x8CE0, kFramebufferComplete = 0x8CD5,
    kVersion = 0x1F02, kRenderer = 0x1F01, kFramebufferSrgb = 0x8DB9,
};

struct Gl {
#define OKAY_GL_FUNCS(F) \
    F(GLuint_, CreateShader, (GLenum_)) \
    F(void, ShaderSource, (GLuint_, GLsizei_, const GLchar_* const*, const GLint_*)) \
    F(void, CompileShader, (GLuint_)) \
    F(void, GetShaderiv, (GLuint_, GLenum_, GLint_*)) \
    F(GLuint_, CreateProgram, ()) \
    F(void, AttachShader, (GLuint_, GLuint_)) \
    F(void, BindAttribLocation, (GLuint_, GLuint_, const GLchar_*)) \
    F(void, LinkProgram, (GLuint_)) \
    F(void, GetProgramiv, (GLuint_, GLenum_, GLint_*)) \
    F(void, DeleteShader, (GLuint_)) \
    F(void, DeleteProgram, (GLuint_)) \
    F(void, UseProgram, (GLuint_)) \
    F(GLint_, GetUniformLocation, (GLuint_, const GLchar_*)) \
    F(void, Uniform2f, (GLint_, GLfloat_, GLfloat_)) \
    F(void, Uniform1i, (GLint_, GLint_)) \
    F(void, GenVertexArrays, (GLsizei_, GLuint_*)) \
    F(void, BindVertexArray, (GLuint_)) \
    F(void, DeleteVertexArrays, (GLsizei_, const GLuint_*)) \
    F(void, GenBuffers, (GLsizei_, GLuint_*)) \
    F(void, BindBuffer, (GLenum_, GLuint_)) \
    F(void, BufferData, (GLenum_, GLsizeiptr_, const void*, GLenum_)) \
    F(void, DeleteBuffers, (GLsizei_, const GLuint_*)) \
    F(void, VertexAttribPointer, (GLuint_, GLint_, GLenum_, GLboolean_, GLsizei_, const void*)) \
    F(void, EnableVertexAttribArray, (GLuint_)) \
    F(void, GenTextures, (GLsizei_, GLuint_*)) \
    F(void, BindTexture, (GLenum_, GLuint_)) \
    F(void, TexImage2D, (GLenum_, GLint_, GLint_, GLsizei_, GLsizei_, GLint_, GLenum_, GLenum_, const void*)) \
    F(void, TexSubImage2D, (GLenum_, GLint_, GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, const void*)) \
    F(void, TexParameteri, (GLenum_, GLenum_, GLint_)) \
    F(void, DeleteTextures, (GLsizei_, const GLuint_*)) \
    F(void, PixelStorei, (GLenum_, GLint_)) \
    F(void, ActiveTexture, (GLenum_)) \
    F(void, Viewport, (GLint_, GLint_, GLsizei_, GLsizei_)) \
    F(void, Scissor, (GLint_, GLint_, GLsizei_, GLsizei_)) \
    F(void, Enable, (GLenum_)) \
    F(void, Disable, (GLenum_)) \
    F(void, BlendFuncSeparate, (GLenum_, GLenum_, GLenum_, GLenum_)) \
    F(void, BlendEquation, (GLenum_)) \
    F(void, ClearColor, (GLfloat_, GLfloat_, GLfloat_, GLfloat_)) \
    F(void, Clear, (GLbitfield_)) \
    F(void, DrawArrays, (GLenum_, GLint_, GLsizei_)) \
    F(void, ReadPixels, (GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, void*)) \
    F(void, Finish, ()) \
    F(void, Flush, ()) \
    F(const unsigned char*, GetString, (GLenum_)) \
    F(void, GenFramebuffers, (GLsizei_, GLuint_*)) \
    F(void, BindFramebuffer, (GLenum_, GLuint_)) \
    F(void, FramebufferTexture2D, (GLenum_, GLenum_, GLenum_, GLuint_, GLint_)) \
    F(GLenum_, CheckFramebufferStatus, (GLenum_)) \
    F(void, DeleteFramebuffers, (GLsizei_, const GLuint_*))
#define OKAY_GL_DECL(ret, name, args) ret (OKAY_GLAPI *name) args = nullptr;
    OKAY_GL_FUNCS(OKAY_GL_DECL)
#undef OKAY_GL_DECL

    bool Load(Backend& b, std::string& missing) {
#define OKAY_GL_LOAD(ret, name, args) \
        name = reinterpret_cast<ret (OKAY_GLAPI *) args>(b.GLProcAddress("gl" #name)); \
        if (!name) { missing = "gl" #name; return false; }
        OKAY_GL_FUNCS(OKAY_GL_LOAD)
#undef OKAY_GL_LOAD
        return true;
    }
#undef OKAY_GL_FUNCS
};

constexpr int kMaxDim = 16384;

inline int Pin(double v) { return !(v == v) ? 0 : (int)std::max(-1e8, std::min(1e8, v)); }
inline int Round(double v) { return Pin(std::floor(v + 0.5)); }
inline int CeilC(double v) { return Pin(std::ceil(v - 0.5)); }   // first pixel whose centre is >= v
inline std::uint8_t Mul255(int a, int b) { const int t = a * b + 128; return (std::uint8_t)((t + (t >> 8)) >> 8); }

struct GTex {
    GLuint_ id = 0, fbo = 0;
    int w = 0, h = 0;
    BlendMode blend = BlendMode::Blend;
    ScaleMode scale = ScaleMode::Nearest;
    bool target = false;
    std::uint8_t mr = 255, mg = 255, mb = 255, ma = 255;
};

struct GVert { float x, y; std::uint8_t r, g, b, a; float u, v; };
static_assert(sizeof(GVert) == 20, "vertex layout");

// What a batch needs from GL besides its vertices.
struct DrawState {
    GLuint_ tex = 0;
    bool blend = true;
    bool clipOn = false;
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;   // device clip, top-left origin
    bool operator==(const DrawState& o) const {
        return tex == o.tex && blend == o.blend && clipOn == o.clipOn && (!clipOn || (cx0 == o.cx0 && cy0 == o.cy0 && cx1 == o.cx1 && cy1 == o.cy1));
    }
};

const char* kVs =
    "uniform vec2 uView;\n"
    "uniform vec2 uFlip;\n"
    "in vec2 aPos;\n"
    "in vec4 aCol;\n"
    "in vec2 aUv;\n"
    "out vec4 vCol;\n"
    "out vec2 vUv;\n"
    "void main() {\n"
    "    vec2 n = aPos / uView * 2.0 - 1.0;\n"
    "    gl_Position = vec4(n.x, n.y * uFlip.y, 0.0, 1.0);\n"
    "    vCol = aCol;\n"
    "    vUv = aUv;\n"
    "}\n";
const char* kFs =
    "uniform sampler2D uTex;\n"
    "uniform int uUseTex;\n"
    "in vec4 vCol;\n"
    "in vec2 vUv;\n"
    "out vec4 oCol;\n"
    "void main() {\n"
    "    vec4 c = vCol;\n"
    "    if (uUseTex != 0) c *= texture(uTex, vUv);\n"
    "    oCol = c;\n"
    "}\n";

// The painter whose context is current on this (the drawing) thread; another painter makes its own current before it draws.
const void* g_currentPainter = nullptr;

class GLPainter final : public Painter {
public:
    GLPainter(Window& w, bool vsync) : m_win(w), m_vsync(vsync) {}
    ~GLPainter() override {
        ReleaseCachedTextures();
        if (m_ready && m_win.MakeCurrent()) {
            g_currentPainter = this;
            for (GTex* t : m_texs) FreeTex(t);
            if (m_vbo) m_gl.DeleteBuffers(1, &m_vbo);
            if (m_vao) m_gl.DeleteVertexArrays(1, &m_vao);
            if (m_prog) m_gl.DeleteProgram(m_prog);
        }
        for (GTex* t : m_texs) delete t;
        m_texs.clear();
        if (g_currentPainter == this) g_currentPainter = nullptr;
        m_win.DestroyGLContext();
    }

    bool Start(Backend& b, std::string& err) {
        GLAttributes a; a.major = 3; a.minor = 3; a.compatibility = false; a.depthBits = 0;
        if (!m_win.CreateGLContext(a)) {
            GLAttributes legacy; legacy.major = 3; legacy.minor = 0; legacy.compatibility = true; legacy.depthBits = 0;
            if (!m_win.CreateGLContext(legacy)) { err = "the window has no GL context (was it made with WindowDesc::opengl?)"; return false; }
        }
        g_currentPainter = this;
        std::string missing;
        if (!m_gl.Load(b, missing)) { err = "OpenGL 3.0 entry point missing: " + missing; return false; }
        const char* ver = (const char*)m_gl.GetString(kVersion);
        int major = 0;
        if (ver) major = std::atoi(ver);
        if (major < 3) { err = std::string("OpenGL 3.0 or newer needed, the driver offers ") + (ver ? ver : "nothing"); return false; }
        if (!BuildProgram()) { err = "the painter's shaders did not compile (GLSL 3.30 and 1.30 tried)"; return false; }
        m_gl.GenVertexArrays(1, &m_vao);
        m_gl.GenBuffers(1, &m_vbo);
        m_gl.BindVertexArray(m_vao);
        m_gl.BindBuffer(kArrayBuffer, m_vbo);
        m_gl.EnableVertexAttribArray(0);
        m_gl.EnableVertexAttribArray(1);
        m_gl.EnableVertexAttribArray(2);
        m_gl.VertexAttribPointer(0, 2, kFloat, 0, sizeof(GVert), (const void*)offsetof(GVert, x));
        m_gl.VertexAttribPointer(1, 4, kUnsignedByte, 1, sizeof(GVert), (const void*)offsetof(GVert, r));
        m_gl.VertexAttribPointer(2, 2, kFloat, 0, sizeof(GVert), (const void*)offsetof(GVert, u));
        m_gl.Disable(kCullFace); m_gl.Disable(kDepthTest); m_gl.Disable(kStencilTest);
        m_gl.Disable(kFramebufferSrgb);
        m_gl.BlendEquation(kFuncAdd);
        m_gl.BlendFuncSeparate(kSrcAlpha, kOneMinusSrcAlpha, kOne, kOneMinusSrcAlpha);
        m_gl.ActiveTexture(kTexture0);
        m_gl.PixelStorei(kUnpackAlignment, 1);
        m_gl.PixelStorei(kPackAlignment, 1);
        if (const char* r = (const char*)m_gl.GetString(kRenderer)) m_rendererName = r;
        m_swapSynced = m_win.SetSwapInterval(m_vsync ? 1 : 0) && m_vsync;
        SyncSize();
        m_ready = true;
        return true;
    }

    PainterKind Kind() const override { return PainterKind::OpenGL; }
    void* NativeRenderer() override { return nullptr; }
    std::string DriverName() override { return "opengl"; }
    bool IsAccelerated() override { return true; }
    /// The GL_RENDERER string (for a log line: "llvmpipe" means Mesa's CPU rasteriser).
    const std::string& RendererName() const { return m_rendererName; }

    // ---- target ---------------------------------------------------------------------------
    void OutputSize(int* w, int* h) override { SyncSize(); if (w) *w = m_w; if (h) *h = m_h; }
    void Clear() override {
        Use(); FlushBatch(); SyncSize();
        BindTarget();
        m_gl.Disable(kScissorTest);
        m_gl.ClearColor(m_r / 255.0f, m_g / 255.0f, m_b / 255.0f, m_a / 255.0f);
        m_gl.Clear(kColorBufferBit);
    }
    void SetBlendMode(BlendMode m) override { m_blend = m; }
    BlendMode GetBlendMode() override { return m_blend; }
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override { m_r = r; m_g = g; m_b = b; m_a = a; }
    bool GetDrawColor(std::uint8_t* r, std::uint8_t* g, std::uint8_t* b, std::uint8_t* a) override { if (r) *r = m_r; if (g) *g = m_g; if (b) *b = m_b; if (a) *a = m_a; return true; }

    // ---- clip and scale ---------------------------------------------------------------------
    void SetClipRect(const ui2d::Rect* r) override { if (r) { m_clipOn = true; m_clip = *r; } else m_clipOn = false; }
    void GetClipRect(ui2d::Rect* r) override { if (r) *r = m_clipOn ? m_clip : ui2d::Rect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return m_clipOn; }
    void SetScale(float sx, float sy) override { if (sx > 0 && sy > 0 && std::isfinite(sx) && std::isfinite(sy)) { m_sx = sx; m_sy = sy; } }

    // ---- primitives -------------------------------------------------------------------------
    void FillRect(const ui2d::Rect* r) override {
        SyncSize();
        if (!r) { FillDev(0, 0, m_w, m_h); return; }
        FillDev(Round(r->x * (double)m_sx), Round(r->y * (double)m_sy), Round((r->x + (double)r->w) * m_sx), Round((r->y + (double)r->h) * m_sy));
    }
    void FillRectF(const ui2d::FRect* r) override {
        SyncSize();
        if (!r) { FillDev(0, 0, m_w, m_h); return; }
        FillDev(CeilC(r->x * (double)m_sx), CeilC(r->y * (double)m_sy), CeilC((r->x + (double)r->w) * m_sx), CeilC((r->y + (double)r->h) * m_sy));
    }
    void DrawRect(const ui2d::Rect* r) override {
        SyncSize();
        const ui2d::Rect full{0, 0, (int)std::lround(m_w / m_sx), (int)std::lround(m_h / m_sy)};
        const ui2d::Rect rc = r ? *r : full;
        if (rc.w <= 0 || rc.h <= 0) return;
        const int x0 = Round(rc.x * (double)m_sx), y0 = Round(rc.y * (double)m_sy);
        const int x1 = std::max(x0 + 1, Round((rc.x + (double)rc.w) * m_sx)), y1 = std::max(y0 + 1, Round((rc.y + (double)rc.h) * m_sy));
        FillDev(x0, y0, x1, y0 + 1);
        if (y1 - 1 > y0) FillDev(x0, y1 - 1, x1, y1);
        if (y1 - y0 > 2) { FillDev(x0, y0 + 1, x0 + 1, y1 - 1); if (x1 - 1 > x0) FillDev(x1 - 1, y0 + 1, x1, y1 - 1); }
    }
    void DrawPoint(int x, int y) override {
        const int dx = Round((x + 0.5) * m_sx - 0.5), dy = Round((y + 0.5) * m_sy - 0.5);
        FillDev(dx, dy, dx + std::max(1, Round(m_sx)), dy + std::max(1, Round(m_sy)));
    }
    void DrawLine(int x0, int y0, int x1, int y1) override { Line(Round(x0 * (double)m_sx), Round(y0 * (double)m_sy), Round(x1 * (double)m_sx), Round(y1 * (double)m_sy)); }
    void DrawLineF(float x0, float y0, float x1, float y1) override { Line(Pin(std::floor(x0 * (double)m_sx)), Pin(std::floor(y0 * (double)m_sy)), Pin(std::floor(x1 * (double)m_sx)), Pin(std::floor(y1 * (double)m_sy))); }

    // ---- textures ---------------------------------------------------------------------------
    ui2d::Tex CreateTexture(const TextureDesc& d, const std::uint8_t* rgba) override {
        if (d.width <= 0 || d.height <= 0 || d.width > kMaxDim || d.height > kMaxDim) return nullptr;
        Use(); FlushBatch();
        GTex* t = new GTex();
        t->w = d.width; t->h = d.height; t->blend = d.blend; t->scale = d.scale; t->target = d.target;
        m_gl.GenTextures(1, &t->id);
        m_gl.BindTexture(kTexture2D, t->id);
        ApplyFilter(*t);
        m_gl.TexParameteri(kTexture2D, kWrapS, (GLint_)kClampToEdge);
        m_gl.TexParameteri(kTexture2D, kWrapT, (GLint_)kClampToEdge);
        m_gl.PixelStorei(kUnpackRowLength, 0);
        std::vector<std::uint8_t> zero;
        if (!rgba) { zero.assign((std::size_t)d.width * (std::size_t)d.height * 4, 0); rgba = zero.data(); }
        m_gl.TexImage2D(kTexture2D, 0, (GLint_)kRgba8, d.width, d.height, 0, kRgba, kUnsignedByte, rgba);
        if (d.target) {
            m_gl.GenFramebuffers(1, &t->fbo);
            m_gl.BindFramebuffer(kFramebuffer, t->fbo);
            m_gl.FramebufferTexture2D(kFramebuffer, kColorAttachment0, kTexture2D, t->id, 0);
            const bool ok = m_gl.CheckFramebufferStatus(kFramebuffer) == kFramebufferComplete;
            BindTarget();
            if (!ok) { FreeTex(t); delete t; return nullptr; }
        }
        m_texs.insert(t);
        return t;
    }
    bool UpdateTexture(ui2d::Tex tex, const ui2d::Rect* r, const void* rgba, int pitch) override {
        GTex* t = T(tex);
        if (!t || !rgba) return false;
        ui2d::Rect rc{0, 0, t->w, t->h};
        if (r) rc = *r;
        if (rc.x < 0 || rc.y < 0 || rc.w <= 0 || rc.h <= 0 || rc.x + (long long)rc.w > t->w || rc.y + (long long)rc.h > t->h) return false;
        if (pitch < rc.w * 4) return false;
        Use(); FlushBatch();
        m_gl.BindTexture(kTexture2D, t->id);
        if (pitch % 4 == 0) {
            m_gl.PixelStorei(kUnpackRowLength, pitch / 4);
            m_gl.TexSubImage2D(kTexture2D, 0, rc.x, rc.y, rc.w, rc.h, kRgba, kUnsignedByte, rgba);
            m_gl.PixelStorei(kUnpackRowLength, 0);
        } else {   // a pitch that is not whole pixels: pack the rows first
            std::vector<std::uint8_t> packed((std::size_t)rc.w * (std::size_t)rc.h * 4);
            for (int y = 0; y < rc.h; ++y)
                std::memcpy(&packed[(std::size_t)y * (std::size_t)rc.w * 4], (const std::uint8_t*)rgba + (std::size_t)y * (std::size_t)pitch, (std::size_t)rc.w * 4);
            m_gl.TexSubImage2D(kTexture2D, 0, rc.x, rc.y, rc.w, rc.h, kRgba, kUnsignedByte, packed.data());
        }
        return true;
    }
    void DestroyTexture(ui2d::Tex tex) override {
        if ((GTex*)tex == m_target) SetRenderTarget(nullptr);
        GTex* t = T(tex);
        if (!t) return;
        Use(); FlushBatch();
        m_texs.erase(t);
        FreeTex(t);
        delete t;
    }
    void SetTextureBlendMode(ui2d::Tex t, BlendMode m) override { if (GTex* s = T(t)) s->blend = m; }
    void SetTextureScaleMode(ui2d::Tex t, ScaleMode m) override {
        GTex* s = T(t);
        if (!s || s->scale == m) return;
        Use(); FlushBatch();
        s->scale = m;
        m_gl.BindTexture(kTexture2D, s->id);
        ApplyFilter(*s);
    }
    void QueryTexture(ui2d::Tex t, int* w, int* h) override {
        GTex* s = (GTex*)t == m_target ? m_target : T(t);
        if (w) *w = s ? s->w : 0;
        if (h) *h = s ? s->h : 0;
    }
    void SetTextureColorMod(ui2d::Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override { if (GTex* s = T(t)) { s->mr = r; s->mg = g; s->mb = b; } }
    void SetTextureAlphaMod(ui2d::Tex t, std::uint8_t a) override { if (GTex* s = T(t)) s->ma = a; }

    void Copy(ui2d::Tex t, const ui2d::Rect* s, const ui2d::Rect* d) override {
        SyncSize();
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, 0.0, nullptr, 0);
        else Blit(t, s, 0, 0, m_w / m_sx, m_h / m_sy, 0.0, nullptr, 0);
    }
    void CopyF(ui2d::Tex t, const ui2d::Rect* s, const ui2d::FRect* d) override {
        SyncSize();
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, 0.0, nullptr, 0);
        else Blit(t, s, 0, 0, m_w / m_sx, m_h / m_sy, 0.0, nullptr, 0);
    }
    void CopyEx(ui2d::Tex t, const ui2d::Rect* s, const ui2d::Rect* d, double angle, const ui2d::Point* c, int flip) override {
        SyncSize();
        ui2d::FPoint fc; if (c) fc = ui2d::FPoint{(float)c->x, (float)c->y};
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, angle, c ? &fc : nullptr, flip);
        else Blit(t, s, 0, 0, m_w / m_sx, m_h / m_sy, angle, c ? &fc : nullptr, flip);
    }
    void CopyExF(ui2d::Tex t, const ui2d::Rect* s, const ui2d::FRect* d, double angle, const ui2d::FPoint* c, int flip) override {
        SyncSize();
        if (d) Blit(t, s, d->x, d->y, d->w, d->h, angle, c, flip);
        else Blit(t, s, 0, 0, m_w / m_sx, m_h / m_sy, angle, c, flip);
    }

    void Geometry(ui2d::Tex tex, const ui2d::Vertex* v, int nv, const int* idx, int ni) override {
        if (!v || nv <= 0) return;
        GTex* st = tex ? T(tex) : nullptr;
        if (tex && !st) return;
        const int n = idx ? ni : nv;
        if (n < 3) return;
        SyncSize();
        DrawState s = StateFor(st, (st ? st->blend : m_blend) == BlendMode::Blend);
        if (s.clipOn && (s.cx0 >= s.cx1 || s.cy0 >= s.cy1)) return;
        GVert* out = Reserve(s, (std::size_t)(n / 3) * 3);
        std::size_t k = 0;
        for (int i = 0; i + 3 <= n; i += 3) {
            const int a = idx ? idx[i] : i, b = idx ? idx[i + 1] : i + 1, c = idx ? idx[i + 2] : i + 2;
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
            for (int j : {a, b, c}) {
                GVert& o = out[k++];
                o.x = (float)(v[j].position.x * (double)m_sx); o.y = (float)(v[j].position.y * (double)m_sy);
                o.r = v[j].color.r; o.g = v[j].color.g; o.b = v[j].color.b; o.a = v[j].color.a;
                if (st) { o.r = Mul255(o.r, st->mr); o.g = Mul255(o.g, st->mg); o.b = Mul255(o.b, st->mb); o.a = Mul255(o.a, st->ma); }
                o.u = v[j].tex_coord.x; o.v = v[j].tex_coord.y;
            }
        }
        m_batch.resize(m_batch.size() - ((std::size_t)(n / 3) * 3 - k));   // skipped triangles
    }

    // ---- render targets ---------------------------------------------------------------------
    bool SetRenderTarget(ui2d::Tex tex) override {
        GTex* nt = nullptr;
        if (tex) { nt = (GTex*)tex; if (!m_texs.count(nt) || !nt->target) return false; }
        if (nt == m_target) return true;
        Use(); FlushBatch();
        if (m_target) {   // back to the window's clip and scale
            m_clip = m_winClip; m_clipOn = m_winClipOn; m_sx = m_winSx; m_sy = m_winSy;
            m_target = nullptr;
        }
        if (nt) {
            m_winClip = m_clip; m_winClipOn = m_clipOn; m_winSx = m_sx; m_winSy = m_sy;
            m_clipOn = false; m_sx = m_sy = 1.0f;
            m_target = nt;
        }
        SyncSize();
        BindTarget();
        return true;
    }
    ui2d::Tex GetRenderTarget() override { return m_target; }
    void Flush() override { Use(); FlushBatch(); m_gl.Flush(); }

    // ---- the frame --------------------------------------------------------------------------
    void Present() override {
        Use(); FlushBatch();
        if (m_target) SetRenderTarget(nullptr);
        m_win.SwapBuffers();
        if (m_vsync && !m_swapSynced) Pace();
    }
    bool SetVSync(bool on) override {
        Use();
        m_vsync = on;
        m_swapSynced = m_win.SetSwapInterval(on ? 1 : 0) && on;
        return m_swapSynced;
    }
    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override {
        Use(); FlushBatch(); SyncSize();
        BindTarget();
        w = m_w; h = m_h;
        rgba.assign((std::size_t)w * (std::size_t)h * 4, 0);
        m_gl.ReadPixels(0, 0, w, h, kRgba, kUnsignedByte, rgba.data());
        if (!m_target) {   // the window's rows come bottom-up; a target's are already top-down (see the vertex shader)
            std::vector<std::uint8_t> row((std::size_t)w * 4);
            for (int y = 0; y < h / 2; ++y) {
                std::uint8_t* a = &rgba[(std::size_t)y * w * 4];
                std::uint8_t* b = &rgba[(std::size_t)(h - 1 - y) * w * 4];
                std::memcpy(row.data(), a, row.size()); std::memcpy(a, b, row.size()); std::memcpy(b, row.data(), row.size());
            }
        }
        return !rgba.empty();
    }

private:
    void Use() {
        if (g_currentPainter == this) return;
        m_win.MakeCurrent();
        g_currentPainter = this;
    }
    GTex* T(ui2d::Tex t) const {   // the render target cannot be drawn from
        GTex* s = (GTex*)t;
        return s && s != m_target && m_texs.count(s) ? s : nullptr;
    }
    void FreeTex(GTex* t) {
        if (t->fbo) m_gl.DeleteFramebuffers(1, &t->fbo);
        if (t->id) m_gl.DeleteTextures(1, &t->id);
        t->fbo = t->id = 0;
    }
    void ApplyFilter(const GTex& t) {
        const GLint_ f = (GLint_)(t.scale == ScaleMode::Linear ? kLinear : kNearest);
        m_gl.TexParameteri(kTexture2D, kMinFilter, f);
        m_gl.TexParameteri(kTexture2D, kMagFilter, f);
    }
    void SyncSize() {
        if (m_target) { m_w = m_target->w; m_h = m_target->h; return; }
        int w = 0, h = 0;
        m_win.GetDrawableSize(w, h);
        if (w > 0 && h > 0) { m_w = std::min(w, kMaxDim); m_h = std::min(h, kMaxDim); }
    }
    void BindTarget() {
        m_gl.BindFramebuffer(kFramebuffer, m_target ? m_target->fbo : 0);
        m_gl.Viewport(0, 0, m_w, m_h);
    }
    bool BuildProgram() {
        for (const char* version : {"#version 330 core\n", "#version 130\n"}) {
            auto compile = [&](GLenum_ kind, const char* body) -> GLuint_ {
                GLuint_ sh = m_gl.CreateShader(kind);
                const GLchar_* src[2] = {version, body};
                m_gl.ShaderSource(sh, 2, src, nullptr);
                m_gl.CompileShader(sh);
                GLint_ ok = 0; m_gl.GetShaderiv(sh, kCompileStatus, &ok);
                if (!ok) { m_gl.DeleteShader(sh); return 0; }
                return sh;
            };
            const GLuint_ vs = compile(kVertexShader, kVs), fs = compile(kFragmentShader, kFs);
            if (!vs || !fs) { if (vs) m_gl.DeleteShader(vs); if (fs) m_gl.DeleteShader(fs); continue; }
            GLuint_ p = m_gl.CreateProgram();
            m_gl.AttachShader(p, vs); m_gl.AttachShader(p, fs);
            m_gl.BindAttribLocation(p, 0, "aPos"); m_gl.BindAttribLocation(p, 1, "aCol"); m_gl.BindAttribLocation(p, 2, "aUv");
            m_gl.LinkProgram(p);
            m_gl.DeleteShader(vs); m_gl.DeleteShader(fs);
            GLint_ ok = 0; m_gl.GetProgramiv(p, kLinkStatus, &ok);
            if (!ok) { m_gl.DeleteProgram(p); continue; }
            m_prog = p;
            m_uView = m_gl.GetUniformLocation(p, "uView");
            m_uFlip = m_gl.GetUniformLocation(p, "uFlip");
            m_uTex = m_gl.GetUniformLocation(p, "uTex");
            m_uUseTex = m_gl.GetUniformLocation(p, "uUseTex");
            return true;
        }
        return false;
    }

    DrawState StateFor(const GTex* t, bool blend) const {
        DrawState s;
        s.tex = t ? t->id : 0;
        s.blend = blend;
        if (m_clipOn) {
            s.clipOn = true;
            if (m_clip.w <= 0 || m_clip.h <= 0) { s.cx0 = s.cy0 = s.cx1 = s.cy1 = 0; return s; }
            s.cx0 = std::max(0, Round(m_clip.x * (double)m_sx)); s.cy0 = std::max(0, Round(m_clip.y * (double)m_sy));
            s.cx1 = std::min(m_w, Round((m_clip.x + (double)m_clip.w) * m_sx)); s.cy1 = std::min(m_h, Round((m_clip.y + (double)m_clip.h) * m_sy));
        }
        return s;
    }
    // Room for `n` more vertices drawn with `s` (the batch so far is drawn first when its state differs).
    GVert* Reserve(const DrawState& s, std::size_t n) {
        if (!m_batch.empty() && !(s == m_state)) { Use(); FlushBatch(); }
        m_state = s;
        const std::size_t at = m_batch.size();
        m_batch.resize(at + n);
        return m_batch.data() + at;
    }
    void FlushBatch() {
        if (m_batch.empty()) return;
        Use();
        BindTarget();
        m_gl.UseProgram(m_prog);
        m_gl.BindVertexArray(m_vao);
        m_gl.BindBuffer(kArrayBuffer, m_vbo);
        m_gl.BufferData(kArrayBuffer, (GLsizeiptr_)(m_batch.size() * sizeof(GVert)), m_batch.data(), kStreamDraw);
        m_gl.Uniform2f(m_uView, (float)m_w, (float)m_h);
        m_gl.Uniform2f(m_uFlip, 1.0f, m_target ? 1.0f : -1.0f);
        m_gl.Uniform1i(m_uTex, 0);
        m_gl.Uniform1i(m_uUseTex, m_state.tex ? 1 : 0);
        m_gl.BindTexture(kTexture2D, m_state.tex);
        if (m_state.blend) m_gl.Enable(kBlend); else m_gl.Disable(kBlend);
        if (m_state.clipOn) {
            m_gl.Enable(kScissorTest);
            const int w = std::max(0, m_state.cx1 - m_state.cx0), h = std::max(0, m_state.cy1 - m_state.cy0);
            m_gl.Scissor(m_state.cx0, m_target ? m_state.cy0 : m_h - m_state.cy0 - h, w, h);
        } else m_gl.Disable(kScissorTest);
        m_gl.DrawArrays(kTriangles, 0, (GLsizei_)m_batch.size());
        m_batch.clear();
    }

    // The device rectangle [x0, x1) x [y0, y1) in the draw colour: two triangles on whole pixel edges, so a pixel is covered
    // exactly when its centre is inside, the same pixels the software painter fills.
    void FillDev(int x0, int y0, int x1, int y1) {
        if (x0 >= x1 || y0 >= y1) return;
        const bool blend = m_blend == BlendMode::Blend;
        if (blend && m_a == 0) return;
        const DrawState s = StateFor(nullptr, blend);
        if (s.clipOn && (s.cx0 >= s.cx1 || s.cy0 >= s.cy1)) return;
        Quad(Reserve(s, 6), (float)x0, (float)y0, (float)x1, (float)y1);
    }
    void Quad(GVert* o, float x0, float y0, float x1, float y1) {
        const GVert a{x0, y0, m_r, m_g, m_b, m_a, 0, 0}, b{x1, y0, m_r, m_g, m_b, m_a, 0, 0};
        const GVert c{x1, y1, m_r, m_g, m_b, m_a, 0, 0}, d{x0, y1, m_r, m_g, m_b, m_a, 0, 0};
        o[0] = a; o[1] = b; o[2] = c; o[3] = a; o[4] = c; o[5] = d;
    }
    // Bresenham in device pixels, both ends included; runs along a row become one rectangle.
    void Line(int x0, int y0, int x1, int y1) {
        const bool blend = m_blend == BlendMode::Blend;
        if (blend && m_a == 0) return;
        const long long dx = std::llabs((long long)x1 - x0), dy = -std::llabs((long long)y1 - y0);
        if (dx > 100000 || -dy > 100000) return;
        const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        long long err = dx + dy;
        int runX = x0, runY = y0, runEnd = x0;
        auto emit = [&]() { const int a = std::min(runX, runEnd), b = std::max(runX, runEnd); FillDev(a, runY, b + 1, runY + 1); };
        for (;;) {
            if (x0 == x1 && y0 == y1) break;
            const long long e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
            if (y0 != runY) { emit(); runX = x0; runY = y0; }
            runEnd = x0;
        }
        emit();
    }

    // A source rectangle of a texture into the destination rectangle (logical units), rotated by `angle` degrees about `centre`
    // (relative to the destination's top-left; default its middle), flipped (1 = horizontal, 2 = vertical).
    void Blit(ui2d::Tex tex, const ui2d::Rect* src, double dx, double dy, double dw, double dh, double angle, const ui2d::FPoint* centre, int flip) {
        GTex* st = T(tex);
        if (!st || !(dw > 0) || !(dh > 0) || !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dw) || !std::isfinite(dh)) return;
        ui2d::Rect sr{0, 0, st->w, st->h};
        if (src) sr = *src;
        if (sr.x < 0 || sr.y < 0 || sr.w <= 0 || sr.h <= 0 || sr.x + (long long)sr.w > st->w || sr.y + (long long)sr.h > st->h) return;
        const DrawState s = StateFor(st, st->blend == BlendMode::Blend);
        if (s.clipOn && (s.cx0 >= s.cx1 || s.cy0 >= s.cy1)) return;
        const double X = dx * m_sx, Y = dy * m_sy, Wd = dw * m_sx, Hd = dh * m_sy;
        double px[4] = {X, X + Wd, X + Wd, X}, py[4] = {Y, Y, Y + Hd, Y + Hd};
        const bool rotated = angle != 0.0 && std::isfinite(angle) && std::fmod(angle, 360.0) != 0.0;
        if (rotated) {
            const double cx = X + (centre ? centre->x * m_sx : Wd * 0.5), cy = Y + (centre ? centre->y * m_sy : Hd * 0.5);
            const double rad = angle * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad);
            for (int i = 0; i < 4; ++i) {
                const double ox = px[i] - cx, oy = py[i] - cy;
                px[i] = cx + ox * cs - oy * sn; py[i] = cy + ox * sn + oy * cs;
            }
        }
        float u0 = (float)((double)sr.x / st->w), u1 = (float)((double)(sr.x + sr.w) / st->w);
        float v0 = (float)((double)sr.y / st->h), v1 = (float)((double)(sr.y + sr.h) / st->h);
        if (flip & 1) std::swap(u0, u1);
        if (flip & 2) std::swap(v0, v1);
        const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
        GVert q[4];
        for (int i = 0; i < 4; ++i) q[i] = GVert{(float)px[i], (float)py[i], st->mr, st->mg, st->mb, st->ma, us[i], vs[i]};
        GVert* o = Reserve(s, 6);
        o[0] = q[0]; o[1] = q[1]; o[2] = q[2]; o[3] = q[0]; o[4] = q[2]; o[5] = q[3];
    }

    void Pace() {   // no display sync (no swap interval): wait out the rest of a refresh period so a loop does not spin
        int hz = m_win.RefreshRateHz();
        if (hz < 20 || hz > 360) hz = 60;
        const std::uint64_t period = 1000u / (std::uint64_t)hz;
        const std::uint64_t now = TicksMs();
        if (m_lastPresent != 0 && now < m_lastPresent + period) { DelayMs((std::uint32_t)(m_lastPresent + period - now)); m_lastPresent += period; }
        else m_lastPresent = now;
    }

    Window& m_win;
    Gl m_gl;
    bool m_ready = false;
    bool m_vsync;
    bool m_swapSynced = false;
    std::uint64_t m_lastPresent = 0;
    std::string m_rendererName;
    GLuint_ m_prog = 0, m_vao = 0, m_vbo = 0;
    GLint_ m_uView = -1, m_uFlip = -1, m_uTex = -1, m_uUseTex = -1;
    int m_w = 1, m_h = 1;
    GTex* m_target = nullptr;
    ui2d::Rect m_winClip{0, 0, 0, 0}; bool m_winClipOn = false; float m_winSx = 1, m_winSy = 1;
    std::uint8_t m_r = 0, m_g = 0, m_b = 0, m_a = 255;
    BlendMode m_blend = BlendMode::None;   // SDL's default draw blend mode
    bool m_clipOn = false;
    ui2d::Rect m_clip{0, 0, 0, 0};
    float m_sx = 1.0f, m_sy = 1.0f;
    std::unordered_set<GTex*> m_texs;
    std::vector<GVert> m_batch;
    DrawState m_state;
};

} // namespace

std::unique_ptr<Painter> CreateGLPainter(Window& w, Backend& b, const PainterDesc& d, std::string* err) {
    if (w.Kind() == BackendKind::Sdl2) {
        if (err) *err = "an sdl2 window is drawn through SDL_Renderer; the OpenGL painter needs a window of the own backend";
        return nullptr;
    }
    std::unique_ptr<GLPainter> p(new GLPainter(w, d.vsync));
    std::string e;
    if (!p->Start(b, e)) {
        if (err) *err = e;
        return nullptr;
    }
    return p;
}

} // namespace okay::platform
