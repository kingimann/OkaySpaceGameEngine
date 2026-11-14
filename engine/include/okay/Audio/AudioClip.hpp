#pragma once
#include "okay/Math/Mathf.hpp"
#include "okay/Core/Random.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// PCM audio data (samples in [-1, 1]) plus its sample rate. Clips are mono
/// (`channels` 1) or stereo (`channels` 2, samples interleaved L, R, L, R, ...);
/// loaders keep a file's channels (more than two are averaged to mono) and only
/// downmix when asked. Includes generators for simple procedural sounds so games
/// (and tests) need no asset files; they all make mono clips.
struct AudioClip {
    std::vector<float> samples;
    int sampleRate = 44100;
    int channels = 1;

    /// Sample frames: one per time step, whatever the channel count.
    std::size_t FrameCount() const {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
    float Duration() const {
        return sampleRate > 0 ? static_cast<float>(FrameCount()) / sampleRate : 0.0f;
    }
    /// A mono copy (the channels averaged); a mono clip comes back unchanged.
    AudioClip Downmixed() const;
    /// A stereo clip from two equally long mono channel arrays (the shorter is padded with silence).
    static AudioClip FromChannels(const std::vector<float>& left, const std::vector<float>& right, int rate = 44100);

    static AudioClip Sine(float freq, float seconds, float amplitude = 0.5f, int rate = 44100) {
        AudioClip c; c.sampleRate = rate;
        int n = static_cast<int>(seconds * rate);
        c.samples.resize(n);
        for (int i = 0; i < n; ++i)
            c.samples[i] = amplitude * Mathf::Sin(2.0f * Mathf::PI * freq * i / rate);
        return c;
    }
    static AudioClip Square(float freq, float seconds, float amplitude = 0.4f, int rate = 44100) {
        AudioClip c; c.sampleRate = rate;
        int n = static_cast<int>(seconds * rate);
        c.samples.resize(n);
        for (int i = 0; i < n; ++i) {
            float ph = freq * i / rate;
            c.samples[i] = (ph - Mathf::Floor(ph) < 0.5f ? amplitude : -amplitude);
        }
        return c;
    }
    static AudioClip Noise(float seconds, float amplitude = 0.3f, std::uint64_t seed = 1, int rate = 44100) {
        AudioClip c; c.sampleRate = rate;
        int n = static_cast<int>(seconds * rate);
        c.samples.resize(n);
        Random rng(seed);
        for (int i = 0; i < n; ++i) c.samples[i] = rng.Range(-amplitude, amplitude);
        return c;
    }
    static AudioClip Silence(float seconds, int rate = 44100) {
        AudioClip c; c.sampleRate = rate;
        c.samples.assign(static_cast<size_t>(seconds * rate), 0.0f);
        return c;
    }
    /// A constant-value clip (handy for tests).
    static AudioClip Const(float value, int count, int rate = 44100) {
        AudioClip c; c.sampleRate = rate;
        c.samples.assign(count, value);
        return c;
    }

    /// Load a PCM/float WAV file, keeping its channels (1 or 2; more are averaged
    /// to mono, and `downmix` forces mono). Returns false (leaving the clip
    /// empty) on failure.
    bool LoadWAV(const std::string& path, std::string* error = nullptr, bool downmix = false);
    /// Load an OGG Vorbis or MP3 file the same way (the file's own rate).
    bool LoadOGG(const std::string& path, std::string* error = nullptr, bool downmix = false);
    bool LoadMP3(const std::string& path, std::string* error = nullptr, bool downmix = false);
    /// Load a WAV, OGG or MP3 file, chosen by its extension.
    bool Load(const std::string& path, std::string* error = nullptr, bool downmix = false);
    /// True for the extensions Load understands (.wav, .ogg, .mp3).
    static bool IsAudioFile(const std::string& path);
    /// Load `path` once (resampled to `rate`, or kept as is when 0) and share the
    /// decoded clip: twenty sources on one file decode it once. Null on failure.
    static std::shared_ptr<const AudioClip> LoadCached(const std::string& path, int rate = 0,
                                                       std::string* error = nullptr, bool downmix = false);
    /// Save as a 16-bit PCM WAV with this clip's channels. Returns false on failure.
    bool SaveWAV(const std::string& path) const;

    /// A copy of this clip resampled to `newRate` (linear interpolation, per channel).
    AudioClip Resampled(int newRate) const;
};

} // namespace okay
