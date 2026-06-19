#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("software_renderer");

    // --- Depth test: a nearer triangle wins the pixel regardless of draw order ---
    {
        Raster r; r.Resize(10, 10);
        r.Clear(0xFF000000u);                       // opaque black
        std::uint32_t red  = 0xFF0000FFu;           // ABGR: red
        std::uint32_t blue = 0xFFFF0000u;           // ABGR: blue
        // A big FAR blue triangle, then a big NEAR red one drawn AFTER it.
        r.Triangle(0, 0, 5.0f,  10, 0, 5.0f,  0, 10, 5.0f, blue);   // far
        r.Triangle(0, 0, 1.0f,  10, 0, 1.0f,  0, 10, 1.0f, red);    // near
        CHECK(r.Get(1, 1) == red);                  // near wins
        CHECK(r.Depth(1, 1) <= 1.01f);

        // Now draw a FAR triangle AFTER the near one — it must NOT overwrite.
        r.Triangle(0, 0, 9.0f,  10, 0, 9.0f,  0, 10, 9.0f, blue);
        CHECK(r.Get(1, 1) == red);                  // still red (depth rejected the far one)
    }

    // --- Pixels outside the triangle are untouched ---
    {
        Raster r; r.Resize(8, 8);
        r.Clear(0xFF112233u);
        r.Triangle(0, 0, 1.0f, 2, 0, 1.0f, 0, 2, 1.0f, 0xFFFFFFFFu); // small corner tri
        CHECK(r.Get(7, 7) == 0xFF112233u);          // far corner unchanged
    }

    // --- RenderMeshes fills the buffer for a cube facing the camera ---
    {
        Scene scene("R");
        GameObject* go = scene.CreateGameObject("Cube");
        go->AddComponent<MeshRenderer>()->color = Color::FromBytes(200, 50, 50);

        Raster r; r.Resize(64, 64);
        r.Clear(0xFF000000u);
        // Camera at +Z looking at origin.
        Mat4 view = Mat4::LookAt({0, 0, 4}, {0, 0, 0}, Vec3::Up);
        Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f);
        RenderMeshes(r, scene, proj * view, {0, 0, 4});

        // The center pixel should be covered by the cube (not the black clear).
        CHECK(r.Get(32, 32) != 0xFF000000u);
        // Depth there is finite (something was drawn).
        CHECK(r.Depth(32, 32) < 1e29f);
    }

    // --- Frustum cull: a mesh far off to the side isn't drawn ---
    {
        Scene scene("Cull");
        GameObject* go = scene.CreateGameObject("Cube");
        go->AddComponent<MeshRenderer>()->color = Color::FromBytes(200, 50, 50);
        go->transform->localPosition = {1000, 0, 0};   // way off-screen to the right

        Raster r; r.Resize(64, 64);
        r.Clear(0xFF000000u);
        Mat4 view = Mat4::LookAt({0, 0, 4}, {0, 0, 0}, Vec3::Up);
        Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f);
        RenderMeshes(r, scene, proj * view, {0, 0, 4});
        CHECK(r.Get(32, 32) == 0xFF000000u);           // nothing rasterized
    }

    // --- Distance fog tints a far surface toward the fog color ---
    {
        auto centerColor = [](bool fog) {
            Scene scene("Fog");
            GameObject* go = scene.CreateGameObject("Plane");
            auto* mr = go->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Cube();
            mr->color = Color::FromBytes(200, 0, 0);   // red
            mr->unlit = true;                          // isolate fog from shading
            go->transform->localPosition = {0, 0, -40};
            scene.renderSettings.fog = fog;
            scene.renderSettings.fogColor = Color::FromBytes(0, 0, 255); // blue
            scene.renderSettings.fogStart = 10.0f;
            scene.renderSettings.fogEnd = 60.0f;
            Raster r; r.Resize(64, 64); r.Clear(0xFF000000u);
            Mat4 view = Mat4::LookAt({0, 0, 4}, {0, 0, 0}, Vec3::Up);
            Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 200.0f);
            RenderMeshes(r, scene, proj * view, {0, 0, 4});
            return r.Get(32, 32);
        };
        std::uint32_t noFog = centerColor(false);
        std::uint32_t withFog = centerColor(true);
        CHECK(noFog != 0xFF000000u);                   // the cube was drawn
        CHECK(withFog != noFog);                       // fog changed the color
        // With blue fog, the blue channel (ABGR: bits 16-23) should rise.
        int bNo = (int)((noFog >> 16) & 0xFF);
        int bFog = (int)((withFog >> 16) & 0xFF);
        CHECK(bFog > bNo);
    }

    TEST_MAIN_RESULT();
}
