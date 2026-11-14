#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include <cmath>

using namespace okay;

// Software renderer: spot and point (and area) light shadows.
//   * the shadow factor under a caster is dark and beside it is lit, for a spot,
//     a point light, and a wall seen through a side cube face;
//   * the light's Cast Shadows and Strength, the mesh's Cast / Receive Shadows and
//     the global Shadows switch each change the result;
//   * RenderMeshes (per-pixel path) draws the shadow as darker pixels, the sun's
//     shadow and a lamp's shadow do not darken each other, and with local shadows
//     off the picture is exactly what it was before.
namespace {
GameObject* MakeMesh(Scene& s, const char* name, Mesh m, Vec3 pos) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = pos;
    go->AddComponent<MeshRenderer>()->mesh = m;
    return go;
}
Light* MakeLight(Scene& s, const char* name, Light::Type t, Vec3 pos, Vec3 aim) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = pos;
    go->transform->localRotation = Quat::LookRotation(aim, std::fabs(aim.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
    auto* l = go->AddComponent<Light>();
    l->type = t;
    return l;
}
// Brightness (sum of channels) of the pixel a world point lands on.
int PixelAt(const Raster& r, const Mat4& vp, const Vec3& p) {
    Vec4 c = vp * Vec4{p, 1.0f};
    int x = (int)((c.x / c.w * 0.5f + 0.5f) * r.width);
    int y = (int)((1.0f - (c.y / c.w * 0.5f + 0.5f)) * r.height);
    std::uint32_t v = r.Get(x, y);
    return (int)(v & 0xFF) + (int)((v >> 8) & 0xFF) + (int)((v >> 16) & 0xFF);
}
} // namespace

int main() {
    RUN_SUITE("software_local_shadows");

    const Vec3 eye{0, 14, 14};
    const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 200.0f) *
                    Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    ShadowsEnabled() = true;
    LocalShadowResolution() = 256;
    const Vec3 up{0, 1, 0};

    // --- Point light over a cube: dark under it, lit beside it ---------------
    {
        Scene s("point");
        MakeMesh(s, "Ground", Mesh::Plane(30.0f), {0, 0, 0});
        MakeMesh(s, "Box", Mesh::Cube(2.0f), {0, 2, 0});
        Light* lamp = MakeLight(s, "Lamp", Light::Type::Point, {0, 6, 0}, {0, -1, 0});
        lamp->range = 20.0f;
        ApplySceneLight(s);
        RenderLocalShadowMaps(s, vp, eye);
        CHECK(LocalShadowsActive());
        CHECK(LocalShadows().maps.size() == 1);
        CHECK(LocalShadows().maps[0].view.tiles == 6);
        CHECK(LocalShadowFactor(0, Vec3{0, 0, 0}, up) < 0.15f);          // under the box
        CHECK(LocalShadowFactor(0, Vec3{0.4f, 0, -0.3f}, up) < 0.15f);   // still under it
        CHECK(LocalShadowFactor(0, Vec3{6, 0, 0}, up) > 0.95f);          // beside it (+x face)
        CHECK(LocalShadowFactor(0, Vec3{-6, 0, 3}, up) > 0.95f);         // (-x face)
        CHECK(LocalShadowFactor(0, Vec3{1, 0, 7}, up) > 0.95f);          // (+z face)
        CHECK(LocalShadowFactor(0, Vec3{0, 3.0f, 0}, up) > 0.95f);       // the box top, facing the lamp
        // A point past the light's range gets no shadow (it gets no light either).
        CHECK_NEAR(LocalShadowFactor(0, Vec3{0, 0, 40}, up), 1.0f, 1e-6f);
        // The plane crosses behind the light for the side faces: the near-plane cut
        // must not lose the ground, so the shadow under the box is still found
        // when the box sits off to the side of the light.
        MakeMesh(s, "Box2", Mesh::Cube(1.0f), {5, 1, 0});
        ApplySceneLight(s);
        RenderLocalShadowMaps(s, vp, eye);
        CHECK(LocalShadowFactor(0, Vec3{6.2f, 0, 0}, up) < 0.15f);       // behind Box2, seen from the lamp
        CHECK(LocalShadowFactor(0, Vec3{5, 0, 5}, up) > 0.95f);          // clear ground

        // Strength scales the darkness; Cast Shadows off removes it.
        lamp->shadowStrength = 0.5f;
        ApplySceneLight(s); RenderLocalShadowMaps(s, vp, eye);
        float half = LocalShadowFactor(0, Vec3{0, 0, 0}, up);
        CHECK(half > 0.45f && half < 0.6f);
        lamp->shadowStrength = 1.0f; lamp->castShadows = false;
        ApplySceneLight(s); RenderLocalShadowMaps(s, vp, eye);
        CHECK(!LocalShadowsActive());
        CHECK_NEAR(LocalShadowFactor(0, Vec3{0, 0, 0}, up), 1.0f, 1e-6f);
        lamp->castShadows = true;
        // The global switch turns the lot off.
        ShadowsEnabled() = false;
        ApplySceneLight(s); RenderLocalShadowMaps(s, vp, eye);
        CHECK(!LocalShadowsActive());
        ShadowsEnabled() = true;
        // A caster that does not cast casts nothing.
        s.Find("Box")->GetComponent<MeshRenderer>()->castShadows = false;
        s.Find("Box2")->GetComponent<MeshRenderer>()->castShadows = false;
        ApplySceneLight(s); RenderLocalShadowMaps(s, vp, eye);
        CHECK_NEAR(LocalShadowFactor(0, Vec3{0, 0, 0}, up), 1.0f, 1e-6f);
    }

    // --- Spot light: shadow inside the cone, nothing outside it --------------
    {
        Scene s("spot");
        MakeMesh(s, "Ground", Mesh::Plane(30.0f), {0, 0, 0});
        MakeMesh(s, "Box", Mesh::Cube(2.0f), {0, 2, 0});
        Light* sp = MakeLight(s, "Spot", Light::Type::Spot, {0, 7, 0}, {0, -1, 0});
        sp->range = 20.0f; sp->spotAngle = 60.0f;
        ApplySceneLight(s);
        RenderLocalShadowMaps(s, vp, eye);
        CHECK(LocalShadows().maps.size() == 1 && LocalShadows().maps[0].view.tiles == 1);
        CHECK(LocalShadowFactor(0, Vec3{0, 0, 0}, up) < 0.15f);
        CHECK(LocalShadowFactor(0, Vec3{3, 0, 0}, up) > 0.95f);
        CHECK(LocalShadowFactor(0, Vec3{0, 0, -3}, up) > 0.95f);
        // Far outside the cone the map does not reach: lit (the cone gives no light there anyway).
        CHECK_NEAR(LocalShadowFactor(0, Vec3{25, 0, 25}, up), 1.0f, 1e-6f);
        // Tilt the spot: the shadow moves with it.
        sp->gameObject->transform->localPosition = {-6, 7, 0};
        sp->gameObject->transform->localRotation = Quat::LookRotation(Vec3{6, -7, 0}.Normalized(), up);
        ApplySceneLight(s); RenderLocalShadowMaps(s, vp, eye);
        CHECK(LocalShadowFactor(0, Vec3{-2.0f, 0, 0}, up) > 0.9f);       // the box's shadow is now further +x (x 2.2 .. 6.2)
        CHECK(LocalShadowFactor(0, Vec3{4.0f, 0, 0}, up) < 0.2f);
    }

    // --- Budget: 16 tiles, nearest first; one point light = 6 tiles ----------
    {
        Scene s("budget");
        MakeMesh(s, "Ground", Mesh::Plane(30.0f), {0, 0, 0});
        MakeMesh(s, "Box", Mesh::Cube(1.0f), {0, 0.5f, 0});
        for (int i = 0; i < 4; ++i) {
            Light* l = MakeLight(s, "P", Light::Type::Point, {(float)i * 2 - 3, 4, 0}, {0, -1, 0});
            l->range = 12.0f;
        }
        ApplySceneLight(s);
        RenderLocalShadowMaps(s, vp, eye);
        CHECK(LocalShadows().maps.size() == 2);        // 6 + 6 = 12 tiles; a third needs 18
        int mapped = 0; for (int i = 0; i < 4; ++i) mapped += LocalShadows().lightMap[i] >= 0;
        CHECK(mapped == 2);
    }

    // --- Per-light factors in the lighting sum -------------------------------
    {
        Scene s("sum");
        MakeLight(s, "Sun", Light::Type::Directional, {0, 9, 0}, {0, -1, 0});
        Light* lamp = MakeLight(s, "Lamp", Light::Type::Point, {0, 3, 0}, {0, -1, 0});
        lamp->color = Color{1, 0, 0, 1}; lamp->range = 10.0f;
        ApplySceneLight(s);
        const Vec3 p{0, 0, 0};
        Vec3 full = SceneLights::ShadeNormalized(p, up);
        CHECK_NEAR(full.x, SceneLights::ShadeNormalized(p, up, nullptr).x, 1e-6f);   // nullptr = as before
        float all1[16]; for (float& f : all1) f = 1.0f;
        Vec3 same = SceneLights::ShadeNormalized(p, up, all1);
        CHECK_NEAR(same.x, full.x, 1e-6f); CHECK_NEAR(same.y, full.y, 1e-6f);
        float noLamp[16]; for (float& f : noLamp) f = 1.0f;
        noLamp[SceneLights::SunIndex() == 0 ? 1 : 0] = 0.0f;                          // the lamp
        Vec3 sunOnly = SceneLights::ShadeNormalized(p, up, noLamp);
        CHECK(sunOnly.y <= full.y + 1e-6f);
        CHECK(sunOnly.x <= full.x);                    // red from the lamp is gone, the sun's white stays
        float noSun[16]; for (float& f : noSun) f = 1.0f;
        noSun[SceneLights::SunIndex()] = 0.0f;
        Vec3 lampOnly = SceneLights::ShadeNormalized(p, up, noSun);
        CHECK(lampOnly.x > lampOnly.y);                // only the red lamp + ambient is left
    }

    // --- RenderMeshes: the shadow is drawn on the per-pixel path -------------
    {
        // A real sun that casts no shadow (and gives a little fill), so the only
        // shadow in the frame is the lamp's.
        auto render = [&](bool lampCasts, bool ppl, int* shadowPx, int* litPx) {
            Scene s("draw");
            MakeMesh(s, "Ground", Mesh::Plane(30.0f), {0, 0, 0});
            MakeMesh(s, "Box", Mesh::Cube(2.0f), {0, 2, 0});
            Light* sun = MakeLight(s, "Sun", Light::Type::Directional, {0, 20, 0}, {0, -1, 0});
            sun->castShadows = false; sun->intensity = 0.2f;
            Light* lamp = MakeLight(s, "Lamp", Light::Type::Point, {0, 7, 0}, {0, -1, 0});
            lamp->range = 25.0f; lamp->intensity = 1.5f; lamp->castShadows = lampCasts;
            SceneLight::SetAmbient(0.15f);
            ApplySceneLight(s);
            PerPixelLighting() = ppl;
            Raster r; r.Resize(96, 96); r.Clear(0xFF000000u);
            RenderMeshes(r, s, vp, eye);
            *shadowPx = PixelAt(r, vp, Vec3{0.2f, 0, 1.6f});   // on the ground in front of the box, inside its shadow
            *litPx = PixelAt(r, vp, Vec3{7, 0, 3});            // open ground
        };
        int sh1, li1, sh0, li0, shF1, liF1, shF0, liF0;
        render(true, true, &sh1, &li1);      // lamp casts, per-pixel
        render(false, true, &sh0, &li0);     // lamp does not cast
        render(true, false, &shF1, &liF1);   // flat path, lamp casts
        render(false, false, &shF0, &liF0);  // flat path, lamp does not cast
        CHECK(li1 > 0 && sh0 > 0);                        // the ground is lit and drawn
        CHECK(sh1 < sh0);                                 // the shadow made that pixel darker
        CHECK(sh0 - sh1 > 20);                            // clearly, not by rounding
        CHECK(li1 == li0);                                // away from the shadow nothing changed
        CHECK(shF1 == shF0 && liF1 == liF0);              // the flat (per-vertex) path draws no shadows, as before
        PerPixelLighting() = false;
    }

    ShadowsEnabled() = false;
    SceneLight::Reset();
    TEST_MAIN_RESULT();
}
