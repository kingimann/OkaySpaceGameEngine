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


// The shadow scene: a grey ground, a sun at 45 degrees and a cube caster 1 unit up drawn with
// `shader`. Seen from above. Probe points: the left and right halves of the shadow, open ground.
struct ShadowRun { int left = 0, right = 0, open = 0; };
const Vec3 kShadowEye{0.0f, 6.0f, 4.0f};
Mat4 ShadowVP() { return Mat4::Perspective(50.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(kShadowEye, {0.0f, 0.0f, 0.6f}, Vec3::Up); }
void BuildShadowScene(Scene& scene, const std::string& shader) {
    scene.physicsEnabled = false;
    scene.renderSettings.tonemap = false;
    scene.renderSettings.skybox = false;
    GameObject* lgo = scene.CreateGameObject("Sun");
    auto* light = lgo->AddComponent<Light>();
    light->type = Light::Type::Directional; light->intensity = 1.0f;
    lgo->transform->localRotation = Quat::Euler(45, 0, 0);
    auto* gm = scene.CreateGameObject("Ground")->AddComponent<MeshRenderer>();
    gm->mesh = Mesh::Plane(8.0f);
    gm->color = Color::FromBytes(180, 180, 180);
    gm->groundShadow = false;
    GameObject* go = scene.CreateGameObject("Caster");
    go->transform->localPosition = {0.0f, 1.0f, 0.0f};
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(0.8f);
    mr->color = Color::FromBytes(90, 90, 200);
    mr->groundShadow = false;
    if (!shader.empty()) { mr->shader = MeshRenderer::Shader::Custom; mr->customShader = shader; }
}
int LumAt(const std::uint32_t* px, int w, int x, int y) {
    const std::uint32_t c = px[(std::size_t)y * w + x];
    return (int)((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF)) / 3;
}
ShadowRun Probe(const Scene& scene, const std::uint32_t* px, int size) {
    Vec3 f{0, -1, 0};
    for (const auto& go : scene.Objects()) if (go->GetComponent<Light>()) f = go->transform->Forward();
    const float t = 1.0f / -f.y;
    const Vec3 c{f.x * t, 0.0f, f.z * t};
    const Mat4 vp = ShadowVP();
    auto at = [&](float dx) {
        const Vec4 q = vp * Vec4{Vec3{c.x + dx, 0.0f, c.z}, 1.0f};
        const int x = (int)((q.x / q.w * 0.5f + 0.5f) * size), y = (int)((1.0f - (q.y / q.w * 0.5f + 0.5f)) * size);
        return LumAt(px, size, x, y);
    };
    return ShadowRun{at(-0.22f), at(0.22f), at(-2.5f)};
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
    // Lit cases: the two renderers' own Standard lighting already differs (OpenGL soft-clips and lights a
    // little brighter; about 10/255 on a plain grey ball), so their limits are wider than the unlit ones.
    std::vector<Case> cases = {
        {"unlit_uv", "mode unlit; surface { albedo = vec3(uv, 0.5) * vcolor; }", "", false, 0.95, 2.0, 0.0f},
        {"unlit_noise", "mode unlit; surface { float n = noise(position * 3.0); albedo = vec3(n, hash(uv * 16.0), fract(n * 4.0)); }", "", false, 0.80, 4.0, 0.0f},
        {"unlit_maths", "mode unlit; surface { vec3 c = vec3(0.0); for i in 0..3 { c += vec3(sin(position.x * 4.0 + i), cos(position.y * 3.0 - i), mod(position.z * 2.0 + i, 1.0)) * 0.2; }"
                        " albedo = clamp(c + 0.4, 0.0, 1.0); albedo.z = smoothstep(0.2, 0.8, saturate(dot(normal, view))); }", "", false, 0.95, 2.0, 0.0f},
        {"unlit_props", "mode unlit; property float amount = 0.5; property color tint = (0.1, 0.2, 0.3); surface { albedo = vec3(amount, tint.g, time * 0.25); }",
                        "amount=0.75;tint=0,0.5,0", false, 0.95, 2.0, 2.0f},
        {"unlit_discard", "mode unlit; property float amount = 0.45; surface { float n = noise(position * 6.0) * 0.7 + noise(position * 13.8) * 0.3;"
                          " if (n < amount) { discard; } albedo = vec3(n); }", "", false, 0.85, 8.0, 0.0f},
        {"lit_passthrough", "surface { albedo = albedo; }", "", true, 0.50, 14.0, 0.0f},
        {"lava", "@lava.okayshader", "", true, 0.90, 4.0, 1.25f},
        // Through its holes the far inside of the ball shows, lit from behind: the renderers light those
        // back faces differently (their Standard lighting differs most there), hence the wide limit.
        {"dissolve", "@dissolve.okayshader", "cycle=0;amount=0.45", true, 0.30, 24.0, 0.0f},
        {"rim_glow", "@rim_glow.okayshader", "", true, 0.85, 10.0, 0.5f},
        // Vertex blocks: the same moved vertices on both renderers (silhouette and colours).
        {"vertex_wobble_unlit", "mode unlit; vertex { position += normal * (0.08 * sin(time * 3.0 + local.y * 12.0)); }"
                                " surface { albedo = vec3(uv, 0.5); }", "", false, 0.85, 6.0, 0.7f},
        {"vertex_wobble_lit", "@wobble.okayshader", "", true, 0.45, 16.0, 0.7f},
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
        const Mat4 view = Mat4::LookAt({0, 0.15f, 1.3f}, {0, 0, 0}, Vec3::Up);
        const Mat4 proj = Mat4::Perspective(55.0f, 1.0f, 0.05f, 100.0f);
        const Mat4 vp = proj * view;
        const Vec3 eye{0, 0.15f, 1.3f};
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
        if (std::string(c.name) == "lit_passthrough") {
            // On one renderer a pass-through custom surface must give Standard's pixels exactly.
            std::vector<std::uint32_t> custom(px, px + (std::size_t)kSize * kSize);
            Scene std0("gl-std");
            BuildScene(std0, path, "");
            for (const auto& go : std0.Objects())
                if (auto* mr = go->GetComponent<MeshRenderer>()) mr->shader = MeshRenderer::Shader::Standard;
            // A custom surface always shades per pixel on the software renderer; Standard does when asked.
            Raster sws; sws.Resize(kSize, kSize); sws.Clear(0xFF000000u);
            const bool ppSave = PerPixelLighting();
            PerPixelLighting() = true;
            RenderMeshes(sws, std0, vp, eye);
            PerPixelLighting() = ppSave;
            const std::uint32_t* sp = gl.RenderToPixels(std0, vp, eye, kSize, kSize, 1, 0.0f, 0.0f, 0.0f, 1.0f);
            int maxGl = 0, maxSw = 0;
            for (int i = 0; sp && i < kSize * kSize; ++i) {
                for (int sh = 0; sh < 24; sh += 8) {
                    maxGl = std::max(maxGl, std::abs((int)((sp[i] >> sh) & 0xFF) - (int)((custom[(std::size_t)i] >> sh) & 0xFF)));
                    maxSw = std::max(maxSw, std::abs((int)((sws.color[(std::size_t)i] >> sh) & 0xFF) - (int)((sw.color[(std::size_t)i] >> sh) & 0xFF)));
                }
            }
            std::cout << "  pass-through vs Standard on the same renderer: OpenGL max " << maxGl << ", software max " << maxSw << "\n";
            CHECK(sp != nullptr);
            CHECK(maxGl <= 1);
            CHECK(maxSw <= 1);
        }
    }
    // A dissolved object casts a dissolved shadow, on both renderers; a vertex block moves the shadow.
    {
        const std::string cut = "okayshader_gl_cut.okayshader";
        { std::ofstream f(cut); f << "surface { if (position.x < 0.0) { discard; } albedo = albedo; }"; }
        oshader::LoadShaderFile(cut, 0);
        Time::ResetForTesting();
        for (const char* which : {"", "cut", "dissolve"}) {
            const std::string sh = std::string(which) == "cut" ? cut : std::string(which) == "dissolve" ? src + "dissolve.okayshader" : std::string();
            Scene scene("shadow");
            BuildShadowScene(scene, sh);
            if (std::string(which) == "dissolve")
                for (const auto& go : scene.Objects()) if (auto* mr = go->GetComponent<MeshRenderer>(); mr && go->name == "Caster") mr->shaderParams = "cycle=0;amount=0.45";
            Raster sw; sw.Resize(kSize, kSize); sw.Clear(0xFF000000u);
            ApplySceneLight(scene);   // the scene's Light components become the frame's lights
            RenderMeshes(sw, scene, ShadowVP(), kShadowEye);
            const std::uint32_t* px = gl.RenderToPixels(scene, ShadowVP(), kShadowEye, kSize, kSize, 1, 0.0f, 0.0f, 0.0f, 1.0f);
            CHECK(px != nullptr);
            if (!px) continue;
            const ShadowRun a = Probe(scene, sw.color.data(), kSize), b = Probe(scene, px, kSize);
            int magenta = 0;
            for (int i = 0; i < kSize * kSize; ++i) if ((px[i] & 0xFFFFFF) == 0xFF00FF) ++magenta;
            std::cout << "  shadow " << (which[0] ? which : "standard") << " (left/right/open): software " << a.left << "/" << a.right << "/" << a.open
                      << ", OpenGL " << b.left << "/" << b.right << "/" << b.open << (magenta ? " (MAGENTA)" : "") << "\n";
            CHECK(magenta == 0);
            if (std::string(which).empty()) {
                CHECK(a.left < a.open - 25 && a.right < a.open - 25);
                CHECK(b.left < b.open - 25 && b.right < b.open - 25);
            } else if (std::string(which) == "cut") {
                CHECK(a.right < a.open - 25 && a.left > a.open - 8);
                CHECK(b.right < b.open - 25 && b.left > b.open - 8);
            }
            if (shotDir) SaveSideBySide(std::string(shotDir) + "/shadow_" + (which[0] ? which : "standard") + ".png", sw, px);
        }
        std::remove(cut.c_str());
    }
    // The waving flag example: a grid on a pole, moved by flag_wave.okayshader on both renderers.
    {
        Time::ResetForTesting();
        for (int k = 0; k < 14; ++k) Time::Step(0.05f);
        Scene scene("flag");
        scene.physicsEnabled = false;
        scene.renderSettings.tonemap = false;
        scene.renderSettings.skybox = false;
        GameObject* lgo = scene.CreateGameObject("Sun");
        auto* light = lgo->AddComponent<Light>();
        light->type = Light::Type::Directional; light->intensity = 1.0f;
        lgo->transform->localRotation = Quat::Euler(35, -25, 0);
        GameObject* go = scene.CreateGameObject("Flag");
        go->transform->localRotation = Quat::Euler(90, 0, 0);   // the grid stands up, its front (+y) towards the camera
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Grid(2.0f, 1.2f, 32, 16);
        mr->doubleSided = true;
        mr->castShadows = false; mr->receiveShadows = false; mr->groundShadow = false;
        mr->shader = MeshRenderer::Shader::Custom;
        mr->customShader = src + "flag_wave.okayshader";
        CHECK(oshader::LoadShaderFile(mr->customShader, 0).compiled->ok);
        const Vec3 eye{1.5f, 0.3f, 2.2f};   // from the side, so the wave shows in the outline
        const Mat4 vp = Mat4::Perspective(55.0f, 1.0f, 0.05f, 100.0f) * Mat4::LookAt(eye, {0, 0, 0}, Vec3::Up);
        Raster sw; sw.Resize(kSize, kSize); sw.Clear(0xFF000000u);
        RenderMeshes(sw, scene, vp, eye);
        const std::uint32_t* px = gl.RenderToPixels(scene, vp, eye, kSize, kSize, 1, 0.0f, 0.0f, 0.0f, 1.0f);
        CHECK(px != nullptr);
        if (px) {
            int both = 0, onlyOne = 0, magenta = 0;
            for (int i = 0; i < kSize * kSize; ++i) {
                const bool a = (sw.color[(std::size_t)i] & 0xFFFFFF) != 0, b = (px[i] & 0xFFFFFF) != 0;
                if ((px[i] & 0xFFFFFF) == 0xFF00FF) ++magenta;
                if (a && b) ++both; else if (a || b) ++onlyOne;
            }
            std::cout << "  flag: " << both << " pixels drawn by both, " << onlyOne << " by one only" << (magenta ? " (MAGENTA)" : "") << "\n";
            CHECK(magenta == 0);
            CHECK(both > kSize * kSize / 6);
            CHECK(onlyOne < both / 8);   // the same moved silhouette
            if (shotDir) SaveSideBySide(std::string(shotDir) + "/flag_wave.png", sw, px);
        }
    }
    // The shipped example scene on both renderers: every custom object compiles on the GPU (no magenta).
    {
        Scene demo("demo");
        CHECK(SceneSerializer::LoadFromFile(demo, src + "shader_demo.okayscene"));
        demo.physicsEnabled = false;
        demo.renderSettings.skybox = false;
        Time::ResetForTesting();
        for (int k = 0; k < 30; ++k) Time::Step(0.05f);
        const Vec3 eye{0.0f, 2.6f, -3.5f};
        const Mat4 vp = Mat4::Perspective(55.0f, 2.0f, 0.1f, 200.0f) * Mat4::LookAt(eye, {0.0f, 0.8f, 4.0f}, Vec3::Up);
        const int W = kSize * 2, H = kSize;
        Raster sw; sw.Resize(W, H); sw.Clear(0xFF000000u);
        RenderMeshes(sw, demo, vp, eye);
        const std::uint32_t* px = gl.RenderToPixels(demo, vp, eye, W, H, 1, 0.0f, 0.0f, 0.0f, 1.0f);
        CHECK(px != nullptr);
        int magenta = 0;
        for (int i = 0; px && i < W * H; ++i) if ((px[i] & 0xFFFFFF) == 0xFF00FF) ++magenta;
        std::cout << "  example scene: " << magenta << " magenta pixels on OpenGL\n";
        CHECK(magenta == 0);
        if (shotDir && px) {
            Image img(W, H * 2);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const std::uint32_t a = sw.Get(x, y), b = px[(std::size_t)y * W + x];
                    img.SetPixel(x, y, Color::FromBytes(a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF));
                    img.SetPixel(x, H + y, Color::FromBytes(b & 0xFF, (b >> 8) & 0xFF, (b >> 16) & 0xFF));
                }
            img.SavePNG(std::string(shotDir) + "/example_scene_software_top_gl_bottom.png");
        }
    }
    std::remove(tmp.c_str());
    gl.Destroy();
    TEST_MAIN_RESULT();
}
