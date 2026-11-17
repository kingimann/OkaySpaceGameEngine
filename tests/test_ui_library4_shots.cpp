// Headless pictures (software renderer, same UI pass as the game; not a real window) of UI library slice 4: the themes side by side, the
// icon sheet, the style sheet, and a picture of every screen. They are written to <build>/tests/ui_library4/ (and to a folder given as the
// first argument or in the OKAY_SHOT_DIR environment variable; pictures are never kept in the repository). Written for this engine; no other project's code or art.
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UILibrary.hpp"
#include "okay/UI/UILayoutUpdate.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Components/UINavigation.hpp"
#include <SDL.h>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

using namespace okay;

namespace {

struct Shot { bool ok = false; Image img{1, 1}; };

// One software renderer for the whole process, as the player has one: the glyph atlas texture of a font is made for the first renderer and kept
// (UIPainterSDL.hpp SDLFontAtlas), so a second renderer would draw no text. Every picture is drawn into the top-left corner of one big surface.
struct Canvas3x { SDL_Surface* surf = nullptr; SDL_Renderer* ren = nullptr; };
Canvas3x& Big() {
    static Canvas3x c = [] {
        Canvas3x k;
        k.surf = SDL_CreateRGBSurfaceWithFormat(0, 3840, 2160, 32, SDL_PIXELFORMAT_ABGR8888);
        k.ren = k.surf ? SDL_CreateSoftwareRenderer(k.surf) : nullptr;
        return k;
    }();
    return c;
}

Shot Render(int width, int height, const std::function<void(Scene&)>& build, bool pixelFont = false) {
    Shot out;
    Canvas3x& big = Big();
    SDL_Renderer* ren = big.ren;
    SDL_Surface* surf = big.surf;
    if (!ren || width > 3840 || height > 2160) return out;
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_Rect all{0, 0, 3840, 2160};
    SDL_RenderSetViewport(ren, &all);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 18, 22, 32, 255);
    SDL_RenderClear(ren);
    SDL_Rect vp{0, 0, width, height};
    SDL_RenderSetViewport(ren, &vp);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    Scene scene("shot");
    scene.physicsEnabled = false;
    scene.uiFont = pixelFont ? "pixel" : "";   // the default UI font is the smooth TrueType face; "pixel" (the old 8x8 bitmap) is only the opt-in look
    build(scene);
    scene.Start();
    UICanvas::Set((float)width, (float)height);
    UpdateUILayouts(scene);
    scene.Update(0.016f);
    ui2d::SDLPainter painter(ren, &cache, "");
    ui2d::RenderSceneUI(scene, painter, width, height);
    SDL_RenderPresent(ren);
    out.img = Image(width, height);
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = (const unsigned char*)surf->pixels + y * (int)surf->pitch;
        for (int x = 0; x < width; ++x) out.img.SetPixel(x, y, Color::FromBytes(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]));
    }
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    int drawn = 0;
    const unsigned char* px = out.img.Data();
    for (int i = 0; i < width * height; ++i) if (px[i * 4] != 18 || px[i * 4 + 1] != 22 || px[i * 4 + 2] != 32) ++drawn;
    out.ok = drawn >= 40;
    return out;
}

// Copy `src` scaled by 1/k (box average) into `dst` at (dx, dy).
void Blit(Image& dst, const Image& src, int dx, int dy, int k) {
    for (int y = 0; y + k <= src.Height(); y += k)
        for (int x = 0; x + k <= src.Width(); x += k) {
            float r = 0, g = 0, b = 0;
            for (int j = 0; j < k; ++j) for (int i = 0; i < k; ++i) { const Color c = src.GetPixel(x + i, y + j); r += c.r; g += c.g; b += c.b; }
            const float n = (float)(k * k);
            if (dx + x / k < dst.Width() && dy + y / k < dst.Height()) dst.SetPixel(dx + x / k, dy + y / k, Color(r / n, g / n, b / n, 1.0f));
        }
}

// One small sample screen used to compare themes: a card with a title, three buttons, a slider, a bar and a toggle.
void Sample(Scene& s, const std::string& theme) {
    using namespace UITemplates; using namespace UITemplates::detail;
    GameObject* canvas = EnsureCanvas(s);
    if (Canvas* cv = canvas->GetComponent<Canvas>()) cv->referenceResolution = {320.0f, 220.0f};
    GameObject* c = Panel(s, canvas, "Card", UIAnchor::StretchFull, {6, 6}, {6, 6}, slate::Panel(), 12.0f, true);
    Text(s, c, "T", theme, UIAnchor::TopLeft, {12, 8}, {296, 26}, 18.0f, slate::Text(), 0, true);
    Button(s, c, "B1", "Play", UIAnchor::TopLeft, {12, 44}, {92, 34});
    Button(s, c, "B2", "More", UIAnchor::TopLeft, {112, 44}, {92, 34}, ButtonKind::Secondary);
    Button(s, c, "B3", "Quit", UIAnchor::TopLeft, {212, 44}, {92, 34}, ButtonKind::Danger);
    GameObject* sl = Child(s, c, "S"); auto* slc = sl->AddComponent<UISlider>(); slc->anchor = UIAnchor::TopLeft; slc->position = {12, 100}; slc->size = {200, 10}; slc->value = 0.6f;
    GameObject* pb = Child(s, c, "P"); auto* pbc = pb->AddComponent<UIProgressBar>(); pbc->anchor = UIAnchor::TopLeft; pbc->position = {12, 130}; pbc->size = {200, 12}; pbc->value = 0.7f;
    GameObject* tg = Child(s, c, "G"); auto* tgc = tg->AddComponent<UIToggle>(); tgc->anchor = UIAnchor::TopLeft; tgc->position = {12, 160}; tgc->size = {44, 24}; tgc->style = UIToggle::Style::Switch; tgc->on = true; tgc->label = "";
    Text(s, c, "L", "Music", UIAnchor::TopLeft, {66, 160}, {120, 24}, 14.0f, slate::Dim());
    UIStyleLib::ApplyLook(c, theme);
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("ui_library4_shots");
    std::filesystem::current_path(OKAY_SOURCE_DIR);   // so the shipped font (assets/fonts/DejaVuSans.ttf) is found like in the editor
    std::string dir = std::string(OKAY_TEST_OUTPUT_DIR) + "/ui_library4";
    if (const char* env = std::getenv("OKAY_SHOT_DIR")) if (env[0]) dir = env;   // pictures go outside the repository
    if (argc >= 2 && argv[1][0]) dir = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(dir + "/screens", ec);
    CHECK(!ec);

    {   // before / after: the old pixel font and the smooth default, at 1x and at 3x zoom (a crop of the 3x render, drawn at its own pixel size)
        struct Tile { const char* name; std::function<void(Scene&)> build; float cx, cy; };   // cx, cy: the crop centre as a fraction of the screen
        const std::vector<Tile> tiles = {
            {"main-menu", [](Scene& s) { UIScreens::Build(s, "main_menu", 0, "Midnight Blue"); }, 0.5f, 0.5f},
            {"settings", [](Scene& s) { UIScreens::Build(s, "settings", 0, "Midnight Blue"); }, 0.5f, 0.3f},
            {"hud", [](Scene& s) { UIScreens::Build(s, "hud_shooter", 0, "Midnight Blue"); }, 0.12f, 0.9f},
            {"icons", [](Scene& s) {
                 using namespace UITemplates; using namespace UITemplates::detail;
                 GameObject* canvas = EnsureCanvas(s);
                 if (Canvas* cv = canvas->GetComponent<Canvas>()) cv->referenceResolution = {1280.0f, 720.0f};
                 Panel(s, canvas, "Bg", UIAnchor::StretchFull, {0, 0}, {0, 0}, Color::FromBytes(28, 32, 44, 255), 0.0f);
                 static const char* n[] = {"heart", "sword", "shield", "gear", "star", "coin", "key", "map", "bell", "fire", "gem", "lock"};
                 for (int i = 0; i < 12; ++i) UIScreens::detail3::AddIconPicture(s, canvas, ("I" + std::to_string(i)).c_str(), n[i], UIAnchor::TopLeft, {440.0f + (float)(i % 4) * 110.0f, 250.0f + (float)(i / 4) * 80.0f}, 64.0f, slate::Text());
                 Button(s, canvas, "B", "Play", UIAnchor::TopLeft, {540, 520}, {200, 54});
             }, 0.5f, 0.55f}};
        for (int zoom : {1, 3}) {
            const int W = 1280 * zoom, H = 720 * zoom, tw = 640, th = 360;
            Image sheet(tw * 2 + 30, th * (int)tiles.size() + 10 * ((int)tiles.size() + 1));
            for (int y = 0; y < sheet.Height(); ++y) for (int x = 0; x < sheet.Width(); ++x) sheet.SetPixel(x, y, Color::FromBytes(8, 9, 12, 255));
            int ok = 0;
            for (std::size_t t = 0; t < tiles.size(); ++t)
                for (int after = 0; after < 2; ++after) {
                    Shot sh = Render(W, H, tiles[t].build, after == 0);
                    ok += sh.ok;
                    if (!sh.ok) continue;
                    int x0 = (int)(tiles[t].cx * (float)W) - tw / 2, y0 = (int)(tiles[t].cy * (float)H) - th / 2;
                    x0 = std::max(0, std::min(W - tw, x0)); y0 = std::max(0, std::min(H - th, y0));
                    for (int y = 0; y < th; ++y) for (int x = 0; x < tw; ++x) sheet.SetPixel(10 + after * (tw + 10) + x, 10 + (int)t * (th + 10) + y, sh.img.GetPixel(x0 + x, y0 + y));
                }
            CHECK(ok == (int)tiles.size() * 2);
            CHECK(sheet.SavePNG(dir + "/before-after-" + std::to_string(zoom) + "x.png"));   // left: old pixel font, right: smooth default
        }
    }

    {   // looks side by side: 24 of the 54 on one sheet (each tile is the same sample screen with another look)
        static const char* names[] = {"Dark", "Light", "Sci-fi", "Fantasy", "Neon", "Retro Pixel", "Cartoon", "Military", "Horror", "Glass", "Minimal", "Modern Flat",
                                      "Midnight Blue", "Paper", "Candy", "Forest", "Wood Plank", "Stone Keep", "Iron Forge", "Terminal Green", "Cyber Pink", "Space Station", "Mobile Clean", "High Contrast"};
        Image sheet(4 * 330, 6 * 230);
        for (int y = 0; y < sheet.Height(); ++y) for (int x = 0; x < sheet.Width(); ++x) sheet.SetPixel(x, y, Color::FromBytes(12, 14, 20, 255));
        int ok = 0;
        for (int i = 0; i < 24; ++i) {
            const std::string n = names[i];
            Shot sh = Render(320, 220, [&](Scene& s) { Sample(s, n); });
            ok += sh.ok;
            if (sh.ok) Blit(sheet, sh.img, 5 + (i % 4) * 330, 5 + (i / 4) * 230, 1);
        }
        CHECK(ok == 24);
        CHECK(sheet.SavePNG(dir + "/looks-side-by-side.png"));
    }

    {   // the icon sheet: every icon, 20 to a row, at 32 px
        const auto& icons = uilib::Icons().icons;
        const int cols = 24, cell = 44, rows = ((int)icons.size() + cols - 1) / cols;
        Shot sh = Render(cols * cell + 20, rows * cell + 20, [&](Scene& s) {
            using namespace UITemplates; using namespace UITemplates::detail;
            GameObject* canvas = EnsureCanvas(s);
            if (Canvas* cv = canvas->GetComponent<Canvas>()) { cv->scaleMode = Canvas::ScaleMode::ConstantPixelSize; }
            for (std::size_t i = 0; i < icons.size(); ++i) {
                GameObject* g = Child(s, canvas, icons[i].name.c_str());
                auto* im = g->AddComponent<UIImage>();
                im->anchor = UIAnchor::TopLeft; im->position = {10.0f + (float)(i % cols) * cell + 6, 10.0f + (float)(i / cols) * cell + 6}; im->size = {32, 32};
                im->texture = "icon:" + icons[i].name; im->color = slate::Text();
            }
        });
        CHECK(sh.ok);
        CHECK(sh.img.SavePNG(dir + "/icons-all.png"));
    }

    {   // the style sheet: 48 named styles, one button each
        const auto& styles = UIStyleLib::Styles().styles;
        const int cols = 6, cw = 200, ch = 64, n = std::min(48, (int)styles.size());
        Shot sh = Render(cols * cw + 20, ((n + cols - 1) / cols) * ch + 20, [&](Scene& s) {
            using namespace UITemplates; using namespace UITemplates::detail;
            GameObject* canvas = EnsureCanvas(s);
            if (Canvas* cv = canvas->GetComponent<Canvas>()) { cv->scaleMode = Canvas::ScaleMode::ConstantPixelSize; }
            Panel(s, canvas, "Bg", UIAnchor::StretchFull, {0, 0}, {0, 0}, Color::FromBytes(28, 32, 44, 255), 0.0f);
            const UIStyleLib::Palette pal = UIStyleLib::PaletteOf(nullptr);
            for (int i = 0; i < n; ++i) {
                const std::string nm = styles[(std::size_t)i * styles.size() / (std::size_t)n].name;   // spread over the families
                GameObject* b = Button(s, canvas, ("S" + std::to_string(i)).c_str(), nm, UIAnchor::TopLeft, {14.0f + (i % cols) * cw, 14.0f + (i / cols) * ch}, {cw - 20.0f, 44});
                b->GetComponent<UIButton>()->fontScale = 1.5f;
                UIStyleLib::ApplyStyle(b, UIStyleLib::SpecOf(nm), pal);
                b->GetComponent<UIButton>()->size.y = 44;
            }
        });
        CHECK(sh.ok);
        CHECK(sh.img.SavePNG(dir + "/styles-sheet.png"));
    }

    {   // three screens with three different looks in ONE scene: a retro menu, a sci-fi HUD and a cartoon popup
        Shot sh = Render(1280, 720, [&](Scene& s) { UILibrary::BuildMixStyles(s); });
        CHECK(sh.ok);
        CHECK(sh.img.SavePNG(dir + "/mix-styles.png"));
    }

    {   // keyboard / gamepad focus: the ring on the focused button
        Shot sh = Render(1280, 720, [&](Scene& s) {
            UIScreens::Build(s, "main_menu", 0, "Midnight Blue");
            if (GameObject* g = s.Find("SettingsButton")) SetWidgetFocused(g, true);
        });
        CHECK(sh.ok);
        CHECK(sh.img.SavePNG(dir + "/focus-ring.png"));
    }

    {   // the Controls screen with a key box waiting for a key
        Shot sh = Render(1280, 720, [&](Scene& s) {
            UIScreens::Build(s, "rebinding", 0, "Midnight Blue");
            if (GameObject* g = s.Find("Fire")) if (auto* kb = g->GetComponent<UIKeybindField>()) kb->Listen();
        });
        CHECK(sh.ok);
        CHECK(sh.img.SavePNG(dir + "/controls-listening.png"));
    }

    {   // a picture of every screen built from data (1280 x 720; a phone-only screen is drawn at 720 x 1280), two contact sheets: one look for all,
        // and one where every section has its own look
        static const std::pair<const char*, const char*> kSectionLook[] = {
            {"menus", "Midnight Blue"}, {"hud", "Space Station"}, {"items", "Wood Plank"}, {"dialogue", "Paper"}, {"social", "Candy"},
            {"popups", "Comic"}, {"forms", "Mobile Clean"}, {"mobile", "Neon"}, {"loading", "Dark"}};
        std::vector<const UIScreens::Def*> defs;
        for (const auto& d : UIScreens::Screens()) if (d.native.empty()) defs.push_back(&d);
        const int cols = 4, rows = ((int)defs.size() + cols - 1) / cols;
        Image sheetA(cols * 646, rows * 366), sheetB(cols * 646, rows * 366);
        for (Image* im : {&sheetA, &sheetB}) for (int y = 0; y < im->Height(); ++y) for (int x = 0; x < im->Width(); ++x) im->SetPixel(x, y, Color::FromBytes(10, 12, 16, 255));
        int ok = 0;
        for (std::size_t i = 0; i < defs.size(); ++i) {
            const UIScreens::Def& d = *defs[i];
            std::string sectionLook = "Midnight Blue";
            for (const auto& sl : kSectionLook) if (d.section == sl.first) sectionLook = sl.second;
            for (int variant = 0; variant < 2; ++variant) {
                const std::string look = variant == 0 ? "Midnight Blue" : sectionLook;
                const bool portrait = !d.landscape;
                Shot sh = Render(portrait ? 720 : 1280, portrait ? 1280 : 720, [&](Scene& s) { UIScreens::Build(s, d.id, 0, look); });
                if (variant == 0) ok += sh.ok;
                if (!sh.ok) { std::cout << "  no picture for screen " << d.id << "\n"; continue; }
                Image tile(640, 360);   // a portrait picture is scaled to the tile height and centred
                for (int y = 0; y < 360; ++y) for (int x = 0; x < 640; ++x) tile.SetPixel(x, y, Color::FromBytes(10, 12, 16, 255));
                if (!portrait) Blit(tile, sh.img, 0, 0, 2);
                else for (int y = 0; y < 360; ++y) for (int x = 0; x < 202; ++x) {   // 1280 -> 360 is 3.55x: the average of each block (picking one pixel made small text look like dots)
                    const int x0 = (int)((float)x * 3.556f), y0 = (int)((float)y * 3.556f), x1 = std::min(720, (int)((float)(x + 1) * 3.556f)), y1 = std::min(1280, (int)((float)(y + 1) * 3.556f));
                    float r = 0, g = 0, b = 0; int cnt = 0;
                    for (int yy = y0; yy < std::max(y1, y0 + 1); ++yy) for (int xx = x0; xx < std::max(x1, x0 + 1); ++xx) { const Color c = sh.img.GetPixel(std::min(719, xx), std::min(1279, yy)); r += c.r; g += c.g; b += c.b; ++cnt; }
                    tile.SetPixel(219 + x, y, Color(r / cnt, g / cnt, b / cnt, 1.0f));
                }
                if (variant == 0) sh.img.SavePNG(dir + "/screens/" + d.id + ".png");   // the full-size picture (a phone screen at 720 x 1280)
                Blit(variant == 0 ? sheetA : sheetB, tile, 3 + (int)(i % cols) * 646, 3 + (int)(i / cols) * 366, 1);
            }
        }
        CHECK(ok == (int)defs.size());
        CHECK(sheetA.SavePNG(dir + "/screens-contact-sheet.png"));
        CHECK(sheetB.SavePNG(dir + "/screens-contact-sheet-varied-looks.png"));
    }

    TEST_MAIN_RESULT();
}
