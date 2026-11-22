// OkayUI's own OpenGL renderer inside an SDL_Renderer that runs on OpenGL (see OkayUIRenderSDL.hpp for what and why).
//
// Written from SDL2's public render API documentation (SDL_render.h: SDL_RenderFlush for mixing your own graphics calls with the
// renderer, SDL_GetRendererInfo, SDL_RenderGetScale / GetLogicalSize / GetViewport, SDL_GetRenderTarget), SDL_video.h
// (SDL_GL_GetProcAddress, SDL_GL_GetCurrentContext) and the OpenGL / OpenGL ES specifications for the GL_VERSION string format
// ("<major>.<minor>[.<release>] <vendor text>" on desktop, "OpenGL ES <major>.<minor> <vendor text>" on ES). The drawing is
// OkayUI's own render::Renderer with its own OpenGL backend (OkayUIRender_GL.cpp).
#include "okay/UI/OkayUIRenderSDL.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"

#include <SDL.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace OkayUI {
namespace render {

namespace {

/// Reads "<major>.<minor>" at `s`; false when it is not there.
bool ParseVersion(const char* s, int* major, int* minor) {
    if (!s || !std::isdigit((unsigned char)*s)) return false;
    int a = 0, b = 0;
    while (std::isdigit((unsigned char)*s)) { a = a * 10 + (*s - '0'); if (a > 1000) return false; ++s; }
    if (*s != '.') return false;
    ++s;
    if (!std::isdigit((unsigned char)*s)) return false;
    while (std::isdigit((unsigned char)*s)) { b = b * 10 + (*s - '0'); if (b > 1000) return false; ++s; }
    *major = a; *minor = b;
    return true;
}

bool EqualNoCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
    return *a == *b;
}

void* GetProc(const char* name) { return SDL_GL_GetProcAddress(name); }

/// What the own renderer keeps for one SDL renderer: its GL backend and front end, made in that renderer's GL context.
struct PerRenderer {
    void* context = nullptr;                 // SDL_GLContext the objects belong to
    GLFlavor flavor = GLFlavor::Core33;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<Renderer> renderer;
    bool failed = false;                     // the backend could not be made in this context: the SDL path from now on
    const char* why = "";
};

/// The validator's question "can the renderer draw this texture key": the atlas, a fallback font's atlas, or an image it holds.
struct Resolve { const DrawData* d; const Renderer* r; };
bool Resolvable(const void* key, void* user) {
    const Resolve* rs = (const Resolve*)user;
    return key == rs->d->atlasTexture || FallbackFontOf(key) != nullptr || rs->r->HasImage(key);
}

} // namespace

bool OwnRenderSwitchValue(const char* v) {
    if (!v) return false;
    return EqualNoCase(v, "1") || EqualNoCase(v, "on") || EqualNoCase(v, "true") || EqualNoCase(v, "yes");
}
bool OwnRenderSwitchOn() { return OwnRenderSwitchValue(std::getenv("OKAY_UI_OWN_RENDER")); }

bool PickGLFlavor(const char* driver, const char* version, GLFlavor* flavor, const char** why) {
    const char* dummy = nullptr;
    if (!why) why = &dummy;
    if (!driver) { *why = "no SDL renderer"; return false; }
    int major = 0, minor = 0;
    if (std::strcmp(driver, "opengl") == 0) {
        if (!version || !ParseVersion(version, &major, &minor)) { *why = "the OpenGL version string could not be read"; return false; }
        if (major < 3 || (major == 3 && minor < 3)) { *why = "the OpenGL context is older than 3.3"; return false; }
        if (flavor) *flavor = GLFlavor::Core33;
        return true;
    }
    if (std::strcmp(driver, "opengles2") == 0) {
        static const char kEs[] = "OpenGL ES ";
        if (!version || std::strncmp(version, kEs, sizeof kEs - 1) != 0 || !ParseVersion(version + sizeof kEs - 1, &major, &minor)) {
            *why = "the OpenGL ES version string could not be read"; return false;
        }
        if (major < 3) { *why = "the OpenGL ES context is older than 3.0"; return false; }
        if (flavor) *flavor = GLFlavor::ES30;
        return true;
    }
    *why = "the SDL renderer is not an OpenGL one (Direct3D, Metal or software): the SDL path draws";
    return false;
}

struct SdlGLHost::Impl {
    HostFrame last;
    std::unordered_map<SDL_Renderer*, PerRenderer> per;

    /// Makes SDL's GL context for `r` current with SDL's queued drawing done. Creating a texture makes SDL activate the renderer's
    /// context (SDL's OpenGL renderers do that before every GL call they make); the texture is freed at once.
    static void* Activate(SDL_Renderer* r) {
        if (SDL_RenderFlush(r) != 0) return nullptr;
        SDL_Texture* probe = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
        if (!probe) return nullptr;
        SDL_DestroyTexture(probe);
        return SDL_GL_GetCurrentContext();
    }

    void Record(HostPath p, const char* why, GLFlavor f = GLFlavor::Core33) {
        last = HostFrame();
        last.path = p; last.reason = why; last.flavor = f;
    }

    /// Why this frame cannot go through the own renderer ("" = it can), from the renderer's settings alone.
    static const char* Blocked(SDL_Renderer* r) {
        if (SDL_GetRenderTarget(r)) return "a render target texture is set";
        int lw = 0, lh = 0; SDL_RenderGetLogicalSize(r, &lw, &lh);
        if (lw || lh) return "a logical size is set";
        SDL_Rect vp{ 0, 0, 0, 0 }; SDL_RenderGetViewport(r, &vp);
        if (vp.x || vp.y) return "the viewport is offset";
        float sx = 1, sy = 1; SDL_RenderGetScale(r, &sx, &sy);
        if (sx != sy) return "the x and y scales differ";
        if (!(sx >= kMinDpi && sx <= kMaxDpi)) return "the scale is outside the supported range";
        return "";
    }
};

SdlGLHost::SdlGLHost() : m(new Impl) {}

SdlGLHost::~SdlGLHost() {
    // No GL calls here: the contexts may already be gone at exit. Whatever was not Forgotten is left to the driver.
    for (auto& kv : m->per) { (void)kv.second.renderer.release(); (void)kv.second.backend.release(); }
}

const HostFrame& SdlGLHost::Last() const { return m->last; }
int SdlGLHost::Count() const { return (int)m->per.size(); }

const HostFrame& SdlGLHost::EndFrameSdl(SDL_Renderer* r) {
    OkayUI::EndFrame(r);
    m->Record(HostPath::Sdl, "the own renderer is off (OKAY_UI_OWN_RENDER)");
    return m->last;
}

const HostFrame& SdlGLHost::EndFrame(SDL_Renderer* r) {
    if (!r) { OkayUI::EndFrame(r); m->Record(HostPath::Sdl, "no renderer"); return m->last; }
    // ---- can the own renderer draw into this SDL renderer at all? ----
    SDL_RendererInfo info; std::memset(&info, 0, sizeof info);
    const char* why = "";
    if (SDL_GetRendererInfo(r, &info) != 0 || !info.name) why = "the SDL renderer could not be asked what it is";
    else if (std::strcmp(info.name, "opengl") != 0 && std::strcmp(info.name, "opengles2") != 0) PickGLFlavor(info.name, nullptr, nullptr, &why);
    if (!*why) why = Impl::Blocked(r);
    if (*why) { OkayUI::EndFrame(r); m->Record(HostPath::Sdl, why); return m->last; }

    void* ctx = Impl::Activate(r);
    if (!ctx) { OkayUI::EndFrame(r); m->Record(HostPath::Sdl, "SDL's GL context could not be made current"); return m->last; }

    PerRenderer& pr = m->per[r];
    if (pr.context && pr.context != ctx) {
        // The same renderer address with another context: a renderer was destroyed without Forget and a new one took its place. The old
        // objects belong to a context that is gone; drop them without GL calls and start again.
        (void)pr.renderer.release(); (void)pr.backend.release();
        pr = PerRenderer();
    }
    if (!pr.context) {
        pr.context = ctx;
        auto glString = (const unsigned char* (*)(unsigned))SDL_GL_GetProcAddress("glGetString");
        const char* version = glString ? (const char*)glString(0x1F02 /* GL_VERSION */) : nullptr;
        if (!PickGLFlavor(info.name, version, &pr.flavor, &pr.why)) pr.failed = true;
        else {
            std::string err;
            pr.backend = CreateGLBackend(&GetProc, pr.flavor, &err);
            if (!pr.backend) { pr.failed = true; pr.why = "the OpenGL backend could not be made (entry point or shader)"; }
            else pr.renderer.reset(new Renderer(*pr.backend));   // default options: linear atlas, as SDL's atlas texture is sampled
        }
    }
    if (pr.failed) { OkayUI::EndFrame(r); m->Record(HostPath::Sdl, pr.why); return m->last; }

    // ---- finish the frame the way EndFrame(SDL_Renderer*) does: toasts placed in the output size ----
    int fbW = 0, fbH = 0;
    SDL_GetRendererOutputSize(r, &fbW, &fbH);
    OkayUI::EndFrameData(fbW, fbH);
    const DrawData d = OkayUI::GetDrawData();
    float dpi = 1.0f, sy = 1.0f; SDL_RenderGetScale(r, &dpi, &sy);

    // ---- an SDL texture as a host image: only the SDL path can draw it, so the whole frame goes there (the order stays right) ----
    if (d.cmdCount > 0) {
        Resolve rs{ &d, pr.renderer.get() };
        ValidateOptions vo; vo.checkIndices = false; vo.checkVertices = false;   // only the texture keys are asked here; Render checks the rest
        vo.resolvable = &Resolvable; vo.resolvableUser = &rs;
        const Report rep = Validate(d, fbW > 0 ? fbW : 1, fbH > 0 ? fbH : 1, dpi, vo);
        if (rep.unresolvedSpans > 0) {
            OkayUI::DrawFinishedFrame(r);
            m->Record(HostPath::Sdl, "the frame shows a host image (an SDL texture) the own renderer does not hold", pr.flavor);
            return m->last;
        }
    }

    FrameInfo fi; fi.fbWidth = fbW; fi.fbHeight = fbH; fi.dpi = dpi;
    const Stats st = pr.renderer->Render(d, fi);
    m->Record(HostPath::OwnGL, st.refused ? "the frame was refused by the validator (nothing drawn)" : "", pr.flavor);
    m->last.stats = st;
    return m->last;
}

void SdlGLHost::Forget(SDL_Renderer* r) {
    auto it = m->per.find(r);
    if (it == m->per.end()) return;
    PerRenderer& pr = it->second;
    if (pr.renderer && pr.context && Impl::Activate(r) == pr.context) {
        pr.renderer->ReleaseAll();
        pr.renderer.reset();
        pr.backend.reset();                  // deletes the shader, the buffers, the vertex array and the white texel
    } else {
        (void)pr.renderer.release(); (void)pr.backend.release();   // not this renderer's context: no GL calls
    }
    m->per.erase(it);
}

} // namespace render
} // namespace OkayUI
