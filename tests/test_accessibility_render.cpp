// Accessibility settings seen in rendered pixels: the engine UI renderer draws a small scene on SDL's
// software renderer with the settings off and on. Text size makes the text wider, the colour filter
// changes a known red panel's pixel, and high contrast lifts dim text off a dark panel.
// Set OKAY_A11Y_SHOTS=<dir> to also save before and after PNGs.
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UITemplates.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <SDL.h>
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

Frame Render(SDL_Renderer* ren, SDL_Surface* surf) {
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_RenderSetClipRect(ren, nullptr);
    SDL_RenderSetScale(ren, 1.0f, 1.0f);
    {
        Scene scene("a11y");
        Build(scene);
        SDL_SetRenderDrawColor(ren, 20, 24, 30, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        ui2d::SDLPainter painter(ren, &cache, "");
        ui2d::RenderSceneUI(scene, painter, kW, kH);
        SDL_RenderPresent(ren);
    }
    Frame f; f.rgba.resize((std::size_t)kW * kH * 4);
    for (int y = 0; y < kH; ++y)
        std::memcpy(&f.rgba[(std::size_t)y * kW * 4], (const unsigned char*)surf->pixels + y * surf->pitch, (std::size_t)kW * 4);
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
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
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* rn = sf ? SDL_CreateSoftwareRenderer(sf) : nullptr;
    if (!rn) return false;
    UICanvas::Set((float)w, (float)h);
    std::unordered_map<std::string, SDL_Texture*> cache;
    {
        Scene scene("settings");
        UITemplates::AddSettings(scene);
        scene.physicsEnabled = false;
        scene.Start();
        for (int i = 0; i < 30; ++i) scene.Update(0.05f);   // let the intro animations finish
        SDL_SetRenderDrawColor(rn, 20, 24, 30, 255);
        SDL_RenderClear(rn);
        SDL_SetRenderDrawBlendMode(rn, SDL_BLENDMODE_BLEND);
        ui2d::SDLPainter painter(rn, &cache, "");
        ui2d::RenderSceneUI(scene, painter, w, h);
        SDL_RenderPresent(rn);
    }
    int lit = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) lit += ((const unsigned char*)sf->pixels)[y * sf->pitch + x * 4] > 60;
    if (const char* dir = std::getenv("OKAY_A11Y_SHOTS")) {
        Image img(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const unsigned char* p = (const unsigned char*)sf->pixels + y * sf->pitch + x * 4;
                img.SetPixel(x, y, Color::FromBytes(p[0], p[1], p[2], 255));
            }
        if (*dir) img.SavePNG(std::string(dir) + "/" + name + ".png");
    }
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    SDL_DestroyRenderer(rn);
    SDL_FreeSurface(sf);
    return lit > 500;
}

} // namespace

int main() {
    RUN_SUITE("accessibility_render");
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, kW, kH, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    CHECK(ren != nullptr);
    if (!ren) TEST_MAIN_RESULT();
    Accessibility::Reset();

    const Frame base = Render(ren, surf);
    SaveShot(base, "1_default");
    int r, g, b;
    base.Pix(40, 40, r, g, b);
    CHECK(r == 220 && g == 40 && b == 30);                        // the red panel as authored
    const int wBase = TextWidth(base, 125, 10, 50, 300);
    CHECK(wBase > 20);                                            // the control: the label drew

    {   // Text size: the same label is wider (and the panel pixel unchanged).
        Accessibility::SetTextScale(2.0f);
        const Frame big = Render(ren, surf);
        SaveShot(big, "2_text_200");
        const int wBig = TextWidth(big, 125, 10, 100, 300);
        CHECK(wBig > wBase * 3 / 2);
        big.Pix(40, 40, r, g, b);
        CHECK(r == 220 && g == 40 && b == 30);
        Accessibility::SetTextScale(0.75f);
        const Frame small = Render(ren, surf);
        CHECK(TextWidth(small, 125, 10, 50, 300) < wBase);
        Accessibility::Reset();
    }

    {   // Colour filter: the known red panel pixel moves to the daltonized colour.
        Accessibility::SetColorblind(ColorblindMode::Deuteranopia, 1.0f);
        const Frame f = Render(ren, surf);
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
        const Frame f = Render(ren, surf);
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
        const Frame again = Render(ren, surf);
        CHECK(again.rgba == base.rgba);
    }

    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    TEST_MAIN_RESULT();
}
