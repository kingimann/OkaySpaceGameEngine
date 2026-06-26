// Headless test for OkayUI. Because OkayUI renders through SDL_Renderer, we can
// drive it with a SOFTWARE renderer on an in-memory surface — no window, no GPU,
// no display — and read the pixels back to prove the button actually rasterized.
// We also exercise the press/release click logic and the ImGui-capture gate.
//
// Run with "--png <path>" to additionally dump a visual preview (normal / hover /
// pressed states) as a PNG.
#include "okay/UI/OkayUI.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Render/Color.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static Uint32 pixelAt(SDL_Surface* s, int x, int y) {
    const Uint32* px = static_cast<const Uint32*>(s->pixels);
    return px[y * (s->pitch / 4) + x];
}

static void savePng(SDL_Surface* s, const char* path) {
    okay::Image img(s->w, s->h);
    SDL_LockSurface(s);
    for (int y = 0; y < s->h; ++y)
        for (int x = 0; x < s->w; ++x) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixelAt(s, x, y), s->format, &r, &g, &b, &a);
            img.SetPixel(x, y, okay::Color::FromBytes(r, g, b, a));
        }
    SDL_UnlockSurface(s);
    std::printf(img.SavePNG(path) ? "wrote %s\n" : "failed to write %s\n", path);
}

int main(int argc, char** argv) {
    const int W = 240, H = 90;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* r = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    CHECK(surf != nullptr);
    CHECK(r != nullptr);
    if (!surf || !r) { std::printf("okayui: SDL software renderer unavailable\n"); return 1; }

    const float bx = 40, by = 25, bw = 160, bh = 40;
    auto clearBlack = [&]{ SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r); };

    // --- Frame 1: idle (mouse away, not pressed). Button should draw, not click. ---
    clearBlack();
    OkayUI::Input in;  // all zero / false
    OkayUI::BeginFrame(in);
    bool idle = OkayUI::Button(1, bx, by, bw, bh, "Play");
    OkayUI::EndFrame(r);
    CHECK(!idle);
    // The button center must be non-black -> geometry actually rasterized.
    SDL_LockSurface(surf);
    Uint32 center = pixelAt(surf, (int)(bx + bw / 2), (int)(by + bh / 2));
    Uint32 outside = pixelAt(surf, 5, 5);
    SDL_UnlockSurface(surf);
    CHECK((center & 0x00FFFFFFu) != 0);   // button drew
    CHECK((outside & 0x00FFFFFFu) == 0);  // background untouched

    // --- Click: press inside (no click yet), then release inside (click). ---
    in.mouseX = bx + bw / 2; in.mouseY = by + bh / 2; in.mouseDown = true;
    OkayUI::BeginFrame(in);
    bool onPress = OkayUI::Button(1, bx, by, bw, bh, "Play");
    OkayUI::EndFrame(r);
    CHECK(!onPress);   // desktop buttons fire on release, not press

    in.mouseDown = false;  // released, still inside
    OkayUI::BeginFrame(in);
    bool onRelease = OkayUI::Button(1, bx, by, bw, bh, "Play");
    OkayUI::EndFrame(r);
    CHECK(onRelease);

    // --- Press inside, release OUTSIDE -> cancelled, no click. ---
    in.mouseX = bx + bw / 2; in.mouseY = by + bh / 2; in.mouseDown = true;
    OkayUI::BeginFrame(in); OkayUI::Button(1, bx, by, bw, bh, "Play"); OkayUI::EndFrame(r);
    in.mouseX = 2; in.mouseY = 2; in.mouseDown = false;
    OkayUI::BeginFrame(in); bool offClick = OkayUI::Button(1, bx, by, bw, bh, "Play"); OkayUI::EndFrame(r);
    CHECK(!offClick);

    // --- ImGui owns the mouse (blocked): full press+release inside, but no click. ---
    in.mouseX = bx + bw / 2; in.mouseY = by + bh / 2; in.mouseDown = true; in.blocked = true;
    OkayUI::BeginFrame(in); OkayUI::Button(1, bx, by, bw, bh, "Play"); OkayUI::EndFrame(r);
    in.mouseDown = false;
    OkayUI::BeginFrame(in); bool blockedClick = OkayUI::Button(1, bx, by, bw, bh, "Play"); OkayUI::EndFrame(r);
    CHECK(!blockedClick);

    // --- Optional visual preview: three states side by side is overkill for one
    //     surface, so render idle + hover + pressed stacked and dump a PNG. ---
    if (argc > 2 && std::strcmp(argv[1], "--png") == 0) {
        SDL_Surface* big = SDL_CreateRGBSurfaceWithFormat(0, 240, 170, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Renderer* br = SDL_CreateSoftwareRenderer(big);
        SDL_SetRenderDrawColor(br, 28, 30, 36, 255); SDL_RenderClear(br);   // dark canvas
        OkayUI::Input pv;
        // idle
        pv.mouseX = -1; pv.mouseY = -1; pv.mouseDown = false;
        OkayUI::BeginFrame(pv); OkayUI::Button(10, 40, 20, 160, 40, "Play"); OkayUI::EndFrame(br);
        // hover
        pv.mouseX = 120; pv.mouseY = 85; pv.mouseDown = false;
        OkayUI::BeginFrame(pv); OkayUI::Button(11, 40, 70, 160, 40, "Hover"); OkayUI::EndFrame(br);
        // pressed (arm then hold)
        pv.mouseX = 120; pv.mouseY = 135; pv.mouseDown = true;
        OkayUI::BeginFrame(pv); OkayUI::Button(12, 40, 120, 160, 40, "Press"); OkayUI::EndFrame(br);
        pv.mouseDown = true;
        OkayUI::BeginFrame(pv); OkayUI::Button(12, 40, 120, 160, 40, "Press"); OkayUI::EndFrame(br);
        savePng(big, argv[2]);
        SDL_DestroyRenderer(br); SDL_FreeSurface(big);
    }

    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    std::printf(g_fail == 0 ? "okayui: all checks passed\n" : "okayui: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
