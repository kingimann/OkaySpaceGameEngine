#pragma once
// A small UI scene on a 320x180 reference canvas ("Scale With Screen Size"), used by the crisp-UI
// test (test_ui_crisp) and the picture tool (ui_crisp_shot). Drawn at 1x, 2x, 3x or 4x it shows
// every kind of thing the UI pass paints: rounded panel and button, text, a slider, a picture and
// a round picture, a joystick and a round minimap (circles and rings), and a crosshair ring.
#include <Okay.hpp>
#include <string>

namespace ui_crisp {

using namespace okay;

constexpr int kRefW = 320, kRefH = 180;

/// A 16x16 test picture (diagonal colour ramp, a ring and a hard cross) written to `path`.
inline bool WriteTexture(const std::string& path) {
    Image img(16, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const float dx = x - 7.5f, dy = y - 7.5f, d = std::sqrt(dx * dx + dy * dy);
            Color c = Color::FromBytes((unsigned char)(x * 16), (unsigned char)(y * 16), 200, 255);
            if (d > 5.0f && d < 7.0f) c = Color::FromBytes(255, 240, 120, 255);
            if (x == 8 || y == 8) c = Color::FromBytes(20, 20, 30, 255);
            img.SetPixel(x, y, c);
        }
    return img.SavePNG(path);
}

inline void Build(Scene& scene, const std::string& texturePath, bool pixelLook = false) {
    scene.physicsEnabled = false;
    scene.uiFont = pixelLook ? "pixel" : "";
    GameObject* cvo = scene.CreateGameObject("Canvas");
    Canvas* cv = cvo->AddComponent<Canvas>();
    cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
    cv->referenceResolution = {(float)kRefW, (float)kRefH};
    auto add = [&](const char* name) {
        GameObject* g = scene.CreateGameObject(name);
        g->transform->SetParent(cvo->transform, false);
        return g;
    };
    auto* card = add("Card")->AddComponent<UIPanel>();
    card->position = {8, 8}; card->size = {148, 112};
    card->shape = UIShape::Rounded; card->cornerRadius = 8.0f;
    card->color = Color::FromBytes(28, 32, 44, 255);
    card->borderWidth = 1.0f; card->borderColor = Color::FromBytes(70, 82, 110, 255);

    auto* title = add("Title")->AddComponent<TextRenderer>();
    title->screenSpace = true; title->text = "Crisp UI 123";
    title->screenPos = {16, 14}; title->size = {120, 12};
    title->pixelSize = pixelLook ? 1.2f : 1.5f;   // the 8x8 font is wider than the smooth one: at 1.5 the pixel-look title ran under the picture
    title->color = Color::White; title->shadow = false; title->outline = false;

    auto* btn = add("Play")->AddComponent<UIButton>();
    btn->position = {16, 40}; btn->size = {100, 24};
    btn->label = "Play"; btn->shape = UIShape::Rounded; btn->cornerRadius = 6.0f; btn->fontScale = 1.5f;
    btn->color = Color::FromBytes(66, 140, 250, 255); btn->textColor = Color::White;

    auto* sl = add("Volume")->AddComponent<UISlider>();
    sl->position = {16, 78}; sl->size = {120, 8}; sl->value = 0.6f;
    sl->trackShape = UIShape::Pill; sl->roundKnob = true; sl->knobSize = 1.4f;
    sl->background = Color::FromBytes(40, 46, 62, 255); sl->fill = Color::FromBytes(66, 140, 250, 255);
    sl->knob = Color::FromBytes(245, 247, 252, 255);

    auto* im = add("Picture")->AddComponent<UIImage>();
    im->position = {172, 8}; im->size = {48, 48}; im->texture = texturePath;

    auto* rim = add("RoundPicture")->AddComponent<UIImage>();
    rim->position = {232, 8}; rim->size = {48, 48}; rim->texture = texturePath;
    rim->shape = UIShape::Circle;

    auto* js = add("Stick")->AddComponent<VirtualJoystick>();
    js->anchor = UIAnchor::TopLeft; js->position = {172, 70}; js->size = {64, 64};
    js->onlyOnTouch = false; js->fadeWhenIdle = false;
    js->baseColor = Color::FromBytes(48, 64, 140, 255); js->knobColor = Color::FromBytes(250, 200, 70, 255);

    auto* mm = add("Map")->AddComponent<Minimap>();
    mm->position = {248, 70}; mm->size = {64, 64}; mm->circular = true; mm->borderWidth = 2.0f;
    mm->background = Color::FromBytes(20, 70, 40, 255); mm->border = Color::FromBytes(230, 230, 240, 255);

    auto* cr = add("Reticle")->AddComponent<Crosshair>();
    cr->anchor = UIAnchor::BottomLeft; cr->position = {40, -30}; cr->showLines = false; cr->dot = false;
    cr->circle = true; cr->circleRadius = 14.0f; cr->circleThickness = 2.0f;
    cr->circleColor = Color::FromBytes(255, 90, 90, 255);
}

} // namespace ui_crisp
