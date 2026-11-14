#pragma once
// A GL context for the render tests, made without a window: EGL (loaded with dlopen, so nothing links GL) on Mesa's
// surfaceless platform or a pbuffer, desktop OpenGL 3.3 core or OpenGL ES 3.0, and an RGBA8 framebuffer object to draw
// into. Returns false (and the test prints SKIPPED) where there is no EGL, no software or hardware driver, or no such
// context. On this machine's Mesa that is llvmpipe, a CPU rasteriser.
#include <dlfcn.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gltest {

typedef void* EGLDisplay_; typedef void* EGLContext_; typedef void* EGLSurface_; typedef void* EGLConfig_;
typedef unsigned EGLBoolean_; typedef int EGLint_; typedef unsigned EGLenum_;

struct Gl {
    // GL entry points the test itself needs
    unsigned (*GetError)() = nullptr;
    const unsigned char* (*GetString)(unsigned) = nullptr;
    void (*GenFramebuffers)(int, unsigned*) = nullptr;
    void (*BindFramebuffer)(unsigned, unsigned) = nullptr;
    void (*GenRenderbuffers)(int, unsigned*) = nullptr;
    void (*BindRenderbuffer)(unsigned, unsigned) = nullptr;
    void (*RenderbufferStorage)(unsigned, unsigned, int, int) = nullptr;
    void (*FramebufferRenderbuffer)(unsigned, unsigned, unsigned, unsigned) = nullptr;
    unsigned (*CheckFramebufferStatus)(unsigned) = nullptr;
    void (*ClearColor)(float, float, float, float) = nullptr;
    void (*Clear)(unsigned) = nullptr;
    void (*Finish)() = nullptr;
    void (*Enable)(unsigned) = nullptr;
    void (*Disable)(unsigned) = nullptr;
    unsigned char (*IsEnabled)(unsigned) = nullptr;
    void (*GetIntegerv)(unsigned, int*) = nullptr;
    void (*Viewport)(int, int, int, int) = nullptr;
    void (*Scissor)(int, int, int, int) = nullptr;
    void (*BlendFunc)(unsigned, unsigned) = nullptr;
    void (*DeleteFramebuffers)(int, const unsigned*) = nullptr;
    void (*DeleteRenderbuffers)(int, const unsigned*) = nullptr;
};

class Context {
public:
    ~Context() { Destroy(); }
    /// `es` = OpenGL ES 3.0, else OpenGL 3.3 core. On failure `why` says what was missing.
    bool Create(bool es, std::string* why) {
        m_egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!m_egl) { if (why) *why = "no libEGL.so.1"; return false; }
        m_gl = dlopen(es ? "libGLESv2.so.2" : "libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
        auto sym = [&](const char* n) { return dlsym(m_egl, n); };
        getProcAddress = (void* (*)(const char*))sym("eglGetProcAddress");
        auto eglGetDisplay = (EGLDisplay_ (*)(void*))sym("eglGetDisplay");
        auto eglInitialize = (EGLBoolean_ (*)(EGLDisplay_, EGLint_*, EGLint_*))sym("eglInitialize");
        auto eglBindAPI = (EGLBoolean_ (*)(EGLenum_))sym("eglBindAPI");
        auto eglChooseConfig = (EGLBoolean_ (*)(EGLDisplay_, const EGLint_*, EGLConfig_*, EGLint_, EGLint_*))sym("eglChooseConfig");
        auto eglCreateContext = (EGLContext_ (*)(EGLDisplay_, EGLConfig_, EGLContext_, const EGLint_*))sym("eglCreateContext");
        auto eglCreatePbufferSurface = (EGLSurface_ (*)(EGLDisplay_, EGLConfig_, const EGLint_*))sym("eglCreatePbufferSurface");
        auto eglMakeCurrent = (EGLBoolean_ (*)(EGLDisplay_, EGLSurface_, EGLSurface_, EGLContext_))sym("eglMakeCurrent");
        if (!getProcAddress || !eglGetDisplay || !eglInitialize || !eglBindAPI || !eglChooseConfig || !eglCreateContext || !eglMakeCurrent) { if (why) *why = "EGL entry points missing"; return false; }
        // Mesa's surfaceless platform needs no display server
        EGLDisplay_ dpy = nullptr;
        auto getPlatformDisplay = (EGLDisplay_ (*)(EGLenum_, void*, const EGLint_*))getProcAddress("eglGetPlatformDisplayEXT");
        if (getPlatformDisplay) dpy = getPlatformDisplay(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, nullptr, nullptr);
        if (!dpy) dpy = eglGetDisplay(nullptr);
        EGLint_ maj = 0, min = 0;
        if (!dpy || !eglInitialize(dpy, &maj, &min)) { if (why) *why = "eglInitialize failed (no driver or display)"; return false; }
        if (!eglBindAPI(es ? 0x30A0 /* OPENGL_ES_API */ : 0x30A2 /* OPENGL_API */)) { if (why) *why = "eglBindAPI failed"; return false; }
        const EGLint_ cfgAttr[] = { 0x3033 /* SURFACE_TYPE */, 0x0001 /* PBUFFER_BIT */, 0x3040 /* RENDERABLE_TYPE */, es ? 0x0040 /* ES3_BIT */ : 0x0008 /* OPENGL_BIT */,
                                    0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038 /* NONE */ };
        EGLConfig_ cfg = nullptr; EGLint_ n = 0;
        if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) { if (why) *why = "no EGL config"; return false; }
        const EGLint_ ctxAttr[] = { 0x3098 /* CONTEXT_MAJOR_VERSION */, 3, 0x30FB /* MINOR */, es ? 0 : 3, 0x30FD /* PROFILE_MASK */, es ? 0 : 1 /* CORE */, 0x3038 };
        const EGLint_ ctxAttrEs[] = { 0x3098, 3, 0x3038 };
        EGLContext_ ctx = eglCreateContext(dpy, cfg, nullptr, es ? ctxAttrEs : ctxAttr);
        if (!ctx) { if (why) *why = es ? "no OpenGL ES 3.0 context" : "no OpenGL 3.3 core context"; return false; }
        EGLSurface_ surf = nullptr;
        if (eglCreatePbufferSurface) { const EGLint_ pb[] = { 0x3057, 16, 0x3056, 16, 0x3038 }; surf = eglCreatePbufferSurface(dpy, cfg, pb); }
        if (!eglMakeCurrent(dpy, surf, surf, ctx) && !eglMakeCurrent(dpy, nullptr, nullptr, ctx)) { if (why) *why = "eglMakeCurrent failed"; return false; }
        m_dpy = dpy; m_ctx = ctx; m_surf = surf; m_es = es;
        eglMakeCurrentFn = eglMakeCurrent;
        if (!LoadGl(why)) return false;
        return true;
    }
    bool MakeFbo(int w, int h) {
        if (m_fbo) { g.DeleteFramebuffers(1, &m_fbo); g.DeleteRenderbuffers(1, &m_rbo); m_fbo = m_rbo = 0; }
        g.GenFramebuffers(1, &m_fbo); g.GenRenderbuffers(1, &m_rbo);
        g.BindRenderbuffer(0x8D41, m_rbo);
        g.RenderbufferStorage(0x8D41, 0x8058 /* RGBA8 */, w, h);
        g.BindFramebuffer(0x8D40, m_fbo);
        g.FramebufferRenderbuffer(0x8D40, 0x8CE0, 0x8D41, m_rbo);
        return g.CheckFramebufferStatus(0x8D40) == 0x8CD5;
    }
    void Clear(float r, float gr, float b, float a) { g.ClearColor(r, gr, b, a); g.Clear(0x4000); }
    std::string Info() const {
        std::string s;
        for (unsigned name : { 0x1F01u /* RENDERER */, 0x1F02u /* VERSION */ }) { const unsigned char* c = g.GetString(name); s += c ? (const char*)c : "?"; s += " | "; }
        return s;
    }
    void Destroy() {
        if (m_dpy && m_ctx) {
            auto eglDestroyContext = (EGLBoolean_ (*)(EGLDisplay_, EGLContext_))dlsym(m_egl, "eglDestroyContext");
            if (eglMakeCurrentFn) eglMakeCurrentFn(m_dpy, nullptr, nullptr, nullptr);
            if (eglDestroyContext) eglDestroyContext(m_dpy, m_ctx);
        }
        m_dpy = m_ctx = m_surf = nullptr; m_fbo = m_rbo = 0;
    }
    void* (*getProcAddress)(const char*) = nullptr;
    void* Proc(const char* n) {
        void* p = getProcAddress ? getProcAddress(n) : nullptr;
        if (!p && m_gl) p = dlsym(m_gl, n);
        return p;
    }
    Gl g;
    bool es() const { return m_es; }
    unsigned fbo() const { return m_fbo; }

private:
    bool LoadGl(std::string* why) {
#define L(m, n) g.m = (decltype(g.m))Proc(n); if (!g.m) { if (why) *why = std::string("missing ") + n; return false; }
        L(GetError, "glGetError"); L(GetString, "glGetString"); L(GenFramebuffers, "glGenFramebuffers"); L(BindFramebuffer, "glBindFramebuffer");
        L(GenRenderbuffers, "glGenRenderbuffers"); L(BindRenderbuffer, "glBindRenderbuffer"); L(RenderbufferStorage, "glRenderbufferStorage");
        L(FramebufferRenderbuffer, "glFramebufferRenderbuffer"); L(CheckFramebufferStatus, "glCheckFramebufferStatus"); L(ClearColor, "glClearColor");
        L(Clear, "glClear"); L(Finish, "glFinish"); L(Enable, "glEnable"); L(Disable, "glDisable"); L(IsEnabled, "glIsEnabled"); L(GetIntegerv, "glGetIntegerv");
        L(Viewport, "glViewport"); L(Scissor, "glScissor"); L(BlendFunc, "glBlendFunc"); L(DeleteFramebuffers, "glDeleteFramebuffers"); L(DeleteRenderbuffers, "glDeleteRenderbuffers");
#undef L
        return true;
    }
    void* m_egl = nullptr; void* m_gl = nullptr;
    EGLDisplay_ m_dpy = nullptr; EGLContext_ m_ctx = nullptr; EGLSurface_ m_surf = nullptr;
    EGLBoolean_ (*eglMakeCurrentFn)(EGLDisplay_, EGLSurface_, EGLSurface_, EGLContext_) = nullptr;
    unsigned m_fbo = 0, m_rbo = 0;
    bool m_es = false;
};

} // namespace gltest
