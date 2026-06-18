#include "okay/Audio/AudioClip.hpp"

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

bool AudioClip::LoadWAV(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (buf.size() < 12 || std::memcmp(buf.data(), "RIFF", 4) != 0 ||
        std::memcmp(buf.data() + 8, "WAVE", 4) != 0)
        return fail("not a RIFF/WAVE file");

    std::uint16_t format = 1, channels = 1, bits = 16;
    std::uint32_t rate = 44100;
    const unsigned char* dataPtr = nullptr;
    std::uint32_t dataLen = 0;

    std::size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const unsigned char* ch = buf.data() + pos;
        std::uint32_t sz = ReadU32(ch + 4);
        const unsigned char* body = ch + 8;
        if (std::memcmp(ch, "fmt ", 4) == 0 && sz >= 16) {
            format = ReadU16(body);
            channels = ReadU16(body + 2);
            rate = ReadU32(body + 4);
            bits = ReadU16(body + 14);
        } else if (std::memcmp(ch, "data", 4) == 0) {
            dataPtr = body;
            dataLen = (pos + 8 + sz <= buf.size()) ? sz
                      : static_cast<std::uint32_t>(buf.size() - (pos + 8));
        }
        pos += 8 + sz + (sz & 1); // chunks are word-aligned
    }
    if (!dataPtr || channels == 0) return fail("missing data/fmt chunk");

    int bytesPerSample = bits / 8;
    if (bytesPerSample == 0) return fail("unsupported bit depth");
    std::uint32_t totalSamples = dataLen / bytesPerSample;
    std::uint32_t frames = totalSamples / channels;

    samples.clear();
    samples.reserve(frames);
    sampleRate = static_cast<int>(rate);

    for (std::uint32_t i = 0; i < frames; ++i) {
        float acc = 0.0f;
        for (int c = 0; c < channels; ++c) {
            const unsigned char* sp = dataPtr + (static_cast<std::size_t>(i) * channels + c) * bytesPerSample;
            float v = 0.0f;
            if (format == 3 && bits == 32) {            // IEEE float
                std::memcpy(&v, sp, 4);
            } else if (bits == 16) {                    // signed 16-bit PCM
                std::int16_t s = static_cast<std::int16_t>(ReadU16(sp));
                v = s / 32768.0f;
            } else if (bits == 8) {                     // unsigned 8-bit PCM
                v = (static_cast<int>(sp[0]) - 128) / 128.0f;
            } else if (bits == 24) {                    // signed 24-bit PCM
                std::int32_t s = (sp[0]) | (sp[1] << 8) | (sp[2] << 16);
                if (s & 0x800000) s |= ~0xFFFFFF;       // sign-extend
                v = s / 8388608.0f;
            } else if (bits == 32) {                    // signed 32-bit PCM
                std::int32_t s = static_cast<std::int32_t>(ReadU32(sp));
                v = s / 2147483648.0f;
            }
            acc += v;
        }
        samples.push_back(acc / channels); // downmix to mono
    }
    return true;
}

bool AudioClip::SaveWAV(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const std::uint16_t channels = 1, bits = 16;
    const std::uint32_t rate = static_cast<std::uint32_t>(sampleRate);
    const std::uint32_t dataLen = static_cast<std::uint32_t>(samples.size()) * 2;
    const std::uint32_t byteRate = rate * channels * (bits / 8);

    f.write("RIFF", 4);
    WriteU32(f, 36 + dataLen);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    WriteU32(f, 16);
    WriteU16(f, 1);                 // PCM
    WriteU16(f, channels);
    WriteU32(f, rate);
    WriteU32(f, byteRate);
    WriteU16(f, static_cast<std::uint16_t>(channels * (bits / 8))); // block align
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
    if (sampleRate <= 0 || newRate <= 0 || samples.empty()) return out;
    if (newRate == sampleRate) { out.samples = samples; return out; }

    double ratio = static_cast<double>(sampleRate) / newRate;
    std::size_t n = static_cast<std::size_t>(samples.size() / ratio);
    out.samples.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        double src = i * ratio;
        std::size_t i0 = static_cast<std::size_t>(src);
        std::size_t i1 = i0 + 1 < samples.size() ? i0 + 1 : i0;
        float frac = static_cast<float>(src - i0);
        out.samples[i] = samples[i0] * (1.0f - frac) + samples[i1] * frac;
    }
    return out;
}

} // namespace okay
