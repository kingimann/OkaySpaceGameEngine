// OGG Vorbis and MP3 decoding for AudioClip, plus Load() by file extension and
// the shared decode cache. OGG goes through the engine's own decoder
// (VorbisDecoder.cpp); MP3 goes through the engine's own decoder
// (Mp3Decoder.cpp).
#include "okay/Audio/AudioClip.hpp"
#include "okay/Audio/Mp3Decoder.hpp"
#include "okay/Audio/VorbisDecoder.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>


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
    VorbisAudio audio;
    if (!DecodeVorbis(buf.data(), buf.size(), audio, nullptr) || audio.FrameCount() == 0) {
        if (error) *error = "not a readable OGG Vorbis file";
        return false;
    }
    Downmix(audio.samples.data(), audio.FrameCount(), audio.channels, samples);
    sampleRate = audio.sampleRate;
    return true;
}

bool AudioClip::LoadMP3(const std::string& path, std::string* error) {
    samples.clear();
    std::vector<unsigned char> buf;
    if (!ReadFile(path, buf, error)) return false;
    Mp3Audio audio;
    if (!DecodeMp3(buf.data(), buf.size(), audio, nullptr) || audio.FrameCount() == 0) {
        if (error) *error = "not a readable MP3 file";
        return false;
    }
    Downmix(audio.samples.data(), audio.FrameCount(), audio.channels, samples);
    sampleRate = audio.sampleRate;
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
    std::string loadErr;
    if (!c.Load(path, &loadErr)) {
        Log::Warning("[asset] cannot load audio '", path, "': ", loadErr);
        if (error) *error = loadErr;
        return nullptr;
    }
    auto shared = std::make_shared<const AudioClip>(rate > 0 && c.sampleRate != rate ? c.Resampled(rate) : std::move(c));
    std::lock_guard<std::mutex> lock(mu);
    cache[key] = shared;
    return shared;
}

} // namespace okay
