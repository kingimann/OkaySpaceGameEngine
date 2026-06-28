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

    // Ground contact shadow: the planar projection flattens any point onto the
    // ground plane along the light, and the MeshRenderer fields round-trip.
    {
        // Light pointing straight down: a point at (3, 5, -2) projects to y = groundY,
        // keeping x/z (vertical light = no horizontal shear).
        Mat4 s = Mat4::PlanarShadow(0.0f, Vec3{0, -1, 0});
        Vec3 p = s.MultiplyPoint(Vec3{3.0f, 5.0f, -2.0f});
        CHECK_NEAR(p.y, 0.0f, 1e-3f);
        CHECK_NEAR(p.x, 3.0f, 1e-3f);
        CHECK_NEAR(p.z, -2.0f, 1e-3f);
        // A slanted light shears the shadow sideways but still lands on the plane.
        Mat4 s2 = Mat4::PlanarShadow(1.0f, Vec3{1, -1, 0});
        Vec3 q = s2.MultiplyPoint(Vec3{0.0f, 5.0f, 0.0f});   // 4 units above the plane
        CHECK_NEAR(q.y, 1.0f, 1e-3f);
        CHECK_NEAR(q.x, 4.0f, 1e-2f);                        // sheared east by the height drop

        Scene sc("GS"); sc.physicsEnabled = false;
        auto* mr = sc.CreateGameObject("Box")->AddComponent<MeshRenderer>();
        CHECK(mr->groundShadow);                              // on by default
        mr->groundShadow = false; mr->groundShadowY = 2.5f; mr->groundShadowStrength = 0.7f;
        Scene s3("x"); SceneSerializer::Deserialize(s3, SceneSerializer::Serialize(sc));
        auto* mr2 = s3.Find("Box") ? s3.Find("Box")->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr2 != nullptr);
        if (mr2) {
            CHECK(!mr2->groundShadow);
            CHECK_NEAR(mr2->groundShadowY, 2.5f, 1e-3f);
            CHECK_NEAR(mr2->groundShadowStrength, 0.7f, 1e-3f);
        }
    }

    // Gradient shader round-trips its top/bottom colours through the scene.
    {
        Scene s("G"); s.physicsEnabled = false;
        auto* mr = s.CreateGameObject("Cube")->AddComponent<MeshRenderer>();
        mr->shader = MeshRenderer::Shader::Gradient;
        mr->gradientTop = Color::FromBytes(255, 0, 0);
        mr->gradientBottom = Color::FromBytes(0, 0, 255);

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* mr2 = s2.Find("Cube") ? s2.Find("Cube")->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr2 != nullptr);
        if (mr2) {
            CHECK(mr2->shader == MeshRenderer::Shader::Gradient);
            CHECK_NEAR(mr2->gradientTop.r, 1.0f, 0.02f);
            CHECK_NEAR(mr2->gradientBottom.b, 1.0f, 0.02f);
        }
    }

    // Gradient + Fresnel actually change the rendered image vs Standard.
    {
        auto renderSphere = [](MeshRenderer::Shader shader) {
            Scene scene("SH"); scene.physicsEnabled = false;
            GameObject* lgo = scene.CreateGameObject("Sun");
            auto* light = lgo->AddComponent<Light>();
            light->type = Light::Type::Directional; light->intensity = 1.0f;
            lgo->transform->localRotation = Quat::Euler(45, -30, 0);
            GameObject* go = scene.CreateGameObject("Ball");
            auto* mr = go->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Sphere();
            mr->color = Color::FromBytes(180, 180, 180);
            mr->shader = shader;
            mr->gradientTop = Color::FromBytes(255, 0, 0);
            mr->gradientBottom = Color::FromBytes(0, 0, 255);
            Raster r; r.Resize(64, 64); r.Clear(0xFF000000u);
            Mat4 view = Mat4::LookAt({0, 0, 4}, {0, 0, 0}, Vec3::Up);
            Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f);
            RenderMeshes(r, scene, proj * view, {0, 0, 4});
            return r;
        };
        Raster std0 = renderSphere(MeshRenderer::Shader::Standard);
        Raster grad = renderSphere(MeshRenderer::Shader::Gradient);
        Raster fres = renderSphere(MeshRenderer::Shader::Fresnel);
        int dGrad = 0, dFres = 0;
        for (int i = 0; i < 64 * 64; ++i) {
            if (std0.Get(i % 64, i / 64) != grad.Get(i % 64, i / 64)) ++dGrad;
            if (std0.Get(i % 64, i / 64) != fres.Get(i % 64, i / 64)) ++dFres;
        }
        CHECK(dGrad > 0);   // gradient ramp repaints the sphere
        CHECK(dFres > 0);   // fresnel rim/darkening repaints the sphere
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

    // Material + scene round-trip the scrolling-UV speed and triplanar flag.
    {
        Material m; m.uvScroll = {0.25f, -0.5f}; m.triplanar = true;
        Material r = Material::FromText(m.ToText());
        CHECK_NEAR(r.uvScroll.x, 0.25f, 1e-4f);
        CHECK_NEAR(r.uvScroll.y, -0.5f, 1e-4f);
        CHECK(r.triplanar);

        Scene s("UV"); s.physicsEnabled = false;
        auto* mr = s.CreateGameObject("Q")->AddComponent<MeshRenderer>();
        mr->uvScroll = {1.0f, 2.0f}; mr->triplanar = true;
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* mr2 = s2.Find("Q") ? s2.Find("Q")->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr2 != nullptr);
        if (mr2) { CHECK_NEAR(mr2->uvScroll.y, 2.0f, 1e-4f); CHECK(mr2->triplanar); }
    }

    // A high-contrast split texture used by the render tests below.
    {
        Image img(16, 16);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                img.SetPixel(x, y, x < 8 ? Color::White : Color::FromBytes(20, 20, 20));
        RegisterTexture("@uvtest", img);
    }

    auto renderQuad = [](float scrollX, bool triplanar) {
        Scene scene("UVR"); scene.physicsEnabled = false;
        auto* mr = scene.CreateGameObject("Q")->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::FromName("Quad");
        mr->unlit = !triplanar;          // triplanar needs the lit per-pixel path
        mr->texture = "@uvtest";
        mr->uvScroll = {scrollX, 0.0f};
        mr->triplanar = triplanar;
        Raster r; r.Resize(48, 48); r.Clear(0xFF000000u);
        Mat4 view = Mat4::LookAt({0, 0, 3}, {0, 0, 0}, Vec3::Up);
        Mat4 proj = Mat4::Perspective(50.0f, 1.0f, 0.1f, 100.0f);
        RenderMeshes(r, scene, proj * view, {0, 0, 3});
        return r;
    };

    // Scrolling UV: advancing time shifts the texture, so the image changes.
    {
        Time::Step(-Time::ElapsedTime());      // reset elapsed time to 0 (timeScale = 1)
        Raster a = renderQuad(0.5f, false);
        Time::Step(1.0f);                      // +1s -> +0.5 UV shift (half the texture)
        Raster b = renderQuad(0.5f, false);
        int diff = 0;
        for (int i = 0; i < 48 * 48; ++i)
            if (a.Get(i % 48, i / 48) != b.Get(i % 48, i / 48)) ++diff;
        CHECK(diff > 0);                       // the texture scrolled
    }

    // Triplanar maps by world position, giving a different pattern than plain UVs.
    {
        Time::Step(-Time::ElapsedTime());
        Raster uv  = renderQuad(0.0f, false);
        Raster tri = renderQuad(0.0f, true);
        int covered = 0, diff = 0;
        for (int i = 0; i < 48 * 48; ++i) {
            std::uint32_t cu = uv.Get(i % 48, i / 48), ct = tri.Get(i % 48, i / 48);
            if (cu != 0xFF000000u || ct != 0xFF000000u) ++covered;
            if (cu != ct) ++diff;
        }
        CHECK(covered > 0);
        CHECK(diff > 0);                       // triplanar differs from UV mapping
    }

    TEST_MAIN_RESULT();
}
