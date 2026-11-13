// The shipped Settings screen, drawn: monitor, display mode, resolution, refresh,
// Apply, and the "keep these settings?" question. Set OKAY_DISPLAY_SHOTS to a directory
// outside the source tree to also save PNGs.
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UITemplates.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"
#include <SDL.h>
#include <cstdlib>
#include <string>
#include <unordered_map>

using namespace okay;

namespace {

void Monitors() {
    DisplayMonitor office;
    office.index = 0; office.name = "Office"; office.width = 1920; office.height = 1080;
    office.modes = {{1280, 720, 60}, {1920, 1080, 60}, {1920, 1080, 144}};
    DisplayMonitor room;
    room.index = 1; room.name = "Living room"; room.x = 1920; room.width = 2560; room.height = 1440;
    room.modes = {{1920, 1080, 60}, {2560, 1440, 60}, {2560, 1440, 120}};
    DisplaySettings::SetMonitors({office, room});
}

struct Shot {
    SDL_Surface* sf = nullptr;
    SDL_Renderer* rn = nullptr;
    std::unordered_map<std::string, SDL_Texture*> cache;
    ui2d::SDLPainter* painter = nullptr;
    static constexpr int W = 1280, H = 720;
    bool Open() {
        sf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
        rn = sf ? SDL_CreateSoftwareRenderer(sf) : nullptr;
        if (!rn) return false;
        painter = new ui2d::SDLPainter(rn, &cache, "");
        return true;
    }
    int Draw(Scene& scene, const char* name) {
        UICanvas::Set((float)W, (float)H);
        SDL_SetRenderDrawColor(rn, 20, 24, 30, 255);
        SDL_RenderClear(rn);
        SDL_SetRenderDrawBlendMode(rn, SDL_BLENDMODE_BLEND);
        ui2d::RenderSceneUI(scene, *painter, W, H);
        SDL_RenderPresent(rn);
        int lit = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) lit += ((const unsigned char*)sf->pixels)[y * sf->pitch + x * 4] > 60;
        if (const char* dir = std::getenv("OKAY_DISPLAY_SHOTS")) {
            if (*dir) {
                Image img(W, H);
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x) {
                        const unsigned char* p = (const unsigned char*)sf->pixels + y * sf->pitch + x * 4;
                        img.SetPixel(x, y, Color::FromBytes(p[0], p[1], p[2], 255));
                    }
                img.SavePNG(std::string(dir) + "/" + name + ".png");
            }
        }
        return lit;
    }
    ~Shot() {
        delete painter;
        for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
        if (rn) SDL_DestroyRenderer(rn);
        if (sf) SDL_FreeSurface(sf);
    }
};

bool Click(Scene& scene, const char* name) {
    GameObject* go = scene.Find(name);
    auto* btn = go ? go->GetComponent<UIButton>() : nullptr;
    if (!btn) return false;
    Vec2 o, sz;
    if (!GetUIScreenRect(go, 1280, 720, o, sz)) return false;
    const Vec2 c{o.x + sz.x * 0.5f, o.y + sz.y * 0.5f};
    Input::FeedMouse(c, 0);
    Input::FeedMouse(c, 1u);
    btn->Update(0.016f);
    if (auto* screen = scene.Find("Settings")->GetComponent<SettingsScreen>()) screen->LateUpdate(0.016f);
    Input::FeedMouse({0, 0}, 0);
    return true;
}

} // namespace

int main() {
    RUN_SUITE("display_screen");
    Prefs::Clear();
    DisplaySettings::ResetToDefaults();
    Monitors();
    UICanvas::Set(1280, 720);
    UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 1280, 720, o, sz); };

    Scene scene("Settings");
    scene.physicsEnabled = false;
    GameObject* root = UITemplates::AddSettings(scene);
    scene.Start();
    root->active = true;
    for (int i = 0; i < 8; ++i) scene.Update(0.05f);

    auto* mode = scene.Find("DisplayModeDropdown")->GetComponent<UIDropdown>();
    auto* res = scene.Find("ResolutionDropdown")->GetComponent<UIDropdown>();
    auto* refresh = scene.Find("RefreshDropdown")->GetComponent<UIDropdown>();
    auto* monitor = scene.Find("MonitorDropdown")->GetComponent<UIDropdown>();
    CHECK(mode && res && refresh && monitor);
    CHECK(monitor->options.size() == 2 && monitor->options[0] == "Office" && monitor->options[1] == "Living room");
    CHECK(mode->options.size() == 3 && mode->options[0] == "Windowed" && mode->options[2] == "Fullscreen");
    CHECK(res->options.size() >= 2);
    Shot shot;
    CHECK(shot.Open());
    CHECK(shot.Draw(scene, "settings-display") > 500);

    mode->value = 2;
    res->value = (int)res->options.size() - 1;
    scene.Update(0.016f);
    if (refresh->options.size() > 1) refresh->value = (int)refresh->options.size() - 1;
    scene.Update(0.016f);
    CHECK(Click(scene, "ApplyButton"));
    CHECK(DisplaySettings::TrialActive());
    CHECK(DisplaySettings::Mode() == WindowMode::Fullscreen);
    auto* label = scene.Find("ConfirmLabel")->GetComponent<TextRenderer>();
    CHECK(label && label->text.find("Keep these settings?") == 0);
    CHECK(scene.Find("KeepButton")->active && scene.Find("RevertButton")->active);
    scene.Update(0.016f);
    CHECK(shot.Draw(scene, "settings-keep") > 500);

    CHECK(Click(scene, "RevertButton"));
    CHECK(!DisplaySettings::TrialActive() && DisplaySettings::Mode() == WindowMode::Windowed);

    UIWorld().rectOf = nullptr;
    DisplaySettings::ResetToDefaults();
    Prefs::Clear();
    TEST_MAIN_RESULT();
}
