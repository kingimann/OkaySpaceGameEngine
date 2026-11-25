// Drives the engine's own engine/src/Audio/SpatialFx.cpp, compiled unchanged by okay-cc (Mathf's
// constant static members and numeric_limits<float>, a nested class's members defined outside
// it, std::llround, vector::assign, float arithmetic in a reverb network).
#include "okay/Audio/SpatialFx.hpp"

extern "C" int printf(const char *fmt, ...);

int main() {
    for (int p = 0; p < (int)okay::ReverbPreset::Count; ++p) {
        okay::ReverbSettings s = okay::PresetSettings((okay::ReverbPreset)p);
        printf("%s %.3f %.3f %.3f %.3f\n", okay::ReverbPresetName((okay::ReverbPreset)p), s.room, s.damp, s.wet,
               s.width);
    }
    okay::RoomReverb rv;
    rv.Prepare(48000);
    rv.Configure(okay::PresetSettings(okay::ReverbPreset::Hall));
    printf("rate %d decay %.4f lines", rv.Rate(), rv.DecaySeconds());
    for (int i = 0; i < 8; ++i) printf(" %d", rv.LineLength(i));
    printf("\n");
    double sumL = 0, sumR = 0;
    float lastL = 0, lastR = 0;
    for (int block = 0; block < 20; ++block) {
        rv.BeginBlock();
        for (int i = 0; i < 256; ++i) {
            float in = (block == 0 && i == 0) ? 1.0f : 0.0f;
            rv.Process(in, lastL, lastR);
            sumL += lastL < 0 ? -lastL : lastL;
            sumR += lastR < 0 ? -lastR : lastR;
        }
    }
    printf("energy %.6f %.6f last %.9g %.9g silent %d\n", sumL, sumR, lastL, lastR, rv.Silent());
    okay::Vec3 lv{0, 0, 0}, sv{0, 0, 10}, dir{0, 0, 1};
    printf("doppler %.6f %.6f %.6f\n", okay::DopplerRatio(lv, sv, dir, 1.0f), okay::DopplerRatio(lv, sv, dir, 0.0f),
           okay::DopplerRatio(sv, lv, dir, 2.0f));
    printf("lowpass %.3f %.3f %.3f %.3f\n", okay::DistanceLowPassHz(5, 1, 50, 2000, true),
           okay::DistanceLowPassHz(0.5f, 1, 50, 2000, true), okay::DistanceLowPassHz(80, 1, 50, 2000, true),
           okay::DistanceLowPassHz(5, 1, 50, 2000, false));
    okay::OcclusionTone a = okay::OcclusionAttenuation(true, 0.3f, 900.0f);
    okay::OcclusionTone b = okay::OcclusionAttenuation(false, 0.3f, 900.0f);
    okay::OcclusionTone c = okay::OcclusionAttenuation(true, 4.0f, 900.0f, 12000.0f);
    printf("occlusion %.3f %.1f %.3f %.1f %.3f %.1f\n", a.volume, a.lowPassHz, b.volume, b.lowPassHz, c.volume,
           c.lowPassHz);
    rv.Clear();
    printf("cleared %d\n", rv.Silent());
    return 0;
}
