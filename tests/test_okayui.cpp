// Headless test for OkayUI. Its frame is drawn by FrameDrawer on the software painter (okayui/soft) on an in-memory
// frame buffer — no window, no GPU, no display — and the pixels are read back to prove the button actually rasterized.
// We also exercise the press/release click logic and the ImGui-capture gate.
//
// Run with "--png <path>" to additionally dump a visual preview (normal / hover /
// pressed states) as a PNG.
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUIDesign.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Render/Color.hpp"
#include "OkayUI_SoftPainter.hpp"
#include <cstdint>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

static const int W = 240, H = 90;

/// A software-painter canvas with OkayUI's frame drawer on it, read back as RGBA rows.
struct Canvas {
    int w = 0, h = 0;
    std::unique_ptr<okay::platform::Painter> p;
    std::unique_ptr<OkayUI::soft::FrameDrawer> d;
    std::vector<std::uint8_t> px;
    bool Open(int cw, int ch) {
        w = cw; h = ch;
        p = OkayUI::soft::CreateBitmapPainter(w, h);
        if (!p) return false;
        p->SetBlendMode(okay::platform::BlendMode::Blend);
        d.reset(new OkayUI::soft::FrameDrawer(*p));
        return true;
    }
    void Clear(int r, int g, int b) { p->SetDrawColor((std::uint8_t)r, (std::uint8_t)g, (std::uint8_t)b, 255); p->Clear(); }
    void End() { OkayUI::EndFrameData(w, h); d->Draw(OkayUI::GetDrawData()); }
    void Lock() { int rw = 0, rh = 0; px.clear(); p->ReadPixels(px, rw, rh); }
    /// The pixel as 0xAARRGGBB (the layout the checks below were written against).
    std::uint32_t At(int x, int y) const {
        const std::size_t i = ((std::size_t)y * w + x) * 4;
        if (i + 3 >= px.size()) return 0;
        return ((std::uint32_t)px[i + 3] << 24) | ((std::uint32_t)px[i] << 16) | ((std::uint32_t)px[i + 1] << 8) | px[i + 2];
    }
    void Rgba(int x, int y, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) const {
        const std::uint32_t c = At(x, y); r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; b = c & 0xFF; a = c >> 24;
    }
    void Save(const char* path) {
        Lock();
        okay::Image img(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) { std::uint8_t r, g, b, a; Rgba(x, y, r, g, b, a); img.SetPixel(x, y, okay::Color::FromBytes(r, g, b, a)); }
        std::printf(img.SavePNG(path) ? "wrote %s\n" : "failed to write %s\n", path);
    }
};
static Canvas g_cv;   // the W x H canvas most checks draw on
static void clearBlack() { g_cv.Clear(0, 0, 0); }
static void endFrame() { g_cv.End(); }
static void lockPixels() { g_cv.Lock(); }
static std::uint32_t pixelAt(int x, int y) { return g_cv.At(x, y); }

int main(int argc, char** argv) {
    const bool opened = g_cv.Open(W, H);
    CHECK(opened);
    if (!opened) { std::printf("okayui: software painter unavailable\n"); return 1; }

    const float bx = 40, by = 25, bw = 160, bh = 40;

    // --- Frame 1: idle (mouse away, not pressed). Button should draw, not click. ---
    clearBlack();
    OkayUI::Input in;  // all zero / false
    OkayUI::BeginFrame(in);
    bool idle = OkayUI::Button(1, bx, by, bw, bh, "Play");
    endFrame();
    CHECK(!idle);
    // The button center must be non-black -> geometry actually rasterized.
    lockPixels();
    std::uint32_t center = pixelAt((int)(bx + bw / 2), (int)(by + bh / 2));
    std::uint32_t outside = pixelAt(5, 5);
    CHECK((center & 0x00FFFFFFu) != 0);   // button drew
    CHECK((outside & 0x00FFFFFFu) == 0);  // background untouched

    // --- Click: press inside (no click yet), then release inside (click). ---
    in.mouseX = bx + bw / 2; in.mouseY = by + bh / 2; in.mouseDown = true;
    OkayUI::BeginFrame(in);
    bool onPress = OkayUI::Button(1, bx, by, bw, bh, "Play");
    endFrame();
    CHECK(!onPress);   // desktop buttons fire on release, not press

    in.mouseDown = false;  // released, still inside
    OkayUI::BeginFrame(in);
    bool onRelease = OkayUI::Button(1, bx, by, bw, bh, "Play");
    endFrame();
    CHECK(onRelease);

    // --- Press inside, release OUTSIDE -> cancelled, no click. ---
    in.mouseX = bx + bw / 2; in.mouseY = by + bh / 2; in.mouseDown = true;
    OkayUI::BeginFrame(in); OkayUI::Button(1, bx, by, bw, bh, "Play"); endFrame();
    in.mouseX = 2; in.mouseY = 2; in.mouseDown = false;
    OkayUI::BeginFrame(in); bool offClick = OkayUI::Button(1, bx, by, bw, bh, "Play"); endFrame();
    CHECK(!offClick);

    // --- ImGui owns the mouse (blocked): full press+release inside, but no click. ---
    in.mouseX = bx + bw / 2; in.mouseY = by + bh / 2; in.mouseDown = true; in.blocked = true;
    OkayUI::BeginFrame(in); OkayUI::Button(1, bx, by, bw, bh, "Play"); endFrame();
    in.mouseDown = false;
    OkayUI::BeginFrame(in); bool blockedClick = OkayUI::Button(1, bx, by, bw, bh, "Play"); endFrame();
    CHECK(!blockedClick);

    // --- Checkbox: press+release inside toggles the bound bool. ---
    bool checkState = false;
    in.mouseX = 30; in.mouseY = 30; in.mouseDown = true; in.blocked = false;
    OkayUI::BeginFrame(in); OkayUI::Checkbox(2, 20, 20, 24, "On", &checkState); endFrame();
    in.mouseDown = false;
    OkayUI::BeginFrame(in); bool cbChanged = OkayUI::Checkbox(2, 20, 20, 24, "On", &checkState); endFrame();
    CHECK(cbChanged);
    CHECK(checkState == true);

    // --- Slider: click at 75% of the groove sets value to ~75% of the range. ---
    float sval = 0.0f;
    const float sx = 20, sy = 20, sw = 200, sh = 24;
    in.mouseX = sx + sw * 0.75f; in.mouseY = sy + sh / 2; in.mouseDown = true;
    OkayUI::BeginFrame(in); bool slChanged = OkayUI::Slider(3, sx, sy, sw, sh, &sval, 0.0f, 100.0f); endFrame();
    CHECK(slChanged);
    CHECK(sval > 70.0f && sval < 80.0f);
    in.mouseDown = false;   // release
    OkayUI::BeginFrame(in); OkayUI::Slider(3, sx, sy, sw, sh, &sval, 0.0f, 100.0f); endFrame();

    // --- ProgressBar: at t=0.5 the left of the groove is accent, the right is track. ---
    clearBlack();
    OkayUI::BeginFrame(OkayUI::Input{});
    OkayUI::ProgressBar(20, 30, 200, 24, 0.5f);
    endFrame();
    lockPixels();
    std::uint32_t leftFill  = pixelAt(40, 42);    // ~10% across -> filled (accent)
    std::uint32_t rightTrk  = pixelAt(200, 42);   // ~90% across -> empty (track)
    CHECK((leftFill & 0x00FFFFFFu) != (rightTrk & 0x00FFFFFFu));   // fill differs from groove

    // --- RadioButton: clicking an option selects it (mutually exclusive). ---
    int radio = 0;
    auto clickRadio = [&](int id, float rx, float ry, int option) {
        in.mouseX = rx + 11; in.mouseY = ry + 11; in.mouseDown = true; in.blocked = false;
        OkayUI::BeginFrame(in); OkayUI::RadioButton(id, rx, ry, 22, "", &radio, option); endFrame();
        in.mouseDown = false;
        OkayUI::BeginFrame(in); OkayUI::RadioButton(id, rx, ry, 22, "", &radio, option); endFrame();
    };
    clickRadio(40, 20, 20, 1); CHECK(radio == 1);
    clickRadio(41, 20, 50, 2); CHECK(radio == 2);   // selecting option 2 replaces option 1

    // --- Tab: clicking a tab makes it current; Tab() reports the selected one. ---
    int tab = 0;
    in.mouseX = 130; in.mouseY = 20; in.mouseDown = true;
    OkayUI::BeginFrame(in);
    OkayUI::Tab(50, 20, 10, 100, 30, "A", &tab, 0);
    OkayUI::Tab(51, 120, 10, 100, 30, "B", &tab, 1);   // cursor is over tab B
    endFrame();
    in.mouseDown = false;   // release inside B -> selection becomes 1
    OkayUI::BeginFrame(in);
    OkayUI::Tab(50, 20, 10, 100, 30, "A", &tab, 0);
    OkayUI::Tab(51, 120, 10, 100, 30, "B", &tab, 1);
    endFrame();
    CHECK(tab == 1);
    // Next frame: with the selection settled, each Tab reports it correctly.
    OkayUI::BeginFrame(in);
    bool aSel = OkayUI::Tab(50, 20, 10, 100, 30, "A", &tab, 0);
    bool bSel = OkayUI::Tab(51, 120, 10, 100, 30, "B", &tab, 1);
    endFrame();
    CHECK(!aSel); CHECK(bSel);

    // --- TextField: focus by click, type, backspace. Host feeds Input::text. ---
    char field[32]; field[0] = '\0';
    const float fx = 20, fy = 20, fw = 180, fh = 30;
    in = OkayUI::Input{}; in.mouseX = fx + 10; in.mouseY = fy + 10; in.mouseDown = true;  // click to focus
    OkayUI::BeginFrame(in); OkayUI::TextField(6, fx, fy, fw, fh, field, 32); endFrame();
    in.mouseDown = false;
    in.text = "Hi";  // typed this frame
    OkayUI::BeginFrame(in); bool t1 = OkayUI::TextField(6, fx, fy, fw, fh, field, 32); endFrame();
    CHECK(t1); CHECK(std::strcmp(field, "Hi") == 0);
    in.text = nullptr; in.backspace = true;  // delete one char
    OkayUI::BeginFrame(in); bool t2 = OkayUI::TextField(6, fx, fy, fw, fh, field, 32); endFrame();
    CHECK(t2); CHECK(std::strcmp(field, "H") == 0);
    // Clicking empty space drops focus, so typing no longer edits the field.
    in = OkayUI::Input{}; in.mouseX = 2; in.mouseY = 2; in.mouseDown = true;
    OkayUI::BeginFrame(in); OkayUI::TextField(6, fx, fy, fw, fh, field, 32); endFrame();
    in.mouseDown = false; in.text = "X";
    OkayUI::BeginFrame(in); OkayUI::TextField(6, fx, fy, fw, fh, field, 32); endFrame();
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
        endFrame();
        in.mouseDown = false;
        OkayUI::BeginFrame(in);
        OkayUI::Begin("LayoutW", 10, 10, 200, 200);
        bool go = OkayUI::Button("Go");
        OkayUI::End();
        endFrame();
        CHECK(go);
    }

    // --- CollapsingHeader: starts closed, a click opens it. ---
    {
        OkayUI::BeginFrame(OkayUI::Input{});
        OkayUI::Begin("CHW", 10, 10, 200, 300);
        bool o0 = OkayUI::CollapsingHeader("Sec");
        OkayUI::End(); endFrame();
        CHECK(!o0);
        // Header is the first item: (20,48) full content width, 28 tall -> click (110,62).
        OkayUI::Input ci; ci.mouseX = 110; ci.mouseY = 62; ci.mouseDown = true;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CHW", 10, 10, 200, 300); OkayUI::CollapsingHeader("Sec"); OkayUI::End(); endFrame();
        ci.mouseDown = false;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CHW", 10, 10, 200, 300); bool o1 = OkayUI::CollapsingHeader("Sec"); OkayUI::End(); endFrame();
        CHECK(o1);
    }

    // --- Combo: open by clicking the box, then pick item index 2. ---
    {
        const char* items[] = {"A", "B", "C"};
        int cur = 0;
        OkayUI::Input ci; ci.mouseX = 40; ci.mouseY = 62; ci.mouseDown = true;  // box at (20,48), click inside
        OkayUI::BeginFrame(ci); OkayUI::Begin("CmbW", 10, 10, 220, 300); OkayUI::Combo("Pick", items, 3, &cur); OkayUI::End(); endFrame();
        ci.mouseDown = false;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CmbW", 10, 10, 220, 300); OkayUI::Combo("Pick", items, 3, &cur); OkayUI::End(); endFrame();
        // Now open: list starts at y=76, item 2 ("C") spans y in [132,160) -> click (40,146).
        ci.mouseX = 40; ci.mouseY = 146; ci.mouseDown = true;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CmbW", 10, 10, 220, 300); OkayUI::Combo("Pick", items, 3, &cur); OkayUI::End(); endFrame();
        ci.mouseDown = false;
        OkayUI::BeginFrame(ci); OkayUI::Begin("CmbW", 10, 10, 220, 300); bool ch = OkayUI::Combo("Pick", items, 3, &cur); OkayUI::End(); endFrame();
        CHECK(ch); CHECK(cur == 2);
    }

    // Ensure the mouse is "up" so the next press is a clean rising edge (prior blocks
    // may have ended mid-drag). A frame with empty input clears the press latch.
    auto release = [&]{ OkayUI::BeginFrame(OkayUI::Input{}); endFrame(); };

    // --- DragFloat: press the box, then move the cursor right to scrub the value. ---
    {
        release();
        float fv = 0.0f;
        OkayUI::Input di; di.mouseX = 40; di.mouseY = 62; di.mouseDown = true;   // press inside box
        OkayUI::BeginFrame(di); OkayUI::Begin("DFW", 10, 10, 220, 200); OkayUI::DragFloat("X", &fv, 0.1f); OkayUI::End(); endFrame();
        di.mouseX = 60;   // moved +20 px while held -> +20*0.1 = +2.0
        OkayUI::BeginFrame(di); OkayUI::Begin("DFW", 10, 10, 220, 200); OkayUI::DragFloat("X", &fv, 0.1f); OkayUI::End(); endFrame();
        CHECK(fv > 1.9f && fv < 2.1f);
    }

    // --- DragInt: +10 px at speed 1.0 -> +10. ---
    {
        release();
        int iv = 0;
        OkayUI::Input di; di.mouseX = 40; di.mouseY = 62; di.mouseDown = true;
        OkayUI::BeginFrame(di); OkayUI::Begin("DIW", 10, 10, 220, 200); OkayUI::DragInt("Y", &iv, 1.0f); OkayUI::End(); endFrame();
        di.mouseX = 50;
        OkayUI::BeginFrame(di); OkayUI::Begin("DIW", 10, 10, 220, 200); OkayUI::DragInt("Y", &iv, 1.0f); OkayUI::End(); endFrame();
        CHECK(iv == 10);
    }

    // --- ColorEdit3: clicking the first channel slider raises R from 0. ---
    {
        release();
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        OkayUI::Input ce; ce.mouseX = 76; ce.mouseY = 62; ce.mouseDown = true;   // over channel-0 slider
        OkayUI::BeginFrame(ce); OkayUI::Begin("CEW", 10, 10, 220, 200); OkayUI::ColorEdit3("C", rgb); OkayUI::End(); endFrame();
        CHECK(rgb[0] > 0.2f);
    }

    // --- TreeNode: starts closed; a click opens it. ---
    {
        OkayUI::BeginFrame(OkayUI::Input{});
        OkayUI::Begin("TNW", 10, 10, 200, 200); bool o0 = OkayUI::TreeNode("N"); if (o0) OkayUI::TreePop(); OkayUI::End(); endFrame();
        CHECK(!o0);
        OkayUI::Input ti; ti.mouseX = 110; ti.mouseY = 62; ti.mouseDown = true;
        OkayUI::BeginFrame(ti); OkayUI::Begin("TNW", 10, 10, 200, 200); bool a = OkayUI::TreeNode("N"); if (a) OkayUI::TreePop(); OkayUI::End(); endFrame();
        ti.mouseDown = false;
        OkayUI::BeginFrame(ti); OkayUI::Begin("TNW", 10, 10, 200, 200); bool o1 = OkayUI::TreeNode("N"); if (o1) OkayUI::TreePop(); OkayUI::End(); endFrame();
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
            OkayUI::End(); endFrame();
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
        OkayUI::BeginFrame(si); OkayUI::Begin("SW", 10, 10, 200, 200); OkayUI::Selectable("Item", false); OkayUI::End(); endFrame();
        si.mouseDown = false;
        OkayUI::BeginFrame(si); OkayUI::Begin("SW", 10, 10, 200, 200); bool c = OkayUI::Selectable("Item", false); OkayUI::End(); endFrame();
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
            OkayUI::BeginFrame(a); OkayUI::Begin("IIW", 10, 10, 200, 120); OkayUI::InputInt("N", &iv); OkayUI::End(); endFrame();
            a.mouseDown = false;
            OkayUI::BeginFrame(a); OkayUI::Begin("IIW", 10, 10, 200, 120); OkayUI::InputInt("N", &iv); OkayUI::End(); endFrame();
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
            endFrame();
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
            endFrame();
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
            endFrame();
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
            endFrame();
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
        OkayUI::BeginFrame(a); OkayUI::Begin("SBW", 10, 10, 200, 120); OkayUI::SmallButton("Go"); OkayUI::End(); endFrame();
        a.mouseDown = false;
        OkayUI::BeginFrame(a); OkayUI::Begin("SBW", 10, 10, 200, 120); bool c = OkayUI::SmallButton("Go"); OkayUI::End(); endFrame();
        CHECK(c);
    }

    // --- PushStyleColor: overrides a color for a widget, then restores it. ---
    {
        // Draw a Text in a pushed red, sample its pixels; then draw again after the
        // pop and confirm the theme color returned (so the push was scoped).
        auto redCount = [&](bool pushed) {
            clearBlack();
            OkayUI::BeginFrame(OkayUI::Input{});
            OkayUI::Begin("PSCW", 4, 4, 220, 80);
            if (pushed) OkayUI::PushStyleColor(OkayUI::Col_Text, 255, 0, 0);
            OkayUI::Text("HELLO");
            if (pushed) OkayUI::PopStyleColor();
            OkayUI::End(); endFrame();
            lockPixels();
            int red = 0;
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                std::uint32_t px = pixelAt(x, y);
                std::uint8_t rr = (px >> 16) & 0xFF, gg = (px >> 8) & 0xFF, bb = px & 0xFF;
                if (rr > 180 && gg < 80 && bb < 80) ++red;   // strong red pixels
            }
            return red;
        };
        CHECK(redCount(true) > 20);    // pushed color drew red body text
        // After a matched pop the default (light) text returns -> no red run.
        CHECK(redCount(false) < 5);
    }

    // --- PlotLines / PlotHistogram: draw accent-colored data inside a frame. ---
    {
        const float data[] = {0.0f, 1.0f, 2.0f, 1.5f, 3.0f, 0.5f, 2.5f};
        auto accentPixels = [&](bool histogram) {
            clearBlack();
            OkayUI::BeginFrame(OkayUI::Input{});
            OkayUI::Begin("PLW", 4, 4, 230, 84);
            if (histogram) OkayUI::PlotHistogram("", data, 7);
            else           OkayUI::PlotLines("", data, 7);
            OkayUI::End(); endFrame();
            lockPixels();
            int accent = 0;   // theme accent is a blue (84,150,240): blue-dominant pixels
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                std::uint32_t px = pixelAt(x, y);
                std::uint8_t rr = (px >> 16) & 0xFF, gg = (px >> 8) & 0xFF, bb = px & 0xFF;
                if (bb > 180 && bb > rr + 40 && gg > 80) ++accent;
            }
            return accent;
        };
        CHECK(accentPixels(false) > 15);   // line plot drew
        CHECK(accentPixels(true) > 15);    // histogram drew
    }

    // --- SetNextItemWidth: narrows the next full-width widget. ---
    {
        auto rightmostAccentX = [&](float widthOverride) {
            clearBlack();
            OkayUI::BeginFrame(OkayUI::Input{});
            OkayUI::Begin("IWW", 4, 4, 230, 60);
            if (widthOverride > 0) OkayUI::SetNextItemWidth(widthOverride);
            OkayUI::ProgressBar(1.0f);   // fills its width with accent
            OkayUI::End(); endFrame();
            lockPixels();
            int maxx = -1;
            // Scan only the content area (below the title bar's full-width accent line).
            for (int y = 40; y < H; ++y) for (int x = 0; x < W; ++x) {
                std::uint32_t px = pixelAt(x, y);
                std::uint8_t rr = (px >> 16) & 0xFF, gg = (px >> 8) & 0xFF, bb = px & 0xFF;
                if (bb > 180 && bb > rr + 40 && gg > 80 && x > maxx) maxx = x;
            }
            return maxx;
        };
        int full = rightmostAccentX(0);
        int narrow = rightmostAccentX(50);
        CHECK(full > 0 && narrow > 0);
        CHECK(narrow < full - 30);   // the 50px-wide bar ends well left of the full one
    }

    // --- BeginChild: clips overflow and scrolls the content into view. ---
    {
        // A short child region with several lines; only the last is red. It sits far
        // below the fold, so it's clipped away until we scroll the region down.
        auto drawChild = [&](float wheel) {
            clearBlack();
            OkayUI::Input a; a.mouseX = 120; a.mouseY = 60; a.wheel = wheel;  // cursor over region
            OkayUI::BeginFrame(a);
            OkayUI::Begin("ScW", 4, 4, 240, 84);
            OkayUI::BeginChild("sc", 0, 40);
            OkayUI::Text("one"); OkayUI::Text("two"); OkayUI::Text("three");
            OkayUI::Text("four"); OkayUI::Text("five");
            OkayUI::TextColored(255, 0, 0, "END");   // the marker line, far down
            OkayUI::EndChild();
            OkayUI::End(); endFrame();
        };
        auto redIn = [&](int y0, int y1) {
            lockPixels();
            int n = 0;
            for (int y = y0; y < y1; ++y) for (int x = 0; x < W; ++x) {
                std::uint32_t px = pixelAt(x, y);
                std::uint8_t rr = (px >> 16) & 0xFF, gg = (px >> 8) & 0xFF, bb = px & 0xFF;
                if (rr > 180 && gg < 80 && bb < 80) ++n;
            }
            return n;
        };
        // Pin to the top (positive wheel scrolls up / clamps at 0).
        for (int i = 0; i < 3; ++i) drawChild(50.0f);
        CHECK(redIn(0, H) < 5);            // END marker is scrolled off + clipped: not visible
        // Scroll down to the bottom; the END marker enters the region.
        for (int i = 0; i < 8; ++i) drawChild(-50.0f);
        CHECK(redIn(42, 82) > 15);         // END marker now visible inside the region
        CHECK(redIn(83, H) < 5);           // and clipping keeps it from spilling below the region
    }

    // --- ListBox: clicking a row selects that item. ---
    {
        release();
        int sel = 0;
        const char* items[] = {"Alpha", "Beta", "Gamma", "Delta"};
        auto frame = [&](float my, bool down) {
            OkayUI::Input a; a.mouseX = 40; a.mouseY = my; a.mouseDown = down;
            OkayUI::BeginFrame(a);
            OkayUI::Begin("LBW", 10, 10, 220, 180);
            OkayUI::ListBox("##list", &sel, items, 4, 3);
            OkayUI::End();
            endFrame();
        };
        // Rows start ~ y=54 with a 34px pitch; the second row ("Beta") is ~ [88,116].
        frame(100, false);
        frame(100, true); frame(100, false);
        CHECK(sel == 1);
    }

    // --- Columns: two buttons land in separate columns and are independently clickable. ---
    {
        release();
        int aHits = 0, bHits = 0;
        auto frame = [&](float mx, bool down) {
            OkayUI::Input a; a.mouseX = mx; a.mouseY = 56; a.mouseDown = down;   // content row
            OkayUI::BeginFrame(a);
            OkayUI::Begin("ColW", 10, 10, 240, 140);
            OkayUI::Columns(2);
            if (OkayUI::Button("A")) aHits++;
            OkayUI::NextColumn();
            if (OkayUI::Button("B")) bHits++;
            OkayUI::EndColumns();
            OkayUI::End();
            endFrame();
        };
        // "A" is at the left (~x 20..60); "B" is in the second column (~x 130..170).
        frame(30, false); frame(30, true); frame(30, false);    // click A
        frame(140, false); frame(140, true); frame(140, false); // click B
        CHECK(aHits == 1);
        CHECK(bHits == 1);
    }

    // --- VSliderFloat: dragging to the top gives max, to the bottom gives min. ---
    {
        release();
        float val = 50.0f;
        auto drag = [&](float my, bool down) {
            OkayUI::Input a; a.mouseX = 30; a.mouseY = my; a.mouseDown = down;
            OkayUI::BeginFrame(a);
            OkayUI::Begin("VSW", 10, 10, 120, 160);
            OkayUI::VSliderFloat("v", 20, 80, &val, 0.0f, 100.0f);
            OkayUI::End(); endFrame();
        };
        drag(50, true); drag(50, true);      // grab near the top of the groove (y~48..128)
        CHECK(val > 80.0f);
        drag(126, true); drag(126, true);    // drag to the bottom
        CHECK(val < 20.0f);
        drag(126, false);
    }

    // --- Knob: dragging up increases the value. ---
    {
        release();
        float k = 50.0f;
        auto dragK = [&](float my, bool down) {
            OkayUI::Input a; a.mouseX = 40; a.mouseY = my; a.mouseDown = down;
            OkayUI::BeginFrame(a);
            OkayUI::Begin("KW", 10, 10, 160, 140);
            OkayUI::Knob("k", &k, 0.0f, 100.0f);
            OkayUI::End(); endFrame();
        };
        dragK(80, true);    // press on the dial
        dragK(40, true);    // move the cursor up ~40px -> value rises
        CHECK(k > 55.0f);
        dragK(40, false);
    }

    // --- Image: a textured quad samples the bound texture. ---
    {
        // Build a small solid-green texture and draw it; expect green pixels on screen.
        std::uint8_t green[16 * 4];
        for (int i = 0; i < 16; ++i) { green[i * 4] = 0; green[i * 4 + 1] = 255; green[i * 4 + 2] = 0; green[i * 4 + 3] = 255; }   // RGBA opaque green
        okay::platform::TextureDesc td; td.width = 4; td.height = 4;
        okay::ui2d::Tex tex = g_cv.p->CreateTexture(td, green);
        CHECK(tex != nullptr);
        if (tex) {
            clearBlack();
            OkayUI::BeginFrame(OkayUI::Input{});
            OkayUI::Begin("IMG", 4, 4, 120, 80);
            OkayUI::Image(tex, 40, 30);
            OkayUI::End(); endFrame();
            lockPixels();
            int greenN = 0;
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                std::uint32_t px = pixelAt(x, y);
                std::uint8_t rr = (px >> 16) & 0xFF, gg = (px >> 8) & 0xFF, bb = px & 0xFF;
                if (gg > 180 && rr < 80 && bb < 80) ++greenN;
            }
            CHECK(greenN > 300);   // the ~40x30 image rasterized green
            g_cv.p->DestroyTexture(tex);
        }
    }

    // --- Rounded corners: a rounded panel leaves its very corner as background. ---
    {
        float savedRound = OkayUI::Style().rounding;
        OkayUI::Style().rounding = 12.0f;
        clearBlack();
        OkayUI::BeginFrame(OkayUI::Input{});
        OkayUI::Begin("RR", 20, 20, 120, 60);   // body spans (20,20)-(140,80)
        OkayUI::End(); endFrame();
        lockPixels();
        std::uint32_t corner = pixelAt(22, 77);  // bottom-left: inside the box, outside the arc
        std::uint32_t center = pixelAt(80, 50);  // well inside -> filled
        CHECK((corner & 0x00FFFFFFu) == 0);     // corner cut away: still background
        CHECK((center & 0x00FFFFFFu) != 0);     // center filled by the panel
        OkayUI::Style().rounding = savedRound;
    }

    // --- ColorPicker3: hue bar and SV square edit the color. ---
    {
        float rgb[3] = {1.0f, 0.0f, 0.0f};   // start red
        float svx = 0, svy = 0, side = 0;     // the square's top-left and side, read from the layout as the picker lays it out
        auto picker = [&]() {
            OkayUI::Begin("CPW", 10, 10, 260, 400);
            const OkayUI::Vec2 cp = OkayUI::GetCursorScreenPos();
            svx = cp.x; svy = cp.y;
            side = std::max(48.0f, std::min(OkayUI::GetContentRegionAvail().x - 26.0f, OkayUI::GetFrameHeight() * 7.0f));
            OkayUI::ColorPicker3("##c", rgb);
            OkayUI::End(); endFrame();
        };
        auto click = [&](float mx, float my) {
            release();
            OkayUI::Input a; a.mouseX = mx; a.mouseY = my; a.mouseDown = true;
            OkayUI::BeginFrame(a); picker();
            a.mouseDown = false;
            OkayUI::BeginFrame(a); picker();
        };
        { OkayUI::Input a; OkayUI::BeginFrame(a); picker(); }   // lay out once to learn the geometry
        // Hue bar (right of the square, 18 px wide) about a third down -> green range.
        click(svx + side + 8.0f + 9.0f, svy + side * 0.33f);
        CHECK(rgb[1] > rgb[0]);
        CHECK(rgb[1] > rgb[2]);
        // SV square top-left (low saturation, high value) -> desaturated/bright: blue rises.
        click(svx + side * 0.1f, svy + side * 0.1f);
        CHECK(rgb[2] > 0.4f);
    }

    // --- Table: resizable columns; dragging a border widens column 0. ---
    {
        release();
        const char* hdr[] = {"A", "B"};
        auto frameAndMeasure = [&](float mx, float my, bool down) {
            clearBlack();
            OkayUI::Input a; a.mouseX = mx; a.mouseY = my; a.mouseDown = down;
            OkayUI::BeginFrame(a);
            OkayUI::Begin("TBLW", 10, 10, 300, 160);
            if (OkayUI::BeginTable("T", 2, hdr)) {
                OkayUI::TableNextRow();
                OkayUI::TableNextColumn(); OkayUI::ProgressBar(1.0f);   // col 0 fills its width
                OkayUI::TableNextColumn(); OkayUI::Text("x");
                OkayUI::EndTable();
            }
            OkayUI::End(); endFrame();
            // Rightmost accent (blue) pixel in the data-row band (below the header).
            lockPixels();
            int maxx = -1;
            for (int y = 78; y < 108 && y < H; ++y) for (int x = 0; x < W; ++x) {
                std::uint32_t px = pixelAt(x, y);
                std::uint8_t rr = (px >> 16) & 0xFF, gg = (px >> 8) & 0xFF, bb = px & 0xFF;
                if (bb > 180 && bb > rr + 40 && gg > 80 && x > maxx) maxx = x;
            }
            return maxx;
        };
        int before = frameAndMeasure(0, 0, false);       // baseline: col0 ~half width
        // Border between the two columns sits near x=160. Press then drag it right.
        frameAndMeasure(160, 90, true);
        frameAndMeasure(200, 90, true);                   // drag +40 -> col0 grows
        frameAndMeasure(200, 90, false);                  // release
        int after = frameAndMeasure(0, 0, false);
        CHECK(before > 0 && after > 0);
        CHECK(after > before + 15);                       // column 0 got wider
    }

    // --- Drag and drop: drag item A onto item B delivers A's payload. ---
    {
        release();
        int got = -1;
        auto frame = [&](float mx, float my, bool down) {
            OkayUI::Input a; a.mouseX = mx; a.mouseY = my; a.mouseDown = down;
            OkayUI::BeginFrame(a);
            OkayUI::Begin("DDW", 10, 10, 200, 160);
            OkayUI::Selectable("A", false); OkayUI::DragSource(7);
            OkayUI::Selectable("B", false); bool d = OkayUI::DropTarget(&got);
            OkayUI::End(); endFrame();
            return d;
        };
        // Row A ~ y[48,76], row B ~ y[82,110]. Press on A, drag down onto B, release.
        frame(40, 60, true);              // arm on A
        frame(45, 78, true);              // move -> active drag
        frame(40, 95, true);              // hover B
        bool dropped = frame(40, 95, false);   // release over B
        CHECK(dropped);
        CHECK(got == 7);
    }

    // --- Context menu: right-click opens a popup; clicking an item fires + closes. ---
    {
        release();
        bool chosen = false;
        auto frame = [&](float mx, float my, bool ldown, bool rdown) {
            OkayUI::Input a; a.mouseX = mx; a.mouseY = my; a.mouseDown = ldown; a.rightDown = rdown;
            OkayUI::BeginFrame(a);
            OkayUI::Begin("CMW", 10, 10, 220, 180);
            OkayUI::Selectable("Row", false);
            if (OkayUI::BeginPopupContextItem("ctx")) {
                if (OkayUI::Selectable("Delete", false)) { chosen = true; OkayUI::CloseCurrentPopup(); }
                OkayUI::EndPopup();
            }
            OkayUI::End(); endFrame();
        };
        // Right-click the row (~y60) to open the popup at the cursor.
        frame(40, 60, false, true);
        frame(40, 60, false, false);
        // The "Delete" item sits at ~ (46,66)-(194,94). Press+release on it.
        frame(100, 80, true, false);
        frame(100, 80, false, false);
        CHECK(chosen);
    }

    // --- Fonts: the bold font lights more pixels than the default for the same text. ---
    {
        auto countLit = [&](const OkayUI::Font* f) {
            OkayUI::SetFont(f);
            clearBlack();
            OkayUI::BeginFrame(OkayUI::Input{});
            OkayUI::Begin("FW", 2, 2, 220, 80);
            OkayUI::Text("WWWW");
            OkayUI::End(); endFrame();
            lockPixels();
            int n = 0;   // count bright (text-colored) pixels; the panel/bg are dark
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
                if (((pixelAt(x, y) >> 16) & 0xFFu) > 150u) ++n;
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
        Canvas big; big.Open(PW, PH);
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
        big.Clear(24, 26, 32);
        OkayUI::Input pv; pv.mouseX = -1; pv.mouseY = -1;
        panel(pv);
        big.End();
        big.Save(argv[2]);
    }

    // ---- The launcher look: Roboto text, Slate theme, the building blocks ----------
    {
        const int LW = 420, LH = 360;
        Canvas ls;
        CHECK(ls.Open(LW, LH));
        const float classicScale = OkayUI::Style().textScale;
        { OkayUI::design::Options o; std::snprintf(o.design, sizeof o.design, "Slate"); OkayUI::design::Set(o); }   // the launcher's own design
        OkayUI::UseLauncherLook(true);
        CHECK(OkayUI::LauncherLook());
        CHECK(OkayUI::Style().panel[0] == 30 && OkayUI::Style().accent[2] == 250);   // Slate palette
        // Proportional TrueType text: "iiii" is far narrower than "WWWW" (the bitmap font made them equal).
        OkayUI::Input none; none.mouseX = -1; none.mouseY = -1;
        OkayUI::BeginFrame(none);
        CHECK(OkayUI::CalcTextWidth("iiii") * 2.0f < OkayUI::CalcTextWidth("WWWW"));
        CHECK(OkayUI::CalcTextWidth("Hello") > 20.0f && OkayUI::CalcTextWidth("Hello") < 60.0f);
        ls.End();

        auto frame = [&](const OkayUI::Input& in, bool* clickedRow, bool* clickedPrimary) {
            ls.Clear(0, 0, 0);
            OkayUI::BeginFrame(in);
            OkayUI::Begin("Lobby", 10, 10, 400, 330);
            OkayUI::Heading("Star Arena");
            OkayUI::BeginCard("players");
            bool row = OkayUI::ListItem("Maya", "Ready", "1,240");
            OkayUI::EndCard();
            bool prim = OkayUI::PrimaryButton("Ready");
            OkayUI::SameLine(); OkayUI::Badge("3 / 4");
            OkayUI::KeyValue("Ping", "23 ms");
            OkayUI::End();
            ls.End();
            if (clickedRow) *clickedRow = row;
            if (clickedPrimary) *clickedPrimary = prim;
        };
        frame(none, nullptr, nullptr);
        frame(none, nullptr, nullptr);   // the card sizes itself from the previous frame
        int aa = 0, accent = 0, raised = 0;
        unsigned char card[4]; OkayUI::CardFillColor(card);   // the card colour of the current (Slate) theme
        CHECK(card[0] == 38 && card[1] == 42 && card[2] == 51);   // Slate's raised surface
        ls.Lock();
        for (int y = 0; y < LH; ++y)
            for (int x = 0; x < LW; ++x) {
                std::uint8_t cr, cg, cb, ca; ls.Rgba(x, y, cr, cg, cb, ca);
                if (cr > 60 && cr < 200 && cg > 60 && cg < 200 && cb > 60 && cb < 220 && cr > 30 + 20) ++aa;   // anti-aliased text edges
                if (cr < 90 && cg > 130 && cg < 170 && cb > 235) ++accent;                                     // accent (heading underline, primary button)
                if (cr == card[0] && cg == card[1] && cb == card[2]) ++raised;                                 // the card
            }
        CHECK(aa > 40);
        CHECK(accent > 200);
        CHECK(raised > 500);

        // Clicks: press + release on the list row and the primary button.
        auto clickAt = [&](float mx, float my, bool* row, bool* prim) {
            OkayUI::Input dn; dn.mouseX = mx; dn.mouseY = my; dn.mouseDown = true;
            frame(dn, nullptr, nullptr);
            OkayUI::Input up; up.mouseX = mx; up.mouseY = my;
            frame(up, row, prim);
        };
        bool row = false, prim = false;
        // Find the row and the button by colour-free geometry: the row sits inside the card under
        // the heading; the primary button is the accent block below the card.
        int btnY = -1, btnX = -1;
        ls.Lock();
        for (int y = LH - 1; y >= 0 && btnY < 0; --y)
            for (int x = 20; x < 120; ++x) {
                std::uint8_t cr, cg, cb, ca; ls.Rgba(x, y, cr, cg, cb, ca);
                if (cr < 90 && cg > 130 && cb > 235) { btnY = y; btnX = x + 4; break; }
            }
        CHECK(btnY > 0);
        clickAt((float)btnX, (float)btnY - 6.0f, &row, &prim);
        CHECK(prim && !row);
        clickAt(200.0f, (float)btnY - 70.0f, &row, &prim);   // inside the card's row
        CHECK(row);

        // A toast shows in the bottom-right corner.
        OkayUI::Toast("Saved", 1, 5.0f);
        frame(none, nullptr, nullptr);
        ls.Lock();
        std::uint8_t tr, tg, tb, ta; ls.Rgba(LW - 40, LH - 40, tr, tg, tb, ta);
        CHECK(tr > 20 || tg > 20 || tb > 20);

        if (argc >= 4 && std::strcmp(argv[1], "--png") == 0) ls.Save(argv[3]);
        OkayUI::UseLauncherLook(false);
        OkayUI::design::Set(OkayUI::design::Options());   // back to the default design (bitmap text)
        CHECK(!OkayUI::LauncherLook() && OkayUI::Style().textScale == classicScale && OkayUI::Style().panel[0] == OkayUI::design::DefaultTheme().panel[0]);
    }

    // ---- The backend-neutral draw list (GetDrawData): commands cover the index buffer in order, with
    // the clip rectangle of a scrolling child and the launcher atlas, and match what EndFrame draws ----
    {
        for (int look = 0; look < 2; ++look) {
            OkayUI::UseLauncherLook(look != 0);
            OkayUI::Input in;
            OkayUI::BeginFrame(in);
            if (OkayUI::Begin("dl", 5, 5, 200, 150)) {
                OkayUI::Text("x");
                if (OkayUI::BeginChild("kid", 0, 60)) { for (int i = 0; i < 6; ++i) OkayUI::Text("row"); OkayUI::EndChild(); }
            }
            OkayUI::End();
            OkayUI::EndFrameData();
            const OkayUI::DrawData d = OkayUI::GetDrawData();
            CHECK(d.vertexCount > 0 && d.indexCount > 0 && d.indexCount % 3 == 0);
            CHECK(d.vertexStride == (int)sizeof(OkayUI::DrawVertex));
            CHECK(d.cmdCount >= 2 && d.cmds != nullptr);
            int next = 0; bool sawClip = false, sawFlat = false, sawAtlas = false;
            for (int i = 0; i < d.cmdCount; ++i) {
                const OkayUI::DrawCmd& c = d.cmds[i];
                CHECK(c.idxOffset == next && c.idxCount > 0 && c.idxCount % 3 == 0);
                next = c.idxOffset + c.idxCount;
                if (c.clipOn) { sawClip = true; CHECK(c.clipW > 0 && c.clipH > 0); }
                if (!c.texture) sawFlat = true;
                if (c.texture && c.texture == d.atlasTexture) sawAtlas = true;
            }
            CHECK(next == d.indexCount);                 // every index belongs to exactly one command
            CHECK(sawClip);                              // the child is clipped
            for (int i = 0; i < d.indexCount; ++i) CHECK(d.indices[i] >= 0 && d.indices[i] < d.vertexCount);
            if (look == 0) { CHECK(sawFlat && !sawAtlas && d.atlasRgba == nullptr); }
            else if (d.atlasRgba) { CHECK(sawAtlas && d.atlasW > 0 && d.atlasH > 0); }
        }
        OkayUI::UseLauncherLook(false);
    }

    g_cv.d.reset(); g_cv.p.reset();   // the drawer frees its textures on the painter first
    std::printf(g_fail == 0 ? "okayui: all checks passed\n" : "okayui: %d FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
