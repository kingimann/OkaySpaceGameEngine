// The focus ring in pixels: keyboard focus draws it, a click hides it, and high-visibility
// focus changes the colour and the thickness. Set OKAY_FOCUS_SHOTS to a directory to save
// focus-normal.png and focus-high-visibility.png.
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UINavigation.hpp"
#include "okay/UI/UIFocus.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include <SDL.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

using namespace okay;

namespace {

constexpr int kW = 480, kH = 280;

struct Frame {
    std::vector<unsigned char> rgba;
    void Pix(int x, int y, int& r, int& g, int& b) const {
        const unsigned char* p = &rgba[((std::size_t)y * kW + x) * 4];
        r = p[0]; g = p[1]; b = p[2];
    }
};

bool Cyan(int r, int g, int b) { return r < 40 && g > 200 && b > 180; }
bool Yellow(int r, int g, int b) { return r > 200 && g > 160 && b < 50; }

int Count(const Frame& f, int x0, int y0, int x1, int y1, bool (*match)(int, int, int)) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            int r, g, b;
            f.Pix(x, y, r, g, b);
            n += match(r, g, b) ? 1 : 0;
        }
    return n;
}

void Build(Scene& s) {
    s.physicsEnabled = false;
    GameObject* cv = s.CreateGameObject("canvas");
    cv->AddComponent<Canvas>()->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
    auto* theme = cv->AddComponent<UITheme>();
    theme->ApplyPreset((int)UITheme::Preset::SciFi);
    theme->focusPulse = false;
    theme->focusColor = Color::FromBytes(0, 255, 240, 255);
    theme->focusThickness = 3.0f;
    theme->focusPadding = 4.0f;
    auto make = [&](const char* name, float y) {
        GameObject* g = s.CreateGameObject(name);
        g->transform->SetParent(cv->transform, false);
        auto* b = g->AddComponent<UIButton>();
        b->position = {60, y};
        b->size = {200, 44};
        b->label = name;
        b->color = Color::FromBytes(36, 40, 48);
        b->hoverColor = b->color;
        b->focusColor = b->color;
        b->separateFocus = true;
        b->textColor = Color::FromBytes(220, 220, 220);
        b->hoverTextColor = b->textColor;
        return b;
    };
    make("Play", 48);
    make("Quit", 140);
}

Frame Render(SDL_Renderer* ren, SDL_Surface* surf) {
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_RenderSetClipRect(ren, nullptr);
    SDL_RenderSetScale(ren, 1.0f, 1.0f);
    {
        Scene scene("focus");
        Build(scene);
        Input::FeedKeys({});
        Input::FeedKeys({'s'});
        NavigateUI(scene);                                    // first key focuses Play
        Input::FeedKeys({});
        Input::FeedKeys({'s'});
        NavigateUI(scene);                                    // down moves to Quit
        SDL_SetRenderDrawColor(ren, 16, 18, 22, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        ui2d::SDLPainter painter(ren, &cache, "");
        ui2d::RenderSceneUI(scene, painter, kW, kH);
        SDL_RenderPresent(ren);
    }
    Frame f;
    f.rgba.resize((std::size_t)kW * kH * 4);
    for (int y = 0; y < kH; ++y)
        std::memcpy(&f.rgba[(std::size_t)y * kW * 4], (const unsigned char*)surf->pixels + y * surf->pitch, (std::size_t)kW * 4);
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    return f;
}

Frame RenderHidden(SDL_Renderer* ren, SDL_Surface* surf) {
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_RenderSetClipRect(ren, nullptr);
    {
        Scene scene("hidden");
        Build(scene);
        Input::FeedKeys({});
        Input::FeedKeys({'s'});
        NavigateUI(scene);
        Input::FeedKeys({});
        Input::FeedMouse({80, 60}, 0);
        Input::FeedMouse({80, 60}, 1u);
        NavigateUI(scene);                                    // the click hides the ring
        SDL_SetRenderDrawColor(ren, 16, 18, 22, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        ui2d::SDLPainter painter(ren, &cache, "");
        ui2d::RenderSceneUI(scene, painter, kW, kH);
        SDL_RenderPresent(ren);
    }
    Frame f;
    f.rgba.resize((std::size_t)kW * kH * 4);
    for (int y = 0; y < kH; ++y)
        std::memcpy(&f.rgba[(std::size_t)y * kW * 4], (const unsigned char*)surf->pixels + y * surf->pitch, (std::size_t)kW * 4);
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    return f;
}

Frame RenderHigh(SDL_Renderer* ren, SDL_Surface* surf) {
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_RenderSetClipRect(ren, nullptr);
    {
        Scene scene("high");
        Build(scene);
        Accessibility::SetFocusHighVisibility(true);
        Accessibility::SetFocusThicknessScale(1.5f);
        Input::FeedMouse({0, 0}, 0);
        Input::FeedKeys({});
        Input::FeedKeys({'s'});
        NavigateUI(scene);
        Input::FeedKeys({});
        Input::FeedKeys({'s'});
        NavigateUI(scene);
        SDL_SetRenderDrawColor(ren, 16, 18, 22, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        ui2d::SDLPainter painter(ren, &cache, "");
        ui2d::RenderSceneUI(scene, painter, kW, kH);
        SDL_RenderPresent(ren);
    }
    Frame f;
    f.rgba.resize((std::size_t)kW * kH * 4);
    for (int y = 0; y < kH; ++y)
        std::memcpy(&f.rgba[(std::size_t)y * kW * 4], (const unsigned char*)surf->pixels + y * surf->pitch, (std::size_t)kW * 4);
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    return f;
}

void SaveShot(const Frame& f, const char* name) {
    const char* dir = std::getenv("OKAY_FOCUS_SHOTS");
    if (!dir || !*dir) return;
    Image img(kW, kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            int r, g, b;
            f.Pix(x, y, r, g, b);
            img.SetPixel(x, y, Color::FromBytes((unsigned char)r, (unsigned char)g, (unsigned char)b, 255));
        }
    img.SavePNG(std::string(dir) + "/" + name);
}

} // namespace

int main() {
    RUN_SUITE("ui_focus_render");
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, kW, kH, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    CHECK(ren != nullptr);
    if (!ren) TEST_MAIN_RESULT();
    UICanvas::Set((float)kW, (float)kH);
    UIFocus::Reset();
    Accessibility::Reset();
    Input::FeedKeys({});
    Input::FeedMouse({0, 0}, 0);

    // Quit sits at y=140. The ring is the band just outside it, not the button fill.
    const int bandY0 = 112, bandY1 = 140, bandX0 = 40, bandX1 = 280;

    const Frame normal = Render(ren, surf);
    SaveShot(normal, "focus-normal.png");
    const int cyanQuit = Count(normal, bandX0, bandY0, bandX1, bandY1, Cyan);
    const int cyanPlay = Count(normal, bandX0, 20, bandX1, 48, Cyan);
    CHECK(cyanQuit > 20);                                     // the ring is drawn on Quit
    CHECK(cyanPlay == 0);                                     // and not on Play
    CHECK(Count(normal, bandX0, bandY0, bandX1, bandY1, Yellow) == 0);

    const Frame hidden = RenderHidden(ren, surf);
    CHECK(Count(hidden, 0, 0, kW, kH, Cyan) == 0);            // a click hides the ring

    const Frame high = RenderHigh(ren, surf);
    SaveShot(high, "focus-high-visibility.png");
    const int yellow = Count(high, bandX0, bandY0 - 8, bandX1, bandY1, Yellow);
    CHECK(yellow > cyanQuit);                                 // thicker than the normal ring
    CHECK(Count(high, bandX0, bandY0, bandX1, bandY1, Cyan) == 0);

    UIFocus::Reset();
    Accessibility::Reset();
    Input::FeedKeys({});
    Input::FeedMouse({0, 0}, 0);
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    TEST_MAIN_RESULT();
}
