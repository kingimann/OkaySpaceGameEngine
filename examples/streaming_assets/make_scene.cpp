// Writes the "streaming assets" example (examples/streaming_assets): our own procedural pictures, one rock model and a
// scene with a field of 144 textured rocks, each with a LOD Group, and an Asset Streaming component.
// Build target make_streaming_assets; run it with the folder to write into:
//   make_streaming_assets examples/streaming_assets
// Everything it writes is made here (no outside pictures or models).
#include <Okay.hpp>
#include "okay/Components/AssetStreamer.hpp"
#include "okay/Components/FreeRoamController.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace okay;

namespace {

float Hash(int x, int y, int s) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

// Smooth value noise, 0..1.
float Noise(float x, float y, int s) {
    const int ix = (int)std::floor(x), iy = (int)std::floor(y);
    const float fx = x - ix, fy = y - iy;
    const float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    const float a = Hash(ix, iy, s), b = Hash(ix + 1, iy, s), c = Hash(ix, iy + 1, s), d = Hash(ix + 1, iy + 1, s);
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}

float Fbm(float x, float y, int s) {
    float v = 0, amp = 0.5f;
    for (int o = 0; o < 4; ++o) { v += amp * Noise(x, y, s + o); x *= 2.03f; y *= 2.03f; amp *= 0.5f; }
    return v;
}

Image Picture(int kind, int n) {
    Image img(n, n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float u = (float)x / n, v = (float)y / n;
            Color c;
            if (kind == 0) {   // bricks: offset rows, mortar lines, a little noise per brick
                const int row = (int)(v * 8);
                const float bu = u * 4 + (row & 1) * 0.5f;
                const bool mortar = std::fmod(v * 8, 1.0f) < 0.08f || std::fmod(bu, 1.0f) < 0.04f;
                const float t = Hash((int)bu, row, 3) * 0.25f + Fbm(u * 32, v * 32, 1) * 0.2f;
                c = mortar ? Color(0.75f, 0.73f, 0.68f, 1) : Color(0.55f + t, 0.22f + t * 0.4f, 0.15f, 1);
            } else if (kind == 1) {   // wooden planks: grain rings along the plank
                const int plank = (int)(u * 5);
                const float g = std::sin((v * 40 + Fbm(u * 6, v * 2, 7 + plank) * 6) * 3.14159f) * 0.5f + 0.5f;
                const bool gap = std::fmod(u * 5, 1.0f) < 0.03f;
                c = gap ? Color(0.15f, 0.1f, 0.06f, 1) : Color(0.55f + 0.15f * g, 0.36f + 0.1f * g, 0.18f + 0.05f * g, 1);
            } else if (kind == 2) {   // floor tiles with numbers of light and dark
                const int tx = (int)(u * 8), ty = (int)(v * 8);
                const bool grout = std::fmod(u * 8, 1.0f) < 0.05f || std::fmod(v * 8, 1.0f) < 0.05f;
                const float k = ((tx + ty) & 1) ? 0.8f : 0.35f;
                const float s = Fbm(u * 24, v * 24, 11) * 0.15f;
                c = grout ? Color(0.2f, 0.2f, 0.22f, 1) : Color(k + s, k * 0.95f + s, k * 0.85f + s, 1);
            } else {   // speckled stone
                const float f = Fbm(u * 12, v * 12, 21);
                const float sp = Hash(x, y, 5) > 0.97f ? 0.25f : 0.0f;
                c = Color(0.35f + f * 0.4f + sp, 0.35f + f * 0.38f + sp, 0.33f + f * 0.35f + sp, 1);
            }
            c.r = std::fmin(1.0f, c.r); c.g = std::fmin(1.0f, c.g); c.b = std::fmin(1.0f, c.b);
            img.SetPixel(x, y, c);
        }
    return img;
}

// A lumpy rock: a UV sphere pushed in and out by noise (the seam where the UVs wrap is kept by the simplifier).
void WriteRock(const std::string& path, int sides, int rings) {
    std::ofstream o(path);
    o << "# rock for the streaming example, made by make_scene.cpp\n";
    for (int r = 0; r <= rings; ++r)
        for (int s = 0; s <= sides; ++s) {
            const float th = 3.14159265f * r / rings, ph = 6.2831853f * (s % sides) / sides;
            float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            const float k = 0.75f + 0.5f * Fbm(x * 2.0f + 5.0f, z * 2.0f + y * 1.7f + 5.0f, 31);
            o << "v " << x * k << " " << y * k * 0.8f << " " << z * k << "\n";
            o << "vt " << (float)s / sides * 2.0f << " " << (float)r / rings << "\n";
        }
    auto id = [&](int r, int s) { return r * (sides + 1) + s + 1; };
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < sides; ++s) {
            const int a = id(r, s), b = id(r + 1, s), c = id(r + 1, s + 1), d = id(r, s + 1);
            if (r != 0) o << "f " << a << "/" << a << " " << d << "/" << d << " " << b << "/" << b << "\n";
            if (r != rings - 1) o << "f " << b << "/" << b << " " << d << "/" << d << " " << c << "/" << c << "\n";
        }
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const char* pics[] = {"bricks.png", "planks.png", "tiles.png", "stone.png"};
    for (int i = 0; i < 4; ++i) Picture(i, 512).SavePNG(dir + "/" + pics[i]);
    WriteRock(dir + "/rock.obj", 64, 32);
    // The model's import settings: Generate LODs, so dragging rock.obj into any scene gives it a LOD Group too.
    std::ofstream(dir + "/rock.obj.okaymeta") << "okaymeta 1\nguid 5f1e0c2a9b7d4e3f8a6b1c2d3e4f5a6b\ntype model\n"
                                                "importer model 1\nset scale 1\nset normals import\nset importAnimations 1\nset smoothingAngle 60\n"
                                                "set generateLods 1\nset lodLevels 4\nset lodReduction 0.4\n";

    Mesh rock; std::string tex, err;
    if (!modeling::LoadMeshAsset(dir + "/rock.obj", rock, &tex, &err)) { std::fprintf(stderr, "rock.obj: %s\n", err.c_str()); return 1; }

    Scene s("Streaming assets: 144 LOD rocks, streamed textures");
    GameObject* sun = s.CreateGameObject("Sun");
    sun->transform->localRotation = Quat::Euler({50.0f, -30.0f, 0.0f});
    sun->AddComponent<Light>()->type = Light::Type::Directional;
    GameObject* camGo = s.CreateGameObject("Main Camera");
    camGo->transform->localPosition = {0.0f, 2.5f, 34.0f};
    auto* cam = camGo->AddComponent<Camera>();
    cam->farClip = 600.0f;
    s.mainCamera = cam;
    camGo->AddComponent<FreeRoamController>()->moveSpeed = 10.0f;
    GameObject* stream = s.CreateGameObject("Asset Streaming");
    auto* as = stream->AddComponent<AssetStreamer>();
    as->startSize = 32;
    as->budgetMB = 48.0f;
    as->uploadMBPerFrame = 4.0f;
    GameObject* ground = s.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.6f, 0};
    ground->transform->localScale = {12, 1, 12};
    auto* gm = ground->AddComponent<MeshRenderer>();
    gm->mesh = Mesh::Plane();
    gm->texture = "tiles.png";
    gm->tiling = {30, 30};
    for (int z = 0; z < 12; ++z)
        for (int x = 0; x < 12; ++x) {
            GameObject* go = s.CreateGameObject("Rock " + std::to_string(z * 12 + x + 1));
            const float jx = (Hash(x, z, 1) - 0.5f) * 2.0f, jz = (Hash(x, z, 2) - 0.5f) * 2.0f;
            go->transform->localPosition = {(x - 5.5f) * 6.0f + jx, 0.2f, -(float)z * 7.0f + jz};
            go->transform->localRotation = Quat::Euler({0.0f, Hash(x, z, 4) * 360.0f, 0.0f});
            const float sc = 1.2f + Hash(x, z, 3) * 1.0f;
            go->transform->localScale = {sc, sc, sc};
            go->isStatic = true;
            auto* mr = go->AddComponent<MeshRenderer>();
            mr->mesh = rock;
            mr->meshPath = "rock.obj";
            mr->meshPathHash = modeling::GeometryHash(rock);
            mr->texture = pics[(x + z) % 4];
            auto* lod = go->AddComponent<LODGroup>();
            lod->levels.resize(4);
            const float dist[4] = {14.0f, 30.0f, 55.0f, 90.0f}, ratio[4] = {1.0f, 0.4f, 0.16f, 0.06f};
            for (int i = 0; i < 4; ++i) { lod->levels[(std::size_t)i].distance = dist[i]; lod->levels[(std::size_t)i].ratio = ratio[i]; }
            lod->cullDistance = 140.0f;
            lod->hysteresis = 0.08f;
            lod->fadeDuration = 0.4f;
        }
    std::ofstream(dir + "/streaming_assets.okayscene") << SceneSerializer::Serialize(s);
    std::printf("wrote %s/streaming_assets.okayscene, rock.obj (%d triangles) and 4 pictures\n", dir.c_str(), rock.TriangleCount());
    return 0;
}
