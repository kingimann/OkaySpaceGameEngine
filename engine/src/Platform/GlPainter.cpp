// okay::platform::Painter over OpenGL: our own GPU painter (CreateGLPainter in okay/Platform/Painter.hpp).
//
// Written from the OpenGL 1.1 specification (vertex arrays, glOrtho, glScissor, glTexImage2D / glTexSubImage2D,
// GL_MODULATE texture environment) and the Painter contract in Painter.hpp / UIPainter.hpp. No GL header and no
// link-time GL dependency: every entry point comes from Backend::GLProcAddress, so the same code serves the sdl2
// backend (SDL_GL), X11/GLX and Win32/WGL. The enum values below are the numbers of the GL specification.
//
// Drawing model, the same as the software painter (okayui/soft) so the two give the same picture:
//  * coordinates are logical units; SetScale multiplies them into device pixels (the projection does it);
//  * a pixel is covered when its centre is inside a triangle (GL's own rasterisation rule);
//  * the vertex colour multiplies the texel, then the texture's colour / alpha modulation;
//  * BlendMode::Blend is source-over (alpha: src + dst * (1 - src) when glBlendFuncSeparate exists).
// What differs: GPU sampling (bilinear on the GPU, not the CPU's rounding), and lines are GL lines.
//
// OWN CODE, NOT READY: used only by the launcher's own renderer (OKAY_LAUNCHER_RENDERER=own), off by default.
#include "okay/Platform/Painter.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  define OKAY_GLAPI __stdcall
#else
#  define OKAY_GLAPI
#endif

namespace okay::platform {
namespace {

typedef unsigned int GLenum_;
typedef unsigned int GLuint_;
typedef int GLint_;
typedef int GLsizei_;
typedef float GLfloat_;
typedef double GLdouble_;
typedef unsigned int GLbitfield_;
typedef float GLclampf_;

enum : unsigned {
    kPoints = 0x0000, kLines = 0x0001, kTriangles = 0x0004,
    kUnsignedByte = 0x1401, kUnsignedInt = 0x1405, kFloat = 0x1406,
    kCullFace = 0x0B44, kLighting = 0x0B50, kDepthTest = 0x0B71, kAlphaTest = 0x0BC0, kBlend = 0x0BE2,
    kScissorTest = 0x0C11, kTexture2D = 0x0DE1,
    kSrcAlpha = 0x0302, kOneMinusSrcAlpha = 0x0303, kOne = 0x0001,
    kColorBufferBit = 0x4000,
    kModelView = 0x1700, kProjection = 0x1701,
    kVertexArray = 0x8074, kColorArray = 0x8076, kTexCoordArray = 0x8078,
    kRgba = 0x1908, kRgba8 = 0x8058,
    kTexMagFilter = 0x2800, kTexMinFilter = 0x2801, kTexWrapS = 0x2802, kTexWrapT = 0x2803,
    kNearest = 0x2600, kLinear = 0x2601, kClampToEdge = 0x812F,
    kUnpackRowLength = 0x0CF2, kUnpackAlignment = 0x0CF5, kPackAlignment = 0x0D05,
    kTextureEnv = 0x2300, kTextureEnvMode = 0x2200, kModulate = 0x2100,
    kMaxTextureSize = 0x0D33,
};

struct Gl {
    void (OKAY_GLAPI *Viewport)(GLint_, GLint_, GLsizei_, GLsizei_) = nullptr;
    void (OKAY_GLAPI *ClearColor)(GLclampf_, GLclampf_, GLclampf_, GLclampf_) = nullptr;
    void (OKAY_GLAPI *Clear)(GLbitfield_) = nullptr;
    void (OKAY_GLAPI *Enable)(GLenum_) = nullptr;
    void (OKAY_GLAPI *Disable)(GLenum_) = nullptr;
    void (OKAY_GLAPI *BlendFunc)(GLenum_, GLenum_) = nullptr;
    void (OKAY_GLAPI *Scissor)(GLint_, GLint_, GLsizei_, GLsizei_) = nullptr;
    void (OKAY_GLAPI *MatrixMode)(GLenum_) = nullptr;
    void (OKAY_GLAPI *LoadIdentity)() = nullptr;
    void (OKAY_GLAPI *Ortho)(GLdouble_, GLdouble_, GLdouble_, GLdouble_, GLdouble_, GLdouble_) = nullptr;
    void (OKAY_GLAPI *GenTextures)(GLsizei_, GLuint_*) = nullptr;
    void (OKAY_GLAPI *DeleteTextures)(GLsizei_, const GLuint_*) = nullptr;
    void (OKAY_GLAPI *BindTexture)(GLenum_, GLuint_) = nullptr;
    void (OKAY_GLAPI *TexImage2D)(GLenum_, GLint_, GLint_, GLsizei_, GLsizei_, GLint_, GLenum_, GLenum_, const void*) = nullptr;
    void (OKAY_GLAPI *TexSubImage2D)(GLenum_, GLint_, GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, const void*) = nullptr;
    void (OKAY_GLAPI *TexParameteri)(GLenum_, GLenum_, GLint_) = nullptr;
    void (OKAY_GLAPI *TexEnvi)(GLenum_, GLenum_, GLint_) = nullptr;
    void (OKAY_GLAPI *PixelStorei)(GLenum_, GLint_) = nullptr;
    void (OKAY_GLAPI *EnableClientState)(GLenum_) = nullptr;
    void (OKAY_GLAPI *VertexPointer)(GLint_, GLenum_, GLsizei_, const void*) = nullptr;
    void (OKAY_GLAPI *ColorPointer)(GLint_, GLenum_, GLsizei_, const void*) = nullptr;
    void (OKAY_GLAPI *TexCoordPointer)(GLint_, GLenum_, GLsizei_, const void*) = nullptr;
    void (OKAY_GLAPI *DrawElements)(GLenum_, GLsizei_, GLenum_, const void*) = nullptr;
    void (OKAY_GLAPI *DrawArrays)(GLenum_, GLint_, GLsizei_) = nullptr;
    void (OKAY_GLAPI *ReadPixels)(GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, void*) = nullptr;
    void (OKAY_GLAPI *GetIntegerv)(GLenum_, GLint_*) = nullptr;
    // GL 1.4; optional (the alpha channel of the target is then blended like the colour).
    void (OKAY_GLAPI *BlendFuncSeparate)(GLenum_, GLenum_, GLenum_, GLenum_) = nullptr;
};

template <class F> bool Load(Backend& b, F& f, const char* name) {
    f = (F)b.GLProcAddress(name);
    return f != nullptr;
}

struct GTex {
    GLuint_ id = 0;
    int w = 0, h = 0;
    BlendMode blend = BlendMode::Blend;
    ScaleMode scale = ScaleMode::Nearest;
    std::uint8_t mr = 255, mg = 255, mb = 255, ma = 255;
};

inline std::uint8_t Mul255(int a, int b) { return (std::uint8_t)((a * b + 127) / 255); }

class GlPainter;
// The painter whose context this thread made current last. Every painter call that touches GL makes its own context
// current first when another painter's is (two windows, two contexts: the launcher and its chat window). The launcher
// draws from one thread only; a program that makes contexts current itself must call Painter calls on that thread too.
GlPainter* g_current = nullptr;

class GlPainter final : public Painter {
public:
    GlPainter(Window& w, const Gl& gl) : m_win(w), m_gl(gl) {
        g_current = this;   // CreateGLContext left our context current
        GLint_ mx = 0;
        m_gl.GetIntegerv(kMaxTextureSize, &mx);
        m_maxTex = mx > 0 ? mx : 2048;
        m_gl.Disable(kDepthTest); m_gl.Disable(kCullFace); m_gl.Disable(kLighting); m_gl.Disable(kAlphaTest);
        m_gl.EnableClientState(kVertexArray); m_gl.EnableClientState(kColorArray); m_gl.EnableClientState(kTexCoordArray);
        m_gl.TexEnvi(kTextureEnv, kTextureEnvMode, kModulate);
        m_gl.PixelStorei(kUnpackAlignment, 1);
        m_gl.PixelStorei(kPackAlignment, 1);
        m_gl.Enable(kBlend);
        if (m_gl.BlendFuncSeparate) m_gl.BlendFuncSeparate(kSrcAlpha, kOneMinusSrcAlpha, kOne, kOneMinusSrcAlpha);
        else m_gl.BlendFunc(kSrcAlpha, kOneMinusSrcAlpha);
        m_blendOn = true;
        Sync(true);
    }
    ~GlPainter() override {
        m_target = &m_win;
        m_win.MakeCurrent();
        g_current = this;
        ReleaseCachedTextures();
        for (GTex* t : m_texs) { m_gl.DeleteTextures(1, &t->id); delete t; }
        m_texs.clear();
        m_win.DestroyGLContext();
        g_current = nullptr;   // no context is current now
    }

    PainterKind Kind() const override { return PainterKind::OpenGL; }
    void* NativeRenderer() override { return nullptr; }

    // ---- target -------------------------------------------------------------------------------
    void OutputSize(int* w, int* h) override { Use(); Sync(false); if (w) *w = m_w; if (h) *h = m_h; }
    void Clear() override {
        Use();
        Sync(false);
        m_gl.Disable(kScissorTest);
        m_gl.ClearColor(m_r / 255.0f, m_g / 255.0f, m_b / 255.0f, m_a / 255.0f);
        m_gl.Clear(kColorBufferBit);
        ApplyClip();
    }
    void SetBlendMode(BlendMode m) override { m_blend = m; }
    BlendMode GetBlendMode() override { return m_blend; }
    void SetDrawColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override { m_r = r; m_g = g; m_b = b; m_a = a; }

    // ---- clip and scale ---------------------------------------------------------------------------
    void SetClipRect(const ui2d::Rect* r) override {
        if (r) { m_clipOn = true; m_clip = *r; } else m_clipOn = false;
        Use();
        ApplyClip();
    }
    void GetClipRect(ui2d::Rect* r) override { if (r) *r = m_clipOn ? m_clip : ui2d::Rect{0, 0, 0, 0}; }
    bool IsClipEnabled() override { return m_clipOn; }
    void SetScale(float sx, float sy) override {
        if (!(sx > 0 && sy > 0 && std::isfinite(sx) && std::isfinite(sy))) return;
        m_sx = sx; m_sy = sy;
        Use();
        Project();
        ApplyClip();
    }

    // ---- primitives (logical units) -----------------------------------------------------------------
    void FillRect(const ui2d::Rect* r) override {
        if (!r) { FillF(0, 0, m_w / m_sx, m_h / m_sy); return; }
        FillF((float)r->x, (float)r->y, (float)r->w, (float)r->h);
    }
    void FillRectF(const ui2d::FRect* r) override {
        if (!r) { FillF(0, 0, m_w / m_sx, m_h / m_sy); return; }
        FillF(r->x, r->y, r->w, r->h);
    }
    void DrawRect(const ui2d::Rect* r) override {
        const float x = r ? (float)r->x : 0.0f, y = r ? (float)r->y : 0.0f;
        const float w = r ? (float)r->w : m_w / m_sx, h = r ? (float)r->h : m_h / m_sy;
        if (w <= 0 || h <= 0) return;
        const float px = 1.0f / m_sx, py = 1.0f / m_sy;   // one device pixel, in logical units
        FillF(x, y, w, py);                                   // top
        if (h > py) FillF(x, y + h - py, w, py);              // bottom
        if (h > 2 * py) { FillF(x, y + py, px, h - 2 * py); if (w > px) FillF(x + w - px, y + py, px, h - 2 * py); }
    }
    void DrawPoint(int x, int y) override { FillF((float)x, (float)y, 1.0f, 1.0f); }
    void DrawLine(int x0, int y0, int x1, int y1) override { DrawLineF((float)x0, (float)y0, (float)x1, (float)y1); }
    void DrawLineF(float x0, float y0, float x1, float y1) override {
        // Through the pixel centres, the last pixel included (a GL line leaves its end point out).
        const float ox = 0.5f / m_sx, oy = 0.5f / m_sy;
        const ui2d::RGBA c{m_r, m_g, m_b, m_a};
        ui2d::Vertex v[3] = {{{x0 + ox, y0 + oy}, c, {0, 0}}, {{x1 + ox, y1 + oy}, c, {0, 0}}, {{x1 + ox, y1 + oy}, c, {0, 0}}};
        Use();
        Bind(nullptr, m_blend);
        Pointers(v);
        m_gl.DrawArrays(kLines, 0, 2);
        m_gl.DrawArrays(kPoints, 2, 1);
    }

    // ---- textures --------------------------------------------------------------------------------
    ui2d::Tex CreateTexture(const TextureDesc& d, const std::uint8_t* rgba) override {
        if (d.width <= 0 || d.height <= 0 || d.width > m_maxTex || d.height > m_maxTex) return nullptr;
        Use();
        GTex* t = new GTex();
        t->w = d.width; t->h = d.height; t->blend = d.blend; t->scale = d.scale;
        m_gl.GenTextures(1, &t->id);
        if (!t->id) { delete t; return nullptr; }
        m_gl.BindTexture(kTexture2D, t->id);
        m_bound = t->id;
        const GLint_ f = d.scale == ScaleMode::Linear ? (GLint_)kLinear : (GLint_)kNearest;
        m_gl.TexParameteri(kTexture2D, kTexMinFilter, f);
        m_gl.TexParameteri(kTexture2D, kTexMagFilter, f);
        m_gl.TexParameteri(kTexture2D, kTexWrapS, kClampToEdge);
        m_gl.TexParameteri(kTexture2D, kTexWrapT, kClampToEdge);
        m_gl.PixelStorei(kUnpackRowLength, 0);
        if (rgba) {
            m_gl.TexImage2D(kTexture2D, 0, kRgba8, d.width, d.height, 0, kRgba, kUnsignedByte, rgba);
        } else {
            std::vector<std::uint8_t> zero((std::size_t)d.width * (std::size_t)d.height * 4, 0);   // GL leaves new storage undefined
            m_gl.TexImage2D(kTexture2D, 0, kRgba8, d.width, d.height, 0, kRgba, kUnsignedByte, zero.data());
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
        Use();
        m_gl.BindTexture(kTexture2D, t->id);
        m_bound = t->id;
        if (pitch % 4 == 0) {
            m_gl.PixelStorei(kUnpackRowLength, pitch / 4);
            m_gl.TexSubImage2D(kTexture2D, 0, rc.x, rc.y, rc.w, rc.h, kRgba, kUnsignedByte, rgba);
            m_gl.PixelStorei(kUnpackRowLength, 0);
        } else {   // a pitch that is not whole pixels: pack the rows first
            std::vector<std::uint8_t> tight((std::size_t)rc.w * (std::size_t)rc.h * 4);
            for (int y = 0; y < rc.h; ++y)
                std::memcpy(&tight[(std::size_t)y * (std::size_t)rc.w * 4], (const std::uint8_t*)rgba + (std::size_t)y * (std::size_t)pitch, (std::size_t)rc.w * 4);
            m_gl.TexSubImage2D(kTexture2D, 0, rc.x, rc.y, rc.w, rc.h, kRgba, kUnsignedByte, tight.data());
        }
        return true;
    }
    void DestroyTexture(ui2d::Tex tex) override {
        GTex* t = T(tex);
        if (!t) return;
        Use();
        if (m_bound == t->id) m_bound = 0;
        m_gl.DeleteTextures(1, &t->id);
        m_texs.erase(t);
        delete t;
    }
    void SetTextureBlendMode(ui2d::Tex t, BlendMode m) override { if (GTex* s = T(t)) s->blend = m; }
    void SetTextureScaleMode(ui2d::Tex tex, ScaleMode m) override {
        GTex* t = T(tex);
        if (!t || t->scale == m) return;
        t->scale = m;
        Use();
        m_gl.BindTexture(kTexture2D, t->id);
        m_bound = t->id;
        const GLint_ f = m == ScaleMode::Linear ? (GLint_)kLinear : (GLint_)kNearest;
        m_gl.TexParameteri(kTexture2D, kTexMinFilter, f);
        m_gl.TexParameteri(kTexture2D, kTexMagFilter, f);
    }
    void QueryTexture(ui2d::Tex tex, int* w, int* h) override {
        GTex* t = T(tex);
        if (w) *w = t ? t->w : 0;
        if (h) *h = t ? t->h : 0;
    }
    void SetTextureColorMod(ui2d::Tex t, std::uint8_t r, std::uint8_t g, std::uint8_t b) override { if (GTex* s = T(t)) { s->mr = r; s->mg = g; s->mb = b; } }
    void SetTextureAlphaMod(ui2d::Tex t, std::uint8_t a) override { if (GTex* s = T(t)) s->ma = a; }

    void Copy(ui2d::Tex t, const ui2d::Rect* s, const ui2d::Rect* d) override {
        ui2d::FRect fd; if (d) fd = ui2d::FRect{(float)d->x, (float)d->y, (float)d->w, (float)d->h};
        Blit(t, s, d ? &fd : nullptr, 0.0, nullptr, 0);
    }
    void CopyF(ui2d::Tex t, const ui2d::Rect* s, const ui2d::FRect* d) override { Blit(t, s, d, 0.0, nullptr, 0); }
    void CopyEx(ui2d::Tex t, const ui2d::Rect* s, const ui2d::Rect* d, double angle, const ui2d::Point* c, int flip) override {
        ui2d::FRect fd; if (d) fd = ui2d::FRect{(float)d->x, (float)d->y, (float)d->w, (float)d->h};
        ui2d::FPoint fc; if (c) fc = ui2d::FPoint{(float)c->x, (float)c->y};
        Blit(t, s, d ? &fd : nullptr, angle, c ? &fc : nullptr, flip);
    }
    void CopyExF(ui2d::Tex t, const ui2d::Rect* s, const ui2d::FRect* d, double angle, const ui2d::FPoint* c, int flip) override {
        Blit(t, s, d, angle, c, flip);
    }

    void Geometry(ui2d::Tex tex, const ui2d::Vertex* v, int nv, const int* idx, int ni) override {
        if (!v || nv <= 0) return;
        GTex* t = tex ? T(tex) : nullptr;
        if (tex && !t) return;
        const int n = idx ? ni : nv;
        if (n < 3) return;
        const ui2d::Vertex* src = v;
        if (t && (t->mr != 255 || t->mg != 255 || t->mb != 255 || t->ma != 255)) {   // the texture's modulation, folded into the colours
            m_vtmp.assign(v, v + nv);
            for (ui2d::Vertex& q : m_vtmp) {
                q.color.r = Mul255(q.color.r, t->mr); q.color.g = Mul255(q.color.g, t->mg);
                q.color.b = Mul255(q.color.b, t->mb); q.color.a = Mul255(q.color.a, t->ma);
            }
            src = m_vtmp.data();
        }
        Use();
        Bind(t, t ? t->blend : m_blend);
        Pointers(src);
        const int tri = n - n % 3;
        if (!idx) { m_gl.DrawArrays(kTriangles, 0, tri); return; }
        // An index past the vertex array would make GL read outside it: drop such triangles, as the software painter does.
        bool ok = true;
        for (int i = 0; i < tri && ok; ++i) ok = idx[i] >= 0 && idx[i] < nv;
        if (ok) { m_gl.DrawElements(kTriangles, tri, kUnsignedInt, idx); return; }
        m_itmp.clear();
        for (int i = 0; i + 3 <= tri; i += 3) {
            if (idx[i] < 0 || idx[i + 1] < 0 || idx[i + 2] < 0 || idx[i] >= nv || idx[i + 1] >= nv || idx[i + 2] >= nv) continue;
            m_itmp.push_back((unsigned)idx[i]); m_itmp.push_back((unsigned)idx[i + 1]); m_itmp.push_back((unsigned)idx[i + 2]);
        }
        if (!m_itmp.empty()) m_gl.DrawElements(kTriangles, (GLsizei_)m_itmp.size(), kUnsignedInt, m_itmp.data());
    }

    // ---- the frame ------------------------------------------------------------------------------
    void Present() override { Use(); m_target->SwapBuffers(); }
    // Another window with this painter's context (Window::MakeCurrentWithContextOf): the textures are the same objects, only the
    // drawable changes, so the viewport and projection follow the new window's size (Sync).
    bool SetTarget(Window* w) override {
        Window* t = w ? w : &m_win;
        bool ok = true;
        if (t == &m_win) ok = m_win.MakeCurrent();
        else if (!t->MakeCurrentWithContextOf(m_win)) { m_win.MakeCurrent(); t = &m_win; ok = false; }
        m_target = t;
        g_current = this;
        Sync(true);
        return w ? ok && t == w : true;
    }
    bool SetVSync(bool on) override { Use(); return m_win.SetSwapInterval(on ? 1 : 0) && on; }
    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override {
        Use();
        Sync(false);
        w = m_w; h = m_h;
        if (w <= 0 || h <= 0) return false;
        std::vector<std::uint8_t> up((std::size_t)w * (std::size_t)h * 4);
        m_gl.ReadPixels(0, 0, w, h, kRgba, kUnsignedByte, up.data());   // what was drawn since the last Present
        rgba.resize(up.size());
        const std::size_t row = (std::size_t)w * 4;
        for (int y = 0; y < h; ++y) std::memcpy(&rgba[(std::size_t)y * row], &up[(std::size_t)(h - 1 - y) * row], row);   // GL rows go bottom up
        return true;
    }

private:
    void Use() {
        if (g_current == this) return;
        if (m_target == &m_win || !m_target->MakeCurrentWithContextOf(m_win)) { m_target = &m_win; m_win.MakeCurrent(); }
        g_current = this;
    }
    GTex* T(ui2d::Tex t) const {
        GTex* s = (GTex*)t;
        return s && m_texs.count(s) ? s : nullptr;
    }
    void Sync(bool force) {
        int w = 0, h = 0;
        m_target->GetDrawableSize(w, h);
        if (!force && w == m_w && h == m_h) return;
        m_w = w; m_h = h;
        m_gl.Viewport(0, 0, w > 0 ? w : 0, h > 0 ? h : 0);
        Project();
        ApplyClip();
    }
    void Project() {
        // Logical units, y down: (0,0) is the top-left corner of the top-left pixel.
        m_gl.MatrixMode(kProjection);
        m_gl.LoadIdentity();
        m_gl.Ortho(0.0, (double)m_w / m_sx, (double)m_h / m_sy, 0.0, -1.0, 1.0);
        m_gl.MatrixMode(kModelView);
        m_gl.LoadIdentity();
    }
    void ApplyClip() {
        if (!m_clipOn) { m_gl.Disable(kScissorTest); return; }
        m_gl.Enable(kScissorTest);
        if (m_clip.w <= 0 || m_clip.h <= 0) { m_gl.Scissor(0, 0, 0, 0); return; }   // an empty clip draws nothing
        const long x0 = std::lround(m_clip.x * (double)m_sx), y0 = std::lround(m_clip.y * (double)m_sy);
        const long x1 = std::lround((m_clip.x + (double)m_clip.w) * m_sx), y1 = std::lround((m_clip.y + (double)m_clip.h) * m_sy);
        const long w = x1 - x0, h = y1 - y0;
        if (w <= 0 || h <= 0) { m_gl.Scissor(0, 0, 0, 0); return; }
        m_gl.Scissor((GLint_)x0, (GLint_)(m_h - y1), (GLsizei_)w, (GLsizei_)h);   // GL counts rows from the bottom
    }
    void Bind(GTex* t, BlendMode blend) {
        if (t) {
            if (!m_texOn) { m_gl.Enable(kTexture2D); m_texOn = true; }
            if (m_bound != t->id) { m_gl.BindTexture(kTexture2D, t->id); m_bound = t->id; }
        } else if (m_texOn) { m_gl.Disable(kTexture2D); m_texOn = false; }
        const bool on = blend == BlendMode::Blend;
        if (on != m_blendOn) { if (on) m_gl.Enable(kBlend); else m_gl.Disable(kBlend); m_blendOn = on; }
    }
    void Pointers(const ui2d::Vertex* v) {
        const GLsizei_ stride = (GLsizei_)sizeof(ui2d::Vertex);
        m_gl.VertexPointer(2, kFloat, stride, &v->position);
        m_gl.ColorPointer(4, kUnsignedByte, stride, &v->color);
        m_gl.TexCoordPointer(2, kFloat, stride, &v->tex_coord);
    }
    void FillF(float x, float y, float w, float h) {
        if (!(w > 0 && h > 0)) return;
        const ui2d::RGBA c{m_r, m_g, m_b, m_a};
        const ui2d::Vertex v[4] = {{{x, y}, c, {0, 0}}, {{x + w, y}, c, {0, 0}}, {{x + w, y + h}, c, {0, 0}}, {{x, y + h}, c, {0, 0}}};
        static const int kQuad[6] = {0, 1, 2, 0, 2, 3};
        Geometry(nullptr, v, 4, kQuad, 6);
    }
    void Blit(ui2d::Tex tex, const ui2d::Rect* s, const ui2d::FRect* d, double angle, const ui2d::FPoint* c, int flip) {
        GTex* t = T(tex);
        if (!t) return;
        ui2d::Rect sr{0, 0, t->w, t->h};
        if (s) sr = *s;
        if (sr.w <= 0 || sr.h <= 0) return;
        const ui2d::FRect dr = d ? *d : ui2d::FRect{0, 0, m_w / m_sx, m_h / m_sy};
        if (!(dr.w > 0 && dr.h > 0)) return;
        float u0 = (float)sr.x / t->w, v0 = (float)sr.y / t->h, u1 = (float)(sr.x + sr.w) / t->w, v1 = (float)(sr.y + sr.h) / t->h;
        if (flip & ui2d::FlipH) std::swap(u0, u1);
        if (flip & ui2d::FlipV) std::swap(v0, v1);
        const float cx = c ? c->x : dr.w * 0.5f, cy = c ? c->y : dr.h * 0.5f;   // centre, relative to the destination's corner
        const double rad = angle * 3.14159265358979323846 / 180.0;               // clockwise on a y-down screen
        const float cs = (float)std::cos(rad), sn = (float)std::sin(rad);
        const float px[4] = {0, dr.w, dr.w, 0}, py[4] = {0, 0, dr.h, dr.h};
        const float tu[4] = {u0, u1, u1, u0}, tv[4] = {v0, v0, v1, v1};
        ui2d::Vertex v[4];
        for (int i = 0; i < 4; ++i) {
            const float rx = px[i] - cx, ry = py[i] - cy;
            v[i].position = {dr.x + cx + rx * cs - ry * sn, dr.y + cy + rx * sn + ry * cs};
            v[i].color = {255, 255, 255, 255};
            v[i].tex_coord = {tu[i], tv[i]};
        }
        static const int kQuad[6] = {0, 1, 2, 0, 2, 3};
        Geometry(t, v, 4, kQuad, 6);
    }

    Window& m_win;
    Window* m_target = &m_win;   // where the painter draws: its own window, or one given to SetTarget
    Gl m_gl;
    int m_w = 0, m_h = 0, m_maxTex = 2048;
    float m_sx = 1.0f, m_sy = 1.0f;
    std::uint8_t m_r = 0, m_g = 0, m_b = 0, m_a = 255;
    BlendMode m_blend = BlendMode::None;   // SDL's default draw blend mode
    bool m_clipOn = false;
    ui2d::Rect m_clip{0, 0, 0, 0};
    bool m_texOn = false, m_blendOn = false;
    GLuint_ m_bound = 0;
    std::unordered_set<GTex*> m_texs;
    std::vector<ui2d::Vertex> m_vtmp;
    std::vector<unsigned> m_itmp;
};

} // namespace

std::unique_ptr<Painter> CreateGLPainter(Window& w, Backend& b, const PainterDesc& d, std::string* err) {
    GLAttributes a;
    a.major = 2; a.minor = 1; a.compatibility = true; a.depthBits = 0;
    if (!w.CreateGLContext(a)) {
        if (err) *err = "the window has no OpenGL context (" + b.LastError() + ")";
        return nullptr;
    }
    Gl gl;
    bool ok = Load(b, gl.Viewport, "glViewport") && Load(b, gl.ClearColor, "glClearColor") && Load(b, gl.Clear, "glClear") &&
              Load(b, gl.Enable, "glEnable") && Load(b, gl.Disable, "glDisable") && Load(b, gl.BlendFunc, "glBlendFunc") &&
              Load(b, gl.Scissor, "glScissor") && Load(b, gl.MatrixMode, "glMatrixMode") && Load(b, gl.LoadIdentity, "glLoadIdentity") &&
              Load(b, gl.Ortho, "glOrtho") && Load(b, gl.GenTextures, "glGenTextures") && Load(b, gl.DeleteTextures, "glDeleteTextures") &&
              Load(b, gl.BindTexture, "glBindTexture") && Load(b, gl.TexImage2D, "glTexImage2D") &&
              Load(b, gl.TexSubImage2D, "glTexSubImage2D") && Load(b, gl.TexParameteri, "glTexParameteri") &&
              Load(b, gl.TexEnvi, "glTexEnvi") && Load(b, gl.PixelStorei, "glPixelStorei") &&
              Load(b, gl.EnableClientState, "glEnableClientState") && Load(b, gl.VertexPointer, "glVertexPointer") &&
              Load(b, gl.ColorPointer, "glColorPointer") && Load(b, gl.TexCoordPointer, "glTexCoordPointer") &&
              Load(b, gl.DrawElements, "glDrawElements") && Load(b, gl.DrawArrays, "glDrawArrays") &&
              Load(b, gl.ReadPixels, "glReadPixels") && Load(b, gl.GetIntegerv, "glGetIntegerv");
    if (!ok) {
        w.DestroyGLContext();
        if (err) *err = "an OpenGL 1.1 entry point is missing";
        return nullptr;
    }
    Load(b, gl.BlendFuncSeparate, "glBlendFuncSeparate");
    w.SetSwapInterval(d.vsync ? 1 : 0);
    return std::unique_ptr<Painter>(new GlPainter(w, gl));
}

} // namespace okay::platform
