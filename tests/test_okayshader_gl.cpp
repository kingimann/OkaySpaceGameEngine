// The same .okayshader surface drawn by the software renderer and by the OpenGL renderer (Mesa
// llvmpipe under Xvfb, through tests/run_under_xvfb.sh) must agree within a tolerance. This is also
// the check that the generated GLSL compiles on a real GL driver: a shader that did not would draw
// the magenta error material. Exit 77 (skipped) without a display or a GL context.
// OKAY_SHOT_DIR=<folder>: also write software | OpenGL side-by-side PNGs of every case there.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/GLRenderer.hpp"
#include "okay/Render/OkayShader.hpp"
#include "okay/Platform/Platform.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

using namespace okay;
using namespace okay::platform;

namespace {

constexpr int kSize = 128;
platform::Backend* g_backend = nullptr;
void* GetProc(const char* name) { return g_backend ? g_backend->GLProcAddress(name) : nullptr; }

struct Case { const char* name; std::string shader; std::string params; bool lit; double minAgree; double maxMean; float time; };

void BuildScene(Scene& scene, const std::string& shaderPath, const std::string& params) {
    scene.physicsEnabled = false;
    auto& rs = scene.renderSettings;
    rs.skybox = false;
    rs.tonemap = false;
    GameObject* lgo = scene.CreateGameObject("Sun");
    auto* light = lgo->AddComponent<Light>();
    light->type = Light::Type::Directional; light->intensity = 1.0f;
    lgo->transform->localRotation = Quat::Euler(40, -35, 0);
    GameObject* go = scene.CreateGameObject("Ball");
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Sphere();
    mr->color = Color::FromBytes(200, 200, 200);
    mr->groundShadow = false;
    mr->castShadows = false; mr->receiveShadows = false;
    mr->shader = MeshRenderer::Shader::Custom;
    mr->customShader = shaderPath;
    mr->shaderParams = params;
}

void SaveSideBySide(const std::string& file, const Raster& a, const std::uint32_t* b) {
    Image img(kSize * 2, kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const std::uint32_t ca = a.Get(x, y), cb = b[(std::size_t)y * kSize + x];
            img.SetPixel(x, y, Color::FromBytes(ca & 0xFF, (ca >> 8) & 0xFF, (ca >> 16) & 0xFF));
            img.SetPixel(kSize + x, y, Color::FromBytes(cb & 0xFF, (cb >> 8) & 0xFF, (cb >> 16) & 0xFF));
        }
    img.SavePNG(file);
}

} // namespace

int main() {
    RUN_SUITE("okayshader_gl");
    if (!std::getenv("DISPLAY")) { std::cout << "SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"; return 77; }
    if (!IsCompiledIn(BackendKind::Own)) { std::cout << "SKIP: own platform backend not compiled in\n"; return 77; }
    g_backend = Get(BackendKind::Own);
    if (!g_backend || !g_backend->Init(InitFlags{true, false, false})) { std::cout << "SKIP: cannot open the display\n"; return 77; }
    WindowDesc d;
    d.title = "okayshader gl"; d.width = kSize; d.height = kSize; d.opengl = true;
    auto win = g_backend->MakeWindow(d);
    if (!win || !win->CreateGLContext(GLAttributes{}) || !win->MakeCurrent()) { std::cout << "SKIP: no GL context\n"; return 77; }
    if (!GLRenderer::LoadGL(&GetProc)) { std::cout << "SKIP: GL entry points missing\n"; return 77; }
    const char* shotDir = std::getenv("OKAY_SHOT_DIR");

    const std::string src = std::string(OKAY_SOURCE_DIR) + "/examples/shaders/";
    const std::string tmp = "okayshader_gl_tmp.okayshader";
    std::vector<Case> cases = {
        {"unlit_uv", "mode unlit; surface { albedo = vec3(uv, 0.5) * vcolor; }", "", false, 0.97, 3.0, 0.0f},
        {"unlit_noise", "mode unlit; surface { float n = noise(position * 3.0); albedo = vec3(n, hash(uv * 16.0), fract(n * 4.0)); }", "", false, 0.95, 4.0, 0.0f},
        {"unlit_maths", "mode unlit; surface { vec3 c = vec3(0.0); for i in 0..3 { c += vec3(sin(position.x * 4.0 + i), cos(position.y * 3.0 - i), mod(position.z * 2.0 + i, 1.0)) * 0.2; }"
                        " albedo = clamp(c + 0.4, 0.0, 1.0); albedo.z = smoothstep(0.2, 0.8, saturate(dot(normal, view))); }", "", false, 0.97, 3.0, 0.0f},
        {"lit_passthrough", "surface { albedo = albedo; }", "", true, 0.90, 6.0, 0.0f},
        {"lava", "@lava.okayshader", "", true, 0.85, 8.0, 1.25f},
        {"dissolve", "@dissolve.okayshader", "cycle=0;amount=0.45", true, 0.85, 8.0, 0.0f},
        {"rim_glow", "@rim_glow.okayshader", "", true, 0.85, 8.0, 0.5f},
    };
    GLRenderer gl;
    for (const Case& c : cases) {
        std::string path = tmp;
        if (c.shader[0] == '@') path = src + c.shader.substr(1);
        else { std::ofstream f(tmp); f << c.shader; }
        oshader::LoadShaderFile(path, 0);
        CHECK(oshader::LoadShaderFile(path, 0).compiled->ok);
        Time::ResetForTesting();   // both renderers read the same clock
        for (int k = 0; k < (int)(c.time / 0.05f); ++k) Time::Step(0.05f);
        Scene scene("gl");
        BuildScene(scene, path, c.params);
        const Mat4 view = Mat4::LookAt({0, 0.3f, 3.2f}, {0, 0, 0}, Vec3::Up);
        const Mat4 proj = Mat4::Perspective(55.0f, 1.0f, 0.1f, 100.0f);
        const Mat4 vp = proj * view;
        const Vec3 eye{0, 0.3f, 3.2f};
        Raster sw; sw.Resize(kSize, kSize); sw.Clear(0xFF000000u);
        RenderMeshes(sw, scene, vp, eye);
        const std::uint32_t* px = gl.RenderToPixels(scene, vp, eye, kSize, kSize, 1, 0.0f, 0.0f, 0.0f, 1.0f);
        CHECK(px != nullptr);
        if (!px) continue;
        // Compare where both drew the ball (silhouettes differ by a pixel between rasterizers).
        int both = 0, agree = 0, magenta = 0;
        double sum = 0.0;
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x) {
                const std::uint32_t a = sw.Get(x, y), b = px[(std::size_t)y * kSize + x];
                const int ar = a & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF;
                const int br = b & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF;
                if (br == 255 && bg == 0 && bb == 255) ++magenta;
                if (ar + ag + ab == 0 || br + bg + bb == 0) continue;
                ++both;
                const int dm = std::max({std::abs(ar - br), std::abs(ag - bg), std::abs(ab - bb)});
                sum += (std::abs(ar - br) + std::abs(ag - bg) + std::abs(ab - bb)) / 3.0;
                if (dm <= 12) ++agree;
            }
        const double agreeF = both ? (double)agree / both : 0.0, mean = both ? sum / both : 999.0;
        std::cout << "  " << c.name << ": " << both << " pixels compared, " << (int)(agreeF * 1000) / 10.0
                  << "% within 12/255, mean difference " << mean << "/255" << (magenta ? " (MAGENTA: GLSL did not compile)" : "") << "\n";
        CHECK(magenta == 0);
        CHECK(both > kSize * kSize / 5);
        CHECK(agreeF >= c.minAgree);
        CHECK(mean <= c.maxMean);
        if (shotDir) SaveSideBySide(std::string(shotDir) + "/" + c.name + ".png", sw, px);
    }
    std::remove(tmp.c_str());
    gl.Destroy();
    TEST_MAIN_RESULT();
}
