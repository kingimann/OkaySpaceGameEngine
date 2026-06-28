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

        // wpp override (used by the fullscreen pause map) doubles the visible range.
        Minimap z; z.worldPerPixel = 1.0f;
        float x2, y2;
        CHECK(Minimap::WorldToMapR(z, center, Vec3{20.0f, 0, 0}, W, H, 0.0f, x2, y2, 2.0f));
        CHECK_NEAR(x2, W * 0.5f + 10.0f, 1e-2f);   // 20 world / 2 wpp = 10 px
    }

    // --- GTA-style custom map: UV window pans with the centre -----------
    {
        Minimap m; m.mapTexture = "world.png"; m.mapWorldSize = 100.0f;
        m.mapWorldCenter = {0.0f, 0.0f}; m.useXZ = true;
        CHECK(m.HasMap());
        float u0, v0, u1, v1;
        // Centre at world origin, 100x100 px panel, wpp 1 -> visible window is the
        // central 100 world units = the full texture (u:0..1).
        Minimap::MapSrcUV(m, Vec3{0, 0, 0}, 100.0f, 100.0f, 1.0f, u0, v0, u1, v1);
        CHECK_NEAR(u0, 0.0f, 1e-3f); CHECK_NEAR(u1, 1.0f, 1e-3f);
        CHECK_NEAR(v0, 0.0f, 1e-3f); CHECK_NEAR(v1, 1.0f, 1e-3f);
        // Pan east by 25 world: the UV window shifts right by 0.25.
        Minimap::MapSrcUV(m, Vec3{25.0f, 0, 0}, 100.0f, 100.0f, 1.0f, u0, v0, u1, v1);
        CHECK_NEAR(u0, 0.25f, 1e-3f); CHECK_NEAR(u1, 1.25f, 1e-3f);
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
        mm->mapTexture = "maps/city.png";
        mm->mapWorldSize = 250.0f;
        mm->mapWorldCenter = {12.5f, -7.5f};
        mm->fullscreenKey = 'n';
        mm->fullscreenZoom = 4.0f;
        mm->fullscreenFrac = 0.9f;
        mm->zoomInKey = '+'; mm->zoomOutKey = '-';
        mm->minZoom = 0.1f; mm->maxZoom = 20.0f; mm->zoomStep = 1.25f;
        mm->markers.push_back({{10.0f, 20.0f}, Color::FromBytes(0, 255, 0), 7.0f, "Shop"});
        mm->markers.push_back({{-30.0f, 5.0f}, Color::FromBytes(255, 0, 0), 5.0f, "Boss"});
        mm->showLabels = true; mm->labelSize = 12.0f;
        mm->labelColor = Color::FromBytes(200, 210, 220);
        mm->routeMarker = 1; mm->routeWidth = 3.0f;
        mm->routeColor = Color::FromBytes(235, 70, 200, 230);
        {
            Minimap::MapShape road; road.kind = Minimap::MapShape::Kind::Line;
            road.color = Color::FromBytes(90, 92, 104); road.thickness = 5.0f; road.filled = false;
            road.points = {{0,0},{20,0},{20,15}};
            Minimap::MapShape house; house.kind = Minimap::MapShape::Kind::Rect;
            house.color = Color::FromBytes(150, 140, 120); house.points = {{5,5},{12,11}};
            mm->mapShapes.push_back(road);
            mm->mapShapes.push_back(house);
        }
        mm->blipRange = 42.5f;
        mm->showSelf = false;
        mm->viewCone = true; mm->viewConeAngle = 75.0f; mm->viewConeLength = 50.0f;
        mm->viewConeColor = Color::FromBytes(10, 20, 30, 80);
        mm->rangeRings = 3; mm->ringColor = Color::FromBytes(40, 50, 60, 90);
        mm->showNorth = true; mm->northColor = Color::FromBytes(250, 80, 80);

        GameObject* enemy = s.CreateGameObject("Enemy");
        auto* bl = enemy->AddComponent<MinimapBlip>();
        bl->color = Color::FromBytes(200, 50, 60, 255);
        bl->size = 5.0f;
        bl->square = false;
        bl->shape = MinimapBlip::Shape::Arrow;
        bl->rotateWithObject = true;
        bl->icon = "icons/enemy.png";
        bl->label = "Grunt";

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
            CHECK(lm->mapTexture == "maps/city.png");
            CHECK_NEAR(lm->mapWorldSize, 250.0f, 1e-3f);
            CHECK_NEAR(lm->mapWorldCenter.x, 12.5f, 1e-3f);
            CHECK_NEAR(lm->mapWorldCenter.y, -7.5f, 1e-3f);
            CHECK(lm->fullscreenKey == 'n');
            CHECK_NEAR(lm->fullscreenZoom, 4.0f, 1e-3f);
            CHECK_NEAR(lm->fullscreenFrac, 0.9f, 1e-3f);
            CHECK(lm->zoomInKey == '+'); CHECK(lm->zoomOutKey == '-');
            CHECK_NEAR(lm->maxZoom, 20.0f, 1e-3f);
            CHECK_NEAR(lm->zoomStep, 1.25f, 1e-3f);
            CHECK(lm->markers.size() == 2);
            if (lm->markers.size() == 2) {
                CHECK_NEAR(lm->markers[0].world.x, 10.0f, 1e-3f);
                CHECK_NEAR(lm->markers[0].world.y, 20.0f, 1e-3f);
                CHECK_NEAR(lm->markers[0].size, 7.0f, 1e-3f);
                CHECK_NEAR(lm->markers[1].world.x, -30.0f, 1e-3f);
            }
            CHECK(!lm->showSelf);
            CHECK(lm->viewCone);
            CHECK_NEAR(lm->viewConeAngle, 75.0f, 1e-3f);
            CHECK_NEAR(lm->viewConeLength, 50.0f, 1e-3f);
            CHECK(lm->rangeRings == 3);
            CHECK(lm->showNorth);
            CHECK_NEAR(lm->northColor.r, 250.0f/255.0f, 0.01f);
            CHECK(lm->showLabels);
            CHECK_NEAR(lm->labelSize, 12.0f, 1e-3f);
            CHECK(lm->routeMarker == 1);
            CHECK_NEAR(lm->routeWidth, 3.0f, 1e-3f);
            CHECK(lm->mapShapes.size() == 2);
            if (lm->mapShapes.size() == 2) {
                CHECK(lm->mapShapes[0].kind == Minimap::MapShape::Kind::Line);
                CHECK(!lm->mapShapes[0].filled);
                CHECK_NEAR(lm->mapShapes[0].thickness, 5.0f, 1e-3f);
                CHECK(lm->mapShapes[0].points.size() == 3);
                CHECK_NEAR(lm->mapShapes[0].points[1].x, 20.0f, 1e-3f);
                CHECK(lm->mapShapes[1].kind == Minimap::MapShape::Kind::Rect);
                CHECK(lm->mapShapes[1].points.size() == 2);
                CHECK_NEAR(lm->mapShapes[1].points[1].y, 11.0f, 1e-3f);
            }
            CHECK_NEAR(lm->blipRange, 42.5f, 1e-3f);
            if (lm->markers.size() == 2) {
                CHECK(lm->markers[0].label == "Shop");
                CHECK(lm->markers[1].label == "Boss");
            }
        }

        GameObject* le = loaded.Find("Enemy");
        CHECK(le != nullptr);
        auto* lb = le ? le->GetComponent<MinimapBlip>() : nullptr;
        CHECK(lb != nullptr);
        if (lb) {
            CHECK_NEAR(lb->size, 5.0f, 1e-3f);
            CHECK(lb->square == false);
            CHECK(lb->shape == MinimapBlip::Shape::Arrow);
            CHECK(lb->rotateWithObject);
            CHECK(lb->icon == "icons/enemy.png");
            CHECK(lb->label == "Grunt");
        }
    }

    // --- Radar blip range gating -----------------------------------------
    {
        Minimap m; m.useXZ = true; m.blipRange = 10.0f;
        Vec3 center{0, 0, 0};
        CHECK(Minimap::WithinRange(m, center, Vec3{6, 0, 0}));        // within 10
        CHECK(!Minimap::WithinRange(m, center, Vec3{0, 0, 15}));      // beyond 10
        m.blipRange = 0.0f;                                            // 0 = unlimited
        CHECK(Minimap::WithinRange(m, center, Vec3{1000, 0, 0}));
    }

    // --- Runtime zoom keys nudge worldPerPixel within [min,max] ----------
    {
        Scene s("zoom");
        auto* mm = s.CreateGameObject("HUD")->AddComponent<Minimap>();
        mm->worldPerPixel = 1.0f; mm->minZoom = 0.5f; mm->maxZoom = 4.0f;
        mm->zoomStep = 2.0f; mm->zoomInKey = 'q'; mm->zoomOutKey = 'e';
        Input::FeedKeys({'q'}); mm->Update(0.016f);          // zoom in: /2 -> 0.5
        CHECK_NEAR(mm->worldPerPixel, 0.5f, 1e-3f);
        Input::FeedKeys({'q'}); mm->Update(0.016f);          // clamp at minZoom 0.5
        CHECK_NEAR(mm->worldPerPixel, 0.5f, 1e-3f);
        Input::FeedKeys({}); // release
        Input::FeedKeys({'e'}); mm->Update(0.016f);          // zoom out: *2 -> 1.0
        CHECK_NEAR(mm->worldPerPixel, 1.0f, 1e-3f);
        Input::FeedKeys({});
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
