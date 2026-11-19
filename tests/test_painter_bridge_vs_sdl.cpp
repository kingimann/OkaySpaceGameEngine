// shared/PainterBridge.hpp against the raw SDL calls the editor and launcher used before it existed. Two identical SDL software renderers
// on surfaces: one is driven with the old call sequences, the other through the bridge; what they hold afterwards (texture properties,
// pixels, read-back bytes, driver name, draw colour, render target) must be equal. The sequences are the ones in the converted code:
// view / thumbnail textures (streaming or static, blended, linear), the sprite editor's nearest texture, FpHands' opaque texture, the
// render-target texture, texture updates by rectangle and pitch, clear, scale, clip and SDL_RenderGeometryRaw (the ImGui draw path).
#include "PainterBridge.hpp"

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace pb = okay::pb;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct Side {
    SDL_Surface* surf = nullptr;
    SDL_Renderer* ren = nullptr;
    Side(int w, int h) { surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888); ren = SDL_CreateSoftwareRenderer(surf); }
    ~Side() { pb::Forget(ren); SDL_DestroyRenderer(ren); SDL_FreeSurface(surf); }
    std::vector<unsigned char> Read() {
        std::vector<unsigned char> v((size_t)surf->w * surf->h * 4);
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ABGR8888, v.data(), surf->w * 4);
        return v;
    }
};

static std::vector<unsigned char> Pattern(int w, int h, int seed) {
    std::vector<unsigned char> v((size_t)w * h * 4);
    for (int i = 0; i < w * h; ++i) { v[i * 4] = (unsigned char)(i * 7 + seed); v[i * 4 + 1] = (unsigned char)(i * 3 + 2 * seed); v[i * 4 + 2] = (unsigned char)(255 - i); v[i * 4 + 3] = (unsigned char)(128 + (i * 5 + seed) % 127); }
    return v;
}

struct Props { Uint32 fmt = 0; int access = 0, w = 0, h = 0; SDL_BlendMode blend = SDL_BLENDMODE_INVALID; SDL_ScaleMode scale = SDL_ScaleModeNearest; };
static Props Get(SDL_Texture* t) {
    Props p;
    SDL_QueryTexture(t, &p.fmt, &p.access, &p.w, &p.h);
    SDL_GetTextureBlendMode(t, &p.blend);
    SDL_GetTextureScaleMode(t, &p.scale);
    return p;
}
static bool Same(const Props& a, const Props& b) { return a.fmt == b.fmt && a.access == b.access && a.w == b.w && a.h == b.h && a.blend == b.blend && a.scale == b.scale; }

int main() {
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0 || SDL_Init(SDL_INIT_EVENTS) == 0);
    const auto px = Pattern(8, 6, 1);
    struct Case { const char* name; SDL_TextureAccess acc; pb::Access pacc; bool blend; bool linear; bool initial; };
    const Case cases[] = {
        {"streaming blended linear (the 3D view)", SDL_TEXTUREACCESS_STREAMING, pb::Access::Streaming, true, true, false},
        {"static blended linear (thumbnails, covers)", SDL_TEXTUREACCESS_STATIC, pb::Access::Static, true, true, true},
        {"static blended nearest (sprite editor, launcher covers)", SDL_TEXTUREACCESS_STATIC, pb::Access::Static, true, false, true},
        {"static opaque nearest (FpHands)", SDL_TEXTUREACCESS_STATIC, pb::Access::Static, false, false, true},
        {"render target blended linear", SDL_TEXTUREACCESS_TARGET, pb::Access::Target, true, true, false},
    };
    for (const Case& c : cases) {
        std::printf("[case] %s\n", c.name);
        Side raw(32, 24), br(32, 24);
        // old sequence on `raw`
        SDL_Texture* t1 = SDL_CreateTexture(raw.ren, SDL_PIXELFORMAT_ABGR8888, c.acc, 8, 6);
        SDL_SetTextureBlendMode(t1, c.blend ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
        if (c.linear) SDL_SetTextureScaleMode(t1, SDL_ScaleModeLinear); else SDL_SetTextureScaleMode(t1, SDL_ScaleModeNearest);
        if (c.acc != SDL_TEXTUREACCESS_TARGET) SDL_UpdateTexture(t1, nullptr, px.data(), 8 * 4);
        // new on `br` (with the initial pixels in the call where the old code uploaded right after creating, else by UpdateTexture)
        SDL_Texture* t2 = pb::CreateTexture(br.ren, 8, 6, c.pacc, c.blend, c.linear, c.initial ? px.data() : nullptr);
        CHECK(t1 && t2);
        if (!c.initial && c.acc != SDL_TEXTUREACCESS_TARGET) CHECK(pb::UpdateTexture(br.ren, t2, nullptr, px.data(), 8 * 4));
        CHECK(Same(Get(t1), Get(t2)));
        int qw = -1, qh = -1; pb::QueryTexture(br.ren, t2, &qw, &qh);
        CHECK(qw == 8 && qh == 6);
        if (c.acc != SDL_TEXTUREACCESS_TARGET) {
            // update of a rectangle with its own pitch (the ImGui atlas path): old SDL_UpdateTexture(t, &rect, p, pitch)
            const auto patch = Pattern(3, 2, 9);
            SDL_Rect r = {2, 1, 3, 2};
            const int rr[4] = {2, 1, 3, 2};
            CHECK(SDL_UpdateTexture(t1, &r, patch.data(), 3 * 4) == 0);
            CHECK(pb::UpdateTexture(br.ren, t2, rr, patch.data(), 3 * 4));
            // draw both scaled up (nearest or linear as set) and compare the whole surface
            for (Side* s : {&raw, &br}) { SDL_SetRenderDrawColor(s->ren, 10, 20, 30, 255); SDL_RenderClear(s->ren); }
            SDL_Rect dst = {1, 1, 24, 18};
            SDL_RenderCopy(raw.ren, t1, nullptr, &dst);
            SDL_RenderCopy(br.ren, t2, nullptr, &dst);
            CHECK(raw.Read() == br.Read());
        }
        SDL_DestroyTexture(t1);
        pb::DestroyTexture(br.ren, t2);
    }
    {   // frame calls: clear with a colour, draw colour read-back, output size, driver, read pixels
        std::printf("[case] frame calls\n");
        Side raw(40, 30), br(40, 30);
        SDL_SetRenderDrawColor(raw.ren, 11, 22, 33, 255); SDL_RenderClear(raw.ren);
        pb::SetDrawColor(br.ren, 11, 22, 33, 255); pb::Clear(br.ren);
        CHECK(raw.Read() == br.Read());
        Uint8 r, g, b, a; SDL_GetRenderDrawColor(raw.ren, &r, &g, &b, &a);
        std::uint8_t r2 = 0, g2 = 0, b2 = 0, a2 = 0; pb::GetDrawColor(br.ren, &r2, &g2, &b2, &a2);
        CHECK(r == r2 && g == g2 && b == b2 && a == a2);
        int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
        SDL_GetRendererOutputSize(raw.ren, &w1, &h1); pb::OutputSize(br.ren, &w2, &h2);
        CHECK(w1 == w2 && h1 == h2 && w2 == 40 && h2 == 30);
        SDL_RendererInfo ri; SDL_GetRendererInfo(raw.ren, &ri);
        CHECK(pb::DriverName(br.ren) == (ri.name ? ri.name : "") && pb::IsAccelerated(br.ren) == ((ri.flags & SDL_RENDERER_ACCELERATED) != 0));
        std::vector<std::uint8_t> v; int vw = 0, vh = 0;
        CHECK(pb::ReadPixels(br.ren, v, vw, vh) && vw == 40 && vh == 30 && v.size() == raw.Read().size());
        CHECK(std::memcmp(v.data(), raw.Read().data(), v.size()) == 0);
        pb::Present(br.ren); SDL_RenderPresent(raw.ren);   // present on a software renderer has nothing to show, and must not change the pixels
        CHECK(raw.Read() == br.Read());
    }
    {   // scale, clip and SDL_RenderGeometryRaw: the editor's ImGui draw path (scale 2, a clip in the scaled space as the editor passed it)
        std::printf("[case] geometry with scale and clip\n");
        Side raw(64, 48), br(64, 48);
        const float xy[8] = {4, 4, 28, 4, 28, 20, 4, 20};
        const unsigned char col[16] = {255, 0, 0, 255, 0, 255, 0, 200, 0, 0, 255, 255, 255, 255, 0, 128};
        const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
        const unsigned short idx16[6] = {0, 1, 2, 0, 2, 3};
        const unsigned idx32[6] = {0, 1, 2, 0, 2, 3};
        for (int variant = 0; variant < 3; ++variant) {
            for (Side* s : {&raw, &br}) { SDL_SetRenderDrawColor(s->ren, 5, 6, 7, 255); SDL_RenderClear(s->ren); }
            SDL_Rect clip = {6, 5, 40, 30};
            okay::ui2d::Rect pclip = {6, 5, 40, 30};
            SDL_RenderSetScale(raw.ren, 2.0f, 2.0f); pb::SetScale(br.ren, 2.0f, 2.0f);
            SDL_RenderSetClipRect(raw.ren, &clip); pb::For(br.ren)->SetClipRect(&pclip);
            const void* ind = variant == 1 ? (const void*)idx32 : (const void*)idx16;
            const int isz = variant == 1 ? 4 : 2;
            const int n = variant == 2 ? 3 : 6;
            SDL_RenderGeometryRaw(raw.ren, nullptr, xy, 8, (const SDL_Color*)col, 4, uv, 8, 4, ind, n, isz);
            pb::For(br.ren)->GeometryRaw(nullptr, xy, 8, col, 4, uv, 8, 4, ind, n, isz);
            SDL_RenderSetClipRect(raw.ren, nullptr); pb::For(br.ren)->SetClipRect(nullptr);
            SDL_RenderSetScale(raw.ren, 1.0f, 1.0f); pb::SetScale(br.ren, 1.0f, 1.0f);
            CHECK(raw.Read() == br.Read());
        }
    }
    {   // render targets: draw into a target texture, back to the window, copy it: same pixels; the previous target is restored
        std::printf("[case] render target\n");
        Side raw(32, 32), br(32, 32);
        SDL_Texture* t1 = SDL_CreateTexture(raw.ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, 16, 16);
        SDL_SetTextureBlendMode(t1, SDL_BLENDMODE_BLEND);
        SDL_Texture* t2 = pb::CreateTexture(br.ren, 16, 16, pb::Access::Target, true, false);
        CHECK(t1 && t2 && pb::GetRenderTarget(br.ren) == SDL_GetRenderTarget(br.ren));
        SDL_Texture* prev1 = SDL_GetRenderTarget(raw.ren); SDL_Texture* prev2 = pb::GetRenderTarget(br.ren);
        CHECK(prev1 == nullptr && prev2 == nullptr);
        CHECK(SDL_SetRenderTarget(raw.ren, t1) == 0 && pb::SetRenderTarget(br.ren, t2));
        CHECK(pb::GetRenderTarget(br.ren) == t2);
        SDL_Rect rect = {2, 2, 8, 8};
        okay::ui2d::Rect prect = {2, 2, 8, 8};
        SDL_SetRenderDrawColor(raw.ren, 0, 0, 0, 0); SDL_RenderClear(raw.ren); SDL_SetRenderDrawColor(raw.ren, 200, 100, 50, 255); SDL_RenderFillRect(raw.ren, &rect);
        pb::SetDrawColor(br.ren, 0, 0, 0, 0); pb::Clear(br.ren); pb::SetDrawColor(br.ren, 200, 100, 50, 255); pb::For(br.ren)->FillRect(&prect);
        pb::Flush(br.ren); SDL_RenderFlush(raw.ren);
        CHECK(SDL_SetRenderTarget(raw.ren, prev1) == 0 && pb::SetRenderTarget(br.ren, prev2));
        CHECK(pb::GetRenderTarget(br.ren) == nullptr);
        for (Side* s : {&raw, &br}) { SDL_SetRenderDrawColor(s->ren, 1, 2, 3, 255); SDL_RenderClear(s->ren); }
        SDL_Rect dst = {4, 4, 16, 16};
        SDL_RenderCopy(raw.ren, t1, nullptr, &dst); SDL_RenderCopy(br.ren, t2, nullptr, &dst);
        CHECK(raw.Read() == br.Read());
        SDL_DestroyTexture(t1); pb::DestroyTexture(br.ren, t2);
    }
    {   // a renderer made by the bridge on an sdl2 window of the seam: accelerated, else any; and its destruction (needs a video driver: dummy)
        std::printf("[case] CreateRenderer on a window\n");
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
            okay::platform::Backend* be = okay::platform::Get(okay::platform::BackendKind::Sdl2);
            okay::platform::WindowDesc wd; wd.width = 64; wd.height = 48; wd.hidden = true;
            std::unique_ptr<okay::platform::Window> win = be ? be->MakeWindow(wd) : nullptr;
            if (win) {
                SDL_Renderer* r = pb::CreateRenderer(*win, false, nullptr, false);
                CHECK(r != nullptr);
                int w = 0, h = 0; pb::OutputSize(r, &w, &h);
                CHECK(w == 64 && h == 48);
                pb::SetDrawColor(r, 9, 9, 9, 255); pb::Clear(r); pb::Present(r);
                pb::DestroyRenderer(r);
                win.reset();
            } else std::printf("  (no window on the dummy video driver: skipped)\n");
        } else std::printf("  (no dummy video driver: skipped)\n");
    }
    std::printf("%d checks, %d failures\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
