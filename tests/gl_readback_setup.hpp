#pragma once
// Setup shared by the GL texture tests that read textures back (test_gl_dds_upload, test_gl_texture_asset): make a
// real GL context (Mesa's llvmpipe through EGL, no display needed), load the renderer's GL functions from it and look
// up the few read-back entry points the tests call directly. Where no context or no glGetTexImage can be had, Open()
// prints SKIPPED and returns false, and the test passes.
#include "test_framework.hpp"
#include "../okayui/tests/RenderGLContext.hpp"

#include "okay/Render/GLRenderer.hpp"

#include <iostream>
#include <string>

namespace glread {

inline gltest::Context* g_ctx = nullptr;
inline void* Proc(const char* n) { return g_ctx ? g_ctx->Proc(n) : nullptr; }

inline void (*glBindTexture_)(unsigned, unsigned) = nullptr;
inline void (*glGetTexImage_)(unsigned, int, unsigned, unsigned, void*) = nullptr;
inline void (*glGetTexLevelParameteriv_)(unsigned, int, unsigned, int*) = nullptr;
inline void (*glGetTexParameterfv_)(unsigned, unsigned, float*) = nullptr;
inline void (*glPixelStorei_)(unsigned, int) = nullptr;
inline unsigned (*glGetError_)() = nullptr;

// Make `ctx`, print what it is, load GL through it and find the read-back functions. False (after printing SKIPPED)
// when the machine cannot make a context or the context has no glGetTexImage.
inline bool Open(gltest::Context& ctx) {
    std::string why;
    if (!ctx.Create(false, &why)) { std::cout << "  SKIPPED: " << why << "\n"; return false; }
    g_ctx = &ctx;
    std::cout << "  context: " << ctx.Info() << "\n";
    CHECK(okay::GLRenderer::LoadGL(&Proc));
    glBindTexture_ = (decltype(glBindTexture_))Proc("glBindTexture");
    glGetTexImage_ = (decltype(glGetTexImage_))Proc("glGetTexImage");
    glGetTexLevelParameteriv_ = (decltype(glGetTexLevelParameteriv_))Proc("glGetTexLevelParameteriv");
    glGetTexParameterfv_ = (decltype(glGetTexParameterfv_))Proc("glGetTexParameterfv");
    glPixelStorei_ = (decltype(glPixelStorei_))Proc("glPixelStorei");
    glGetError_ = (decltype(glGetError_))Proc("glGetError");
    if (!glBindTexture_ || !glGetTexImage_ || !glGetTexLevelParameteriv_ || !glPixelStorei_) {
        std::cout << "  SKIPPED: no glGetTexImage\n";
        return false;
    }
    return true;
}

} // namespace glread
