// Software renderer speed-ups must not change a pixel (docs/engine/perf_log.md). RenderMeshes is run on a set of scenes with
//   - the plain loop of the shadow depth pass (TriangleSpanDisabled), no band skipping and no light culling  ("legacy"), and
//   - every shortcut on ("fast"), and each shortcut alone,
// and the colour and depth buffers are compared bit for bit (memcmp).
//  * BandSkipEnabled: a thread (a band of rows) skips meshes and triangles that lie wholly outside its rows.
//  * LightCullingEnabled: a mesh shades with only the lights whose range reaches its bounding box.
//  * TriangleSpanDisabled: the sun's shadow maps are filled with row spans and four texels at a time.
// The scenes are chosen to hit the awkward cases: a camera inside a box and close to a wall (triangles cut by the near
// plane), a huge ground, thin and tilted triangles, textured, unlit, double sided, outlined, shiny, fogged meshes, the
// per-pixel and per-vertex lighting paths, shadows on and off, point, spot and area lights of both falloffs, lights exactly at
// the edge of their range, and a mesh whose vertices are changed between frames.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace okay;

namespace {

struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (std::uint32_t)(s >> 33); }
    float Unit() { return (float)(Next() & 0xFFFFFF) / 16777216.0f; }
    float Range(float a, float b) { return a + (b - a) * Unit(); }
    int Below(int n) { return (int)(Next() % (std::uint32_t)n); }
};

GameObject* AddMesh(Scene& s, const Mesh& m, Vec3 pos, Color col, Vec3 scale = {1, 1, 1}, Quat rot = Quat::Identity) {
    GameObject* go = s.CreateGameObject("m");
    go->transform->localPosition = pos;
    go->transform->localScale = scale;
    go->transform->localRotation = rot;
    auto* mr = go->AddComponent<MeshRenderer>();
    mr->mesh = m;
    mr->color = col;
    return go;
}

struct Options { bool shadows; bool perPixel; bool fog; };

struct Frame { std::vector<std::uint32_t> color; std::vector<float> depth; };

enum class Shortcuts { Legacy, All, BandOnly, LightsOnly, SpanOnly };

Frame Render(Scene& s, const Mat4& vp, const Vec3& eye, int w, int h, Shortcuts mode, const Options& o) {
    BandSkipEnabled()     = mode == Shortcuts::All || mode == Shortcuts::BandOnly;
    LightCullingEnabled() = mode == Shortcuts::All || mode == Shortcuts::LightsOnly;
    detail::TriangleSpanDisabled() = !(mode == Shortcuts::All || mode == Shortcuts::SpanOnly);
    ShadowsEnabled() = o.shadows;
    PerPixelLighting() = o.perPixel;
    s.renderSettings.fog = o.fog;
    s.renderSettings.fogStart = 8.0f; s.renderSettings.fogEnd = 40.0f;
    ShadowMapResolution() = 512;   // small: the shadow pass is what is being compared, not timed
    ApplySceneLight(s);
    Raster r;
    r.Resize(w, h);
    r.Clear(0xFF101820u);
    RenderMeshes(r, s, vp, eye);
    Frame f;
    f.color = r.color; f.depth = r.depth;
    BandSkipEnabled() = true; LightCullingEnabled() = true; detail::TriangleSpanDisabled() = false;
    return f;
}

bool SameFrame(const Frame& a, const Frame& b) {
    return a.color.size() == b.color.size() && a.depth.size() == b.depth.size() &&
           std::memcmp(a.color.data(), b.color.data(), a.color.size() * 4) == 0 &&
           std::memcmp(a.depth.data(), b.depth.data(), a.depth.size() * 4) == 0;
}

int CountDifferent(const Frame& a, const Frame& b) {
    int n = 0;
    for (std::size_t i = 0; i < a.color.size() && i < b.color.size(); ++i) if (a.color[i] != b.color[i]) ++n;
    return n;
}

void AddLights(Scene& s, Lcg& r, int points, int spots, int areas, bool shadowCasters) {
    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::LookRotation(Vec3{0.3f, -1, 0.2f}.Normalized(), Vec3{0, 0, 1});
    sun->AddComponent<Light>()->type = Light::Type::Directional;
    auto mk = [&](Light::Type t, float range, Vec3 pos) {
        GameObject* go = s.CreateGameObject("Lamp");
        go->transform->localPosition = pos;
        if (t == Light::Type::Spot) go->transform->localRotation = Quat::LookRotation(Vec3{r.Range(-0.3f, 0.3f), -1, r.Range(-0.3f, 0.3f)}.Normalized(), Vec3{0, 0, 1});
        auto* l = go->AddComponent<Light>();
        l->type = t; l->range = range; l->intensity = r.Range(0.4f, 1.2f);
        l->color = Color(r.Range(0.5f, 1.0f), r.Range(0.5f, 1.0f), r.Range(0.5f, 1.0f));
        l->falloff = r.Below(2) ? Light::Falloff::InverseSquare : Light::Falloff::Linear;
        l->castShadows = shadowCasters && r.Below(3) == 0;
    };
    for (int i = 0; i < points; ++i) mk(Light::Type::Point, r.Range(3.0f, 9.0f), {r.Range(-14, 14), r.Range(1.5f, 5.0f), r.Range(-14, 14)});
    for (int i = 0; i < spots; ++i) mk(Light::Type::Spot, r.Range(6.0f, 12.0f), {r.Range(-12, 12), r.Range(4.0f, 7.0f), r.Range(-12, 12)});
    for (int i = 0; i < areas; ++i) mk(Light::Type::Area, r.Range(4.0f, 8.0f), {r.Range(-12, 12), r.Range(2.0f, 4.0f), r.Range(-12, 12)});
}

// A varied level: ground, a grid of boxes and balls with different materials, some tilted and stretched.
void BuildLevel(Scene& s, Lcg& r, int lamps) {
    GameObject* ground = AddMesh(s, Mesh::Cube(1.0f), {0, -0.5f, 0}, Color(0.4f, 0.55f, 0.35f), {60, 1, 60});
    ground->GetComponent<MeshRenderer>()->specular = 0.3f;
    Image tex(16, 16);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) tex.SetPixel(x, y, ((x / 4 + y / 4) & 1) ? Color(0.9f, 0.8f, 0.5f) : Color(0.4f, 0.3f, 0.2f));
    RegisterTexture("@perf_checker", tex);
    Mesh cube = Mesh::Cube(1.0f), ball = Mesh::Sphere(0.5f, 10, 14), pole = Mesh::Cylinder(0.3f, 2.0f, 10);
    for (int i = 0; i < 90; ++i) {
        const float x = ((i % 10) - 5) * 3.1f + r.Range(-0.4f, 0.4f), z = ((i / 10) - 4) * 3.1f + r.Range(-0.4f, 0.4f);
        const Mesh& m = (i % 3 == 0) ? ball : ((i % 3 == 1) ? cube : pole);
        const Quat q = Quat::Euler(r.Range(0, 40), r.Range(0, 360), r.Range(0, 40));
        GameObject* go = AddMesh(s, m, {x, 0.6f + r.Range(0, 1.5f), z}, Color(r.Range(0.3f, 1), r.Range(0.3f, 1), r.Range(0.3f, 1)),
                                 {r.Range(0.6f, 1.6f), r.Range(0.6f, 2.2f), r.Range(0.6f, 1.6f)}, q);
        auto* mr = go->GetComponent<MeshRenderer>();
        switch (i % 9) {
            case 1: mr->texture = "@perf_checker"; break;
            case 2: mr->unlit = true; break;
            case 3: mr->doubleSided = true; mr->specular = 0.5f; mr->shininess = 40.0f; break;
            case 4: mr->outline = true; mr->outlineWidth = 0.08f; break;
            case 5: mr->specular = 0.8f; mr->shininess = 64.0f; break;
            case 6: mr->receiveShadows = false; break;
            case 7: mr->texture = "@perf_checker"; mr->unlit = true; break;
            default: break;
        }
    }
    // A flat plane, one-sided and tilted, and a thin sliver.
    AddMesh(s, Mesh::Plane(8.0f), {3, 3, 3}, Color(0.9f, 0.4f, 0.4f), {1, 1, 1}, Quat::Euler(70, 20, 0));
    AddMesh(s, Mesh::Cube(1.0f), {-6, 2, -3}, Color(0.4f, 0.4f, 0.9f), {0.02f, 6.0f, 9.0f}, Quat::Euler(5, 33, 12));
    AddLights(s, r, lamps, lamps / 4, lamps / 6, true);
}

} // namespace

int main() {
    RUN_SUITE("software renderer shortcuts leave every pixel as it was");
    const int W = 200, H = 128;
    const float aspect = (float)W / (float)H;
    struct Cam { Vec3 eye, target; const char* name; };
    const Cam cams[] = {
        {{0, 18, 30}, {0, 0, 0}, "overview"},
        {{-9, 3.0f, 9}, {2, 1, 0}, "low and wide"},
        {{0.2f, 1.0f, 0.2f}, {5, 1, -2}, "inside the middle of the level"},
        {{3.5f, 3.2f, 3.5f}, {3, 3, 3}, "right at a tilted plane (near plane cuts it)"},
        {{0, 60, 0.1f}, {0, 0, 0}, "straight down from high up"},
        {{0, 1.0f, 40}, {0, 1.0f, 0}, "along the ground, far"},
    };
    const Options opts[] = {
        {true, false, false}, {false, false, false}, {true, true, true}, {false, true, false}, {false, false, true},
    };
    int configs = 0, identical = 0;
    for (int lamps : {0, 3, 24}) {
        Lcg r(1000 + lamps);
        Scene s("perf");
        BuildLevel(s, r, lamps);
        s.Start();
        for (const Cam& c : cams) {
            const Mat4 vp = Mat4::Perspective(60.0f, aspect, 0.1f, 300.0f) * Mat4::LookAt(c.eye, c.target, Vec3{0, 1, 0});
            for (const Options& o : opts) {
                const Frame legacy = Render(s, vp, c.eye, W, H, Shortcuts::Legacy, o);
                for (Shortcuts m : {Shortcuts::All, Shortcuts::BandOnly, Shortcuts::LightsOnly, Shortcuts::SpanOnly}) {
                    const Frame f = Render(s, vp, c.eye, W, H, m, o);
                    ++configs;
                    if (SameFrame(legacy, f)) ++identical;
                    else std::printf("  DIFFERENT: lamps %d camera '%s' shadows %d perPixel %d fog %d mode %d: %d pixels\n", lamps, c.name,
                                     o.shadows, o.perPixel, o.fog, (int)m, CountDifferent(legacy, f));
                }
            }
        }
    }
    std::printf("  %d renders compared with the legacy path, %d identical\n", configs, identical);
    CHECK(identical == configs);

    RUN_SUITE("software renderer shortcuts: a mesh edited between frames");
    {
        Lcg r(77);
        Scene s("deform");
        AddMesh(s, Mesh::Cube(1.0f), {0, -0.5f, 0}, Color(0.4f, 0.5f, 0.3f), {40, 1, 40});
        Mesh wave = Mesh::Plane(12.0f);
        GameObject* sheet = AddMesh(s, wave, {0, 1.0f, 0}, Color(0.2f, 0.4f, 0.9f));
        sheet->GetComponent<MeshRenderer>()->doubleSided = true;
        AddLights(s, r, 10, 2, 1, false);
        s.Start();
        const Vec3 eye{0, 9, 16};
        const Mat4 vp = Mat4::Perspective(60.0f, aspect, 0.1f, 300.0f) * Mat4::LookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
        int same = 0, total = 0;
        for (int frame = 0; frame < 6; ++frame) {
            // the vertices move in place (height waves that grow), the way a water or terrain component edits a mesh
            Mesh& m = sheet->GetComponent<MeshRenderer>()->mesh;
            for (std::size_t i = 0; i < m.vertices.size(); ++i) m.vertices[i].y = std::sin(m.vertices[i].x * 1.3f + (float)frame) * (0.5f + 0.7f * (float)frame);
            for (const Options& o : opts) {
                const Frame a = Render(s, vp, eye, W, H, Shortcuts::Legacy, o), b = Render(s, vp, eye, W, H, Shortcuts::All, o);
                ++total; if (SameFrame(a, b)) ++same;
            }
        }
        CHECK(same == total);
    }

    TEST_MAIN_RESULT();
}
