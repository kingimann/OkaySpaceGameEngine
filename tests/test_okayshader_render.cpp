// The Custom shader mode end to end on the software renderer: MeshRenderer / .okaymat / scene fields,
// a custom surface drawn per pixel, the magenta error material, discard, hot reload, and the shipped
// example scene. (The OpenGL comparison is tests/test_okayshader_gl.cpp.)
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/OkayShader.hpp"
#include <cstdio>
#include <cstdlib>
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


// Pixel of a world point under `vp` in a size x size picture.
void Project(const Mat4& vp, const Vec3& p, int size, int& x, int& y) {
    const Vec4 c = vp * Vec4{p, 1.0f};
    x = (int)((c.x / c.w * 0.5f + 0.5f) * size);
    y = (int)((1.0f - (c.y / c.w * 0.5f + 0.5f)) * size);
}
int Lum(const Raster& r, int x, int y) { const Rgb c = At(r, x, y); return (c.r + c.g + c.b) / 3; }

// A grey ground, a sun at 45 degrees and a cube caster 1 unit up, seen from above; the caster's
// renderer gets `setup`. Returns the ground brightness at the left and right halves of the
// shadow and on open ground.
struct ShadowProbe { int left, right, open, movedRight; };
template <class F> ShadowProbe RenderShadow(F setup, Raster* out = nullptr) {
    const int S = 160;
    Scene scene("SHD"); scene.physicsEnabled = false;
    scene.renderSettings.tonemap = false;
    scene.renderSettings.skybox = false;
    GameObject* lgo = scene.CreateGameObject("Sun");
    auto* light = lgo->AddComponent<Light>();
    light->type = Light::Type::Directional; light->intensity = 1.0f;
    lgo->transform->localRotation = Quat::Euler(45, 0, 0);
    GameObject* ground = scene.CreateGameObject("Ground");
    auto* gm = ground->AddComponent<MeshRenderer>();
    gm->mesh = Mesh::Plane(8.0f);
    gm->color = Color::FromBytes(180, 180, 180);
    gm->groundShadow = false;
    GameObject* go = scene.CreateGameObject("Caster");
    go->transform->localPosition = {0.0f, 1.0f, 0.0f};
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(0.8f);
    mr->color = Color::FromBytes(90, 90, 200);
    mr->groundShadow = false;
    setup(*mr);
    const Vec3 eye{0.0f, 6.0f, 4.0f};
    const Mat4 vp = Mat4::Perspective(50.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(eye, {0.0f, 0.0f, 0.6f}, Vec3::Up);
    Raster r; r.Resize(S, S); r.Clear(0xFF000000u);
    ApplySceneLight(scene);   // the scene's Light components become the frame's lights (the game loop does this)
    RenderMeshes(r, scene, vp, eye);
    // Where the caster's centre falls on the ground along the light.
    const Vec3 f = lgo->transform->Forward();
    const float t = 1.0f / -f.y;
    const Vec3 c{f.x * t, 0.0f, f.z * t};
    ShadowProbe p{};
    int x, y;
    Project(vp, {c.x - 0.22f, 0.0f, c.z}, S, x, y); p.left = Lum(r, x, y);
    Project(vp, {c.x + 0.22f, 0.0f, c.z}, S, x, y); p.right = Lum(r, x, y);
    Project(vp, {c.x + 1.2f, 0.0f, c.z}, S, x, y); p.movedRight = Lum(r, x, y);
    Project(vp, {c.x - 2.5f, 0.0f, c.z}, S, x, y); p.open = Lum(r, x, y);
    if (const char* shot = std::getenv("OKAY_SHOT_DIR")) {   // a look at the scene (debugging)
        static int n = 0;
        Image img(S, S);
        for (int yy = 0; yy < S; ++yy) for (int xx = 0; xx < S; ++xx) { const Rgb q = At(r, xx, yy); img.SetPixel(xx, yy, Color::FromBytes(q.r, q.g, q.b)); }
        img.SavePNG(std::string(shot) + "/render_shadow_" + std::to_string(n++) + ".png");
        std::cout << "  light forward " << f.x << " " << f.y << " " << f.z << ", SceneLight " << SceneLight::Direction().x << " " << SceneLight::Direction().y << " " << SceneLight::Direction().z << "\n";
    }
    if (out) *out = r;
    return p;
}

// Rows of the topmost and bottommost lit pixel and the number of lit pixels.
struct Extent { int top = 9999, bottom = -1, count = 0, left = 9999, right = -1; };
Extent LitExtent(const Raster& r) {
    Extent e;
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x)
            if (Lum(r, x, y) > 10) { e.top = std::min(e.top, y); e.bottom = std::max(e.bottom, y); e.left = std::min(e.left, x); e.right = std::max(e.right, x); ++e.count; }
    return e;
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


    // ---- a dissolved object casts a dissolved shadow ------------------------------------------------
    {
        WriteFile(dir + "/halfcut.okayshader", "surface { if (position.x < 0.0) { discard; } }");
        const ShadowProbe std0 = RenderShadow([](MeshRenderer&) {});
        const ShadowProbe cut = RenderShadow([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/halfcut.okayshader"; });
        std::cout << "  shadow (left/right/open): Standard " << std0.left << "/" << std0.right << "/" << std0.open
                  << ", half-discarded " << cut.left << "/" << cut.right << "/" << cut.open << "\n";
        CHECK(std0.open > 60);
        CHECK(std0.left < std0.open - 25 && std0.right < std0.open - 25);   // the whole cube shadows
        CHECK(cut.right < cut.open - 25);                                  // the half that is left still does
        CHECK(cut.left > cut.open - 8);                                    // the cut half casts nothing
        // An alpha below one half cuts the shadow too (alpha cut-out).
        WriteFile(dir + "/halfalpha.okayshader", "surface { alpha = position.x < 0.0 ? 0.2 : 1.0; }");
        const ShadowProbe al = RenderShadow([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/halfalpha.okayshader"; });
        std::cout << "  shadow, alpha 0.2 on the left half: " << al.left << "/" << al.right << "/" << al.open << "\n";
        CHECK(al.right < al.open - 25);
        CHECK(al.left > al.open - 8);
    }

    // ---- the vertex block moves the surface (software renderer) --------------------------------------
    {
        WriteFile(dir + "/white.okayshader", "mode unlit; surface { albedo = vec3(1.0); }");
        WriteFile(dir + "/up.okayshader", "mode unlit; vertex { position.y += 0.15; } surface { albedo = vec3(1.0); }");
        WriteFile(dir + "/grow.okayshader", "mode unlit; vertex { position += normal * 0.1; } surface { albedo = vec3(1.0); }");
        WriteFile(dir + "/sway.okayshader", "mode unlit; vertex { position.x += sin(time) * 0.2; } surface { albedo = vec3(1.0); }");
        auto custom = [&](const std::string& f) { return [&, f](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/" + f; }; };
        Time::ResetForTesting();
        const Extent base = LitExtent(RenderBall(custom("white.okayshader")));
        const Extent up = LitExtent(RenderBall(custom("up.okayshader")));
        const Extent grow = LitExtent(RenderBall(custom("grow.okayshader")));
        const Extent sway0 = LitExtent(RenderBall(custom("sway.okayshader")));
        for (int k = 0; k < 32; ++k) Time::Step(0.05f);   // 1.6 s: sin is near its peak
        const Extent sway1 = LitExtent(RenderBall(custom("sway.okayshader")));
        std::cout << "  vertex block: base top " << base.top << " count " << base.count << ", moved up top " << up.top
                  << ", grown count " << grow.count << ", sway left " << sway0.left << " -> " << sway1.left << "\n";
        CHECK(base.count > 500);
        CHECK(up.top < base.top - 4 && up.bottom < base.bottom - 4);
        CHECK(grow.count > base.count + 200);
        CHECK(sway1.left > sway0.left + 4);
        Time::ResetForTesting();
    }

    // ---- the vertex block moves the shadow as well --------------------------------------------------
    {
        WriteFile(dir + "/shift.okayshader", "vertex { position.x += 1.2; }");
        const ShadowProbe moved = RenderShadow([&](MeshRenderer& mr) { mr.shader = MeshRenderer::Shader::Custom; mr.customShader = dir + "/shift.okayshader"; });
        std::cout << "  shadow of a cube moved 1.2 to the right by its vertex block: at the old place " << moved.left << "/"
                  << moved.right << ", at the new place " << moved.movedRight << ", open " << moved.open << "\n";
        CHECK(moved.left > moved.open - 8);
        CHECK(moved.movedRight < moved.open - 25);
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
        CHECK(custom >= 5);
        CHECK(compiled == custom);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
