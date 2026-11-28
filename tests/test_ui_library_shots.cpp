// Headless pictures (software renderer, same UI pass as the game) of the UI library slice 1 parts.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UILibrary.hpp"
#include "ui_soft_render.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

using namespace okay;

namespace {

bool Render(const std::string& path, int width, int height, const std::function<void(Scene&)>& build) {
    Scene scene("shot");
    scene.physicsEnabled = false;
    scene.uiFont = "pixel";
    build(scene);
    Image img;
    if (!okaytest::SoftRenderSceneUI(scene, width, height, img, false, 18, 22, 32)) return false;
    int drawn = 0;
    const unsigned char* px = img.Data();
    for (int i = 0; i < width * height; ++i)
        if (px[i * 4] != 18 || px[i * 4 + 1] != 22 || px[i * 4 + 2] != 32) ++drawn;
    if (drawn < 40) return false;
    return img.SavePNG(path);
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("ui_library_shots");
    std::string dir = std::string(OKAY_TEST_OUTPUT_DIR) + "/ui_library";
    if (argc >= 2 && argv[1][0]) dir = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    CHECK(!ec);
    auto at = [&](const char* file) { return dir + "/" + file; };

    CHECK(Render(at("range-slider.png"), 360, 150, [](Scene& s) {
        auto* a = s.CreateGameObject("Price")->AddComponent<UIRangeSlider>();
        a->anchor = UIAnchor::TopLeft; a->position = {30, 40}; a->size = {300, 30};
        a->minValue = 0; a->maxValue = 500; a->low = 120; a->high = 380; a->step = 10;
        a->valueFormat = "${lo} to ${hi}";
        auto* b = s.CreateGameObject("Level")->AddComponent<UIRangeSlider>();
        b->anchor = UIAnchor::TopLeft; b->position = {30, 105}; b->size = {300, 30};
        b->minValue = 1; b->maxValue = 60; b->low = 10; b->high = 25; b->wholeNumbers = true;
        b->valueFormat = "Levels {lo}-{hi}";
        b->fill = Color::FromBytes(120, 200, 110);
    }));
    CHECK(Render(at("input-validation.png"), 380, 230, [](Scene& s) {
        auto mk = [&](const char* name, float y, const char* text, UIInputValidator::Kind k) {
            GameObject* g = s.CreateGameObject(name);
            auto* in = g->AddComponent<UIInputField>();
            in->anchor = UIAnchor::TopLeft; in->position = {20, y}; in->size = {340, 36}; in->text = text;
            auto* v = g->AddComponent<UIInputValidator>();
            v->kind = k; v->quietUntilTouched = false; v->showOk = true;
            v->Check();
            v->MarkTouched();
        };
        mk("Mail", 18, "someone@", UIInputValidator::Kind::Email);
        mk("Age", 84, "not a number", UIInputValidator::Kind::Integer);
        mk("Name", 150, "Ada Lovelace", UIInputValidator::Kind::Letters);
    }));
    struct Screen { const char* file; int w, h; };
    const Screen screens[] = {{"aspect-landscape.png", 480, 270}, {"aspect-portrait.png", 270, 480}, {"aspect-ultrawide.png", 600, 200}};
    for (const Screen& sc : screens) {
        CHECK(Render(at(sc.file), sc.w, sc.h, [](Scene& s) {
            GameObject* canvas = s.CreateGameObject("Canvas");
            canvas->AddComponent<Canvas>();
            GameObject* frame = s.CreateGameObject("Frame");
            frame->transform->SetParent(canvas->transform);
            frame->AddComponent<UIAspectRatio>()->padding = 6.0f;
            GameObject* card = s.CreateGameObject("Card");
            card->transform->SetParent(frame->transform);
            auto* p = card->AddComponent<UIPanel>();
            p->anchor = UIAnchor::StretchFull; p->position = {0, 0}; p->size = {0, 0};
            p->color = Color::FromBytes(54, 92, 150); p->borderColor = Color::FromBytes(230, 236, 250);
            p->borderWidth = 3.0f;
            GameObject* inner = s.CreateGameObject("Dot");
            inner->transform->SetParent(card->transform);
            auto* d = inner->AddComponent<UIPanel>();
            d->anchor = UIAnchor::Center; d->position = {0, 0}; d->size = {60, 60};
            d->color = Color::FromBytes(250, 200, 80);
            d->shape = UIShape::Circle;
        }));
    }
    // The whole example scene, at a desktop and a tall window size.
    CHECK(Render(at("example-1280x720.png"), 1280, 720, [](Scene& s) { UILibrary::BuildSlice1(s); }));
    CHECK(Render(at("example-1280x900.png"), 1280, 900, [](Scene& s) { UILibrary::BuildSlice1(s); }));
    TEST_MAIN_RESULT();
}
