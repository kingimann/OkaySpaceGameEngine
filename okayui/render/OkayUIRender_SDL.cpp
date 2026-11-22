// OkayUI's own renderer inside an SDL_Renderer that runs on OpenGL or Direct3D 11 (see OkayUIRenderSDL.hpp for what and why).
//
// Written from SDL2's public API documentation: SDL_render.h (SDL_RenderFlush for mixing your own graphics calls with the renderer,
// SDL_GetRendererInfo, SDL_RenderGetScale / GetLogicalSize / GetViewport / GetClipRect, SDL_GetRenderTarget / SetRenderTarget,
// SDL_QueryTexture, SDL_GetTextureScaleMode, SDL_RenderCopy, SDL_GL_BindTexture / SDL_GL_UnbindTexture and their note that SDL may keep
// RGB textures as BGR on some renderers), SDL_video.h (SDL_GL_GetProcAddress, SDL_GL_GetCurrentContext), SDL_system.h
// (SDL_RenderGetD3D11Device: the renderer's device, with a reference the caller releases); the OpenGL / OpenGL ES specifications for the
// GL_VERSION string format ("<major>.<minor>[.<release>] <vendor text>" on desktop, "OpenGL ES <major>.<minor> <vendor text>" on ES)
// and GL_TEXTURE_BINDING_2D; and Microsoft's Direct3D 11 reference (ID3D11DeviceContext::OMGetRenderTargets, ID3D11View::GetResource,
// ID3D11Texture2D::GetDesc). The drawing is OkayUI's own render::Renderer with its own OpenGL and Direct3D 11 backends.
#include "okay/UI/OkayUIRenderSDL.hpp"
#include "okay/UI/OkayUIRenderBackends.hpp"

#include <SDL.h>
#if defined(_WIN32)
#include <SDL_system.h>   // SDL_RenderGetD3D11Device
#include <d3d11.h>
#endif

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

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

constexpr int kImageKeepFrames = 2;   // an image unused for this many host frames is let go of
constexpr int kMirrorMax = 8192;      // the Direct3D 11 backend's texture limit

/// One host image the own renderer holds for an SDL texture.
struct HostImage {
    int lastUsed = 0;                 // the host frame it was last drawn in
    SDL_Texture* mirror = nullptr;    // Direct3D 11: the render-target copy SDL draws the texture into (owned here)
    int w = 0, h = 0;
};

/// What the own renderer keeps for one SDL renderer: its backend and front end, made for that renderer's GL context or D3D11 device.
struct PerRenderer {
    HostPath kind = HostPath::OwnGL;
    void* context = nullptr;          // GL: the SDL_GLContext; Direct3D 11: the ID3D11Device* (identity only, no reference held)
    GLFlavor flavor = GLFlavor::Core33;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<Renderer> renderer;
    bool failed = false;              // the backend could not be made: the SDL path from now on
    const char* why = "";
    int frame = 0;                    // host frames drawn through this state
    std::unordered_map<const void*, HostImage> images;
    void Drop() {                     // no GPU calls: the objects belong to a context or device that is gone or not current
        (void)renderer.release(); (void)backend.release();
    }
};

/// The validator's question "can the renderer draw this texture key": the atlas, a fallback font's atlas, or an image it holds.
struct Resolve { const DrawData* d; const Renderer* r; };
bool Resolvable(const void* key, void* user) {
    const Resolve* rs = (const Resolve*)user;
    return key == rs->d->atlasTexture || FallbackFontOf(key) != nullptr || rs->r->HasImage(key);
}

/// The host image keys of a frame (SDL textures): every span texture that is not the glyph atlas or a fallback font, once each.
void HostImageKeys(const DrawData& d, std::vector<const void*>& out) {
    out.clear();
    for (int i = 0; i < d.cmdCount; ++i) {
        const void* k = d.cmds[i].texture;
        if (!k || k == d.atlasTexture || FallbackFontOf(k)) continue;
        bool seen = false;
        for (const void* o : out) if (o == k) { seen = true; break; }
        if (!seen) out.push_back(k);
    }
}

bool TextureIsLinear(SDL_Texture* t) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_ScaleMode m = SDL_ScaleModeNearest;
    if (SDL_GetTextureScaleMode(t, &m) == 0) return m != SDL_ScaleModeNearest;
#else
    (void)t;
#endif
    return false;
}

/// The renderer settings a host image copy or a target probe changes, put back afterwards.
struct SavedView {
    float sx = 1, sy = 1;
    SDL_bool clipOn = SDL_FALSE;
    SDL_Rect clip{ 0, 0, 0, 0 };
    void Save(SDL_Renderer* r) {
        SDL_RenderGetScale(r, &sx, &sy);
        clipOn = SDL_RenderIsClipEnabled(r);
        SDL_RenderGetClipRect(r, &clip);
    }
    void Restore(SDL_Renderer* r) const {
        float nx = 1, ny = 1; SDL_RenderGetScale(r, &nx, &ny);
        if (nx != sx || ny != sy) SDL_RenderSetScale(r, sx, sy);
        SDL_RenderSetClipRect(r, clipOn ? &clip : nullptr);
    }
};

#if defined(_WIN32)
template <class T> void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

/// The texture behind the render target view bound on the device's immediate context, AddRef'd (null when none). After SDL_RenderFlush
/// that is the target of SDL's last draw.
ID3D11Texture2D* BoundTarget(ID3D11Device* dev) {
    ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
    if (!ctx) return nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    Rel(ctx);
    if (!rtv) return nullptr;
    ID3D11Resource* res = nullptr; rtv->GetResource(&res); Rel(rtv);
    if (!res) return nullptr;
    ID3D11Texture2D* t = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t))) t = nullptr;
    Rel(res);
    return t;
}
#endif

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

HostPath HostBackendFor(const char* driver, const char** why) {
    const char* dummy = nullptr;
    if (!why) why = &dummy;
    if (!driver) { *why = "no SDL renderer"; return HostPath::Sdl; }
    if (std::strcmp(driver, "opengl") == 0 || std::strcmp(driver, "opengles2") == 0) { *why = ""; return HostPath::OwnGL; }
    if (std::strcmp(driver, "direct3d11") == 0) { *why = ""; return HostPath::OwnD3D11; }
    *why = "the SDL renderer is neither OpenGL nor Direct3D 11 (Direct3D 9, Direct3D 12, Metal or software): the SDL path draws";
    return HostPath::Sdl;
}

struct SdlHost::Impl {
    HostFrame last;
    std::unordered_map<SDL_Renderer*, PerRenderer> per;
    std::vector<const void*> keys;    // scratch

    /// Makes SDL's GL context for `r` current with SDL's queued drawing done. Creating a texture makes SDL activate the renderer's
    /// context (SDL's OpenGL renderers do that before every GL call they make); the texture is freed at once.
    static void* ActivateGL(SDL_Renderer* r) {
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

    /// The own renderer's state for `r`, made on first use or remade when SDL's context / device changed. `ctx` is the current GL
    /// context or the D3D11 device. Sets `pr.failed` and `pr.why` when no backend can be made.
    PerRenderer& StateFor(SDL_Renderer* r, HostPath kind, void* ctx, const char* driver) {
        PerRenderer& pr = per[r];
        if (pr.context && (pr.context != ctx || pr.kind != kind)) {
            // The same renderer address with another context or device: SDL remade its device (a device reset), or a renderer was
            // destroyed without Forget and a new one took its place. The old objects cannot be freed through this context; start again.
            // Image mirrors are not freed here: if the renderer was destroyed they went with it (the pointers are stale), and after a device
            // reset they are still SDL textures of `r` that SDL frees with it.
            pr.Drop();
            pr = PerRenderer();
        }
        if (pr.context) return pr;
        pr.context = ctx; pr.kind = kind;
        std::string err;
        if (kind == HostPath::OwnGL) {
            auto glString = (const unsigned char* (*)(unsigned))SDL_GL_GetProcAddress("glGetString");
            const char* version = glString ? (const char*)glString(0x1F02 /* GL_VERSION */) : nullptr;
            if (!PickGLFlavor(driver, version, &pr.flavor, &pr.why)) { pr.failed = true; return pr; }
            pr.backend = CreateGLBackend(&GetProc, pr.flavor, &err);
            if (!pr.backend) { pr.failed = true; pr.why = "the OpenGL backend could not be made (entry point or shader)"; return pr; }
        } else {
#if defined(_WIN32)
            ID3D11Device* dev = (ID3D11Device*)ctx;
            ID3D11DeviceContext* dc = nullptr; dev->GetImmediateContext(&dc);
            if (dc) pr.backend = CreateD3D11Backend(dev, dc, &err);
            Rel(dc);
            if (!pr.backend) { pr.failed = true; pr.why = "the Direct3D 11 backend could not be made (a shader did not compile)"; return pr; }
#else
            pr.failed = true; pr.why = "Direct3D 11 exists on Windows only"; return pr;
#endif
        }
        pr.renderer.reset(new Renderer(*pr.backend));   // default options: linear atlas, as SDL's atlas texture is sampled
        return pr;
    }

    /// OpenGL: the GL texture SDL keeps for `tex`, given to the renderer under `key` without copying. "" or why not.
    static const char* ShareGL(SDL_Renderer* r, PerRenderer& pr, const void* key) {
        SDL_Texture* tex = (SDL_Texture*)key;
        Uint32 fmt = 0; int access = 0, w = 0, h = 0;
        if (SDL_QueryTexture(tex, &fmt, &access, &w, &h) != 0) return "a host image is not an SDL texture";
        if (SDL_ISPIXELFORMAT_FOURCC(fmt)) return "a host image is a YUV texture (several GL textures): the SDL path draws it";
        if (pr.flavor == GLFlavor::ES30 && fmt != SDL_PIXELFORMAT_ABGR8888 && fmt != SDL_PIXELFORMAT_BGR888)
            return "a host image is kept by SDL's OpenGL ES renderer with its channels swapped (only ABGR / BGR images are shared there)";
        float tw = 0, th = 0;
        if (SDL_GL_BindTexture(tex, &tw, &th) != 0) return "a host image could not be bound (SDL_GL_BindTexture)";
        const bool ours = SDL_GL_GetCurrentContext() == pr.context;
        int name = 0;
        auto getInt = (void (*)(unsigned, int*))SDL_GL_GetProcAddress("glGetIntegerv");
        if (ours && getInt) getInt(0x8069 /* GL_TEXTURE_BINDING_2D */, &name);
        SDL_GL_UnbindTexture(tex);
        (void)r;
        if (!ours) return "a host image belongs to another SDL renderer";
        if (std::fabs(tw - 1.0f) > 1e-4f || std::fabs(th - 1.0f) > 1e-4f || name <= 0)
            return "a host image is a padded or rectangle GL texture (texture coordinates are not 0..1)";
        if (!pr.renderer->SetNativeImage(key, (std::uintptr_t)name, w, h, TextureIsLinear(tex))) return "a host image could not be wrapped";
        return "";
    }

#if defined(_WIN32)
    /// Direct3D 11: SDL copies `tex` into the mirror kept for it, and the mirror's ID3D11Texture2D is given to the renderer. "" or why not.
    static const char* ShareD3D11(SDL_Renderer* r, PerRenderer& pr, const void* key) {
        SDL_Texture* tex = (SDL_Texture*)key;
        Uint32 fmt = 0; int access = 0, w = 0, h = 0;
        if (SDL_QueryTexture(tex, &fmt, &access, &w, &h) != 0) return "a host image is not an SDL texture";
        if (w <= 0 || h <= 0 || w > kMirrorMax || h > kMirrorMax) return "a host image is larger than 8192 pixels";
        HostImage& im = pr.images[key];
        if (im.mirror && (im.w != w || im.h != h)) { SDL_DestroyTexture(im.mirror); im.mirror = nullptr; }
        if (!im.mirror) {
            im.mirror = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
            if (!im.mirror) return "a host image copy could not be made (SDL_CreateTexture)";
            SDL_SetTextureBlendMode(im.mirror, SDL_BLENDMODE_NONE);
            im.w = w; im.h = h;
        }
        // the copy: blending off and no colour or alpha modulation, so the mirror holds the texture's own pixels
        SDL_BlendMode bm = SDL_BLENDMODE_NONE; SDL_GetTextureBlendMode(tex, &bm);
        Uint8 cr = 255, cg = 255, cb = 255, ca = 255;
        SDL_GetTextureColorMod(tex, &cr, &cg, &cb); SDL_GetTextureAlphaMod(tex, &ca);
        SavedView view; view.Save(r);
        if (SDL_SetRenderTarget(r, im.mirror) != 0) return "a host image copy could not be drawn (SDL_SetRenderTarget)";
        SDL_RenderSetScale(r, 1.0f, 1.0f);
        SDL_RenderSetClipRect(r, nullptr);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
        SDL_SetTextureColorMod(tex, 255, 255, 255); SDL_SetTextureAlphaMod(tex, 255);
        const int copied = SDL_RenderCopy(r, tex, nullptr, nullptr);
        SDL_SetTextureBlendMode(tex, bm);
        SDL_SetTextureColorMod(tex, cr, cg, cb); SDL_SetTextureAlphaMod(tex, ca);
        ID3D11Texture2D* t = nullptr;
        if (copied == 0 && SDL_RenderFlush(r) == 0) t = BoundTarget((ID3D11Device*)pr.context);
        SDL_SetRenderTarget(r, nullptr);
        view.Restore(r);
        if (copied != 0) return "a host image could not be copied (not a texture of this SDL renderer?)";
        if (!t) return "a host image copy has no Direct3D 11 texture after the flush";
        D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof td); t->GetDesc(&td);
        const bool ok = (int)td.Width == w && (int)td.Height == h
                        && pr.renderer->SetNativeImage(key, (std::uintptr_t)t, w, h, TextureIsLinear(tex));
        Rel(t);
        return ok ? "" : "a host image copy could not be wrapped (size or bind flags)";
    }

    /// Direct3D 11: makes SDL bind its current render target (a transparent point, which changes no pixel under blending) and flush, then
    /// checks that the bound target is the renderer's output. "" or why not.
    static const char* BindD3D11Target(SDL_Renderer* r, PerRenderer& pr, int fbW, int fbH) {
        Uint8 dr = 0, dg = 0, db = 0, da = 0; SDL_GetRenderDrawColor(r, &dr, &dg, &db, &da);
        SDL_BlendMode bm = SDL_BLENDMODE_NONE; SDL_GetRenderDrawBlendMode(r, &bm);
        SavedView view; view.Save(r);
        SDL_RenderSetClipRect(r, nullptr);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        const int drew = SDL_RenderDrawPoint(r, 0, 0);
        SDL_SetRenderDrawColor(r, dr, dg, db, da);
        SDL_SetRenderDrawBlendMode(r, bm);
        view.Restore(r);
        if (drew != 0 || SDL_RenderFlush(r) != 0) return "SDL could not draw before the own renderer";
        ID3D11Texture2D* t = BoundTarget((ID3D11Device*)pr.context);
        if (!t) return "no Direct3D 11 render target is bound after SDL's flush";
        D3D11_TEXTURE2D_DESC td; std::memset(&td, 0, sizeof td); t->GetDesc(&td);
        Rel(t);
        if ((int)td.Width != fbW || (int)td.Height != fbH) return "the bound Direct3D 11 render target is not the renderer's output size";
        return "";
    }
#endif

    /// Lets go of the images not drawn in the last kImageKeepFrames frames (and their mirrors; `r` is alive).
    static void PruneImages(PerRenderer& pr) {
        for (auto it = pr.images.begin(); it != pr.images.end();) {
            if (pr.frame - it->second.lastUsed >= kImageKeepFrames) {
                pr.renderer->RemoveImage(it->first);
                if (it->second.mirror) SDL_DestroyTexture(it->second.mirror);
                it = pr.images.erase(it);
            } else ++it;
        }
    }
};

SdlHost::SdlHost() : m(new Impl) {}

SdlHost::~SdlHost() {
    // No GPU or SDL calls here: the contexts may already be gone at exit. Whatever was not Forgotten is left to the driver and SDL.
    for (auto& kv : m->per) kv.second.Drop();
}

const HostFrame& SdlHost::Last() const { return m->last; }
int SdlHost::Count() const { return (int)m->per.size(); }
int SdlHost::ImageCount() const {
    int n = 0;
    for (const auto& kv : m->per) n += (int)kv.second.images.size();
    return n;
}

const HostFrame& SdlHost::EndFrameSdl(SDL_Renderer* r) {
    OkayUI::EndFrame(r);
    m->Record(HostPath::Sdl, "the own renderer is off (OKAY_UI_OWN_RENDER)");
    return m->last;
}

const HostFrame& SdlHost::EndFrame(SDL_Renderer* r) {
    if (!r) { OkayUI::EndFrame(r); m->Record(HostPath::Sdl, "no renderer"); return m->last; }
    // ---- can the own renderer draw into this SDL renderer at all? ----
    SDL_RendererInfo info; std::memset(&info, 0, sizeof info);
    const char* why = "";
    HostPath kind = HostPath::Sdl;
    if (SDL_GetRendererInfo(r, &info) != 0 || !info.name) why = "the SDL renderer could not be asked what it is";
    else kind = HostBackendFor(info.name, &why);
    if (!*why) why = Impl::Blocked(r);
    if (*why) { OkayUI::EndFrame(r); m->Record(HostPath::Sdl, why); return m->last; }

    void* ctx = nullptr;
    if (kind == HostPath::OwnGL) {
        ctx = Impl::ActivateGL(r);
        if (!ctx) { OkayUI::EndFrame(r); m->Record(HostPath::Sdl, "SDL's GL context could not be made current"); return m->last; }
    } else {
#if defined(_WIN32)
        ID3D11Device* dev = SDL_RenderGetD3D11Device(r);   // a reference we release at once; the backend keeps its own
        ctx = dev;
        if (dev) dev->Release();
#endif
        if (!ctx) { OkayUI::EndFrame(r); m->Record(HostPath::Sdl, "SDL gave no Direct3D 11 device (SDL_RenderGetD3D11Device)"); return m->last; }
    }

    PerRenderer& pr = m->StateFor(r, kind, ctx, info.name);
    if (pr.failed) { OkayUI::EndFrame(r); m->Record(HostPath::Sdl, pr.why); return m->last; }
    ++pr.frame;

    // ---- finish the frame the way EndFrame(SDL_Renderer*) does: toasts placed in the output size ----
    int fbW = 0, fbH = 0;
    SDL_GetRendererOutputSize(r, &fbW, &fbH);
    OkayUI::EndFrameData(fbW, fbH);
    const DrawData d = OkayUI::GetDrawData();
    float dpi = 1.0f, sy = 1.0f; SDL_RenderGetScale(r, &dpi, &sy);

    // ---- host images: SDL textures shared with the own renderer (if one cannot be, the whole frame goes to SDL: the order stays right) ----
    const char* imageWhy = "";
    HostImageKeys(d, m->keys);
    for (const void* key : m->keys) {
#if defined(_WIN32)
        imageWhy = kind == HostPath::OwnGL ? Impl::ShareGL(r, pr, key) : Impl::ShareD3D11(r, pr, key);
#else
        imageWhy = Impl::ShareGL(r, pr, key);
#endif
        HostImage& im = pr.images[key];
        im.lastUsed = pr.frame;
        if (*imageWhy) break;
    }
    if (kind == HostPath::OwnGL && !m->keys.empty() && Impl::ActivateGL(r) != pr.context) imageWhy = "SDL's GL context could not be made current";
#if defined(_WIN32)
    if (!*imageWhy && kind == HostPath::OwnD3D11) imageWhy = Impl::BindD3D11Target(r, pr, fbW, fbH);
#endif
    if (*imageWhy) {
        OkayUI::DrawFinishedFrame(r);
        m->Record(HostPath::Sdl, imageWhy, pr.flavor);
        Impl::PruneImages(pr);
        return m->last;
    }

    // ---- every texture key must be one the renderer holds (a key that is none of the above: the SDL path) ----
    if (d.cmdCount > 0) {
        Resolve rs{ &d, pr.renderer.get() };
        ValidateOptions vo; vo.checkIndices = false; vo.checkVertices = false;   // only the texture keys are asked here; Render checks the rest
        vo.resolvable = &Resolvable; vo.resolvableUser = &rs;
        const Report rep = Validate(d, fbW > 0 ? fbW : 1, fbH > 0 ? fbH : 1, dpi, vo);
        if (rep.unresolvedSpans > 0) {
            OkayUI::DrawFinishedFrame(r);
            m->Record(HostPath::Sdl, "the frame shows a host image the own renderer does not hold", pr.flavor);
            Impl::PruneImages(pr);
            return m->last;
        }
    }

    FrameInfo fi; fi.fbWidth = fbW; fi.fbHeight = fbH; fi.dpi = dpi;
    const Stats st = pr.renderer->Render(d, fi);
    m->Record(kind, st.refused ? "the frame was refused by the validator (nothing drawn)" : "", pr.flavor);
    m->last.stats = st;
    m->last.images = (int)m->keys.size();
    Impl::PruneImages(pr);
    return m->last;
}

void SdlHost::Forget(SDL_Renderer* r) {
    auto it = m->per.find(r);
    if (it == m->per.end()) return;
    PerRenderer& pr = it->second;
    for (auto& kv : pr.images) if (kv.second.mirror) SDL_DestroyTexture(kv.second.mirror);   // `r` is still alive
    bool live = false;
    if (pr.renderer && pr.context) {
        if (pr.kind == HostPath::OwnGL) live = Impl::ActivateGL(r) == pr.context;
#if defined(_WIN32)
        else { ID3D11Device* dev = SDL_RenderGetD3D11Device(r); live = dev == (ID3D11Device*)pr.context; if (dev) dev->Release(); }
#endif
    }
    if (live) {
        pr.renderer->ReleaseAll();
        pr.renderer.reset();
        pr.backend.reset();                  // deletes the shaders, the buffers and the white texel (GL), or releases them (D3D11)
    } else {
        pr.Drop();                           // not this renderer's context: no GPU calls
    }
    m->per.erase(it);
}

} // namespace render
} // namespace OkayUI
