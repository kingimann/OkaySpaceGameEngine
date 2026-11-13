// BakeFaceLighting writes per-face colours from the scene lights, including
// shadows and ambient occlusion. RayTriangle is the occlusion test it uses.
#include "test_framework.hpp"
#include "okay/Render/Lightmap.hpp"
#include <cmath>
#include <vector>

using namespace okay;

namespace {

void ResetLights() {
    SceneLights::Clear();
    SceneLights::SetAmbient(0.0f);
    SceneLights::Hemisphere() = false;
    SceneLights::SkyAmbient() = Vec3{0.25f, 0.25f, 0.25f};
    SceneLights::GroundAmbient() = Vec3{0.25f, 0.25f, 0.25f};
}

// One triangle in the XZ plane, normal +Y. Centroid is (1/3, 0, 1/3).
Mesh UpFace() {
    Mesh m;
    m.vertices = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}};
    m.triangles = {0, 1, 2};
    return m;
}
Mesh DownFace() {
    Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    m.triangles = {0, 1, 2};
    return m;
}

void Expect(const Color& c, float r, float g, float b, float a = 1.0f) {
    CHECK_NEAR(c.r, r, 2e-3);
    CHECK_NEAR(c.g, g, 2e-3);
    CHECK_NEAR(c.b, b, 2e-3);
    CHECK_NEAR(c.a, a, 2e-3);
}

const auto kNoHit = [](const Vec3&, const Vec3&, float) { return false; };
const auto kAlways = [](const Vec3&, const Vec3&, float) { return true; };

} // namespace

int main() {
    RUN_SUITE("lightmap");
    ResetLights();

    // Ray / triangle: hits from either side, misses a parallel ray and a miss.
    {
        const Vec3 v0{0, 0, 0}, v1{1, 0, 0}, v2{0, 1, 0};
        float t = -1.0f;
        CHECK(RayTriangle({0.2f, 0.2f, 1.0f}, {0, 0, -1}, v0, v1, v2, 10.0f, t));
        CHECK_NEAR(t, 1.0f, 1e-4f);
        CHECK(RayTriangle({0.2f, 0.2f, -1.0f}, {0, 0, 1}, v0, v1, v2, 10.0f, t));   // no back-face cull
        CHECK_NEAR(t, 1.0f, 1e-4f);
        CHECK(!RayTriangle({0.2f, 0.2f, 1.0f}, {0, 0, 1}, v0, v1, v2, 10.0f, t));    // going away
        CHECK(!RayTriangle({0.2f, 0.2f, 1.0f}, {1, 0, 0}, v0, v1, v2, 10.0f, t));    // parallel
        CHECK(!RayTriangle({2.0f, 2.0f, 1.0f}, {0, 0, -1}, v0, v1, v2, 10.0f, t));   // outside
        CHECK(!RayTriangle({0.2f, 0.2f, 1.0f}, {0, 0, -1}, v0, v1, v2, 0.5f, t));    // hit is past maxDist
    }

    // An empty mesh is left alone. A degenerate face still gets the ambient colour.
    {
        Mesh empty;
        BakeFaceLighting(empty, Mat4::Identity(), Color::White, kNoHit);
        CHECK(empty.triColors.empty());
        Mesh flat;
        flat.vertices = {{1, 2, 3}, {1, 2, 3}, {1, 2, 3}};
        flat.triangles = {0, 1, 2};
        SceneLights::SetAmbient(0.5f);
        BakeFaceLighting(flat, Mat4::Identity(), Color::White, kNoHit, 0);
        CHECK(flat.triColors.size() == 1);
        Expect(flat.triColors[0], 0.5f, 0.5f, 0.5f);
    }

    // No lights: the face is the ambient colour times the material, and alpha is kept.
    {
        ResetLights();
        SceneLights::SetAmbient(1.0f);
        Mesh m = UpFace();
        BakeFaceLighting(m, Mat4::Identity(), Color{0.5f, 0.25f, 0.0f, 0.4f}, kNoHit, 0);
        Expect(m.triColors[0], 0.5f, 0.25f, 0.0f, 0.4f);
    }

    // A downward sun lights an upward face and misses a downward one. A sun from
    // the side lights neither. The product is clamped at 1.
    {
        ResetLights();
        LightSample sun;
        sun.type = 0;
        sun.dir = {0, -1, 0};
        sun.color = {2.0f, 0.0f, 0.0f};
        SceneLights::Add(sun);
        Mesh up = UpFace();
        BakeFaceLighting(up, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(up.triColors[0], 1.0f, 0.0f, 0.0f);
        Mesh down = DownFace();
        BakeFaceLighting(down, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(down.triColors[0], 0.0f, 0.0f, 0.0f);
        SceneLights::Clear();
        sun.dir = {1, 0, 0};
        sun.color = {1, 1, 1};
        SceneLights::Add(sun);
        BakeFaceLighting(up, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(up.triColors[0], 0.0f, 0.0f, 0.0f);
    }

    // Point light: in range it falls off, past range it adds nothing. The mesh
    // transform is applied, so a light at the moved face still hits it.
    {
        ResetLights();
        LightSample pt;
        pt.type = 1;
        pt.falloff = 0;
        pt.range = 10.0f;
        pt.color = {1, 0, 0};
        pt.pos = {1.0f / 3.0f, 2.0f, 1.0f / 3.0f};
        SceneLights::Add(pt);
        Mesh m = UpFace();
        BakeFaceLighting(m, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(m.triColors[0], 0.64f, 0.0f, 0.0f);            // (1 - 2/10)^2
        pt.range = 1.0f;
        SceneLights::Clear();
        SceneLights::Add(pt);
        BakeFaceLighting(m, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(m.triColors[0], 0.0f, 0.0f, 0.0f);

        SceneLights::Clear();
        pt.range = 4.0f;
        pt.color = {0, 1, 0};
        pt.pos = {10.0f + 1.0f / 3.0f, 2.0f, 1.0f / 3.0f};
        SceneLights::Add(pt);
        BakeFaceLighting(m, Mat4::Translate({10, 0, 0}), Color::White, kNoHit, 0);
        Expect(m.triColors[0], 0.0f, 0.25f, 0.0f);            // (1 - 2/4)^2, and only if the move was applied
    }

    // A spot aimed at the face lights it. Aimed away, it does not.
    {
        ResetLights();
        LightSample spot;
        spot.type = 2;
        spot.falloff = 0;
        spot.range = 10.0f;
        spot.color = {0, 0, 1};
        spot.cosInner = 0.95f;
        spot.cosOuter = 0.50f;
        spot.pos = {1.0f / 3.0f, 2.0f, 1.0f / 3.0f};
        spot.dir = {0, -1, 0};
        SceneLights::Add(spot);
        Mesh m = UpFace();
        BakeFaceLighting(m, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(m.triColors[0], 0.0f, 0.0f, 0.64f);
        SceneLights::Clear();
        spot.dir = {1, 0, 0};
        SceneLights::Add(spot);
        BakeFaceLighting(m, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(m.triColors[0], 0.0f, 0.0f, 0.0f);
    }

    // An area light still fills a face it does not face head-on. A point light there does not.
    {
        ResetLights();
        LightSample area;
        area.type = 3;
        area.falloff = 0;
        area.range = 10.0f;
        area.color = {1, 0, 0};
        area.pos = {1.0f / 3.0f + 2.0f, 0.0f, 1.0f / 3.0f};
        SceneLights::Add(area);
        Mesh m = UpFace();
        BakeFaceLighting(m, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(m.triColors[0], (1.0f / 3.0f) * 0.64f, 0.0f, 0.0f);
        SceneLights::Clear();
        area.type = 1;
        SceneLights::Add(area);
        BakeFaceLighting(m, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(m.triColors[0], 0.0f, 0.0f, 0.0f);
    }

    // Hemisphere ambient: an upward face takes the sky colour, a downward face the ground.
    {
        ResetLights();
        SceneLights::Hemisphere() = true;
        SceneLights::SkyAmbient() = Vec3{0, 0, 1};
        SceneLights::GroundAmbient() = Vec3{1, 0, 0};
        Mesh up = UpFace();
        Mesh down = DownFace();
        BakeFaceLighting(up, Mat4::Identity(), Color::White, kNoHit, 0);
        BakeFaceLighting(down, Mat4::Identity(), Color::White, kNoHit, 0);
        Expect(up.triColors[0], 0.0f, 0.0f, 1.0f);
        Expect(down.triColors[0], 1.0f, 0.0f, 0.0f);
    }

    // A blocker above the near face puts that face in shadow and leaves a far face lit.
    // With no lights, occluded hemisphere rays darken ambient by up to 70%, and
    // aoSamples is capped at the six built-in directions.
    {
        ResetLights();
        LightSample sun;
        sun.type = 0;
        sun.dir = {0, -1, 0};
        sun.color = {1, 0, 0};
        SceneLights::Add(sun);
        const Vec3 b0{0, 1, 0}, b1{0, 1, 1}, b2{1, 1, 0};
        auto blocked = [&](const Vec3& o, const Vec3& d, float maxDist) {
            float t = 0;
            return RayTriangle(o, d, b0, b1, b2, maxDist, t);
        };
        Mesh both;
        both.vertices = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0},
                         {20, 0, 0}, {20, 0, 1}, {21, 0, 0}};
        both.triangles = {0, 1, 2, 3, 4, 5};
        BakeFaceLighting(both, Mat4::Identity(), Color::White, blocked, 0);
        Expect(both.triColors[0], 0.0f, 0.0f, 0.0f);          // shadowed: the red sun is blocked, ambient is 0
        Expect(both.triColors[1], 1.0f, 0.0f, 0.0f);          // the far face is not under the blocker

        ResetLights();
        SceneLights::SetAmbient(1.0f);
        Mesh m = UpFace();
        int calls = 0;
        auto firstOnly = [&](const Vec3&, const Vec3&, float) { return ++calls == 1; };
        BakeFaceLighting(m, Mat4::Identity(), Color::White, firstOnly, 6);
        CHECK(calls == 6);
        Expect(m.triColors[0], 1.0f - 0.7f / 6.0f, 1.0f - 0.7f / 6.0f, 1.0f - 0.7f / 6.0f);
        calls = 0;
        BakeFaceLighting(m, Mat4::Identity(), Color::White, firstOnly, 1);
        CHECK(calls == 1);
        Expect(m.triColors[0], 0.3f, 0.3f, 0.3f);
        calls = 0;
        BakeFaceLighting(m, Mat4::Identity(), Color::White, firstOnly, 100);
        CHECK(calls == 6);                                    // never more than the six directions
        BakeFaceLighting(m, Mat4::Identity(), Color::White, kAlways, 0);
        Expect(m.triColors[0], 1.0f, 1.0f, 1.0f);             // aoSamples 0 skips occlusion darkening

        // Shadow and AO together: the sun is blocked and the ambient is darkened. Not red.
        SceneLights::Clear();
        SceneLights::SetAmbient(1.0f);
        SceneLights::Add(sun);
        BakeFaceLighting(m, Mat4::Identity(), Color::White, kAlways, 6);
        Expect(m.triColors[0], 0.3f, 0.3f, 0.3f);
    }

    ResetLights();
    TEST_MAIN_RESULT();
}
