#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
using namespace okay;

// Native Minimap + MinimapBlip and Crosshair: the shared world->map projection is
// correct (center, +X right, +Z up = smaller y, out-of-range), and all three
// components survive a SceneSerializer round-trip with their fields intact.
int main() {
    RUN_SUITE("minimap");

    // --- WorldToMap projection (shared by renderer + this test) ----------
    {
        Minimap m;
        m.worldPerPixel = 0.5f;
        m.useXZ = true;
        const float mapW = 180.0f, mapH = 180.0f;
        Vec3 center{0, 0, 0};
        float x = 0, y = 0;

        // A blip at the center world pos maps to the map center.
        CHECK(Minimap::WorldToMap(m, center, center, mapW, mapH, x, y));
        CHECK_NEAR(x, mapW * 0.5f, 1e-3f);
        CHECK_NEAR(y, mapH * 0.5f, 1e-3f);

        // Offset by +worldPerPixel in X maps one pixel to the right.
        CHECK(Minimap::WorldToMap(m, center, Vec3{m.worldPerPixel, 0, 0}, mapW, mapH, x, y));
        CHECK_NEAR(x, mapW * 0.5f + 1.0f, 1e-3f);
        CHECK_NEAR(y, mapH * 0.5f, 1e-3f);

        // Offset by +worldPerPixel in Z maps one pixel UP (smaller y).
        CHECK(Minimap::WorldToMap(m, center, Vec3{0, 0, m.worldPerPixel}, mapW, mapH, x, y));
        CHECK_NEAR(x, mapW * 0.5f, 1e-3f);
        CHECK_NEAR(y, mapH * 0.5f - 1.0f, 1e-3f);

        // Far outside the map rect returns false.
        CHECK(!Minimap::WorldToMap(m, center, Vec3{10000.0f, 0, 0}, mapW, mapH, x, y));

        // 2D mode uses Y instead of Z (also "up").
        Minimap m2; m2.useXZ = false; m2.worldPerPixel = 1.0f;
        CHECK(Minimap::WorldToMap(m2, center, Vec3{0, 1.0f, 0}, 100.0f, 100.0f, x, y));
        CHECK_NEAR(y, 50.0f - 1.0f, 1e-3f);
    }

    // --- Expanded: heading, rotation, circular mask ----------------------
    {
        // HeadingOf: forward = +Z (north) -> 0 rad; forward = +X (east) -> +pi/2.
        CHECK_NEAR(Minimap::HeadingOf(Vec3{0, 0, 1}, true), 0.0f, 1e-3f);
        CHECK_NEAR(Minimap::HeadingOf(Vec3{1, 0, 0}, true), 1.5707963f, 1e-3f);

        // Heading-up rotation: a blip due east of the target, with the target
        // facing east (heading +pi/2), should map to straight UP on the map.
        Minimap m; m.worldPerPixel = 1.0f; m.useXZ = true;
        const float W = 100.0f, H = 100.0f;
        Vec3 center{0, 0, 0};
        float x = 0, y = 0;
        float heading = Minimap::HeadingOf(Vec3{1, 0, 0}, true);   // facing east
        CHECK(Minimap::WorldToMapR(m, center, Vec3{5.0f, 0, 0}, W, H, heading, x, y));
        CHECK_NEAR(x, W * 0.5f, 1e-2f);          // no horizontal offset
        CHECK_NEAR(y, H * 0.5f - 5.0f, 1e-2f);   // 5 px straight up

        // Circular mask: a point inside the rect but outside the inscribed circle
        // is reported outside; the same point in a rectangular map is inside.
        Minimap rect; rect.worldPerPixel = 1.0f; rect.circular = false;
        Minimap circ; circ.worldPerPixel = 1.0f; circ.circular = true;
        // Corner-ish: dx=45,dy=45 -> dist ~63.6 > radius 50.
        CHECK(Minimap::WorldToMapR(rect, center, Vec3{45.0f, 0, -45.0f}, W, H, 0.0f, x, y));
        CHECK(!Minimap::WorldToMapR(circ, center, Vec3{45.0f, 0, -45.0f}, W, H, 0.0f, x, y));
    }

    // --- Minimap + MinimapBlip round-trip through serialization ----------
    {
        Scene s("map");
        GameObject* hud = s.CreateGameObject("HUD");
        auto* mm = hud->AddComponent<Minimap>();
        mm->position = {30.0f, 40.0f};
        mm->size = {200.0f, 160.0f};
        mm->borderWidth = 3.0f;
        mm->target = "Hero";
        mm->worldPerPixel = 0.25f;
        mm->useXZ = false;
        mm->blipSize = 6.0f;
        mm->anchor = UIAnchor::BottomLeft;
        mm->background = Color::FromBytes(10, 20, 30, 200);
        mm->border = Color::FromBytes(100, 110, 120);
        mm->targetColor = Color::FromBytes(1, 2, 3, 4);
        mm->circular = true;
        mm->rotateWithTarget = true;
        mm->playerArrow = false;
        mm->clampBlips = true;
        mm->showGrid = true;
        mm->gridColor = Color::FromBytes(60, 70, 80, 100);
        mm->gridSpacing = 32.0f;

        GameObject* enemy = s.CreateGameObject("Enemy");
        auto* bl = enemy->AddComponent<MinimapBlip>();
        bl->color = Color::FromBytes(200, 50, 60, 255);
        bl->size = 5.0f;
        bl->square = false;
        bl->shape = MinimapBlip::Shape::Arrow;
        bl->rotateWithObject = true;

        std::string text = SceneSerializer::Serialize(s);
        Scene loaded("loaded");
        CHECK(SceneSerializer::Deserialize(loaded, text));

        GameObject* lh = loaded.Find("HUD");
        CHECK(lh != nullptr);
        auto* lm = lh ? lh->GetComponent<Minimap>() : nullptr;
        CHECK(lm != nullptr);
        if (lm) {
            CHECK_NEAR(lm->position.x, 30.0f, 1e-3f);
            CHECK_NEAR(lm->position.y, 40.0f, 1e-3f);
            CHECK_NEAR(lm->size.x, 200.0f, 1e-3f);
            CHECK_NEAR(lm->size.y, 160.0f, 1e-3f);
            CHECK_NEAR(lm->borderWidth, 3.0f, 1e-3f);
            CHECK(lm->target == "Hero");
            CHECK_NEAR(lm->worldPerPixel, 0.25f, 1e-3f);
            CHECK(lm->useXZ == false);
            CHECK_NEAR(lm->blipSize, 6.0f, 1e-3f);
            CHECK(lm->anchor == UIAnchor::BottomLeft);
            CHECK(lm->circular);
            CHECK(lm->rotateWithTarget);
            CHECK(!lm->playerArrow);
            CHECK(lm->clampBlips);
            CHECK(lm->showGrid);
            CHECK_NEAR(lm->gridSpacing, 32.0f, 1e-3f);
        }

        GameObject* le = loaded.Find("Enemy");
        CHECK(le != nullptr);
        auto* lb = le ? le->GetComponent<MinimapBlip>() : nullptr;
        CHECK(lb != nullptr);
        if (lb) {
            CHECK_NEAR(lb->size, 5.0f, 1e-3f);
            CHECK(lb->square == false);
        }
    }

    // --- Crosshair round-trip --------------------------------------------
    {
        Scene s("cross");
        GameObject* hud = s.CreateGameObject("Reticle");
        auto* cr = hud->AddComponent<Crosshair>();
        cr->position = {2.0f, -3.0f};
        cr->size = {32.0f, 32.0f};
        cr->thickness = 3.0f;
        cr->gap = 8.0f;
        cr->length = 12.0f;
        cr->showLines = false;
        cr->dot = true;
        cr->dotSize = 5.0f;
        cr->outline = false;
        cr->anchor = UIAnchor::Center;
        cr->color = Color::FromBytes(255, 0, 0, 255);
        cr->dotColor = Color::FromBytes(0, 255, 0, 200);

        std::string text = SceneSerializer::Serialize(s);
        Scene loaded("loaded");
        CHECK(SceneSerializer::Deserialize(loaded, text));

        GameObject* lh = loaded.Find("Reticle");
        CHECK(lh != nullptr);
        auto* lc = lh ? lh->GetComponent<Crosshair>() : nullptr;
        CHECK(lc != nullptr);
        if (lc) {
            CHECK_NEAR(lc->position.x, 2.0f, 1e-3f);
            CHECK_NEAR(lc->position.y, -3.0f, 1e-3f);
            CHECK_NEAR(lc->size.x, 32.0f, 1e-3f);
            CHECK_NEAR(lc->thickness, 3.0f, 1e-3f);
            CHECK_NEAR(lc->gap, 8.0f, 1e-3f);
            CHECK_NEAR(lc->length, 12.0f, 1e-3f);
            CHECK(lc->showLines == false);
            CHECK(lc->dot == true);
            CHECK_NEAR(lc->dotSize, 5.0f, 1e-3f);
            CHECK(lc->outline == false);
            CHECK(lc->anchor == UIAnchor::Center);
        }
    }

    TEST_MAIN_RESULT();
}
