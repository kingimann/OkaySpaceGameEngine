#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture: shares the texture cache
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
#ifndef GL_TEXTURE0
#define GL_TEXTURE0            0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1            0x84C1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE       0x812F
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
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
typedef void   (*PFNUniform3fv)(GLint, GLsizei, const GLfloat*);
typedef void   (*PFNUniform1fv)(GLint, GLsizei, const GLfloat*);
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
typedef void   (*PFNDisable)(GLenum);
typedef void   (*PFNBlendFunc)(GLenum, GLenum);
typedef void   (*PFNDepthMask)(GLboolean);
typedef void   (*PFNDepthFunc)(GLenum);
typedef void   (*PFNDrawArrays)(GLenum, GLint, GLsizei);
typedef void   (*PFNReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef GLenum (*PFNGetError)(void);
typedef void   (*PFNUniform1i)(GLint, GLint);
typedef void   (*PFNUniform2f)(GLint, GLfloat, GLfloat);
typedef void   (*PFNActiveTexture)(GLenum);
typedef void   (*PFNGenerateMipmap)(GLenum);
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
    PFNUniform3fv Uniform3fv; PFNUniform1fv Uniform1fv;
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
    PFNDisable Disable; PFNBlendFunc BlendFunc; PFNDepthMask DepthMask;
    PFNDrawArrays DrawArrays; PFNReadPixels ReadPixels; PFNGetError GetError;
    PFNUniform1i Uniform1i; PFNUniform2f Uniform2f;
    PFNActiveTexture ActiveTexture; PFNGenerateMipmap GenerateMipmap;
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
    "uniform mat4 uMVP; uniform mat4 uModel; uniform vec2 uTiling;\n"
    "attribute vec3 aPos; attribute vec3 aNormal; attribute vec2 aUV; attribute vec3 aColor;\n"
    "attribute vec3 aTan;\n"
    "varying vec3 vN; varying vec3 vWorld; varying vec2 vUV; varying vec3 vCol; varying vec3 vTan;\n"
    "void main(){\n"
    "  gl_Position = uMVP * vec4(aPos,1.0);\n"
    "  vN = mat3(uModel) * aNormal;\n"
    "  vTan = mat3(uModel) * aTan;\n"
    "  vWorld = (uModel * vec4(aPos,1.0)).xyz;\n"
    "  vUV = aUV * uTiling;\n"
    "  vCol = aColor;\n"
    "}\n";

const char* kFrag =
    "#version 120\n"
    "uniform vec3 uColor; uniform vec3 uLightDir; uniform vec3 uLightColor;\n"
    "uniform vec3 uAmbient; uniform vec3 uEmissive; uniform vec3 uEye;\n"
    "uniform float uSpecular; uniform float uShininess; uniform float uUnlit;\n"
    "uniform sampler2D uTex; uniform float uUseTex;\n"
    "uniform float uShaderMode; uniform float uToonBands; uniform float uRimStr;\n"
    "uniform vec3 uRimColor; uniform vec3 uGradTop; uniform vec3 uGradBot;\n"
    "uniform float uShadowAlpha;\n"   // >=0: draw a flat translucent black (ground contact shadow)
    "uniform int uLightCount;\n"      // multi-light: directional + point + spot from the scene
    "uniform float uLType[8]; uniform vec3 uLPos[8]; uniform vec3 uLDir[8];\n"
    "uniform vec3 uLCol[8]; uniform float uLRange[8]; uniform float uLCosOut[8]; uniform float uLCosIn[8];\n"
    "uniform sampler2D uNormalTex; uniform float uHasNormal; uniform float uNormalStrength;\n"  // bump/normal map
    "varying vec3 vN; varying vec3 vWorld; varying vec2 vUV; varying vec3 vCol; varying vec3 vTan;\n"
    "void main(){\n"
    "  if (uShadowAlpha >= 0.0) { gl_FragColor = vec4(0.0, 0.0, 0.0, uShadowAlpha); return; }\n"
    "  vec3 N = normalize(vN);\n"
    "  if (uHasNormal > 0.5) {\n"                                  // perturb N by the tangent-space normal map
    "    vec3 tn = texture2D(uNormalTex, vUV).rgb * 2.0 - 1.0;\n"
    "    tn.xy *= uNormalStrength;\n"
    "    vec3 Tn = vTan - N * dot(N, vTan);\n"                     // Gram-Schmidt orthonormalize the tangent
    "    if (length(Tn) > 1e-5) {\n"
    "      Tn = normalize(Tn); vec3 Bn = cross(N, Tn);\n"
    "      vec3 pn = Tn * tn.x + Bn * tn.y + N * tn.z;\n"
    "      if (length(pn) > 1e-5) N = normalize(pn);\n"
    "    }\n"
    "  }\n"
    "  vec3 base = uColor * vCol;\n"
    "  if (uUseTex > 0.5) base *= texture2D(uTex, vUV).rgb;\n"
    "  vec3 Vv = normalize(uEye - vWorld);\n"
    "  float fz = 1.0 - max(dot(N, Vv), 0.0);\n"                  // Fresnel term (grazing)
    "  if (uShaderMode > 2.5 && uShaderMode < 3.5)\n"             // Gradient
    "    base = mix(uGradBot, uGradTop, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));\n"
    "  bool fres = uShaderMode > 3.5 && uShaderMode < 4.5;\n"     // Fresnel
    "  if (fres) base *= 0.10;\n"
    "  if (uShaderMode > 4.5 && uShaderMode < 5.5) {\n"           // Iridescent (oil-slick)
    "    float ph = fz * 6.2831853;\n"
    "    base *= vec3(0.5+0.5*cos(ph), 0.5+0.5*cos(ph+2.0944), 0.5+0.5*cos(ph+4.1888));\n"
    "  }\n"
    "  bool holo = uShaderMode > 5.5 && uShaderMode < 6.5;\n"     // Hologram
    "  if (holo) base *= 0.18 * (0.55 + 0.45 * sin(vWorld.y * 40.0));\n"
    "  if (uShaderMode > 6.5) base = floor(base * 5.0) / 5.0;\n"  // Posterize (retro banding)
    "  if (uUnlit > 0.5) { gl_FragColor = vec4(base + uEmissive, 1.0); return; }\n"
    "  bool toon = uShaderMode > 1.5 && uShaderMode < 2.5 && uToonBands > 0.5;\n"
    "  vec3 amb = uAmbient * mix(0.55, 1.15, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));\n"  // hemisphere ambient
    "  vec3 lit = amb; float spec = 0.0;\n"
    "  if (uLightCount < 1) {\n"                                  // fallback: the global directional
    "    float ndl = max(dot(N, uLightDir), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "    lit += uLightColor * ndl;\n"
    "    if (uSpecular > 0.0) { vec3 H = normalize(uLightDir + Vv);\n"
    "      spec = pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular; }\n"
    "  } else {\n"
    "    for (int i = 0; i < 8; i++) { if (i >= uLightCount) break;\n"   // directional + point + spot
    "      vec3 ld; float at = 1.0;\n"
    "      if (uLType[i] < 0.5) { ld = normalize(-uLDir[i]); }\n"        // directional
    "      else { vec3 toL = uLPos[i] - vWorld; float d = length(toL); ld = toL / max(d, 1e-4);\n"
    "        at = max(1.0 - d / max(uLRange[i], 1e-4), 0.0); at *= at;\n"
    "        if (uLType[i] > 1.5) { float cs = dot(normalize(uLDir[i]), -ld);\n"   // spot cone
    "          float dn = uLCosIn[i] - uLCosOut[i];\n"
    "          float sp = dn > 1e-4 ? clamp((cs - uLCosOut[i]) / dn, 0.0, 1.0) : (cs >= uLCosOut[i] ? 1.0 : 0.0);\n"
    "          at *= sp * sp; } }\n"
    "      float ndl = max(dot(N, ld), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "      lit += uLCol[i] * ndl * at;\n"
    "      if (uSpecular > 0.0) { vec3 H = normalize(ld + Vv);\n"
    "        spec += pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular * at; }\n"
    "    }\n"
    "  }\n"
    "  if (uShaderMode > 7.5) lit += fz * fz * (lit) * 0.6;\n"     // Velvet: fuzzy grazing sheen
    "  vec3 diff = base * lit;\n"
    "  vec3 rim = vec3(0.0);\n"                                   // rim glow (Fresnel/Hologram or rimStr>0)
    "  float rs = uRimStr; if ((fres || holo) && rs < 0.8) rs = 1.6;\n"
    "  if (rs > 0.0) rim = uRimColor * (fz*fz*fz * rs);\n"
    "  gl_FragColor = vec4(diff + vec3(spec) + rim + uEmissive, 1.0);\n"
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
    r &= load(g.Uniform3fv, gp, "glUniform3fv");
    r &= load(g.Uniform1fv, gp, "glUniform1fv");
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
    r &= load(g.Disable, gp, "glDisable");
    r &= load(g.BlendFunc, gp, "glBlendFunc");
    r &= load(g.DepthMask, gp, "glDepthMask");
    r &= load(g.DepthFunc, gp, "glDepthFunc");
    r &= load(g.DrawArrays, gp, "glDrawArrays");
    r &= load(g.ReadPixels, gp, "glReadPixels");
    r &= load(g.GetError, gp, "glGetError");
    r &= load(g.Uniform1i, gp, "glUniform1i");
    r &= load(g.Uniform2f, gp, "glUniform2f");
    r &= load(g.ActiveTexture, gp, "glActiveTexture");
    load(g.GenerateMipmap, gp, "glGenerateMipmap");   // optional (GL 3.0+); mips skipped if absent
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
    g.BindAttribLocation(m_prog, 2, "aUV");
    g.BindAttribLocation(m_prog, 3, "aColor");
    g.BindAttribLocation(m_prog, 4, "aTan");
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
    m_uTex = g.GetUniformLocation(m_prog, "uTex");
    m_uUseTex = g.GetUniformLocation(m_prog, "uUseTex");
    m_uTiling = g.GetUniformLocation(m_prog, "uTiling");
    m_uShaderMode = g.GetUniformLocation(m_prog, "uShaderMode");
    m_uToonBands = g.GetUniformLocation(m_prog, "uToonBands");
    m_uRimStr = g.GetUniformLocation(m_prog, "uRimStr");
    m_uRimColor = g.GetUniformLocation(m_prog, "uRimColor");
    m_uGradTop = g.GetUniformLocation(m_prog, "uGradTop");
    m_uGradBot = g.GetUniformLocation(m_prog, "uGradBot");
    m_uShadowAlpha = g.GetUniformLocation(m_prog, "uShadowAlpha");
    m_uLightCount = g.GetUniformLocation(m_prog, "uLightCount");
    m_uLType   = g.GetUniformLocation(m_prog, "uLType");
    m_uLPos    = g.GetUniformLocation(m_prog, "uLPos");
    m_uLDir    = g.GetUniformLocation(m_prog, "uLDir");
    m_uLCol    = g.GetUniformLocation(m_prog, "uLCol");
    m_uLRange  = g.GetUniformLocation(m_prog, "uLRange");
    m_uLCosOut = g.GetUniformLocation(m_prog, "uLCosOut");
    m_uLCosIn  = g.GetUniformLocation(m_prog, "uLCosIn");
    m_uNormalTex = g.GetUniformLocation(m_prog, "uNormalTex");
    m_uHasNormal = g.GetUniformLocation(m_prog, "uHasNormal");
    m_uNormalStrength = g.GetUniformLocation(m_prog, "uNormalStrength");
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

// Upload (once) a material texture by name and cache its GL object. Shares the
// software renderer's image cache via GetCachedTexture, so the same files/registered
// images appear. Returns 0 (cached) when the texture is missing, so we don't retry.
unsigned int GLRenderer::TextureFor(const std::string& name) {
    if (name.empty()) return 0;
    auto it = m_texCache.find(name);
    if (it != m_texCache.end()) return it->second;
    unsigned int tex = 0;
    Image* img = GetCachedTexture(name);
    if (img && img->Width() > 0) {
        g.GenTextures(1, &tex);
        g.BindTexture(GL_TEXTURE_2D, tex);
        g.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img->Width(), img->Height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img->Data());
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (g.GenerateMipmap) {
            g.GenerateMipmap(GL_TEXTURE_2D);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            // Anisotropic filtering: keeps textured floors/terrain sharp at grazing
            // angles instead of blurring into the distance (no-op if unsupported).
            g.TexParameteri(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, 8);
        } else {
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
    }
    m_texCache[name] = tex;
    return tex;
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
    Vec3 lightTravel = SceneLight::Direction();   // direction light travels (for ground shadows)
    g.Uniform3f(m_uLightDir, toLight.x, toLight.y, toLight.z);
    g.Uniform1f(m_uShadowAlpha, -1.0f);           // normal (lit) draws; flipped on for the shadow pass
    g.Uniform3f(m_uLightColor, 0.85f, 0.85f, 0.85f);
    // Hemisphere/colored ambient from the scene's gathered lights (falls back to grey).
    Vec3 amb = SceneLights::AmbientColor();
    if (amb.x + amb.y + amb.z < 1e-4f) amb = Vec3{0.35f, 0.36f, 0.40f};
    g.Uniform3f(m_uAmbient, amb.x, amb.y, amb.z);
    g.Uniform3f(m_uEye, eye.x, eye.y, eye.z);

    // Upload every gathered scene light (directional + point + spot) so all lights
    // shade on the GPU exactly like the software renderer — not just the sun.
    {
        const auto& L = SceneLights::List();
        int n = (int)L.size(); if (n > 8) n = 8;
        float lt[8] = {0}, lr[8] = {0}, lco[8] = {0}, lci[8] = {0};
        float lp[24] = {0}, ld[24] = {0}, lc[24] = {0};
        for (int i = 0; i < n; ++i) {
            const LightSample& s = L[i];
            lt[i] = (float)s.type;
            lp[i*3+0] = s.pos.x; lp[i*3+1] = s.pos.y; lp[i*3+2] = s.pos.z;
            Vec3 d = s.dir.Normalized();
            ld[i*3+0] = d.x; ld[i*3+1] = d.y; ld[i*3+2] = d.z;
            lc[i*3+0] = s.color.x; lc[i*3+1] = s.color.y; lc[i*3+2] = s.color.z;
            lr[i] = s.range; lco[i] = s.cosOuter; lci[i] = s.cosInner;
        }
        g.Uniform1i(m_uLightCount, n);
        if (n > 0) {
            g.Uniform1fv(m_uLType, n, lt);
            g.Uniform3fv(m_uLPos, n, lp);
            g.Uniform3fv(m_uLDir, n, ld);
            g.Uniform3fv(m_uLCol, n, lc);
            g.Uniform1fv(m_uLRange, n, lr);
            g.Uniform1fv(m_uLCosOut, n, lco);
            g.Uniform1fv(m_uLCosIn, n, lci);
        }
    }

    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || go == ignore) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled) continue;
        const Mesh& mesh = mr->mesh;
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        if (V.empty() || T.size() < 3) continue;
        const bool hasN = mesh.HasNormals();
        // Texture the mesh whenever the material has one. UVs come from the mesh; if it
        // has none we box-project (dominant face axis), matching the software renderer
        // so e.g. an untextured-UV player model still shows its texture on the GPU path.
        const bool hasUV = !mesh.uvs.empty() && mesh.uvs.size() == V.size();
        // Per-triangle face colors color UV-less meshes in the software renderer (the
        // Character's skin/clothing, foliage tints, ...). Honor them here so those models
        // don't collapse to one flat uColor on the GPU path.
        const bool faceCols = mesh.HasFaceColors();
        unsigned int tex = TextureFor(mr->texture);

        // Expand triangles (non-indexed) into pos(3)+normal(3)+uv(2)+color(3): flat shading via
        // per-face normal when the mesh has none, smooth via per-vertex normals when it does.
        const int nv = (int)V.size();
        m_verts.clear(); m_verts.reserve(T.size() * 14);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], c = T[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue; // skip bad tris
            // Face normal: used for flat shading AND for box-projecting UVs.
            Vec3 fn = Vec3::Cross(V[b] - V[a], V[c] - V[a]);
            { float m = fn.Magnitude(); fn = m > 1e-8f ? fn * (1.0f / m) : Vec3{0, 1, 0}; }
            const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
            float cr = 1.0f, cg = 1.0f, cb = 1.0f;  // white = no-op when there are no face colors
            if (faceCols) { const Color& fc = mesh.triColors[i / 3]; cr = fc.r; cg = fc.g; cb = fc.b; }
            const int idx[3] = {a, b, c};
            // Per-vertex UVs (mesh or box-projected) first, so we can derive the face tangent.
            float uu[3], vv[3];
            for (int k = 0; k < 3; ++k) {
                const Vec3& p = V[idx[k]];
                if (hasUV) { uu[k] = mesh.uvs[idx[k]].x; vv[k] = mesh.uvs[idx[k]].y; }
                else if (ax >= ay && ax >= az) { uu[k] = p.z + 0.5f; vv[k] = p.y + 0.5f; }
                else if (ay >= ax && ay >= az) { uu[k] = p.x + 0.5f; vv[k] = p.z + 0.5f; }
                else                           { uu[k] = p.x + 0.5f; vv[k] = p.y + 0.5f; }
            }
            // Local-space per-face tangent (edges + UV deltas); transformed to world by
            // mat3(uModel) in the vertex shader. Mirrors the software renderer's TBN.
            Vec3 e1 = V[b] - V[a], e2 = V[c] - V[a];
            float du1 = uu[1] - uu[0], dv1 = vv[1] - vv[0];
            float du2 = uu[2] - uu[0], dv2 = vv[2] - vv[0];
            float det = du1 * dv2 - du2 * dv1;
            Vec3 tan = std::fabs(det) > 1e-8f ? (e1 * dv2 - e2 * dv1) * (1.0f / det) : e1;
            { float m = tan.Magnitude(); tan = m > 1e-6f ? tan * (1.0f / m) : Vec3{1, 0, 0}; }
            for (int k = 0; k < 3; ++k) {
                const Vec3& p = V[idx[k]];
                Vec3 n = hasN ? mesh.normals[idx[k]] : fn;
                m_verts.push_back(p.x); m_verts.push_back(p.y); m_verts.push_back(p.z);
                m_verts.push_back(n.x); m_verts.push_back(n.y); m_verts.push_back(n.z);
                m_verts.push_back(uu[k]); m_verts.push_back(vv[k]);
                m_verts.push_back(cr);  m_verts.push_back(cg);  m_verts.push_back(cb);
                m_verts.push_back(tan.x); m_verts.push_back(tan.y); m_verts.push_back(tan.z);
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
        g.Uniform1f(m_uShaderMode, (float)(int)mr->shader);
        g.Uniform1f(m_uToonBands, (float)mr->toonBands);
        g.Uniform1f(m_uRimStr, mr->rimStrength);
        g.Uniform3f(m_uRimColor, mr->rimColor.r, mr->rimColor.g, mr->rimColor.b);
        g.Uniform3f(m_uGradTop, mr->gradientTop.r, mr->gradientTop.g, mr->gradientTop.b);
        g.Uniform3f(m_uGradBot, mr->gradientBottom.r, mr->gradientBottom.g, mr->gradientBottom.b);
        if (tex) {
            g.ActiveTexture(GL_TEXTURE0);
            g.BindTexture(GL_TEXTURE_2D, tex);
            // Per-material filter: Pixel = crisp nearest (no up-close blur); Smooth =
            // trilinear. Set on the bound texture each draw so one cache serves both.
            bool pixel = mr->texFilter == MeshRenderer::TexFilter::Pixel;
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pixel ? GL_NEAREST : GL_LINEAR);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            pixel ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
            g.Uniform1i(m_uTex, 0);
            g.Uniform1f(m_uUseTex, 1.0f);
            g.Uniform2f(m_uTiling, mr->tiling.x, mr->tiling.y);
        } else {
            g.Uniform1f(m_uUseTex, 0.0f);
            g.Uniform2f(m_uTiling, 1.0f, 1.0f);
        }

        // Normal/bump map on texture unit 1: perturbs the surface normal per fragment
        // so flat faces show sculpted detail under lighting, matching the software path.
        unsigned int ntex = mr->normalMap.empty() ? 0u : TextureFor(mr->normalMap);
        if (ntex) {
            g.ActiveTexture(GL_TEXTURE1);
            g.BindTexture(GL_TEXTURE_2D, ntex);
            g.Uniform1i(m_uNormalTex, 1);
            g.Uniform1f(m_uHasNormal, 1.0f);
            g.Uniform1f(m_uNormalStrength, mr->normalStrength);
            g.ActiveTexture(GL_TEXTURE0);
        } else {
            g.Uniform1f(m_uHasNormal, 0.0f);
        }

        g.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)), m_verts.data(), GL_DYNAMIC_DRAW);
        const GLsizei stride = 14 * sizeof(float);
        g.EnableVertexAttribArray(0);
        g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
        g.EnableVertexAttribArray(1);
        g.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(3 * sizeof(float)));
        g.EnableVertexAttribArray(2);
        g.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(6 * sizeof(float)));
        g.EnableVertexAttribArray(3);
        g.VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(8 * sizeof(float)));
        g.EnableVertexAttribArray(4);
        g.VertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(11 * sizeof(float)));
        g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(m_verts.size() / 14));

        // Ground contact shadow: re-draw the SAME geometry flattened onto the ground
        // plane (a planar projection along the light) as a flat translucent black, so
        // the object reads as sitting on the ground instead of floating.
        if (mr->groundShadow && mr->groundShadowStrength > 0.001f) {
            Mat4 shadowModel = Mat4::PlanarShadow(mr->groundShadowY + 0.01f, lightTravel) * model;
            Mat4 smvp = vp * shadowModel;
            g.UniformMatrix4fv(m_uMVP, 1, GL_FALSE, smvp.m);
            g.Uniform1f(m_uShadowAlpha, mr->groundShadowStrength > 1.0f ? 1.0f : mr->groundShadowStrength);
            g.Enable(GL_BLEND);
            g.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            g.DepthMask(GL_FALSE);
            g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(m_verts.size() / 14));
            g.DepthMask(GL_TRUE);
            g.Disable(GL_BLEND);
            g.Uniform1f(m_uShadowAlpha, -1.0f);
        }
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
    for (auto& kv : m_texCache) if (kv.second) g.DeleteTextures(1, &kv.second);
    m_texCache.clear();
    if (m_vbo) { g.DeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao && g.DeleteVertexArrays) { g.DeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_prog) { g.DeleteProgram(m_prog); m_prog = 0; }
}

} // namespace okay
