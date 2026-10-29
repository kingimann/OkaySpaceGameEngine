#pragma once
// ---------------------------------------------------------------------------
// Weather: rain / snow / storm presets built from the particle system, following
// the active camera so the player is always under it. UpdateWeather runs once
// per frame (Scene::Update while playing; the editor ticks it in edit mode for
// the live preview) and keeps a transient "Weather (runtime)" object in step with
// the scene's RenderSettings::weather: emitters are created on demand, emission
// scales with the intensity, a preset change cross-fades over `fade` seconds and
// the rig is removed again once "none" has faded out. A storm adds wind gusts
// and lightning (a brief exposure flash, see LightningFlash, plus an optional
// thunder clip).
// ---------------------------------------------------------------------------
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Audio/AudioLoad.hpp"
#include "okay/Render/Atmosphere.hpp"
#include "okay/Core/Random.hpp"
#include <cmath>
#include <string>

namespace okay {

/// How many weather rigs are alive across all scenes, so a scene with no weather
/// pays nothing per frame (UpdateWeather only searches for a rig while one exists).
inline int& WeatherRigCount() { static int n = 0; return n; }

/// Runtime state of the weather rig (never saved: its object is transient).
class WeatherRig : public Behaviour {
public:
    static constexpr const char* kObjectName = "Weather (runtime)";
    int   current = 0;            // the preset the rig is fading toward
    int   previous = 0;           // the one it is fading from
    float blend = 1.0f;           // 0 = previous .. 1 = current
    float strikeIn = 4.0f;        // seconds until the next lightning strike (storm)
    float thunderIn = -1.0f;      // seconds until the thunder clip plays (-1 = none pending)
    float gustPhase = 0.0f;
    Vec3  gust{0, 0, 0};          // storm gust added to the scene's wind
    GameObject* rainObj = nullptr;
    GameObject* snowObj = nullptr;
    AudioSource* audio = nullptr;
    std::string loadedClip;
    Random rng;
    void Awake() override { rng.Seed(0x5eedu); }
    void OnDestroy() override { if (WeatherRigCount() > 0) --WeatherRigCount(); }
};

namespace weather_detail {
using W = Scene::RenderSettings::WeatherSettings;

/// Particles per second each emitter spawns at full intensity for a preset.
inline float RainRate(int preset) { return preset == W::Rain ? 450.0f : preset == W::Storm ? 700.0f : 0.0f; }
inline float SnowRate(int preset) { return preset == W::Snow ? 140.0f : 0.0f; }

inline GameObject* MakeEmitter(Scene& scene, GameObject* rig, bool rain) {
    GameObject* go = scene.CreateGameObject(rain ? "Rain" : "Snow");
    go->transient = true;
    go->transform->SetParent(rig->transform, false);
    auto* ps = go->AddComponent<ParticleSystem>();
    ps->ApplyPreset(rain ? ParticleSystem::Preset::Rain : ParticleSystem::Preset::Snow);
    ps->boxSize = {30.0f, 0.5f, 30.0f};
    if (rain) {
        ps->maxParticles = 3000; ps->startLifetime = 1.8f; ps->startLifetimeRandom = 0.2f;
        ps->startVelocity = {0.0f, -11.0f, 0.0f}; ps->startSize = 0.05f;
        go->transform->localPosition = {0.0f, 12.0f, 0.0f};
    } else {
        // The stock Snow look hovers (it is meant for an emitter near the ground);
        // from above the camera the flakes need to keep falling: light gravity
        // against the drag settles them at about 2 m/s.
        ps->maxParticles = 1800; ps->startLifetime = 8.0f; ps->startLifetimeRandom = 2.0f;
        ps->startVelocity = {0.0f, -1.0f, 0.0f};
        ps->gravity = {0.0f, -1.0f, 0.0f}; ps->gravityModifier = 1.0f; ps->damping = 0.5f;
        go->transform->localPosition = {0.0f, 6.0f, 0.0f};
    }
    ps->emissionRate = 0.0f;
    ps->playing = true;
    return go;
}

/// Set an emitter's rate, creating it when needed and dropping it once it is
/// idle and empty, so a finished fade leaves nothing behind.
inline void DriveEmitter(Scene& scene, GameObject* rig, GameObject*& obj, bool rain, float rate) {
    if (rate > 0.0f && !obj) obj = MakeEmitter(scene, rig, rain);
    if (!obj) return;
    auto* ps = obj->GetComponent<ParticleSystem>();
    if (!ps) { obj = nullptr; return; }
    ps->emissionRate = rate;
    if (rate <= 0.0f && ps->AliveCount() == 0) { scene.Destroy(obj); obj = nullptr; }
}
} // namespace weather_detail

/// Advance the scene's weather by `dt` seconds. `follow` overrides the point the
/// rig stays over (the editor's Scene camera); otherwise the main camera is used.
inline void UpdateWeather(Scene& scene, float dt, const Vec3* follow = nullptr) {
    using namespace weather_detail;
    const auto& ws = scene.renderSettings.weather;
    if (dt < 0.0f) dt = 0.0f;
    // The flash fades over a few frames whatever the weather does next.
    float& flash = LightningFlash();
    if (flash > 0.0f) { flash -= dt * 6.0f; if (flash < 0.0f) flash = 0.0f; }

    // The common case (no weather anywhere) costs no search at all.
    if (ws.preset == W::None && WeatherRigCount() <= 0) { WindVector() = ws.wind; return; }
    GameObject* rigObj = scene.Find(WeatherRig::kObjectName);
    WeatherRig* rig = rigObj ? rigObj->GetComponent<WeatherRig>() : nullptr;
    if (!rig) {
        WindVector() = ws.wind;
        if (ws.preset == W::None) return;
        rigObj = scene.CreateGameObject(WeatherRig::kObjectName);
        rigObj->transient = true;
        rig = rigObj->AddComponent<WeatherRig>();
        rig->rng.Seed(0x5eedu);
        rig->previous = W::None; rig->current = ws.preset; rig->blend = 0.0f;
        ++WeatherRigCount();
    }
    if (rig->current != ws.preset) { rig->previous = rig->current; rig->current = ws.preset; rig->blend = 0.0f; }
    const float fade = ws.fade > 0.01f ? ws.fade : 0.01f;
    rig->blend += dt / fade; if (rig->blend > 1.0f) rig->blend = 1.0f;
    const float intensity = Mathf::Clamp01(ws.intensity);
    const float k = rig->blend;

    // Stay over the camera (x/z and height), so the rain never runs out from under the player.
    Vec3 at = rigObj->transform->Position();
    if (follow) at = *follow;
    else if (scene.mainCamera && scene.mainCamera->gameObject && scene.mainCamera->gameObject->transform)
        at = scene.mainCamera->gameObject->transform->Position();
    rigObj->transform->localPosition = at;

    const float rainRate = (RainRate(rig->previous) * (1.0f - k) + RainRate(rig->current) * k) * intensity;
    const float snowRate = (SnowRate(rig->previous) * (1.0f - k) + SnowRate(rig->current) * k) * intensity;
    DriveEmitter(scene, rigObj, rig->rainObj, true, rainRate);
    DriveEmitter(scene, rigObj, rig->snowObj, false, snowRate);

    // Storm: gusty wind and lightning. The gust is a slow swell along the wind
    // (or a default direction when the scene's wind is zero).
    const float storm = (rig->current == W::Storm ? k : 0.0f) + (rig->previous == W::Storm ? 1.0f - k : 0.0f);
    if (storm > 0.0f) {
        rig->gustPhase += dt * 0.7f;
        Vec3 dir = ws.wind.SqrMagnitude() > 1e-6f ? ws.wind.Normalized() : Vec3{1.0f, 0.0f, 0.3f}.Normalized();
        const float swell = 0.5f + 0.5f * std::sin(rig->gustPhase) * std::sin(rig->gustPhase * 2.3f + 1.0f);
        rig->gust = dir * (swell * 4.0f * intensity * storm);
        if (rig->current == W::Storm) {
            rig->strikeIn -= dt;
            if (rig->strikeIn <= 0.0f) {
                flash = 1.0f;
                rig->strikeIn = rig->rng.Range(3.0f, 10.0f) / (0.25f + intensity);
                rig->thunderIn = rig->rng.Range(0.3f, 1.5f);
            }
        }
    } else {
        rig->gust = {0, 0, 0};
    }
    if (rig->thunderIn >= 0.0f) {
        rig->thunderIn -= dt;
        if (rig->thunderIn < 0.0f) {
            rig->thunderIn = -1.0f;
            if (!ws.thunderSound.empty()) {
                if (!rig->audio) { rig->audio = rigObj->AddComponent<AudioSource>(); rig->audio->group = AudioGroup::SFX; }
                if (rig->loadedClip != ws.thunderSound) {
                    if (auto clip = LoadAudioFile(ws.thunderSound)) { rig->audio->clip = *clip; rig->loadedClip = ws.thunderSound; }
                }
                if (!rig->audio->clip.samples.empty()) rig->audio->PlayOneShot(0.6f + 0.4f * intensity);
            }
        }
    }
    WindVector() = ws.wind + rig->gust;

    // Faded out to "none" with nothing left falling: take the rig down.
    if (ws.preset == W::None && rig->blend >= 1.0f && !rig->rainObj && !rig->snowObj && rig->thunderIn < 0.0f) {
        WindVector() = ws.wind;
        scene.Destroy(rigObj);
    }
}

} // namespace okay
