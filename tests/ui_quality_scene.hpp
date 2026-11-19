#pragma once
// A representative in-game UI screen on a 640x360 reference canvas ("Scale With Screen Size"), shared by the UI
// quality test (test_ui_quality), the picture tool (ui_quality_shot) and the example scene examples/ui_quality.
// It shows what the UI pass paints: text of several sizes, rounded and pill buttons with borders, soft shadows,
// vertical and horizontal gradients, a hairline and a half-pixel line, pictures drawn smaller and bigger than they
// are, a round picture, a slider, a progress bar and a toggle. Written for this engine's own tests.
#include <Okay.hpp>
#include <cmath>
#include <string>

namespace ui_quality {

using namespace okay;

constexpr int kRefW = 640, kRefH = 360;

/// A 128x128 test picture full of fine detail (a one-pixel checker in one corner, diagonal lines, rings) so a
/// picture shrunk without filtering shimmers and one shrunk with it stays smooth.
inline bool WriteTexture(const std::string& path) {
    Image img(128, 128);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            Color c = Color::FromBytes((unsigned char)(x * 2), (unsigned char)(y * 2), 190, 255);
            if (x < 64 && y < 64) c = ((x + y) & 1) ? Color::White : Color::FromBytes(10, 10, 20, 255);   // 1 px checker
            else if (x >= 64 && y < 64) c = ((x / 2 + y / 2) & 1) ? Color::FromBytes(255, 220, 80, 255) : Color::FromBytes(30, 30, 90, 255);
            else if (x < 64) { if (((x - 64 + y) % 6) == 0) c = Color::White; }
            else { const float d = std::sqrt((x - 96.0f) * (x - 96.0f) + (y - 96.0f) * (y - 96.0f)); if (std::fmod(d, 5.0f) < 1.6f) c = Color::FromBytes(255, 255, 255, 255); }
            img.SetPixel(x, y, c);
        }
    return img.SavePNG(path);
}

inline void Build(Scene& scene, const std::string& texturePath, int quality = 1) {
    scene.physicsEnabled = false;
    scene.uiFont = "";
    scene.uiQuality = quality;
    GameObject* cvo = scene.CreateGameObject("Canvas");
    Canvas* cv = cvo->AddComponent<Canvas>();
    cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
    cv->referenceResolution = {(float)kRefW, (float)kRefH};
    auto add = [&](const char* name) {
        GameObject* g = scene.CreateGameObject(name);
        g->transform->SetParent(cvo->transform, false);
        return g;
    };
    auto text = [&](const char* name, const char* s, float x, float y, float px, Color col) {
        auto* t = add(name)->AddComponent<TextRenderer>();
        t->screenSpace = true; t->text = s; t->screenPos = {x, y}; t->size = {260, 40};
        t->pixelSize = px; t->color = col; t->shadow = false; t->outline = false;
        return t;
    };
    auto panel = [&](const char* name, float x, float y, float w, float h, Color col) {
        auto* p = add(name)->AddComponent<UIPanel>();
        p->position = {x, y}; p->size = {w, h}; p->color = col;
        return p;
    };
    const Color ink = Color::FromBytes(235, 240, 250, 255);

    // Left card: a rounded panel with a soft shadow and a border; text of several sizes; buttons.
    auto* card = panel("Card", 14, 14, 300, 214, Color::FromBytes(34, 39, 54, 255));
    card->shape = UIShape::Rounded; card->cornerRadius = 12.0f; card->borderWidth = 1.0f;
    card->borderColor = Color::FromBytes(84, 98, 134, 255);
    card->shadow = true; card->shadowOffset = {0, 6}; card->shadowSoftness = 16.0f; card->shadowColor = Color::FromBytes(0, 0, 0, 170);
    text("T12", "Small text 123 fi", 26, 24, 1.0f, ink);
    text("T16", "Body text 456 Wag", 26, 38, 1.5f * 0.8f, ink);
    text("T20", "Heading Quick fox", 26, 54, 1.9f * 0.8f, ink);
    text("T30", "Big Title", 26, 74, 3.0f * 0.8f, Color::FromBytes(255, 214, 102, 255));

    auto* b1 = add("Play")->AddComponent<UIButton>();
    b1->position = {26, 112}; b1->size = {124, 30}; b1->label = "Play"; b1->shape = UIShape::Rounded; b1->cornerRadius = 8.0f; b1->fontScale = 1.4f;
    b1->color = Color::FromBytes(66, 140, 250, 255); b1->textColor = Color::White;
    auto* b2 = add("Options")->AddComponent<UIButton>();
    b2->position = {160, 112}; b2->size = {140, 30}; b2->label = "Options"; b2->shape = UIShape::Pill; b2->fontScale = 1.4f;
    b2->color = Color::FromBytes(54, 60, 80, 255); b2->textColor = ink; b2->borderWidth = 1.0f; b2->borderColor = Color::FromBytes(120, 140, 190, 255);
    auto* b3 = add("Quit")->AddComponent<UIButton>();
    b3->position = {26, 150}; b3->size = {124, 24}; b3->label = "Quit"; b3->shape = UIShape::Rounded; b3->cornerRadius = 5.0f; b3->fontScale = 1.2f;
    b3->color = Color::FromBytes(200, 70, 70, 255); b3->textColor = Color::White;

    auto* tg = add("Toggle")->AddComponent<UIToggle>();
    tg->position = {160, 152}; tg->size = {44, 22}; tg->on = true;

    auto* sl = add("Volume")->AddComponent<UISlider>();
    sl->position = {26, 186}; sl->size = {200, 10}; sl->value = 0.62f; sl->trackShape = UIShape::Pill; sl->roundKnob = true; sl->knobSize = 1.6f;
    sl->background = Color::FromBytes(46, 52, 72, 255); sl->fill = Color::FromBytes(66, 140, 250, 255);
    auto* pb = add("Health")->AddComponent<UIProgressBar>();
    pb->position = {26, 206}; pb->size = {200, 10}; pb->value = 0.7f; pb->cornerRadius = 5.0f;

    // Right column: pictures, gradients, thin lines.
    auto* big = add("PictureDown")->AddComponent<UIImage>();     // 128 shown at 36: needs a filtered shrink
    big->position = {336, 14}; big->size = {36, 36}; big->texture = texturePath;
    auto* mid = add("PictureDown2")->AddComponent<UIImage>();    // 128 shown at 61 (about half)
    mid->position = {380, 14}; mid->size = {61, 61}; mid->texture = texturePath;
    auto* up = add("PictureUp")->AddComponent<UIImage>();        // 128 shown at 128 (1:1 at 1x)
    up->position = {450, 14}; up->size = {96, 96}; up->texture = texturePath;
    auto* rnd = add("PictureRound")->AddComponent<UIImage>();
    rnd->position = {556, 14}; rnd->size = {64, 64}; rnd->texture = texturePath; rnd->shape = UIShape::Circle;

    auto* g1 = panel("GradV", 336, 90, 280, 36, Color::FromBytes(40, 44, 60, 255));
    g1->useGradient = true; g1->colorBottom = Color::FromBytes(24, 26, 38, 255); g1->shape = UIShape::Rounded; g1->cornerRadius = 8.0f;
    auto* g2 = panel("GradH", 336, 134, 280, 20, Color::FromBytes(30, 34, 50, 255));
    g2->useGradient = true; g2->gradientDir = UIPanel::GradientDir::Horizontal; g2->colorBottom = Color::FromBytes(48, 58, 96, 255);

    panel("Line1", 336.0f, 166.0f, 120, 1, Color::FromBytes(230, 230, 240, 255));
    panel("LineHalf", 336.5f, 172.5f, 120, 1, Color::FromBytes(230, 230, 240, 255));
    panel("Line2", 336.0f, 180.0f, 120, 2, Color::FromBytes(230, 230, 240, 255));
    auto* dot = panel("Dot", 470, 164, 24, 24, Color::FromBytes(250, 200, 70, 255)); dot->shape = UIShape::Circle;
    auto* pill = panel("PillBar", 502, 168, 80, 14, Color::FromBytes(70, 200, 140, 255)); pill->shape = UIShape::Pill;
    auto* ring = panel("Ring", 592, 160, 28, 28, Color::FromBytes(0, 0, 0, 0)); ring->shape = UIShape::Circle; ring->borderWidth = 2.0f; ring->borderColor = Color::FromBytes(255, 120, 120, 255);

    // A second card with a shadow on a lighter ground (shadow edges read best there).
    auto* ground = panel("Ground", 336, 206, 284, 140, Color::FromBytes(200, 206, 222, 255));
    ground->shape = UIShape::Rounded; ground->cornerRadius = 6.0f;
    auto* c2 = panel("Card2", 352, 222, 120, 90, Color::FromBytes(250, 251, 255, 255));
    c2->shape = UIShape::Rounded; c2->cornerRadius = 14.0f; c2->shadow = true; c2->shadowOffset = {0, 8}; c2->shadowSoftness = 18.0f; c2->shadowColor = Color::FromBytes(20, 30, 60, 150);
    text("DarkText", "Dark on light", 490, 240, 1.2f, Color::FromBytes(30, 36, 52, 255));
    text("DarkSmall", "small dark text 12px", 490, 262, 1.0f, Color::FromBytes(30, 36, 52, 255));
    text("DarkTiny", "tiny 9px ghijk", 490, 280, 0.8f, Color::FromBytes(30, 36, 52, 255));
    text("LightTiny", "Light tiny text on dark ground", 26, 240, 0.9f, ink);
    text("LightSmall", "Light small text, quick brown fox jumps over", 26, 256, 1.0f, ink);
    text("LightMid", "Medium: The Quick Brown Fox", 26, 274, 1.5f * 0.8f, ink);
}

/// The example scene (examples/ui_quality): the same small card drawn by three canvases at a scale of 1x, 1.5x and 2x, side by
/// side on a 1280x720 window, so the sharpness at each scale can be compared at a glance.
inline void BuildExample(Scene& scene, const std::string& texturePath, int quality = 1) {
    scene.physicsEnabled = false;
    scene.uiFont = "";
    scene.uiQuality = quality;
    const float scales[3] = {1.0f, 1.5f, 2.0f};
    float screenX = 24.0f;   // where each copy starts on the screen
    for (int k = 0; k < 3; ++k) {
        const float S = scales[k];
        GameObject* cvo = scene.CreateGameObject(k == 0 ? "Canvas 1x" : (k == 1 ? "Canvas 1.5x" : "Canvas 2x"));
        Canvas* cv = cvo->AddComponent<Canvas>();
        cv->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
        cv->scaleFactor = S;
        const float ox = screenX / S, oy = 24.0f / S;
        auto add = [&](const char* name) {
            GameObject* g = scene.CreateGameObject(name);
            g->transform->SetParent(cvo->transform, false);
            return g;
        };
        auto* card = add("Card")->AddComponent<UIPanel>();
        card->position = {ox, oy}; card->size = {200, 168}; card->shape = UIShape::Rounded; card->cornerRadius = 10.0f;
        card->color = Color::FromBytes(34, 39, 54, 255); card->borderWidth = 1.0f; card->borderColor = Color::FromBytes(84, 98, 134, 255);
        card->shadow = true; card->shadowOffset = {0, 5}; card->shadowSoftness = 14.0f; card->shadowColor = Color::FromBytes(0, 0, 0, 170);
        auto text = [&](const char* name, const char* str, float x, float y, float px, Color col) {
            auto* t = add(name)->AddComponent<TextRenderer>();
            t->screenSpace = true; t->text = str; t->screenPos = {ox + x, oy + y}; t->size = {180, 20};
            t->pixelSize = px; t->color = col; t->shadow = false; t->outline = false;
        };
        const Color ink = Color::FromBytes(235, 240, 250, 255);
        text("Title", "Crisp UI", 12, 10, 2.0f * 0.8f, Color::FromBytes(255, 214, 102, 255));
        text("Body", "The quick brown fox jumps", 12, 32, 1.0f, ink);
        text("Small", "small 9 px text, 0123456789", 12, 46, 0.8f, ink);
        auto* b = add("Button")->AddComponent<UIButton>();
        b->position = {ox + 12, oy + 64}; b->size = {120, 26}; b->label = "Play"; b->shape = UIShape::Rounded; b->cornerRadius = 8.0f; b->fontScale = 1.3f;
        b->color = Color::FromBytes(66, 140, 250, 255); b->textColor = Color::White; b->borderWidth = 1.0f; b->borderColor = Color::FromBytes(150, 190, 255, 255);
        auto* sl = add("Slider")->AddComponent<UISlider>();
        sl->position = {ox + 12, oy + 102}; sl->size = {150, 8}; sl->value = 0.6f; sl->trackShape = UIShape::Pill; sl->roundKnob = true; sl->knobSize = 1.6f;
        sl->background = Color::FromBytes(46, 52, 72, 255); sl->fill = Color::FromBytes(66, 140, 250, 255);
        auto* pb = add("Bar")->AddComponent<UIProgressBar>();
        pb->position = {ox + 12, oy + 122}; pb->size = {150, 8}; pb->value = 0.7f; pb->cornerRadius = 4.0f;
        auto* pic = add("Picture")->AddComponent<UIImage>();     // 128 px picture shown at 28
        pic->position = {ox + 12, oy + 138}; pic->size = {24, 24}; pic->texture = texturePath;
        auto* rnd = add("Round")->AddComponent<UIImage>();
        rnd->position = {ox + 44, oy + 138}; rnd->size = {24, 24}; rnd->texture = texturePath; rnd->shape = UIShape::Circle;
        auto* line = add("Line")->AddComponent<UIPanel>();
        line->position = {ox + 80, oy + 150}; line->size = {100, 1}; line->color = Color::FromBytes(230, 230, 240, 255);
        screenX += 200.0f * S + 24.0f;
    }
}

} // namespace ui_quality
