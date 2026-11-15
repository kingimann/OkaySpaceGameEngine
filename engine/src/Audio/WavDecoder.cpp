#include "okay/Audio/WavDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace okay {
namespace wav {

namespace {

std::uint32_t U32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint16_t U16(const unsigned char* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
std::int16_t  S16(const unsigned char* p) { return static_cast<std::int16_t>(U16(p)); }

bool Fail(std::string* e, const char* m) { if (e) *e = m; return false; }

// The IMA / DVI ADPCM tables (public recommended practice).
const int kImaIndexAdjust[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int kImaStep[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};

// Microsoft ADPCM: the adaptation table and the seven standard coefficient pairs.
const int kMsAdapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
const int kMsStdCoef[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};

int Clamp16(std::int64_t v) { return int(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

// G.711, 16-bit linear output (the usual scaling: A-law spans +-32256, mu-law +-32124).
int ALawToLinear(unsigned a) {
    a ^= 0x55u;
    int t = int(a & 0x0Fu) << 4;
    const int seg = int((a & 0x70u) >> 4);
    if (seg == 0) t += 8;
    else { t += 0x108; if (seg > 1) t <<= (seg - 1); }
    return (a & 0x80u) ? t : -t;
}
int MuLawToLinear(unsigned u) {
    u = ~u & 0xFFu;
    const int exp = int((u & 0x70u) >> 4);
    int t = (((int(u & 0x0Fu)) << 3) + 0x84) << exp;
    t -= 0x84;
    return (u & 0x80u) ? -t : t;
}

float Sane(float v) { return std::isfinite(v) ? v : 0.0f; }

struct Fmt {
    unsigned tag = 0;          // effective format tag (sub-format for extensible)
    int channels = 0;
    std::uint32_t rate = 0;
    unsigned blockAlign = 0;
    int bits = 0;
    bool haveCoef = false;
    int numCoef = 0;
    int coef[256][2] = {};
    unsigned samplesPerBlock = 0;
};

} // namespace

bool OwnWavEnabled() {
    bool v =
#ifdef OKAY_OWN_WAV_DEFAULT
        true;
#else
        false;
#endif
    if (const char* e = std::getenv("OKAY_OWN_WAV")) { if (e[0] == '1') v = true; else if (e[0] == '0') v = false; }
    return v;
}

bool DecodeWav(const unsigned char* data, std::size_t size, WavAudio& out, std::string* error, const Limits* lim) {
    const Limits L = lim ? *lim : Limits();
    out = WavAudio();
    if (!data || size < 12) return Fail(error, "not a RIFF/WAVE file");
    if (size > L.maxInputBytes) return Fail(error, "input larger than the size budget");
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return Fail(error, "not a RIFF/WAVE file");

    // Walk the chunks once: remember the first "fmt " and the first "data" (either order).
    bool haveFmt = false, haveData = false;
    Fmt f;
    std::uint64_t dataOff = 0, dataLen = 0;
    std::uint64_t pos = 12;
    int chunks = 0;
    while (pos + 8 <= size) {
        if (++chunks > L.maxChunks) return Fail(error, "too many chunks");
        const std::uint32_t sz = U32(data + pos + 4);
        const std::uint64_t body = pos + 8;
        if (std::memcmp(data + pos, "fmt ", 4) == 0 && !haveFmt) {
            if (sz < 16 || body + 16 > size) return Fail(error, "fmt chunk too short");
            const std::uint64_t avail = std::min<std::uint64_t>(sz, size - body);
            const unsigned char* p = data + body;
            f.tag = U16(p); f.channels = U16(p + 2); f.rate = U32(p + 4); f.blockAlign = U16(p + 12); f.bits = U16(p + 14);
            if (avail >= 18) {
                const unsigned cb = U16(p + 16);
                if (f.tag == 0xFFFE) {
                    if (cb < 22 || avail < 40) return Fail(error, "extensible fmt chunk too short");
                    // The sub-format GUID is {tag as a 32-bit value, 0000-0010-8000-00AA00389B71}: bytes 2 to 15 are fixed.
                    static const unsigned char tail[14] = {0, 0, 0, 0, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
                    if (std::memcmp(p + 26, tail, 14) != 0) return Fail(error, "unknown extensible sub-format");
                    f.tag = U16(p + 24);
                    // p+18: valid bits per sample. The container size (f.bits) is what the data uses.
                } else if (f.tag == 0x0002 && avail >= 22) {
                    f.samplesPerBlock = U16(p + 18);
                    const unsigned n = U16(p + 20);
                    if (n > 256 || avail < 22 + std::uint64_t(n) * 4) return Fail(error, "MS ADPCM coefficient table cut");
                    f.numCoef = int(n); f.haveCoef = n > 0;
                    for (unsigned i = 0; i < n; ++i) { f.coef[i][0] = S16(p + 22 + i * 4); f.coef[i][1] = S16(p + 24 + i * 4); }
                } else if (f.tag == 0x0011 && avail >= 20) {
                    f.samplesPerBlock = U16(p + 18);
                }
            }
            haveFmt = true;
        } else if (std::memcmp(data + pos, "data", 4) == 0 && !haveData) {
            dataOff = body;
            dataLen = (body + sz <= size) ? sz : size - body;
            if (body + sz > size) out.warnings.push_back("data chunk longer than the file: cut at the end of the file");
            haveData = true;
        }
        pos = body + sz + (sz & 1u);
    }
    if (!haveFmt) return Fail(error, "missing fmt chunk");
    if (!haveData) return Fail(error, "missing data chunk");
    if (f.channels < 1) return Fail(error, "zero channels");
    if (f.channels > L.maxChannels) return Fail(error, "too many channels");
    if (f.rate < 1 || f.rate > L.maxSampleRate) return Fail(error, "sample rate out of range");

    const unsigned char* d = data + dataOff;
    const std::size_t ch = std::size_t(f.channels);
    out.sampleRate = int(f.rate);
    out.channels = f.channels;
    out.bitsPerSample = f.bits;

    // Output size by encoding, checked against the budget BEFORE allocating.
    auto budgetOk = [&](std::uint64_t frames) { return frames <= L.maxOutputSamples / ch; };

    if (f.tag == 0x0001 || f.tag == 0x0003 || f.tag == 0x0006 || f.tag == 0x0007) {
        const bool isFloat = f.tag == 0x0003, isA = f.tag == 0x0006, isMu = f.tag == 0x0007;
        out.encoding = isFloat ? Encoding::Float : isA ? Encoding::ALaw : isMu ? Encoding::MuLaw : Encoding::Pcm;
        if (isFloat) { if (f.bits != 32 && f.bits != 64) return Fail(error, "float WAV must be 32 or 64 bit"); }
        else if (isA || isMu) { if (f.bits != 8) return Fail(error, "A-law and mu-law WAV must be 8 bit"); }
        else if (f.bits < 1 || f.bits > 32) return Fail(error, "unsupported PCM bit depth");
        const std::size_t sb = std::size_t((f.bits + 7) / 8);   // container bytes per sample
        std::size_t stride = f.blockAlign;
        if (stride < sb * ch) {                                 // missing or wrong block align: use the natural one
            if (f.blockAlign != 0) out.warnings.push_back("block align smaller than one frame: ignored");
            stride = sb * ch;
        }
        std::uint64_t frames = dataLen / stride;
        if (dataLen % stride) out.warnings.push_back("half a frame at the end of the data dropped");
        if (!budgetOk(frames)) return Fail(error, "decoded size over the budget");
        out.samples.resize(std::size_t(frames) * ch);
        for (std::uint64_t i = 0; i < frames; ++i) {
            for (std::size_t c = 0; c < ch; ++c) {
                const unsigned char* s = d + std::size_t(i) * stride + c * sb;
                float v = 0.0f;
                if (isFloat) {
                    if (sb == 4) { float x; std::memcpy(&x, s, 4); v = Sane(x); }
                    else { double x; std::memcpy(&x, s, 8); v = std::isfinite(x) ? float(x) : 0.0f; }
                } else if (isA) v = float(ALawToLinear(s[0])) / 32768.0f;
                else if (isMu) v = float(MuLawToLinear(s[0])) / 32768.0f;
                else if (sb == 1) v = (int(s[0]) - 128) / 128.0f;
                else {
                    // Little-endian, left-justified in the container, signed; scale by the container width.
                    std::int64_t x = 0;
                    for (std::size_t b = 0; b < sb; ++b) x |= std::int64_t(s[b]) << (8 * b);
                    if (x & (std::int64_t(1) << (8 * sb - 1))) x -= std::int64_t(1) << (8 * sb);
                    v = float(double(x) / double(std::int64_t(1) << (8 * sb - 1)));
                }
                out.samples[std::size_t(i) * ch + c] = v;
            }
        }
        return true;
    }

    if (f.tag == 0x0002) {   // Microsoft ADPCM
        out.encoding = Encoding::MsAdpcm;
        out.bitsPerSample = f.bits ? f.bits : 4;
        if (ch > 2) return Fail(error, "MS ADPCM supports one or two channels");
        if (f.bits != 4) return Fail(error, "MS ADPCM must be 4 bit");
        const std::size_t hdr = 7 * ch;
        if (f.blockAlign < hdr || f.blockAlign > 65535) return Fail(error, "bad MS ADPCM block align");
        const std::uint64_t spb = std::uint64_t(f.blockAlign - hdr) * 2 / ch + 2;   // frames in a full block
        if (f.samplesPerBlock && f.samplesPerBlock != spb) out.warnings.push_back("samplesPerBlock disagrees with the block align: block align wins");
        const std::uint64_t full = dataLen / f.blockAlign, rest = dataLen % f.blockAlign;
        std::uint64_t restFrames = 0;
        if (rest >= hdr) restFrames = 2 + (rest - hdr) * 2 / ch;
        const std::uint64_t frames = full * spb + restFrames;
        if (!budgetOk(frames)) return Fail(error, "decoded size over the budget");
        out.samples.reserve(std::size_t(frames) * ch);
        const int nco = f.haveCoef ? f.numCoef : 7;
        for (std::uint64_t blk = 0; blk < full + (restFrames ? 1 : 0); ++blk) {
            const unsigned char* b = d + std::size_t(blk) * f.blockAlign;
            const std::size_t len = blk < full ? f.blockAlign : std::size_t(rest);
            int c1[2], c2[2], delta[2], s1[2], s2[2];
            for (std::size_t c = 0; c < ch; ++c) {
                const unsigned pi = b[c];
                if (int(pi) >= nco) return Fail(error, "MS ADPCM predictor index out of range");
                c1[c] = f.haveCoef ? f.coef[pi][0] : kMsStdCoef[pi][0];
                c2[c] = f.haveCoef ? f.coef[pi][1] : kMsStdCoef[pi][1];
                delta[c] = S16(b + ch + 2 * c);
                s1[c] = S16(b + 3 * ch + 2 * c);
                s2[c] = S16(b + 5 * ch + 2 * c);
            }
            for (std::size_t c = 0; c < ch; ++c) out.samples.push_back(float(s2[c]) / 32768.0f);
            for (std::size_t c = 0; c < ch; ++c) out.samples.push_back(float(s1[c]) / 32768.0f);
            const std::uint64_t nib = (len - hdr) * 2 / ch * ch;   // whole frames only
            for (std::uint64_t k = 0; k < nib; ++k) {
                const std::size_t c = std::size_t(k % ch);
                const unsigned byte = b[hdr + k / 2];
                const int n = (k & 1) ? int(byte & 15u) : int(byte >> 4);
                const int sn = n >= 8 ? n - 16 : n;
                const std::int64_t pred = (std::int64_t(s1[c]) * c1[c] + std::int64_t(s2[c]) * c2[c]) >> 8;
                const int smp = Clamp16(pred + std::int64_t(sn) * delta[c]);
                s2[c] = s1[c]; s1[c] = smp;
                std::int64_t nd = (std::int64_t(kMsAdapt[n]) * delta[c]) >> 8;
                delta[c] = int(nd < 16 ? 16 : nd > 1000000 ? 1000000 : nd);
                out.samples.push_back(float(smp) / 32768.0f);
            }
        }
        return true;
    }

    if (f.tag == 0x0011) {   // IMA / DVI ADPCM
        out.encoding = Encoding::ImaAdpcm;
        out.bitsPerSample = f.bits ? f.bits : 4;
        if (f.bits != 4) return Fail(error, "IMA ADPCM must be 4 bit");
        if (ch > 32) return Fail(error, "IMA ADPCM supports up to 32 channels here");
        const std::size_t hdr = 4 * ch;
        if (f.blockAlign < hdr || f.blockAlign > 65535 || (f.blockAlign - hdr) % hdr != 0)
            return Fail(error, "bad IMA ADPCM block align");
        const std::uint64_t groupsFull = (f.blockAlign - hdr) / hdr;
        const std::uint64_t spb = 1 + 8 * groupsFull;
        if (f.samplesPerBlock && f.samplesPerBlock != spb) out.warnings.push_back("samplesPerBlock disagrees with the block align: block align wins");
        const std::uint64_t full = dataLen / f.blockAlign, rest = dataLen % f.blockAlign;
        std::uint64_t restGroups = rest >= hdr ? (rest - hdr) / hdr : 0;
        const bool restBlock = rest >= hdr;
        const std::uint64_t frames = full * spb + (restBlock ? 1 + 8 * restGroups : 0);
        if (!budgetOk(frames)) return Fail(error, "decoded size over the budget");
        out.samples.assign(std::size_t(frames) * ch, 0.0f);
        std::size_t outFrame = 0;
        for (std::uint64_t blk = 0; blk < full + (restBlock ? 1 : 0); ++blk) {
            const unsigned char* b = d + std::size_t(blk) * f.blockAlign;
            const std::uint64_t groups = blk < full ? groupsFull : restGroups;
            int pred[32], idx[32];
            for (std::size_t c = 0; c < ch; ++c) {
                pred[c] = S16(b + 4 * c);
                idx[c] = b[4 * c + 2];
                if (idx[c] > 88) { idx[c] = 88; out.warnings.push_back("IMA step index above 88 clamped"); }
                out.samples[outFrame * ch + c] = float(pred[c]) / 32768.0f;
            }
            for (std::uint64_t g = 0; g < groups; ++g) {
                for (std::size_t c = 0; c < ch; ++c) {
                    const unsigned char* gb = b + hdr + (std::size_t(g) * ch + c) * 4;
                    for (int j = 0; j < 8; ++j) {
                        const int n = (j & 1) ? (gb[j / 2] >> 4) : (gb[j / 2] & 15);   // low nibble first
                        const int step = kImaStep[idx[c]];
                        int diff = step >> 3;
                        if (n & 1) diff += step >> 2;
                        if (n & 2) diff += step >> 1;
                        if (n & 4) diff += step;
                        pred[c] = Clamp16(std::int64_t(pred[c]) + ((n & 8) ? -diff : diff));
                        idx[c] += kImaIndexAdjust[n & 7];
                        idx[c] = idx[c] < 0 ? 0 : idx[c] > 88 ? 88 : idx[c];
                        out.samples[(outFrame + 1 + std::size_t(g) * 8 + std::size_t(j)) * ch + c] = float(pred[c]) / 32768.0f;
                    }
                }
            }
            outFrame += 1 + std::size_t(groups) * 8;
        }
        return true;
    }

    return Fail(error, "unsupported WAV format tag");
}

std::vector<std::int16_t> ToS16(const WavAudio& a) {
    std::vector<std::int16_t> o(a.samples.size());
    for (std::size_t i = 0; i < o.size(); ++i) {
        const float v = a.samples[i] * 32768.0f;
        const float r = std::nearbyint(v);
        o[i] = r <= -32768.0f ? std::int16_t(-32768) : r >= 32767.0f ? std::int16_t(32767) : std::int16_t(r);
    }
    return o;
}

} // namespace wav
} // namespace okay
