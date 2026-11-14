// Spatial effects: a Schroeder reverb (impulse response and presets), doppler pitch
// ratio, occlusion level, and a distance low-pass curve. Scene tests run them
// through the real mixer, including a stereo clip, a stream, and the demo scene.
// Nothing here writes into the source tree.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

void Reset() {
    AudioMixer::sampleRate = 44100;
    AudioMixer::masterVolume = 1.0f;
    AudioMixer::muted = false;
    AudioMixer::focusMuted = false;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        AudioMixer::groupVolume[i] = 1.0f;
        AudioMixer::groupMuted[i] = false;
    }
    AudioMixer::SetListener(Vec3::Zero);
    AudioMixer::ResetSpatial();
}

double AbsSum(const std::vector<float>& s, int from, int to) {
    double a = 0.0;
    if (from < 0) from = 0;
    if (to > (int)s.size()) to = (int)s.size();
    for (int i = from; i < to; ++i) a += std::fabs(s[(std::size_t)i]);
    return a;
}

double Rms(const std::vector<float>& s) {
    if (s.empty()) return 0.0;
    double a = 0.0;
    for (float v : s) a += (double)v * (double)v;
    return std::sqrt(a / (double)s.size());
}

bool AllFinite(const std::vector<float>& s) {
    for (float v : s) if (!std::isfinite(v)) return false;
    return true;
}

std::vector<float> Render(Scene& scene, int frames) {
    std::vector<float> out((std::size_t)frames * 2);
    AudioMixer::RenderStereo(scene, out.data(), frames);
    return out;
}

double TailOf(ReverbPreset preset) {
    SchroederReverb rev;
    rev.Prepare(44100);
    rev.Configure(PresetSettings(preset));
    rev.BeginBlock();
    double early = 0.0, late = 0.0;
    const int n = 30000;
    for (int i = 0; i < n; ++i) {
        float l = 0.0f, r = 0.0f;
        rev.Process(i == 0 ? 1.0f : 0.0f, l, r);
        const double e = std::fabs(l) + std::fabs(r);
        if (i >= 1200 && i < 4000) early += e;
        if (i >= 20000 && i < 28000) late += e;
        if (!std::isfinite(l) || !std::isfinite(r)) return -1.0;
    }
    return late / (early + 1.0e-12);
}

void Put16(std::vector<unsigned char>& v, unsigned x) {
    v.push_back((unsigned char)(x & 255));
    v.push_back((unsigned char)((x >> 8) & 255));
}
void Put32(std::vector<unsigned char>& v, unsigned x) {
    for (int i = 0; i < 4; ++i) v.push_back((unsigned char)((x >> (8 * i)) & 255));
}

// A 16-bit PCM wav in memory, written to a temp file (never the source tree).
std::string WriteWav(const fs::path& path, const std::vector<float>& interleaved, int channels, int rate) {
    std::vector<unsigned char> data;
    for (float s : interleaved) {
        float c = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
        int q = (int)std::lround(c * 32767.0f);
        if (q < -32768) q = -32768;
        if (q > 32767) q = 32767;
        Put16(data, (unsigned)(q & 0xFFFF));
    }
    std::vector<unsigned char> wav;
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    Put32(wav, 36 + (unsigned)data.size());
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    Put32(wav, 16);
    Put16(wav, 1);
    Put16(wav, (unsigned)channels);
    Put32(wav, (unsigned)rate);
    Put32(wav, (unsigned)(rate * channels * 2));
    Put16(wav, (unsigned)(channels * 2));
    Put16(wav, 16);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    Put32(wav, (unsigned)data.size());
    wav.insert(wav.end(), data.begin(), data.end());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(wav.data()), (std::streamsize)wav.size());
    return path.string();
}

} // namespace

int main() {
    RUN_SUITE("spatial_audio");

    // ---- Doppler ratio ------------------------------------------------------------------------
    {
        const Vec3 dir{1, 0, 0};
        const float toward = DopplerRatio(Vec3::Zero, Vec3{-34.3f, 0, 0}, dir, 1.0f, 343.0f);
        CHECK_NEAR(toward, 343.0f / (343.0f - 34.3f), 1.0e-4);
        const float listenerIn = DopplerRatio(Vec3{34.3f, 0, 0}, Vec3::Zero, dir, 1.0f, 343.0f);
        CHECK_NEAR(listenerIn, (343.0f + 34.3f) / 343.0f, 1.0e-4);
        CHECK_NEAR(DopplerRatio(Vec3{10, 0, 0}, Vec3{10, 0, 0}, dir, 1.0f), 1.0f, 1.0e-5);
        CHECK_NEAR(DopplerRatio(Vec3{80, 0, 0}, Vec3{-80, 0, 0}, dir, 0.0f), 1.0f, 1.0e-6);
        const float scaled = DopplerRatio(Vec3::Zero, Vec3{-34.3f, 0, 0}, dir, 2.0f, 343.0f);
        CHECK_NEAR(scaled, 343.0f / (343.0f - 68.6f), 1.0e-4);
        CHECK(scaled > toward);
        // Past the speed of sound the ratio stays finite and positive.
        const float huge = DopplerRatio(Vec3::Zero, Vec3{-10000.0f, 0, 0}, dir, 1.0f, 343.0f);
        CHECK(huge >= 0.25f && huge <= 4.0f);
    }

    // ---- Distance low-pass curve --------------------------------------------------------------
    {
        CHECK_NEAR(DistanceLowPassHz(1.0f, 1.0f, 21.0f, 1000.0f, true), 22000.0f, 1.0e-2);
        CHECK_NEAR(DistanceLowPassHz(21.0f, 1.0f, 21.0f, 1000.0f, true), 1000.0f, 1.0e-2);
        const float mid = DistanceLowPassHz(11.0f, 1.0f, 21.0f, 1000.0f, true);
        const float straight = 22000.0f + (1000.0f - 22000.0f) * 0.5f;
        CHECK(mid > straight);   // t^2 stays brighter than a straight fade at the middle
        CHECK_NEAR(mid, 22000.0f + (1000.0f - 22000.0f) * 0.25f, 1.0f);
        CHECK_NEAR(DistanceLowPassHz(100.0f, 1.0f, 21.0f, 1000.0f, false), 22000.0f, 1.0e-2);
    }

    // ---- Occlusion attenuation ----------------------------------------------------------------
    {
        const OcclusionTone clear = OcclusionAttenuation(false, 0.25f, 800.0f);
        CHECK_NEAR(clear.volume, 1.0f, 1.0e-6);
        CHECK_NEAR(clear.lowPassHz, 22000.0f, 1.0e-3);
        const OcclusionTone blocked = OcclusionAttenuation(true, 0.25f, 800.0f);
        CHECK_NEAR(blocked.volume, 0.25f, 1.0e-6);
        CHECK_NEAR(blocked.lowPassHz, 800.0f, 1.0e-3);
        const OcclusionTone silent = OcclusionAttenuation(true, 2.0f, 5.0f);
        CHECK_NEAR(silent.volume, 1.0f, 1.0e-6);          // clamped: a volume above 1 does not boost
        CHECK(silent.lowPassHz >= 10.0f);
    }

    // ---- Reverb impulse response --------------------------------------------------------------
    {
        SchroederReverb rev;
        rev.Prepare(44100);
        rev.Clear();
        rev.Configure(PresetSettings(ReverbPreset::Hall));
        rev.BeginBlock();
        float peak = 0.0f;
        double early = 0.0, before = 0.0;
        bool finite = true;
        for (int i = 0; i < 8000; ++i) {
            float l = 0.0f, r = 0.0f;
            rev.Process(i == 0 ? 1.0f : 0.0f, l, r);
            if (!std::isfinite(l) || !std::isfinite(r)) finite = false;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            const double e = std::fabs(l) + std::fabs(r);
            if (i < 200) before += e;          // the combs have not spoken yet
            if (i >= 1200 && i < 4000) early += e;
        }
        CHECK(finite);
        CHECK(peak > 0.0f && peak < 4.0f);
        CHECK(before < early * 0.05);          // the tail arrives after the comb delays, not on the impulse
        CHECK(early > 0.05);

        rev.Clear();
        rev.BeginBlock();
        float leftover = 0.0f;
        for (int i = 0; i < 64; ++i) {
            float l = 0.0f, r = 0.0f;
            rev.Process(0.0f, l, r);
            leftover = std::max(leftover, std::max(std::fabs(l), std::fabs(r)));
        }
        CHECK(leftover < 1.0e-6f);

        const double cave = TailOf(ReverbPreset::Cave);
        const double outdoor = TailOf(ReverbPreset::Outdoor);
        const double room = TailOf(ReverbPreset::Room);
        CHECK(cave > 0.0 && outdoor > 0.0 && room > 0.0);
        CHECK(cave > outdoor * 2.0);           // a cave keeps its tail; outdoor dies away
        CHECK(std::fabs(room - cave) > 1.0e-4);
    }

    // ---- Defaults leave a 3D source unchanged -------------------------------------------------
    {
        Reset();
        Scene s("defaults");
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clip = AudioClip::Const(1.0f, 64);
        a->spatial = true;
        a->minDistance = 10.0f;
        a->maxDistance = 20.0f;
        a->transform->SetPosition({3, 0, 0});
        s.Start();
        a->Play();
        AudioMixer::SetListener(Vec3::Zero, Vec3{1, 0, 0});
        const std::vector<float> st = Render(s, 2);
        CHECK_NEAR(st[1], 1.0f, 1.0e-4);                              // near ear, full (inside min distance)
        CHECK_NEAR(st[0], 1.0f - AudioMixer::kSpatialPan, 1.0e-4);
    }

    // ---- Doppler changes the playhead by the ratio -------------------------------------------
    {
        Reset();
        const int frames = 4410;
        const float dt = (float)frames / 44100.0f;
        const float speed = 34.3f;
        Scene s("doppler");
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clip = AudioClip::Const(0.2f, 40000);
        a->spatial = true;
        a->doppler = true;
        a->dopplerScale = 1.0f;
        a->minDistance = 100.0f;
        a->maxDistance = 200.0f;
        a->transform->SetPosition({20, 0, 0});
        s.Start();
        a->Play();
        AudioMixer::SetListener(Vec3::Zero);
        Render(s, frames);                                            // learn the positions; ratio is 1
        const std::size_t p0 = a->Playhead();
        a->transform->SetPosition({20.0f - speed * dt, 0, 0});
        Render(s, frames);
        const double ratio = DopplerRatio(Vec3::Zero, Vec3{-speed, 0, 0}, Vec3{1, 0, 0}, 1.0f);
        CHECK_NEAR((double)a->Playhead() - (double)p0, (double)frames * ratio, 3.0);

        // Scale 0 and the off switch both leave the playhead at one sample per output frame.
        a->dopplerScale = 0.0f;
        a->Play();
        AudioMixer::ResetSpatial();
        a->transform->SetPosition({20, 0, 0});
        Render(s, frames);
        const std::size_t q0 = a->Playhead();
        a->transform->SetPosition({20.0f - speed * dt, 0, 0});
        Render(s, frames);
        CHECK_NEAR((double)a->Playhead() - (double)q0, (double)frames, 2.0);

        a->doppler = false;
        a->dopplerScale = 1.0f;
        a->Play();
        AudioMixer::ResetSpatial();
        a->transform->SetPosition({20, 0, 0});
        Render(s, frames);
        const std::size_t r0 = a->Playhead();
        a->transform->SetPosition({10, 0, 0});
        Render(s, frames);
        CHECK_NEAR((double)a->Playhead() - (double)r0, (double)frames, 2.0);
    }

    // ---- Occlusion: a wall quiets and muffles, a trigger does not ---------------------------
    {
        Reset();
        Scene s("occlusion");
        auto* wall = s.CreateGameObject("Wall");
        wall->transform->SetPosition({5, 0, 0});
        auto* box = wall->AddComponent<BoxCollider3D>();
        box->size = {1, 4, 8};
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clip = AudioClip::Const(0.5f, 44100);
        a->spatial = true;
        a->occlude = true;
        a->occlusionVolume = 0.25f;
        a->occlusionLowPass = 800.0f;
        a->minDistance = 100.0f;
        a->maxDistance = 200.0f;
        a->transform->SetPosition({10, 0, 0});
        s.Start();
        a->Play();
        AudioMixer::SetListener(Vec3::Zero);
        std::vector<float> last;
        for (int i = 0; i < 8; ++i) last = Render(s, 2205);            // 0.4 s, the smoothing has settled
        const OcclusionTone blocked = OcclusionAttenuation(true, 0.25f, 800.0f);
        CHECK_NEAR(Rms(last), 0.5 * (double)blocked.volume, 0.02);

        box->isTrigger = true;
        for (int i = 0; i < 8; ++i) last = Render(s, 2205);
        CHECK_NEAR(Rms(last), 0.5, 0.02);                              // a trigger is not a wall

        // A bright tone is duller when the wall blocks it (volume held at 1, so only the filter remains).
        box->isTrigger = false;
        a->occlusionVolume = 1.0f;
        a->occlusionLowPass = 300.0f;
        a->clip = AudioClip::Sine(4000.0f, 1.0f, 0.5f);
        a->Play();
        AudioMixer::ResetSpatial();
        for (int i = 0; i < 8; ++i) last = Render(s, 2205);
        const double muffled = Rms(last);
        box->isTrigger = true;
        a->Play();
        AudioMixer::ResetSpatial();
        for (int i = 0; i < 4; ++i) last = Render(s, 2205);
        const double open = Rms(last);
        CHECK(open > 0.05);
        CHECK(muffled < open * 0.35);
        CHECK(muffled > 0.0);
    }

    // ---- Distance low-pass in a scene ---------------------------------------------------------
    {
        Reset();
        Scene s("distance");
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clip = AudioClip::Sine(8000.0f, 0.3f, 0.5f);
        a->spatial = true;
        a->minDistance = 1.0f;
        a->maxDistance = 40.0f;
        a->distanceLowPass = true;
        a->distanceLowPassHz = 200.0f;
        a->transform->SetPosition({0.2f, 0, 0});                      // inside min: the curve is open
        s.Start();
        a->Play();
        AudioMixer::SetListener(Vec3::Zero);
        const double closeOn = Rms(Render(s, 4096));
        a->distanceLowPass = false;
        a->Play();
        const double closeOff = Rms(Render(s, 4096));
        CHECK_NEAR(closeOn, closeOff, 1.0e-3);

        a->distanceLowPass = true;
        a->transform->SetPosition({38, 0, 0});                        // near max, still audible, cutoff has closed
        a->Play();
        const double farOn = Rms(Render(s, 4096));
        a->distanceLowPass = false;
        a->Play();
        const double farOff = Rms(Render(s, 4096));
        CHECK(farOff > 1.0e-3);
        CHECK(farOn < farOff * 0.55);
        CHECK(farOn > 0.0);
    }

    // ---- Reverb zone: tail inside, blend at the edge, dry outside, 2D stays dry --------------
    {
        Reset();
        auto measure = [](const Vec3& ear, float blend, bool zoneOn) {
            AudioMixer::ResetSpatial();
            Scene s("zone");
            if (zoneOn) {
                auto* v = s.CreateGameObject("Zone")->AddComponent<Volume>();
                v->kind = Volume::Kind::Reverb;
                v->reverbPreset = (int)ReverbPreset::Cave;
                v->reverbAmount = 1.0f;
                v->blend = blend;
                v->size = {10, 10, 10};
            }
            auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
            a->spatial = true;
            a->minDistance = 100.0f;
            a->maxDistance = 200.0f;
            a->clip.sampleRate = 44100;
            a->clip.samples.assign(8000, 0.0f);
            a->clip.samples[0] = 1.0f;
            s.Start();
            a->Play();
            AudioMixer::SetListener(ear);
            return AbsSum(Render(s, 6000), 3000, 12000);
        };
        const double centre = measure(Vec3::Zero, 1.0f, true);
        const double edge = measure({2.5f, 0, 0}, 1.0f, true);        // weight 0.5
        const double outside = measure({20, 0, 0}, 1.0f, true);
        const double noZone = measure(Vec3::Zero, 1.0f, false);
        CHECK(centre > 0.02);
        CHECK(edge > outside * 5.0);
        CHECK(edge < centre * 0.85);
        CHECK(outside < 1.0e-3);
        CHECK(noZone < 1.0e-3);

        // A stereo 2D clip inside the zone is unchanged on both channels.
        AudioMixer::ResetSpatial();
        Scene stereo("stereo");
        auto* z = stereo.CreateGameObject("Zone")->AddComponent<Volume>();
        z->kind = Volume::Kind::Reverb;
        z->reverbPreset = (int)ReverbPreset::Hall;
        z->size = {20, 20, 20};
        z->blend = 0.0f;
        auto* a = stereo.CreateGameObject("Music")->AddComponent<AudioSource>();
        a->spatial = false;
        a->clip = AudioClip::FromChannels(std::vector<float>(128, 0.4f), std::vector<float>(128, 0.1f));
        stereo.Start();
        a->Play();
        AudioMixer::SetListener(Vec3::Zero);
        const std::vector<float> st = Render(stereo, 32);
        CHECK(AllFinite(st));
        CHECK_NEAR(st[0], 0.4f, 1.0e-4);
        CHECK_NEAR(st[1], 0.1f, 1.0e-4);
        CHECK_NEAR(st[10], 0.4f, 1.0e-4);
        CHECK_NEAR(st[11], 0.1f, 1.0e-4);
    }

    // ---- A stream still plays, and doppler still shifts it -----------------------------------
    {
        Reset();
        const fs::path dir = fs::temp_directory_path() / "okay_spatial_audio_test";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path wav = dir / "tone.wav";
        std::vector<float> mono(44100);
        for (int i = 0; i < 44100; ++i)
            mono[(std::size_t)i] = 0.4f * std::sin(2.0f * 3.14159265f * 440.0f * (float)i / 44100.0f);
        WriteWav(wav, mono, 1, 44100);

        Scene s("stream");
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        std::string err;
        CHECK(a->OpenStream(wav.string(), &err));
        a->spatial = true;
        a->doppler = true;
        a->minDistance = 100.0f;
        a->maxDistance = 200.0f;
        a->transform->SetPosition({20, 0, 0});
        s.Start();
        a->Play();
        AudioMixer::SetListener(Vec3::Zero);
        const std::vector<float> first = Render(s, 4410);
        CHECK(AllFinite(first));
        CHECK(Rms(first) > 0.02);
        const std::size_t p0 = a->Playhead();
        const float dt = 4410.0f / 44100.0f;
        a->transform->SetPosition({20.0f - 34.3f * dt, 0, 0});
        Render(s, 4410);
        const double ratio = DopplerRatio(Vec3::Zero, Vec3{-34.3f, 0, 0}, Vec3{1, 0, 0}, 1.0f);
        CHECK_NEAR((double)a->Playhead() - (double)p0, 4410.0 * ratio, 4.0);

        // Stereo stream, 2D: the left channel stays left.
        std::vector<float> stereoSamples;
        for (int i = 0; i < 4096; ++i) { stereoSamples.push_back(0.5f); stereoSamples.push_back(0.05f); }
        const fs::path stereoPath = dir / "stereo.wav";
        WriteWav(stereoPath, stereoSamples, 2, 44100);
        Scene s2("stereo-stream");
        auto* b = s2.CreateGameObject("B")->AddComponent<AudioSource>();
        CHECK(b->OpenStream(stereoPath.string(), &err));
        b->spatial = false;
        s2.Start();
        b->Play();
        const std::vector<float> st = Render(s2, 256);
        CHECK_NEAR(st[0], 0.5f, 0.02);
        CHECK_NEAR(st[1], 0.05f, 0.02);
        fs::remove_all(dir, ec);
    }

    // ---- Save and load, including an old line that has none of the new fields ----------------
    {
        Reset();
        Scene s("save");
        auto* a = s.CreateGameObject("Speaker")->AddComponent<AudioSource>();
        a->doppler = true;
        a->dopplerScale = 1.75f;
        a->occlude = true;
        a->occlusionVolume = 0.4f;
        a->occlusionLowPass = 640.0f;
        a->distanceLowPass = true;
        a->distanceLowPassHz = 900.0f;
        a->spatial = true;
        auto* v = s.CreateGameObject("Cave")->AddComponent<Volume>();
        v->kind = Volume::Kind::Reverb;
        v->reverbPreset = (int)ReverbPreset::Cave;
        v->reverbAmount = 0.6f;
        v->blend = 0.2f;
        const std::string text = SceneSerializer::Serialize(s);
        Scene back("back");
        std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err, nullptr));
        auto* b = back.Find("Speaker")->GetComponent<AudioSource>();
        CHECK(b && b->doppler && b->occlude && b->distanceLowPass);
        CHECK_NEAR(b->dopplerScale, 1.75f, 1.0e-4);
        CHECK_NEAR(b->occlusionVolume, 0.4f, 1.0e-4);
        CHECK_NEAR(b->occlusionLowPass, 640.0f, 1.0e-3);
        CHECK_NEAR(b->distanceLowPassHz, 900.0f, 1.0e-3);
        auto* rv = back.Find("Cave")->GetComponent<Volume>();
        CHECK(rv && rv->kind == Volume::Kind::Reverb);
        CHECK(rv->reverbPreset == (int)ReverbPreset::Cave);
        CHECK_NEAR(rv->reverbAmount, 0.6f, 1.0e-4);

        const char* old =
            "okayscene 3\n"
            "gameobject 0 \"Old\"\n"
            "  active 1\n"
            "  parent -1\n"
            "  transform 0 0 0 0 0 0 1 1 1 1\n"
            "  audio \"a.wav\" 0.5 1 0 1 2 30 1.2 0.1 1 2 4000 1\n"
            "end\n";
        Scene older("older");
        CHECK(SceneSerializer::Deserialize(older, old, &err, nullptr));
        auto* o = older.Find("Old")->GetComponent<AudioSource>();
        CHECK(o && o->stream && !o->doppler && !o->occlude && !o->distanceLowPass);
        CHECK_NEAR(o->dopplerScale, 1.0f, 1.0e-6);
        CHECK_NEAR(o->occlusionVolume, 0.25f, 1.0e-6);
        CHECK_NEAR(o->distanceLowPassHz, 2000.0f, 1.0e-3);
        CHECK_NEAR(o->lowPass, 4000.0f, 1.0e-3);

        auto info = PropertyRegistry::FindByName("AudioSource");
        CHECK(info && info->Find("doppler") && info->Find("occlude") && info->Find("distanceLowPass"));
        auto vol = PropertyRegistry::FindByName("Volume");
        const PropertyDesc* preset = vol ? vol->Find("reverbPreset") : nullptr;
        CHECK(preset && preset->enumOptions.size() == 4);
        if (preset && preset->enumOptions.size() == 4) {
            CHECK(preset->enumOptions[0] == "Room");
            CHECK(preset->enumOptions[2] == "Cave");
        }
    }

    // ---- The demo scene is wired, and its wall and cave do what they say ---------------------
    {
        Reset();
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/spatial_audio/spatial_audio.okayscene";
        Scene demo("demo");
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(demo, path, &err));
        auto* fly = demo.Find("Flyby");
        auto* behind = demo.Find("Behind Wall");
        auto* wall = demo.Find("Wall");
        auto* cave = demo.Find("Cave");
        CHECK(fly && behind && wall && cave);
        auto* flyAu = fly ? fly->GetComponent<AudioSource>() : nullptr;
        auto* behindAu = behind ? behind->GetComponent<AudioSource>() : nullptr;
        auto* box = wall ? wall->GetComponent<BoxCollider3D>() : nullptr;
        auto* caveVol = cave ? cave->GetComponent<Volume>() : nullptr;
        CHECK(flyAu && flyAu->doppler && flyAu->spatial);
        CHECK(behindAu && behindAu->occlude && behindAu->spatial);
        CHECK(box && !box->isTrigger);
        CHECK(caveVol && caveVol->kind == Volume::Kind::Reverb && caveVol->reverbPreset == (int)ReverbPreset::Cave);
        int presets[4] = {0, 0, 0, 0};
        for (Volume* v : demo.FindObjectsOfType<Volume>())
            if (v->kind == Volume::Kind::Reverb && v->reverbPreset >= 0 && v->reverbPreset < 4)
                presets[v->reverbPreset] = 1;
        CHECK(presets[0] && presets[1] && presets[2] && presets[3]);
        auto* far = demo.Find("Far Speaker");
        auto* farAu = far ? far->GetComponent<AudioSource>() : nullptr;
        CHECK(farAu && farAu->distanceLowPass);
        GameObject* cam = demo.Find("Main Camera");
        GameObject* caveGo = demo.Find("Cave Speaker");
        auto* caveSpeaker = caveGo ? caveGo->GetComponent<AudioSource>() : nullptr;
        CHECK(cam && cam->transform && caveSpeaker && caveVol);

        if (behindAu && box && cam && cam->transform && caveSpeaker && caveVol) {
            for (Volume* v : demo.FindObjectsOfType<Volume>())
                if (v->kind == Volume::Kind::Reverb) v->enabled = false;
            for (AudioSource* src : demo.FindObjectsOfType<AudioSource>()) { src->playOnAwake = false; src->Stop(); }
            behindAu->clip = AudioClip::Const(0.5f, 44100);
            behindAu->minDistance = 100.0f;
            behindAu->maxDistance = 200.0f;
            behindAu->occlusionVolume = 0.25f;
            demo.Start();
            behindAu->Play();
            AudioMixer::SetListener(cam->transform->Position());
            std::vector<float> last;
            for (int i = 0; i < 8; ++i) last = Render(demo, 2205);
            const double blocked = Rms(last);
            box->isTrigger = true;
            for (int i = 0; i < 8; ++i) last = Render(demo, 2205);
            const double open = Rms(last);
            CHECK(open > blocked * 2.0);
            CHECK(blocked > 0.0);

            AudioMixer::ResetSpatial();
            behindAu->Stop();
            caveVol->enabled = true;
            caveSpeaker->Stop();
            caveSpeaker->spatial = true;
            caveSpeaker->minDistance = 100.0f;
            caveSpeaker->maxDistance = 200.0f;
            caveSpeaker->occlude = false;
            caveSpeaker->distanceLowPass = false;
            caveSpeaker->doppler = false;
            caveSpeaker->clip.sampleRate = 44100;
            caveSpeaker->clip.samples.assign(8000, 0.0f);
            caveSpeaker->clip.samples[0] = 1.0f;
            caveSpeaker->Play();
            AudioMixer::SetListener(cave->transform->Position());
            const double inCave = AbsSum(Render(demo, 6000), 3000, 12000);
            AudioMixer::ResetSpatial();
            caveVol->enabled = false;
            caveSpeaker->Play();
            AudioMixer::SetListener(cave->transform->Position());
            const double dry = AbsSum(Render(demo, 6000), 3000, 12000);
            CHECK(inCave > 0.02);
            CHECK(dry < 1.0e-3);
        }
    }

    Reset();
    TEST_MAIN_RESULT();
}
