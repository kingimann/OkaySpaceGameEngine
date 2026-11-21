// Hostile-input tests for our own FLAC decoder (engine/src/Audio/FlacDecoder.cpp): damaged, cut, lying and random
// files must not crash, hang or grow memory without bound, and the whole-file decoder and the streaming one must
// agree on whatever they accept. Written from the public FLAC format description only. The two CRCs are
// re-implemented here (CRC-8 polynomial 0x07, CRC-16 polynomial 0x8005, both started at zero) so mutated frames can be
// re-sealed and reach the predictor and residual code instead of being thrown away by the checksum.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Audio/FlacDecoder.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;
using Bytes = std::vector<unsigned char>;

namespace {

const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/audio/flac/";

bool ReadAll(const std::string& path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

struct HRng {   // fixed generator: the <random> engines differ between libraries
    std::uint32_t s;
    explicit HRng(std::uint32_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    std::uint32_t Below(std::uint32_t n) { return n ? Next() % n : 0; }
};

std::uint8_t Crc8(const unsigned char* p, std::size_t n) {
    std::uint8_t c = 0;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b) c = static_cast<std::uint8_t>((c & 0x80) ? (c << 1) ^ 0x07 : (c << 1));
    }
    return c;
}
std::uint16_t Crc16(const unsigned char* p, std::size_t n) {
    std::uint16_t c = 0;
    for (std::size_t i = 0; i < n; ++i) {
        c = static_cast<std::uint16_t>(c ^ (static_cast<std::uint16_t>(p[i]) << 8));
        for (int b = 0; b < 8; ++b) c = static_cast<std::uint16_t>((c & 0x8000) ? (c << 1) ^ 0x8005 : (c << 1));
    }
    return c;
}

// Where the audio starts: skips "fLaC" and the metadata blocks (no ID3 in the fixtures).
std::size_t AudioStart(const Bytes& d) {
    std::size_t pos = 4;
    for (;;) {
        if (pos + 4 > d.size()) return d.size();
        const bool last = (d[pos] & 0x80) != 0;
        const std::size_t len = (std::size_t(d[pos + 1]) << 16) | (std::size_t(d[pos + 2]) << 8) | d[pos + 3];
        pos += 4 + len;
        if (last) return pos;
    }
}

struct Frame { std::size_t start, hdrLen, end; };   // [start, end) with the trailing CRC-16 inside

std::size_t HeaderLen(const Bytes& d, std::size_t at) {
    for (std::size_t L = 5; L <= 16 && at + L < d.size(); ++L)
        if (Crc8(&d[at], L - 1) == d[at + L - 1]) return L;
    return 0;
}

// Frames whose header CRC-8 and trailing CRC-16 both check out.
std::vector<Frame> FindFrames(const Bytes& d) {
    std::vector<std::size_t> starts;
    for (std::size_t i = AudioStart(d); i + 6 < d.size(); ++i) {
        if (d[i] != 0xFF || (d[i + 1] & 0xFE) != 0xF8) continue;
        if (HeaderLen(d, i)) starts.push_back(i);
    }
    std::vector<Frame> out;
    for (std::size_t k = 0; k < starts.size(); ++k) {
        const std::size_t end = k + 1 < starts.size() ? starts[k + 1] : d.size();
        if (end < starts[k] + 8) continue;
        if (Crc16(&d[starts[k]], end - starts[k] - 2) != static_cast<std::uint16_t>((d[end - 2] << 8) | d[end - 1])) continue;
        out.push_back({starts[k], HeaderLen(d, starts[k]), end});
    }
    return out;
}

void Reseal(Bytes& d, const Frame& f) {
    const std::uint16_t c = Crc16(&d[f.start], f.end - f.start - 2);
    d[f.end - 2] = static_cast<unsigned char>(c >> 8);
    d[f.end - 1] = static_cast<unsigned char>(c);
}

bool AllFinite(const std::vector<float>& v) {
    for (float f : v) if (!std::isfinite(f)) return false;
    return true;
}

// Decodes whole and streamed; checks the bounds and that the two agree. Returns whether the whole decode succeeded.
bool Probe(const Bytes& d, bool checkAgree = true) {
    FlacAudio a;
    std::string err;
    const bool ok = DecodeFlac(d.data(), d.size(), a, &err);
    CHECK(AllFinite(a.samples));
    CHECK(a.samples.size() <= (std::size_t(1) << 28));
    CHECK(a.samples.size() <= d.size() * 20000u + 1u);   // a tiny constant frame may expand, but not without a bound
    if (ok) CHECK(a.channels >= 1 && a.channels <= 8 && a.sampleRate >= 1 && a.samples.size() % std::size_t(a.channels) == 0);
    if (!ok) CHECK(!err.empty());

    FlacStream st;
    auto bytes = std::make_shared<const Bytes>(d);
    const bool sok = st.Open(AudioByteSource::FromMemory(bytes), nullptr);
    if (sok) {
        std::vector<float> got;
        std::vector<float> chunk(97 * 8);
        for (int guard = 0; guard < 4000000; ++guard) {
            const std::size_t n = st.Read(chunk.data(), 97);
            if (n == 0) break;
            got.insert(got.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * static_cast<std::size_t>(st.Channels())));
            if (got.size() > (std::size_t(1) << 28)) break;
        }
        CHECK(AllFinite(got));
        // The two decoders share code; on a file both accept they must give the same samples.
        if (checkAgree && ok) CHECK(got == a.samples);
        st.Seek(static_cast<std::int64_t>(got.size() / 2 / static_cast<std::size_t>(st.Channels())));
        st.Read(chunk.data(), 97);
        st.Seek(0);
        st.Seek(std::int64_t(1) << 30);   // far past the end: no crash
        CHECK(st.Read(chunk.data(), 97) == 0);
    }
    return ok;
}

} // namespace

int main() {
    const char* names[] = {"noise_mono_8k_fixed", "noise_mono_8k_lpc", "pcm24_mono_16k", "sine_mono_44k", "tones_stereo_22k"};
    std::vector<Bytes> files;
    for (const char* n : names) {
        Bytes b;
        CHECK(ReadAll(kDir + n + std::string(".flac"), b));
        files.push_back(b);
        CHECK(Probe(b));   // the untouched originals decode
    }
    const auto t0 = std::chrono::steady_clock::now();

    RUN_SUITE("every truncation length");
    for (const Bytes& f : files) {
        const std::size_t step = f.size() > 6000 ? 7 : 1;
        for (std::size_t n = 0; n < f.size(); n += step) {
            Bytes cut(f.begin(), f.begin() + static_cast<std::ptrdiff_t>(n));
            const bool ok = Probe(cut);
            if (n < AudioStart(f) + 8) CHECK(!ok);   // header only or less: nothing to decode
        }
    }

    RUN_SUITE("frames found for resealing");
    for (const Bytes& f : files) CHECK(FindFrames(f).size() >= 2);

    RUN_SUITE("bit flips, raw (the checksums reject most)");
    {
        HRng rng(7);
        for (const Bytes& f : files)
            for (int it = 0; it < 150; ++it) {
                Bytes d = f;
                const int flips = 1 + static_cast<int>(rng.Below(12));
                for (int k = 0; k < flips; ++k) d[rng.Below(static_cast<std::uint32_t>(d.size()))] ^= static_cast<unsigned char>(1u << rng.Below(8));
                Probe(d);
            }
    }

    RUN_SUITE("bit flips inside frames, checksums repaired");
    {
        HRng rng(99);
        int accepted = 0, total = 0;
        for (const Bytes& f : files) {
            const std::vector<Frame> fr = FindFrames(f);
            for (int it = 0; it < 300; ++it) {
                Bytes d = f;
                const Frame& t = fr[rng.Below(static_cast<std::uint32_t>(fr.size()))];
                const std::size_t body = t.end - 2 - (t.start + t.hdrLen);
                if (body == 0) continue;
                const int flips = 1 + static_cast<int>(rng.Below(6));
                for (int k = 0; k < flips; ++k) d[t.start + t.hdrLen + rng.Below(static_cast<std::uint32_t>(body))] ^= static_cast<unsigned char>(1u << rng.Below(8));
                if (it % 11 == 0) {   // a burst of random bytes
                    const std::size_t at = t.start + t.hdrLen + rng.Below(static_cast<std::uint32_t>(body));
                    for (std::size_t i = at; i < std::min(t.end - 2, at + 1 + rng.Below(40)); ++i) d[i] = static_cast<unsigned char>(rng.Next());
                }
                Reseal(d, t);
                ++total;
                if (Probe(d)) ++accepted;
            }
        }
        CHECK(accepted > total / 4);   // damaged frames that still parse are decoded, not all refused
    }

    RUN_SUITE("header bytes (block size, rate, channel codes) damaged and resealed");
    {
        HRng rng(5);
        for (const Bytes& f : files) {
            const std::vector<Frame> fr = FindFrames(f);
            for (int it = 0; it < 200; ++it) {
                Bytes d = f;
                const Frame& t = fr[rng.Below(static_cast<std::uint32_t>(fr.size()))];
                d[t.start + 2 + rng.Below(2)] ^= static_cast<unsigned char>(1u << rng.Below(8));
                d[t.start + t.hdrLen - 1] = Crc8(&d[t.start], t.hdrLen - 1);
                Reseal(d, t);
                Probe(d);
            }
        }
    }

    RUN_SUITE("STREAMINFO that lies");
    {
        const Bytes& base = files[3];
        // STREAMINFO body starts at byte 8; bytes 18..25 carry rate(20) channels-1(3) bps-1(5) total(36).
        auto withInfo = [&](std::uint64_t word) {
            Bytes d = base;
            for (int i = 0; i < 8; ++i) d[18 + i] = static_cast<unsigned char>(word >> (56 - 8 * i));
            return d;
        };
        {   // total claims 2^36-1 frames: the output stays what the frames really hold
            Bytes d = withInfo((std::uint64_t(44100) << 44) | (std::uint64_t(15) << 36) | ((std::uint64_t(1) << 36) - 1));
            FlacAudio a;
            DecodeFlac(d.data(), d.size(), a, nullptr);
            CHECK(a.samples.size() <= std::size_t(22050));
            Probe(d);
        }
        for (std::uint64_t rate : {std::uint64_t(0), std::uint64_t(1), std::uint64_t(655350), std::uint64_t(655351), std::uint64_t(1048575)})
            for (std::uint64_t ch = 0; ch < 8; ++ch)
                for (std::uint64_t bps : {std::uint64_t(0), std::uint64_t(3), std::uint64_t(15), std::uint64_t(31)}) {
                    Bytes d = withInfo((rate << 44) | (ch << 41) | (bps << 36) | 22050);
                    FlacAudio a;
                    const bool ok = DecodeFlac(d.data(), d.size(), a, nullptr);
                    if (rate < 1 || rate > 655350 || bps + 1 < 4) CHECK(!ok);
                    Probe(d, false);
                }
    }

    RUN_SUITE("metadata that lies or never ends");
    {
        FlacAudio a;
        Bytes big = files[3];
        big[5] = 0xFF; big[6] = 0xFF; big[7] = 0xFF;      // a 16 MiB - 1 first block: past the end of the file
        CHECK(!DecodeFlac(big.data(), big.size(), a, nullptr));
        Probe(big);
        // 300 padding blocks that are never marked last: the walk stops instead of running on.
        Bytes many(files[3].begin(), files[3].begin() + 4 + 4 + 34);
        many[4] &= 0x7F;
        for (int i = 0; i < 300; ++i) { many.push_back(1); many.push_back(0); many.push_back(0); many.push_back(0); }
        CHECK(!DecodeFlac(many.data(), many.size(), a, nullptr));
        Probe(many);
        // An ID3 header that claims a huge tag.
        Bytes id3 = {'I', 'D', '3', 3, 0, 0, 0x7F, 0x7F, 0x7F, 0x7F};
        id3.insert(id3.end(), files[3].begin(), files[3].end());
        CHECK(!DecodeFlac(id3.data(), id3.size(), a, nullptr));
        Probe(id3);
        // Metadata only.
        Bytes head(files[3].begin(), files[3].begin() + static_cast<std::ptrdiff_t>(AudioStart(files[3])));
        CHECK(!DecodeFlac(head.data(), head.size(), a, nullptr));
    }

    RUN_SUITE("garbage");
    {
        HRng rng(31337);
        for (int it = 0; it < 400; ++it) {
            Bytes d(1 + rng.Below(3000));
            for (auto& b : d) b = static_cast<unsigned char>(rng.Next());
            Probe(d);
            if (d.size() >= 4) { d[0] = 'f'; d[1] = 'L'; d[2] = 'a'; d[3] = 'C'; Probe(d); }   // right magic, random rest
        }
        FlacAudio a;
        CHECK(!DecodeFlac(nullptr, 0, a, nullptr));
        const unsigned char one = 0;
        CHECK(!DecodeFlac(&one, 1, a, nullptr));
        FlacStream st;
        CHECK(!st.Open(nullptr, nullptr));
        // A valid header with random bytes after it.
        const Bytes head(files[3].begin(), files[3].begin() + static_cast<std::ptrdiff_t>(AudioStart(files[3])));
        for (int it = 0; it < 200; ++it) {
            Bytes d = head;
            const std::size_t extra = 1 + rng.Below(4000);
            for (std::size_t i = 0; i < extra; ++i) d.push_back(static_cast<unsigned char>(rng.Next()));
            Probe(d);
        }
    }

    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(secs < 120.0);   // no hang: all cases finish in a bounded time
    TEST_MAIN_RESULT();
}
