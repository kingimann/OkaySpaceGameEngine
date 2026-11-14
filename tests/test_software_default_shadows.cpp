#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace okay;

// Software renderer: shadows draw with the DEFAULT settings.
// Before, shadows were sampled only on the per-pixel lighting path, which is off by default, so a default
// software frame had none. Now a mesh that receives shadows takes the per-pixel path whenever this frame
// has a shadow map (ShadowsUsePerPixel(), default on). Switching it off gives the old per-vertex frame.
namespace {
GameObject* MakeMesh(Scene& s, const char* name, Mesh m, Vec3 pos) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = pos;
    go->AddComponent<MeshRenderer>()->mesh = m;
    return go;
}
int PixelAt(const Raster& r, const Mat4& vp, const Vec3& p) {
    Vec4 c = vp * Vec4{p, 1.0f};
    int x = (int)((c.x / c.w * 0.5f + 0.5f) * r.width);
    int y = (int)((1.0f - (c.y / c.w * 0.5f + 0.5f)) * r.height);
    std::uint32_t v = r.Get(x, y);
    return (int)(v & 0xFF) + (int)((v >> 8) & 0xFF) + (int)((v >> 16) & 0xFF);
}
// A sun straight down, a ground plane and a box.
void Build(Scene& s, bool sunCasts) {
    MakeMesh(s, "Ground", Mesh::Plane(30.0f), {0, 0, 0});
    MakeMesh(s, "Box", Mesh::Cube(2.0f), {0, 1, 0});
    GameObject* go = s.CreateGameObject("Sun");
    go->transform->localRotation = Quat::LookRotation(Vec3{0.3f, -1, 0.2f}.Normalized(), Vec3{0, 0, 1});
    auto* l = go->AddComponent<Light>();
    l->type = Light::Type::Directional; l->castShadows = sunCasts;
}
double Seconds(const std::function<void()>& f, int reps) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) f();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / reps;
}
} // namespace

int main() {
    RUN_SUITE("software_default_shadows");
    const Vec3 eye{0, 12, 12};
    const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 200.0f) * Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});

    // The defaults the player gets.
    CHECK(!PerPixelLighting());
    CHECK(ShadowsEnabled());
    CHECK(ShadowsUsePerPixel());
    ShadowMapResolution() = 1024;

    auto frame = [&](bool sunCasts, Raster& r) {
        Scene s("d"); Build(s, sunCasts);
        ApplySceneLight(s);
        r.Resize(128, 128); r.Clear(0xFF000000u);
        RenderMeshes(r, s, vp, eye);
    };
    // Where the box's shadow falls: the light travels toward +x, -y, +z, so the shadow lands at +x / +z of the box.
    const Vec3 inShadow{1.3f, 0, 0.5f}, open{-8, 0, -4};

    Raster a, b, c, d;
    frame(true, a);     // defaults: per-pixel lighting off, shadows on
    frame(false, b);    // same, but the sun casts no shadow
    CHECK(PixelAt(b, vp, inShadow) > 0);
    CHECK(PixelAt(a, vp, inShadow) < PixelAt(b, vp, inShadow));            // the shadow is there by default
    CHECK(PixelAt(b, vp, inShadow) - PixelAt(a, vp, inShadow) > 20);       // clearly
    CHECK(PixelAt(a, vp, open) == PixelAt(b, vp, open));                   // open ground is untouched

    // The way back: switch it off and the frame is the per-vertex one, with no shadow.
    ShadowsUsePerPixel() = false;
    frame(true, c); frame(false, d);
    CHECK(PixelAt(c, vp, inShadow) == PixelAt(d, vp, inShadow));
    CHECK(c.color == d.color);
    ShadowsUsePerPixel() = true;

    // The global Shadows switch still turns it all off, and then the cheap path is used.
    ShadowsEnabled() = false;
    Raster e; frame(true, e);
    CHECK(e.color == d.color);
    ShadowsEnabled() = true;

    // A mesh that does not receive shadows keeps the cheap path (no shadow on it).
    {
        Scene s("norecv"); Build(s, true);
        s.Find("Ground")->GetComponent<MeshRenderer>()->receiveShadows = false;
        ApplySceneLight(s);
        Raster r; r.Resize(128, 128); r.Clear(0xFF000000u);
        RenderMeshes(r, s, vp, eye);
        CHECK(PixelAt(r, vp, inShadow) == PixelAt(d, vp, inShadow));
    }

    // A scene with no shadow-casting light pays nothing: shadow map is off, so the flat path stays.
    {
        Scene s("nosun"); MakeMesh(s, "Ground", Mesh::Plane(30.0f), {0, 0, 0});
        ApplySceneLight(s);
        Raster r; r.Resize(32, 32); r.Clear(0xFF000000u);
        RenderMeshes(r, s, vp, eye);
        CHECK(!LocalShadowsActive());
    }

    // ---- Cost: a busier scene, default frame with and without the per-pixel path.
    // Printed so the numbers are on record; the bound is loose (machines vary) and only catches a blow-up.
    {
        Scene s("bench");
        MakeMesh(s, "Ground", Mesh::Plane(60.0f), {0, 0, 0});
        for (int i = 0; i < 40; ++i)
            MakeMesh(s, "B", i % 2 ? Mesh::Sphere(1.0f, 24, 16) : Mesh::Cube(1.5f), {(float)(i % 8) * 3 - 10, 1, (float)(i / 8) * 3 - 6});
        GameObject* go = s.CreateGameObject("Sun");
        go->transform->localRotation = Quat::LookRotation(Vec3{0.3f, -1, 0.2f}.Normalized(), Vec3{0, 0, 1});
        go->AddComponent<Light>()->type = Light::Type::Directional;
        ApplySceneLight(s);
        Raster r; r.Resize(640, 360); r.Clear(0xFF000000u);
        const Mat4 vp2 = Mat4::Perspective(60.0f, 16.0f / 9.0f, 0.1f, 200.0f) * Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
        auto run = [&] { r.Clear(0xFF000000u); RenderMeshes(r, s, vp2, eye); };
        run();
        ShadowsUsePerPixel() = true;  const double on  = Seconds(run, 5);
        ShadowsUsePerPixel() = false; const double off = Seconds(run, 5);
        ShadowsUsePerPixel() = true;
        std::printf("  bench 640x360, 41 meshes, sun shadow: per-pixel+shadows %.1f ms, per-vertex no shadows %.1f ms (x%.2f)\n",
                    on * 1e3, off * 1e3, on / (off > 1e-9 ? off : 1e-9));
        CHECK(on < off * 12.0 + 0.05);   // loose: catches a blow-up, not a few percent
    }

    ShadowsEnabled() = false;
    SceneLight::Reset();
    TEST_MAIN_RESULT();
}
