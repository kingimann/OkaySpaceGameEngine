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

    TEST_MAIN_RESULT();
}
