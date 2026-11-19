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
#include <functional>
#include <cstdint>
#include <string>

namespace okay {

/// A day/night cycle. Advances a 24-hour clock (a full day takes `dayLengthSeconds`
/// of real time) and drives the scene's sun and sky so it slowly turns from day to
/// dusk to night to dawn: the directional Light rotates like the sun, brightens at
/// noon and dims to a moonlit floor at night (warm at sunrise/sunset), and the main
/// camera's background blends day-blue → horizon-orange → night.
///
/// It is also the game's calendar and clock: a day counter, four seasons, a "08:30" text for a UI,
/// a speed multiplier, and events a script can listen for (see "Events" below). The hours are kept in a
/// double inside, so a day stays exact however long the game runs and at any frame rate.
///
/// Drop it anywhere. It drives the sibling Light if there is one, otherwise the first
/// Light in the scene (your "Sun"), plus the scene's main camera for the sky. Publishes
/// the current hour to a saved value `hour` (bind a UI text with `{hour}`).
class DayNightCycle : public Behaviour {
public:
    float dayLengthSeconds = 120.0f;   // real seconds for a full 24h day
    float time = 8.0f;                 // current time of day, hours [0,24)
    bool  paused = false;

    // ---- Calendar, speed, events ----------------------------------------------------------------
    int   day = 1;                     // the day counter: 1 on the first day, +1 every midnight
    int   daysPerSeason = 30;          // a season lasts this many days (Spring, Summer, Autumn, Winter)
    float speed = 1.0f;                // 1 = normal, 2 = twice as fast, 0 = the day stands still
    float sunriseHour = 6.0f;          // OnSunrise fires when the clock passes this hour
    float sunsetHour  = 18.0f;         // OnSunset fires when the clock passes this hour
    /// "" = follow the scene's game time (it stops when the game is paused or slowed). A name = follow that
    /// Clock channel instead: "ui" keeps the days moving while the game is paused.
    std::string clockName;
    /// Events. Each is delivered to every object in the scene as a named event ("OnHour", "OnDay",
    /// "OnSunrise", "OnSunset", "OnSeason": a Visual Script On Custom Event node, a script's
    /// OnEvent_OnHour(value), an Actions "On Message") and also called here for C++ code. The value is the
    /// hour (0-23), the day number, 0 and the season (0 Spring, 1 Summer, 2 Autumn, 3 Winter).
    /// A jump (SetTime) fires nothing; time that passes, AddHours forward and SkipToHour fire everything
    /// they cross, in order, even when one frame crosses several.
    std::function<void(int)> onHour, onDay, onSeason;
    std::function<void()> onSunrise, onSunset;

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

    /// Jump to an hour of the day (no events fire, the day counter stays).
    void SetTime(float hour) { time = Wrap(hour); SyncHours(); }
    /// Move the clock on by `h` hours: forward fires every event it passes; backward just jumps.
    void AddHours(float h);
    /// Run the clock forward to the next time it is `hour` (a full day if it already is), firing the
    /// events on the way: "sleep until morning" is SkipToHour(6).
    void SkipToHour(float hour);
    void Pause(bool on)      { paused = on; }

    int   Minute() const { return (int)std::floor((m_hours - std::floor(m_hours)) * 60.0 + 1e-9) % 60; }
    int   HourOfDay() const { return (int)std::floor(m_hours + 1e-9) % 24; }
    /// 0 Spring, 1 Summer, 2 Autumn, 3 Winter.
    int   Season() const { return SeasonOfDay(day, daysPerSeason); }
    static int SeasonOfDay(int day, int daysPerSeason) {
        const int per = daysPerSeason < 1 ? 1 : daysPerSeason;
        const int d = day < 1 ? 0 : day - 1;
        return (d / per) % 4;
    }
    const char* SeasonName() const { static const char* n[4] = {"Spring", "Summer", "Autumn", "Winter"}; return n[Season()]; }
    /// "08:30" (24 hour). The same text is stored for a UI as the saved value `clock`.
    std::string ClockText() const;

    void Start() override { time = Wrap(time); SyncHours(); Apply(); }
    void Update(float dt) override {
        SyncHours();
        Advance(dt);
        Apply();
    }

private:
    /// Out of line (DayNightCycle.cpp): the exact clock step and the events.
    void Advance(float dt);
    void MoveForward(double hours);
    void SyncHours() { if (!m_synced || (float)m_hours != time) { m_hours = (double)Wrap(time); m_synced = true; } }
    double m_hours = 8.0;              // the exact time of day (time is its float view)
    bool   m_synced = false;
    std::uint64_t m_ticks = 0; double m_elapsed = 0.0; bool m_primed = false;   // follows clockName (a ClockCursor, kept as plain numbers)
    int    m_lastMinute = -1;
    static float Wrap(float h) { h = std::fmod(h, 24.0f); return h < 0.0f ? h + 24.0f : h; }
    void PublishCalendar();   // DayNightCycle.cpp
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
        PublishCalendar();

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
            // A Procedural sky already follows the sun (it reads the Light this
            // cycle rotates), so the camera's background is left to the sky then.
            if (s && s->renderSettings.skyMode == Scene::RenderSettings::SkyProcedural) return;
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
