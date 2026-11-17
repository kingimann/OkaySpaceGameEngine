// Headless pictures (software renderer, same UI pass as the game; not a real window) of UI library slice 4: the themes side by side, the
// icon sheet, the style sheet, and a picture of every screen. They are written to <build>/tests/ui_library4/ (and to a folder given as the
// first argument, which is how docs/images/ui_library/ was made). Written for this engine; no other project's code or art.
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UILibrary.hpp"
#include "okay/UI/UILayoutUpdate.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <SDL.h>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

using namespace okay;

namespace {

struct Shot { bool ok = false; Image img{1, 1}; };

Shot Render(int width, int height, const std::function<void(Scene&)>& build) {
    Shot out;
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    if (!ren) { if (surf) SDL_FreeSurface(surf); return out; }
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 18, 22, 32, 255);
    SDL_RenderClear(ren);
    Scene scene("shot");
    scene.physicsEnabled = false;
    scene.uiFont = "pixel";
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
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
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
    std::string dir = std::string(OKAY_TEST_OUTPUT_DIR) + "/ui_library4";
    if (argc >= 2 && argv[1][0]) dir = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(dir + "/screens", ec);
    CHECK(!ec);

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

    {   // a picture of every screen built from data (1280 x 720, saved at half size) and a contact sheet of them
        std::vector<std::string> ids;
        for (const auto& d : UIScreens::Screens()) if (d.native.empty()) ids.push_back(d.id);
        Image contact(4 * 646, (((int)ids.size() + 3) / 4) * 366);
        for (int y = 0; y < contact.Height(); ++y) for (int x = 0; x < contact.Width(); ++x) contact.SetPixel(x, y, Color::FromBytes(10, 12, 16, 255));
        int ok = 0;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            Shot sh = Render(1280, 720, [&](Scene& s) {
                UIScreens::Build(s, ids[i], 0, "Midnight Blue");
                if (ids[i].rfind("hud_", 0) == 0 || ids[i] == "minimap_frame" || ids[i] == "touch_hud") {}
            });
            ok += sh.ok;
            if (!sh.ok) { std::cout << "  no picture for screen " << ids[i] << "\n"; continue; }
            Image half(640, 360);
            Blit(half, sh.img, 0, 0, 2);
            half.SavePNG(dir + "/screens/" + ids[i] + ".png");
            Blit(contact, sh.img, 3 + (int)(i % 4) * 646, 3 + (int)(i / 4) * 366, 2);
        }
        CHECK(ok == (int)ids.size());
        CHECK(contact.SavePNG(dir + "/screens-contact-sheet.png"));
    }

    TEST_MAIN_RESULT();
}
