#pragma once
// Private to the engine: the OpenGL renderer's loaded entry points (GL), its shader sources and the helpers the GLRenderer*.cpp
// files share. GLRenderer.cpp keeps the frame (RenderToPixels, the draw passes), GLRendererShaders.cpp the shader text and compile,
// GLRendererPasses.cpp the programs and post-chain targets, GLRendererResources.cpp the texture and mesh caches.
#include "okay/Render/GLRenderer.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#if defined(_WIN32)
// The Windows SDK's GL/gl.h (MSVC) needs WINGDIAPI/APIENTRY from windows.h first; MinGW's does not.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif
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
namespace gl_render {


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
typedef void   (*PFNCompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
typedef const GLubyte* (*PFNGetString)(GLenum);
typedef const GLubyte* (*PFNGetStringi)(GLenum, GLuint);
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
// Custom shaders: copy the frame's uniforms from the main program (GL 2.0 core).
typedef void   (*PFNGetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, char*);
typedef void   (*PFNGetUniformfv)(GLuint, GLint, GLfloat*);
typedef void   (*PFNGetUniformiv)(GLuint, GLint, GLint*);

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
    PFNGetActiveUniform GetActiveUniform = nullptr; PFNGetUniformfv GetUniformfv = nullptr; PFNGetUniformiv GetUniformiv = nullptr;
    // optional: block-compressed textures (.dds) and the extension list that says which formats the GPU takes
    PFNCompressedTexImage2D CompressedTexImage2D = nullptr; PFNGetString GetString = nullptr; PFNGetStringi GetStringi = nullptr;
    bool ok = false;
};
extern GL g;   // resolved entry points (defined in GLRendererShaders.cpp)

template <class F> bool load(F& fn, GLRenderer::GLGetProc gp, const char* name) {
    fn = reinterpret_cast<F>(gp(name));
    return fn != nullptr;
}

extern const char* kSkinGlsl;
extern const char* kVert;
extern const char* kFrag;
extern const char* kDepthVert;
extern const char* kDepthFrag;
extern const char* kQuadVert;
extern const char* kBrightFrag;
extern const char* kBlurFrag;
extern const char* kAddFrag;
extern const char* kSsaoDepthVert;
extern const char* kSsaoDepthFrag;
extern const char* kSsaoFrag;
extern const char* kMulFrag;
extern const char* kSkyFrag;
extern const char* kDownFrag;
extern const char* kUberFrag;
extern const char* kFxaaFrag;
extern const char* kDofFrag;
extern const char* kDofCopyFrag;
extern const char* kDofBlurFrag;
extern int g_skinBones;   // joints the skinning shaders take (set when the first shader is compiled)
GLuint compile(GLenum type, const char* src);
GLuint buildQuadProgram(const char* fragSrc);
bool makeColorTarget(unsigned int& fbo, unsigned int& tex, int w, int h, bool hdr);
std::uint64_t MeshKey(const SkinnedMesh* skin);   // defined with the mesh cache (GLRendererResources.cpp)
int GpuTextureCategory();
const float kSsaoFar = 150.0f;   // linear-depth range; AO fades out beyond this

} // namespace gl_render
} // namespace okay
