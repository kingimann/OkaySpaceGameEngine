#include "test_framework.hpp"
#include <Okay.hpp>
#include <set>

using namespace okay;

int main() {
    RUN_SUITE("shaders");

    // Material preset round-trips the shader model + cel band count.
    {
        Material m;
        m.shader = MeshRenderer::Shader::Toon;
        m.toonBands = 5;
        Material r = Material::FromText(m.ToText());
        CHECK(r.shader == MeshRenderer::Shader::Toon);
        CHECK(r.toonBands == 5);

        // FromRenderer / ApplyTo carry the fields too.
        MeshRenderer mr;
        m.ApplyTo(mr);
        CHECK(mr.shader == MeshRenderer::Shader::Toon);
        CHECK(mr.toonBands == 5);
        Material back = Material::FromRenderer(mr);
        CHECK(back.shader == MeshRenderer::Shader::Toon);
        CHECK(back.toonBands == 5);
    }

    // Scene serialization round-trips the MeshRenderer shader.
    {
        Scene s("S"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Cube");
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->shader = MeshRenderer::Shader::Toon; mr->toonBands = 4;

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* mr2 = s2.Find("Cube") ? s2.Find("Cube")->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr2 != nullptr);
        if (mr2) {
            CHECK(mr2->shader == MeshRenderer::Shader::Toon);
            CHECK(mr2->toonBands == 4);
        }
    }

    // The Toon shader posterizes shading: a lit sphere shows far fewer distinct
    // shades than the smooth Standard shader.
    {
        auto countShades = [](MeshRenderer::Shader shader) {
            Scene scene("T"); scene.physicsEnabled = false;
            GameObject* lgo = scene.CreateGameObject("Sun");
            auto* light = lgo->AddComponent<Light>();
            light->type = Light::Type::Directional; light->intensity = 1.0f;
            lgo->transform->localRotation = Quat::Euler(45, -30, 0);

            GameObject* go = scene.CreateGameObject("Ball");
            auto* mr = go->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Sphere();
            mr->color = Color::FromBytes(200, 200, 200);
            mr->shader = shader; mr->toonBands = 2;

            Raster r; r.Resize(64, 64);
            r.Clear(0xFF000000u);
            Mat4 view = Mat4::LookAt({0, 0, 4}, {0, 0, 0}, Vec3::Up);
            Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f);
            RenderMeshes(r, scene, proj * view, {0, 0, 4});

            std::set<std::uint32_t> shades;
            for (int y = 8; y < 56; ++y)
                for (int x = 8; x < 56; ++x) {
                    std::uint32_t c = r.Get(x, y);
                    if (c != 0xFF000000u) shades.insert(c);   // skip background
                }
            return shades.size();
        };

        std::size_t standardShades = countShades(MeshRenderer::Shader::Standard);
        std::size_t toonShades     = countShades(MeshRenderer::Shader::Toon);
        CHECK(standardShades > 0);
        CHECK(toonShades > 0);
        CHECK(toonShades < standardShades);   // banding collapses the gradient
    }

    // Material round-trips the rim + outline fields.
    {
        Material m;
        m.rimStrength = 0.8f; m.rimPower = 4.0f; m.rimColor = Color::FromBytes(0, 128, 255);
        m.outline = true; m.outlineWidth = 0.05f; m.outlineColor = Color::FromBytes(10, 20, 30);
        Material r = Material::FromText(m.ToText());
        CHECK_NEAR(r.rimStrength, 0.8f, 1e-4f);
        CHECK_NEAR(r.rimPower, 4.0f, 1e-4f);
        CHECK(r.outline);
        CHECK_NEAR(r.outlineWidth, 0.05f, 1e-4f);
    }

    // Scene serialization round-trips rim + outline on a MeshRenderer.
    {
        Scene s("RO"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Cube");
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->rimStrength = 0.5f; mr->rimPower = 2.0f;
        mr->outline = true; mr->outlineWidth = 0.07f;

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* mr2 = s2.Find("Cube") ? s2.Find("Cube")->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr2 != nullptr);
        if (mr2) {
            CHECK_NEAR(mr2->rimStrength, 0.5f, 1e-4f);
            CHECK(mr2->outline);
            CHECK_NEAR(mr2->outlineWidth, 0.07f, 1e-4f);
        }
    }

    // The Outline shader draws a silhouette: an outlined dark sphere paints more
    // non-background pixels than the same sphere without an outline.
    {
        auto countCovered = [](bool outline) {
            Scene scene("O"); scene.physicsEnabled = false;
            GameObject* go = scene.CreateGameObject("Ball");
            auto* mr = go->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Sphere();
            mr->unlit = true;                              // flat, deterministic fill
            mr->color = Color::FromBytes(200, 200, 200);
            // Red outline (distinct from the black background) so the ring is countable.
            mr->outline = outline; mr->outlineColor = Color::FromBytes(255, 0, 0);
            mr->outlineWidth = 0.15f;

            Raster r; r.Resize(64, 64);
            r.Clear(0xFF000000u);
            Mat4 view = Mat4::LookAt({0, 0, 4}, {0, 0, 0}, Vec3::Up);
            Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f);
            RenderMeshes(r, scene, proj * view, {0, 0, 4});

            int covered = 0;
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    if (r.Get(x, y) != 0xFF000000u) ++covered;
            return covered;
        };
        int plain   = countCovered(false);
        int outlined = countCovered(true);
        CHECK(plain > 0);
        CHECK(outlined > plain);   // the hull shell adds a silhouette ring of pixels
    }

    TEST_MAIN_RESULT();
}
