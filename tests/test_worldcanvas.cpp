#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UIElement.hpp"
using namespace okay;

// A widget under a world-space Canvas should project through the active camera
// context: the shared GetUIScreenRect maps its design-space rect to the screen, so
// every widget type renders in-world with no per-widget code.
int main() {
    RUN_SUITE("worldcanvas");

    Scene s("W");
    GameObject* canvasObj = s.CreateGameObject("WorldCanvas");
    Canvas* cv = canvasObj->AddComponent<Canvas>();
    cv->worldSpace = true; cv->billboard = true;
    cv->designResolution = {800.0f, 600.0f};
    cv->worldPixelsPerUnit = 100.0f;

    GameObject* btnObj = s.CreateGameObject("Btn");
    btnObj->transform->SetParent(canvasObj->transform, false);
    UIButton* b = btnObj->AddComponent<UIButton>();
    b->anchor = UIAnchor::Center; b->position = {0.0f, 0.0f}; b->size = {200.0f, 100.0f};

    // With no active world context, a world-canvas widget can't project -> false.
    Vec2 o, sz;
    CHECK(!GetUIScreenRect(btnObj, 1280.0f, 720.0f, o, sz));

    // Install a simple linear projector: world (x,y) -> screen (400 + x*100, 300 - y*100).
    // center maps to (400,300); one world unit right = 100 screen px, so with ppu=100
    // the design->screen scale k = 1.
    UIWorld().active = true;
    UIWorld().right = {1.0f, 0.0f, 0.0f};
    UIWorld().screenW = 800.0f; UIWorld().screenH = 600.0f;
    UIWorld().project = [](const Vec3& p, Vec2& out, float& depth) {
        out = Vec2{400.0f + p.x * 100.0f, 300.0f - p.y * 100.0f};
        depth = 5.0f; return true;
    };

    float k = 0.0f;
    CHECK(GetUIScreenRect(btnObj, 800.0f, 600.0f, o, sz, &k));
    CHECK_NEAR(k, 1.0f, 0.001f);                 // design px == screen px at this distance
    CHECK_NEAR(sz.x, 200.0f, 0.5f);
    CHECK_NEAR(sz.y, 100.0f, 0.5f);
    // Centered button: design origin (300,250); projected canvas center (400,300);
    // screen origin = (400,300) + ((300,250)-(400,300))*1 = (300,250).
    CHECK_NEAR(o.x, 300.0f, 0.5f);
    CHECK_NEAR(o.y, 250.0f, 0.5f);

    // Moving the canvas object right by 1 unit shifts everything +100 px on screen.
    canvasObj->transform->localPosition = {1.0f, 0.0f, 0.0f};
    CHECK(GetUIScreenRect(btnObj, 800.0f, 600.0f, o, sz, &k));
    CHECK_NEAR(o.x, 400.0f, 0.5f);

    UIWorld().active = false;
    TEST_MAIN_RESULT();
}
