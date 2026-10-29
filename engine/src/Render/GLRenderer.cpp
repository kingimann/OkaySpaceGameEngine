#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture: shares the texture cache
#include "okay/Render/ShadowSetup.hpp"
#include "okay/Render/Atmosphere.hpp"         // sky palette, fog modes, post settings
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SkinnedMesh.hpp"    // skinned meshes: VB cache keyed on SkinnedMesh::version
#include "okay/Components/Character.hpp"
#include <utility>
#include "okay/Core/Log.hpp"

#include <GL/gl.h>          // GL 1.1 types + enums only
#include <cmath>
#include <cstring>
#include <string>
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
#ifndef GL_RGBA16F
#define GL_RGBA16F             0x881A
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
#ifndef GL_TEXTURE2
#define GL_TEXTURE2            0x84C2
#endif
#ifndef GL_TEXTURE3
#define GL_TEXTURE3            0x84C3
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
typedef void   (*PFNUniform4fv)(GLint, GLsizei, const GLfloat*);
typedef void   (*PFNGetIntegerv)(GLenum, GLint*);
typedef void   (*PFNGenBuffers)(GLsizei, GLuint*);
typedef void   (*PFNBindBuffer)(GLenum, GLuint);
typedef void   (*PFNBufferData)(GLenum, GLsizeiptrOK, const void*, GLenum);
typedef void   (*PFNDeleteBuffers)(GLsizei, const GLuint*);
typedef void   (*PFNVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (*PFNEnableVertexAttribArray)(GLuint);
typedef void   (*PFNDisableVertexAttribArray)(GLuint);
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
// Depth-only FBO (shadow map): GL needs an explicit "no colour buffer" so a
// depth-texture-only framebuffer is complete. glDrawBuffer/glReadBuffer are core 1.0.
typedef void   (*PFNDrawBuffer)(GLenum);
typedef void   (*PFNReadBuffer)(GLenum);

struct GL {
    PFNCreateShader CreateShader; PFNShaderSource ShaderSource; PFNCompileShader CompileShader;
    PFNGetShaderiv GetShaderiv; PFNGetShaderInfoLog GetShaderInfoLog;
    PFNCreateProgram CreateProgram; PFNAttachShader AttachShader; PFNBindAttribLocation BindAttribLocation;
    PFNLinkProgram LinkProgram; PFNGetProgramiv GetProgramiv; PFNGetProgramInfoLog GetProgramInfoLog;
    PFNDeleteShader DeleteShader; PFNDeleteProgram DeleteProgram; PFNUseProgram UseProgram;
    PFNGetUniformLocation GetUniformLocation; PFNUniformMatrix4fv UniformMatrix4fv;
    PFNUniform3f Uniform3f; PFNUniform1f Uniform1f;
    PFNUniform3fv Uniform3fv; PFNUniform1fv Uniform1fv;
    PFNUniform4fv Uniform4fv = nullptr; PFNGetIntegerv GetIntegerv = nullptr;   // GPU skinning
    PFNGenBuffers GenBuffers; PFNBindBuffer BindBuffer; PFNBufferData BufferData; PFNDeleteBuffers DeleteBuffers;
    PFNVertexAttribPointer VertexAttribPointer; PFNEnableVertexAttribArray EnableVertexAttribArray;
    PFNDisableVertexAttribArray DisableVertexAttribArray;
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
    PFNDrawBuffer DrawBuffer; PFNReadBuffer ReadBuffer;
    bool ok = false;
};
GL g;   // resolved entry points

template <class F> bool load(F& fn, GLRenderer::GLGetProc gp, const char* name) {
    fn = reinterpret_cast<F>(gp(name));
    return fn != nullptr;
}

// GPU skinning, shared by every vertex shader that draws meshes: uBones holds
// each joint's skin matrix (meshNode^-1 * jointWorld * inverseBind) as three
// rows; a vertex blends its four joints' rows by weight and applies them once.
// SKIN_BONES is defined when the program is built (from the GPU's uniform limit).
const char* kSkinGlsl =
    "uniform float uSkin; uniform vec4 uBones[SKIN_BONES * 3];\n"
    "attribute vec4 aJoints; attribute vec4 aWeights;\n"
    "void skinRows(out vec4 r0, out vec4 r1, out vec4 r2){\n"
    "  int j0 = int(aJoints.x + 0.5) * 3, j1 = int(aJoints.y + 0.5) * 3;\n"
    "  int j2 = int(aJoints.z + 0.5) * 3, j3 = int(aJoints.w + 0.5) * 3;\n"
    "  r0 = uBones[j0] * aWeights.x + uBones[j1] * aWeights.y + uBones[j2] * aWeights.z + uBones[j3] * aWeights.w;\n"
    "  r1 = uBones[j0+1] * aWeights.x + uBones[j1+1] * aWeights.y + uBones[j2+1] * aWeights.z + uBones[j3+1] * aWeights.w;\n"
    "  r2 = uBones[j0+2] * aWeights.x + uBones[j1+2] * aWeights.y + uBones[j2+2] * aWeights.z + uBones[j3+2] * aWeights.w;\n"
    "}\n"
    "vec3 skinPos(vec3 p){\n"
    "  if (uSkin < 0.5) return p;\n"
    "  vec4 r0, r1, r2; skinRows(r0, r1, r2); vec4 h = vec4(p, 1.0);\n"
    "  return vec3(dot(r0, h), dot(r1, h), dot(r2, h));\n"
    "}\n";

const char* kVert =
    "#version 120\n"
    "SKIN\n"
    "uniform mat4 uMVP; uniform mat4 uModel; uniform vec2 uTiling; uniform vec2 uTexOffset;\n"
    "attribute vec3 aPos; attribute vec3 aNormal; attribute vec2 aUV; attribute vec3 aColor;\n"
    "attribute vec3 aTan;\n"
    "varying vec3 vN; varying vec3 vWorld; varying vec2 vUV; varying vec3 vCol; varying vec3 vTan;\n"
    "void main(){\n"
    "  vec3 P = aPos, Nn = aNormal, Tt = aTan;\n"
    "  if (uSkin > 0.5) {\n"
    "    vec4 r0, r1, r2; skinRows(r0, r1, r2); vec4 h = vec4(aPos, 1.0);\n"
    "    P = vec3(dot(r0, h), dot(r1, h), dot(r2, h));\n"
    "    Nn = vec3(dot(r0.xyz, aNormal), dot(r1.xyz, aNormal), dot(r2.xyz, aNormal));\n"
    "    Tt = vec3(dot(r0.xyz, aTan), dot(r1.xyz, aTan), dot(r2.xyz, aTan));\n"
    "  }\n"
    "  gl_Position = uMVP * vec4(P,1.0);\n"
    "  vN = mat3(uModel) * Nn;\n"
    "  vTan = mat3(uModel) * Tt;\n"
    "  vWorld = (uModel * vec4(P,1.0)).xyz;\n"
    "  vUV = aUV * uTiling + uTexOffset;\n"
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
    "uniform float uAlpha;\n"         // material opacity (1 = opaque); blended for water/glass
    "uniform int uLightCount;\n"      // multi-light: directional + point + spot from the scene
    "uniform float uLType[16]; uniform vec3 uLPos[16]; uniform vec3 uLDir[16];\n"
    "uniform vec3 uLCol[16]; uniform float uLRange[16]; uniform float uLCosOut[16]; uniform float uLCosIn[16];\n"
    "uniform float uLFalloff[16];\n"   // 0 = linear (1-d/r)^2, 1 = physical inverse-square
    "uniform float uFogOn; uniform vec3 uFogColor; uniform float uFogStart; uniform float uFogEnd;\n"  // distance fog
    "uniform float uFogMode; uniform float uFogDensity; uniform float uFogHeight; uniform float uFogFalloff;\n"  // linear / exponential / height (Atmosphere.hpp)
    "uniform float uTonemap;\n"                                                                        // filmic ACES tonemap toggle
    "uniform float uHdrOut;\n"                                                                         // 1 = leave the colour linear for the post chain
    "vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }\n"        // ACES filmic curve
    "uniform sampler2D uNormalTex; uniform float uHasNormal; uniform float uNormalStrength;\n"  // bump/normal map
    "uniform sampler2D uSpecTex; uniform float uHasSpecMap;\n"                  // gloss map (per-texel specular)
    "uniform sampler2D uAoTex; uniform float uHasAo; uniform float uAoStrength;\n"  // ambient-occlusion map
    "uniform float uMetallic; uniform float uReflectivity;\n"                  // PBR metalness + env reflection
    "uniform vec3 uSkyTop; uniform vec3 uSkyHor; uniform vec3 uSkyBot; uniform float uEnvOn;\n"  // sky for reflections
    // ---- Shadows ----
    // Sun: up to 4 ortho cascades side by side in one depth atlas (count*S x S).
    // Local: a 4x4 atlas of tiles for spot (1 tile) and point/area (6 cube-face
    // tiles) lights. Both are sampler2DShadow, so every tap is a hardware 2x2
    // bilinear depth compare; 12 rotated Poisson taps on top give soft edges.
    "uniform sampler2DShadow uShadowTex; uniform float uShadowOn;\n"
    "uniform mat4 uCasVP[4]; uniform vec4 uCasSphere[4]; uniform float uCasTexel[4];\n"
    "uniform float uCasCount; uniform float uCasSize; uniform float uShadowSoft;\n"
    "uniform float uSunIndex; uniform float uSunStrength; uniform float uSunBias;\n"
    "uniform sampler2DShadow uLocalTex; uniform mat4 uLocalVP[16]; uniform vec4 uLShadow[16];\n"   // x=first tile (-1 none), y=strength, z=bias, w=texel at 1m
    "uniform float uLocalSize; uniform float uReceive;\n"
    "uniform vec3 uAmbSky; uniform vec3 uAmbGround;\n"                                          // ambient mode (up / down)
    "varying vec3 vN; varying vec3 vWorld; varying vec2 vUV; varying vec3 vCol; varying vec3 vTan;\n"
    "mat2 gRot;\n"                                                                              // per-pixel Poisson rotation
    "float sunTap(vec2 uv, vec2 o, float z, float ci){\n"
    "  float e = 1.0 / uCasSize;\n"                                                             // stay a texel inside the cascade tile
    "  vec2 t = clamp(uv + gRot * o, vec2(e), vec2(1.0 - e));\n"
    "  return shadow2D(uShadowTex, vec3((ci + t.x) / uCasCount, t.y, z)).r;\n"
    "}\n"
    "float sunCascade(int i, vec3 wp, vec3 n){\n"
    "  float tx = uCasTexel[i];\n"
    // World-space bias: push the receiver off its surface along the normal (more
    // for a wider filter) and a little toward the sun. Same rule for every cascade.
    "  vec3 p = wp + n * (tx * (1.0 + 0.6 * uShadowSoft) * uSunBias) + uLightDir * (tx * 0.75 * uSunBias);\n"
    "  vec3 q = (uCasVP[i] * vec4(p, 1.0)).xyz * 0.5 + 0.5;\n"
    "  if (q.z >= 1.0) return 1.0;\n"
    "  float r = uShadowSoft / uCasSize; float ci = float(i); float s = 0.0;\n"
    "  s += sunTap(q.xy, vec2(-0.326,-0.406)*r, q.z, ci); s += sunTap(q.xy, vec2(-0.840,-0.074)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2(-0.696, 0.457)*r, q.z, ci); s += sunTap(q.xy, vec2(-0.203, 0.621)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2( 0.962,-0.195)*r, q.z, ci); s += sunTap(q.xy, vec2( 0.473,-0.480)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2( 0.519, 0.767)*r, q.z, ci); s += sunTap(q.xy, vec2( 0.185,-0.893)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2( 0.507, 0.064)*r, q.z, ci); s += sunTap(q.xy, vec2( 0.896, 0.412)*r, q.z, ci);\n"
    "  s += sunTap(q.xy, vec2(-0.322,-0.933)*r, q.z, ci); s += sunTap(q.xy, vec2(-0.792,-0.598)*r, q.z, ci);\n"
    "  return s / 12.0;\n"
    "}\n"
    "float shadowFactor(vec3 wp, vec3 n){\n"                                                    // sun: 0 = shadowed .. 1 = lit
    "  int cnt = int(uCasCount + 0.5);\n"
    "  for (int i = 0; i < 4; i++) { if (i >= cnt) break;\n"
    "    float d = length(wp - uCasSphere[i].xyz); float R = uCasSphere[i].w;\n"
    "    if (d >= R) continue;\n"                                                               // tightest cascade holding the point
    "    float s = sunCascade(i, wp, n);\n"
    "    float edge = R * 0.82;\n"                                                              // blend into the next cascade (no seam)
    "    if (d > edge) { float f = (d - edge) / (R - edge);\n"
    "      s = mix(s, (i + 1 < cnt) ? sunCascade(i + 1, wp, n) : 1.0, f); }\n"                // last one fades out at the distance
    "    return 1.0 - uSunStrength * (1.0 - s);\n"
    "  }\n"
    "  return 1.0;\n"
    "}\n"
    "float localTap(vec2 org, vec2 uv, vec2 o, float z){\n"
    "  float e = 1.0 / uLocalSize;\n"
    "  vec2 t = clamp(uv + gRot * o, vec2(e), vec2(1.0 - e));\n"
    "  return shadow2D(uLocalTex, vec3(org + t * 0.25, z)).r;\n"
    "}\n"
    "float localShadow(int li, vec3 wp, vec3 n, vec3 l, float dist){\n"                         // spot/point/area: 0 = shadowed .. 1 = lit
    "  vec4 info = uLShadow[li];\n"
    "  float tw = dist * info.w;\n"                                                             // world size of a shadow texel here
    "  vec3 p = wp + n * (tw * (1.5 + 0.6 * uShadowSoft) * info.z) + l * (tw * info.z);\n"
    "  float tile = info.x;\n"
    "  if (uLType[li] < 1.5 || uLType[li] > 2.5) {\n"                                          // point / area: pick the cube face
    "    vec3 v = p - uLPos[li]; vec3 a = abs(v);\n"
    "    if (a.x >= a.y && a.x >= a.z) tile += (v.x > 0.0) ? 0.0 : 1.0;\n"
    "    else if (a.y >= a.z) tile += (v.y > 0.0) ? 2.0 : 3.0;\n"
    "    else tile += (v.z > 0.0) ? 4.0 : 5.0;\n"
    "  }\n"
    "  vec4 sc = uLocalVP[int(tile + 0.5)] * vec4(p, 1.0);\n"
    "  if (sc.w <= 1e-5) return 1.0;\n"
    "  vec3 q = sc.xyz / sc.w * 0.5 + 0.5;\n"
    "  if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0 || q.z >= 1.0) return 1.0;\n"
    "  vec2 org = vec2(mod(tile, 4.0), floor(tile / 4.0 + 0.01)) * 0.25;\n"
    "  float r = uShadowSoft / uLocalSize; float s = 0.0;\n"
    "  s += localTap(org, q.xy, vec2(-0.326,-0.406)*r, q.z); s += localTap(org, q.xy, vec2(-0.840,-0.074)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2(-0.696, 0.457)*r, q.z); s += localTap(org, q.xy, vec2(-0.203, 0.621)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2( 0.962,-0.195)*r, q.z); s += localTap(org, q.xy, vec2( 0.473,-0.480)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2( 0.519, 0.767)*r, q.z); s += localTap(org, q.xy, vec2( 0.185,-0.893)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2( 0.507, 0.064)*r, q.z); s += localTap(org, q.xy, vec2( 0.896, 0.412)*r, q.z);\n"
    "  s += localTap(org, q.xy, vec2(-0.322,-0.933)*r, q.z); s += localTap(org, q.xy, vec2(-0.792,-0.598)*r, q.z);\n"
    "  return 1.0 - info.y * (1.0 - s / 12.0);\n"
    "}\n"
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
    // Filmic mode = a gamma-correct pipeline: albedo/emissive are authored in
    // sRGB, so decode to LINEAR before lighting and re-encode at the end.
    // Lighting math done directly in sRGB space (the old path, kept when the
    // toggle is off) is what makes shading look flat and washed-out.
    "  bool lin = uTonemap > 0.5;\n"
    "  if (lin) base = pow(max(base, vec3(0.0)), vec3(2.2));\n"
    "  vec3 emis = uEmissive;\n"
    "  if (lin) emis = pow(max(emis, vec3(0.0)), vec3(2.2));\n"
    // Unlit: decode+encode cancel, so unlit colors look identical either way.
    "  if (uUnlit > 0.5) { vec3 uc = base + emis;\n"
    "    if (lin && uHdrOut < 0.5) uc = pow(max(uc, vec3(0.0)), vec3(1.0/2.2));\n"
    "    gl_FragColor = vec4(uc, uAlpha); return; }\n"
    "  bool toon = uShaderMode > 1.5 && uShaderMode < 2.5 && uToonBands > 0.5;\n"
    "  vec3 amb = mix(uAmbGround, uAmbSky, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));\n"  // scene Ambient Mode (flat / hemisphere / sky)
    "  vec3 lit = amb; vec3 spec = vec3(0.0);\n"
    "  { float a = 52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) * 6.2831853;\n"
    "    float ca = cos(a), sa = sin(a); gRot = mat2(ca, sa, -sa, ca); }\n"   // interleaved-gradient noise: rotate the Poisson disk per pixel
    "  vec3 Ns = normalize(vN);\n"                                // geometric normal for shadow bias (not the bumped one)
    "  bool recv = uReceive > 0.5;\n"                            // MeshRenderer::receiveShadows
    "  float sh = (uShadowOn > 0.5 && recv) ? shadowFactor(vWorld, Ns) : 1.0;\n"   // sun occlusion (real cast shadows)
    "  if (uLightCount < 1) {\n"                                  // fallback: the global directional
    "    float ndl = max(dot(N, uLightDir), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "    lit += uLightColor * ndl * sh;\n"
    "    if (uSpecular > 0.0) { vec3 H = normalize(uLightDir + Vv);\n"
    "      spec = vec3(pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular * sh); }\n"
    "  } else {\n"
    "    for (int i = 0; i < 16; i++) { if (i >= uLightCount) break;\n"   // directional + point + spot
    "      vec3 ld; float at = 1.0;\n"
    "      float wrap = 0.0; float d = 0.0;\n"
    "      if (uLType[i] < 0.5) { ld = normalize(-uLDir[i]); }\n"        // directional
    "      else { vec3 toL = uLPos[i] - vWorld; d = length(toL); ld = toL / max(d, 1e-4);\n"
    "        float rng = max(uLRange[i], 1e-4);\n"
    "        if (uLFalloff[i] > 0.5) { float dr = d/rng; float win = max(1.0 - dr*dr*dr*dr, 0.0); win*=win;\n"  // physical inverse-square, windowed
    "          at = win / (1.0 + 8.0*d*d/(rng*rng)); }\n"
    "        else { at = max(1.0 - d / rng, 0.0); at *= at; }\n"                                                // linear rolloff
    "        if (uLType[i] > 2.5) { wrap = 0.5; }\n"                                                            // area: soft wrap fill
    "        else if (uLType[i] > 1.5) { float cs = dot(normalize(uLDir[i]), -ld);\n"   // spot cone
    "          float dn = uLCosIn[i] - uLCosOut[i];\n"
    "          float sp = dn > 1e-4 ? clamp((cs - uLCosOut[i]) / dn, 0.0, 1.0) : (cs >= uLCosOut[i] ? 1.0 : 0.0);\n"
    "          at *= sp * sp; } }\n"
    "      float ndl = max((dot(N, ld) + wrap) / (1.0 + wrap), 0.0); if (toon) ndl = ceil(ndl*uToonBands)/uToonBands;\n"
    "      if (ndl * at <= 0.0) continue;\n"                      // no light reaches here: skip the shadow lookup
    "      float lsh = 1.0;\n"
    "      if (uLType[i] < 0.5) { if (abs(float(i) - uSunIndex) < 0.5) lsh = sh; }\n"   // only the main sun has cascades
    "      else if (recv && uLShadow[i].x > -0.5) lsh = localShadow(i, vWorld, Ns, ld, d);\n"     // spot / point / area shadow tiles
    "      lit += uLCol[i] * ndl * at * lsh;\n"
    "      if (uSpecular > 0.0) { vec3 H = normalize(ld + Vv);\n"   // highlights take the light's colour + intensity
    "        spec += uLCol[i] * (pow(max(dot(N,H),0.0), max(uShininess,1.0)) * uSpecular * at * lsh); }\n"
    "    }\n"
    "  }\n"
    "  if (uShaderMode > 7.5) lit += fz * fz * (lit) * 0.6;\n"     // Velvet: fuzzy grazing sheen
    "  float gloss = 1.0;\n"                                      // gloss/specular map (per-texel shininess)
    "  if (uHasSpecMap > 0.5) gloss = dot(texture2D(uSpecTex, vUV).rgb, vec3(0.2126, 0.7152, 0.0722));\n"
    "  if (uHasAo > 0.5) {\n"                                     // occlude ambient + diffuse in creases
    "    float ao = dot(texture2D(uAoTex, vUV).rgb, vec3(0.2126, 0.7152, 0.0722));\n"
    "    lit *= (1.0 - uAoStrength * (1.0 - ao));\n"
    "  }\n"
    "  float metal = clamp(uMetallic, 0.0, 1.0);\n"               // metalness: kill diffuse, tint spec/reflection
    "  float diffK = 1.0 - 0.9 * metal;\n"
    "  vec3 f0 = mix(vec3(1.0), base, metal);\n"                  // dielectric = white, metal = albedo-tinted
    "  vec3 diff = base * lit * diffK;\n"
    "  vec3 rim = vec3(0.0);\n"                                   // rim glow (Fresnel/Hologram or rimStr>0)
    "  float rs = uRimStr; if ((fres || holo) && rs < 0.8) rs = 1.6;\n"
    "  if (rs > 0.0) rim = uRimColor * (fz*fz*fz * rs);\n"
    "  vec3 col = diff + (spec * gloss) * f0 + rim + emis;\n"
    "  float reflAmt = max(uReflectivity, metal);\n"              // env reflection of the sky gradient
    "  float reflK = reflAmt * gloss;\n"
    "  if (reflK > 0.0 && uEnvOn > 0.5) {\n"
    "    float ndv = max(dot(N, Vv), 0.0);\n"
    "    vec3 R = reflect(-Vv, N); float ry = clamp(R.y, -1.0, 1.0);\n"
    "    vec3 env = mix(uSkyHor, ry >= 0.0 ? uSkyTop : uSkyBot, abs(ry));\n"
    "    if (lin) env = pow(max(env, vec3(0.0)), vec3(2.2));\n"
    "    float fr2 = 1.0 - ndv; fr2 = fr2*fr2*fr2*fr2*fr2;\n"     // Schlick (1-n·v)^5
    "    float kk = reflK + (1.0 - reflK) * fr2;\n"
    "    col = col * (1.0 - kk) + env * f0 * kk;\n"
    "  }\n"
    "  if (uFogOn > 0.5) {\n"                                      // fog (FogFactor in Atmosphere.hpp, same maths)
    "    float fd = length(uEye - vWorld);\n"
    "    float ff;\n"
    "    if (uFogMode < 0.5) ff = clamp((fd - uFogStart) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);\n"   // linear
    "    else if (uFogMode < 1.5) ff = 1.0 - exp(-uFogDensity * fd);\n"                                   // exponential
    "    else { float te = exp(-max(uEye.y - uFogHeight, 0.0) * uFogFalloff);\n"                          // height: thins above fogHeight
    "           float tw = exp(-max(vWorld.y - uFogHeight, 0.0) * uFogFalloff);\n"
    "           ff = 1.0 - exp(-uFogDensity * fd * 0.5 * (te + tw)); }\n"
    "    vec3 fc = uFogColor;\n"
    "    if (lin) fc = pow(max(fc, vec3(0.0)), vec3(2.2));\n"      // fog is authored sRGB too
    "    col = mix(col, fc, ff);\n"
    "  }\n"
    // Filmic output: ACES roll-off in linear light, then encode back to sRGB.
    // The gamma encode is what was missing before — without it linear results
    // were displayed raw, crushing mid-tones. With the post chain running the
    // colour stays linear here: the chain exposes, tonemaps and encodes it.
    "  if (lin && uHdrOut < 0.5) { col = aces(col); col = pow(max(col, vec3(0.0)), vec3(1.0/2.2)); }\n"
    "  gl_FragColor = vec4(col, uAlpha);\n"
    "}\n";

// ---- Depth-only program (shadow-map pass) -------------------------------------
// Renders just clip-space position from the light's point of view; the depth
// buffer it fills IS the shadow map the main shader samples. No colour output.
const char* kDepthVert =
    "#version 120\n"
    "SKIN\n"
    "attribute vec3 aPos;\n"
    "uniform mat4 uDepthMVP;\n"
    "void main(){ gl_Position = uDepthMVP * vec4(skinPos(aPos), 1.0); }\n";
const char* kDepthFrag =
    "#version 120\n"
    "void main(){ }\n";   // depth is written automatically; no colour

// Joints the skinning shaders take (from the GPU's vertex uniform limit; set once).
int g_skinBones = 0;

GLuint compile(GLenum type, const char* src) {
    // A "SKIN" line expands to the skinning helpers sized for this GPU.
    std::string expanded;
    if (const char* at = std::strstr(src, "SKIN\n")) {
        if (g_skinBones == 0) {
            GLint comps = 1024;   // GL guarantees at least this many vertex uniform components
            if (g.GetIntegerv) g.GetIntegerv(0x8B4A /*GL_MAX_VERTEX_UNIFORM_COMPONENTS*/, &comps);
            int n = (comps - 160) / 12;   // leave room for the other uniforms; 12 floats per joint
            g_skinBones = n > 256 ? 256 : (n < 1 ? 1 : n);
        }
        expanded.assign(src, at);
        expanded += "#define SKIN_BONES " + std::to_string(g_skinBones) + "\n";
        expanded += kSkinGlsl;
        expanded += at + 5;
        src = expanded.c_str();
    }
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
    r &= load(g.DisableVertexAttribArray, gp, "glDisableVertexAttribArray");
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
    // Optional: depth-only shadow-map FBO needs these to declare "no colour buffer".
    // If absent, shadow mapping is skipped (renderer still works without cast shadows).
    load(g.DrawBuffer, gp, "glDrawBuffer");
    load(g.ReadBuffer, gp, "glReadBuffer");
    // Optional: GPU skinning (bone matrices as a vec4 uniform array).
    load(g.Uniform4fv, gp, "glUniform4fv");
    load(g.GetIntegerv, gp, "glGetIntegerv");
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
    g.BindAttribLocation(m_prog, 5, "aJoints");
    g.BindAttribLocation(m_prog, 6, "aWeights");
    g.LinkProgram(m_prog);
    GLint ok = 0; g.GetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    g.DeleteShader(vs); g.DeleteShader(fs);
    if (!ok) { char log[512]; g.GetProgramInfoLog(m_prog, 512, nullptr, log); Log::Error("[gl] link: ", log); g.DeleteProgram(m_prog); m_prog = 0; return false; }
    m_uMVP = g.GetUniformLocation(m_prog, "uMVP");
    m_uSkin = g.GetUniformLocation(m_prog, "uSkin");
    m_uBones = g.GetUniformLocation(m_prog, "uBones");
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
    m_uAlpha = g.GetUniformLocation(m_prog, "uAlpha");
    m_uLightCount = g.GetUniformLocation(m_prog, "uLightCount");
    m_uLType   = g.GetUniformLocation(m_prog, "uLType");
    m_uLPos    = g.GetUniformLocation(m_prog, "uLPos");
    m_uLDir    = g.GetUniformLocation(m_prog, "uLDir");
    m_uLCol    = g.GetUniformLocation(m_prog, "uLCol");
    m_uLRange  = g.GetUniformLocation(m_prog, "uLRange");
    m_uLFalloff = g.GetUniformLocation(m_prog, "uLFalloff");
    m_uLCosOut = g.GetUniformLocation(m_prog, "uLCosOut");
    m_uLCosIn  = g.GetUniformLocation(m_prog, "uLCosIn");
    m_uNormalTex = g.GetUniformLocation(m_prog, "uNormalTex");
    m_uHasNormal = g.GetUniformLocation(m_prog, "uHasNormal");
    m_uNormalStrength = g.GetUniformLocation(m_prog, "uNormalStrength");
    m_uTexOffset = g.GetUniformLocation(m_prog, "uTexOffset");
    m_uSpecTex = g.GetUniformLocation(m_prog, "uSpecTex");
    m_uHasSpecMap = g.GetUniformLocation(m_prog, "uHasSpecMap");
    m_uAoTex = g.GetUniformLocation(m_prog, "uAoTex");
    m_uHasAo = g.GetUniformLocation(m_prog, "uHasAo");
    m_uAoStrength = g.GetUniformLocation(m_prog, "uAoStrength");
    m_uMetallic = g.GetUniformLocation(m_prog, "uMetallic");
    m_uReflectivity = g.GetUniformLocation(m_prog, "uReflectivity");
    m_uSkyTop = g.GetUniformLocation(m_prog, "uSkyTop");
    m_uSkyHor = g.GetUniformLocation(m_prog, "uSkyHor");
    m_uSkyBot = g.GetUniformLocation(m_prog, "uSkyBot");
    m_uEnvOn = g.GetUniformLocation(m_prog, "uEnvOn");
    m_uFogOn = g.GetUniformLocation(m_prog, "uFogOn");
    m_uTonemap = g.GetUniformLocation(m_prog, "uTonemap");
    m_uFogColor = g.GetUniformLocation(m_prog, "uFogColor");
    m_uFogStart = g.GetUniformLocation(m_prog, "uFogStart");
    m_uFogEnd = g.GetUniformLocation(m_prog, "uFogEnd");
    m_uFogMode = g.GetUniformLocation(m_prog, "uFogMode");
    m_uFogDensity = g.GetUniformLocation(m_prog, "uFogDensity");
    m_uFogHeight = g.GetUniformLocation(m_prog, "uFogHeight");
    m_uFogFalloff = g.GetUniformLocation(m_prog, "uFogFalloff");
    m_uHdrOut = g.GetUniformLocation(m_prog, "uHdrOut");
    m_uShadowTex = g.GetUniformLocation(m_prog, "uShadowTex");
    m_uShadowOn = g.GetUniformLocation(m_prog, "uShadowOn");
    m_uCasVP = g.GetUniformLocation(m_prog, "uCasVP");
    m_uCasSphere = g.GetUniformLocation(m_prog, "uCasSphere");
    m_uCasTexel = g.GetUniformLocation(m_prog, "uCasTexel");
    m_uCasCount = g.GetUniformLocation(m_prog, "uCasCount");
    m_uCasSize = g.GetUniformLocation(m_prog, "uCasSize");
    m_uShadowSoft = g.GetUniformLocation(m_prog, "uShadowSoft");
    m_uSunIndex = g.GetUniformLocation(m_prog, "uSunIndex");
    m_uSunStrength = g.GetUniformLocation(m_prog, "uSunStrength");
    m_uSunBias = g.GetUniformLocation(m_prog, "uSunBias");
    m_uLocalTex = g.GetUniformLocation(m_prog, "uLocalTex");
    m_uLocalVP = g.GetUniformLocation(m_prog, "uLocalVP");
    m_uLShadow = g.GetUniformLocation(m_prog, "uLShadow");
    m_uLocalSize = g.GetUniformLocation(m_prog, "uLocalSize");
    m_uReceive = g.GetUniformLocation(m_prog, "uReceive");
    m_uAmbSky = g.GetUniformLocation(m_prog, "uAmbSky");
    m_uAmbGround = g.GetUniformLocation(m_prog, "uAmbGround");
    // The two shadow samplers are sampler2DShadow: give them their own texture
    // units up front so they never alias a sampler2D unit (a draw-time error).
    g.UseProgram(m_prog);
    g.Uniform1i(m_uShadowTex, 4);
    g.Uniform1i(m_uLocalTex, 5);
    g.UseProgram(0);
    g.GenBuffers(1, &m_vbo);
    // A core-profile context refuses to draw without a bound VAO (and some drivers
    // crash on the attempt). Create one when available; on a compatibility context
    // the default VAO is used instead. This persists across viewport resizes.
    if (g.GenVertexArrays && !m_vao) g.GenVertexArrays(1, &m_vao);
    return true;
}

bool GLRenderer::EnsureDepthProgram() {
    if (m_depthProg) return true;
    GLuint vs = compile(GL_VERTEX_SHADER, kDepthVert), fs = compile(GL_FRAGMENT_SHADER, kDepthFrag);
    if (!vs || !fs) return false;
    m_depthProg = g.CreateProgram();
    g.AttachShader(m_depthProg, vs); g.AttachShader(m_depthProg, fs);
    g.BindAttribLocation(m_depthProg, 0, "aPos");
    g.BindAttribLocation(m_depthProg, 5, "aJoints");
    g.BindAttribLocation(m_depthProg, 6, "aWeights");
    g.LinkProgram(m_depthProg);
    GLint ok = 0; g.GetProgramiv(m_depthProg, GL_LINK_STATUS, &ok);
    g.DeleteShader(vs); g.DeleteShader(fs);
    if (!ok) { g.DeleteProgram(m_depthProg); m_depthProg = 0; return false; }
    m_uDepthMVP = g.GetUniformLocation(m_depthProg, "uDepthMVP");
    m_udSkin = g.GetUniformLocation(m_depthProg, "uSkin");
    m_udBones = g.GetUniformLocation(m_depthProg, "uBones");
    return true;
}

// (Re)create a depth-only framebuffer of w x h: one GL_DEPTH_COMPONENT texture in
// compare mode (sampler2DShadow: each lookup is a filtered depth test).
bool GLRenderer::EnsureDepthTarget(unsigned int& fbo, unsigned int& tex, int& curW, int& curH, int w, int h) {
    if (fbo && curW == w && curH == h) return true;
    if (tex) { g.DeleteTextures(1, &tex); tex = 0; }
    if (fbo) { g.DeleteFramebuffers(1, &fbo); fbo = 0; }
    curW = curH = 0;
    if (!g.DrawBuffer || !g.ReadBuffer) return false;   // can't make a colourless FBO -> no shadows
    g.GenTextures(1, &tex);
    g.BindTexture(GL_TEXTURE_2D, tex);
    g.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g.TexParameteri(GL_TEXTURE_2D, 0x884C /*GL_TEXTURE_COMPARE_MODE*/, 0x884E /*GL_COMPARE_REF_TO_TEXTURE*/);
    g.TexParameteri(GL_TEXTURE_2D, 0x884D /*GL_TEXTURE_COMPARE_FUNC*/, GL_LEQUAL);
    g.GenFramebuffers(1, &fbo);
    g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex, 0);
    g.DrawBuffer(0 /*GL_NONE*/);   // depth-only: declare no colour attachments
    g.ReadBuffer(0 /*GL_NONE*/);
    bool okFbo = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    g.BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!okFbo) {
        g.DeleteTextures(1, &tex); tex = 0;
        g.DeleteFramebuffers(1, &fbo); fbo = 0;
        return false;
    }
    curW = w; curH = h;
    return true;
}

void GLRenderer::DestroyShadow() {
    if (m_shadowTex) { g.DeleteTextures(1, &m_shadowTex); m_shadowTex = 0; }
    if (m_shadowFbo) { g.DeleteFramebuffers(1, &m_shadowFbo); m_shadowFbo = 0; }
    if (m_localTex) { g.DeleteTextures(1, &m_localTex); m_localTex = 0; }
    if (m_localFbo) { g.DeleteFramebuffers(1, &m_localFbo); m_localFbo = 0; }
    m_shadowW = m_shadowH = m_localW = m_localH = 0;
}

// ---- Bloom post chain -------------------------------------------------------
// Bright pixels glow: threshold the finished frame at half resolution, blur the
// result with two separable gaussian passes, then add it back over the frame.
// Uses the same BloomEnabled()/BloomThreshold()/BloomIntensity() globals as the
// software renderer, so the editor's existing Bloom controls drive both paths.
namespace {
const char* kQuadVert =
    "#version 120\n"
    "attribute vec2 aPos; varying vec2 vUV;\n"
    "void main(){ vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
const char* kBrightFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform float uThresh; varying vec2 vUV;\n"
    "void main(){\n"
    "  vec3 c = texture2D(uSrc, vUV).rgb;\n"
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "  float k = max(l - uThresh, 0.0) / max(l, 1e-4);\n"   // keep only the over-threshold part
    "  gl_FragColor = vec4(c * k, 1.0);\n"
    "}\n";
const char* kBlurFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform vec2 uDir; varying vec2 vUV;\n"   // uDir = one texel H or V
    "void main(){\n"                                                   // 9-tap gaussian (linear-sampled 5-tap)
    "  vec3 s = texture2D(uSrc, vUV).rgb * 0.227027;\n"
    "  s += (texture2D(uSrc, vUV + uDir * 1.384615).rgb + texture2D(uSrc, vUV - uDir * 1.384615).rgb) * 0.316216;\n"
    "  s += (texture2D(uSrc, vUV + uDir * 3.230769).rgb + texture2D(uSrc, vUV - uDir * 3.230769).rgb) * 0.070270;\n"
    "  gl_FragColor = vec4(s, 1.0);\n"
    "}\n";
const char* kAddFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform float uStrength; varying vec2 vUV;\n"
    "void main(){ gl_FragColor = vec4(texture2D(uSrc, vUV).rgb * uStrength, 1.0); }\n";
} // namespace

bool GLRenderer::EnsurePostProgs() {
    if (m_brightProg && m_blurProg && m_addProg) return true;
    auto build = [&](const char* fragSrc, unsigned int& prog) -> bool {
        if (prog) return true;
        GLuint vs = compile(GL_VERTEX_SHADER, kQuadVert), fs = compile(GL_FRAGMENT_SHADER, fragSrc);
        if (!vs || !fs) return false;
        prog = g.CreateProgram();
        g.AttachShader(prog, vs); g.AttachShader(prog, fs);
        g.BindAttribLocation(prog, 0, "aPos");
        g.LinkProgram(prog);
        GLint ok = 0; g.GetProgramiv(prog, GL_LINK_STATUS, &ok);
        g.DeleteShader(vs); g.DeleteShader(fs);
        if (!ok) { g.DeleteProgram(prog); prog = 0; return false; }
        return true;
    };
    if (!build(kBrightFrag, m_brightProg) || !build(kBlurFrag, m_blurProg) || !build(kAddFrag, m_addProg))
        return false;
    m_ubSrc = g.GetUniformLocation(m_brightProg, "uSrc");
    m_ubThresh = g.GetUniformLocation(m_brightProg, "uThresh");
    m_ublSrc = g.GetUniformLocation(m_blurProg, "uSrc");
    m_ublDir = g.GetUniformLocation(m_blurProg, "uDir");
    m_uaSrc = g.GetUniformLocation(m_addProg, "uSrc");
    m_uaStrength = g.GetUniformLocation(m_addProg, "uStrength");
    return EnsureQuad();
}

bool GLRenderer::EnsureBloomTargets(int w, int h) {
    if (m_bloomFboA && m_bloomW == w && m_bloomH == h) return true;
    DestroyBloom();
    m_bloomW = w; m_bloomH = h;
    auto makeTarget = [&](unsigned int& fbo, unsigned int& tex) -> bool {
        g.GenTextures(1, &tex);
        g.BindTexture(GL_TEXTURE_2D, tex);
        g.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g.GenFramebuffers(1, &fbo);
        g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        bool ok = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        g.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return ok;
    };
    if (!makeTarget(m_bloomFboA, m_bloomTexA) || !makeTarget(m_bloomFboB, m_bloomTexB)) {
        DestroyBloom();
        return false;
    }
    return true;
}

void GLRenderer::DestroyBloom() {
    if (m_bloomTexA) { g.DeleteTextures(1, &m_bloomTexA); m_bloomTexA = 0; }
    if (m_bloomTexB) { g.DeleteTextures(1, &m_bloomTexB); m_bloomTexB = 0; }
    if (m_bloomFboA) { g.DeleteFramebuffers(1, &m_bloomFboA); m_bloomFboA = 0; }
    if (m_bloomFboB) { g.DeleteFramebuffers(1, &m_bloomFboB); m_bloomFboB = 0; }
    m_bloomW = m_bloomH = 0;
}

// ---- SSAO (screen-space ambient occlusion) ----------------------------------
// Corners, crevices and contact points darken: the scene's opaque geometry is
// re-rendered at half resolution as PACKED LINEAR DEPTH (camera distance / far,
// 16 bits across the R+G channels of an RGBA8 target — GL2-safe, no float
// textures needed), a spiral of depth-difference taps estimates occlusion, one
// gaussian round smooths the noise, and the result multiplies the frame.
// Driven by the same SSAOEnabled()/SSAORadius()/SSAOStrength() globals as the
// software renderer, so the editor's existing AO controls work unchanged.
namespace {
const float kSsaoFar = 150.0f;   // linear-depth range; AO fades out beyond this
const char* kSsaoDepthVert =
    "#version 120\n"
    "SKIN\n"
    "attribute vec3 aPos; uniform mat4 uMVP; uniform mat4 uModel; varying vec3 vWorld;\n"
    "void main(){ vec3 P = skinPos(aPos); vWorld = (uModel * vec4(P, 1.0)).xyz; gl_Position = uMVP * vec4(P, 1.0); }\n";
const char* kSsaoDepthFrag =
    "#version 120\n"
    "uniform vec3 uEye; uniform float uFar; varying vec3 vWorld;\n"
    "void main(){\n"
    "  float d = clamp(length(uEye - vWorld) / uFar, 0.0, 1.0);\n"
    "  float hi = floor(d * 255.0) / 255.0;\n"                     // 16-bit pack in R+G
    "  gl_FragColor = vec4(hi, fract(d * 255.0), 0.0, 1.0);\n"
    "}\n";
const char* kSsaoFrag =
    "#version 120\n"
    "uniform sampler2D uDepth; uniform vec2 uTexel; uniform float uRadius; uniform float uFar;\n"
    "varying vec2 vUV;\n"
    "float depthAt(vec2 uv){ vec4 t = texture2D(uDepth, uv); return t.r + t.g / 255.0; }\n"
    "void main(){\n"
    "  float d = depthAt(vUV);\n"
    "  if (d >= 0.995) { gl_FragColor = vec4(1.0); return; }\n"    // background/sky
    "  float dist = max(d * uFar, 0.5);\n"
    "  float sr = clamp(uRadius / dist, 0.004, 0.12);\n"           // world radius -> screen UV (approx)
    "  float occ = 0.0;\n"
    "  float range = uRadius * 1.6;\n"
    "  for (int i = 0; i < 12; i++) {\n"
    "    float a = float(i) * 2.618034;\n"                          // golden-angle spiral
    "    float rr = sr * (0.25 + 0.75 * float(i) / 11.0);\n"
    "    vec2 o = vec2(cos(a), sin(a)) * rr;\n"
    "    o.x *= uTexel.x / uTexel.y;\n"                             // keep the disc round
    "    float ds = depthAt(vUV + o);\n"
    "    float diffW = (d - ds) * uFar;\n"                          // + = sample nearer (occluder)
    "    if (diffW > 0.03 && diffW < range) occ += 1.0 - diffW / range;\n"
    "  }\n"
    "  float ao = 1.0 - occ / 12.0;\n"
    "  gl_FragColor = vec4(ao, ao, ao, 1.0);\n"
    "}\n";
const char* kMulFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform float uStrength; varying vec2 vUV;\n"
    "void main(){\n"
    "  vec3 ao = texture2D(uSrc, vUV).rgb;\n"
    "  gl_FragColor = vec4(mix(vec3(1.0), ao, uStrength), 1.0);\n"  // blended DST_COLOR*src
    "}\n";
} // namespace

bool GLRenderer::EnsureSsaoProgs() {
    if (m_ssaoDepthProg && m_ssaoProg && m_mulProg) return true;
    auto build = [&](const char* vsrc, const char* fsrc, unsigned int& prog) -> bool {
        if (prog) return true;
        GLuint vs = compile(GL_VERTEX_SHADER, vsrc), fs = compile(GL_FRAGMENT_SHADER, fsrc);
        if (!vs || !fs) return false;
        prog = g.CreateProgram();
        g.AttachShader(prog, vs); g.AttachShader(prog, fs);
        g.BindAttribLocation(prog, 0, "aPos");
        g.BindAttribLocation(prog, 5, "aJoints");
        g.BindAttribLocation(prog, 6, "aWeights");
        g.LinkProgram(prog);
        GLint ok = 0; g.GetProgramiv(prog, GL_LINK_STATUS, &ok);
        g.DeleteShader(vs); g.DeleteShader(fs);
        if (!ok) { g.DeleteProgram(prog); prog = 0; return false; }
        return true;
    };
    if (!build(kSsaoDepthVert, kSsaoDepthFrag, m_ssaoDepthProg)) return false;
    if (!build(kQuadVert, kSsaoFrag, m_ssaoProg)) return false;
    if (!build(kQuadVert, kMulFrag, m_mulProg)) return false;
    m_usdMVP = g.GetUniformLocation(m_ssaoDepthProg, "uMVP");
    m_usdSkin = g.GetUniformLocation(m_ssaoDepthProg, "uSkin");
    m_usdBones = g.GetUniformLocation(m_ssaoDepthProg, "uBones");
    m_usdModel = g.GetUniformLocation(m_ssaoDepthProg, "uModel");
    m_usdEye = g.GetUniformLocation(m_ssaoDepthProg, "uEye");
    m_usdFar = g.GetUniformLocation(m_ssaoDepthProg, "uFar");
    m_usDepth = g.GetUniformLocation(m_ssaoProg, "uDepth");
    m_usTexel = g.GetUniformLocation(m_ssaoProg, "uTexel");
    m_usRadius = g.GetUniformLocation(m_ssaoProg, "uRadius");
    m_usFar = g.GetUniformLocation(m_ssaoProg, "uFar");
    m_umSrc = g.GetUniformLocation(m_mulProg, "uSrc");
    m_umStrength = g.GetUniformLocation(m_mulProg, "uStrength");
    return true;
}

bool GLRenderer::EnsureSsaoTargets(int w, int h) {
    if (m_ssaoDepthFbo && m_ssaoW == w && m_ssaoH == h) return true;
    DestroySsao();
    m_ssaoW = w; m_ssaoH = h;
    auto makeColorTarget = [&](unsigned int& fbo, unsigned int& tex, bool withDepth) -> bool {
        g.GenTextures(1, &tex);
        g.BindTexture(GL_TEXTURE_2D, tex);
        g.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g.GenFramebuffers(1, &fbo);
        g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (withDepth) {                                  // z-test the geometry pass
            g.GenRenderbuffers(1, &m_ssaoDepthRb);
            g.BindRenderbuffer(GL_RENDERBUFFER, m_ssaoDepthRb);
            g.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
            g.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_ssaoDepthRb);
        }
        bool ok = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        g.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return ok;
    };
    if (!makeColorTarget(m_ssaoDepthFbo, m_ssaoDepthTex, true) ||
        !makeColorTarget(m_ssaoFboA, m_ssaoTexA, false) ||
        !makeColorTarget(m_ssaoFboB, m_ssaoTexB, false)) {
        DestroySsao();
        return false;
    }
    return true;
}

void GLRenderer::DestroySsao() {
    if (m_ssaoDepthTex) { g.DeleteTextures(1, &m_ssaoDepthTex); m_ssaoDepthTex = 0; }
    if (m_ssaoTexA) { g.DeleteTextures(1, &m_ssaoTexA); m_ssaoTexA = 0; }
    if (m_ssaoTexB) { g.DeleteTextures(1, &m_ssaoTexB); m_ssaoTexB = 0; }
    if (m_ssaoDepthRb) { g.DeleteRenderbuffers(1, &m_ssaoDepthRb); m_ssaoDepthRb = 0; }
    if (m_ssaoDepthFbo) { g.DeleteFramebuffers(1, &m_ssaoDepthFbo); m_ssaoDepthFbo = 0; }
    if (m_ssaoFboA) { g.DeleteFramebuffers(1, &m_ssaoFboA); m_ssaoFboA = 0; }
    if (m_ssaoFboB) { g.DeleteFramebuffers(1, &m_ssaoFboB); m_ssaoFboB = 0; }
    m_ssaoW = m_ssaoH = 0;
}


// ---- Procedural sky + the scene's post chain ------------------------------------
// The sky is a fullscreen pass drawn before the meshes (depth off): every pixel
// unprojects to a world direction, so it follows the camera's rotation and field
// of view, and its colours come from the sun's height (SkyPaletteFor). The post
// chain runs when PostActive(): the scene resolves from a float target, then
// SSAO -> bloom (half + quarter res) -> exposure / tonemap / grading / vignette
// -> depth of field -> FXAA, ending in an RGBA8 texture the host reads back.
namespace {
const char* kSkyFrag =
    "#version 120\n"
    "uniform mat4 uInvVP; uniform vec3 uEye; uniform vec3 uSunDir;\n"
    "uniform vec3 uZenith; uniform vec3 uHorizon; uniform vec3 uGround; uniform vec3 uSunColor;\n"
    "uniform float uSunSize; uniform float uSunHalo; uniform float uSunDisc; uniform float uHaze; uniform float uNight;\n"
    "uniform float uStarDensity; uniform float uStarBright;\n"
    "uniform float uCloudCover; uniform float uCloudDensity; uniform float uCloudScale; uniform vec2 uCloudOffset;\n"
    "uniform float uTime; uniform float uLin;\n"
    // Colour mode (uMode 0): the screen-space gradient the host paints, drawn in-frame
    // while the post chain runs so grading / bloom / the vignette cover the sky too.
    "uniform float uMode; uniform vec3 uTop; uniform vec3 uHor; uniform vec3 uBot; uniform float uHorizonPos;\n"
    "uniform float uSunOn; uniform vec2 uSunPos; uniform float uSunRad; uniform float uStarsOn; uniform vec2 uViewSize;\n"
    "varying vec2 vUV;\n"
    "float hash21(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
    "float vnoise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0)), c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));\n"
    "  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y); }\n"
    "float fbm(vec2 p){ float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; } return s; }\n"
    "void main(){\n"
    "  if (uMode < 0.5) {\n"                                                       // Colour mode (see above)
    "    float t = 1.0 - vUV.y; float hp = clamp(uHorizonPos, 0.05, 0.95);\n"
    "    vec3 col = t < hp ? mix(uTop, uHor, t / hp) : mix(uHor, uBot, (t - hp) / (1.0 - hp));\n"
    "    float hz = 1.0 - abs(t - hp) / 0.10;\n"
    "    if (hz > 0.0) col = mix(col, min(uHor * vec3(1.15, 1.15, 1.10) + vec3(0.12, 0.12, 0.10), vec3(1.0)), hz * 0.5);\n"
    "    if (uStarsOn > 0.5 && t < hp) {\n"
    "      vec2 g = vUV * uViewSize / 22.0; vec2 cell = floor(g), cf = fract(g);\n"
    "      float h = hash21(cell); vec2 sp = vec2(hash21(cell + 3.1), hash21(cell + 9.7)) * 0.8 + 0.1;\n"
    "      col += vec3((h > 1.0 - 0.5 * uStarDensity ? 1.0 : 0.0) * smoothstep(0.12, 0.0, length(cf - sp)) * uStarBright);\n"
    "    }\n"
    "    if (uSunOn > 0.5) {\n"
    "      float dp = length((vec2(vUV.x, t) - uSunPos) * uViewSize);\n"
    "      float r = max(uSunRad, 0.005) * uViewSize.y; float glowR = 0.45 * uViewSize.y;\n"
    "      for (int i = 12; i >= 1; --i) { float fr = float(i) / 12.0; if (dp < glowR * fr) col = mix(col, uSunColor, (34.0 / 255.0) * (1.0 - fr) * (1.0 - fr)); }\n"
    "      for (int gi = 4; gi >= 1; --gi) { if (dp < r * (1.0 + float(gi) * 0.7)) col = mix(col, uSunColor, (40.0 - float(gi) * 6.0) / 255.0); }\n"
    "      if (dp < r) col = uSunColor;\n"
    "    }\n"
    "    if (uLin > 0.5) col = pow(max(col, vec3(0.0)), vec3(2.2));\n"
    "    gl_FragColor = vec4(col, 1.0); return;\n"
    "  }\n"
    "  vec2 ndc = vUV * 2.0 - 1.0;\n"
    "  vec4 p0 = uInvVP * vec4(ndc, -1.0, 1.0); vec4 p1 = uInvVP * vec4(ndc, 1.0, 1.0);\n"
    "  vec3 dir = normalize(p1.xyz / p1.w - p0.xyz / p0.w);\n"
    "  float y = dir.y;\n"
    "  vec3 col = (y >= 0.0) ? mix(uHorizon, uZenith, pow(y, 0.55)) : mix(uHorizon, uGround, smoothstep(0.0, 0.25, -y));\n"
    "  vec3 hazeCol = uHorizon * 1.12 + 0.06;\n"                                   // aerial haze brightens the horizon
    "  col = mix(col, hazeCol, uHaze * 0.6 * exp(-abs(y) * 5.0));\n"
    "  vec3 flatD = normalize(vec3(dir.x, 0.0, dir.z) + vec3(1e-5)); vec3 flatS = normalize(vec3(uSunDir.x, 0.0, uSunDir.z) + vec3(1e-5));\n"
    "  col += uSunColor * (0.14 * uHaze * uSunDisc * exp(-abs(y) * 4.0) * pow(max(dot(flatD, flatS), 0.0), 3.0));\n"   // the horizon warms toward the sun
    "  float ang = acos(clamp(dot(dir, uSunDir), -1.0, 1.0));\n"
    "  float disc = (1.0 - smoothstep(uSunSize * 0.85, uSunSize * 1.15, ang)) * uSunDisc;\n"
    "  float halo = uSunHalo * uSunDisc * (0.6 * exp(-ang * 4.0) + 0.3 * exp(-ang * 1.2));\n"
    "  col += uSunColor * (disc * 1.2 + halo);\n"
    "  if (uNight > 0.0 && y > 0.0) {\n"                                           // stars: a hashed cell grid over the dome
    "    float az = atan(dir.z, dir.x); float el = asin(clamp(y, -1.0, 1.0));\n"
    "    vec2 sc = vec2(az, el) * (64.0 * (0.5 + uStarDensity));\n"
    "    vec2 ci = floor(sc), cf = fract(sc);\n"
    "    float h = hash21(ci);\n"
    "    vec2 sp = vec2(hash21(ci + 7.3), hash21(ci + 13.7)) * 0.8 + 0.1;\n"
    "    float d = length(cf - sp);\n"
    "    float star = (h > 1.0 - 0.35 * uStarDensity ? 1.0 : 0.0) * smoothstep(0.09, 0.0, d);\n"
    "    float tw = 0.7 + 0.3 * sin(uTime * 3.0 + h * 40.0);\n"
    "    col += vec3(star * uStarBright * uNight * tw * smoothstep(0.0, 0.15, y));\n"
    "  }\n"
    "  if (uCloudCover > 0.0 && y > 0.01) {\n"                                     // a cloud layer on a plane above the camera
    "    vec2 uv = dir.xz / (y + 0.12) * (0.6 / max(uCloudScale, 0.05)) + uCloudOffset;\n"
    "    float n = fbm(uv);\n"
    "    float cov = smoothstep(1.0 - uCloudCover, 1.0 - uCloudCover * 0.6 + 0.05, n);\n"
    "    float lit = clamp(0.5 + (fbm(uv + flatS.xz * 0.06) - n) * 7.0, 0.0, 1.0);\n"  // thinner toward the sun = lit edge
    "    vec3 bright = mix(vec3(1.0), uSunColor, 0.35) * (0.95 - 0.8 * uNight);\n"
    "    vec3 shade = mix(uHorizon, uZenith, 0.5) * (0.8 - 0.3 * uNight);\n"
    "    vec3 cc = mix(shade, bright, lit);\n"
    "    float a = cov * uCloudDensity * smoothstep(0.0, 0.2, y);\n"
    "    col = mix(col, cc, a);\n"
    "  }\n"
    "  if (uLin > 0.5) col = pow(max(col, vec3(0.0)), vec3(2.2));\n"              // linear for the HDR chain
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

// Bright-pass + 2x2 box downsample (uThresh 0 = plain downsample).
const char* kDownFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform vec2 uTexel; uniform float uThresh; uniform float uRadius; varying vec2 vUV;\n"
    "vec3 tap(vec2 uv){ vec3 c = texture2D(uSrc, uv).rgb; float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "  float k = max(l - uThresh, 0.0) / max(l, 1e-4); return c * k; }\n"
    "void main(){ vec2 o = uTexel * uRadius;\n"
    "  vec3 s = tap(vUV + vec2(-o.x, -o.y)) + tap(vUV + vec2(o.x, -o.y)) + tap(vUV + vec2(-o.x, o.y)) + tap(vUV + vec2(o.x, o.y));\n"
    "  gl_FragColor = vec4(s * 0.25, 1.0); }\n";

// Exposure, bloom composite, tonemap, colour grading and vignette in one pass.
// The alpha trick: the host composites this image over its own sky, so a sky
// pixel (alpha 0) that bloom or the vignette touch is written as "colour at
// coverage" (black at alpha v darkens whatever is behind by 1 - v).
const char* kUberFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform sampler2D uBloomA; uniform sampler2D uBloomC;\n"
    "uniform float uExposure; uniform float uLinearIn; uniform float uTonemap;\n"
    "uniform float uBloomOn; uniform float uBloomIntensity;\n"
    "uniform float uGradeOn; uniform float uTemperature; uniform float uTint; uniform float uSaturation; uniform float uContrast;\n"
    "uniform vec3 uLift; uniform vec3 uGamma; uniform vec3 uGain;\n"
    "uniform float uVignette; uniform float uVignetteSmooth; uniform float uAspect;\n"
    "varying vec2 vUV;\n"
    "vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }\n"
    "vec3 hable(vec3 v){ float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;\n"
    "  return ((v*(A*v+C*B)+D*E)/(v*(A*v+B)+D*F))-E/F; }\n"
    "vec3 filmic(vec3 x){ return clamp(hable(x * 2.0) / hable(vec3(11.2)), 0.0, 1.0); }\n"
    "void main(){\n"
    "  vec4 s = texture2D(uSrc, vUV);\n"
    "  vec3 c = s.rgb * uExposure;\n"
    "  float blLum = 0.0;\n"
    "  if (uBloomOn > 0.5) {\n"
    "    vec3 bl = (texture2D(uBloomA, vUV).rgb + texture2D(uBloomC, vUV).rgb) * (0.5 * uBloomIntensity * uExposure);\n"
    "    c += bl; blLum = dot(bl, vec3(0.2126, 0.7152, 0.0722));\n"
    "  }\n"
    "  if (uLinearIn > 0.5) {\n"
    "    if (uTonemap > 1.5) c = filmic(c); else if (uTonemap > 0.5) c = aces(c); else c = clamp(c, 0.0, 1.0);\n"
    "    c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));\n"
    "  } else c = clamp(c, 0.0, 1.0);\n"
    "  if (uGradeOn > 0.5) {\n"
    "    c *= vec3(1.0 + 0.25 * uTemperature, 1.0 + 0.2 * uTint, 1.0 - 0.25 * uTemperature);\n"   // white balance
    "    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "    c = l + (c - l) * uSaturation;\n"
    "    c = (c - 0.5) * uContrast + 0.5;\n"
    "    c = c * (1.0 + uGain) + uLift * (1.0 - c);\n"                                           // lift the blacks, scale the whites
    "    c = pow(max(c, vec3(0.0)), vec3(1.0) / max(vec3(1.0) + uGamma * 0.75, vec3(0.1)));\n"   // mid-tone gamma
    "  }\n"
    "  float cov = clamp(s.a + blLum * 2.0, 0.0, 1.0);\n"
    "  float v = 0.0;\n"
    "  if (uVignette > 0.0) {\n"
    "    vec2 d = (vUV - 0.5) * vec2(uAspect, 1.0) * 2.0;\n"
    "    float r = length(d) / sqrt(uAspect * uAspect + 1.0);\n"
    "    v = 0.8 * uVignette * smoothstep(max(1.0 - uVignetteSmooth * 1.3, 0.0), 1.05, r);\n"
    "  }\n"
    "  vec3 pc = c * cov * (1.0 - v);\n"
    "  float oa = 1.0 - (1.0 - v) * (1.0 - cov);\n"
    "  gl_FragColor = vec4(oa > 1e-4 ? pc / oa : vec3(0.0), oa);\n"
    "}\n";

// FXAA: luma-driven edge blur along the gradient (the classic 3.x formulation).
const char* kFxaaFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform vec2 uTexel; varying vec2 vUV;\n"
    "float luma(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    "void main(){\n"
    "  vec4 c = texture2D(uSrc, vUV);\n"
    "  float lM = luma(c.rgb);\n"
    "  float lNW = luma(texture2D(uSrc, vUV + vec2(-1.0, -1.0) * uTexel).rgb);\n"
    "  float lNE = luma(texture2D(uSrc, vUV + vec2( 1.0, -1.0) * uTexel).rgb);\n"
    "  float lSW = luma(texture2D(uSrc, vUV + vec2(-1.0,  1.0) * uTexel).rgb);\n"
    "  float lSE = luma(texture2D(uSrc, vUV + vec2( 1.0,  1.0) * uTexel).rgb);\n"
    "  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n"
    "  float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n"
    "  if (lMax - lMin < max(0.0312, lMax * 0.125)) { gl_FragColor = c; return; }\n"
    "  vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));\n"
    "  float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * 0.125), 1.0 / 128.0);\n"
    "  float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);\n"
    "  dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * uTexel;\n"
    "  vec4 rgbA = 0.5 * (texture2D(uSrc, vUV + dir * (1.0 / 3.0 - 0.5)) + texture2D(uSrc, vUV + dir * (2.0 / 3.0 - 0.5)));\n"
    "  vec4 rgbB = rgbA * 0.5 + 0.25 * (texture2D(uSrc, vUV + dir * -0.5) + texture2D(uSrc, vUV + dir * 0.5));\n"
    "  float lB = luma(rgbB.rgb);\n"
    "  gl_FragColor = (lB < lMin || lB > lMax) ? rgbA : rgbB;\n"
    "}\n";

// Depth of field: blend toward the blurred frame by how far the pixel's linear
// depth (the SSAO depth pass) sits outside the focus range.
const char* kDofFrag =
    "#version 120\n"
    "uniform sampler2D uSrc; uniform sampler2D uBlur; uniform sampler2D uDepth;\n"
    "uniform float uFocus; uniform float uRange; uniform float uFar; varying vec2 vUV;\n"
    "void main(){\n"
    "  vec4 t = texture2D(uDepth, vUV); float dn = t.r + t.g / 255.0;\n"
    "  float d = dn >= 0.995 ? uFar * 4.0 : dn * uFar;\n"                           // background: far away
    "  float coc = clamp((abs(d - uFocus) - uRange * 0.5) / max(uRange, 1e-3), 0.0, 1.0);\n"
    "  gl_FragColor = mix(texture2D(uSrc, vUV), texture2D(uBlur, vUV), coc);\n"
    "}\n";

GLuint buildQuadProgram(const char* fragSrc) {
    GLuint vs = compile(GL_VERTEX_SHADER, kQuadVert), fs = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) { if (vs) g.DeleteShader(vs); if (fs) g.DeleteShader(fs); return 0; }
    GLuint prog = g.CreateProgram();
    g.AttachShader(prog, vs); g.AttachShader(prog, fs);
    g.BindAttribLocation(prog, 0, "aPos");
    g.LinkProgram(prog);
    GLint ok = 0; g.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    g.DeleteShader(vs); g.DeleteShader(fs);
    if (!ok) { char log[512]; g.GetProgramInfoLog(prog, 512, nullptr, log); Log::Error("[gl] post link: ", log); g.DeleteProgram(prog); return 0; }
    return prog;
}

// A colour render target of the given format; false (and nothing left behind) on failure.
bool makeColorTarget(unsigned int& fbo, unsigned int& tex, int w, int h, bool hdr) {
    g.GenTextures(1, &tex);
    g.BindTexture(GL_TEXTURE_2D, tex);
    g.TexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g.GenFramebuffers(1, &fbo);
    g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    const bool ok = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    g.BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { g.DeleteTextures(1, &tex); tex = 0; g.DeleteFramebuffers(1, &fbo); fbo = 0; }
    return ok;
}
} // namespace

bool GLRenderer::EnsureQuad() {
    if (m_quadVbo) return true;
    static const float quad[12] = {-1,-1,  1,-1,  1,1,  -1,-1,  1,1,  -1,1};
    g.GenBuffers(1, &m_quadVbo);
    g.BindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    g.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    return m_quadVbo != 0;
}

void GLRenderer::DrawQuad() {
    g.BindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    g.EnableVertexAttribArray(0);
    g.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void*)0);
    g.DisableVertexAttribArray(1); g.DisableVertexAttribArray(2);
    g.DisableVertexAttribArray(3); g.DisableVertexAttribArray(4);
    g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);
    g.DrawArrays(GL_TRIANGLES, 0, 6);
}

bool GLRenderer::EnsureSkyProgram() {
    if (m_skyProg) return true;
    m_skyProg = buildQuadProgram(kSkyFrag);
    if (!m_skyProg) return false;
    auto u = [&](const char* n) { return g.GetUniformLocation(m_skyProg, n); };
    m_uskInvVP = u("uInvVP"); m_uskEye = u("uEye"); m_uskSun = u("uSunDir");
    m_uskZenith = u("uZenith"); m_uskHorizon = u("uHorizon"); m_uskGround = u("uGround"); m_uskSunColor = u("uSunColor");
    m_uskSunSize = u("uSunSize"); m_uskSunHalo = u("uSunHalo"); m_uskSunDisc = u("uSunDisc"); m_uskHaze = u("uHaze");
    m_uskNight = u("uNight"); m_uskStarDensity = u("uStarDensity"); m_uskStarBright = u("uStarBright");
    m_uskCloudCover = u("uCloudCover"); m_uskCloudDensity = u("uCloudDensity"); m_uskCloudScale = u("uCloudScale");
    m_uskCloudOffset = u("uCloudOffset"); m_uskTime = u("uTime"); m_uskLin = u("uLin");
    m_uskMode = u("uMode"); m_uskTop = u("uTop"); m_uskHor = u("uHor"); m_uskBot = u("uBot"); m_uskHorizonPos = u("uHorizonPos");
    m_uskSunOn = u("uSunOn"); m_uskSunPos = u("uSunPos"); m_uskSunRad = u("uSunRad"); m_uskStarsOn = u("uStarsOn"); m_uskViewSize = u("uViewSize");
    return true;
}

bool GLRenderer::EnsurePostChain() {
    if (m_uberProg && m_fxaaProg && m_dofProg && m_downProg) return true;
    if (!EnsurePostProgs()) return false;   // the shared blur / add programs + the quad
    if (!m_uberProg) {
        m_uberProg = buildQuadProgram(kUberFrag);
        if (!m_uberProg) return false;
        auto u = [&](const char* n) { return g.GetUniformLocation(m_uberProg, n); };
        m_uuSrc = u("uSrc"); m_uuBloomA = u("uBloomA"); m_uuBloomC = u("uBloomC");
        m_uuExposure = u("uExposure"); m_uuLinearIn = u("uLinearIn"); m_uuTonemap = u("uTonemap");
        m_uuBloomOn = u("uBloomOn"); m_uuBloomIntensity = u("uBloomIntensity");
        m_uuGradeOn = u("uGradeOn"); m_uuTemperature = u("uTemperature"); m_uuTint = u("uTint");
        m_uuSaturation = u("uSaturation"); m_uuContrast = u("uContrast");
        m_uuLift = u("uLift"); m_uuGamma = u("uGamma"); m_uuGain = u("uGain");
        m_uuVignette = u("uVignette"); m_uuVignetteSmooth = u("uVignetteSmooth"); m_uuAspect = u("uAspect");
    }
    if (!m_fxaaProg) {
        m_fxaaProg = buildQuadProgram(kFxaaFrag);
        if (!m_fxaaProg) return false;
        m_ufSrc = g.GetUniformLocation(m_fxaaProg, "uSrc"); m_ufTexel = g.GetUniformLocation(m_fxaaProg, "uTexel");
    }
    if (!m_dofProg) {
        m_dofProg = buildQuadProgram(kDofFrag);
        if (!m_dofProg) return false;
        auto u = [&](const char* n) { return g.GetUniformLocation(m_dofProg, n); };
        m_udfSrc = u("uSrc"); m_udfBlur = u("uBlur"); m_udfDepth = u("uDepth");
        m_udfFocus = u("uFocus"); m_udfRange = u("uRange"); m_udfFar = u("uFar");
    }
    if (!m_downProg) {
        m_downProg = buildQuadProgram(kDownFrag);
        if (!m_downProg) return false;
        auto u = [&](const char* n) { return g.GetUniformLocation(m_downProg, n); };
        m_udnSrc = u("uSrc"); m_udnTexel = u("uTexel"); m_udnThresh = u("uThresh"); m_udnRadius = u("uRadius");
    }
    return true;
}

// The chain's own targets: a full-res RGBA8 output + ping-pong pair, and the
// bloom pairs (half res, reusing the bloom A/B targets, plus a quarter-res C/D)
// in the frame's format.
bool GLRenderer::EnsurePostTargets(int w, int h, bool hdr) {
    if (m_outFbo && m_postW == w && m_postH == h && m_postHdr == hdr) return true;
    DestroyPostTargets();
    DestroyBloom();
    m_postW = w; m_postH = h; m_postHdr = hdr;
    const int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;
    const int qw = w / 4 > 0 ? w / 4 : 1, qh = h / 4 > 0 ? h / 4 : 1;
    bool ok = makeColorTarget(m_outFbo, m_outTex, w, h, false) &&
              makeColorTarget(m_pingFbo[0], m_pingTex[0], w, h, false) &&
              makeColorTarget(m_pingFbo[1], m_pingTex[1], w, h, false) &&
              makeColorTarget(m_bloomFboA, m_bloomTexA, hw, hh, hdr) &&
              makeColorTarget(m_bloomFboB, m_bloomTexB, hw, hh, hdr) &&
              makeColorTarget(m_bloomFboC, m_bloomTexC, qw, qh, hdr) &&
              makeColorTarget(m_bloomFboD, m_bloomTexD, qw, qh, hdr);
    if (!ok) { DestroyPostTargets(); DestroyBloom(); return false; }
    m_bloomW = hw; m_bloomH = hh;
    return true;
}

void GLRenderer::DestroyPostTargets() {
    unsigned int* tex[] = {&m_outTex, &m_pingTex[0], &m_pingTex[1], &m_bloomTexC, &m_bloomTexD};
    unsigned int* fbo[] = {&m_outFbo, &m_pingFbo[0], &m_pingFbo[1], &m_bloomFboC, &m_bloomFboD};
    for (unsigned int* t : tex) if (*t) { g.DeleteTextures(1, t); *t = 0; }
    for (unsigned int* f : fbo) if (*f) { g.DeleteFramebuffers(1, f); *f = 0; }
    m_postW = m_postH = 0;
}

static std::uint64_t MeshKey(const SkinnedMesh* skin);   // defined with the mesh cache below

// Opaque geometry as packed linear depth at half resolution (the SSAO and DoF
// passes read it). Leaves the depth FBO bound and the depth test on.
void GLRenderer::DrawLinearDepth(const Scene& scene, const Mat4& vp, const Vec3& eye, const GameObject* ignore,
                                 const Frustum& viewFr) {
    g.BindFramebuffer(GL_FRAMEBUFFER, m_ssaoDepthFbo);
    g.Viewport(0, 0, m_ssaoW, m_ssaoH);
    g.ClearColor(1.0f, 1.0f, 0.0f, 1.0f);            // far plane = no occlusion
    g.Enable(GL_DEPTH_TEST); g.DepthFunc(GL_LEQUAL); g.DepthMask(GL_TRUE);
    g.Disable(GL_BLEND);
    g.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g.UseProgram(m_ssaoDepthProg);
    g.Uniform3f(m_usdEye, eye.x, eye.y, eye.z);
    g.Uniform1f(m_usdFar, kSsaoFar);
    g.DisableVertexAttribArray(1); g.DisableVertexAttribArray(2);
    g.DisableVertexAttribArray(3); g.DisableVertexAttribArray(4);
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || go->HiddenFromCamera(ignore)) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled) continue;
        if (mr->color.a < 0.999f) continue;           // transparents don't occlude
        const Mesh& mesh = mr->DisplayMesh();
        if (mesh.vertices.empty() || mesh.triangles.size() < 3) continue;
        if (CulledByFrustum(viewFr, go, mr)) continue;
        Mat4 model = go->transform->LocalToWorldMatrix();
        Mat4 mvp = vp * model;
        g.UniformMatrix4fv(m_usdMVP, 1, GL_FALSE, mvp.m);
        g.UniformMatrix4fv(m_usdModel, 1, GL_FALSE, model.m);
        const bool deformed = go->GetComponent<Character>() != nullptr;
        if (!deformed) {
            const auto* skin = go->GetComponent<SkinnedMesh>();
            MeshVB* mv = EnsureMeshVB(mesh, MeshKey(skin));
            if (!mv) continue;
            g.BindBuffer(GL_ARRAY_BUFFER, mv->vbo);
            g.EnableVertexAttribArray(0);
            g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (const void*)0);
            BindSkin(skin, mesh, mv, m_usdSkin, m_usdBones);
            g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)mv->vertCount);
            continue;
        }
        BindSkin(nullptr, mesh, nullptr, m_usdSkin, m_usdBones);
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        const int nv = (int)V.size();
        m_verts.clear(); m_verts.reserve(T.size() * 3);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], c = T[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
            const Vec3& pa = V[a]; const Vec3& pb = V[b]; const Vec3& pc = V[c];
            m_verts.push_back(pa.x); m_verts.push_back(pa.y); m_verts.push_back(pa.z);
            m_verts.push_back(pb.x); m_verts.push_back(pb.y); m_verts.push_back(pb.z);
            m_verts.push_back(pc.x); m_verts.push_back(pc.y); m_verts.push_back(pc.z);
        }
        if (m_verts.empty()) continue;
        g.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)), m_verts.data(), GL_DYNAMIC_DRAW);
        g.EnableVertexAttribArray(0);
        g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (const void*)0);
        g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(m_verts.size() / 3));
    }
    g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);
}

void GLRenderer::SwapSizeSet(SizeSet& s) {
    unsigned int* f[26] = { &m_fbo, &m_colorRb, &m_depthRb, &m_resolveFbo, &m_resolveTex,
                            &m_bloomFboA, &m_bloomTexA, &m_bloomFboB, &m_bloomTexB,
                            &m_ssaoDepthFbo, &m_ssaoDepthTex, &m_ssaoDepthRb,
                            &m_ssaoFboA, &m_ssaoTexA, &m_ssaoFboB, &m_ssaoTexB,
                            &m_outFbo, &m_outTex, &m_pingFbo[0], &m_pingTex[0], &m_pingFbo[1], &m_pingTex[1],
                            &m_bloomFboC, &m_bloomTexC, &m_bloomFboD, &m_bloomTexD };
    for (int i = 0; i < 26; ++i) std::swap(*f[i], s.ids[i]);
    std::swap(m_w, s.w); std::swap(m_h, s.h); std::swap(m_samples, s.samples);
    std::swap(m_bloomW, s.bloomW); std::swap(m_bloomH, s.bloomH);
    std::swap(m_ssaoW, s.ssaoW); std::swap(m_ssaoH, s.ssaoH);
    std::swap(m_postW, s.postW); std::swap(m_postH, s.postH);
    std::swap(m_hdr, s.hdr); std::swap(m_postHdr, s.postHdr);
}

void GLRenderer::DeleteSizeSet(SizeSet& s) {
    SwapSizeSet(s);                    // live <- s
    DestroyTargets(); DestroyBloom(); DestroySsao(); DestroyPostTargets();
    SwapSizeSet(s);                    // live back, s <- emptied
}

bool GLRenderer::EnsureTargets(int w, int h, int samples, bool hdr) {
    ++m_targetUse;
    if (hdr && m_hdrUnsupported) hdr = false;
    if (m_fbo && m_w == w && m_h == h && m_samples == samples && m_hdr == hdr) return true;
    // Park the live set, then swap in a parked one of this size if there is one.
    if (m_fbo) {
        SizeSet park;
        SwapSizeSet(park);
        park.lastUse = m_targetUse;
        m_parkedSets.push_back(park);
    }
    for (std::size_t i = 0; i < m_parkedSets.size(); ++i) {
        SizeSet& ps = m_parkedSets[i];
        if (ps.w != w || ps.h != h || ps.samples != samples || ps.hdr != hdr) continue;
        DestroyTargets(); DestroyBloom(); DestroySsao(); DestroyPostTargets();   // (already empty)
        SwapSizeSet(ps);
        m_parkedSets.erase(m_parkedSets.begin() + (std::ptrdiff_t)i);
        return true;
    }
    while (m_parkedSets.size() > 4) {   // a resized view leaves a trail of stale sizes
        std::size_t oldest = 0;
        for (std::size_t i = 1; i < m_parkedSets.size(); ++i)
            if (m_parkedSets[i].lastUse < m_parkedSets[oldest].lastUse) oldest = i;
        DeleteSizeSet(m_parkedSets[oldest]);
        m_parkedSets.erase(m_parkedSets.begin() + (std::ptrdiff_t)oldest);
    }
    DestroyTargets();   // size/sample/format change -> rebuild ONLY the render targets
    // The post chain renders into a float (RGBA16F) target so highlights above
    // 1.0 survive for bloom and the tonemap. A GPU that can't build one falls
    // back to RGBA8 for good (the chain then works on display-range colour).
    const bool wantHdr = hdr;
    for (int attempt = 0; attempt < 2; ++attempt) {
        hdr = wantHdr && attempt == 0;
        m_w = w; m_h = h; m_samples = samples < 1 ? 1 : samples; m_hdr = hdr;
        const GLenum fmt = hdr ? GL_RGBA16F : GL_RGBA8;
        while (g.GetError() != 0) {}
        g.GenFramebuffers(1, &m_fbo);
        g.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        g.GenRenderbuffers(1, &m_colorRb);
        g.BindRenderbuffer(GL_RENDERBUFFER, m_colorRb);
        g.RenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, fmt, w, h);
        g.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_colorRb);
        g.GenRenderbuffers(1, &m_depthRb);
        g.BindRenderbuffer(GL_RENDERBUFFER, m_depthRb);
        g.RenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_DEPTH_COMPONENT24, w, h);
        g.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRb);
        bool ok = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (ok) {
            g.GenFramebuffers(1, &m_resolveFbo);
            g.BindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
            g.GenTextures(1, &m_resolveTex);
            g.BindTexture(GL_TEXTURE_2D, m_resolveTex);
            g.TexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            g.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            g.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_resolveTex, 0);
            ok = g.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        if (ok && g.GetError() != 0) ok = false;
        g.BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (ok) return true;
        DestroyTargets();
        if (hdr) { m_hdrUnsupported = true; Log::Warning("[gl] float render target unavailable; post-processing runs on RGBA8"); continue; }
        Log::Error("[gl] MSAA / resolve FBO incomplete");
        return false;
    }
    return false;
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

// ---- Per-mesh vertex-buffer cache -------------------------------------------
// FNV-1a over raw bytes; `seed` chains multiple arrays into one hash.
static std::uint64_t FnvBytes(const void* data, std::size_t nbytes,
                              std::uint64_t seed = 1469598103934665603ull) {
    const unsigned char* p = (const unsigned char*)data;
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < nbytes; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
// Cheap per-frame change sniff: sizes + <=2048 sampled vertices/indices. Catches
// whole-mesh edits (skinning, inflate, import-over) instantly; a lone-vertex edit
// is caught by the periodic full hash below.
static std::uint64_t MeshSampleHash(const Mesh& m) {
    std::size_t sizes[5] = { m.vertices.size(), m.triangles.size(), m.normals.size(),
                             m.uvs.size(), m.triColors.size() };
    std::uint64_t h = FnvBytes(sizes, sizeof(sizes));
    const std::size_t vn = m.vertices.size(), step = vn / 2048 + 1;
    for (std::size_t i = 0; i < vn; i += step) h = FnvBytes(&m.vertices[i], sizeof(Vec3), h);
    const std::size_t tn = m.triangles.size(), tstep = tn / 2048 + 1;
    for (std::size_t i = 0; i < tn; i += tstep) h = FnvBytes(&m.triangles[i], sizeof(int), h);
    return h;
}
// Exact content hash over every array the expanded buffer derives from.
static std::uint64_t MeshFullHash(const Mesh& m) {
    std::uint64_t h = 1469598103934665603ull;
    if (!m.vertices.empty())  h = FnvBytes(m.vertices.data(),  m.vertices.size()  * sizeof(Vec3),  h);
    if (!m.normals.empty())   h = FnvBytes(m.normals.data(),   m.normals.size()   * sizeof(Vec3),  h);
    if (!m.uvs.empty())       h = FnvBytes(m.uvs.data(),       m.uvs.size()       * sizeof(Vec2),  h);
    if (!m.triangles.empty()) h = FnvBytes(m.triangles.data(), m.triangles.size() * sizeof(int),   h);
    if (!m.triColors.empty()) h = FnvBytes(m.triColors.data(), m.triColors.size() * sizeof(Color), h);
    return h;
}

// Expand indexed triangles into the interleaved non-indexed layout the shader
// consumes: pos(3)+normal(3)+uv(2)+color(3)+tangent(3) = 14 floats per vertex.
// Flat shading via the face normal when the mesh has none; box-projected UVs when
// it has no UVs; per-triangle face colors honored (matches the software renderer).
void GLRenderer::BuildExpandedVerts(const Mesh& mesh) {
    const auto& V = mesh.vertices; const auto& T = mesh.triangles;
    const bool hasN = mesh.HasNormals();
    const bool hasUV = !mesh.uvs.empty() && mesh.uvs.size() == V.size();
    const bool faceCols = mesh.HasFaceColors();
    const int nv = (int)V.size();
    m_verts.resize(T.size() / 3 * 3 * 14);   // written through a raw pointer, trimmed at the end
    float* out = m_verts.data();
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
            out[0] = p.x;   out[1] = p.y;   out[2] = p.z;
            out[3] = n.x;   out[4] = n.y;   out[5] = n.z;
            out[6] = uu[k]; out[7] = vv[k];
            out[8] = cr;    out[9] = cg;    out[10] = cb;
            out[11] = tan.x; out[12] = tan.y; out[13] = tan.z;
            out += 14;
        }
    }
    m_verts.resize((std::size_t)(out - m_verts.data()));
}

// Return the (up-to-date) cached GPU buffer for a mesh, expanding + uploading only
// when the mesh actually changed. nullptr = mesh has no drawable triangles.
GLRenderer::MeshVB* GLRenderer::EnsureMeshVB(const Mesh& mesh, std::uint64_t version) {
    MeshVB& e = m_meshVB[&mesh];
    e.lastUse = m_frame;
    bool same = e.vbo != 0 &&
        e.vp  == (const void*)mesh.vertices.data()  && e.vn  == mesh.vertices.size()  &&
        e.tp  == (const void*)mesh.triangles.data() && e.tn  == mesh.triangles.size() &&
        e.np  == (const void*)mesh.normals.data()   && e.nn  == mesh.normals.size()   &&
        e.uvp == (const void*)mesh.uvs.data()       && e.uvn == mesh.uvs.size()       &&
        e.fcp == (const void*)mesh.triColors.data() && e.fcn == mesh.triColors.size();
    std::uint64_t sh = 0;
    if (version) {   // skinned: the deformer says when it changed, no hashing needed
        if (same && e.version == version) return e.vertCount > 0 ? &e : nullptr;
        same = false;
    }
    if (same) { sh = MeshSampleHash(mesh); if (sh != e.sampleHash) same = false; }
    if (same && m_frame - e.lastFullHash >= 60) {   // exact check ~once a second
        std::uint64_t fh = MeshFullHash(mesh);
        e.lastFullHash = m_frame;
        if (fh != e.fullHash) same = false; else e.fullHash = fh;
    }
    if (same) return e.vertCount > 0 ? &e : nullptr;
    BuildExpandedVerts(mesh);
    e.vp  = mesh.vertices.data();  e.vn  = mesh.vertices.size();
    e.tp  = mesh.triangles.data(); e.tn  = mesh.triangles.size();
    e.np  = mesh.normals.data();   e.nn  = mesh.normals.size();
    e.uvp = mesh.uvs.data();       e.uvn = mesh.uvs.size();
    e.fcp = mesh.triColors.data(); e.fcn = mesh.triColors.size();
    e.version = version;
    if (!version) {
        e.sampleHash = sh ? sh : MeshSampleHash(mesh);
        e.fullHash = MeshFullHash(mesh);
        e.lastFullHash = m_frame;
    }
    e.vertCount = (int)(m_verts.size() / 14);
    ++e.buildSerial;
    if (e.vertCount == 0) return nullptr;
    if (!e.vbo) g.GenBuffers(1, &e.vbo);
    if (!e.vbo) { e.vertCount = 0; return nullptr; }
    g.BindBuffer(GL_ARRAY_BUFFER, e.vbo);
    g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)),
                 m_verts.data(), version ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    return &e;
}

bool GLRenderer::SupportsGpuSkinning() const {
    return g.ok && g.Uniform4fv && m_prog && m_uSkin >= 0 && m_uBones >= 0 && g_skinBones >= 32;
}
int GLRenderer::GpuSkinJoints() const { return g_skinBones; }

// Which version keys a mesh's cached buffer: a GPU-skinned mesh draws its bind
// pose (changes only with bindVersion); a CPU-skinned one changes every frame.
static std::uint64_t MeshKey(const SkinnedMesh* skin) {
    if (!skin) return 0;
    return skin->UsesGpuSkinning() ? skin->bindVersion : skin->version;
}

// Feed (or stop feeding) GPU skinning for the next draw in the current program:
// per-vertex joints/weights from a static buffer expanded like the mesh's, and
// the bone rows. Leaves GL_ARRAY_BUFFER bound to the skin buffer when skinning.
void GLRenderer::BindSkin(const SkinnedMesh* skin, const Mesh& mesh, MeshVB* mv, int uSkin, int uBones) {
    const bool on = skin && mv && skin->UsesGpuSkinning() && g.Uniform4fv && uBones >= 0 &&
                    (int)skin->palette.size() <= g_skinBones && !skin->palette.empty() &&
                    skin->jointIdx.size() == mesh.vertices.size() && skin->jointWt.size() == mesh.vertices.size();
    if (!on) {
        g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);
        if (uSkin >= 0) g.Uniform1f(uSkin, 0.0f);
        return;
    }
    // Joints + weights in the same expanded order as BuildExpandedVerts (8 floats per vertex).
    if (!mv->skinVbo || mv->skinFor != mv->vbo || mv->skinVer != mv->buildSerial) {
        const auto& T = mesh.triangles;
        const int nv = (int)mesh.vertices.size();
        std::vector<float> sv;
        sv.reserve((std::size_t)mv->vertCount * 8);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], c = T[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
            for (int v : {a, b, c}) {
                const auto& ji = skin->jointIdx[v]; const auto& jw = skin->jointWt[v];
                for (int k = 0; k < 4; ++k) sv.push_back((float)ji[k]);
                for (int k = 0; k < 4; ++k) sv.push_back(jw[k]);
            }
        }
        if (!mv->skinVbo) g.GenBuffers(1, &mv->skinVbo);
        g.BindBuffer(GL_ARRAY_BUFFER, mv->skinVbo);
        g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(sv.size() * sizeof(float)), sv.data(), GL_STATIC_DRAW);
        mv->skinFor = mv->vbo; mv->skinVer = mv->buildSerial;
    }
    g.BindBuffer(GL_ARRAY_BUFFER, mv->skinVbo);
    g.EnableVertexAttribArray(5);
    g.VertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void*)0);
    g.EnableVertexAttribArray(6);
    g.VertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void*)(4 * sizeof(float)));
    // Bone rows (Mat4 is column-major: row r = m[r], m[4+r], m[8+r], m[12+r]).
    m_boneRows.resize(skin->palette.size() * 12);
    float* o = m_boneRows.data();
    for (const Mat4& M : skin->palette)
        for (int r = 0; r < 3; ++r) { *o++ = M.m[r]; *o++ = M.m[4 + r]; *o++ = M.m[8 + r]; *o++ = M.m[12 + r]; }
    g.Uniform4fv(uBones, (GLsizei)(skin->palette.size() * 3), m_boneRows.data());
    g.Uniform1f(uSkin, 1.0f);
}

// Draw every shadow-casting mesh's depth with `lightVP` into the bound depth
// target (the caller sets the viewport to the tile). `local` = the spot/point
// light being rendered, so casters out of its range are skipped.
void GLRenderer::DrawShadowCasters(const Scene& scene, const Mat4& lightVP, const GameObject* ignore,
                                   const LightSample* local) {
    const Frustum lightFr = Frustum::FromVP(lightVP);   // only casters inside the light's view
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || go->firstPersonOnly || go->HiddenFromCamera(ignore)) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled || !mr->castShadows) continue;
        if (mr->color.a < 0.999f) continue;   // transparent meshes don't cast
        const Mesh& mesh = mr->DisplayMesh();
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        if (V.empty() || T.size() < 3) continue;
        if (CulledByFrustum(lightFr, go, mr)) continue;
        if (local) {
            Vec3 c; float r;
            if (ObjectWorldSphere(go, mr, c, r) && !CasterInLightRange(*local, c, r)) continue;
        }
        Mat4 dmvp = lightVP * go->transform->LocalToWorldMatrix();
        g.UniformMatrix4fv(m_uDepthMVP, 1, GL_FALSE, dmvp.m);
        // Static meshes: draw positions straight from the cached interleaved
        // buffer (stride 14). Per-frame-deformed meshes rebuild dynamically.
        // Skinned meshes are cached too, keyed on SkinnedMesh::version.
        const bool deformed = go->GetComponent<Character>() != nullptr;
        if (!deformed) {
            const auto* skin = go->GetComponent<SkinnedMesh>();
            MeshVB* mv = EnsureMeshVB(mesh, MeshKey(skin));
            if (!mv) continue;
            g.BindBuffer(GL_ARRAY_BUFFER, mv->vbo);
            g.EnableVertexAttribArray(0);
            g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (const void*)0);
            BindSkin(skin, mesh, mv, m_udSkin, m_udBones);
            g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)mv->vertCount);
            continue;
        }
        BindSkin(nullptr, mesh, nullptr, m_udSkin, m_udBones);
        const int nv = (int)V.size();
        m_verts.clear(); m_verts.reserve(T.size() * 3);
        for (std::size_t i = 0; i + 2 < T.size(); i += 3) {
            int a = T[i], b = T[i + 1], c = T[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) continue;
            const Vec3& pa = V[a]; const Vec3& pb = V[b]; const Vec3& pc = V[c];
            m_verts.push_back(pa.x); m_verts.push_back(pa.y); m_verts.push_back(pa.z);
            m_verts.push_back(pb.x); m_verts.push_back(pb.y); m_verts.push_back(pb.z);
            m_verts.push_back(pc.x); m_verts.push_back(pc.y); m_verts.push_back(pc.z);
        }
        if (m_verts.empty()) continue;
        g.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)), m_verts.data(), GL_DYNAMIC_DRAW);
        g.EnableVertexAttribArray(0);
        g.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (const void*)0);
        g.DrawArrays(GL_TRIANGLES, 0, (GLsizei)(m_verts.size() / 3));
    }
}

const std::uint32_t* GLRenderer::RenderToPixels(const Scene& scene, const Mat4& vp, const Vec3& eye,
                                                int w, int h, int samples,
                                                float clearR, float clearG, float clearB, float clearA,
                                                const GameObject* ignore) {
    if (!g.ok || w < 1 || h < 1) return nullptr;
    // The scene's post chain (bloom, grading, exposure, ...) wants a float target
    // and linear colour out of the mesh shader; with everything off the frame
    // takes the plain RGBA8 path exactly as before.
    const auto& rsTop = scene.renderSettings;
    const bool chain = PostActive(rsTop) && EnsurePostChain();
    const bool wantHdr = chain && rsTop.tonemap;
    if (!EnsureProgram() || !EnsureTargets(w, h, samples, wantHdr)) return nullptr;
    const bool hdrOut = chain && m_hdr;   // the mesh shader leaves its colour linear
    while (g.GetError() != 0) {}   // drain any stale GL error state before we render

    // Advance the mesh-VB cache clock and evict buffers unused for ~5s of frames
    // (deleted objects, replaced meshes), so GPU memory doesn't grow unbounded.
    ++m_frame;
    if ((m_frame % 300u) == 0u) {
        for (auto it = m_meshVB.begin(); it != m_meshVB.end();) {
            if (m_frame - it->second.lastUse > 300u) {
                if (it->second.vbo) g.DeleteBuffers(1, &it->second.vbo);
                if (it->second.skinVbo) g.DeleteBuffers(1, &it->second.skinVbo);
                it = m_meshVB.erase(it);
            } else ++it;
        }
    }

    // ---- Shadow-map depth pre-passes -------------------------------------------
    // Sun: one ortho cascade per tile of the sun atlas. Local lights: one
    // perspective tile per spot light, six per point/area light (cube faces).
    // PlanShadows (ShadowSetup.hpp) decides the cascades and tile assignment.
    ShadowPlan plan;
    bool shadowOn = false, localOn = false;
    if (ShadowsEnabled() && EnsureDepthProgram()) {
        GLint maxTex = 8192;
        if (g.GetIntegerv) g.GetIntegerv(0x0D33 /*GL_MAX_TEXTURE_SIZE*/, &maxTex);
        if (maxTex < 1024) maxTex = 1024;
        PlanShadows(scene, vp, eye, plan, maxTex < 8192 ? (int)maxTex : 8192);
        auto beginPass = [&](unsigned int fbo) {
            g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
            g.Enable(GL_DEPTH_TEST); g.DepthFunc(GL_LEQUAL); g.DepthMask(GL_TRUE);
            g.Disable(GL_BLEND);
            g.Clear(GL_DEPTH_BUFFER_BIT);
            g.UseProgram(m_depthProg);
            if (g.BindVertexArray && m_vao) g.BindVertexArray(m_vao);
            // Only attribute 0 (position) is fed in these passes; make sure no stale
            // arrays from a previous frame get fetched against the position buffer.
            g.DisableVertexAttribArray(1); g.DisableVertexAttribArray(2);
            g.DisableVertexAttribArray(3); g.DisableVertexAttribArray(4);
        };
        const int S = plan.cascadeSize;
        if (plan.cascades > 0 &&
            EnsureDepthTarget(m_shadowFbo, m_shadowTex, m_shadowW, m_shadowH, S * plan.cascades, S)) {
            beginPass(m_shadowFbo);
            for (int c = 0; c < plan.cascades; ++c) {
                g.Viewport(c * S, 0, S, S);
                DrawShadowCasters(scene, plan.cascade[c].vp, ignore, nullptr);
            }
            shadowOn = true;
        }
        const int T = plan.tileSize, G = ShadowPlan::kLocalGrid;
        if (!plan.local.empty() &&
            EnsureDepthTarget(m_localFbo, m_localTex, m_localW, m_localH, T * G, T * G)) {
            beginPass(m_localFbo);
            const auto& L = SceneLights::List();
            for (const LocalShadowGpu& ls : plan.local)
                for (int f = 0; f < ls.tiles; ++f) {
                    int t = ls.firstTile + f;
                    g.Viewport((t % G) * T, (t / G) * T, T, T);
                    DrawShadowCasters(scene, ls.vp[f], ignore, &L[ls.light]);
                }
            localOn = true;
        }
        g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);   // skin inputs off
    }

    g.BindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    g.Viewport(0, 0, w, h);
    g.ClearColor(clearR, clearG, clearB, clearA);
    g.Enable(GL_DEPTH_TEST);
    g.DepthFunc(GL_LEQUAL);
    g.Enable(GL_MULTISAMPLE);
    g.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (g.BindVertexArray && m_vao) g.BindVertexArray(m_vao);

    // The sky as a fullscreen pass behind everything (no depth): the Procedural
    // sky (coloured by the sun's height, with the disc, haze, stars and clouds),
    // or, while the post chain runs, the Colour gradient the host would paint,
    // so the chain's grading, bloom and vignette cover the sky as well.
    const bool procedural = rsTop.skyMode == Scene::RenderSettings::SkyProcedural;
    if (rsTop.skybox && SkyPassEnabled() && (procedural || chain) && EnsureSkyProgram() && EnsureQuad()) {
        const auto now = std::chrono::steady_clock::now();
        if (!m_clockSet) { m_clock0 = now; m_clockSet = true; }
        const float t = std::chrono::duration<float>(now - m_clock0).count();
        const float dt = t - m_skyTime; m_skyTime = t;
        const Vec3 wind = WindVector();
        // Clouds drift with the wind (and a little on their own), as a running offset
        // so changing the speed never makes them jump.
        m_cloudOffset.x += (0.012f * rsTop.cloudSpeed + wind.x * 0.004f) * dt;
        m_cloudOffset.y += (0.005f * rsTop.cloudSpeed + wind.z * 0.004f) * dt;
        const Vec3 sun = SunDirection();
        const SkyPalette pal = SkyPaletteFor(sun.y);
        const Mat4 inv = vp.Inverse();
        g.Disable(GL_DEPTH_TEST); g.DepthMask(GL_FALSE); g.Disable(GL_BLEND);
        g.UseProgram(m_skyProg);
        g.UniformMatrix4fv(m_uskInvVP, 1, GL_FALSE, inv.m);
        g.Uniform3f(m_uskEye, eye.x, eye.y, eye.z);
        g.Uniform3f(m_uskSun, sun.x, sun.y, sun.z);
        g.Uniform3f(m_uskZenith, pal.zenith.x, pal.zenith.y, pal.zenith.z);
        g.Uniform3f(m_uskHorizon, pal.horizon.x, pal.horizon.y, pal.horizon.z);
        g.Uniform3f(m_uskGround, pal.ground.x, pal.ground.y, pal.ground.z);
        g.Uniform3f(m_uskSunColor, pal.sun.x, pal.sun.y, pal.sun.z);
        g.Uniform1f(m_uskSunSize, rsTop.skySunDisc * 0.0174532925f);
        g.Uniform1f(m_uskSunHalo, rsTop.skySunHalo);
        g.Uniform1f(m_uskSunDisc, pal.sunDisc);
        g.Uniform1f(m_uskHaze, rsTop.skyHaze);
        g.Uniform1f(m_uskNight, pal.night);
        g.Uniform1f(m_uskStarDensity, rsTop.skyStarDensity);
        g.Uniform1f(m_uskStarBright, rsTop.skyStarBright);
        g.Uniform1f(m_uskCloudCover, rsTop.cloudCover);
        g.Uniform1f(m_uskCloudDensity, rsTop.cloudDensity);
        g.Uniform1f(m_uskCloudScale, rsTop.cloudScale);
        g.Uniform2f(m_uskCloudOffset, m_cloudOffset.x, m_cloudOffset.y);
        g.Uniform1f(m_uskTime, t);
        g.Uniform1f(m_uskLin, hdrOut ? 1.0f : 0.0f);
        g.Uniform1f(m_uskMode, procedural ? 1.0f : 0.0f);
        g.Uniform3f(m_uskTop, rsTop.skyTop.r, rsTop.skyTop.g, rsTop.skyTop.b);
        g.Uniform3f(m_uskHor, rsTop.skyHorizon.r, rsTop.skyHorizon.g, rsTop.skyHorizon.b);
        g.Uniform3f(m_uskBot, rsTop.skyBottom.r, rsTop.skyBottom.g, rsTop.skyBottom.b);
        g.Uniform1f(m_uskHorizonPos, rsTop.skyHorizonPos);
        g.Uniform1f(m_uskSunOn, rsTop.skySun ? 1.0f : 0.0f);
        g.Uniform2f(m_uskSunPos, rsTop.skySunX, rsTop.skySunY);
        g.Uniform1f(m_uskSunRad, rsTop.skySunSize);
        if (!procedural) g.Uniform3f(m_uskSunColor, rsTop.skySunColor.r, rsTop.skySunColor.g, rsTop.skySunColor.b);
        g.Uniform1f(m_uskStarsOn, rsTop.skyStars ? 1.0f : 0.0f);
        g.Uniform2f(m_uskViewSize, (float)w, (float)h);
        DrawQuad();
        g.Enable(GL_DEPTH_TEST); g.DepthMask(GL_TRUE);
    }

    g.UseProgram(m_prog);
    Vec3 toLight = (SceneLight::Direction() * -1.0f).Normalized();
    Vec3 lightTravel = SceneLight::Direction();   // direction light travels (for ground shadows)
    g.Uniform3f(m_uLightDir, toLight.x, toLight.y, toLight.z);
    g.Uniform1f(m_uShadowAlpha, -1.0f);           // normal (lit) draws; flipped on for the shadow pass
    g.Uniform1f(m_uAlpha, 1.0f);                   // opaque by default; set per material below
    g.Uniform3f(m_uLightColor, 0.85f, 0.85f, 0.85f);
    // Hemisphere/colored ambient from the scene's gathered lights (falls back to grey).
    Vec3 amb = SceneLights::AmbientColor();
    if (amb.x + amb.y + amb.z < 1e-4f) amb = Vec3{0.35f, 0.36f, 0.40f};
    g.Uniform3f(m_uAmbient, amb.x, amb.y, amb.z);
    {
        Vec3 aSky, aGround;
        SceneAmbientColors(scene.renderSettings, amb, aSky, aGround);   // the scene's Ambient Mode
        g.Uniform3f(m_uAmbSky, aSky.x, aSky.y, aSky.z);
        g.Uniform3f(m_uAmbGround, aGround.x, aGround.y, aGround.z);
    }
    g.Uniform3f(m_uEye, eye.x, eye.y, eye.z);
    // Sky gradient for environment reflections. Refresh the shared global from the
    // scene first (the software RenderMeshes does this too, but it may not run on the
    // GPU path), so reflective/metal materials see the right sky in the editor + game.
    {
        const auto& rs = scene.renderSettings;
        EnvSky().enabled = rs.skybox;
        EffectiveSkyColors(rs, EnvSky().top, EnvSky().horizon, EnvSky().bottom);   // gradient, or the sun's palette
    }
    const EnvSkyData& sky = EnvSky();
    g.Uniform3f(m_uSkyTop, sky.top.x, sky.top.y, sky.top.z);
    g.Uniform3f(m_uSkyHor, sky.horizon.x, sky.horizon.y, sky.horizon.z);
    g.Uniform3f(m_uSkyBot, sky.bottom.x, sky.bottom.y, sky.bottom.z);
    g.Uniform1f(m_uEnvOn, sky.enabled ? 1.0f : 0.0f);

    // Fog from the scene's render settings (the same maths as FogFactor / the software renderer).
    {
        const auto& rs = scene.renderSettings;
        const Vec3 fc = EffectiveFogColor(rs);
        g.Uniform1f(m_uFogOn, FogActive(rs) ? 1.0f : 0.0f);
        g.Uniform3f(m_uFogColor, fc.x, fc.y, fc.z);
        g.Uniform1f(m_uFogStart, rs.fogStart);
        g.Uniform1f(m_uFogEnd, rs.fogEnd);
        g.Uniform1f(m_uFogMode, (float)rs.fogMode);
        g.Uniform1f(m_uFogDensity, rs.fogDensity);
        g.Uniform1f(m_uFogHeight, rs.fogHeight);
        g.Uniform1f(m_uFogFalloff, rs.fogFalloff);
        g.Uniform1f(m_uTonemap, rs.tonemap ? 1.0f : 0.0f);
        g.Uniform1f(m_uHdrOut, hdrOut ? 1.0f : 0.0f);
    }

    // Shadow atlases: sun cascades on texture unit 4, local light tiles on unit 5.
    g.Uniform1f(m_uShadowOn, shadowOn ? 1.0f : 0.0f);
    g.Uniform1f(m_uShadowSoft, ShadowSoftness() < 0.0f ? 0.0f : ShadowSoftness());
    {
        const LightSample* sun = SceneLights::Sun();
        g.Uniform1f(m_uSunIndex, (float)plan.sunLight);
        g.Uniform1f(m_uSunStrength, sun ? sun->shadowStrength : 1.0f);
        g.Uniform1f(m_uSunBias, sun ? sun->shadowBias : 1.0f);
    }
    if (shadowOn) {
        float cvp[16 * ShadowPlan::kMaxCascades] = {0}, csph[4 * ShadowPlan::kMaxCascades] = {0};
        float ctx[ShadowPlan::kMaxCascades] = {0};
        for (int c = 0; c < plan.cascades; ++c) {
            std::memcpy(&cvp[c * 16], plan.cascade[c].vp.m, sizeof(float) * 16);
            csph[c * 4 + 0] = plan.cascade[c].center.x; csph[c * 4 + 1] = plan.cascade[c].center.y;
            csph[c * 4 + 2] = plan.cascade[c].center.z; csph[c * 4 + 3] = plan.cascade[c].radius;
            ctx[c] = plan.cascade[c].texelWorld;
        }
        g.UniformMatrix4fv(m_uCasVP, plan.cascades, GL_FALSE, cvp);
        if (g.Uniform4fv) g.Uniform4fv(m_uCasSphere, plan.cascades, csph);
        g.Uniform1fv(m_uCasTexel, plan.cascades, ctx);
        g.Uniform1f(m_uCasCount, (float)plan.cascades);
        g.Uniform1f(m_uCasSize, (float)plan.cascadeSize);
        g.ActiveTexture(GL_TEXTURE0 + 4);
        g.BindTexture(GL_TEXTURE_2D, m_shadowTex);
        g.ActiveTexture(GL_TEXTURE0);
    }
    {
        // Per light: x = first tile (-1 = no shadow), y = strength, z = bias, w = texel at 1m.
        float lsh[16 * 4];
        for (int i = 0; i < 16; ++i) { lsh[i * 4] = -1.0f; lsh[i * 4 + 1] = lsh[i * 4 + 2] = lsh[i * 4 + 3] = 0.0f; }
        float lvp[16 * ShadowPlan::kMaxLocalTiles] = {0};
        if (localOn) {
            const auto& L = SceneLights::List();
            for (const LocalShadowGpu& ls : plan.local) {
                if (ls.light < 0 || ls.light >= 16) continue;
                lsh[ls.light * 4 + 0] = (float)ls.firstTile;
                lsh[ls.light * 4 + 1] = L[ls.light].shadowStrength;
                lsh[ls.light * 4 + 2] = L[ls.light].shadowBias;
                lsh[ls.light * 4 + 3] = ls.texelAt1;
                for (int f = 0; f < ls.tiles; ++f)
                    std::memcpy(&lvp[(ls.firstTile + f) * 16], ls.vp[f].m, sizeof(float) * 16);
            }
            g.UniformMatrix4fv(m_uLocalVP, ShadowPlan::kMaxLocalTiles, GL_FALSE, lvp);
            g.Uniform1f(m_uLocalSize, (float)plan.tileSize);
            g.ActiveTexture(GL_TEXTURE0 + 5);
            g.BindTexture(GL_TEXTURE_2D, m_localTex);
            g.ActiveTexture(GL_TEXTURE0);
        }
        if (g.Uniform4fv) g.Uniform4fv(m_uLShadow, 16, lsh);
    }

    // Flat ground contact shadows stand in for real shadows only when the sun's
    // shadow map isn't drawn (Shadows off / unsupported) but the sun does cast.
    const LightSample* sunL = SceneLights::Sun();
    const bool sunCasts = !sunL || (sunL->castShadows && sunL->shadowStrength > 0.0f);
    const bool mapsWork = ShadowsEnabled() && m_depthProg && (plan.cascades == 0 || shadowOn);
    const bool planarShadows = sunCasts && !mapsWork;

    // Upload every gathered scene light (directional + point + spot) so all lights
    // shade on the GPU exactly like the software renderer — not just the sun.
    {
        const auto& L = SceneLights::List();
        int n = (int)L.size(); if (n > 16) n = 16;
        float lt[16] = {0}, lr[16] = {0}, lco[16] = {0}, lci[16] = {0}, lfo[16] = {0};
        float lp[48] = {0}, ld[48] = {0}, lc[48] = {0};
        for (int i = 0; i < n; ++i) {
            const LightSample& s = L[i];
            lt[i] = (float)s.type;
            lfo[i] = (float)s.falloff;
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
            if (m_uLFalloff >= 0) g.Uniform1fv(m_uLFalloff, n, lfo);
        }
    }

    // Skip what the camera can't see (main pass and the SSAO depth pass below).
    const Frustum viewFr = Frustum::FromVP(vp);
    int drawn = 0, culled = 0;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (!go || !go->active || go->HiddenFromCamera(ignore)) continue;
        if (!(RenderCullingMask() & (1 << (go->layer & 31)))) continue;   // camera layer cull
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || mr->wireframe || !mr->enabled) continue;
        const Mesh& mesh = mr->DisplayMesh();
        const auto& V = mesh.vertices; const auto& T = mesh.triangles;
        if (V.empty() || T.size() < 3) continue;
        if (CulledByFrustum(viewFr, go, mr)) { ++culled; continue; }
        ++drawn;
        unsigned int tex = TextureFor(mr->texture);

        // Geometry source: static meshes draw from the per-mesh cached GPU buffer
        // (expanded + uploaded once — see EnsureMeshVB); meshes deformed on the CPU
        // every frame by a Character rebuild + re-upload dynamically.
        // SkinnedMesh is cached as well: re-expanded only when its version moves
        // (once per animated frame, shared by every view and pass).
        const bool deformed = go->GetComponent<Character>() != nullptr;
        MeshVB* mvb = nullptr;
        const SkinnedMesh* gpuSkin = nullptr;
        if (!deformed) {
            const auto* skin = go->GetComponent<SkinnedMesh>();
            gpuSkin = skin;
            mvb = EnsureMeshVB(mesh, MeshKey(skin));
            if (!mvb) continue;                    // no drawable triangles
        } else {
            BuildExpandedVerts(mesh);
            if (m_verts.empty()) continue;
        }

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
        g.Uniform1f(m_uReceive, mr->receiveShadows ? 1.0f : 0.0f);
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

        // Gloss/specular map on unit 2 (per-texel specular + reflection scale).
        unsigned int stex = mr->specularMap.empty() ? 0u : TextureFor(mr->specularMap);
        if (stex) {
            g.ActiveTexture(GL_TEXTURE2);
            g.BindTexture(GL_TEXTURE_2D, stex);
            g.Uniform1i(m_uSpecTex, 2);
            g.Uniform1f(m_uHasSpecMap, 1.0f);
            g.ActiveTexture(GL_TEXTURE0);
        } else {
            g.Uniform1f(m_uHasSpecMap, 0.0f);
        }

        // Ambient-occlusion map on unit 3 (darken ambient + diffuse in creases).
        unsigned int aotex = mr->aoMap.empty() ? 0u : TextureFor(mr->aoMap);
        if (aotex) {
            g.ActiveTexture(GL_TEXTURE3);
            g.BindTexture(GL_TEXTURE_2D, aotex);
            g.Uniform1i(m_uAoTex, 3);
            g.Uniform1f(m_uHasAo, 1.0f);
            g.Uniform1f(m_uAoStrength, mr->aoStrength);
            g.ActiveTexture(GL_TEXTURE0);
        } else {
            g.Uniform1f(m_uHasAo, 0.0f);
        }

        // PBR scalars + texture offset (slide/atlas). Metalness/reflection tint by albedo.
        g.Uniform1f(m_uMetallic, unlit ? 0.0f : mr->metallic);
        g.Uniform1f(m_uReflectivity, unlit ? 0.0f : mr->reflectivity);
        g.Uniform2f(m_uTexOffset, mr->texOffset.x, mr->texOffset.y);

        GLsizei drawCount;
        if (mvb) {                                  // cached static buffer: just bind
            g.BindBuffer(GL_ARRAY_BUFFER, mvb->vbo);
            drawCount = (GLsizei)mvb->vertCount;
        } else {                                    // deformed: stream this frame's verts
            g.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
            g.BufferData(GL_ARRAY_BUFFER, (GLsizeiptrOK)(m_verts.size() * sizeof(float)), m_verts.data(), GL_DYNAMIC_DRAW);
            drawCount = (GLsizei)(m_verts.size() / 14);
        }
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
        BindSkin(mvb ? gpuSkin : nullptr, mesh, mvb, m_uSkin, m_uBones);
        // Transparency: a material alpha < 1 (water, glass) alpha-blends over the
        // scene and doesn't write depth, so geometry behind shows through. (Objects
        // draw in scene order — keep transparent meshes after opaque ones.)
        float matAlpha = mr->color.a;
        bool transparent = matAlpha < 0.999f;
        g.Uniform1f(m_uAlpha, matAlpha);
        if (transparent) {
            g.Enable(GL_BLEND);
            g.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            g.DepthMask(GL_FALSE);
        }
        g.DrawArrays(GL_TRIANGLES, 0, drawCount);
        if (transparent) {
            g.DepthMask(GL_TRUE);
            g.Disable(GL_BLEND);
            g.Uniform1f(m_uAlpha, 1.0f);
        }

        // Ground contact shadow: re-draw the SAME geometry flattened onto the ground
        // plane (a planar projection along the light) as a flat translucent black, so
        // the object reads as sitting on the ground instead of floating.
        // Only a fallback: with real shadow maps on, it would double-darken the
        // shadow and show through anything between the object and the plane.
        if (planarShadows && !go->firstPersonOnly && mr->groundShadow && mr->castShadows && mr->groundShadowStrength > 0.001f) {
            Mat4 shadowModel = Mat4::PlanarShadow(mr->groundShadowY + 0.01f, lightTravel) * model;
            Mat4 smvp = vp * shadowModel;
            g.UniformMatrix4fv(m_uMVP, 1, GL_FALSE, smvp.m);
            g.Uniform1f(m_uShadowAlpha, mr->groundShadowStrength > 1.0f ? 1.0f : mr->groundShadowStrength);
            g.Enable(GL_BLEND);
            g.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            // Every flattened triangle lies on the same plane: with a strict depth
            // test + depth writes, overlapping triangles (front and back faces,
            // other objects' shadows) are blended once instead of stacking darker.
            g.DepthFunc(GL_LESS);
            g.DrawArrays(GL_TRIANGLES, 0, drawCount);
            g.DepthFunc(GL_LEQUAL);
            g.Disable(GL_BLEND);
            g.Uniform1f(m_uShadowAlpha, -1.0f);
        }
    }

    LastCullCounters().drawn = drawn; LastCullCounters().culled = culled;
    g.DisableVertexAttribArray(5); g.DisableVertexAttribArray(6);   // skin inputs off for the post passes
    // Resolve MSAA into the single-sample texture.
    g.BindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    g.BindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    g.BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    const int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;
    // One fullscreen pass: program + source texture on unit 0 -> target FBO.
    auto quadPass = [&](unsigned int prog, unsigned int fbo, int vw, int vh, unsigned int srcTex, int uSrc) {
        g.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        g.Viewport(0, 0, vw, vh);
        g.UseProgram(prog);
        g.ActiveTexture(GL_TEXTURE0);
        g.BindTexture(GL_TEXTURE_2D, srcTex);
        if (uSrc >= 0) g.Uniform1i(uSrc, 0);
    };
    // AO estimate + one blur round from the linear-depth texture into m_ssaoTexA.
    auto computeAo = [&](float radius) {
        g.Disable(GL_DEPTH_TEST);
        quadPass(m_ssaoProg, m_ssaoFboA, m_ssaoW, m_ssaoH, m_ssaoDepthTex, m_usDepth);
        g.Uniform2f(m_usTexel, 1.0f / (float)m_ssaoW, 1.0f / (float)m_ssaoH);
        g.Uniform1f(m_usRadius, radius);
        g.Uniform1f(m_usFar, kSsaoFar);
        DrawQuad();
        quadPass(m_blurProg, m_ssaoFboB, m_ssaoW, m_ssaoH, m_ssaoTexA, m_ublSrc);
        g.Uniform2f(m_ublDir, 1.0f / (float)m_ssaoW, 0.0f);
        DrawQuad();
        quadPass(m_blurProg, m_ssaoFboA, m_ssaoW, m_ssaoH, m_ssaoTexB, m_ublSrc);
        g.Uniform2f(m_ublDir, 0.0f, 1.0f / (float)m_ssaoH);
        DrawQuad();
    };
    // Multiply the AO onto a target (dst *= src).
    auto multiplyAo = [&](unsigned int fbo, float strength) {
        quadPass(m_mulProg, fbo, w, h, m_ssaoTexA, m_umSrc);
        g.Uniform1f(m_umStrength, strength > 1.0f ? 1.0f : strength);
        g.Enable(GL_BLEND);
        g.BlendFunc(GL_DST_COLOR, GL_ZERO);
        DrawQuad();
        g.Disable(GL_BLEND);
    };

    const bool useChain = chain && EnsurePostTargets(w, h, m_hdr);
    PostVignetteHandled() = useChain && rsTop.vignette > 0.0f;
    unsigned int readFbo = m_resolveFbo;
    if (useChain) {
        const auto& ps = rsTop.post;
        g.Disable(GL_BLEND);
        // 1) Linear depth (half res) for SSAO and depth of field.
        const bool ssaoOn = ps.ssao || SSAOEnabled();
        bool haveDepth = false;
        if ((ssaoOn || ps.dof) && EnsureSsaoProgs() && EnsureSsaoTargets(hw, hh)) {
            DrawLinearDepth(scene, vp, eye, ignore, viewFr);
            haveDepth = true;
        }
        g.Disable(GL_DEPTH_TEST);
        if (ssaoOn && haveDepth) {
            computeAo(ps.ssao ? ps.ssaoRadius : SSAORadius());
            multiplyAo(m_resolveFbo, ps.ssao ? ps.ssaoIntensity : SSAOStrength());
        }
        // 2) Bloom: bright-pass to half res, blur, down to quarter, blur. Both
        //    levels are added in the uber pass (a wide soft glow + a tight one).
        const bool bloomOn = ps.bloom || BloomEnabled();
        const float bloomThresh = ps.bloom ? ps.bloomThreshold : BloomThreshold();
        const float bloomIntensity = ps.bloom ? ps.bloomIntensity : BloomIntensity();
        const float bloomRadius = ps.bloom ? ps.bloomRadius : 1.0f;
        const int qw = m_bloomW / 2 > 0 ? m_bloomW / 2 : 1, qh = m_bloomH / 2 > 0 ? m_bloomH / 2 : 1;
        if (bloomOn) {
            quadPass(m_downProg, m_bloomFboA, m_bloomW, m_bloomH, m_resolveTex, m_udnSrc);
            g.Uniform2f(m_udnTexel, 1.0f / (float)w, 1.0f / (float)h);
            g.Uniform1f(m_udnThresh, bloomThresh);
            g.Uniform1f(m_udnRadius, 1.0f);
            DrawQuad();
            for (int it = 0; it < 2; ++it) {
                quadPass(m_blurProg, m_bloomFboB, m_bloomW, m_bloomH, m_bloomTexA, m_ublSrc);
                g.Uniform2f(m_ublDir, bloomRadius / (float)m_bloomW, 0.0f);
                DrawQuad();
                quadPass(m_blurProg, m_bloomFboA, m_bloomW, m_bloomH, m_bloomTexB, m_ublSrc);
                g.Uniform2f(m_ublDir, 0.0f, bloomRadius / (float)m_bloomH);
                DrawQuad();
            }
            quadPass(m_downProg, m_bloomFboC, qw, qh, m_bloomTexA, m_udnSrc);
            g.Uniform2f(m_udnTexel, 1.0f / (float)m_bloomW, 1.0f / (float)m_bloomH);
            g.Uniform1f(m_udnThresh, 0.0f);
            g.Uniform1f(m_udnRadius, 1.0f);
            DrawQuad();
            for (int it = 0; it < 2; ++it) {
                quadPass(m_blurProg, m_bloomFboD, qw, qh, m_bloomTexC, m_ublSrc);
                g.Uniform2f(m_ublDir, bloomRadius * 1.5f / (float)qw, 0.0f);
                DrawQuad();
                quadPass(m_blurProg, m_bloomFboC, qw, qh, m_bloomTexD, m_ublSrc);
                g.Uniform2f(m_ublDir, 0.0f, bloomRadius * 1.5f / (float)qh);
                DrawQuad();
            }
        }
        // 3) Exposure, bloom composite, tonemap, grading, vignette.
        const bool dofOn = ps.dof && haveDepth;
        int after = (dofOn ? 1 : 0) + (ps.fxaa ? 1 : 0);
        int ping = 0;
        auto nextTarget = [&]() -> unsigned int {   // where the current pass writes
            if (after == 0) return m_outFbo;
            --after;
            return m_pingFbo[ping];
        };
        unsigned int cur = m_resolveTex;
        {
            unsigned int dst = nextTarget();
            quadPass(m_uberProg, dst, w, h, cur, m_uuSrc);
            g.ActiveTexture(GL_TEXTURE1); g.BindTexture(GL_TEXTURE_2D, m_bloomTexA); g.Uniform1i(m_uuBloomA, 1);
            g.ActiveTexture(GL_TEXTURE2); g.BindTexture(GL_TEXTURE_2D, m_bloomTexC); g.Uniform1i(m_uuBloomC, 2);
            g.ActiveTexture(GL_TEXTURE0);
            g.Uniform1f(m_uuExposure, ExposureScale(rsTop));
            g.Uniform1f(m_uuLinearIn, hdrOut ? 1.0f : 0.0f);
            g.Uniform1f(m_uuTonemap, (float)ps.tonemap);
            g.Uniform1f(m_uuBloomOn, bloomOn ? 1.0f : 0.0f);
            g.Uniform1f(m_uuBloomIntensity, bloomIntensity);
            g.Uniform1f(m_uuGradeOn, ps.grading ? 1.0f : 0.0f);
            g.Uniform1f(m_uuTemperature, ps.temperature);
            g.Uniform1f(m_uuTint, ps.tint);
            g.Uniform1f(m_uuSaturation, ps.saturation);
            g.Uniform1f(m_uuContrast, ps.contrast);
            g.Uniform3f(m_uuLift, ps.lift.x, ps.lift.y, ps.lift.z);
            g.Uniform3f(m_uuGamma, ps.gamma.x, ps.gamma.y, ps.gamma.z);
            g.Uniform3f(m_uuGain, ps.gain.x, ps.gain.y, ps.gain.z);
            g.Uniform1f(m_uuVignette, rsTop.vignette > 1.0f ? 1.0f : rsTop.vignette);
            g.Uniform1f(m_uuVignetteSmooth, ps.vignetteSmooth);
            g.Uniform1f(m_uuAspect, (float)w / (float)h);
            DrawQuad();
            cur = dst == m_outFbo ? m_outTex : m_pingTex[ping];
            if (dst != m_outFbo) ping ^= 1;
        }
        // 4) Depth of field: a blurred copy (half res, two blur rounds) mixed in
        //    by the distance from the focus plane.
        if (dofOn) {
            quadPass(m_addProg, m_bloomFboA, m_bloomW, m_bloomH, cur, m_uaSrc);
            g.Uniform1f(m_uaStrength, 1.0f);
            DrawQuad();
            for (int it = 0; it < 2; ++it) {
                quadPass(m_blurProg, m_bloomFboB, m_bloomW, m_bloomH, m_bloomTexA, m_ublSrc);
                g.Uniform2f(m_ublDir, 1.0f / (float)m_bloomW, 0.0f);
                DrawQuad();
                quadPass(m_blurProg, m_bloomFboA, m_bloomW, m_bloomH, m_bloomTexB, m_ublSrc);
                g.Uniform2f(m_ublDir, 0.0f, 1.0f / (float)m_bloomH);
                DrawQuad();
            }
            unsigned int dst = nextTarget();
            quadPass(m_dofProg, dst, w, h, cur, m_udfSrc);
            g.ActiveTexture(GL_TEXTURE1); g.BindTexture(GL_TEXTURE_2D, m_bloomTexA); g.Uniform1i(m_udfBlur, 1);
            g.ActiveTexture(GL_TEXTURE2); g.BindTexture(GL_TEXTURE_2D, m_ssaoDepthTex); g.Uniform1i(m_udfDepth, 2);
            g.ActiveTexture(GL_TEXTURE0);
            g.Uniform1f(m_udfFocus, ps.dofFocus);
            g.Uniform1f(m_udfRange, ps.dofRange);
            g.Uniform1f(m_udfFar, kSsaoFar);
            DrawQuad();
            cur = dst == m_outFbo ? m_outTex : m_pingTex[ping];
            if (dst != m_outFbo) ping ^= 1;
        }
        // 5) FXAA last, on the display image.
        if (ps.fxaa) {
            unsigned int dst = nextTarget();
            quadPass(m_fxaaProg, dst, w, h, cur, m_ufSrc);
            g.Uniform2f(m_ufTexel, 1.0f / (float)w, 1.0f / (float)h);
            DrawQuad();
        }
        g.Enable(GL_DEPTH_TEST);
        g.BindTexture(GL_TEXTURE_2D, 0);
        g.ClearColor(clearR, clearG, clearB, clearA);     // restore the caller's clear
        readFbo = m_outFbo;
    } else {
        // SSAO: darken corners/creases/contact points. Re-render opaque geometry at
        // half res as packed linear depth, estimate occlusion from depth differences,
        // blur once, multiply onto the frame. Runs BEFORE bloom (darken, then glow).
        if (SSAOEnabled() && EnsurePostProgs() && EnsureSsaoProgs() && EnsureSsaoTargets(hw, hh)) {
            DrawLinearDepth(scene, vp, eye, ignore, viewFr);
            computeAo(SSAORadius());
            multiplyAo(m_resolveFbo, SSAOStrength());
            g.Enable(GL_DEPTH_TEST);
            g.BindTexture(GL_TEXTURE_2D, 0);
            g.ClearColor(clearR, clearG, clearB, clearA);     // restore the caller's clear
        }

        // Bloom: bright-pass the resolved frame at half res, blur, add back on top.
        // Any failure just skips the effect (the frame is already complete without it).
        if (BloomEnabled() && EnsurePostProgs() && EnsureBloomTargets(hw, hh)) {
            g.Disable(GL_DEPTH_TEST);
            g.Disable(GL_BLEND);
            // 1) bright-pass: resolve texture -> half-res A
            quadPass(m_brightProg, m_bloomFboA, m_bloomW, m_bloomH, m_resolveTex, m_ubSrc);
            g.Uniform1f(m_ubThresh, BloomThreshold());
            DrawQuad();
            // 2) two separable gaussian rounds: A -> B (horizontal), B -> A (vertical)
            for (int it = 0; it < 2; ++it) {
                quadPass(m_blurProg, m_bloomFboB, m_bloomW, m_bloomH, m_bloomTexA, m_ublSrc);
                g.Uniform2f(m_ublDir, 1.0f / (float)m_bloomW, 0.0f);
                DrawQuad();
                quadPass(m_blurProg, m_bloomFboA, m_bloomW, m_bloomH, m_bloomTexB, m_ublSrc);
                g.Uniform2f(m_ublDir, 0.0f, 1.0f / (float)m_bloomH);
                DrawQuad();
            }
            // 3) additive composite onto the resolved frame
            quadPass(m_addProg, m_resolveFbo, w, h, m_bloomTexA, m_uaSrc);
            g.Uniform1f(m_uaStrength, BloomIntensity());
            g.Enable(GL_BLEND);
            g.BlendFunc(GL_ONE, GL_ONE);
            DrawQuad();
            g.Disable(GL_BLEND);
            g.Enable(GL_DEPTH_TEST);
            g.BindTexture(GL_TEXTURE_2D, 0);
        }
    }

    g.BindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
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
    for (SizeSet& ps : m_parkedSets) DeleteSizeSet(ps);
    m_parkedSets.clear();
    DestroyTargets();
    DestroyShadow();
    DestroyBloom();
    DestroySsao();
    DestroyPostTargets();
    for (auto& kv : m_texCache) if (kv.second) g.DeleteTextures(1, &kv.second);
    m_texCache.clear();
    for (auto& kv : m_meshVB) if (kv.second.vbo) g.DeleteBuffers(1, &kv.second.vbo);
    m_meshVB.clear();
    if (m_vbo) { g.DeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_quadVbo) { g.DeleteBuffers(1, &m_quadVbo); m_quadVbo = 0; }
    if (m_vao && g.DeleteVertexArrays) { g.DeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_depthProg) { g.DeleteProgram(m_depthProg); m_depthProg = 0; }
    if (m_brightProg) { g.DeleteProgram(m_brightProg); m_brightProg = 0; }
    if (m_blurProg) { g.DeleteProgram(m_blurProg); m_blurProg = 0; }
    if (m_addProg) { g.DeleteProgram(m_addProg); m_addProg = 0; }
    if (m_skyProg) { g.DeleteProgram(m_skyProg); m_skyProg = 0; }
    if (m_uberProg) { g.DeleteProgram(m_uberProg); m_uberProg = 0; }
    if (m_fxaaProg) { g.DeleteProgram(m_fxaaProg); m_fxaaProg = 0; }
    if (m_dofProg) { g.DeleteProgram(m_dofProg); m_dofProg = 0; }
    if (m_downProg) { g.DeleteProgram(m_downProg); m_downProg = 0; }
    if (m_ssaoDepthProg) { g.DeleteProgram(m_ssaoDepthProg); m_ssaoDepthProg = 0; }
    if (m_ssaoProg) { g.DeleteProgram(m_ssaoProg); m_ssaoProg = 0; }
    if (m_mulProg) { g.DeleteProgram(m_mulProg); m_mulProg = 0; }
    if (m_prog) { g.DeleteProgram(m_prog); m_prog = 0; }
}

} // namespace okay
