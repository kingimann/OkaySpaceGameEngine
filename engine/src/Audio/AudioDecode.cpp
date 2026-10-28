// OGG Vorbis and MP3 decoding for AudioClip, plus Load() by file extension and
// the shared decode cache. The decoders are vendored single-file libraries
// (stb_vorbis and dr_mp3, both public domain), compiled only in this file
// (warnings off for it in engine/CMakeLists.txt).
#include "okay/Audio/AudioClip.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
// stb_vorbis leaks single-letter macros (C, L, R) into the rest of the file.
#undef C
#undef L
#undef R

namespace okay {

namespace {

bool ReadFile(const std::string& path, std::vector<unsigned char>& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Interleaved samples -> mono (average of the channels).
void Downmix(const float* in, std::size_t frames, int channels, std::vector<float>& out) {
    out.resize(frames);
    if (channels <= 1) { std::copy(in, in + frames, out.begin()); return; }
    const float k = 1.0f / static_cast<float>(channels);
    for (std::size_t i = 0; i < frames; ++i) {
        float s = 0.0f;
        for (int c = 0; c < channels; ++c) s += in[i * channels + c];
        out[i] = s * k;
    }
}

std::string LowerExt(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

} // namespace

bool AudioClip::LoadOGG(const std::string& path, std::string* error) {
    samples.clear();
    std::vector<unsigned char> buf;
    if (!ReadFile(path, buf, error)) return false;
    int channels = 0, rate = 0;
    short* pcm = nullptr;
    const int frames = stb_vorbis_decode_memory(buf.data(), (int)buf.size(), &channels, &rate, &pcm);
    if (frames <= 0 || !pcm || channels <= 0) {
        if (pcm) std::free(pcm);
        if (error) *error = "not a readable OGG Vorbis file";
        return false;
    }
    std::vector<float> f(static_cast<std::size_t>(frames) * channels);
    for (std::size_t i = 0; i < f.size(); ++i) f[i] = pcm[i] / 32768.0f;
    std::free(pcm);
    Downmix(f.data(), (std::size_t)frames, channels, samples);
    sampleRate = rate;
    return true;
}

bool AudioClip::LoadMP3(const std::string& path, std::string* error) {
    samples.clear();
    std::vector<unsigned char> buf;
    if (!ReadFile(path, buf, error)) return false;
    drmp3_config cfg{};
    drmp3_uint64 frames = 0;
    float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(buf.data(), buf.size(), &cfg, &frames, nullptr);
    if (!pcm || frames == 0 || cfg.channels == 0) {
        if (pcm) drmp3_free(pcm, nullptr);
        if (error) *error = "not a readable MP3 file";
        return false;
    }
    Downmix(pcm, (std::size_t)frames, (int)cfg.channels, samples);
    drmp3_free(pcm, nullptr);
    sampleRate = (int)cfg.sampleRate;
    return true;
}

bool AudioClip::Load(const std::string& path, std::string* error) {
    const std::string ext = LowerExt(path);
    if (ext == "ogg") return LoadOGG(path, error);
    if (ext == "mp3") return LoadMP3(path, error);
    if (ext == "wav") return LoadWAV(path, error);
    // Unknown extension: try each format in turn.
    if (LoadWAV(path, nullptr) || LoadOGG(path, nullptr) || LoadMP3(path, nullptr)) return true;
    if (error) *error = "unsupported audio file (use WAV, OGG or MP3): " + path;
    return false;
}

bool AudioClip::IsAudioFile(const std::string& path) {
    const std::string ext = LowerExt(path);
    return ext == "wav" || ext == "ogg" || ext == "mp3";
}

std::shared_ptr<const AudioClip> AudioClip::LoadCached(const std::string& path, int rate, std::string* error) {
    static std::mutex mu;
    static std::map<std::pair<std::string, int>, std::shared_ptr<const AudioClip>> cache;
    const auto key = std::make_pair(path, rate);
    {
        std::lock_guard<std::mutex> lock(mu);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    AudioClip c;
    if (!c.Load(path, error)) return nullptr;
    auto shared = std::make_shared<const AudioClip>(rate > 0 && c.sampleRate != rate ? c.Resampled(rate) : std::move(c));
    std::lock_guard<std::mutex> lock(mu);
    cache[key] = shared;
    return shared;
}

} // namespace okay
