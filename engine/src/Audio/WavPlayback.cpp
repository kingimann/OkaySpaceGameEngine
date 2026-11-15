#include "okay/Audio/WavPlayback.hpp"
#include "okay/Audio/WavDecoder.hpp"

#include <cstring>
#include <fstream>

namespace okay {
namespace wav {

bool DecodeForPlayback(const unsigned char* data, std::size_t size, PlaybackClip& out, std::string* error) {
    out = PlaybackClip{};
    WavAudio a;
    if (!data || size == 0) { if (error) *error = "empty file"; return false; }
    if (!DecodeWav(data, size, a, error)) return false;
    if (a.samples.empty() || a.channels <= 0 || a.sampleRate <= 0) { if (error) *error = "no samples"; return false; }
    if (a.encoding == Encoding::Float && a.bitsPerSample == 32) {
        out.format = PlaybackFormat::F32;
        out.bytes.resize(a.samples.size() * sizeof(float));
        std::memcpy(out.bytes.data(), a.samples.data(), out.bytes.size());
    } else {
        out.format = PlaybackFormat::S16;
        const std::vector<std::int16_t> s = ToS16(a);
        out.bytes.resize(s.size() * sizeof(std::int16_t));
        if (!s.empty()) std::memcpy(out.bytes.data(), s.data(), out.bytes.size());
    }
    out.channels = a.channels;
    out.sampleRate = a.sampleRate;
    if (out.bytes.empty()) { if (error) *error = "no samples"; return false; }
    return true;
}

bool LoadForPlayback(const std::string& path, PlaybackClip& out, std::string* error) {
    out = PlaybackClip{};
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { if (error) *error = "cannot open file"; return false; }
    const std::streamoff n = f.tellg();
    if (n <= 0) { if (error) *error = "empty file"; return false; }
    if (static_cast<unsigned long long>(n) > Limits{}.maxInputBytes) { if (error) *error = "file too large"; return false; }
    std::vector<unsigned char> buf(static_cast<std::size_t>(n));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(buf.data()), n)) { if (error) *error = "read failed"; return false; }
    return DecodeForPlayback(buf.data(), buf.size(), out, error);
}

} // namespace wav
} // namespace okay
