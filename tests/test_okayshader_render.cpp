// The Custom shader mode end to end on the software renderer: MeshRenderer / .okaymat / scene fields,
// a custom surface drawn per pixel, the magenta error material, discard, hot reload, and the shipped
// example scene. (The OpenGL comparison is tests/test_okayshader_gl.cpp.)
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/OkayShader.hpp"
#include <cstdio>
#include <fstream>
#include <string>

using namespace okay;

namespace {

void WriteFile(const std::string& path, const std::string& text) { std::ofstream f(path); f << text; }

struct Rgb { int r, g, b; };
Rgb At(const Raster& r, int x, int y) {
    const std::uint32_t c = r.Get(x, y);
    return {(int)(c & 0xFF), (int)((c >> 8) & 0xFF), (int)((c >> 16) & 0xFF)};
}

// A lit sphere in front of the camera, drawn with `setup` applied to its renderer.
template <class F> Raster RenderBall(F setup, int size = 64) {
    Scene scene("SH"); scene.physicsEnabled = false;
    scene.renderSettings.tonemap = false;   // exact colours: no filmic curve on top
    GameObject* lgo = scene.CreateGameObject("Sun");
    auto* light = lgo->AddComponent<Light>();
    light->type = Light::Type::Directional; light->intensity = 1.0f;
    lgo->transform->localRotation = Quat::Euler(45, -30, 0);
    GameObject* go = scene.CreateGameObject("Ball");
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Sphere();
    mr->color = Color::FromBytes(200, 60, 40);
    mr->specular = 0.3f; mr->shininess = 24.0f;
    setup(*mr);
    Raster r; r.Resize(size, size); r.Clear(0xFF000000u);
    Mat4 view = Mat4::LookAt({0, 0, 1.2f}, {0, 0, 0}, Vec3::Up);   // the ball fills most of the picture
    Mat4 proj = Mat4::Perspective(60.0f, 1.0f, 0.05f, 100.0f);
    RenderMeshes(r, scene, proj * view, {0, 0, 1.2f});
    return r;
}

} // namespace

int main() {
    RUN_SUITE("okayshader_render");
    oshader::ClearShaderFileCache();

    // ---- fields: MeshRenderer, scene file, .okaymat ------------------------------------------
    {
        Scene s("S"); s.physicsEnabled = false;
        auto* mr = s.CreateGameObject("Cube")->AddComponent<MeshRenderer>();
        mr->shader = MeshRenderer::Shader::Custom;
        mr->customShader = "my shaders/lava.okayshader";
        mr->shaderParams = "speed=0.5;hot=1,0.5,0.25;tex:detail=a b.png";
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* mr2 = s2.Find("Cube") ? s2.Find("Cube")->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr2 != nullptr);
        if (mr2) {
            CHECK(mr2->shader == MeshRenderer::Shader::Custom);
            CHECK(mr2->customShader == "my shaders/lava.okayshader");
            CHECK(mr2->shaderParams == "speed=0.5;hot=1,0.5,0.25;tex:detail=a b.png");
        }
        Material m = Material::FromRenderer(*mr);
        Material back = Material::FromText(m.ToText());
        CHECK(back.shader == MeshRenderer::Shader::Custom);
        CHECK(back.customShader == mr->customShader);
        CHECK(back.shaderParams == mr->shaderParams);
        MeshRenderer target;
        back.ApplyTo(target);
        CHECK(target.shader == MeshRenderer::Shader::Custom && target.customShader == mr->customShader);
        // An older .okaymat (no custom lines) still loads as before.
        Material old = Material::FromText("okaymat 1\nshader 2 4\n");
        CHECK(old.shader == MeshRenderer::Shader::Toon && old.customShader.empty());
    }

    const std::string dir = "okayshader_render_tmp";
    std::remove((dir + "/a.okayshader").c_str());
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }

    // ---- a pass-through custom surface lights exactly like Standard -------------------------
    {
        WriteFile(dir + "/pass.okayshader", "surface { albedo = albedo; }");
        Raster stdR = RenderBall([](MeshRenderer&) {});
        Raster cusR = RenderBall([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/pass.okayshader"; });
        int maxD = 0, lit = 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            Rgb a = At(stdR, x, y), b = At(cusR, x, y);
            maxD = std::max({maxD, std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
            if (a.r > 20) ++lit;
        }
        std::cout << "  pass-through vs Standard: max channel difference " << maxD << " over " << lit << " lit pixels\n";
        CHECK(lit > 500);
        CHECK(maxD <= 1);
    }

    // ---- an unlit surface draws exactly what it computes ----------------------------------------
    {
        WriteFile(dir + "/flat.okayshader", "mode unlit;\nproperty color tint = (0.2, 0.4, 0.8);\nsurface { albedo = tint; }");
        Raster r = RenderBall([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/flat.okayshader"; });
        Rgb c = At(r, 32, 32);
        CHECK(std::abs(c.r - 51) <= 1 && std::abs(c.g - 102) <= 1 && std::abs(c.b - 204) <= 1);
        // The material's value replaces the default.
        Raster r2 = RenderBall([&](MeshRenderer& mr) {
            mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/flat.okayshader"; mr.shaderParams = "tint=1,0,0";
        });
        Rgb c2 = At(r2, 32, 32);
        CHECK(c2.r >= 254 && c2.g <= 1 && c2.b <= 1);
        // An edge pixel is the same flat colour (no lighting).
        Rgb e = At(r, 20, 32);
        CHECK(std::abs(e.r - 51) <= 1 && std::abs(e.b - 204) <= 1);
    }

    // ---- uv, position and view reach the surface ------------------------------------------------
    {
        WriteFile(dir + "/uv.okayshader", "mode unlit; surface { albedo = vec3(uv.x, position.y * 0.5 + 0.5, view.z); }");
        Raster r = RenderBall([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/uv.okayshader"; });
        Rgb top = At(r, 32, 14), bottom = At(r, 32, 50), mid = At(r, 32, 32);
        CHECK(top.g > bottom.g + 60);          // higher on the ball = larger position.y
        CHECK(mid.b > 230);                    // facing the camera: view.z near 1
        CHECK(top.r != bottom.r || top.r != mid.r || At(r, 20, 32).r != At(r, 44, 32).r);   // uv varies
    }

    // ---- a broken shader draws the magenta error material ---------------------------------------
    {
        WriteFile(dir + "/broken.okayshader", "surface { albedo = nope; }");
        Raster r = RenderBall([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/broken.okayshader"; });
        Rgb c = At(r, 32, 32);
        CHECK(c.r == 255 && c.g == 0 && c.b == 255);
        Raster m = RenderBall([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/missing.okayshader"; });
        Rgb cm = At(m, 32, 32);
        CHECK(cm.r == 255 && cm.g == 0 && cm.b == 255);
        Raster e = RenderBall([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader.clear(); });
        CHECK(At(e, 32, 32).r == 255 && At(e, 32, 32).g == 0);
    }

    // ---- discard cuts holes -----------------------------------------------------------------------
    {
        WriteFile(dir + "/cut.okayshader", "mode unlit; surface { if (position.x < 0.0) { discard; } albedo = vec3(1.0); }");
        Raster r = RenderBall([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/cut.okayshader"; });
        // Left half: the front is cut away, so the inside of the back (culled) shows the background.
        Rgb left = At(r, 22, 32), right = At(r, 42, 32);
        CHECK(left.r == 0 && left.g == 0 && left.b == 0);
        CHECK(right.r == 255);
    }

    // ---- hot reload: an edited file draws the new surface ------------------------------------------
    {
        const std::string p = dir + "/hot.okayshader";
        WriteFile(p, "mode unlit; surface { albedo = vec3(1.0, 0.0, 0.0); }");
        oshader::LoadShaderFile(p, 0);
        Raster a = RenderBall([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = p; });
        WriteFile(p, "mode unlit; surface { albedo = vec3(0.0, 1.0, 0.0); }  // edited");
        oshader::LoadShaderFile(p, 0);   // the editor's 250 ms check, without the wait
        Raster b = RenderBall([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = p; });
        CHECK(At(a, 32, 32).r == 255 && At(a, 32, 32).g == 0);
        CHECK(At(b, 32, 32).g == 255 && At(b, 32, 32).r == 0);
    }

    // ---- the shipped example scene loads and every custom object compiles and draws ------------------
    {
        const std::string scenePath = std::string(OKAY_SOURCE_DIR) + "/examples/shaders/shader_demo.okayscene";
        Scene s("demo"); s.physicsEnabled = false;
        CHECK(SceneSerializer::LoadFromFile(s, scenePath));
        int custom = 0, compiled = 0;
        for (const auto& go : s.Objects())
            if (auto* mr = go->GetComponent<MeshRenderer>(); mr && mr->shader == MeshRenderer::Shader::Custom) {
                ++custom;
                const oshader::ShaderFile sf = oshader::LoadShaderFile(mr->customShader, 0);
                if (sf.compiled && sf.compiled->ok) ++compiled;
                else std::cerr << "    " << mr->customShader << ": " << (sf.compiled ? sf.compiled->FirstError() : "?") << "\n";
            }
        CHECK(custom >= 3);
        CHECK(compiled == custom);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
