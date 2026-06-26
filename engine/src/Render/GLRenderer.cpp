#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Log.hpp"

#include <GL/gl.h>          // GL 1.1 types + enums only
#include <cmath>
#include <cstring>

// ---- Modern GL enums/types not in gl.h (1.1) ------------------------------------
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER   0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER    0x8892
#define GL_STATIC_DRAW     0x88E4
#define GL_DYNAMIC_DRAW    0x88E8
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER         0x8D40
#define GL_RENDERBUFFER        0x8D41
#define GL_COLOR_ATTACHMENT0   0x8CE0
#define GL_DEPTH_ATTACHMENT    0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_READ_FRAMEBUFFER    0x8CA8
#define GL_DRAW_FRAMEBUFFER    0x8CA9
#endif
#ifndef GL_RGBA8
#define GL_RGBA8               0x8058
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24   0x81A6
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE         0x809D
#endif

typedef std::ptrdiff_t GLsizeiptrOK;

namespace okay {
namespace {

// ---- Loaded GL entry points (resolved via the host GetProcAddress) -------------
// Every call goes through these, so the engine library has NO link-time GL
// dependency; headless tests never touch GL.
typedef GLuint (*PFNCreateShader)(GLenum);
typedef void   (*PFNShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void   (*PFNCompileShader)(GLuint);
typedef void   (*PFNGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (*PFNGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, char*);
typedef GLuint (*PFNCreateProgram)(void);
typedef void   (*PFNAttachShader)(GLuint, GLuint);
typedef void   (*PFNBindAttribLocation)(GLuint, GLuint, const char*);
typedef void   (*PFNLinkProgram)(GLuint);
typedef void   (*PFNGetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (*PFNGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, char*);
typedef void   (*PFNDeleteShader)(GLuint);
typedef void   (*PFNDeleteProgram)(GLuint);
typedef void   (*PFNUseProgram)(GLuint);
typedef GLint  (*PFNGetUniformLocation)(GLuint, const char*);
typedef void   (*PFNUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (*PFNUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (*PFNUniform1f)(GLint, GLfloat);
typedef void   (*PFNGenBuffers)(GLsizei, GLuint*);
typedef void   (*PFNBindBuffer)(GLenum, GLuint);
typedef void   (*PFNBufferData)(GLenum, GLsizeiptrOK, const void*, GLenum);
typedef void   (*PFNDeleteBuffers)(GLsizei, const GLuint*);
typedef void   (*PFNVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (*PFNEnableVertexAttribArray)(GLuint);
typedef void   (*PFNGenFramebuffers)(GLsizei, GLuint*);
typedef void   (*PFNBindFramebuffer)(GLenum, GLuint);
typedef void   (*PFNDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void   (*PFNGenRenderbuffers)(GLsizei, GLuint*);
typedef void   (*PFNBindRenderbuffer)(GLenum, GLuint);
typedef void   (*PFNDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void   (*PFNRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void   (*PFNRenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void   (*PFNFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef void   (*PFNFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFNCheckFramebufferStatus)(GLenum);
typedef void   (*PFNBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
// GL 1.1 functions — loaded too (SDL_GL_GetProcAddress returns these), so the engine
// library never has to LINK libGL/opengl32 and headless tests stay GL-free.
typedef void   (*PFNGenTextures)(GLsizei, GLuint*);
typedef void   (*PFNBindTexture)(GLenum, GLuint);
typedef void   (*PFNTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void   (*PFNTexParameteri)(GLenum, GLenum, GLint);
typedef void   (*PFNDeleteTextures)(GLsizei, const GLuint*);
typedef void   (*PFNViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void   (*PFNClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (*PFNClear)(GLbitfield);
typedef void   (*PFNEnable)(GLenum);
typedef void   (*PFNDepthFunc)(GLenum);
typedef void   (*PFNDrawArrays)(GLenum, GLint, GLsizei);
typedef void   (*PFNReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef GLenum (*PFNGetError)(void);
// VAOs: core in GL 3.0+ and REQUIRED for draws in a core profile; on a 2.1 /
// compatibility context the default VAO (0) works, so these are loaded optionally.
typedef void   (*PFNGenVertexArrays)(GLsizei, GLuint*);
typedef void   (*PFNBindVertexArray)(GLuint);
typedef void   (*PFNDeleteVertexArrays)(GLsizei, const GLuint*);

struct GL {
    PFNCreateShader CreateShader; PFNShaderSource ShaderSource; PFNCompileShader CompileShader;
    PFNGetShaderiv GetShaderiv; PFNGetShaderInfoLog GetShaderInfoLog;
    PFNCreateProgram CreateProgram; PFNAttachShader AttachShader; PFNBindAttribLocation BindAttribLocation;
    PFNLinkProgram LinkProgram; PFNGetProgramiv GetProgramiv; PFNGetProgramInfoLog GetProgramInfoLog;
    PFNDeleteShader DeleteShader; PFNDeleteProgram DeleteProgram; PFNUseProgram UseProgram;
    PFNGetUniformLocation GetUniformLocation; PFNUniformMatrix4fv UniformMatrix4fv;
    PFNUniform3f Uniform3f; PFNUniform1f Uniform1f;
    PFNGenBuffers GenBuffers; PFNBindBuffer BindBuffer; PFNBufferData BufferData; PFNDeleteBuffers DeleteBuffers;
    PFNVertexAttribPointer VertexAttribPointer; PFNEnableVertexAttribArray EnableVertexAttribArray;
    PFNGenFramebuffers GenFramebuffers; PFNBindFramebuffer BindFramebuffer; PFNDeleteFramebuffers DeleteFramebuffers;
    PFNGenRenderbuffers GenRenderbuffers; PFNBindRenderbuffer BindRenderbuffer; PFNDeleteRenderbuffers DeleteRenderbuffers;
    PFNRenderbufferStorage RenderbufferStorage; PFNRenderbufferStorageMultisample RenderbufferStorageMultisample;
    PFNFramebufferRenderbuffer FramebufferRenderbuffer; PFNFramebufferTexture2D FramebufferTexture2D;
    PFNCheckFramebufferStatus CheckFramebufferStatus; PFNBlitFramebuffer BlitFramebuffer;
    PFNGenTextures GenTextures; PFNBindTexture BindTexture; PFNTexImage2D TexImage2D;
    PFNTexParameteri TexParameteri; PFNDeleteTextures DeleteTextures; PFNViewport Viewport;
    PFNClearColor ClearColor; PFNClear Clear; PFNEnable Enable; PFNDepthFunc DepthFunc;
    PFNDrawArrays DrawArrays; PFNReadPixels ReadPixels; PFNGetError GetError;
    PFNGenVertexArrays GenVertexArrays; PFNBindVertexArray BindVertexArray;
    PFNDeleteVertexArrays DeleteVertexArrays;
    bool ok = false;
};
GL g;   // resolved entry points

template <class F> bool load(F& fn, GLRenderer::GLGetProc gp, const char* name) {
    fn = reinterpret_cast<F>(gp(name));
    return fn != nullptr;
}

const char* kVert =
    "#version 120\n"
    "uniform mat4 uMVP; uniform mat4 uModel;\n"
    "attribute vec3 aPos; attribute vec3 aNormal;\n"
    "varying vec3 vN; varying vec3 vWorld;\n"
    "void main(){\n"
    "  gl_Position = uMVP * vec4(aPos,1.0);\n"
    "  vN = mat3(uModel) * aNormal;\n"
    "  vWorld = (uModel * vec4(aPos,1.0)).xyz;\n"
    "}\n";

const char* kFrag =
    "#version 120\n"
    "uniform vec3 uColor; uniform vec3 uLightDir; uniform vec3 uLightColor;\n"
    "uniform vec3 uAmbient; uniform vec3 uEmissive; uniform vec3 uEye;\n"
    "uniform float uSpecular; uniform float uShininess; uniform float uUnlit;\n"
    "varying vec3 vN; varying vec3 vWorld;\n"
    "void main(){\n"
    "  if (uUnlit > 0.5) { gl_FragColor = vec4(uColor + uEmissive, 1.0); return; }\n"
    "  vec3 N = normalize(vN);\n"
    "  float ndl = max(dot(N, uLightDir), 0.0);\n"
    "  vec3 diff = uColor * (uAmbient + uLightColor * ndl);\n"
    "  float spec = 0.0;\n"
    "  if (uSpecular > 0.0) {\n"
    "    vec3 V = normalize(uEye - vWorld);\n"
    "    vec3 H = normalize(uLightDir + V);\n"
    "    spec = pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular;\n"
    "  }\n"
    "  gl_FragColor = vec4(diff + vec3(spec) + uEmissive, 1.0);\n"
    "}\n";

GLuint compile(GLenum type, const char* src) {
    GLuint s = g.CreateShader(type);
    g.ShaderSource(s, 1, &src, nullptr);
    g.CompileShader(s);
    GLint ok = 0; g.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; g.GetShaderInfoLog(s, 512, nullptr, log); Log::Error("[gl] shader: ", log); g.DeleteShader(s); return 0; }
    return s;
}

} // namespace

bool GLRenderer::LoadGL(GLGetProc gp) {
    if (g.ok) return true;
    if (!gp) return false;
    bool r = true;
    r &= load(g.CreateShader, gp, "glCreateShader");
    r &= load(g.ShaderSource, gp, "glShaderSource");
    r &= load(g.CompileShader, gp, "glCompileShader");
    r &= load(g.GetShaderiv, gp, "glGetShaderiv");
    r &= load(g.GetShaderInfoLog, gp, "glGetShaderInfoLog");
    r &= load(g.CreateProgram, gp, "glCreateProgram");
    r &= load(g.AttachShader, gp, "glAttachShader");
    r &= load(g.BindAttribLocation, gp, "glBindAttribLocation");
    r &= load(g.LinkProgram, gp, "glLinkProgram");
    r &= load(g.GetProgramiv, gp, "glGetProgramiv");
    r &= load(g.GetProgramInfoLog, gp, "glGetProgramInfoLog");
    r &= load(g.DeleteShader, gp, "glDeleteShader");
    r &= load(g.DeleteProgram, gp, "glDeleteProgram");
    r &= load(g.UseProgram, gp, "glUseProgram");
    r &= load(g.GetUniformLocation, gp, "glGetUniformLocation");
    r &= load(g.UniformMatrix4fv, gp, "glUniformMatrix4fv");
    r &= load(g.Uniform3f, gp, "glUniform3f");
    r &= load(g.Uniform1f, gp, "glUniform1f");
    r &= load(g.GenBuffers, gp, "glGenBuffers");
    r &= load(g.BindBuffer, gp, "glBindBuffer");
    r &= load(g.BufferData, gp, "glBufferData");
    r &= load(g.DeleteBuffers, gp, "glDeleteBuffers");
    r &= load(g.VertexAttribPointer, gp, "glVertexAttribPointer");
    r &= load(g.EnableVertexAttribArray, gp, "glEnableVertexAttribArray");
    r &= load(g.GenFramebuffers, gp, "glGenFramebuffers");
    r &= load(g.BindFramebuffer, gp, "glBindFramebuffer");
    r &= load(g.DeleteFramebuffers, gp, "glDeleteFramebuffers");
    r &= load(g.GenRenderbuffers, gp, "glGenRenderbuffers");
    r &= load(g.BindRenderbuffer, gp, "glBindRenderbuffer");
    r &= load(g.DeleteRenderbuffers, gp, "glDeleteRenderbuffers");
    r &= load(g.RenderbufferStorage, gp, "glRenderbufferStorage");
    r &= load(g.RenderbufferStorageMultisample, gp, "glRenderbufferStorageMultisample");
    r &= load(g.FramebufferRenderbuffer, gp, "glFramebufferRenderbuffer");
    r &= load(g.FramebufferTexture2D, gp, "glFramebufferTexture2D");
    r &= load(g.CheckFramebufferStatus, gp, "glCheckFramebufferStatus");
    r &= load(g.BlitFramebuffer, gp, "glBlitFramebuffer");
    r &= load(g.GenTextures, gp, "glGenTextures");
    r &= load(g.BindTexture, gp, "glBindTexture");
    r &= load(g.TexImage2D, gp, "glTexImage2D");
    r &= load(g.TexParameteri, gp, "glTexParameteri");
    r &= load(g.DeleteTextures, gp, "glDeleteTextures");
    r &= load(g.Viewport, gp, "glViewport");
    r &= load(g.ClearColor, gp, "glClearColor");
    r &= load(g.Clear, gp, "glClear");
    r &= load(g.Enable, gp, "glEnable");
    r &= load(g.DepthFunc, gp, "glDepthFunc");
    r &= load(g.DrawArrays, gp, "glDrawArrays");
    r &= load(g.ReadPixels, gp, "glReadPixels");
    r &= load(g.GetError, gp, "glGetError");
    // Optional (present on core 3.0+; absent on 2.1 compatibility where VAO 0 works):
    load(g.GenVertexArrays, gp, "glGenVertexArrays");
    load(g.BindVertexArray, gp, "glBindVertexArray");
    load(g.DeleteVertexArrays, gp, "glDeleteVertexArrays");
    g.ok = r;
    if (!r) Log::Error("[gl] required OpenGL entry points missing — using software renderer");
    return r;
}

bool GLRenderer::Available() { return g.ok; }

GLRenderer::~GLRenderer() { /* GL objects freed via Destroy() with context current */ }

bool GLRenderer::EnsureProgram() {
    if (m_prog) return true;
    GLuint vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) return false;
    m_prog = g.CreateProgram();
    g.AttachShader(m_prog, vs); g.AttachShader(m_prog, fs);
    g.BindAttribLocation(m_prog, 0, "aPos");
    g.BindAttribLocation(m_prog, 1, "aNormal");
    g.LinkProgram(m_prog);
    GLint ok = 0; g.GetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    g.DeleteShader(vs); g.DeleteShader(fs);
    if (!ok) { char log[512]; g.GetProgramInfoLog(m_prog, 512, nullptr, log); Log::Error("[gl] link: ", log); g.DeleteProgram(m_prog); m_prog = 0; return false; }
    m_uMVP = g.GetUniformLocation(m_prog, "uMVP");
    m_uModel = g.GetUniformLocation(m_prog, "uModel");
    m_uColor = g.GetUniformLocation(m_prog, "uColor");
    m_uLightDir = g.GetUniformLocation(m_prog, "uLightDir");
    m_uLightColor = g.GetUniformLocation(m_prog, "uLightColor");
    m_uAmbient = g.GetUniformLocation(m_prog, "uAmbient");
    m_uEmissive = g.GetUniformLocation(m_prog, "uEmissive");
    m_uEye = g.GetUniformLocation(m_prog, "uEye");
    m_uSpecular = g.GetUniformLocation(m_prog, "uSpecular");
    m_uShininess = g.GetUniformLocation(m_prog, "uShininess");
    m_uUnlit = g.GetUniformLocation(m_prog, "uUnlit");
    g.GenBuffers(1, &m_vbo);
    // A core-profile context refuses to draw without a bound VAO (and some drivers
    // crash on the attempt). Create one when available; on a compatibility context
    // the default VAO is used instead. This persists across viewport resizes.
    if (g.GenVertexArrays && !m_vao) g.GenVertexArrays(1, &m_vao);
    return true;
}

bool GLRenderer::EnsureTargets(int w, int h, int samples) {
    if (m_fbo && m_w == w && m_h == h && m_samples == samples) return true;
    DestroyTargets();   // size/sample change -> rebuild ONLY the render targets
    m_w = w; m_h = h; m_samples = samples < 1 ? 1 : samples;

    g.GenFramebuffers(1, &m_fbo);
    g.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    g.GenRenderbuffers(1, &m_colorRb);
    g.BindRenderbuffer(GL_RENDERBUFFER, m_colorRb);
    g.RenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_RGBA8, w, h);
    g.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_colorRb);
    g.GenRenderbuffers(1, &m_depthRb);
    g.BindRenderbuffer(GL_RENDERBUFFER, m_depthRb);
    g.RenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_DEPTH_COMPONENT24, w, h);
    g.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRb);
    if (g.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { Log::Error("[gl] MSAA FBO incomplete"); return false; }

    g.GenFramebuffers(1, &m_resolveFbo);
    g.BindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
    g.GenTextures(1, &m_resolveTex);
    g.BindTexture(GL_TEXTURE_2D, m_resolveTex);
    g.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_resolveTex, 0);
    if (g.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { Log::Error("[gl] resolve FBO incomplete"); return false; }
    g.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

const std::uint32_t* GLRenderer::RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                                int w, int h, int samples,
                                                float clearR, float clearG, float clearB, float clearA,
                                                const GameObject* ignore) {
    if (!g.ok || w < 1 || h < 1) return nullptr;
    if (!EnsureProgram() || !EnsureTargets(w, h, samples)) return nullptr;
    while (g.GetError() != 0) {}   // drain any stale GL error state before we render

    g.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    g.Viewport(0, 0, w, h);
    g.ClearColor(clearR, clearG, clearB, clearA);
    g.Enable(GL_DEPTH_TEST);
    g.DepthFunc(GL_LEQUAL);
    g.Enable(GL_MULTISAMPLE);
    g.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    g.UseProgram(m_prog);
    if (g.BindVertexArray && m_vao) g.BindVertexArray(m_vao);
    Vec3 toLight = (SceneLight::Direction() * -1.0f).Normalized();
    g.Uniform3f(m_uLightDir, toLight.x, toLight.y, toLight.z);
    g.Uniform3f(m_uLightColor, 0.85f, 0.85f, 0.85f);
    g.Uniform3f(m_uAmbient, 0.35f, 0.36f, 0.40f);
    g.Uniform3f(m_uEye, eye.x, eye.y, eye.z);

    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || go == ignore) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled) continue;
        const Mesh& mesh = mr->mesh;
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        if (V.empty() || T.size() < 3) continue;
        const bool hasN = mesh.HasNormals();

        // Expand triangles (non-indexed): flat shading via per-face normal when the
        // mesh has none, smooth via per-vertex normals when it does.
        m_verts.clear(); m_verts.reserve(T.size() * 6);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], c = T[i + 2];
            Vec3 fn;
            if (!hasN) { fn = Vec3::Cross(V[b] - V[a], V[c] - V[a]); float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
            const int idx[3] = {a, b, c};
            for (int k = 0; k < 3; ++k) {
                const Vec3& p = V[idx[k]];
                Vec3 n = hasN ? mesh.normals[idx[k]] : fn;
                m_verts.push_back(p.x); m_verts.push_back(p.y); m_verts.push_back(p.z);
                m_verts.push_back(n.x); m_verts.push_back(n.y); m_verts.push_back(n.z);
            }
        }
        if (m_verts.empty()) continue;

        Mat4 model = go->transform->LocalToWorldMatrix();
        Mat4 mvp = vp * model;
        g.UniformMatrix4fv(m_uMVP, 1, GL_FALSE, mvp.m);
        g.UniformMatrix4fv(m_uModel, 1, GL_FALSE, model.m);
        bool unlit = mr->unlit || mr->shader == MeshRenderer::Shader::Unlit;
        g.Uniform3f(m_uColor, mr->color.r, mr->color.g, mr->color.b);
        g.Uniform3f(m_uEmissive, mr->emissive.r, mr->emissive.g, mr->emissive.b);
        g.Uniform1f(m_uSpecular, unlit ? 0.0f : mr->specular);
        g.Uniform1f(m_uShininess, mr->shininess);
        g.Uniform1f(m_uUnlit, unlit ? 1.0f : 0.0f);

        g.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)), m_verts.data(), GL_DYNAMIC_DRAW);
        g.EnableVertexAttribArray(0);
        g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void*)0);
        g.EnableVertexAttribArray(1);
        g.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (const void*)(3 * sizeof(float)));
        g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(m_verts.size() / 6));
    }

    // Resolve MSAA into the single-sample texture, then read it back.
    g.BindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    g.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    g.BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    g.BindFramebuffer(GL_READ_FRAMEBUFFER, m_resolveFbo);
    m_pixels.assign((std::size_t)w * h, 0u);
    g.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
    g.BindFramebuffer(GL_FRAMEBUFFER, 0);

    // If anything in the pipeline errored (unsupported feature, bad state), bail to
    // the caller so it can fall back to the software renderer instead of showing junk.
    if (g.GetError() != 0) { Log::Error("[gl] render produced a GL error; falling back to software"); return nullptr; }

    // GL's origin is bottom-left; the editor/SDL texture is top-left — flip rows.
    for (int y = 0; y < h / 2; ++y) {
        std::uint32_t* a = m_pixels.data() + (std::size_t)y * w;
        std::uint32_t* b = m_pixels.data() + (std::size_t)(h - 1 - y) * w;
        for (int x = 0; x < w; ++x) std::swap(a[x], b[x]);
    }
    return m_pixels.data();
}

// Free only the size-dependent render targets (rebuilt on viewport/sample change).
// Crucially this does NOT touch the VBO/VAO/program — those persist for the life of
// the renderer; deleting the VBO here (the old Destroy() did) left the draw calls
// bound to buffer 0, i.e. a client array at address 0, segfaulting on the next draw.
void GLRenderer::DestroyTargets() {
    if (m_colorRb) { g.DeleteRenderbuffers(1, &m_colorRb); m_colorRb = 0; }
    if (m_depthRb) { g.DeleteRenderbuffers(1, &m_depthRb); m_depthRb = 0; }
    if (m_fbo) { g.DeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
    if (m_resolveTex) { g.DeleteTextures(1, &m_resolveTex); m_resolveTex = 0; }
    if (m_resolveFbo) { g.DeleteFramebuffers(1, &m_resolveFbo); m_resolveFbo = 0; }
    m_w = m_h = m_samples = 0;
}

void GLRenderer::Destroy() {
    DestroyTargets();
    if (m_vbo) { g.DeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao && g.DeleteVertexArrays) { g.DeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_prog) { g.DeleteProgram(m_prog); m_prog = 0; }
}

} // namespace okay
