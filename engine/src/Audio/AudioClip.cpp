#include "okay/Audio/AudioClip.hpp"
#include "AudioInternal.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace okay {

namespace {
std::uint32_t ReadU32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint16_t ReadU16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
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

const char* ParseWav(AudioByteSource& src, WavInfo& w) {
    const std::uint64_t size = src.Size();
    unsigned char hdr[12];
    if (size < 12 || src.Read(0, hdr, 12) != 12 || std::memcmp(hdr, "RIFF", 4) != 0 ||
        std::memcmp(hdr + 8, "WAVE", 4) != 0)
        return "not a RIFF/WAVE file";
    bool haveData = false;
    std::uint64_t pos = 12;
    while (pos + 8 <= size) {
        unsigned char ch[8];
        if (src.Read(pos, ch, 8) != 8) break;
        const std::uint32_t sz = ReadU32(ch + 4);
        const std::uint64_t body = pos + 8;
        if (std::memcmp(ch, "fmt ", 4) == 0 && sz >= 16 && body + 16 <= size) {
            unsigned char f[16];
            if (src.Read(body, f, 16) == 16) {
                w.format = ReadU16(f);
                w.channels = ReadU16(f + 2);
                w.rate = ReadU32(f + 4);
                w.bits = ReadU16(f + 14);
            }
        } else if (std::memcmp(ch, "data", 4) == 0) {
            w.dataOffset = body;
            w.dataLen = (body + sz <= size) ? sz : size - body;   // a chunk longer than the file is cut at the end
            haveData = true;
        }
        pos = body + sz + (sz & 1); // chunks are word-aligned
    }
    if (!haveData || w.channels == 0) return "missing data/fmt chunk";
    w.bytesPerSample = w.bits / 8;
    if (w.bytesPerSample == 0) return "unsupported bit depth";
    return nullptr;
}

void ConvertWavFrames(const WavInfo& w, const unsigned char* raw, std::size_t frames, float* out, int outCh) {
    const int channels = w.channels;
    const int bps = w.bytesPerSample;
    for (std::size_t i = 0; i < frames; ++i) {
        float acc = 0.0f;
        for (int c = 0; c < channels; ++c) {
            const unsigned char* sp = raw + (i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)) * static_cast<std::size_t>(bps);
            float v = 0.0f;
            if (w.format == 3 && w.bits == 32) {            // IEEE float
                std::memcpy(&v, sp, 4);
            } else if (w.bits == 16) {                      // signed 16-bit PCM
                std::int16_t s = static_cast<std::int16_t>(ReadU16(sp));
                v = s / 32768.0f;
            } else if (w.bits == 8) {                       // unsigned 8-bit PCM
                v = (static_cast<int>(sp[0]) - 128) / 128.0f;
            } else if (w.bits == 24) {                      // signed 24-bit PCM
                std::int32_t s = (sp[0]) | (sp[1] << 8) | (sp[2] << 16);
                if (s & 0x800000) s |= ~0xFFFFFF;           // sign-extend
                v = s / 8388608.0f;
            } else if (w.bits == 32) {                      // signed 32-bit PCM
                std::int32_t s = static_cast<std::int32_t>(ReadU32(sp));
                v = s / 2147483648.0f;
            }
            if (outCh == channels) out[i * static_cast<std::size_t>(outCh) + static_cast<std::size_t>(c)] = v;
            else acc += v;
        }
        if (outCh != channels) out[i] = acc / channels;     // downmixed to mono
    }
}

void DownmixToMono(const float* in, std::size_t frames, int channels, float* out) {
    if (channels <= 1) { std::copy(in, in + frames, out); return; }
    const float k = 1.0f / static_cast<float>(channels);
    for (std::size_t i = 0; i < frames; ++i) {
        float s = 0.0f;
        for (int c = 0; c < channels; ++c) s += in[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
        out[i] = s * k;
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
    auto src = AudioByteSource::FromPointer(buf.data(), buf.size());
    audio_detail::WavInfo w;
    if (const char* err = audio_detail::ParseWav(*src, w)) { if (error) *error = err; return false; }

    const int outCh = audio_detail::KeptChannels(w.channels, downmix);
    const std::uint64_t frames = w.Frames();
    samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(outCh));
    sampleRate = static_cast<int>(w.rate);
    channels = outCh;
    audio_detail::ConvertWavFrames(w, buf.data() + w.dataOffset, static_cast<std::size_t>(frames), samples.data(), outCh);
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
