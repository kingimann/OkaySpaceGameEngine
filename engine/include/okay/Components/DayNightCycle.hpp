#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// A day/night cycle. Advances a 24-hour clock (a full day takes `dayLengthSeconds`
/// of real time) and drives the scene's sun and sky so it slowly turns from day to
/// dusk to night to dawn: the directional Light rotates like the sun, brightens at
/// noon and dims to a moonlit floor at night (warm at sunrise/sunset), and the main
/// camera's background blends day-blue → horizon-orange → night.
///
/// Drop it anywhere. It drives the sibling Light if there is one, otherwise the first
/// Light in the scene (your "Sun"), plus the scene's main camera for the sky. Publishes
/// the current hour to a saved value `hour` (bind a UI text with `{hour}`).
class DayNightCycle : public Behaviour {
public:
    float dayLengthSeconds = 120.0f;   // real seconds for a full 24h day
    float time = 8.0f;                 // current time of day, hours [0,24)
    bool  paused = false;

    bool  controlSun = true;           // drive the sun Light (intensity/color/ambient)
    bool  rotateSun  = true;           // rotate the sun Light's transform across the sky
    bool  controlSky = true;           // drive the main camera's background color

    float dayIntensity   = 1.2f, nightIntensity = 0.15f;
    float dayAmbient     = 0.35f, nightAmbient   = 0.08f;
    Color dayLight    = Color::FromBytes(255, 250, 235);   // midday white
    Color nightLight  = Color::FromBytes(120, 140, 200);   // cool moonlight
    Color skyDay      = Color::FromBytes(120, 175, 235);    // clear blue
    Color skyHorizon  = Color::FromBytes(240, 140,  70);    // sunrise/sunset orange
    Color skyNight    = Color::FromBytes(  8,  12,  32);    // deep night

    float Hour() const { return time; }
    bool  IsNight() const { return Elevation() < 0.0f; }
    /// -1 (midnight) .. +1 (noon): the sun's height above the horizon.
    float Elevation() const { return std::sin(2.0f * 3.14159265f * (time - 6.0f) / 24.0f); }

    void SetTime(float hour) { time = Wrap(hour); }
    void AddHours(float h)   { time = Wrap(time + h); }
    void Pause(bool on)      { paused = on; }

    void Start() override { time = Wrap(time); Apply(); }
    void Update(float dt) override {
        if (!paused && dt > 0.0f && dayLengthSeconds > 0.0f)
            time = Wrap(time + dt * (24.0f / dayLengthSeconds));
        Apply();
    }

private:
    static float Wrap(float h) { h = std::fmod(h, 24.0f); return h < 0.0f ? h + 24.0f : h; }
    Light* FindSun() const {
        if (gameObject) if (auto* l = gameObject->GetComponent<Light>()) return l;
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (s) for (const auto& go : s->Objects())
            if (go->active) if (auto* l = go->GetComponent<Light>()) return l;
        return nullptr;
    }

    void Apply() {
        float e = Elevation();                          // -1..1
        float day = Mathf::Clamp01(e / 0.25f);          // 0 below horizon .. 1 well up
        Prefs::SetFloat("hour", time);

        if (controlSun) {
            if (Light* sun = FindSun()) {
                sun->intensity = Mathf::Lerp(nightIntensity, dayIntensity, day);
                sun->useTemperature = false;
                // Warm light low on the horizon, white at noon, cool moonlight at night.
                Color warm = Color::Lerp(skyHorizon, dayLight, day);
                sun->color = e >= 0.0f ? warm : Color::Lerp(nightLight, skyHorizon, Mathf::Clamp01(1.0f + e / 0.25f));
                sun->ambient = Mathf::Lerp(nightAmbient, dayAmbient, day);
                sun->ambientColor = Color::Lerp(skyNight, skyDay, day);
                if (rotateSun && sun->gameObject && sun->gameObject->transform) {
                    // Sun arc: a full rotation about X over the day; noon points down.
                    float pitch = time / 24.0f * 360.0f - 90.0f;
                    sun->gameObject->transform->localRotation = Quat::Euler(pitch, 30.0f, 0.0f);
                }
            }
        }
        if (controlSky) {
            Scene* s = gameObject ? gameObject->scene() : nullptr;
            if (s && s->mainCamera && s->mainCamera->gameObject) {
                Color sky = e >= 0.0f ? Color::Lerp(skyHorizon, skyDay, day)
                                      : Color::Lerp(skyHorizon, skyNight, Mathf::Clamp01(-e / 0.25f));
                s->mainCamera->backgroundColor = sky;
                s->mainCamera->clearFlags = Camera::ClearFlags::SolidColor;
            }
        }
    }
};

} // namespace okay
