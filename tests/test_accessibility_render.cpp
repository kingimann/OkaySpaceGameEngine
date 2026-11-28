// Accessibility settings seen in rendered pixels: the engine UI renderer draws a small scene on the software painter
// (okayui/soft) with the settings off and on. Text size makes the text wider, the colour filter
// changes a known red panel's pixel, and high contrast lifts dim text off a dark panel.
// Set OKAY_A11Y_SHOTS=<dir> to also save before and after PNGs.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UITemplates.hpp"
#include "ui_soft_render.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

using namespace okay;

namespace {

constexpr int kW = 360, kH = 200;

void Build(Scene& s) {
    s.physicsEnabled = false;
    GameObject* cv = s.CreateGameObject("canvas");
    cv->AddComponent<Canvas>()->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
    auto make = [&](const char* n) { GameObject* g = s.CreateGameObject(n); g->transform->SetParent(cv->transform, false); return g; };
    auto* red = make("red")->AddComponent<UIPanel>();
    red->position = Vec2{10, 10}; red->size = Vec2{100, 60}; red->shape = UIShape::Rectangle;
    red->color = Color::FromBytes(220, 40, 30, 255);
    auto* label = make("label")->AddComponent<TextRenderer>();
    label->screenSpace = true; label->text = "Hello"; label->screenPos = Vec2{130, 10}; label->pixelSize = 2.0f;
    label->size = Vec2{220, 40}; label->color = Color::White;
    auto* dark = make("dark")->AddComponent<UIPanel>();
    dark->position = Vec2{10, 100}; dark->size = Vec2{340, 80}; dark->shape = UIShape::Rectangle;
    dark->color = Color::FromBytes(30, 30, 34, 255);
    auto* dim = make("dim")->AddComponent<TextRenderer>();
    dim->screenSpace = true; dim->text = "Dim text"; dim->screenPos = Vec2{20, 120}; dim->pixelSize = 2.0f;
    dim->size = Vec2{300, 40}; dim->color = Color::FromBytes(70, 70, 76, 255);
}

struct Frame {
    std::vector<unsigned char> rgba;
    void Pix(int x, int y, int& r, int& g, int& b) const {
        const unsigned char* p = &rgba[((std::size_t)y * kW + x) * 4];
        r = p[0]; g = p[1]; b = p[2];
    }
};

Frame Render() {
    Image shot;
    {
        Scene scene("a11y");
        Build(scene);
        okaytest::SoftRenderSceneUI(scene, kW, kH, shot, false, 20, 24, 30);
    }
    Frame f;
    if (shot.Valid()) f.rgba.assign(shot.Data(), shot.Data() + (std::size_t)kW * kH * 4);
    else f.rgba.assign((std::size_t)kW * kH * 4, 0);
    return f;
}

// How many columns, between x0 and kW, in rows y0..y1, hold a pixel brighter than `above`.
int TextWidth(const Frame& f, int x0, int y0, int y1, int above) {
    int lo = kW, hi = -1;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < kW; ++x) {
            int r, g, b; f.Pix(x, y, r, g, b);
            if (r + g + b > above) { lo = std::min(lo, x); hi = std::max(hi, x); }
        }
    return hi < lo ? 0 : hi - lo + 1;
}

int Brightest(const Frame& f, int x0, int y0, int x1, int y1) {
    int best = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) { int r, g, b; f.Pix(x, y, r, g, b); best = std::max(best, r + g + b); }
    return best;
}

void SaveShot(const Frame& f, const char* name) {
    const char* dir = std::getenv("OKAY_A11Y_SHOTS");
    if (!dir || !*dir) return;
    Image img(kW, kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) { int r, g, b; f.Pix(x, y, r, g, b); img.SetPixel(x, y, Color::FromBytes((unsigned char)r, (unsigned char)g, (unsigned char)b, 255)); }
    img.SavePNG(std::string(dir) + "/" + name + ".png");
}

// The prebuilt Settings screen with its accessibility rows, drawn at 1280 x 720 (saved when asked; checked non-empty).
bool SettingsShot(const char* name) {
    const int w = 1280, h = 720;
    UICanvas::Set((float)w, (float)h);
    Image shot;
    {
        Scene scene("settings");
        UITemplates::AddSettings(scene);
        scene.physicsEnabled = false;
        scene.Start();
        for (int i = 0; i < 30; ++i) scene.Update(0.05f);   // let the intro animations finish
        if (!okaytest::SoftRenderSceneUI(scene, w, h, shot, false, 20, 24, 30)) return false;
    }
    int lit = 0;
    for (int i = 0; i < w * h; ++i) lit += shot.Data()[(std::size_t)i * 4] > 60;
    if (const char* dir = std::getenv("OKAY_A11Y_SHOTS")) if (*dir) shot.SavePNG(std::string(dir) + "/" + name + ".png");
    return lit > 500;
}

} // namespace

int main() {
    RUN_SUITE("accessibility_render");
    Accessibility::Reset();

    const Frame base = Render();
    SaveShot(base, "1_default");
    int r, g, b;
    base.Pix(40, 40, r, g, b);
    CHECK(r == 220 && g == 40 && b == 30);                        // the red panel as authored
    const int wBase = TextWidth(base, 125, 10, 50, 300);
    CHECK(wBase > 20);                                            // the control: the label drew

    {   // Text size: the same label is wider (and the panel pixel unchanged).
        Accessibility::SetTextScale(2.0f);
        const Frame big = Render();
        SaveShot(big, "2_text_200");
        const int wBig = TextWidth(big, 125, 10, 100, 300);
        CHECK(wBig > wBase * 3 / 2);
        big.Pix(40, 40, r, g, b);
        CHECK(r == 220 && g == 40 && b == 30);
        Accessibility::SetTextScale(0.75f);
        const Frame small = Render();
        CHECK(TextWidth(small, 125, 10, 50, 300) < wBase);
        Accessibility::Reset();
    }

    {   // Colour filter: the known red panel pixel moves to the daltonized colour.
        Accessibility::SetColorblind(ColorblindMode::Deuteranopia, 1.0f);
        const Frame f = Render();
        SaveShot(f, "3_deuteranopia");
        f.Pix(40, 40, r, g, b);
        const Color expect = DaltonizeColor(Color::FromBytes(220, 40, 30, 255), ColorblindMode::Deuteranopia, 1.0f);
        CHECK(!(r == 220 && g == 40 && b == 30));
        CHECK(std::abs(r - (int)(expect.r * 255.0f + 0.5f)) <= 2);
        CHECK(std::abs(g - (int)(expect.g * 255.0f + 0.5f)) <= 2);
        CHECK(std::abs(b - (int)(expect.b * 255.0f + 0.5f)) <= 2);
        f.Pix(1, 1, r, g, b);
        CHECK(r == 20 && g == 24 && b == 30);                     // the scene's own clear colour is not UI
        Accessibility::Reset();
    }

    {   // High contrast: dim text on a dark panel gets much brighter.
        const int before = Brightest(base, 18, 118, 200, 150);
        Accessibility::SetHighContrast(true);
        const Frame f = Render();
        SaveShot(f, "4_high_contrast");
        const int after = Brightest(f, 18, 118, 200, 150);
        CHECK(before < 300);
        CHECK(after > 500);
        CHECK(Brightest(f, 12, 102, 17, 106) == Brightest(base, 12, 102, 17, 106));   // the panel itself is unchanged
        Accessibility::Reset();
    }

    {   // The Settings screen itself (the controls the player uses), before and after.
        CHECK(SettingsShot("5_settings_default"));
        Accessibility::SetTextScale(1.5f);
        Accessibility::SetColorblind(ColorblindMode::Deuteranopia, 1.0f);
        CHECK(SettingsShot("6_settings_150_deuteranopia"));
        Accessibility::Reset();
        UICanvas::Set(1280, 720);
    }

    {   // Everything off again draws the first frame exactly.
        const Frame again = Render();
        CHECK(again.rgba == base.rgba);
    }

    TEST_MAIN_RESULT();
}
