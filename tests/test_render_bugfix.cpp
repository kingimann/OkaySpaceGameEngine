// Regression tests for renderer bugs found in an audit (CPU side, headless).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/Lightmap.hpp"
#include <cmath>

using namespace okay;

static Mat4 CamVP(const Vec3& eye) {
    const Mat4 view = Mat4::LookAt(eye, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 500.0f);
    return proj * view;
}

int main() {
    RUN_SUITE("transparent meshes draw far to near");
    {
        Scene s;
        // Scene order is near, far, middle: the old list kept exactly this order.
        const float zs[3] = {2.0f, -6.0f, -2.0f};
        GameObject* made[3];
        for (int i = 0; i < 3; ++i) {
            GameObject* go = s.CreateGameObject("Glass");
            go->transform->localPosition = {0, 0, zs[i]};
            auto* mr = go->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Cube();
            mr->color = Color(1, 1, 1, 0.5f);
            made[i] = go;
        }
        GameObject* solid = s.CreateGameObject("Solid");
        solid->transform->localPosition = {3, 0, -3};
        auto* smr = solid->AddComponent<MeshRenderer>();
        smr->mesh = Mesh::Cube();
        const Vec3 eye{0, 0, 10};
        const Mat4 view = Mat4::LookAt(eye, {0, 0, 0}, {0, 1, 0});
        const Mat4 vp = Mat4::Perspective(60.0f, 1.0f, 0.1f, 500.0f) * view;
        const visibility::Frame& f = visibility::Build(s, vp, eye, 256, 256, 60, nullptr);
        CHECK(f.items.size() == 4u);
        if (f.items.size() == 4u) {
            CHECK(f.items[0].go == solid);          // opaque first
            CHECK(f.items[1].go == made[1]);        // z = -6, farthest
            CHECK(f.items[2].go == made[2]);        // z = -2
            CHECK(f.items[3].go == made[0]);        // z = +2, nearest, drawn last
        }
    }

    RUN_SUITE("static batching never merges different looks");
    {
        using Edit = void (*)(MeshRenderer&);
        struct Case { const char* what; Edit edit; };
        const Case cases[] = {
            {"same look (control, must merge)", [](MeshRenderer&) {}},
            {"toon bands", [](MeshRenderer& m) { m.shader = MeshRenderer::Shader::Toon; m.toonBands = 5; }},
            {"rim strength", [](MeshRenderer& m) { m.rimStrength = 0.8f; }},
            {"rim power", [](MeshRenderer& m) { m.rimPower = 7.0f; }},
            {"rim colour", [](MeshRenderer& m) { m.rimColor = Color(1, 0, 0, 1); }},
            {"gradient top", [](MeshRenderer& m) { m.gradientTop = Color(1, 0, 0, 1); }},
            {"gradient bottom", [](MeshRenderer& m) { m.gradientBottom = Color(0, 1, 0, 1); }},
            {"casts shadows", [](MeshRenderer& m) { m.castShadows = false; }},
            {"receives shadows", [](MeshRenderer& m) { m.receiveShadows = false; }},
            {"normal strength", [](MeshRenderer& m) { m.normalStrength = 2.0f; }},
            {"ao strength", [](MeshRenderer& m) { m.aoStrength = 0.2f; }},
            {"matcap", [](MeshRenderer& m) { m.matcap = "shiny.png"; }},
        };
        for (const Case& c : cases) {
            Scene s;
            GameObject* a = s.CreateGameObject("A");
            GameObject* b = s.CreateGameObject("B");
            a->isStatic = b->isStatic = true;
            a->transform->localPosition = {-1.5f, 0, 0};
            b->transform->localPosition = {1.5f, 0, 0};
            auto* ma = a->AddComponent<MeshRenderer>();
            auto* mb = b->AddComponent<MeshRenderer>();
            ma->mesh = Mesh::Cube(); mb->mesh = Mesh::Cube();
            c.edit(*mb);
            const Vec3 eye{0, 0, 8};
            const Mat4 vp = CamVP(eye);
            const visibility::Frame& f = visibility::Build(s, vp, eye, 256, 256, 60, nullptr);
            const bool control = c.what[0] == 's';
            const std::size_t want = control ? 1u : 2u;
            if (f.items.size() != want) std::cout << "  batching case: " << c.what << " gave " << f.items.size() << " draws\n";
            CHECK(f.items.size() == want);
        }
    }

    RUN_SUITE("camera field of view stays finite");
    {
        Scene s;
        GameObject* go = s.CreateGameObject("Cam");
        auto* cam = go->AddComponent<Camera>();
        const float fovs[] = {0.0f, -10.0f, 180.0f, 179.99f, 400.0f, std::nanf("")};
        for (float fov : fovs) {
            cam->fieldOfView = fov;
            const Mat4 p = cam->ProjectionMatrix(16.0f / 9.0f);
            bool finite = true;
            for (int i = 0; i < 16; ++i) finite = finite && std::isfinite(p.m[i]);
            CHECK(finite);
            CHECK(p.at(1, 1) > 0.0f);     // not mirrored upside down
        }
    }

    RUN_SUITE("mirror and combine keep texture coordinates");
    {
        Mesh tri;
        tri.vertices = {{1, 0, 0}, {2, 0, 0}, {1, 1, 0}};
        tri.triangles = {0, 1, 2};
        tri.uvs = {{0, 0}, {1, 0}, {0, 1}};
        tri.triColors = {Color(1, 0, 0, 1)};
        Mesh m = tri.Mirrored(0, false);
        CHECK(m.vertices.size() == 6u);
        CHECK(m.TriangleCount() == 2);
        CHECK(m.HasUVs());
        CHECK(m.HasFaceColors());
        if (m.HasUVs()) {
            CHECK_NEAR(m.uvs[4].x, 1.0f, 1e-6f);   // copy of vertex 1 keeps its UV
            CHECK_NEAR(m.uvs[5].y, 1.0f, 1e-6f);
        }
        Mesh a = tri, b = tri;
        b.vertices[0].z = 5.0f;
        a.Combine(b);
        CHECK(a.TriangleCount() == 2);
        CHECK(a.HasUVs());
        CHECK(a.HasFaceColors());
        Mesh plain;                       // no UVs: the textured half must keep its own
        plain.vertices = {{0, 0, 3}, {1, 0, 3}, {0, 1, 3}};
        plain.triangles = {0, 1, 2};
        Mesh c = tri;
        c.Combine(plain);
        CHECK(c.HasUVs());
        if (c.HasUVs()) CHECK_NEAR(c.uvs[1].x, 1.0f, 1e-6f);
    }

    RUN_SUITE("sprite sort key orders every int sort order");
    {
        SpriteRenderer lo, mid, hi, back;
        lo.sortOrder = -2000000000; mid.sortOrder = 0; hi.sortOrder = 2000000000;
        CHECK(lo.SortKey() < mid.SortKey());
        CHECK(mid.SortKey() < hi.SortKey());
        back.sortingLayer = -1; back.sortOrder = 2000000000;
        mid.sortingLayer = 0;   mid.sortOrder = -2000000000;
        CHECK(back.SortKey() < mid.SortKey());    // the layer always wins
    }

    RUN_SUITE("baked light matches what the renderer would shade");
    {
        SceneLights::Clear();
        SceneLights::SetAmbient(0.0f);
        SceneLights::Hemisphere() = false;
        LightSample sun;
        sun.type = 0; sun.dir = {0, -1, 0}; sun.color = {3.0f, 3.0f, 3.0f};   // overbright
        SceneLights::Add(sun);
        Mesh face;
        face.vertices = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}};   // normal +Y
        face.triangles = {0, 1, 2};
        const Color base(0.8f, 0.5f, 0.2f, 1.0f);
        BakeFaceLighting(face, Mat4::Identity(), base,
                         [](const Vec3&, const Vec3&, float) { return false; }, 0);
        const Vec3 live = SceneLights::ShadeNormalized({1.0f / 3, 0, 1.0f / 3}, {0, 1, 0});
        CHECK_NEAR(face.triColors[0].r, base.r * live.x, 2e-3f);
        CHECK_NEAR(face.triColors[0].g, base.g * live.y, 2e-3f);
        CHECK_NEAR(face.triColors[0].b, base.b * live.z, 2e-3f);
        SceneLights::Clear();
    }

    TEST_MAIN_RESULT();
}
