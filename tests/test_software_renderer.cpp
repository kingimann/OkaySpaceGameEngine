#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("software_renderer");

    // --- Depth test (W-buffer: depth = 1/w, so LARGER = nearer) ---
    {
        Raster r; r.Resize(10, 10);
        r.Clear(0xFF000000u);                       // opaque black
        std::uint32_t red  = 0xFF0000FFu;           // ABGR: red
        std::uint32_t blue = 0xFFFF0000u;           // ABGR: blue
        // A big FAR blue triangle (small 1/w), then a big NEAR red one (large 1/w).
        r.Triangle(0, 0, 0.2f,  10, 0, 0.2f,  0, 10, 0.2f, blue);   // far
        r.Triangle(0, 0, 1.0f,  10, 0, 1.0f,  0, 10, 1.0f, red);    // near
        CHECK(r.Get(1, 1) == red);                  // near wins
        CHECK(r.Depth(1, 1) >= 0.99f);              // stored the near 1/w

        // Now draw a FARTHER triangle (even smaller 1/w) AFTER — it must NOT overwrite.
        r.Triangle(0, 0, 0.1f,  10, 0, 0.1f,  0, 10, 0.1f, blue);
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

    // --- Watertight: a quad split into two triangles sharing a diagonal must have
    //     NO crack (every interior pixel covered) and the top-left fill rule means no
    //     pixel is owned by both triangles (no overlap z-fight shimmer). ---
    {
        Raster r; r.Resize(32, 32);
        r.Clear(0xFF000000u);                       // black background
        std::uint32_t white = 0xFFFFFFFFu;
        // Quad corners (4..28). Two triangles share the diagonal (4,4)-(28,28).
        r.Triangle(4, 4, 1.0f,  28, 4, 1.0f,  28, 28, 1.0f, white);  // upper-right
        r.Triangle(4, 4, 1.0f,  28, 28, 1.0f, 4, 28, 1.0f, white);   // lower-left
        // Every pixel well inside the quad must be filled — no dashed crack on the
        // shared diagonal.
        int gaps = 0;
        for (int y = 6; y <= 26; ++y)
            for (int x = 6; x <= 26; ++x)
                if (r.Get(x, y) != white) ++gaps;
        CHECK(gaps == 0);

        // The diagonal pixels themselves are covered (the shared edge isn't dropped).
        CHECK(r.Get(16, 16) == white);
        CHECK(r.Get(10, 10) == white);
        CHECK(r.Get(22, 22) == white);
    }

    TEST_MAIN_RESULT();
}
