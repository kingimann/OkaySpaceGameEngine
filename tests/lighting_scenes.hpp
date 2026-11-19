#pragma once
// Small test scenes for the lighting work (headless renders and image tests). Written for this engine.
// Each builder fills a Scene and returns the camera (eye, target) that shows it well.
#include <Okay.hpp>
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace lightscene {
using namespace okay;

struct CamView { Vec3 eye, target; float fov = 50.0f; };

inline GameObject* MakeBox(Scene& s, const char* n, Vec3 p, Vec3 sc, Color c) {
    GameObject* g = s.CreateGameObject(n);
    g->transform->localPosition = p;
    g->transform->localScale = sc;
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(1.0f);
    mr->color = c;
    return g;
}
inline Light* AddLight(Scene& s, const char* n, Light::Type t, Vec3 pos, float pitch, float yaw, Color col, float inten) {
    GameObject* g = s.CreateGameObject(n);
    g->transform->localPosition = pos;
    g->transform->localRotation = Quat::Euler(pitch, yaw, 0.0f);
    auto* l = g->AddComponent<Light>();
    l->type = t; l->color = col; l->intensity = inten;
    return l;
}

/// 1. Sun and cubes on a big floor: shadows, cascades, the far edge of the shadow distance.
inline CamView BuildSun(Scene& s) {
    MakeBox(s, "Floor", {0, -0.5f, 20}, {80, 1, 120}, Color::FromBytes(120, 150, 100));
    for (int i = 0; i < 6; ++i)
        MakeBox(s, "Cube", {-6.0f + 2.4f * (i % 3) + (i / 3) * 1.1f, 0.75f + 0.5f * (i % 2), 4.0f + 7.0f * (i / 3)},
            {1.5f, 1.5f + (i % 2), 1.5f}, Color::FromBytes(200 - 20 * i, 90 + 25 * i, 80 + 10 * i));
    for (int i = 0; i < 8; ++i)
        MakeBox(s, "Far", {-12.0f + 3.2f * i, 1.5f, 30.0f + 6.0f * (i % 3)}, {2, 3, 2}, Color::FromBytes(180, 170, 150));
    Light* sun = AddLight(s, "Sun", Light::Type::Directional, {0, 10, 0}, 30.0f, 105.0f, Color::FromBytes(255, 244, 224), 1.0f);
    sun->ambient = 0.3f;
    return {{-3, 5.5f, -6}, {0, 0.5f, 12}, 55.0f};
}
/// 2. Indoor room with one lamp and a window-like opening: walls, ceiling, point light, shadows from furniture.
inline CamView BuildRoom(Scene& s, bool withLamp = true) {
    Color wall = Color::FromBytes(200, 190, 175);
    MakeBox(s, "Floor", {0, -0.1f, 0}, {8, 0.2f, 8}, Color::FromBytes(140, 100, 70));
    MakeBox(s, "Ceiling", {0, 3.1f, 0}, {8, 0.2f, 8}, wall);
    MakeBox(s, "WallBack", {0, 1.5f, 4.1f}, {8.4f, 3.2f, 0.2f}, wall);
    MakeBox(s, "WallLeft", {-4.1f, 1.5f, 0}, {0.2f, 3.2f, 8.4f}, Color::FromBytes(170, 120, 110));
    MakeBox(s, "WallRight", {4.1f, 1.5f, 0}, {0.2f, 3.2f, 8.4f}, Color::FromBytes(110, 140, 170));
    MakeBox(s, "Table", {0, 0.5f, 1.0f}, {2.2f, 0.12f, 1.2f}, Color::FromBytes(90, 60, 40));
    for (int i = 0; i < 4; ++i)
        MakeBox(s, "Leg", {(i % 2 ? 0.95f : -0.95f), 0.22f, 1.0f + (i / 2 ? 0.45f : -0.45f)}, {0.12f, 0.45f, 0.12f}, Color::FromBytes(90, 60, 40));
    MakeBox(s, "Crate", {-2.6f, 0.6f, 2.8f}, {1.2f, 1.2f, 1.2f}, Color::FromBytes(170, 130, 70));
    {
        auto* mr = MakeBox(s, "Lamp", {0, 2.5f, 1.0f}, {0.2f, 0.2f, 0.2f}, Color::FromBytes(255, 240, 200))->GetComponent<MeshRenderer>();
        mr->emissive = Color::FromBytes(255, 230, 160);
        mr->castShadows = false;   // a bulb must not block its own light
    }
    if (withLamp) {
        Light* l = AddLight(s, "Bulb", Light::Type::Point, {0, 2.4f, 1.0f}, 0, 0, Color::FromBytes(255, 214, 160), 1.6f);
        l->range = 9.0f; l->ambient = 0.12f;
    }
    return {{0, 1.8f, -3.6f}, {0, 1.2f, 2.0f}, 70.0f};
}
/// A little village square with no lights at all (the lighting presets add them): houses, trees, a well, a wall.
inline CamView BuildVillage(Scene& s) {
    MakeBox(s, "Ground", {0, -0.5f, 8}, {70, 1, 70}, Color::FromBytes(110, 140, 90));
    MakeBox(s, "Path", {0, 0.01f, 8}, {3.2f, 0.04f, 40}, Color::FromBytes(170, 150, 120));
    for (int i = 0; i < 4; ++i) {
        const float x = (i % 2 ? 7.5f : -7.5f), z = 6.0f + 9.0f * (i / 2);
        MakeBox(s, "House", {x, 1.6f, z}, {5, 3.2f, 5}, Color::FromBytes(205 - 15 * i, 185, 160));
        GameObject* roof = MakeBox(s, "Roof", {x, 3.5f, z}, {5.6f, 0.6f, 5.6f}, Color::FromBytes(150, 70, 60));
        roof->transform->localRotation = Quat::Euler(0, 45, 0);
    }
    for (int i = 0; i < 6; ++i) {
        const float x = (i % 2 ? 3.8f : -3.8f), z = 3.0f + 5.0f * (i / 2);
        MakeBox(s, "Trunk", {x, 1.0f, z}, {0.4f, 2.0f, 0.4f}, Color::FromBytes(100, 70, 45));
        MakeBox(s, "Crown", {x, 2.8f, z}, {2.0f, 2.0f, 2.0f}, Color::FromBytes(60, 120, 60));
    }
    MakeBox(s, "Well", {0, 0.6f, 12}, {1.4f, 1.2f, 1.4f}, Color::FromBytes(140, 140, 145));
    MakeBox(s, "Wall", {0, 1.0f, 26}, {20, 2.0f, 0.5f}, Color::FromBytes(160, 155, 150));
    return {{0, 3.2f, -3.0f}, {0, 1.2f, 12.0f}, 62.0f};
}
/// 3. A spot light cone on a floor with a pillar in it.
inline CamView BuildSpot(Scene& s) {
    MakeBox(s, "Floor", {0, -0.1f, 4}, {16, 0.2f, 16}, Color::FromBytes(150, 150, 155));
    MakeBox(s, "Wall", {0, 2.0f, 10}, {16, 4.2f, 0.2f}, Color::FromBytes(170, 165, 160));
    MakeBox(s, "Pillar", {-0.8f, 1.0f, 3.7f}, {0.8f, 2.0f, 0.8f}, Color::FromBytes(190, 100, 90));
    Light* l = AddLight(s, "Spot", Light::Type::Spot, {-3.0f, 5.0f, 0.5f}, 52.0f, 35.0f, Color::FromBytes(255, 240, 210), 2.2f);
    l->spotAngle = 50.0f; l->spotSoftness = 0.5f; l->range = 16.0f; l->ambient = 0.06f;
    return {{0, 3.5f, -4.0f}, {0, 0.5f, 6.0f}, 60.0f};
}
/// 4. Many coloured point lights over a floor with pillars.
inline CamView BuildPoints(Scene& s) {
    MakeBox(s, "Floor", {0, -0.1f, 6}, {24, 0.2f, 24}, Color::FromBytes(170, 170, 170));
    for (int i = 0; i < 9; ++i)
        MakeBox(s, "Pillar", {-6.0f + 3.0f * (i % 3) * 1.0f, 1.0f, 3.0f + 3.2f * (i / 3)}, {0.7f, 2.0f, 0.7f}, Color::FromBytes(200, 200, 200));
    const Color cols[] = {Color::FromBytes(255, 80, 60), Color::FromBytes(70, 140, 255), Color::FromBytes(90, 255, 120),
                          Color::FromBytes(255, 210, 70), Color::FromBytes(220, 90, 255), Color::FromBytes(80, 240, 240)};
    for (int i = 0; i < 12; ++i) {
        Light* l = AddLight(s, "Lamp", Light::Type::Point, {-7.0f + 1.3f * i, 1.6f + 0.5f * (i % 3), 2.0f + 5.5f * ((i * 5) % 4) / 3.0f}, 0, 0, cols[i % 6], 1.2f);
        l->range = 6.0f; l->ambient = 0.05f; l->castShadows = i < 3;
    }
    return {{0, 4.5f, -4.0f}, {0, 0.5f, 8.0f}, 60.0f};
}

/// 5. The same lamps, much brighter: the old hard clamp turned every lamp above 1.0 into the same flat white.
inline CamView BuildBrightPoints(Scene& s) {
    CamView v = BuildPoints(s);
    for (const auto& go : s.Objects()) if (auto* l = go->GetComponent<Light>()) { l->intensity = 3.0f; l->range = 7.0f; }
    return v;
}
/// 6. The room with no bulb: an area panel on the ceiling and a window of sun with a mask.
inline CamView BuildPanelRoom(Scene& s) {
    CamView v = BuildRoom(s, false);
    Light* panel = AddLight(s, "Panel", Light::Type::Area, {0, 2.95f, 1.0f}, 90.0f, 0.0f, Color::FromBytes(255, 225, 190), 1.4f);
    panel->areaWidth = 2.4f; panel->areaHeight = 1.4f; panel->range = 10.0f; panel->ambient = 0.10f;
    Light* sun = AddLight(s, "Window sun", Light::Type::Directional, {0, 6, 0}, 40.0f, 80.0f, Color::FromBytes(255, 240, 210), 0.9f);
    sun->cookie = "builtin:window"; sun->cookieScale = 3.5f; sun->ambient = 0.10f;
    return v;
}

/// Software render of `s` from `v` into a W x H image (sky colour behind). Not a real window.
inline Image RenderScene(Scene& s, const CamView& v, int W, int H, std::uint32_t sky = 0xFFD8B890u) {
    Raster r;
    r.Resize(W, H);
    r.Clear(sky);
    ApplySceneLight(s);
    const Mat4 vp = Mat4::Perspective(v.fov, (float)W / H, 0.1f, 200.0f) * Mat4::LookAt(v.eye, v.target, Vec3{0, 1, 0});
    RenderMeshes(r, s, vp, v.eye, nullptr);
    Image im(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const std::uint32_t c = r.Get(x, y);
            im.SetPixel(x, y, Color::FromBytes(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF));
        }
    return im;
}
} // namespace lightscene
