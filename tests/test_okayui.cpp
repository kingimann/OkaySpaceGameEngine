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

    // --- Auto-layout: a window's first auto Button is placed at a known spot and
    //     clicking it works (verifies the cursor + label->id hashing). ---
    {
        // Window at (10,10) 200x200: titleH = 8*2+12 = 28, pad = 10, so content origin
        // is (20, 48). The first Button("Go") is at (20,48), at least 24 wide x 28 tall,
        // so (30, 58) is robustly inside it.
        in = OkayUI::Input{}; in.mouseX = 30; in.mouseY = 58; in.mouseDown = true;
        OkayUI::BeginFrame(in);
        OkayUI::Begin("LayoutW", 10, 10, 200, 200);
        OkayUI::Button("Go");
        OkayUI::End();
        OkayUI::EndFrame(r);
        in.mouseDown = false;
        OkayUI::BeginFrame(in);
        OkayUI::Begin("LayoutW", 10, 10, 200, 200);
        bool go = OkayUI::Button("Go");
        OkayUI::End();
        OkayUI::EndFrame(r);
        CHECK(go);
    }

    // --- CollapsingHeader: starts closed, a click opens it. ---
    {
        OkayUI::BeginFrame(OkayUI::Input{});
        OkayUI::Begin("CHW", 10, 10, 200, 300);
        bool o0 = OkayUI::CollapsingHeader("Sec");
        OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(!o0);
        // Header is the first item: (20,48) full content width, 28 tall -> click (110,62).
        OkayUI::Input ci; ci.mouseX = 110; ci.mouseY = 62; ci.mouseDown = true;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CHW", 10, 10, 200, 300); OkayUI::CollapsingHeader("Sec"); OkayUI::End(); OkayUI::EndFrame(r);
        ci.mouseDown = false;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CHW", 10, 10, 200, 300); bool o1 = OkayUI::CollapsingHeader("Sec"); OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(o1);
    }

    // --- Combo: open by clicking the box, then pick item index 2. ---
    {
        const char* items[] = {"A", "B", "C"};
        int cur = 0;
        OkayUI::Input ci; ci.mouseX = 40; ci.mouseY = 62; ci.mouseDown = true;  // box at (20,48), click inside
        OkayUI::BeginFrame(ci); OkayUI::Begin("CmbW", 10, 10, 220, 300); OkayUI::Combo("Pick", items, 3, &cur); OkayUI::End(); OkayUI::EndFrame(r);
        ci.mouseDown = false;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CmbW", 10, 10, 220, 300); OkayUI::Combo("Pick", items, 3, &cur); OkayUI::End(); OkayUI::EndFrame(r);
        // Now open: list starts at y=76, item 2 ("C") spans y in [132,160) -> click (40,146).
        ci.mouseX = 40; ci.mouseY = 146; ci.mouseDown = true;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CmbW", 10, 10, 220, 300); OkayUI::Combo("Pick", items, 3, &cur); OkayUI::End(); OkayUI::EndFrame(r);
        ci.mouseDown = false;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CmbW", 10, 10, 220, 300); bool ch = OkayUI::Combo("Pick", items, 3, &cur); OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(ch); CHECK(cur == 2);
    }

    // Ensure the mouse is "up" so the next press is a clean rising edge (prior blocks
    // may have ended mid-drag). A frame with empty input clears the press latch.
    auto release = [&]{ OkayUI::BeginFrame(OkayUI::Input{}); OkayUI::EndFrame(r); };

    // --- DragFloat: press the box, then move the cursor right to scrub the value. ---
    {
        release();
        float fv = 0.0f;
        OkayUI::Input di; di.mouseX = 40; di.mouseY = 62; di.mouseDown = true;   // press inside box
        OkayUI::BeginFrame(di); OkayUI::Begin("DFW", 10, 10, 220, 200); OkayUI::DragFloat("X", &fv, 0.1f); OkayUI::End(); OkayUI::EndFrame(r);
        di.mouseX = 60;   // moved +20 px while held -> +20*0.1 = +2.0
        OkayUI::BeginFrame(di); OkayUI::Begin("DFW", 10, 10, 220, 200); OkayUI::DragFloat("X", &fv, 0.1f); OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(fv > 1.9f && fv < 2.1f);
    }

    // --- DragInt: +10 px at speed 1.0 -> +10. ---
    {
        release();
        int iv = 0;
        OkayUI::Input di; di.mouseX = 40; di.mouseY = 62; di.mouseDown = true;
        OkayUI::BeginFrame(di); OkayUI::Begin("DIW", 10, 10, 220, 200); OkayUI::DragInt("Y", &iv, 1.0f); OkayUI::End(); OkayUI::EndFrame(r);
        di.mouseX = 50;
        OkayUI::BeginFrame(di); OkayUI::Begin("DIW", 10, 10, 220, 200); OkayUI::DragInt("Y", &iv, 1.0f); OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(iv == 10);
    }

    // --- ColorEdit3: clicking the first channel slider raises R from 0. ---
    {
        release();
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        OkayUI::Input ce; ce.mouseX = 76; ce.mouseY = 62; ce.mouseDown = true;   // over channel-0 slider
        OkayUI::BeginFrame(ce); OkayUI::Begin("CEW", 10, 10, 220, 200); OkayUI::ColorEdit3("C", rgb); OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(rgb[0] > 0.2f);
    }

    // --- TreeNode: starts closed; a click opens it. ---
    {
        OkayUI::BeginFrame(OkayUI::Input{});
        OkayUI::Begin("TNW", 10, 10, 200, 200); bool o0 = OkayUI::TreeNode("N"); if (o0) OkayUI::TreePop(); OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(!o0);
        OkayUI::Input ti; ti.mouseX = 110; ti.mouseY = 62; ti.mouseDown = true;
        OkayUI::BeginFrame(ti); OkayUI::Begin("TNW", 10, 10, 200, 200); bool a = OkayUI::TreeNode("N"); if (a) OkayUI::TreePop(); OkayUI::End(); OkayUI::EndFrame(r);
        ti.mouseDown = false;
        OkayUI::BeginFrame(ti); OkayUI::Begin("TNW", 10, 10, 200, 200); bool o1 = OkayUI::TreeNode("N"); if (o1) OkayUI::TreePop(); OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(o1);
    }

    // --- Menu: open "File", then click "Open". ---
    {
        release();
        bool opened = false;
        auto mframe = [&](OkayUI::Input mi) {
            OkayUI::BeginFrame(mi);
            OkayUI::Begin("MW", 10, 10, 260, 200);
            OkayUI::BeginMenuBar();
            if (OkayUI::BeginMenu("File")) { if (OkayUI::MenuItem("Open")) opened = true; OkayUI::EndMenu(); }
            OkayUI::EndMenuBar();
            OkayUI::End(); OkayUI::EndFrame(r);
        };
        OkayUI::Input mi;
        mi.mouseX = 40; mi.mouseY = 62; mi.mouseDown = true;  mframe(mi);  // press File button
        mi.mouseDown = false;                                 mframe(mi);  // release -> menu opens
        mi.mouseX = 40; mi.mouseY = 90; mi.mouseDown = true;  mframe(mi);  // press "Open" item
        mi.mouseDown = false;                                 mframe(mi);  // release -> click
        CHECK(opened);
    }

    // --- Selectable: a full-width row reports a click. ---
    {
        release();
        OkayUI::Input si; si.mouseX = 40; si.mouseY = 62; si.mouseDown = true;
        OkayUI::BeginFrame(si); OkayUI::Begin("SW", 10, 10, 200, 200); OkayUI::Selectable("Item", false); OkayUI::End(); OkayUI::EndFrame(r);
        si.mouseDown = false;
        OkayUI::BeginFrame(si); OkayUI::Begin("SW", 10, 10, 200, 200); bool c = OkayUI::Selectable("Item", false); OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(c);
    }

    // --- InputInt: clicking [+] / [-] steps the value. ---
    {
        int iv = 5;
        // The row is at the top of the window content; [-] and [+] are the two square
        // buttons on the right. Window at (10,10), width 200. Click the '+' stepper.
        auto clickAt = [&](float mx, float my) {
            release();
            OkayUI::Input a; a.mouseX = mx; a.mouseY = my; a.mouseDown = true;
            OkayUI::BeginFrame(a); OkayUI::Begin("IIW", 10, 10, 200, 120); OkayUI::InputInt("N", &iv); OkayUI::End(); OkayUI::EndFrame(r);
            a.mouseDown = false;
            OkayUI::BeginFrame(a); OkayUI::Begin("IIW", 10, 10, 200, 120); OkayUI::InputInt("N", &iv); OkayUI::End(); OkayUI::EndFrame(r);
        };
        // Content starts at y ~48 (title bar + pad); the value box is ~92px wide, then
        // the two 28px steppers. The '+' stepper center lands near (162, 62).
        clickAt(162, 62);
        CHECK(iv == 6);   // stepped up by 1
    }

    // --- PushID: same label in a loop yields distinct, independently-clickable widgets. ---
    {
        int clicks[3] = {0, 0, 0};
        auto frame = [&](float mx, float my, bool down) {
            OkayUI::Input a; a.mouseX = mx; a.mouseY = my; a.mouseDown = down;
            OkayUI::BeginFrame(a);
            OkayUI::Begin("PIDW", 10, 10, 200, 200);
            for (int i = 0; i < 3; ++i) {
                OkayUI::PushID(i);
                if (OkayUI::Button("Go")) clicks[i]++;
                OkayUI::PopID();
            }
            OkayUI::End();
            OkayUI::EndFrame(r);
        };
        release();
        // The three "Go" buttons stack vertically. Press+release on the SECOND one.
        // Row height ~ textH*2 + 12 ≈ 28; second button roughly y ≈ 10 + 28 + 14.
        frame(40, 52, true);
        frame(40, 52, false);
        // Exactly one button should have registered the click (not all three colliding).
        int total = clicks[0] + clicks[1] + clicks[2];
        CHECK(total == 1);
    }

    // --- Window collapse: clicking the title-bar caret folds the window. ---
    {
        release();
        bool contentRan = false;
        auto frame = [&](bool down) {
            OkayUI::Input a; a.mouseX = 18; a.mouseY = 20; a.mouseDown = down;   // over the caret
            OkayUI::BeginFrame(a);
            contentRan = false;
            if (OkayUI::Begin("ColW", 10, 10, 200, 160)) { OkayUI::Text("body"); contentRan = true; }
            OkayUI::End();
            OkayUI::EndFrame(r);
        };
        frame(false); CHECK(contentRan);      // starts expanded
        frame(true); frame(false);            // click the caret -> toggle collapsed
        frame(false); CHECK(!contentRan);     // now folded: Begin returned false
        frame(true); frame(false);            // click again -> expand
        frame(false); CHECK(contentRan);
    }

    // --- Window close button: clicking [x] clears the caller's open flag. ---
    {
        release();
        bool open = true;
        auto frame = [&](bool down) {
            OkayUI::Input a; a.mouseX = 196; a.mouseY = 20; a.mouseDown = down;  // over the [x]
            OkayUI::BeginFrame(a);
            if (OkayUI::Begin("CloseW", 10, 10, 200, 160, &open)) OkayUI::Text("body");
            OkayUI::End();
            OkayUI::EndFrame(r);
        };
        frame(false); CHECK(open);
        frame(true); frame(false);
        CHECK(!open);   // close button cleared it
    }

    // --- TabBar: clicking a tab sets *current to its index. ---
    {
        release();
        int cur = 0;
        const char* tabs[] = {"One", "Two", "Three"};
        auto frame = [&](float mx, bool down) {
            OkayUI::Input a; a.mouseX = mx; a.mouseY = 60; a.mouseDown = down;  // content row (below title)
            OkayUI::BeginFrame(a);
            OkayUI::Begin("TBW", 10, 10, 260, 160);
            OkayUI::TabBar(tabs, 3, &cur);
            OkayUI::End();
            OkayUI::EndFrame(r);
        };
        // "One" ~ 20+? First tab starts at content x=20; "One" width = 3*8*2 + 20 = 68,
        // so "Two" begins ~ x=90. Click within the second tab.
        frame(110, false);           // draw once (no click)
        frame(110, true); frame(110, false);
        CHECK(cur == 1);             // selected the second tab
    }

    // --- SmallButton: reports a click like Button, in a tighter rect. ---
    {
        release();
        OkayUI::Input a; a.mouseX = 30; a.mouseY = 56; a.mouseDown = true;   // content row
        OkayUI::BeginFrame(a); OkayUI::Begin("SBW", 10, 10, 200, 120); OkayUI::SmallButton("Go"); OkayUI::End(); OkayUI::EndFrame(r);
        a.mouseDown = false;
        OkayUI::BeginFrame(a); OkayUI::Begin("SBW", 10, 10, 200, 120); bool c = OkayUI::SmallButton("Go"); OkayUI::End(); OkayUI::EndFrame(r);
        CHECK(c);
    }

    // --- PushStyleColor: overrides a color for a widget, then restores it. ---
    {
        // Draw a Text in a pushed red, sample its pixels; then draw again after the
        // pop and confirm the theme color returned (so the push was scoped).
        auto redCount = [&](bool pushed) {
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r);
            OkayUI::BeginFrame(OkayUI::Input{});
            OkayUI::Begin("PSCW", 4, 4, 220, 80);
            if (pushed) OkayUI::PushStyleColor(OkayUI::Col_Text, 255, 0, 0);
            OkayUI::Text("HELLO");
            if (pushed) OkayUI::PopStyleColor();
            OkayUI::End(); OkayUI::EndFrame(r);
            SDL_LockSurface(surf);
            int red = 0;
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                Uint32 px = pixelAt(surf, x, y);
                Uint8 rr = (px >> 16) & 0xFF, gg = (px >> 8) & 0xFF, bb = px & 0xFF;
                if (rr > 180 && gg < 80 && bb < 80) ++red;   // strong red pixels
            }
            SDL_UnlockSurface(surf);
            return red;
        };
        CHECK(redCount(true) > 20);    // pushed color drew red body text
        // After a matched pop the default (light) text returns -> no red run.
        CHECK(redCount(false) < 5);
    }

    // --- Fonts: the bold font lights more pixels than the default for the same text. ---
    {
        auto countLit = [&](const OkayUI::Font* f) {
            OkayUI::SetFont(f);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r);
            OkayUI::BeginFrame(OkayUI::Input{});
            OkayUI::Begin("FW", 2, 2, 220, 80);
            OkayUI::Text("WWWW");
            OkayUI::End(); OkayUI::EndFrame(r);
            SDL_LockSurface(surf);
            int n = 0;   // count bright (text-colored) pixels; the panel/bg are dark
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
                if (((pixelAt(surf, x, y) >> 16) & 0xFFu) > 150u) ++n;
            SDL_UnlockSurface(surf);
            return n;
        };
        int dn = countLit(OkayUI::FontDefault());
        int bn = countLit(OkayUI::FontBold());
        OkayUI::SetFont(OkayUI::FontDefault());
        CHECK(bn > dn);
        CHECK(OkayUI::GetFont() == OkayUI::FontDefault());
    }

    // --- Optional visual preview: an auto-layout window showing the widgets. ---
    if (argc > 2 && std::strcmp(argv[1], "--png") == 0) {
        const int PW = 360, PH = 600;
        SDL_Surface* big = SDL_CreateRGBSurfaceWithFormat(0, PW, PH, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Renderer* br = SDL_CreateSoftwareRenderer(big);
        char demoName[16] = "Player1"; int demoMode = 0, demoSel = 1, demoLives = 3;
        bool demoCheck = true; float demoSlider = 65.0f, demoSpeed = 1.5f;
        float demoCol[3] = {0.30f, 0.65f, 0.95f};
        const char* opts[] = {"Low", "Medium", "High"};
        auto panel = [&](OkayUI::Input pv) {
            OkayUI::BeginFrame(pv);
            OkayUI::SetFont(OkayUI::FontBold());            // bold window title...
            OkayUI::Begin("Settings", 12, 12, PW - 24, PH - 24);
            OkayUI::SetFont(OkayUI::FontDefault());         // ...regular body
            OkayUI::BeginMenuBar();
            if (OkayUI::BeginMenu("File")) { OkayUI::MenuItem("Open"); OkayUI::MenuItem("Save"); OkayUI::EndMenu(); }
            if (OkayUI::BeginMenu("Help")) { OkayUI::MenuItem("About"); OkayUI::EndMenu(); }
            OkayUI::EndMenuBar();
            OkayUI::Combo("Quality", opts, 3, &demoSel);   // first item under the bar
            OkayUI::Text("Auto-layout + overlay");
            OkayUI::Separator();
            OkayUI::InputText("Name", demoName, 16);
            OkayUI::RadioButton("Easy", &demoMode, 0);
            OkayUI::SameLine();
            OkayUI::RadioButton("Hard", &demoMode, 1);
            OkayUI::SliderFloat("Volume", &demoSlider, 0.0f, 100.0f);
            OkayUI::DragFloat("Speed", &demoSpeed, 0.1f, 0.0f, 10.0f);
            OkayUI::DragInt("Lives", &demoLives, 0.25f, 0, 9);
            OkayUI::ColorEdit3("Tint", demoCol);
            OkayUI::Checkbox("Sound", &demoCheck);
            OkayUI::Separator();
            if (OkayUI::TreeNode("Stats")) {
                OkayUI::Text("Health"); OkayUI::ProgressBar(0.8f);
                OkayUI::Text("Hunger"); OkayUI::ProgressBar(0.35f);
                OkayUI::TreePop();
            }
            OkayUI::Spacing();
            OkayUI::Button("Play");  OkayUI::SameLine();  OkayUI::Button("Quit");
            OkayUI::End();
        };
        // Render the widgets in their default state so all of them are visible.
        SDL_SetRenderDrawColor(br, 24, 26, 32, 255); SDL_RenderClear(br);
        OkayUI::Input pv; pv.mouseX = -1; pv.mouseY = -1;
        panel(pv);
        OkayUI::EndFrame(br);
        savePng(big, argv[2]);
        SDL_DestroyRenderer(br); SDL_FreeSurface(big);
    }

    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    std::printf(g_fail == 0 ? "okayui: all checks passed\n" : "okayui: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
