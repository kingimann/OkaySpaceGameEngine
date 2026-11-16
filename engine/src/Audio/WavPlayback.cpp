#include "okay/Audio/WavPlayback.hpp"
#include "okay/Audio/WavDecoder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace okay {
namespace wav {

// DecodeForPlayback is in WavDecoder.cpp, next to the decoder it shares its plan and sinks with.
namespace detail {
bool DecodeFileForPlayback(std::vector<std::uint8_t>& head, std::vector<std::uint8_t>& rest, std::size_t restAt,
                           std::uint64_t split, std::uint64_t fileSize, PlaybackClip& out, std::string* error);
std::uint64_t FindDataStart(const unsigned char* p, std::size_t n, std::uint64_t size, bool* widens);
}

namespace {
struct File {   // C stdio, unbuffered: no stream buffer is allocated, reads go straight into our buffers
    std::FILE* f = nullptr;
    ~File() { if (f) std::fclose(f); }
    bool Read(unsigned char* dst, std::size_t n) { return n == 0 || std::fread(dst, 1, n, f) == n; }
};
} // namespace

bool LoadForPlayback(const std::string& path, PlaybackClip& out, std::string* error) {
    out = PlaybackClip{};
    File file;
    file.f = std::fopen(path.c_str(), "rb");
    if (!file.f) { if (error) *error = "cannot open file"; return false; }
    std::setvbuf(file.f, nullptr, _IONBF, 0);
#ifdef _WIN32
    if (_fseeki64(file.f, 0, SEEK_END) != 0) { if (error) *error = "read failed"; return false; }
    const long long n = _ftelli64(file.f);
#else
    if (std::fseek(file.f, 0, SEEK_END) != 0) { if (error) *error = "read failed"; return false; }
    const long long n = std::ftell(file.f);   // long is 64-bit on Linux and macOS
#endif
    if (n <= 0) { if (error) *error = "empty file"; return false; }
    if (static_cast<unsigned long long>(n) > Limits{}.maxInputBytes) { if (error) *error = "file too large"; return false; }
    std::rewind(file.f);
    const std::size_t size = static_cast<std::size_t>(n);

    // The header is read first (512 bytes, then up to 64 KiB if the data chunk does not start in those), and the
    // samples onward go into a buffer of their own, laid out so the decode can work inside it (see
    // DecodeFileForPlayback): samples at its start, or, for 8-bit PCM, A-law and mu-law, in its second half.
    // Small files, and files whose data chunk does not start in the first 64 KiB, are read whole into one buffer.
    std::vector<std::uint8_t> head, rest;
    auto readHead = [&](std::size_t want) {
        const std::size_t had = head.size();
        head.resize(std::min(size, want));
        return file.Read(head.data() + had, head.size() - had);
    };
    std::size_t restAt = 0;
    std::uint64_t split = size;
    bool widens = false;
    if (!readHead(512)) { if (error) *error = "read failed"; return false; }
    std::uint64_t start = size > head.size() ? detail::FindDataStart(head.data(), head.size(), size, &widens) : 0;
    if (start == 0 && size > head.size()) {
        if (!readHead(64 * 1024)) { if (error) *error = "read failed"; return false; }
        start = size > head.size() ? detail::FindDataStart(head.data(), head.size(), size, &widens) : 0;
    }
    if (start == 0 && size > head.size()) {   // one buffer after all
        if (!readHead(size)) { if (error) *error = "read failed"; return false; }
    } else if (start != 0) {
        split = start;
        const std::size_t tail = size - std::size_t(start);   // bytes from the samples to the end of the file
        restAt = widens ? tail : 0;
        rest.resize(restAt + tail);
        const std::size_t have = head.size() - std::size_t(start);   // sample bytes already read with the header
        std::memcpy(rest.data() + restAt, head.data() + start, have);
        head.resize(std::size_t(start));
        if (!file.Read(rest.data() + restAt + have, tail - have)) { if (error) *error = "read failed"; return false; }
    }
    return detail::DecodeFileForPlayback(head, rest, restAt, split, size, out, error);
}

} // namespace wav
} // namespace okay
