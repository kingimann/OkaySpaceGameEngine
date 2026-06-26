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

    // --- Checkbox: press+release inside toggles the bound bool. ---
    bool checkState = false;
    in.mouseX = 30; in.mouseY = 30; in.mouseDown = true; in.blocked = false;
    OkayUI::BeginFrame(in); OkayUI::Checkbox(2, 20, 20, 24, "On", &checkState); OkayUI::EndFrame(r);
    in.mouseDown = false;
    OkayUI::BeginFrame(in); bool cbChanged = OkayUI::Checkbox(2, 20, 20, 24, "On", &checkState); OkayUI::EndFrame(r);
    CHECK(cbChanged);
    CHECK(checkState == true);

    // --- Slider: click at 75% of the groove sets value to ~75% of the range. ---
    float sval = 0.0f;
    const float sx = 20, sy = 20, sw = 200, sh = 24;
    in.mouseX = sx + sw * 0.75f; in.mouseY = sy + sh / 2; in.mouseDown = true;
    OkayUI::BeginFrame(in); bool slChanged = OkayUI::Slider(3, sx, sy, sw, sh, &sval, 0.0f, 100.0f); OkayUI::EndFrame(r);
    CHECK(slChanged);
    CHECK(sval > 70.0f && sval < 80.0f);
    in.mouseDown = false;   // release
    OkayUI::BeginFrame(in); OkayUI::Slider(3, sx, sy, sw, sh, &sval, 0.0f, 100.0f); OkayUI::EndFrame(r);

    // --- ProgressBar: at t=0.5 the left of the groove is accent, the right is track. ---
    clearBlack();
    OkayUI::BeginFrame(OkayUI::Input{});
    OkayUI::ProgressBar(20, 30, 200, 24, 0.5f);
    OkayUI::EndFrame(r);
    SDL_LockSurface(surf);
    Uint32 leftFill  = pixelAt(surf, 40, 42);    // ~10% across -> filled (accent)
    Uint32 rightTrk  = pixelAt(surf, 200, 42);   // ~90% across -> empty (track)
    SDL_UnlockSurface(surf);
    CHECK((leftFill & 0x00FFFFFFu) != (rightTrk & 0x00FFFFFFu));   // fill differs from groove

    // --- RadioButton: clicking an option selects it (mutually exclusive). ---
    int radio = 0;
    auto clickRadio = [&](int id, float rx, float ry, int option) {
        in.mouseX = rx + 11; in.mouseY = ry + 11; in.mouseDown = true; in.blocked = false;
        OkayUI::BeginFrame(in); OkayUI::RadioButton(id, rx, ry, 22, "", &radio, option); OkayUI::EndFrame(r);
        in.mouseDown = false;
        OkayUI::BeginFrame(in); OkayUI::RadioButton(id, rx, ry, 22, "", &radio, option); OkayUI::EndFrame(r);
    };
    clickRadio(40, 20, 20, 1); CHECK(radio == 1);
    clickRadio(41, 20, 50, 2); CHECK(radio == 2);   // selecting option 2 replaces option 1

    // --- Tab: clicking a tab makes it current; Tab() reports the selected one. ---
    int tab = 0;
    in.mouseX = 130; in.mouseY = 20; in.mouseDown = true;
    OkayUI::BeginFrame(in);
    OkayUI::Tab(50, 20, 10, 100, 30, "A", &tab, 0);
    OkayUI::Tab(51, 120, 10, 100, 30, "B", &tab, 1);   // cursor is over tab B
    OkayUI::EndFrame(r);
    in.mouseDown = false;   // release inside B -> selection becomes 1
    OkayUI::BeginFrame(in);
    OkayUI::Tab(50, 20, 10, 100, 30, "A", &tab, 0);
    OkayUI::Tab(51, 120, 10, 100, 30, "B", &tab, 1);
    OkayUI::EndFrame(r);
    CHECK(tab == 1);
    // Next frame: with the selection settled, each Tab reports it correctly.
    OkayUI::BeginFrame(in);
    bool aSel = OkayUI::Tab(50, 20, 10, 100, 30, "A", &tab, 0);
    bool bSel = OkayUI::Tab(51, 120, 10, 100, 30, "B", &tab, 1);
    OkayUI::EndFrame(r);
    CHECK(!aSel); CHECK(bSel);

    // --- TextField: focus by click, type, backspace. Host feeds Input::text. ---
    char field[32]; field[0] = '\0';
    const float fx = 20, fy = 20, fw = 180, fh = 30;
    in = OkayUI::Input{}; in.mouseX = fx + 10; in.mouseY = fy + 10; in.mouseDown = true;  // click to focus
    OkayUI::BeginFrame(in); OkayUI::TextField(6, fx, fy, fw, fh, field, 32); OkayUI::EndFrame(r);
    in.mouseDown = false;
    in.text = "Hi";  // typed this frame
    OkayUI::BeginFrame(in); bool t1 = OkayUI::TextField(6, fx, fy, fw, fh, field, 32); OkayUI::EndFrame(r);
    CHECK(t1); CHECK(std::strcmp(field, "Hi") == 0);
    in.text = nullptr; in.backspace = true;  // delete one char
    OkayUI::BeginFrame(in); bool t2 = OkayUI::TextField(6, fx, fy, fw, fh, field, 32); OkayUI::EndFrame(r);
    CHECK(t2); CHECK(std::strcmp(field, "H") == 0);
    // Clicking empty space drops focus, so typing no longer edits the field.
    in = OkayUI::Input{}; in.mouseX = 2; in.mouseY = 2; in.mouseDown = true;
    OkayUI::BeginFrame(in); OkayUI::TextField(6, fx, fy, fw, fh, field, 32); OkayUI::EndFrame(r);
    in.mouseDown = false; in.text = "X";
    OkayUI::BeginFrame(in); OkayUI::TextField(6, fx, fy, fw, fh, field, 32); OkayUI::EndFrame(r);
    CHECK(std::strcmp(field, "H") == 0);   // unfocused -> unchanged

    // --- Optional visual preview: a small HUD panel showing every widget. ---
    if (argc > 2 && std::strcmp(argv[1], "--png") == 0) {
        const int PW = 360, PH = 380;
        SDL_Surface* big = SDL_CreateRGBSurfaceWithFormat(0, PW, PH, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Renderer* br = SDL_CreateSoftwareRenderer(big);
        SDL_SetRenderDrawColor(br, 24, 26, 32, 255); SDL_RenderClear(br);
        char demoName[16] = "Player1"; int demoTab = 0, demoMode = 0;
        bool demoCheck = true; float demoSlider = 65.0f;
        OkayUI::Input pv; pv.mouseX = -1; pv.mouseY = -1;   // nothing hovered
        OkayUI::BeginFrame(pv);
        OkayUI::Panel(16, 16, PW - 32, PH - 32);
        OkayUI::Tab(60, 24, 28, 100, 30, "General", &demoTab, 0);
        OkayUI::Tab(61, 128, 28, 100, 30, "Audio",   &demoTab, 1);
        OkayUI::Tab(62, 232, 28, 100, 30, "About",   &demoTab, 2);
        OkayUI::Label(24, 74, "Name");
        OkayUI::TextField(63, 110, 70, 226, 30, demoName, 16);
        OkayUI::Label(24, 116, "Mode");
        OkayUI::RadioButton(64, 110, 114, 22, "Easy", &demoMode, 0);
        OkayUI::RadioButton(65, 206, 114, 22, "Hard", &demoMode, 1);
        OkayUI::Label(24, 152, "Volume");
        OkayUI::Slider(66, 110, 148, 226, 24, &demoSlider, 0.0f, 100.0f);
        OkayUI::Checkbox(67, 110, 186, 26, "Sound", &demoCheck);
        OkayUI::Label(24, 230, "Health");
        OkayUI::ProgressBar(110, 226, 226, 22, 0.8f);
        OkayUI::Label(24, 266, "Hunger");
        OkayUI::ProgressBar(110, 262, 226, 22, 0.35f);
        OkayUI::Button(68, 110, 302, 130, 40, "Play");
        OkayUI::EndFrame(br);
        savePng(big, argv[2]);
        SDL_DestroyRenderer(br); SDL_FreeSurface(big);
    }

    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    std::printf(g_fail == 0 ? "okayui: all checks passed\n" : "okayui: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
