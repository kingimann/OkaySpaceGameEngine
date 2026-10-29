#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <string>

using namespace okay;

// Post-processing, sky, fog and weather: the scene settings round-trip through
// the serializer, old scenes load with everything off, the look presets fill the
// expected values, the weather controller drives its emitters, and the shared
// sky / fog / tonemap maths the shaders mirror behaves.

using RS = Scene::RenderSettings;
using PS = RS::PostSettings;
using WS = RS::WeatherSettings;

static GameObject* RigChild(Scene& s, const char* name) {
    GameObject* rig = s.Find(WeatherRig::kObjectName);
    if (!rig) return nullptr;
    for (const auto& up : s.Objects())
        if (up->name == name && up->transform->Parent() == rig->transform) return up.get();
    return nullptr;
}
static ParticleSystem* Emitter(Scene& s, const char* name) {
    GameObject* go = RigChild(s, name);
    return go ? go->GetComponent<ParticleSystem>() : nullptr;
}
static void Run(Scene& s, float seconds, float dt = 0.1f) {
    for (float t = 0.0f; t < seconds - 1e-4f; t += dt) s.Update(dt);
}

int main() {
    RUN_SUITE("post_sky");

    // ---- Every setting survives a save / load -----------------------------------
    {
        Scene s("look");
        auto& rs = s.renderSettings;
        rs.fog = true; rs.fogMode = RS::FogHeight; rs.fogDensity = 0.07f; rs.fogHeight = 3.5f; rs.fogFalloff = 0.4f;
        rs.fogMatchSky = true;
        rs.skyMode = RS::SkyProcedural; rs.skySunDisc = 3.25f; rs.skySunHalo = 0.8f; rs.skyHaze = 0.9f;
        rs.cloudCover = 0.65f; rs.cloudDensity = 0.45f; rs.cloudSpeed = 2.5f; rs.cloudScale = 1.75f;
        rs.vignette = 0.4f;
        auto& p = rs.post;
        p.exposure = 1.3f; p.tonemap = PS::TonemapFilmic;
        p.bloom = true; p.bloomThreshold = 0.75f; p.bloomIntensity = 0.9f; p.bloomRadius = 1.6f;
        p.grading = true; p.temperature = 0.2f; p.tint = -0.1f; p.saturation = 1.2f; p.contrast = 1.15f;
        p.lift = {0.01f, 0.02f, 0.03f}; p.gamma = {-0.04f, 0.05f, 0.06f}; p.gain = {0.07f, -0.08f, 0.09f};
        p.vignetteSmooth = 0.35f; p.fxaa = true;
        p.ssao = true; p.ssaoRadius = 0.8f; p.ssaoIntensity = 1.1f;
        p.dof = true; p.dofFocus = 14.0f; p.dofRange = 5.5f;
        auto& w = rs.weather;
        w.preset = WS::Storm; w.intensity = 0.75f; w.fade = 3.5f; w.wind = {4.0f, 0.5f, -2.0f};
        w.thunderSound = "Assets/Audio/thunder.wav";
        GameObject* go = s.CreateGameObject("Emitter");
        go->AddComponent<ParticleSystem>()->windInfluence = 0.25f;

        const std::string txt = SceneSerializer::Serialize(s);
        CHECK(txt.find("okayscene 3\n") == 0);
        CHECK(txt.find("\nfogmode ") != std::string::npos);
        CHECK(txt.find("\nskyproc ") != std::string::npos);
        CHECK(txt.find("\npost ") != std::string::npos);
        CHECK(txt.find("\nweather ") != std::string::npos);
        CHECK(txt.find("particleswind 0.25") != std::string::npos);

        Scene s2("loaded");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s2, txt, &err));
        CHECK(err.empty());
        const auto& r = s2.renderSettings;
        CHECK(r.fog); CHECK(r.fogMode == RS::FogHeight);
        CHECK_NEAR(r.fogDensity, 0.07f, 1e-5f); CHECK_NEAR(r.fogHeight, 3.5f, 1e-5f); CHECK_NEAR(r.fogFalloff, 0.4f, 1e-5f);
        CHECK(r.fogMatchSky);
        CHECK(r.skyMode == RS::SkyProcedural);
        CHECK_NEAR(r.skySunDisc, 3.25f, 1e-5f); CHECK_NEAR(r.skySunHalo, 0.8f, 1e-5f); CHECK_NEAR(r.skyHaze, 0.9f, 1e-5f);
        CHECK_NEAR(r.cloudCover, 0.65f, 1e-5f); CHECK_NEAR(r.cloudDensity, 0.45f, 1e-5f);
        CHECK_NEAR(r.cloudSpeed, 2.5f, 1e-5f); CHECK_NEAR(r.cloudScale, 1.75f, 1e-5f);
        CHECK_NEAR(r.vignette, 0.4f, 1e-5f);
        const auto& q = r.post;
        CHECK_NEAR(q.exposure, 1.3f, 1e-5f); CHECK(q.tonemap == PS::TonemapFilmic);
        CHECK(q.bloom); CHECK_NEAR(q.bloomThreshold, 0.75f, 1e-5f); CHECK_NEAR(q.bloomIntensity, 0.9f, 1e-5f);
        CHECK_NEAR(q.bloomRadius, 1.6f, 1e-5f);
        CHECK(q.grading); CHECK_NEAR(q.temperature, 0.2f, 1e-5f); CHECK_NEAR(q.tint, -0.1f, 1e-5f);
        CHECK_NEAR(q.saturation, 1.2f, 1e-5f); CHECK_NEAR(q.contrast, 1.15f, 1e-5f);
        CHECK_NEAR(q.lift.x, 0.01f, 1e-5f); CHECK_NEAR(q.lift.y, 0.02f, 1e-5f); CHECK_NEAR(q.lift.z, 0.03f, 1e-5f);
        CHECK_NEAR(q.gamma.x, -0.04f, 1e-5f); CHECK_NEAR(q.gamma.y, 0.05f, 1e-5f); CHECK_NEAR(q.gamma.z, 0.06f, 1e-5f);
        CHECK_NEAR(q.gain.x, 0.07f, 1e-5f); CHECK_NEAR(q.gain.y, -0.08f, 1e-5f); CHECK_NEAR(q.gain.z, 0.09f, 1e-5f);
        CHECK_NEAR(q.vignetteSmooth, 0.35f, 1e-5f); CHECK(q.fxaa);
        CHECK(q.ssao); CHECK_NEAR(q.ssaoRadius, 0.8f, 1e-5f); CHECK_NEAR(q.ssaoIntensity, 1.1f, 1e-5f);
        CHECK(q.dof); CHECK_NEAR(q.dofFocus, 14.0f, 1e-5f); CHECK_NEAR(q.dofRange, 5.5f, 1e-5f);
        const auto& v = r.weather;
        CHECK(v.preset == WS::Storm); CHECK_NEAR(v.intensity, 0.75f, 1e-5f); CHECK_NEAR(v.fade, 3.5f, 1e-5f);
        CHECK_NEAR(v.wind.x, 4.0f, 1e-5f); CHECK_NEAR(v.wind.y, 0.5f, 1e-5f); CHECK_NEAR(v.wind.z, -2.0f, 1e-5f);
        CHECK(v.thunderSound == "Assets/Audio/thunder.wav");
        GameObject* e2 = s2.Find("Emitter");
        CHECK(e2 && e2->GetComponent<ParticleSystem>());
        if (e2 && e2->GetComponent<ParticleSystem>()) CHECK_NEAR(e2->GetComponent<ParticleSystem>()->windInfluence, 0.25f, 1e-5f);

        // The weather record keeps an empty sound path too.
        s.renderSettings.weather.thunderSound.clear();
        Scene s3("again");
        CHECK(SceneSerializer::Deserialize(s3, SceneSerializer::Serialize(s), &err));
        CHECK(s3.renderSettings.weather.preset == WS::Storm);
        CHECK(s3.renderSettings.weather.thunderSound.empty());
    }

    // ---- A scene saved before these settings existed: everything off / default ---
    {
        const char* old =
            "okayscene 3\nname \"Old\"\ngravity 0 -9.8\n"
            "rendersettings 1 0.27 0.47 0.78 0.59 0.73 0.88 0.47 0.47 0.51 0.15\n"
            "fog 1 0.5 0.6 0.7 20 90\ntonemap 1\n"
            "gameobject 0 \"Cube\"\n  active 1\n  parent -1\nend\n";
        Scene s("old");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, old, &err));
        CHECK(err.empty());
        const RS d;
        const auto& r = s.renderSettings;
        CHECK(r.fog); CHECK(r.fogMode == RS::FogLinear); CHECK_NEAR(r.fogStart, 20.0f, 1e-4f); CHECK_NEAR(r.fogEnd, 90.0f, 1e-4f);
        CHECK(!r.fogMatchSky); CHECK_NEAR(r.fogDensity, d.fogDensity, 1e-6f);
        CHECK(r.skyMode == RS::SkyColour); CHECK_NEAR(r.cloudCover, 0.0f, 1e-6f);
        CHECK(!r.post.bloom); CHECK(!r.post.grading); CHECK(!r.post.fxaa); CHECK(!r.post.ssao); CHECK(!r.post.dof);
        CHECK_NEAR(r.post.exposure, 1.0f, 1e-6f); CHECK(r.post.tonemap == PS::TonemapACES);
        CHECK(!PostActive(r));
        CHECK(r.weather.preset == WS::None);
        // Defaults are written as nothing: the text has no record for them, so an
        // older engine still reads a scene that never touched the new settings.
        const std::string txt = SceneSerializer::Serialize(s);
        CHECK(txt.find("\npost ") == std::string::npos);
        CHECK(txt.find("\nskyproc ") == std::string::npos);
        CHECK(txt.find("\nfogmode ") == std::string::npos);
        CHECK(txt.find("\nweather ") == std::string::npos);
        CHECK(txt.find("particleswind") == std::string::npos);
    }

    // ---- Look presets --------------------------------------------------------------
    {
        int n = 0;
        const char* const* names = LookPresetNames(n);
        CHECK(n == 8);
        for (int i = 0; i < n; ++i) { RS rs; CHECK(ApplyLookPreset(rs, names[i])); }
        RS rs;
        CHECK(!ApplyLookPreset(rs, "No such look"));

        float sunDeg = -1000.0f;
        CHECK(ApplyLookPreset(rs, "Cinematic", &sunDeg));
        CHECK(rs.post.tonemap == PS::TonemapFilmic); CHECK(rs.post.bloom); CHECK(rs.post.grading); CHECK(rs.post.fxaa);
        CHECK(rs.vignette > 0.3f); CHECK(rs.post.saturation < 1.0f); CHECK(PostActive(rs));
        CHECK(sunDeg == -1000.0f);                       // keeps the sun where it is
        CHECK(rs.skyMode == RS::SkyColour); CHECK(rs.weather.preset == WS::None);

        CHECK(ApplyLookPreset(rs, "Vivid"));
        CHECK(rs.post.saturation > 1.2f); CHECK(rs.post.exposure > 1.0f); CHECK(rs.post.tonemap == PS::TonemapACES);

        CHECK(ApplyLookPreset(rs, "Noir"));
        CHECK_NEAR(rs.post.saturation, 0.0f, 1e-6f); CHECK(rs.post.contrast > 1.2f); CHECK(rs.vignette > 0.5f);

        CHECK(ApplyLookPreset(rs, "Sunset", &sunDeg));
        CHECK(rs.skyMode == RS::SkyProcedural); CHECK(rs.fog); CHECK(rs.fogMode == RS::FogExponential); CHECK(rs.fogMatchSky);
        CHECK(rs.post.temperature > 0.1f); CHECK(sunDeg > 0.0f && sunDeg < 15.0f);

        CHECK(ApplyLookPreset(rs, "Night", &sunDeg));
        CHECK(rs.skyMode == RS::SkyProcedural); CHECK(sunDeg < 0.0f); CHECK(rs.post.exposure < 1.0f);
        CHECK(rs.skyStarDensity > 0.7f); CHECK(rs.post.temperature < 0.0f);

        CHECK(ApplyLookPreset(rs, "Foggy morning", &sunDeg));
        CHECK(rs.fog && rs.fogMode == RS::FogHeight); CHECK(rs.fogHeight > 0.0f); CHECK(rs.cloudCover > 0.5f);
        CHECK(sunDeg > 0.0f && sunDeg < 20.0f);

        CHECK(ApplyLookPreset(rs, "Storm", &sunDeg));
        CHECK(rs.weather.preset == WS::Storm); CHECK(rs.weather.intensity > 0.7f);
        CHECK(rs.weather.wind.SqrMagnitude() > 1.0f); CHECK(rs.cloudCover > 0.9f); CHECK(rs.fog);

        // Default / Reset: the new settings go back to their defaults, the sky
        // gradient colours are left alone.
        rs.skyTop = Color::FromBytes(1, 2, 3);
        CHECK(ApplyLookPreset(rs, "Default"));
        CHECK(!PostActive(rs)); CHECK(rs.skyMode == RS::SkyColour); CHECK(!rs.fog);
        CHECK(rs.weather.preset == WS::None); CHECK_NEAR(rs.vignette, 0.0f, 1e-6f);
        CHECK_NEAR(rs.skyTop.r, 1.0f / 255.0f, 1e-4f);
        rs.post.bloom = true; rs.weather.preset = WS::Rain;
        ResetLook(rs);
        CHECK(!rs.post.bloom); CHECK(rs.weather.preset == WS::None);
    }

    // ---- Setting values by name (post_set / PostProcess.Set) -----------------------
    {
        RS rs;
        CHECK(PostSet(rs, "bloom", 1)); CHECK(rs.post.bloom);
        CHECK(PostSet(rs, "bloom_threshold", 0.5f)); CHECK_NEAR(rs.post.bloomThreshold, 0.5f, 1e-6f);
        CHECK(PostSet(rs, "exposure", 1.8f)); CHECK_NEAR(rs.post.exposure, 1.8f, 1e-6f);
        CHECK(PostSet(rs, "tonemap", 2)); CHECK(rs.post.tonemap == PS::TonemapFilmic);
        CHECK(PostSet(rs, "saturation", 0.3f)); CHECK_NEAR(rs.post.saturation, 0.3f, 1e-6f);
        CHECK(PostSet(rs, "vignette", 0.7f)); CHECK_NEAR(rs.vignette, 0.7f, 1e-6f);
        CHECK(PostSet(rs, "fxaa", 1)); CHECK(rs.post.fxaa);
        CHECK(PostSet(rs, "dof_focus", 25.0f)); CHECK_NEAR(rs.post.dofFocus, 25.0f, 1e-6f);
        CHECK(!PostSet(rs, "sharpen", 1));
        CHECK(PostSet(rs, "exposure", 100.0f)); CHECK(rs.post.exposure <= 8.0f);   // clamped
        CHECK(TonemapFromName("filmic") == PS::TonemapFilmic);
        CHECK(TonemapFromName("none") == PS::TonemapNone);
        CHECK(FogModeFromName("height") == RS::FogHeight);
        CHECK(WeatherPresetFromName("snow") == WS::Snow);
        CHECK(WeatherPresetFromName("whatever") == WS::None);
        CHECK(std::string(WeatherPresetName(WS::Storm)) == "storm");
    }

    // ---- Sun-driven sky colours ------------------------------------------------------
    {
        const SkyPalette day = SkyPaletteFor(0.9f);
        CHECK(day.zenith.z > day.zenith.x * 2.0f);                      // a blue sky
        CHECK(day.zenith.z > 0.6f);
        CHECK(day.night < 0.01f); CHECK(day.sunDisc > 0.99f);
        const SkyPalette dusk = SkyPaletteFor(0.0f);
        CHECK(dusk.horizon.x > dusk.horizon.z * 2.0f);                  // an orange horizon
        CHECK(dusk.sun.x > dusk.sun.z);                                 // a warm sun
        CHECK(dusk.zenith.z > dusk.zenith.x);                           // the top still bluish
        const SkyPalette night = SkyPaletteFor(-0.5f);
        CHECK(night.zenith.x + night.zenith.y + night.zenith.z < 0.1f); // dark
        CHECK(night.horizon.x + night.horizon.y + night.horizon.z < 0.25f);
        CHECK(night.night > 0.99f); CHECK(night.sunDisc < 0.01f);
        // Continuous: a small step in sun height moves the colours a little.
        const SkyPalette a = SkyPaletteFor(0.10f), b = SkyPaletteFor(0.11f);
        CHECK(std::fabs(a.horizon.x - b.horizon.x) < 0.05f);
        // The gradient colours a renderer uses follow the mode.
        RS rs; rs.skyTop = Color::FromBytes(10, 20, 30);
        Vec3 t, h, g;
        EffectiveSkyColors(rs, t, h, g);
        CHECK_NEAR(t.x, 10.0f / 255.0f, 1e-4f);
        rs.skyMode = RS::SkyProcedural;
        SceneLight::SetDirection(Vec3{0.0f, -1.0f, 0.0f});              // noon: the light shines straight down
        EffectiveSkyColors(rs, t, h, g);
        CHECK(t.z > 0.6f);
        SceneLight::Reset();
    }

    // ---- Fog factor per mode --------------------------------------------------------
    {
        RS rs;
        CHECK_NEAR(FogFactor(rs, 50.0f, 0.0f, 0.0f), 0.0f, 1e-6f);       // off
        rs.fog = true; rs.fogMode = RS::FogLinear; rs.fogStart = 10.0f; rs.fogEnd = 30.0f;
        CHECK_NEAR(FogFactor(rs, 5.0f, 0, 0), 0.0f, 1e-6f);
        CHECK_NEAR(FogFactor(rs, 20.0f, 0, 0), 0.5f, 1e-6f);
        CHECK_NEAR(FogFactor(rs, 40.0f, 0, 0), 1.0f, 1e-6f);
        rs.fogEnd = rs.fogStart; CHECK(!FogActive(rs));                   // a degenerate range is no fog
        rs.fogMode = RS::FogExponential; rs.fogDensity = 0.05f;
        CHECK(FogActive(rs));
        CHECK_NEAR(FogFactor(rs, 0.0f, 0, 0), 0.0f, 1e-6f);
        CHECK_NEAR(FogFactor(rs, 20.0f, 0, 0), 1.0f - std::exp(-1.0f), 1e-5f);
        CHECK(FogFactor(rs, 200.0f, 0, 0) > 0.99f);
        CHECK(FogFactor(rs, 10.0f, 0, 0) < FogFactor(rs, 11.0f, 0, 0));  // monotonic
        rs.fogDensity = 0.0f; CHECK(!FogActive(rs));
        rs.fogMode = RS::FogHeight; rs.fogDensity = 0.05f; rs.fogHeight = 2.0f; rs.fogFalloff = 0.5f;
        const float low = FogFactor(rs, 20.0f, 0.0f, 0.0f);              // both ends in the layer
        const float high = FogFactor(rs, 20.0f, 20.0f, 20.0f);           // both ends far above it
        const float mixed = FogFactor(rs, 20.0f, 0.0f, 20.0f);
        CHECK_NEAR(low, 1.0f - std::exp(-1.0f), 1e-5f);
        CHECK(high < 0.01f);
        CHECK(mixed > high && mixed < low);
        // The colour: authored, or the sky's horizon when matching the sky.
        rs.fogColor = Color::FromBytes(255, 0, 0);
        CHECK_NEAR(EffectiveFogColor(rs).x, 1.0f, 1e-5f);
        rs.fogMatchSky = true;
        CHECK_NEAR(EffectiveFogColor(rs).x, rs.skyHorizon.r, 1e-5f);
    }

    // ---- Tonemap / exposure maths (mirrored in the shaders) -------------------------
    {
        CHECK_NEAR(TonemapACES(0.0f), 0.0f, 1e-6f);
        CHECK(TonemapACES(1.0f) > 0.75f && TonemapACES(1.0f) < 0.85f);
        CHECK(TonemapACES(10.0f) > 0.98f && TonemapACES(10.0f) <= 1.0f);
        CHECK(TonemapACES(0.2f) < TonemapACES(0.4f));
        CHECK_NEAR(TonemapFilmic(0.0f), 0.0f, 1e-4f);
        CHECK(TonemapFilmic(0.5f) < TonemapFilmic(1.0f));
        CHECK(TonemapFilmic(100.0f) <= 1.0f && TonemapFilmic(100.0f) > 0.95f);
        CHECK(TonemapFilmic(1.0f) < TonemapACES(1.0f));                   // the filmic shoulder is softer
        const Vec3 none = TonemapColor({2.0f, 0.5f, -1.0f}, PS::TonemapNone);
        CHECK_NEAR(none.x, 1.0f, 1e-6f); CHECK_NEAR(none.y, 0.5f, 1e-6f); CHECK_NEAR(none.z, 0.0f, 1e-6f);
        CHECK_NEAR(TonemapColor({0.3f, 0.3f, 0.3f}, PS::TonemapACES).x, TonemapACES(0.3f), 1e-6f);
        RS rs;
        CHECK_NEAR(ExposureScale(rs), 1.0f, 1e-6f);
        rs.post.exposure = 1.5f; CHECK_NEAR(ExposureScale(rs), 1.5f, 1e-6f);
        LightningFlash() = 1.0f; CHECK_NEAR(ExposureScale(rs), 6.0f, 1e-5f);   // a strike blows the frame out
        rs.post.exposure = 1.0f; CHECK(PostActive(rs));                           // ...so the chain runs for it
        LightningFlash() = 0.0f; CHECK(!PostActive(rs));
        rs.tonemap = true; rs.post.tonemap = PS::TonemapNone; CHECK(PostActive(rs));
        rs.tonemap = false; CHECK(!PostActive(rs));                                // no curve on the legacy pipeline
    }

    // ---- The weather controller -------------------------------------------------------
    {
        Scene s("weather");
        GameObject* camObj = s.CreateGameObject("Cam");
        camObj->AddComponent<Camera>()->main = true;
        camObj->transform->localPosition = {10.0f, 2.0f, -5.0f};
        s.Start();
        s.Update(0.1f);
        CHECK(s.Find(WeatherRig::kObjectName) == nullptr);                // none: no rig at all
        CHECK(WindVector().SqrMagnitude() < 1e-8f);

        auto& w = s.renderSettings.weather;
        w.preset = WS::Rain; w.intensity = 1.0f; w.fade = 1.0f;
        Run(s, 1.5f);
        GameObject* rig = s.Find(WeatherRig::kObjectName);
        CHECK(rig != nullptr);
        CHECK(rig && rig->transient);                                        // never saved
        CHECK(SceneSerializer::Serialize(s).find(WeatherRig::kObjectName) == std::string::npos);
        ParticleSystem* rain = Emitter(s, "Rain");
        CHECK(rain != nullptr);
        CHECK(Emitter(s, "Snow") == nullptr);
        if (rain) {
            CHECK_NEAR(rain->emissionRate, 450.0f, 1e-3f);                 // full intensity after the fade
            CHECK(rain->AliveCount() > 100);
            CHECK(rain->renderMode == ParticleSystem::RenderMode::Stretch);
            CHECK(rain->startVelocity.y < -5.0f);
        }
        // Follows the camera.
        if (rig) {
            CHECK_NEAR(rig->transform->Position().x, 10.0f, 1e-4f);
            CHECK_NEAR(rig->transform->Position().z, -5.0f, 1e-4f);
            camObj->transform->localPosition = {40.0f, 2.0f, 8.0f};
            s.Update(0.1f);
            CHECK_NEAR(rig->transform->Position().x, 40.0f, 1e-4f);
            GameObject* rainObj = RigChild(s, "Rain");
            CHECK(rainObj && rainObj->transform->Position().y > camObj->transform->Position().y + 5.0f);   // the box sits above
        }
        // Intensity scales emission.
        w.intensity = 0.4f; s.Update(0.1f);
        if (rain) CHECK_NEAR(rain->emissionRate, 450.0f * 0.4f, 1e-3f);
        w.intensity = 1.0f;

        // Rain -> snow cross-fades: half way, both run at half rate.
        w.preset = WS::Snow; w.fade = 2.0f;
        Run(s, 1.0f);
        ParticleSystem* snow = Emitter(s, "Snow");
        CHECK(snow != nullptr);
        if (rain && snow) {
            CHECK(rain->emissionRate > 450.0f * 0.4f && rain->emissionRate < 450.0f * 0.6f);
            CHECK(snow->emissionRate > 140.0f * 0.4f && snow->emissionRate < 140.0f * 0.6f);
            CHECK(snow->startVelocity.y > -2.0f);                           // drifting flakes, not streaks
        }
        Run(s, 1.5f);
        if (snow) CHECK_NEAR(snow->emissionRate, 140.0f, 1e-3f);
        if (rain) CHECK_NEAR(rain->emissionRate, 0.0f, 1e-6f);
        Run(s, 3.0f);                                                        // the last drops land
        CHECK(Emitter(s, "Rain") == nullptr);                                // the idle emitter is dropped
        CHECK(Emitter(s, "Snow") != nullptr);

        // Storm: heavier rain, gusts on top of the wind, lightning.
        w.preset = WS::Storm; w.fade = 0.5f; w.wind = {3.0f, 0.0f, 0.0f};
        LightningFlash() = 0.0f;
        bool flashed = false; float peakWind = 0.0f;
        for (int i = 0; i < 150; ++i) {
            s.Update(0.1f);
            if (LightningFlash() > 0.5f) flashed = true;
            peakWind = std::fmax(peakWind, WindVector().x);
        }
        CHECK(flashed);
        CHECK(peakWind > 3.5f);                                              // a gust beyond the base wind
        CHECK(WindVector().x >= 3.0f - 1e-4f);
        rain = Emitter(s, "Rain");
        CHECK(rain != nullptr);
        if (rain) CHECK_NEAR(rain->emissionRate, 700.0f, 1e-3f);
        CHECK(Emitter(s, "Snow") == nullptr);                                // the snow has fallen and gone

        // Back to none: the rig fades out and disappears once the rain has landed.
        w.preset = WS::None; w.fade = 0.5f;
        Run(s, 4.0f);
        CHECK(s.Find(WeatherRig::kObjectName) == nullptr);
        CHECK(Emitter(s, "Rain") == nullptr);
        CHECK_NEAR(WindVector().x, 3.0f, 1e-5f);                             // the scene's wind stays
        w.wind = {0, 0, 0}; s.Update(0.1f);
        CHECK(WindVector().SqrMagnitude() < 1e-8f);
        LightningFlash() = 0.0f;
    }

    // ---- Wind moves particles; a still scene leaves them alone -----------------------
    {
        Scene s("wind");
        GameObject* go = s.CreateGameObject("Puff");
        auto* ps = go->AddComponent<ParticleSystem>();
        ps->startVelocity = {0, 0, 0}; ps->velocityRandom = 0.0f; ps->gravityModifier = 0.0f;
        ps->startLifetime = 10.0f; ps->emissionRate = 0.0f;
        s.Start(); s.Update(0.0f);
        ps->Emit(1);
        WindVector() = {0, 0, 0};
        for (int i = 0; i < 20; ++i) s.Update(0.05f);
        const auto& still = ps->Particles();
        float sx = 0.0f; for (const auto& p : still) if (p.alive) sx = p.position.x;
        CHECK_NEAR(sx, 0.0f, 1e-5f);
        s.renderSettings.weather.wind = {5.0f, 0.0f, 0.0f};                 // the scene's wind feeds WindVector()
        for (int i = 0; i < 20; ++i) s.Update(0.05f);
        CHECK_NEAR(WindVector().x, 5.0f, 1e-5f);
        float wx = 0.0f, wvx = 0.0f; for (const auto& p : ps->Particles()) if (p.alive) { wx = p.position.x; wvx = p.velocity.x; }
        CHECK(wx > 0.5f);
        CHECK(wvx > 1.0f && wvx <= 5.0f + 1e-4f);                            // relaxes toward the wind, never past it
        // A system that ignores the wind keeps its own motion.
        ps->windInfluence = 0.0f;
        for (const auto& p : ps->Particles()) if (p.alive) const_cast<ParticleSystem::Particle&>(p).velocity = {0, 0, 0};
        for (int i = 0; i < 10; ++i) s.Update(0.05f);
        float ivx = 1.0f; for (const auto& p : ps->Particles()) if (p.alive) ivx = p.velocity.x;
        CHECK_NEAR(ivx, 0.0f, 1e-5f);
        s.renderSettings.weather.wind = {0, 0, 0};
        s.Update(0.05f);
        LightningFlash() = 0.0f;
    }

    // ---- A procedural sky leaves the camera background to the sky itself ------------
    {
        Scene s("dn");
        GameObject* camObj = s.CreateGameObject("Cam");
        auto* cam = camObj->AddComponent<Camera>();
        s.CreateGameObject("Sun")->AddComponent<Light>();
        auto* dn = s.CreateGameObject("Time")->AddComponent<DayNightCycle>();
        dn->paused = true;
        s.renderSettings.skyMode = RS::SkyProcedural;
        s.Start(); dn->SetTime(12.0f); s.Update(0.0f);
        CHECK(cam->clearFlags == Camera::ClearFlags::Skybox);
        s.renderSettings.skyMode = RS::SkyColour;
        s.Update(0.0f);
        CHECK(cam->clearFlags == Camera::ClearFlags::SolidColor);
    }

    TEST_MAIN_RESULT();
}
