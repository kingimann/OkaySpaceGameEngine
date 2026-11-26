#include "okay/Audio/AudioClip.hpp"
#include "okay/Audio/WavDecoder.hpp"
#include "AudioInternal.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace okay {

namespace {
void WriteU32(std::ostream& o, std::uint32_t v) {
    unsigned char b[4] = {(unsigned char)(v), (unsigned char)(v >> 8),
                          (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    o.write(reinterpret_cast<char*>(b), 4);
}
void WriteU16(std::ostream& o, std::uint16_t v) {
    unsigned char b[2] = {(unsigned char)(v), (unsigned char)(v >> 8)};
    o.write(reinterpret_cast<char*>(b), 2);
}
} // namespace

namespace audio_detail {

void DownmixToMono(const float* in, std::size_t frames, int channels, float* out) {
    if (channels <= 1) { std::copy(in, in + frames, out); return; }
    // Sum in channel order, then divide (not multiply by 1/channels). AudioClip::LoadWAV and the streamed path
    // (AudioStream's mono wrapper) both average through here, so a clip loaded whole and the same file streamed
    // give the same samples.
    for (std::size_t i = 0; i < frames; ++i) {
        float s = 0.0f;
        for (int c = 0; c < channels; ++c) s += in[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
        out[i] = s / static_cast<float>(channels);
    }
}

} // namespace audio_detail

AudioClip AudioClip::Downmixed() const {
    if (channels <= 1) return *this;
    AudioClip m;
    m.sampleRate = sampleRate;
    m.channels = 1;
    m.samples.resize(FrameCount());
    audio_detail::DownmixToMono(samples.data(), FrameCount(), channels, m.samples.data());
    return m;
}

AudioClip AudioClip::FromChannels(const std::vector<float>& left, const std::vector<float>& right, int rate) {
    AudioClip c;
    c.sampleRate = rate;
    c.channels = 2;
    const std::size_t n = std::max(left.size(), right.size());
    c.samples.assign(n * 2, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        if (i < left.size()) c.samples[2 * i] = left[i];
        if (i < right.size()) c.samples[2 * i + 1] = right[i];
    }
    return c;
}

bool AudioClip::LoadWAV(const std::string& path, std::string* error, bool downmix) {
    samples.clear();
    channels = 1;
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    // Our own decoder (okay::wav) is the only WAV reader: PCM 8 to 32 bit, float, A-law, mu-law, MS and IMA ADPCM.
    wav::WavAudio a;
    std::string e;
    if (!wav::DecodeWav(buf.data(), buf.size(), a, &e)) { if (error) *error = e; return false; }
    sampleRate = a.sampleRate;
    channels = audio_detail::KeptChannels(a.channels, downmix);
    if (channels == a.channels) samples = std::move(a.samples);
    else {
        samples.resize(a.FrameCount());
        audio_detail::DownmixToMono(a.samples.data(), a.FrameCount(), a.channels, samples.data());
    }
    return true;
}

bool AudioClip::SaveWAV(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const std::uint16_t ch = static_cast<std::uint16_t>(channels < 1 ? 1 : channels), bits = 16;
    const std::uint32_t rate = static_cast<std::uint32_t>(sampleRate);
    const std::uint32_t dataLen = static_cast<std::uint32_t>(samples.size()) * 2;
    const std::uint32_t byteRate = rate * ch * (bits / 8);

    f.write("RIFF", 4);
    WriteU32(f, 36 + dataLen);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    WriteU32(f, 16);
    WriteU16(f, 1);                 // PCM
    WriteU16(f, ch);
    WriteU32(f, rate);
    WriteU32(f, byteRate);
    WriteU16(f, static_cast<std::uint16_t>(ch * (bits / 8))); // block align
    WriteU16(f, bits);
    f.write("data", 4);
    WriteU32(f, dataLen);
    for (float s : samples) {
        float c = s < -1.0f ? -1.0f : s > 1.0f ? 1.0f : s;
        std::int16_t v = static_cast<std::int16_t>(c * 32767.0f);
        WriteU16(f, static_cast<std::uint16_t>(v));
    }
    return static_cast<bool>(f);
}

AudioClip AudioClip::Resampled(int newRate) const {
    AudioClip out;
    out.sampleRate = newRate;
    out.channels = channels;
    const std::size_t frames = FrameCount();
    const std::size_t ch = static_cast<std::size_t>(channels > 0 ? channels : 1);
    if (sampleRate <= 0 || newRate <= 0 || frames == 0) return out;
    if (newRate == sampleRate) { out.samples = samples; return out; }

    double ratio = static_cast<double>(sampleRate) / newRate;
    std::size_t n = static_cast<std::size_t>(frames / ratio);
    out.samples.resize(n * ch);
    for (std::size_t i = 0; i < n; ++i) {
        double src = i * ratio;
        std::size_t i0 = static_cast<std::size_t>(src);
        std::size_t i1 = i0 + 1 < frames ? i0 + 1 : i0;
        float frac = static_cast<float>(src - i0);
        for (std::size_t c = 0; c < ch; ++c)
            out.samples[i * ch + c] = samples[i0 * ch + c] * (1.0f - frac) + samples[i1 * ch + c] * frac;
    }
    return out;
}

} // namespace okay
