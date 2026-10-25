#pragma once
// Scenes for the UI renderer golden-image test (test_ui_renderer_golden). Each one
// exercises a group of widgets and draw features; the test renders them with the
// engine UI renderer on SDL's software renderer and compares against PNGs in
// tests/golden/ui/ that were captured from the player's UI pass before it moved
// into the engine. Keep the scenes deterministic: no time, input or hover state.
// Not covered here (they need input, time or a camera): tooltips, the focus ring,
// world-space canvases and WorldUI labels. The drop-target highlight is drawn by
// forcing the hover flag (scroll_layers).
#include <Okay.hpp>
#include <string>

namespace ui_golden {

using namespace okay;

constexpr int kWidth = 480, kHeight = 320;

inline int SceneCount() { return 7; }

inline const char* SceneName(int i) {
    static const char* const names[] = {"panels", "images", "controls", "text",
                                        "minimap", "scroll_layers", "canvas_scaled"};
    return (i >= 0 && i < SceneCount()) ? names[i] : "";
}

/// A small procedural RGBA test texture (checker + gradient), written to `path`.
inline bool WriteTestTexture(const std::string& path) {
    Image img(32, 24);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 32; ++x) {
            bool chk = ((x / 4) + (y / 4)) % 2 == 0;
            img.SetPixel(x, y, Color::FromBytes((unsigned char)(x * 8), (unsigned char)(y * 10),
                                                chk ? 220 : 60, (x < 2 || y < 2) ? 255 : 230));
        }
    return img.SavePNG(path);
}

inline GameObject* Make(Scene& s, const char* name, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    return g;
}

inline GameObject* PixelCanvas(Scene& s, const char* name = "canvas") {
    GameObject* c = Make(s, name);
    c->AddComponent<Canvas>()->scaleMode = Canvas::ScaleMode::ConstantPixelSize;
    return c;
}

/// Build scene `i` into `s`. `tex` is a texture path from WriteTestTexture.
/// `pixelFont` selects the classic 8x8 font instead of the default UI font.
inline void Build(int i, Scene& s, const std::string& tex) {
    s.physicsEnabled = false;
    switch (i) {
    case 0: {   // panels: shapes, gradients, shadows, outline, border, highlight, corner mask
        GameObject* cv = PixelCanvas(s);
        const UIShape shapes[] = {UIShape::Rectangle, UIShape::Rounded, UIShape::Circle, UIShape::Pill,
                                  UIShape::Diamond, UIShape::Hexagon, UIShape::Squircle, UIShape::TabTop};
        for (int k = 0; k < 8; ++k) {
            auto* p = Make(s, "shape", cv)->AddComponent<UIPanel>();
            p->position = Vec2{10.0f + (k % 4) * 115.0f, 10.0f + (k / 4) * 80.0f};
            p->size = Vec2{100.0f, 64.0f};
            p->shape = shapes[k];
            p->color = Color::FromBytes((unsigned char)(40 + k * 25), 120, (unsigned char)(220 - k * 20), 255);
        }
        const UIPanel::GradientDir dirs[] = {UIPanel::GradientDir::Vertical, UIPanel::GradientDir::Horizontal,
                                             UIPanel::GradientDir::DiagonalDown, UIPanel::GradientDir::DiagonalUp};
        for (int k = 0; k < 4; ++k) {
            auto* p = Make(s, "grad", cv)->AddComponent<UIPanel>();
            p->position = Vec2{10.0f + k * 115.0f, 175.0f};
            p->size = Vec2{100.0f, 60.0f};
            p->useGradient = true;
            p->gradientDir = dirs[k];
            p->color = Color::FromBytes(250, 120, 60, 255);
            p->colorBottom = Color::FromBytes(40, 60, 200, 200);
            p->shadow = (k % 2) == 0;
            p->shadowSoftness = k == 2 ? 6.0f : 0.0f;
            p->outlineWidth = k == 1 ? 3.0f : 0.0f;
            p->borderWidth = k >= 2 ? 2.0f : 0.0f;
            p->topHighlight = k == 3;
            p->cornerMask = k == 3 ? (UICornerTL | UICornerBR) : UICornerAll;
        }
        auto* dim = Make(s, "dim", cv)->AddComponent<UIPanel>();   // half-transparent overlay
        dim->position = Vec2{200.0f, 250.0f};
        dim->size = Vec2{260.0f, 60.0f};
        dim->color = Color::FromBytes(255, 255, 255, 60);
        dim->borderWidth = 1.0f;
        break;
    }
    case 1: {   // images: flat, textured, nine-slice, filled, aspect, rounded/circle, border, raw
        GameObject* cv = PixelCanvas(s);
        auto img = [&](float x, float y, float w, float h) {
            auto* im = Make(s, "img", cv)->AddComponent<UIImage>();
            im->position = Vec2{x, y}; im->size = Vec2{w, h};
            return im;
        };
        { auto* im = img(10, 10, 80, 60); im->color = Color::FromBytes(200, 80, 80, 255); }
        { auto* im = img(100, 10, 80, 60); im->texture = tex; }
        { auto* im = img(190, 10, 120, 70); im->texture = tex; im->nineSlice = true; im->border = 6.0f; }
        { auto* im = img(320, 10, 80, 60); im->texture = tex; im->fillMode = UIImage::FillMode::Left; im->fillAmount = 0.6f; }
        { auto* im = img(10, 90, 120, 60); im->texture = tex; im->preserveAspect = true; im->flipX = true; }
        { auto* im = img(140, 90, 70, 70); im->texture = tex; im->shape = UIShape::Circle; }
        { auto* im = img(220, 90, 90, 60); im->texture = tex; im->shape = UIShape::Rounded; im->cornerRadius = 14.0f; im->flipY = true; }
        { auto* im = img(320, 90, 80, 60); im->color = Color::FromBytes(60, 200, 120, 255); im->shape = UIShape::Pill;
          im->borderWidth = 3.0f; im->shadow = true; im->shadowSoftness = 4.0f; }
        { auto* im = img(10, 170, 80, 60); im->color = Color::FromBytes(90, 90, 220, 255); im->fillMode = UIImage::FillMode::Up; im->fillAmount = 0.4f; }
        { auto* im = img(100, 170, 80, 60); im->texture = "missing_texture_does_not_exist.png"; im->color = Color::FromBytes(255, 200, 0, 255); }
        auto* raw = Make(s, "raw", cv)->AddComponent<UIRawImage>();
        raw->position = Vec2{200.0f, 170.0f}; raw->size = Vec2{120.0f, 80.0f};
        raw->texture = tex; raw->uvW = 2.0f; raw->uvH = 1.5f; raw->color = Color::FromBytes(255, 255, 255, 200);
        auto* raw2 = Make(s, "raw_missing", cv)->AddComponent<UIRawImage>();
        raw2->position = Vec2{330.0f, 170.0f}; raw2->size = Vec2{60.0f, 60.0f};
        raw2->texture = "missing_raw.png"; raw2->color = Color::FromBytes(120, 40, 160, 255);
        break;
    }
    case 2: {   // controls: progress, radial, slider, stepper, rating, toggle, tabs, button, dropdown, input
        GameObject* cv = PixelCanvas(s);
        { auto* pb = Make(s, "pb", cv)->AddComponent<UIProgressBar>(); pb->position = Vec2{10, 10}; pb->value = 0.65f; pb->showPercent = true; }
        { auto* pb = Make(s, "pb2", cv)->AddComponent<UIProgressBar>(); pb->position = Vec2{10, 40}; pb->value = 0.3f;
          pb->gradientFill = true; pb->fillDir = UIProgressBar::FillDir::RightLeft; pb->shape = UIShape::Pill; }
        { auto* rp = Make(s, "rp", cv)->AddComponent<UIRadialProgress>(); rp->position = Vec2{220, 10}; rp->size = Vec2{64, 64};
          rp->value = 0.7f; rp->showPercent = true; }
        { auto* rp = Make(s, "pie", cv)->AddComponent<UIRadialProgress>(); rp->position = Vec2{290, 10}; rp->size = Vec2{48, 48};
          rp->value = 0.35f; rp->thickness = 0.0f; rp->clockwise = false; }
        { auto* sl = Make(s, "sl", cv)->AddComponent<UISlider>(); sl->position = Vec2{10, 75}; sl->value = 0.4f; sl->showValue = true; }
        { auto* sl = Make(s, "slv", cv)->AddComponent<UISlider>(); sl->position = Vec2{360, 10}; sl->size = Vec2{20, 120};
          sl->vertical = true; sl->value = 0.75f; sl->roundKnob = false; }
        { auto* sl = Make(s, "sld", cv)->AddComponent<UISlider>(); sl->position = Vec2{10, 105}; sl->interactable = false; }
        { auto* st = Make(s, "st", cv)->AddComponent<UIStepper>(); st->position = Vec2{10, 140}; st->value = 7.0f; }
        { auto* st = Make(s, "st2", cv)->AddComponent<UIStepper>(); st->position = Vec2{180, 140}; st->wholeNumbers = false;
          st->value = 2.5f; st->interactable = false; }
        { auto* rt = Make(s, "rt", cv)->AddComponent<UIRating>(); rt->position = Vec2{10, 180}; rt->value = 3.5f; rt->allowHalf = true; }
        { auto* tg = Make(s, "tg", cv)->AddComponent<UIToggle>(); tg->position = Vec2{200, 180}; tg->label = "Sound"; }
        { auto* tg = Make(s, "sw", cv)->AddComponent<UIToggle>(); tg->position = Vec2{330, 180}; tg->size = Vec2{48, 24};
          tg->style = UIToggle::Style::Switch; tg->label = "Sw"; tg->interactable = false; }
        { auto* tb = Make(s, "tabs", cv)->AddComponent<UITabs>(); tb->position = Vec2{10, 220}; tb->value = 1; }
        { auto* bt = Make(s, "btn", cv)->AddComponent<UIButton>(); bt->position = Vec2{320, 215}; bt->size = Vec2{140, 40};
          bt->label = "Play"; bt->borderWidth = 2.0f; bt->shadow = true; bt->icon = tex; bt->iconSize = 20.0f; }
        { auto* dd = Make(s, "dd", cv)->AddComponent<UIDropdown>(); dd->position = Vec2{10, 268}; dd->size = Vec2{150, 24}; dd->value = 1; }
        { auto* in = Make(s, "in", cv)->AddComponent<UIInputField>(); in->position = Vec2{180, 268}; in->size = Vec2{140, 30};
          in->text = "player_one_with_a_long_name"; in->borderWidth = 2.0f; }
        { auto* in = Make(s, "in2", cv)->AddComponent<UIInputField>(); in->position = Vec2{330, 268}; in->size = Vec2{140, 30}; }
        break;
    }
    case 3: {   // text: TTF default font, styles, alignment, background; then the classic pixel font
        GameObject* cv = PixelCanvas(s);
        auto text = [&](const char* str, float x, float y, float px) {
            auto* t = Make(s, "txt", cv)->AddComponent<TextRenderer>();
            t->screenSpace = true; t->text = str; t->screenPos = Vec2{x, y}; t->pixelSize = px;
            t->size = Vec2{220.0f, 30.0f};
            return t;
        };
        text("Hello, OkaySpace!", 10, 10, 3.0f);
        { auto* t = text("Shadow & outline", 10, 50, 2.5f); t->shadow = true; t->shadowOffset = Vec2{2, 2}; t->outline = true;
          t->color = Color::FromBytes(255, 220, 90, 255); }
        { auto* t = text("Bold italic", 10, 90, 2.5f); t->bold = true; t->italic = true; }
        { auto* t = text("Gradient text", 10, 130, 3.0f); t->gradient = true; }
        { auto* t = text("Centered", 240, 10, 2.0f); t->align = 1; t->background = true; }
        { auto* t = text("Right", 240, 50, 2.0f); t->align = 2; t->background = true; t->letterSpacing = 1.0f; }
        { auto* t = text("Two\nlines", 240, 90, 2.0f); t->lineSpacing = 2.0f; t->size = Vec2{220.0f, 60.0f}; }
        { auto* t = text("UPPER wrap text that is long", 240, 160, 1.5f); t->uppercase = true; t->wrap = true;
          t->size = Vec2{120.0f, 80.0f}; }
        { auto* wt = Make(s, "world_text"); wt->AddComponent<TextRenderer>()->text = "not screen space"; }
        { auto* wu = Make(s, "worldui"); wu->AddComponent<WorldUI>()->text = "no camera: skipped"; }
        break;
    }
    case 4: {   // minimap: background, grid, shapes, rings, cone, blips, markers, route, north, border
        auto* tgt = Make(s, "player");
        tgt->transform->SetPosition(Vec3{5.0f, 0.0f, 3.0f});
        auto* mmGo = Make(s, "map");
        auto* mm = mmGo->AddComponent<Minimap>();
        mm->anchor = UIAnchor::TopLeft; mm->position = Vec2{10, 10}; mm->size = Vec2{200, 200};
        mm->target = "player"; mm->showGrid = true; mm->rangeRings = 2; mm->viewCone = true;
        mm->showNorth = true; mm->showLabels = true; mm->borderWidth = 3.0f; mm->mapClickWaypoint = false;
        Minimap::MapShape road; road.kind = Minimap::MapShape::Kind::Line; road.thickness = 4.0f;
        road.points = {Vec2{-40, -10}, Vec2{0, 0}, Vec2{30, 25}};
        Minimap::MapShape house; house.kind = Minimap::MapShape::Kind::Rect; house.points = {Vec2{10, 10}, Vec2{25, 20}};
        Minimap::MapShape zone; zone.kind = Minimap::MapShape::Kind::Poly; zone.filled = false;
        zone.points = {Vec2{-30, 20}, Vec2{-10, 35}, Vec2{-25, 45}};
        mm->mapShapes = {road, house, zone};
        Minimap::Marker m1; m1.world = Vec2{20, -20}; m1.label = "Shop";
        Minimap::Marker m2; m2.world = Vec2{400, 400};
        mm->markers = {m1, m2}; mm->routeMarker = 0; mm->clampBlips = true;
        const MinimapBlip::Shape shapes[] = {MinimapBlip::Shape::Square, MinimapBlip::Shape::Dot,
                                             MinimapBlip::Shape::Triangle, MinimapBlip::Shape::Arrow};
        for (int k = 0; k < 4; ++k) {
            auto* b = Make(s, "blip");
            b->transform->SetPosition(Vec3{-20.0f + k * 14.0f, 0.0f, -15.0f + k * 9.0f});
            auto* bl = b->AddComponent<MinimapBlip>();
            bl->shape = shapes[k]; bl->size = 5.0f; bl->label = k == 0 ? "A" : "";
        }
        auto* round = Make(s, "round")->AddComponent<Minimap>();
        round->anchor = UIAnchor::TopLeft; round->position = Vec2{250, 20}; round->size = Vec2{160, 160};
        round->circular = true; round->rotateWithTarget = true; round->target = "player";
        round->mapClickWaypoint = false; round->hasUserWaypoint = true; round->userWaypoint = Vec2{15, 15};
        auto* ch = Make(s, "crosshair")->AddComponent<Crosshair>();
        ch->dot = true; ch->circle = true; ch->spread = 2.0f; ch->lineUp = false;
        break;
    }
    case 5: {   // scroll view clipping, drop targets, canvas opacity/sort order, uiDrawOrder overrides
        GameObject* cv = PixelCanvas(s);
        auto* svGo = Make(s, "scroll", cv);
        auto* sv = svGo->AddComponent<UIScrollView>();
        sv->position = Vec2{10, 10}; sv->size = Vec2{200, 150}; sv->contentHeight = 400.0f; sv->scroll = 60.0f;
        sv->autoContent = false;
        for (int k = 0; k < 6; ++k) {
            auto* row = Make(s, "row", svGo)->AddComponent<UIPanel>();
            row->position = Vec2{8.0f, 8.0f + k * 50.0f}; row->size = Vec2{170.0f, 40.0f};
            row->color = Color::FromBytes(60, (unsigned char)(80 + k * 25), 160, 255);
            auto* lbl = Make(s, "rowbtn", svGo)->AddComponent<UIButton>();
            lbl->position = Vec2{100.0f, 14.0f + k * 50.0f}; lbl->size = Vec2{70.0f, 28.0f}; lbl->label = "Go";
        }
        auto* slot = Make(s, "slot", cv)->AddComponent<UIDropTarget>();
        slot->drawBackground = true; slot->borderWidth = 2.0f;
        slot->gameObject->AddComponent<UIPanel>()->position = Vec2{230, 10};   // gives the slot a rect
        slot->gameObject->GetComponent<UIPanel>()->size = Vec2{64, 64};
        slot->gameObject->GetComponent<UIPanel>()->color = Color::FromBytes(255, 255, 255, 30);
        auto* hov = Make(s, "slot_hover", cv)->AddComponent<UIDropTarget>();
        hov->SetHovered(true); hov->SetValid(false);
        hov->gameObject->AddComponent<UIPanel>()->position = Vec2{300, 10};
        hov->gameObject->GetComponent<UIPanel>()->size = Vec2{64, 64};
        // A faded canvas drawn under a higher sort-order canvas.
        GameObject* lo = PixelCanvas(s, "lo");
        lo->GetComponent<Canvas>()->sortOrder = 5; lo->GetComponent<Canvas>()->opacity = 0.5f;
        GameObject* hi = PixelCanvas(s, "hi");
        hi->GetComponent<Canvas>()->sortOrder = 1;
        { auto* p = Make(s, "lo_panel", lo)->AddComponent<UIPanel>(); p->position = Vec2{240, 120}; p->size = Vec2{160, 100};
          p->color = Color::FromBytes(255, 80, 80, 255); }
        { auto* p = Make(s, "hi_panel", hi)->AddComponent<UIPanel>(); p->position = Vec2{300, 160}; p->size = Vec2{160, 100};
          p->color = Color::FromBytes(80, 255, 80, 255); }
        // uiDrawOrder: an image lifted above a later panel, a panel sent to the back.
        auto* imGo = Make(s, "lifted", cv);
        auto* im = imGo->AddComponent<UIImage>(); im->position = Vec2{30, 190}; im->size = Vec2{100, 80};
        im->color = Color::FromBytes(250, 220, 40, 255); imGo->uiDrawOrder = 50;
        auto* cover = Make(s, "cover", cv)->AddComponent<UIPanel>(); cover->position = Vec2{60, 210};
        cover->size = Vec2{120, 80}; cover->color = Color::FromBytes(40, 40, 200, 255);
        auto* backGo = Make(s, "back", cv);
        auto* back = backGo->AddComponent<UIPanel>(); back->position = Vec2{0, 180}; back->size = Vec2{220, 140};
        back->color = Color::FromBytes(120, 120, 120, 255); backGo->uiDrawOrder = -5;
        break;
    }
    case 6: {   // Scale With Screen Size canvas (renderer scale), stretch anchors, hidden widgets
        GameObject* cv = Make(s, "scaled");
        auto* c = cv->AddComponent<Canvas>();
        c->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
        c->referenceResolution = Vec2{960.0f, 640.0f};   // half-size output: scale 0.5
        auto* p = Make(s, "corner", cv)->AddComponent<UIPanel>();
        p->anchor = UIAnchor::BottomRight; p->position = Vec2{20, 20}; p->size = Vec2{300, 200};
        p->color = Color::FromBytes(200, 100, 40, 255);
        auto* full = Make(s, "stretch", cv)->AddComponent<UIPanel>();
        full->anchor = UIAnchor::StretchFull; full->position = Vec2{40, 40}; full->size = Vec2{-80, -80};
        full->color = Color::FromBytes(255, 255, 255, 40);
        auto* b = Make(s, "btn", cv)->AddComponent<UIButton>();
        b->anchor = UIAnchor::Center; b->size = Vec2{320, 96}; b->label = "Scaled"; b->fontScale = 4.0f;
        auto* hid = Make(s, "hidden", cv); hid->AddComponent<UIPanel>()->color = Color::FromBytes(255, 0, 0, 255);
        hid->active = false;
        GameObject* hc = PixelCanvas(s, "hidden_canvas"); hc->GetComponent<Canvas>()->visible = false;
        Make(s, "in_hidden", hc)->AddComponent<UIPanel>()->color = Color::FromBytes(255, 0, 0, 255);
        break;
    }
    default: break;
    }
}

} // namespace ui_golden
