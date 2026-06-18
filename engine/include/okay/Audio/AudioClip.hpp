#pragma once
#include "okay/Math/Mathf.hpp"
#include "okay/Core/Random.hpp"
#include <vector>

namespace okay {

/// Mono PCM audio data (samples in [-1, 1]) plus its sample rate. Includes
/// generators for simple procedural sounds so games (and tests) need no asset
/// files.
struct AudioClip {
    std::vector<float> samples;
    int sampleRate = 44100;

    float Duration() const {
        return sampleRate > 0 ? static_cast<float>(samples.size()) / sampleRate : 0.0f;
    }

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
};

} // namespace okay
