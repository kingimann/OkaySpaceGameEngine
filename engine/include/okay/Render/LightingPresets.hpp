#pragma once
// ---------------------------------------------------------------------------
// One-click lighting presets: "Sunny day", "Sunset", "Night with lamps", "Indoor", "Spooky fog".
//
// A preset sets the scene's sky, ambient light, fog and post look (the existing Environment and look-preset settings),
// and puts the lights in the scene: a sun or moon, plus lamps, a ceiling panel or a lantern where the look needs them.
// Everything it adds is a normal Light object whose name starts with "Lighting: ", so you can select, move and edit it, and
// applying a preset again (or another one) replaces those objects. A Directional light you made yourself is reused as the
// sun/moon (its settings change), and any other lights of yours are left alone. The editor's Lighting window applies one
// as a single undo step.
//
// Written for this engine from the look presets in Atmosphere.hpp and general lighting practice (colour temperature of
// sunlight, moonlight and lamps). No other project's code or data was used.
// ---------------------------------------------------------------------------
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Atmosphere.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <string>

namespace okay {

struct LightingPresetInfo { const char* name; const char* blurb; };

inline const LightingPresetInfo* LightingPresetList(int& count) {
    static const LightingPresetInfo k[] = {
        {"Sunny day",        "Bright warm sun, blue sky, crisp shadows."},
        {"Sunset",           "Low orange sun, long soft shadows, purple sky."},
        {"Night with lamps", "Dim blue moon and warm street lamps."},
        {"Indoor",           "A ceiling panel, a sunny window and soft shadows."},
        {"Spooky fog",       "Thick teal fog, a weak moon and a flickering lantern."},
    };
    count = 5;
    return k;
}

namespace lightingpreset_detail {
inline constexpr const char* kPrefix = "Lighting: ";
inline bool IsPresetObject(const GameObject& go) { return go.name.compare(0, 10, kPrefix) == 0; }

/// World box of the meshes in the scene (a 20 m box around the origin when there are none).
inline void SceneBox(const Scene& scene, Vec3& lo, Vec3& hi) {
    lo = {1e30f, 1e30f, 1e30f}; hi = {-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (const auto& go : scene.Objects()) {
        if (!go->active || IsPresetObject(*go)) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !mr->enabled) continue;
        Vec3 blo, bhi; mr->DisplayMesh().Bounds(blo, bhi);
        const Mat4 m = go->transform->LocalToWorldMatrix();
        for (int c = 0; c < 8; ++c) {
            const Vec3 w = m.MultiplyPoint({(c & 1) ? bhi.x : blo.x, (c & 2) ? bhi.y : blo.y, (c & 4) ? bhi.z : blo.z});
            lo.x = std::fmin(lo.x, w.x); lo.y = std::fmin(lo.y, w.y); lo.z = std::fmin(lo.z, w.z);
            hi.x = std::fmax(hi.x, w.x); hi.y = std::fmax(hi.y, w.y); hi.z = std::fmax(hi.z, w.z);
            any = true;
        }
    }
    if (!any) { lo = {-10, 0, -10}; hi = {10, 6, 10}; return; }
    // A huge ground plane would put lamps kilometres away: limit the box to 60 m around its centre.
    for (int a = 0; a < 3; ++a) {
        float* l = &lo.x + a; float* h = &hi.x + a;
        if (*h - *l > 60.0f) { const float c = 0.5f * (*l + *h); *l = c - 30.0f; *h = c + 30.0f; }
    }
}

/// A new "Lighting: <name>" object with a Light.
inline Light* NewLight(Scene& scene, const std::string& name, Light::Type type, const Vec3& pos, float pitch, float yaw) {
    GameObject* go = scene.CreateGameObject(std::string(kPrefix) + name);
    go->transform->localPosition = pos;
    go->transform->localRotation = Quat::Euler(pitch, yaw, 0.0f);
    Light* l = go->AddComponent<Light>();
    l->type = type;
    return l;
}
/// The scene's own first Directional light (reused as sun or moon), or a new "Lighting: Sun".
inline Light* SunLight(Scene& scene, const char* newName) {
    for (const auto& go : scene.Objects()) {
        if (!go->active || IsPresetObject(*go)) continue;
        if (auto* l = go->GetComponent<Light>()) if (l->type == Light::Type::Directional) return l;
    }
    return NewLight(scene, newName, Light::Type::Directional, {0, 10, 0}, 50, 30);
}
inline void Aim(Light* l, float pitch, float yaw) { l->gameObject->transform->localRotation = Quat::Euler(pitch, yaw, 0.0f); }
inline void SunDefaults(Light* l) {   // the common starting point every preset adjusts
    l->enabled = true; l->castShadows = true; l->shadowStrength = 1.0f; l->shadowBias = 1.0f; l->shadowSoftness = 0.5f;
    l->useTemperature = true; l->cookie.clear(); l->flicker = 0.0f; l->unit = Light::Unit::Simple;
    l->ambientColor = Color::White;
}
inline void Lamp(Light* l, float kelvin, float intensity, float range) {
    l->useTemperature = true; l->temperature = kelvin; l->intensity = intensity; l->range = range;
    l->falloff = Light::Falloff::InverseSquare; l->ambient = 0.0f; l->castShadows = false; l->shadowSoftness = 0.6f;
}
} // namespace lightingpreset_detail

/// Apply the preset called `name` (one of LightingPresetList). Returns false for an unknown name and changes nothing.
inline bool ApplyLightingPreset(Scene& scene, const std::string& name) {
    using namespace lightingpreset_detail;
    using RS = Scene::RenderSettings;
    int n = 0; const LightingPresetInfo* list = LightingPresetList(n);
    bool known = false;
    for (int i = 0; i < n; ++i) if (name == list[i].name) known = true;
    if (!known) return false;

    for (const auto& go : scene.Objects())               // replace what an earlier preset added
        if (IsPresetObject(*go)) scene.Destroy(go.get());

    Vec3 lo, hi; SceneBox(scene, lo, hi);
    const Vec3 ctr{(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
    const float sx = hi.x - lo.x, sz = hi.z - lo.z;
    RS& rs = scene.renderSettings;
    const std::string empty;

    if (name == "Sunny day") {
        ApplyLookPreset(rs, "Default");
        rs.skybox = true; rs.skyMode = RS::SkyProcedural; rs.skyHaze = 0.35f; rs.cloudCover = 0.25f;
        rs.ambient = 0.30f; rs.ambientMode = RS::AmbientSky; rs.ambientSkyTint = 0.8f;
        rs.fog = false; rs.post.fxaa = true;
        Light* sun = SunLight(scene, "Sun"); SunDefaults(sun); Aim(sun, 52.0f, 30.0f);
        sun->temperature = 5600.0f; sun->intensity = 1.15f; sun->ambient = 0.30f; sun->shadowSoftness = 0.45f;
    } else if (name == "Sunset") {
        ApplyLookPreset(rs, "Sunset");
        rs.skybox = true; rs.ambient = 0.20f; rs.ambientMode = RS::AmbientSky; rs.ambientSkyTint = 1.1f;
        Light* sun = SunLight(scene, "Sun"); SunDefaults(sun); Aim(sun, 9.0f, 215.0f);
        sun->temperature = 2700.0f; sun->intensity = 1.2f; sun->ambient = 0.20f; sun->shadowSoftness = 0.75f;
        sun->ambientColor = Color{0.85f, 0.65f, 0.75f, 1.0f};
    } else if (name == "Night with lamps") {
        ApplyLookPreset(rs, "Night");
        rs.skybox = true; rs.ambient = 0.07f; rs.ambientMode = RS::AmbientSky; rs.ambientSkyTint = 0.8f;
        Light* moon = SunLight(scene, "Moon"); SunDefaults(moon); Aim(moon, 42.0f, 200.0f);
        moon->temperature = 9500.0f; moon->intensity = 0.20f; moon->ambient = 0.07f; moon->shadowStrength = 0.6f; moon->shadowSoftness = 0.6f;
        moon->ambientColor = Color{0.6f, 0.7f, 1.0f, 1.0f};
        float r = 0.30f * (sx < sz ? sx : sz) + 1.0f; if (r < 4.0f) r = 4.0f; if (r > 8.0f) r = 8.0f;   // lamps near the middle, not at the edge of a big ground
        for (int i = 0; i < 4; ++i) {                          // four street lamps around the middle
            const float a = 0.7854f + 1.5708f * (float)i;
            const Vec3 pos{ctr.x + r * std::cos(a), ctr.y + 3.2f, ctr.z + r * std::sin(a)};
            Light* l = NewLight(scene, "Lamp " + std::to_string(i + 1), Light::Type::Point, pos, 0, 0);
            Lamp(l, 2700.0f, 1.6f, 11.0f);
            l->castShadows = i < 2;                            // two lamps get shadows (a point light uses 6 of the 16 shadow tiles)
            GameObject* bulb = scene.CreateGameObject(std::string(kPrefix) + "Lamp " + std::to_string(i + 1) + " bulb");
            bulb->transform->localPosition = pos;
            auto* mr = bulb->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Sphere(0.14f); mr->color = Color{1.0f, 0.85f, 0.55f, 1.0f};
            mr->emissive = Color{1.0f, 0.75f, 0.4f, 1.0f}; mr->castShadows = false; mr->receiveShadows = false;
        }
    } else if (name == "Indoor") {
        ApplyLookPreset(rs, "Default");
        rs.skybox = false; rs.ambient = 0.16f; rs.ambientMode = RS::AmbientHemisphere; rs.fog = false;
        rs.post.fxaa = true; rs.post.grading = true; rs.post.temperature = 0.06f;
        Light* win = SunLight(scene, "Window sun"); SunDefaults(win); Aim(win, 38.0f, 100.0f);
        win->temperature = 5200.0f; win->intensity = 0.65f; win->ambient = 0.16f; win->shadowSoftness = 0.3f;
        win->cookie = "builtin:window"; win->cookieScale = 4.5f;
        float h = 2.8f; if (hi.y - lo.y > 0.5f && hi.y - lo.y < h + 1.0f) h = hi.y - lo.y - 0.25f;
        Light* panel = NewLight(scene, "Ceiling panel", Light::Type::Area, {ctr.x, lo.y + h, ctr.z}, 90.0f, 0.0f);
        panel->areaWidth = 2.4f; panel->areaHeight = 1.6f; panel->useTemperature = true; panel->temperature = 3600.0f;
        panel->intensity = 1.3f; panel->range = 12.0f; panel->ambient = 0.0f; panel->shadowSoftness = 0.8f;
    } else if (name == "Spooky fog") {
        ApplyLookPreset(rs, "Foggy morning");
        rs.skybox = true; rs.skyMode = RS::SkyColour;
        rs.skyTop = Color{0.04f, 0.06f, 0.09f, 1.0f}; rs.skyHorizon = Color{0.16f, 0.22f, 0.24f, 1.0f}; rs.skyBottom = Color{0.05f, 0.06f, 0.07f, 1.0f};
        rs.skySun = false; rs.skyStars = false;
        rs.fog = true; rs.fogMode = RS::FogExponential; rs.fogDensity = 0.055f; rs.fogMatchSky = false;
        rs.fogColor = Color{0.17f, 0.23f, 0.25f, 1.0f};
        rs.ambient = 0.09f; rs.ambientMode = RS::AmbientHemisphere;
        rs.vignette = 0.55f; rs.post.saturation = 0.7f; rs.post.grading = true;
        Light* moon = SunLight(scene, "Moon"); SunDefaults(moon); Aim(moon, 55.0f, 150.0f);
        moon->temperature = 8000.0f; moon->intensity = 0.35f; moon->ambient = 0.09f; moon->shadowSoftness = 0.8f;
        moon->ambientColor = Color{0.6f, 0.9f, 1.0f, 1.0f};
        Light* lantern = NewLight(scene, "Lantern", Light::Type::Point, {ctr.x, lo.y + 1.8f, ctr.z}, 0, 0);
        Lamp(lantern, 2200.0f, 1.8f, 8.0f);
        lantern->castShadows = true; lantern->flicker = 0.30f; lantern->flickerSpeed = 10.0f;
    }
    return true;
}

} // namespace okay
