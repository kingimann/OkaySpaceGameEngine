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

    // --- Standalone in-world widget (WorldSpaceUI): its OWN object is the anchor ---
    {
        Scene s2("Self");
        GameObject* wb = s2.CreateGameObject("WBtn");
        wb->transform->localPosition = {0.0f, 0.0f, 0.0f};
        UIButton* bb = wb->AddComponent<UIButton>();
        bb->anchor = UIAnchor::Center; bb->position = {0.0f, 0.0f}; bb->size = {200.0f, 100.0f};
        WorldSpaceUI* w = wb->AddComponent<WorldSpaceUI>();
        w->pixelsPerUnit = 100.0f; w->billboard = true;
        Vec2 o2, sz2; float k2 = 0.0f;
        CHECK(GetUIScreenRect(wb, 800.0f, 600.0f, o2, sz2, &k2));
        CHECK_NEAR(k2, 1.0f, 0.001f);
        CHECK_NEAR(sz2.x, 200.0f, 0.5f);
        CHECK_NEAR(o2.x, 300.0f, 0.5f);   // centered on the object at world (0,0)
        CHECK_NEAR(o2.y, 250.0f, 0.5f);
        wb->transform->localPosition = {1.0f, 0.0f, 0.0f};   // moving the object moves it
        CHECK(GetUIScreenRect(wb, 800.0f, 600.0f, o2, sz2, &k2));
        CHECK_NEAR(o2.x, 400.0f, 0.5f);
    }

    // --- Runtime click: a world-space button hit-tests where it projects -------
    // The renderer supplies UIWorld().rectOf so UIButton::Contains() can test the
    // projected on-screen rect instead of the button's flat screen coordinates.
    {
        Scene s3("Click");
        GameObject* wb = s3.CreateGameObject("WBtn");
        UIButton* bb = wb->AddComponent<UIButton>();
        bb->anchor = UIAnchor::Center; bb->position = {0.0f, 0.0f}; bb->size = {200.0f, 100.0f};
        wb->AddComponent<WorldSpaceUI>();
        // A screen-space button (no WorldSpaceUI) for the gating check.
        GameObject* sbo = s3.CreateGameObject("ScreenBtn");
        UIButton* sb = sbo->AddComponent<UIButton>();
        sb->anchor = UIAnchor::TopLeft; sb->position = {10.0f, 10.0f}; sb->size = {80.0f, 40.0f};

        // rectOf reports the world button at a fixed projected rect far from its
        // screen-space anchor, so the two hit-test paths are clearly distinguishable.
        UIWorld().active = true;
        UIWorld().rectOf = [&](GameObject* go, Vec2& o, Vec2& sz) {
            if (go == wb) { o = Vec2{500.0f, 400.0f}; sz = Vec2{120.0f, 60.0f}; return true; }
            return GetUIScreenRect(go, 800.0f, 600.0f, o, sz);   // others: real rect
        };
        CHECK(bb->IsWorldSpaceUI());
        CHECK(!sb->IsWorldSpaceUI());
        // World button: clickable inside the PROJECTED rect, not at its design anchor.
        CHECK(bb->Contains(Vec2{560.0f, 430.0f}));
        CHECK(!bb->Contains(Vec2{400.0f, 300.0f}));
        // Screen button ignores the world path: still hit-tests its own silhouette.
        CHECK(sb->Contains(Vec2{20.0f, 20.0f}));
        CHECK(!sb->Contains(Vec2{500.0f, 400.0f}));
        UIWorld().rectOf = nullptr;
    }

    UIWorld().active = false;
    TEST_MAIN_RESULT();
}
