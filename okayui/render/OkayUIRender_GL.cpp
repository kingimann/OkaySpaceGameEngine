// OkayUI render backend: OpenGL 3.3 core / OpenGL ES 3.0. See OkayUIRenderBackends.hpp.
//
// No GL header and no link-time GL dependency: every entry point comes from the host's get-proc function (the way
// okay::GLRenderer::LoadGL does it), so the same object file serves SDL, EGL, GLX and WGL hosts, and a headless build
// that never creates a context never touches GL. The enums are written out below.
#include "okay/UI/OkayUIRenderBackends.hpp"

#include <cstddef>
#include <cstring>
#include <unordered_map>

namespace OkayUI {
namespace render {
namespace {

typedef unsigned int  GLenum_; typedef unsigned int GLuint_; typedef int GLint_; typedef int GLsizei_;
typedef unsigned char GLboolean_; typedef float GLfloat_; typedef char GLchar_; typedef std::ptrdiff_t GLsizeiptr_;
typedef unsigned int  GLbitfield_;

enum : unsigned {
    GL_FALSE_ = 0, GL_TRUE_ = 1,
    GL_TRIANGLES_ = 0x0004, GL_UNSIGNED_BYTE_ = 0x1401, GL_UNSIGNED_INT_ = 0x1405, GL_FLOAT_ = 0x1406,
    GL_CULL_FACE_ = 0x0B44, GL_DEPTH_TEST_ = 0x0B71, GL_STENCIL_TEST_ = 0x0B90, GL_SCISSOR_TEST_ = 0x0C11, GL_BLEND_ = 0x0BE2,
    GL_SCISSOR_BOX_ = 0x0C10, GL_VIEWPORT_ = 0x0BA2, GL_COLOR_WRITEMASK_ = 0x0C23,
    GL_UNPACK_ALIGNMENT_ = 0x0CF5, GL_UNPACK_ROW_LENGTH_ = 0x0CF2, GL_PACK_ALIGNMENT_ = 0x0D05,
    GL_TEXTURE_2D_ = 0x0DE1, GL_TEXTURE_MAG_FILTER_ = 0x2800, GL_TEXTURE_MIN_FILTER_ = 0x2801, GL_TEXTURE_WRAP_S_ = 0x2802, GL_TEXTURE_WRAP_T_ = 0x2803,
    GL_NEAREST_ = 0x2600, GL_LINEAR_ = 0x2601, GL_CLAMP_TO_EDGE_ = 0x812F,
    GL_RGBA_ = 0x1908, GL_RGBA8_ = 0x8058,
    GL_TEXTURE0_ = 0x84C0, GL_ACTIVE_TEXTURE_ = 0x84E0, GL_TEXTURE_BINDING_2D_ = 0x8069,
    GL_SRC_ALPHA_ = 0x0302, GL_ONE_MINUS_SRC_ALPHA_ = 0x0303, GL_ONE_ = 1, GL_FUNC_ADD_ = 0x8006,
    GL_BLEND_SRC_RGB_ = 0x80C9, GL_BLEND_DST_RGB_ = 0x80C8, GL_BLEND_SRC_ALPHA_ = 0x80CB, GL_BLEND_DST_ALPHA_ = 0x80CA,
    GL_BLEND_EQUATION_RGB_ = 0x8009, GL_BLEND_EQUATION_ALPHA_ = 0x883D,
    GL_ARRAY_BUFFER_ = 0x8892, GL_ELEMENT_ARRAY_BUFFER_ = 0x8893, GL_ARRAY_BUFFER_BINDING_ = 0x8894, GL_STREAM_DRAW_ = 0x88E0,
    GL_VERTEX_ARRAY_BINDING_ = 0x85B5, GL_CURRENT_PROGRAM_ = 0x8B8D,
    GL_VERTEX_SHADER_ = 0x8B31, GL_FRAGMENT_SHADER_ = 0x8B30, GL_COMPILE_STATUS_ = 0x8B81, GL_LINK_STATUS_ = 0x8B82,
    GL_FRAMEBUFFER_SRGB_ = 0x8DB9,
};

struct Api {
    GLuint_ (*CreateShader)(GLenum_) = nullptr;
    void (*ShaderSource)(GLuint_, GLsizei_, const GLchar_* const*, const GLint_*) = nullptr;
    void (*CompileShader)(GLuint_) = nullptr;
    void (*GetShaderiv)(GLuint_, GLenum_, GLint_*) = nullptr;
    void (*GetShaderInfoLog)(GLuint_, GLsizei_, GLsizei_*, GLchar_*) = nullptr;
    GLuint_ (*CreateProgram)() = nullptr;
    void (*AttachShader)(GLuint_, GLuint_) = nullptr;
    void (*LinkProgram)(GLuint_) = nullptr;
    void (*GetProgramiv)(GLuint_, GLenum_, GLint_*) = nullptr;
    void (*GetProgramInfoLog)(GLuint_, GLsizei_, GLsizei_*, GLchar_*) = nullptr;
    void (*DeleteShader)(GLuint_) = nullptr;
    void (*DeleteProgram)(GLuint_) = nullptr;
    void (*UseProgram)(GLuint_) = nullptr;
    GLint_ (*GetUniformLocation)(GLuint_, const GLchar_*) = nullptr;
    void (*Uniform2f)(GLint_, GLfloat_, GLfloat_) = nullptr;
    void (*Uniform1i)(GLint_, GLint_) = nullptr;
    void (*GenVertexArrays)(GLsizei_, GLuint_*) = nullptr;
    void (*BindVertexArray)(GLuint_) = nullptr;
    void (*DeleteVertexArrays)(GLsizei_, const GLuint_*) = nullptr;
    void (*GenBuffers)(GLsizei_, GLuint_*) = nullptr;
    void (*BindBuffer)(GLenum_, GLuint_) = nullptr;
    void (*BufferData)(GLenum_, GLsizeiptr_, const void*, GLenum_) = nullptr;
    void (*DeleteBuffers)(GLsizei_, const GLuint_*) = nullptr;
    void (*VertexAttribPointer)(GLuint_, GLint_, GLenum_, GLboolean_, GLsizei_, const void*) = nullptr;
    void (*EnableVertexAttribArray)(GLuint_) = nullptr;
    void (*GenTextures)(GLsizei_, GLuint_*) = nullptr;
    void (*BindTexture)(GLenum_, GLuint_) = nullptr;
    void (*TexImage2D)(GLenum_, GLint_, GLint_, GLsizei_, GLsizei_, GLint_, GLenum_, GLenum_, const void*) = nullptr;
    void (*TexParameteri)(GLenum_, GLenum_, GLint_) = nullptr;
    void (*DeleteTextures)(GLsizei_, const GLuint_*) = nullptr;
    void (*PixelStorei)(GLenum_, GLint_) = nullptr;
    void (*ActiveTexture)(GLenum_) = nullptr;
    void (*Viewport)(GLint_, GLint_, GLsizei_, GLsizei_) = nullptr;
    void (*Scissor)(GLint_, GLint_, GLsizei_, GLsizei_) = nullptr;
    void (*Enable)(GLenum_) = nullptr;
    void (*Disable)(GLenum_) = nullptr;
    GLboolean_ (*IsEnabled)(GLenum_) = nullptr;
    void (*BlendFuncSeparate)(GLenum_, GLenum_, GLenum_, GLenum_) = nullptr;
    void (*BlendEquationSeparate)(GLenum_, GLenum_) = nullptr;
    void (*ColorMask)(GLboolean_, GLboolean_, GLboolean_, GLboolean_) = nullptr;
    void (*DrawElements)(GLenum_, GLsizei_, GLenum_, const void*) = nullptr;
    void (*GetIntegerv)(GLenum_, GLint_*) = nullptr;
    void (*GetBooleanv)(GLenum_, GLboolean_*) = nullptr;
    void (*ReadPixels)(GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, void*) = nullptr;
};

template <class F> bool Load(F& f, GLGetProc gp, const char* name, std::string* missing) {
    f = (F)gp(name);
    if (!f) { if (missing) { *missing += name; *missing += ' '; } return false; }
    return true;
}

bool LoadApi(Api& a, GLGetProc gp, std::string* missing) {
    bool ok = true;
#define L(member, name) ok &= Load(a.member, gp, name, missing)
    L(CreateShader, "glCreateShader"); L(ShaderSource, "glShaderSource"); L(CompileShader, "glCompileShader");
    L(GetShaderiv, "glGetShaderiv"); L(GetShaderInfoLog, "glGetShaderInfoLog"); L(CreateProgram, "glCreateProgram");
    L(AttachShader, "glAttachShader"); L(LinkProgram, "glLinkProgram"); L(GetProgramiv, "glGetProgramiv");
    L(GetProgramInfoLog, "glGetProgramInfoLog"); L(DeleteShader, "glDeleteShader"); L(DeleteProgram, "glDeleteProgram");
    L(UseProgram, "glUseProgram"); L(GetUniformLocation, "glGetUniformLocation"); L(Uniform2f, "glUniform2f"); L(Uniform1i, "glUniform1i");
    L(GenVertexArrays, "glGenVertexArrays"); L(BindVertexArray, "glBindVertexArray"); L(DeleteVertexArrays, "glDeleteVertexArrays");
    L(GenBuffers, "glGenBuffers"); L(BindBuffer, "glBindBuffer"); L(BufferData, "glBufferData"); L(DeleteBuffers, "glDeleteBuffers");
    L(VertexAttribPointer, "glVertexAttribPointer"); L(EnableVertexAttribArray, "glEnableVertexAttribArray");
    L(GenTextures, "glGenTextures"); L(BindTexture, "glBindTexture"); L(TexImage2D, "glTexImage2D"); L(TexParameteri, "glTexParameteri");
    L(DeleteTextures, "glDeleteTextures"); L(PixelStorei, "glPixelStorei"); L(ActiveTexture, "glActiveTexture");
    L(Viewport, "glViewport"); L(Scissor, "glScissor"); L(Enable, "glEnable"); L(Disable, "glDisable"); L(IsEnabled, "glIsEnabled");
    L(BlendFuncSeparate, "glBlendFuncSeparate"); L(BlendEquationSeparate, "glBlendEquationSeparate"); L(ColorMask, "glColorMask");
    L(DrawElements, "glDrawElements"); L(GetIntegerv, "glGetIntegerv"); L(GetBooleanv, "glGetBooleanv"); L(ReadPixels, "glReadPixels");
#undef L
    return ok;
}

const char* const kVertCore =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec4 aCol;\n"
    "layout(location = 2) in vec2 aUv;\n"
    "uniform vec2 uScale;\n"            // 2 * dpi / framebuffer size: logical pixels -> clip space, top-left origin
    "out vec4 vCol;\n"
    "out vec2 vUv;\n"
    "void main() {\n"
    "  vCol = aCol; vUv = aUv;\n"
    "  gl_Position = vec4(aPos.x * uScale.x - 1.0, 1.0 - aPos.y * uScale.y, 0.0, 1.0);\n"
    "}\n";
const char* const kFragCore =
    "#version 330 core\n"
    "uniform sampler2D uTex;\n"
    "in vec4 vCol;\n"
    "in vec2 vUv;\n"
    "out vec4 oCol;\n"
    "void main() { oCol = vCol * texture(uTex, vUv); }\n";
const char* const kVertEs =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec4 aCol;\n"
    "layout(location = 2) in vec2 aUv;\n"
    "uniform vec2 uScale;\n"
    "out vec4 vCol;\n"
    "out vec2 vUv;\n"
    "void main() {\n"
    "  vCol = aCol; vUv = aUv;\n"
    "  gl_Position = vec4(aPos.x * uScale.x - 1.0, 1.0 - aPos.y * uScale.y, 0.0, 1.0);\n"
    "}\n";
const char* const kFragEs =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D uTex;\n"
    "in vec4 vCol;\n"
    "in vec2 vUv;\n"
    "out vec4 oCol;\n"
    "void main() { oCol = vCol * texture(uTex, vUv); }\n";

class GLBackend final : public Backend {
public:
    GLBackend(GLFlavor f) : m_flavor(f) {}
    ~GLBackend() override { Destroy(); }

    bool Init(GLGetProc gp, std::string* err) {
        std::string missing;
        if (!gp || !LoadApi(m_g, gp, &missing)) { if (err) *err = "missing GL entry points: " + missing; return false; }
        const char* vs = GLShaderSource(m_flavor, 0); const char* fs = GLShaderSource(m_flavor, 1);
        const GLuint_ v = Compile(GL_VERTEX_SHADER_, vs, err);
        const GLuint_ fr = v ? Compile(GL_FRAGMENT_SHADER_, fs, err) : 0;
        if (!v || !fr) { if (v) m_g.DeleteShader(v); return false; }
        m_prog = m_g.CreateProgram();
        m_g.AttachShader(m_prog, v); m_g.AttachShader(m_prog, fr);
        m_g.LinkProgram(m_prog);
        m_g.DeleteShader(v); m_g.DeleteShader(fr);
        GLint_ ok = 0; m_g.GetProgramiv(m_prog, GL_LINK_STATUS_, &ok);
        if (!ok) {
            char log[512] = {0}; m_g.GetProgramInfoLog(m_prog, 512, nullptr, log);
            if (err) *err = std::string("program link: ") + log;
            m_g.DeleteProgram(m_prog); m_prog = 0; return false;
        }
        m_uScale = m_g.GetUniformLocation(m_prog, "uScale");
        m_uTex = m_g.GetUniformLocation(m_prog, "uTex");
        // state that survives while the app does something else: a VAO with the vertex layout, two buffers, a white texel
        GLint_ oldVao = 0, oldAb = 0, oldTex = 0;
        m_g.GetIntegerv(GL_VERTEX_ARRAY_BINDING_, &oldVao); m_g.GetIntegerv(GL_ARRAY_BUFFER_BINDING_, &oldAb); m_g.GetIntegerv(GL_TEXTURE_BINDING_2D_, &oldTex);
        m_g.GenVertexArrays(1, &m_vao); m_g.GenBuffers(1, &m_vbo); m_g.GenBuffers(1, &m_ibo);
        m_g.BindVertexArray(m_vao);
        m_g.BindBuffer(GL_ARRAY_BUFFER_, m_vbo);
        m_g.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_, m_ibo);
        m_g.EnableVertexAttribArray(0); m_g.EnableVertexAttribArray(1); m_g.EnableVertexAttribArray(2);
        m_g.VertexAttribPointer(0, 2, GL_FLOAT_, GL_FALSE_, (GLsizei_)sizeof(Vertex), (const void*)offsetof(Vertex, x));
        m_g.VertexAttribPointer(1, 4, GL_UNSIGNED_BYTE_, GL_TRUE_, (GLsizei_)sizeof(Vertex), (const void*)offsetof(Vertex, r));
        m_g.VertexAttribPointer(2, 2, GL_FLOAT_, GL_FALSE_, (GLsizei_)sizeof(Vertex), (const void*)offsetof(Vertex, u));
        m_g.BindVertexArray((GLuint_)oldVao);
        m_g.BindBuffer(GL_ARRAY_BUFFER_, (GLuint_)oldAb);
        const std::uint8_t white[4] = { 255, 255, 255, 255 };
        m_white = MakeTexture(1, 1, false, white);
        m_g.BindTexture(GL_TEXTURE_2D_, (GLuint_)oldTex);
        return m_white != 0;
    }

    const char* Name() const override { return m_flavor == GLFlavor::Core33 ? "opengl-3.3-core" : "opengl-es-3.0"; }
    Caps Capabilities() const override { Caps c; c.maxTexture = kMaxTexture; return c; }   // GL_MAX_TEXTURE_SIZE is not queried

    TexId CreateTexture(int w, int h, bool linear, const std::uint8_t* rgba) override {
        GLint_ oldTex = 0; m_g.GetIntegerv(GL_TEXTURE_BINDING_2D_, &oldTex);
        const GLuint_ t = MakeTexture(w, h, linear, rgba);
        m_g.BindTexture(GL_TEXTURE_2D_, (GLuint_)oldTex);
        if (!t) return 0;
        const TexId id = ++m_next;
        m_tex[id] = t;
        return id;
    }
    bool UpdateTexture(TexId id, int w, int h, const std::uint8_t* rgba) override {
        auto it = m_tex.find(id);
        if (it == m_tex.end() || !rgba) return false;
        GLint_ oldTex = 0; m_g.GetIntegerv(GL_TEXTURE_BINDING_2D_, &oldTex);
        m_g.BindTexture(GL_TEXTURE_2D_, it->second);
        Upload(w, h, rgba);
        m_g.BindTexture(GL_TEXTURE_2D_, (GLuint_)oldTex);
        return true;
    }
    void DestroyTexture(TexId id) override {
        auto it = m_tex.find(id);
        if (it == m_tex.end()) return;
        m_g.DeleteTextures(1, &it->second);
        m_tex.erase(it);
    }

    bool BeginFrame(int fbW, int fbH, float dpi) override {
        if (!m_prog) return false;
        m_w = fbW; m_h = fbH; m_dpi = dpi;
        // ---- save the state we change ----
        GLint_ vp[4], sc[4];
        m_g.GetIntegerv(GL_VIEWPORT_, vp); m_g.GetIntegerv(GL_SCISSOR_BOX_, sc);
        for (int i = 0; i < 4; ++i) { m_sVp[i] = vp[i]; m_sSc[i] = sc[i]; }
        m_g.GetIntegerv(GL_CURRENT_PROGRAM_, &m_sProg); m_g.GetIntegerv(GL_VERTEX_ARRAY_BINDING_, &m_sVao);
        m_g.GetIntegerv(GL_ARRAY_BUFFER_BINDING_, &m_sAb); m_g.GetIntegerv(GL_ACTIVE_TEXTURE_, &m_sActive);
        m_g.ActiveTexture(GL_TEXTURE0_);
        m_g.GetIntegerv(GL_TEXTURE_BINDING_2D_, &m_sTex);
        m_g.GetIntegerv(GL_BLEND_SRC_RGB_, &m_sBf[0]); m_g.GetIntegerv(GL_BLEND_DST_RGB_, &m_sBf[1]);
        m_g.GetIntegerv(GL_BLEND_SRC_ALPHA_, &m_sBf[2]); m_g.GetIntegerv(GL_BLEND_DST_ALPHA_, &m_sBf[3]);
        m_g.GetIntegerv(GL_BLEND_EQUATION_RGB_, &m_sBe[0]); m_g.GetIntegerv(GL_BLEND_EQUATION_ALPHA_, &m_sBe[1]);
        m_g.GetBooleanv(GL_COLOR_WRITEMASK_, m_sMask);
        m_g.GetIntegerv(GL_UNPACK_ALIGNMENT_, &m_sUnpack);
        m_sBlend = m_g.IsEnabled(GL_BLEND_); m_sCull = m_g.IsEnabled(GL_CULL_FACE_); m_sDepth = m_g.IsEnabled(GL_DEPTH_TEST_);
        m_sStencil = m_g.IsEnabled(GL_STENCIL_TEST_); m_sScissor = m_g.IsEnabled(GL_SCISSOR_TEST_);
        m_sSrgb = m_flavor == GLFlavor::Core33 ? m_g.IsEnabled(GL_FRAMEBUFFER_SRGB_) : 0;
        // ---- ours ----
        m_g.Enable(GL_BLEND_);
        m_g.BlendEquationSeparate(GL_FUNC_ADD_, GL_FUNC_ADD_);
        m_g.BlendFuncSeparate(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_, GL_ONE_, GL_ONE_MINUS_SRC_ALPHA_);   // straight alpha, source-over
        m_g.ColorMask(GL_TRUE_, GL_TRUE_, GL_TRUE_, GL_TRUE_);
        m_g.Disable(GL_CULL_FACE_); m_g.Disable(GL_DEPTH_TEST_); m_g.Disable(GL_STENCIL_TEST_);
        if (m_flavor == GLFlavor::Core33) m_g.Disable(GL_FRAMEBUFFER_SRGB_);
        m_g.Enable(GL_SCISSOR_TEST_);
        m_g.PixelStorei(GL_UNPACK_ALIGNMENT_, 4);
        m_g.Viewport(0, 0, fbW, fbH);
        m_g.UseProgram(m_prog);
        m_g.Uniform2f(m_uScale, 2.0f * dpi / (float)fbW, 2.0f * dpi / (float)fbH);
        m_g.Uniform1i(m_uTex, 0);
        m_g.BindVertexArray(m_vao);
        return true;
    }

    void Draw(const Vertex* vtx, int nv, const std::int32_t* idx, int ni, const Span* spans, int ns) override {
        m_g.BindBuffer(GL_ARRAY_BUFFER_, m_vbo);
        m_g.BufferData(GL_ARRAY_BUFFER_, (GLsizeiptr_)nv * (GLsizeiptr_)sizeof(Vertex), vtx, GL_STREAM_DRAW_);
        m_g.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_, m_ibo);
        m_g.BufferData(GL_ELEMENT_ARRAY_BUFFER_, (GLsizeiptr_)ni * (GLsizeiptr_)sizeof(std::int32_t), idx, GL_STREAM_DRAW_);
        GLuint_ bound = ~0u;
        int cx0 = -1, cy0 = -1, cx1 = -1, cy1 = -1;
        for (int s = 0; s < ns; ++s) {
            const Span& sp = spans[s];
            GLuint_ t = m_white;
            if (sp.tex) { auto it = m_tex.find(sp.tex); if (it == m_tex.end()) continue; t = it->second; }
            if (t != bound) { m_g.BindTexture(GL_TEXTURE_2D_, t); bound = t; }
            int x0 = 0, y0 = 0, x1 = m_w, y1 = m_h;
            if (sp.clipOn) { x0 = sp.x0; y0 = sp.y0; x1 = sp.x1; y1 = sp.y1; }
            if (x0 != cx0 || y0 != cy0 || x1 != cx1 || y1 != cy1) {
                m_g.Scissor(x0, m_h - y1, x1 - x0, y1 - y0);          // GL's origin is the bottom left
                cx0 = x0; cy0 = y0; cx1 = x1; cy1 = y1;
            }
            m_g.DrawElements(GL_TRIANGLES_, sp.idxCount, GL_UNSIGNED_INT_, (const void*)((std::uintptr_t)sp.idxOffset * sizeof(std::int32_t)));
        }
    }

    void EndFrame() override {
        m_g.BindVertexArray((GLuint_)m_sVao);
        m_g.BindBuffer(GL_ARRAY_BUFFER_, (GLuint_)m_sAb);
        m_g.UseProgram((GLuint_)m_sProg);
        m_g.BindTexture(GL_TEXTURE_2D_, (GLuint_)m_sTex);
        m_g.ActiveTexture((GLenum_)m_sActive);
        m_g.BlendEquationSeparate((GLenum_)m_sBe[0], (GLenum_)m_sBe[1]);
        m_g.BlendFuncSeparate((GLenum_)m_sBf[0], (GLenum_)m_sBf[1], (GLenum_)m_sBf[2], (GLenum_)m_sBf[3]);
        m_g.ColorMask(m_sMask[0], m_sMask[1], m_sMask[2], m_sMask[3]);
        m_g.PixelStorei(GL_UNPACK_ALIGNMENT_, m_sUnpack);
        Set(GL_BLEND_, m_sBlend); Set(GL_CULL_FACE_, m_sCull); Set(GL_DEPTH_TEST_, m_sDepth); Set(GL_STENCIL_TEST_, m_sStencil); Set(GL_SCISSOR_TEST_, m_sScissor);
        if (m_flavor == GLFlavor::Core33) Set(GL_FRAMEBUFFER_SRGB_, m_sSrgb);
        m_g.Viewport(m_sVp[0], m_sVp[1], m_sVp[2], m_sVp[3]);
        m_g.Scissor(m_sSc[0], m_sSc[1], m_sSc[2], m_sSc[3]);
    }

    bool ReadPixels(std::vector<std::uint8_t>& rgba, int& w, int& h) override {
        if (m_w <= 0 || m_h <= 0) return false;
        w = m_w; h = m_h;
        std::vector<std::uint8_t> raw((size_t)w * (size_t)h * 4);
        GLint_ oldPack = 0; m_g.GetIntegerv(GL_PACK_ALIGNMENT_, &oldPack);
        m_g.PixelStorei(GL_PACK_ALIGNMENT_, 4);
        m_g.ReadPixels(0, 0, w, h, GL_RGBA_, GL_UNSIGNED_BYTE_, raw.data());
        m_g.PixelStorei(GL_PACK_ALIGNMENT_, oldPack);
        rgba.resize(raw.size());
        for (int y = 0; y < h; ++y) std::memcpy(&rgba[(size_t)y * w * 4], &raw[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);   // bottom-up to top-down
        return true;
    }

private:
    void Set(unsigned cap, unsigned char on) { if (on) m_g.Enable(cap); else m_g.Disable(cap); }
    GLuint_ Compile(unsigned type, const char* src, std::string* err) {
        const GLuint_ s = m_g.CreateShader(type);
        m_g.ShaderSource(s, 1, &src, nullptr);
        m_g.CompileShader(s);
        GLint_ ok = 0; m_g.GetShaderiv(s, GL_COMPILE_STATUS_, &ok);
        if (!ok) {
            char log[512] = {0}; m_g.GetShaderInfoLog(s, 512, nullptr, log);
            if (err) *err = std::string(type == GL_VERTEX_SHADER_ ? "vertex" : "fragment") + " shader: " + log;
            m_g.DeleteShader(s); return 0;
        }
        return s;
    }
    void Upload(int w, int h, const std::uint8_t* rgba) {
        GLint_ oldRow = 0; m_g.GetIntegerv(GL_UNPACK_ROW_LENGTH_, &oldRow);
        GLint_ oldAl = 0; m_g.GetIntegerv(GL_UNPACK_ALIGNMENT_, &oldAl);
        m_g.PixelStorei(GL_UNPACK_ROW_LENGTH_, 0); m_g.PixelStorei(GL_UNPACK_ALIGNMENT_, 4);
        m_g.TexImage2D(GL_TEXTURE_2D_, 0, (GLint_)GL_RGBA8_, w, h, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, rgba);
        m_g.PixelStorei(GL_UNPACK_ROW_LENGTH_, oldRow); m_g.PixelStorei(GL_UNPACK_ALIGNMENT_, oldAl);
    }
    GLuint_ MakeTexture(int w, int h, bool linear, const std::uint8_t* rgba) {
        if (w <= 0 || h <= 0 || w > kMaxTexture || h > kMaxTexture) return 0;
        GLuint_ t = 0; m_g.GenTextures(1, &t);
        if (!t) return 0;
        m_g.ActiveTexture(GL_TEXTURE0_);
        m_g.BindTexture(GL_TEXTURE_2D_, t);
        m_g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, linear ? (GLint_)GL_LINEAR_ : (GLint_)GL_NEAREST_);
        m_g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, linear ? (GLint_)GL_LINEAR_ : (GLint_)GL_NEAREST_);
        m_g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, (GLint_)GL_CLAMP_TO_EDGE_);
        m_g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_T_, (GLint_)GL_CLAMP_TO_EDGE_);
        std::vector<std::uint8_t> zero;
        if (!rgba) { zero.assign((size_t)w * (size_t)h * 4, 0); rgba = zero.data(); }
        Upload(w, h, rgba);
        return t;
    }
    void Destroy() {
        if (!m_g.DeleteTextures) return;
        for (auto& kv : m_tex) m_g.DeleteTextures(1, &kv.second);
        m_tex.clear();
        if (m_white) m_g.DeleteTextures(1, &m_white);
        if (m_vbo) m_g.DeleteBuffers(1, &m_vbo);
        if (m_ibo) m_g.DeleteBuffers(1, &m_ibo);
        if (m_vao) m_g.DeleteVertexArrays(1, &m_vao);
        if (m_prog) m_g.DeleteProgram(m_prog);
        m_white = m_vbo = m_ibo = m_vao = m_prog = 0;
    }

    GLFlavor m_flavor;
    Api m_g;
    GLuint_ m_prog = 0, m_vao = 0, m_vbo = 0, m_ibo = 0, m_white = 0;
    GLint_ m_uScale = -1, m_uTex = -1;
    std::unordered_map<TexId, GLuint_> m_tex;
    TexId m_next = 0;
    int m_w = 0, m_h = 0; float m_dpi = 1.0f;
    // saved state
    GLint_ m_sVp[4] = {0}, m_sSc[4] = {0}, m_sProg = 0, m_sVao = 0, m_sAb = 0, m_sActive = 0, m_sTex = 0, m_sBf[4] = {0}, m_sBe[2] = {0}, m_sUnpack = 4;
    GLboolean_ m_sMask[4] = {1, 1, 1, 1};
    GLboolean_ m_sBlend = 0, m_sCull = 0, m_sDepth = 0, m_sStencil = 0, m_sScissor = 0, m_sSrgb = 0;
};

} // namespace

const char* GLShaderSource(GLFlavor flavor, int stage) {
    if (flavor == GLFlavor::Core33) return stage == 0 ? kVertCore : kFragCore;
    return stage == 0 ? kVertEs : kFragEs;
}

std::unique_ptr<Backend> CreateGLBackend(GLGetProc getProc, GLFlavor flavor, std::string* err) {
    std::unique_ptr<GLBackend> b(new GLBackend(flavor));
    if (!b->Init(getProc, err)) return nullptr;
    return std::unique_ptr<Backend>(b.release());
}

} // namespace render
} // namespace OkayUI
