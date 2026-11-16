// The shipped example scene examples/render_effects/render_effects.okayscene (depth of field and ambient occlusion),
// rendered headless by the software renderer. Written for this engine.
//
// What this checks:
//   * the file on disk loads, has the camera and both effects switched on with the values the docs name, and is the
//     same scene as the one BuildExample() below builds (so the example cannot rot: change one, this test fails);
//   * with ambient occlusion on, the floor right in front of the box stack is darker than with it off, and open floor
//     far from everything is not touched;
//   * with depth of field on, the pillars far behind the focus plane are blurred (the biggest step between two
//     neighbouring pixels across a pillar's edge drops a lot) and the crate in focus is bit for bit the same;
//   * both on together differ from either alone.
// OKAY_SHOT_DIR=<folder> also saves the four pictures (both off, ambient occlusion, depth of field, both) as PNGs: the
// pictures in docs/try_it.md and docs/images/render_effects/. They are rendered here by the software renderer, not on a
// GPU. OKAY_WRITE_EXAMPLE=<file> writes the scene BuildExample() builds to that file and exits (how the example was made).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okay;

namespace {
const int W = 960, H = 540;

GameObject* Box(Scene& s, const char* name, Vec3 pos, Vec3 size, Color c, float yaw = 0.0f, const char* mesh = nullptr) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = pos;
    go->transform->localScale = size;
    if (yaw != 0.0f) go->transform->localRotation = Quat::Euler(0.0f, yaw, 0.0f);
    MeshRenderer* mr = go->AddComponent<MeshRenderer>();
    if (mesh && std::strcmp(mesh, "Cylinder") == 0) mr->mesh = Mesh::Cylinder();   // the same one a saved "Cylinder" comes back as
    mr->color = c;
    return go;
}

// The example scene. The camera stands at z = -7.5 and looks along +z, so a thing at z = -1.5 is 6 away: that is the
// depth of field's Focus Distance, with a Focus Range of 2 (sharp from 5 to 7 away, fully blurred past 3 / 9). Looking
// along +z, world +x is on the LEFT of the picture, so the nook (at +x) is on the left.
void BuildExample(Scene& s) {
    s.SetName("Render effects");
    auto& rs = s.renderSettings;
    rs.skyTop = Color{0.20f, 0.28f, 0.45f, 1.0f};
    rs.skyHorizon = Color{0.72f, 0.78f, 0.86f, 1.0f};
    rs.skyBottom = Color{0.40f, 0.42f, 0.46f, 1.0f};
    rs.fog = true; rs.fogColor = rs.skyHorizon; rs.fogStart = 25.0f; rs.fogEnd = 80.0f;   // the far end of the floor fades into the haze
    rs.post.ssao = true;  rs.post.ssaoRadius = 1.2f; rs.post.ssaoIntensity = 1.0f; rs.post.ssaoPower = 1.5f;
    rs.post.dof = true;   rs.post.dofFocus = 6.0f;   rs.post.dofRange = 2.0f;

    GameObject* cam = s.CreateGameObject("Main Camera");
    cam->transform->localPosition = {0.0f, 2.1f, -7.5f};
    cam->transform->localRotation = Quat::Euler(-8.0f, 180.0f, 0.0f);   // a camera looks along its local -z: turn it to look along +z, tilted down 8 degrees
    Camera* c = cam->AddComponent<Camera>();
    c->fieldOfView = 55.0f;
    c->main = true;

    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler(48.0f, 30.0f, 0.0f);    // from behind the camera, a little to the right of it (looking along +z)
    Light* li = sun->AddComponent<Light>();
    li->type = Light::Type::Directional;
    li->intensity = 1.0f;
    li->ambient = 0.25f;
    li->castShadows = true;

    const Color grey = Color::FromBytes(140, 140, 136);
    Box(s, "Floor", {0.0f, -0.1f, 25.0f}, {160.0f, 0.2f, 90.0f}, grey);

    // The hero: a stack of crates in the focus zone (6 away), with creases where they touch each other and the floor.
    const Color wood = Color::FromBytes(206, 140, 70);
    Box(s, "Crate A", {0.0f, 0.5f, -1.5f}, {1.0f, 1.0f, 1.0f}, wood);
    Box(s, "Crate B", {0.12f, 1.4f, -1.5f}, {0.8f, 0.8f, 0.8f}, wood, 20.0f);
    Box(s, "Crate C", {1.25f, 0.35f, -1.1f}, {0.7f, 0.7f, 0.7f}, Color::FromBytes(70, 130, 200), -15.0f);
    Box(s, "Crate D", {-1.2f, 0.25f, -1.3f}, {0.5f, 0.5f, 0.5f}, Color::FromBytes(120, 190, 110), 35.0f);

    // A nook: two walls meeting at a right angle, with a box in it (creases against walls and floor).
    const Color plaster = Color::FromBytes(214, 208, 196);
    Box(s, "Nook Back Wall", {3.0f, 1.0f, 0.2f}, {3.0f, 2.0f, 0.3f}, plaster);
    Box(s, "Nook Side Wall", {4.65f, 1.0f, -1.3f}, {0.3f, 2.0f, 3.3f}, plaster);
    Box(s, "Nook Box", {3.5f, 0.3f, -0.45f}, {0.6f, 0.6f, 0.6f}, Color::FromBytes(200, 90, 80), 10.0f);

    // Near the lens (about 3 away): blurred, because they are too close.
    Box(s, "Near Box Yellow", {-1.3f, 0.35f, -4.5f}, {0.7f, 0.7f, 0.7f}, Color::FromBytes(230, 200, 80), 25.0f);
    Box(s, "Near Box Pink", {1.5f, 0.25f, -4.8f}, {0.5f, 0.5f, 0.5f}, Color::FromBytes(210, 90, 150), -20.0f);

    // Two rows of pillars and a line of stepping stones going away: the farther, the softer.
    const Color pillarCols[4] = {Color::FromBytes(222, 190, 70), Color::FromBytes(200, 70, 60),
                                 Color::FromBytes(60, 160, 170), Color::FromBytes(150, 110, 190)};
    for (int i = 0; i < 7; ++i) {
        const float z = 3.0f + 3.6f * (float)i;
        char name[40];
        std::snprintf(name, sizeof name, "Round Pillar %d", i + 1);
        Box(s, name, {-3.6f, 1.6f, z}, {0.9f, 3.2f, 0.9f}, pillarCols[i % 4], 0.0f, "Cylinder");
        std::snprintf(name, sizeof name, "Square Pillar %d", i + 1);
        Box(s, name, {3.6f, 1.6f, z + 1.8f}, {0.9f, 3.2f, 0.9f}, pillarCols[(i + 2) % 4]);
    }
    for (int i = 0; i < 5; ++i) {
        char name[40];
        std::snprintf(name, sizeof name, "Stone %d", i + 1);
        Box(s, name, {0.0f, 0.25f, 3.0f + 4.0f * (float)i}, {1.0f, 0.5f, 1.0f}, Color::FromBytes(190, 190, 186));
    }
}

struct View { Mat4 vp; Vec3 eye; };
View ViewOf(const Scene& s, int w, int h) {
    View v;
    const Camera* c = s.mainCamera;
    v.vp = c->ProjectionMatrix((float)w / (float)h) * c->ViewMatrix();
    v.eye = c->RenderPosition();
    return v;
}

Raster Render(Scene& s, bool ssao, bool dof, int w = W, int h = H) {
    s.renderSettings.post.ssao = ssao;
    s.renderSettings.post.dof = dof;
    ApplySceneLight(s);
    Raster r; r.Resize(w, h); r.Clear(0u);       // transparent: the host draws the sky behind, as the editor does
    const View v = ViewOf(s, w, h);
    RenderMeshes(r, s, v.vp, v.eye);
    return r;
}

// World point -> pixel.
void Pixel(const View& v, const Vec3& p, int w, int h, int& px, int& py) {
    const Vec4 c = v.vp * Vec4{p, 1.0f};
    px = (int)((c.x / c.w * 0.5f + 0.5f) * (float)w);
    py = (int)((1.0f - (c.y / c.w * 0.5f + 0.5f)) * (float)h);
}
// Brightness times coverage (alpha): the frame's transparent background counts as black, and depth of field blurs the
// alpha, so a blurred edge against the background is a smooth ramp in this number.
int Lum(std::uint32_t v) { return (int)(((v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF)) * (v >> 24) / 255u); }

// The biggest step in that brightness between two neighbouring pixels along a row, around column cx (+-span).
int BiggestStep(const Raster& r, int y, int cx, int span) {
    int best = 0;
    for (int x = std::max(1, cx - span); x < std::min(r.width - 1, cx + span); ++x)
        best = std::max(best, std::abs(Lum(r.color[(std::size_t)y * r.width + x]) - Lum(r.color[(std::size_t)y * r.width + x - 1])));
    return best;
}

// Scene text without the random guid lines (every object gets a new one when it is created).
std::string WithoutGuids(const std::string& text) {
    std::string out; std::size_t i = 0;
    while (i < text.size()) {
        std::size_t e = text.find('\n', i); if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(i, e - i);
        if (line.find("guid ") == std::string::npos) { out += line; out += '\n'; }
        i = e + 1;
    }
    return out;
}

// The picture with the sky drawn behind it (the transparent pixels), as the editor shows it.
void SaveShot(const Scene& s, const Raster& r, const std::string& path) {
    const auto& rs = s.renderSettings;
    Image img(r.width, r.height);
    std::uint32_t* out = (std::uint32_t*)img.Data();
    for (int y = 0; y < r.height; ++y) {
        const float t = (float)y / (float)(r.height - 1);                      // 0 top .. 1 bottom
        const float k = std::min(1.0f, t / 0.5f);                              // top -> horizon over the upper half
        const float sr = rs.skyTop.r + (rs.skyHorizon.r - rs.skyTop.r) * k;
        const float sg = rs.skyTop.g + (rs.skyHorizon.g - rs.skyTop.g) * k;
        const float sb = rs.skyTop.b + (rs.skyHorizon.b - rs.skyTop.b) * k;
        for (int x = 0; x < r.width; ++x) {
            const std::uint32_t p = r.color[(std::size_t)y * r.width + x];
            const float a = (float)(p >> 24) / 255.0f;
            const float R = (float)(p & 0xFF) / 255.0f, G = (float)((p >> 8) & 0xFF) / 255.0f, B = (float)((p >> 16) & 0xFF) / 255.0f;
            auto q = [](float v) { return (std::uint32_t)(std::fmin(1.0f, std::fmax(0.0f, v)) * 255.0f + 0.5f); };
            // the stored colour is premultiplied only where DoF blurred it; an untouched frame stores plain colour with
            // alpha 255 or 0, so blend as "colour over sky" with the stored alpha
            out[(std::size_t)y * r.width + x] = 0xFF000000u | (q(B * a + sb * (1.0f - a)) << 16) | (q(G * a + sg * (1.0f - a)) << 8) | q(R * a + sr * (1.0f - a));
        }
    }
    img.SavePNG(path);
}
} // namespace

int main() {
    RUN_SUITE("render_effects_example");
    PerPixelLighting() = true;                        // the same lighting path in every frame, so only the effects differ
    SSAOEnabled() = false; BloomEnabled() = false; FXAAEnabled() = false;

    if (const char* w = std::getenv("OKAY_WRITE_EXAMPLE"); w && *w) {
        Scene s("x"); BuildExample(s);
        std::string err;
        if (!SceneSerializer::SaveToFile(s, w, &err)) { std::fprintf(stderr, "could not write %s: %s\n", w, err.c_str()); return 1; }
        std::printf("wrote %s\n", w);
        return 0;
    }

    // ---- the file on disk
    const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/render_effects/render_effects.okayscene";
    Scene file("loaded");
    std::string err;
    const bool loaded = SceneSerializer::LoadFromFile(file, path, &err, nullptr);
    if (!loaded) std::fprintf(stderr, "  could not load %s: %s\n", path.c_str(), err.c_str());
    CHECK(loaded);
    if (!loaded) TEST_MAIN_RESULT();
    {   // the example is exactly what BuildExample builds (compared before the scene starts, so nothing has changed yet)
        Scene built("built"); BuildExample(built);
        const std::string a = WithoutGuids(SceneSerializer::Serialize(file)), b = WithoutGuids(SceneSerializer::Serialize(built));
        if (a != b) std::fprintf(stderr, "  the example file differs from BuildExample(): regenerate it with OKAY_WRITE_EXAMPLE=%s\n", path.c_str());
        CHECK(a == b);
    }
    file.Start();                    // loaded components wait until the scene starts; then the camera can be picked
    file.Update(0.0f);
    file.ResolveMainCamera();
    CHECK(file.mainCamera != nullptr);
    if (!file.mainCamera) TEST_MAIN_RESULT();
    CHECK(file.renderSettings.post.ssao);
    CHECK(file.renderSettings.post.dof);
    CHECK_NEAR(file.renderSettings.post.dofFocus, 6.0, 1e-6);
    CHECK_NEAR(file.renderSettings.post.dofRange, 2.0, 1e-6);
    CHECK_NEAR(file.renderSettings.post.ssaoRadius, 1.2, 1e-6);
    CHECK_NEAR(file.renderSettings.post.ssaoIntensity, 1.0, 1e-6);
    CHECK(file.Objects().size() >= 30);

    // ---- the four renders
    Scene& s = file;
    const Raster off = Render(s, false, false);
    const Raster ao = Render(s, true, false);
    const Raster dof = Render(s, false, true);
    const Raster both = Render(s, true, true);
    const View v = ViewOf(s, W, H);

    // ambient occlusion: the floor right in front of the box stack darkens, open floor does not
    int px, py;
    Pixel(v, Vec3{0.0f, 0.0f, -2.12f}, W, H, px, py);      // 0.12 in front of crate A's front face, on the floor
    const int crease = (int)(ao.color[(std::size_t)py * W + px] & 0xFF), plain = (int)(off.color[(std::size_t)py * W + px] & 0xFF);
    std::printf("  floor beside the crate: %d with ambient occlusion, %d without\n", crease, plain);
    CHECK(plain > 0);
    CHECK(crease < plain - 4);
    Pixel(v, Vec3{0.5f, 0.0f, -3.6f}, W, H, px, py);       // open floor, more than the radius from anything
    CHECK(ao.color[(std::size_t)py * W + px] == off.color[(std::size_t)py * W + px]);

    // depth of field: the crate in focus is bit for bit the same, a pillar far away is blurred
    Pixel(v, Vec3{0.0f, 0.5f, -2.0f}, W, H, px, py);       // the middle of crate A's front face: about 5.7 away, in the focus zone
    CHECK(dof.color[(std::size_t)py * W + px] == off.color[(std::size_t)py * W + px]);
    Pixel(v, Vec3{-3.6f, 2.2f, 17.4f}, W, H, px, py);      // the 5th left pillar, about 25 away
    const int stepOff = BiggestStep(off, py, px, 40), stepDof = BiggestStep(dof, py, px, 40);
    std::printf("  far pillar edge: biggest step between neighbouring pixels %d sharp, %d with depth of field\n", stepOff, stepDof);
    CHECK(stepOff > 120);
    CHECK(stepDof * 5 < stepOff * 2);                      // under 40 percent of it

    // both together differ from either alone
    CHECK(both.color != ao.color);
    CHECK(both.color != dof.color);
    CHECK(ao.color != off.color);
    CHECK(dof.color != off.color);

    // OKAY_SHOT_DIR: the pictures for the docs
    if (const char* dir = std::getenv("OKAY_SHOT_DIR"); dir && *dir) {
        SaveShot(s, off, std::string(dir) + "/render_effects_off.png");
        SaveShot(s, ao, std::string(dir) + "/render_effects_ssao.png");
        SaveShot(s, dof, std::string(dir) + "/render_effects_dof.png");
        SaveShot(s, both, std::string(dir) + "/render_effects_both.png");
    }
    TEST_MAIN_RESULT();
}
