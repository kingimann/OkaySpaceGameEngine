#include "okay/Audio/WavDecoder.hpp"
#include "okay/Audio/AudioByteSource.hpp"

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

// The largest fmt body that is ever looked at: 22 bytes of MS ADPCM header plus 256 coefficient pairs.
constexpr std::size_t kFmtMax = 22 + 256 * 4;

// Where the samples are and how they are laid out: what the chunk walk and the format checks found. DecodeWav and
// WavStream both work from it, with the same decode functions below, so the two give the same samples bit for bit.
struct Layout {
    Fmt f;
    std::uint64_t dataOff = 0, dataLen = 0;
    Encoding enc = Encoding::Pcm;
    int bits = 0;                  // bitsPerSample as reported
    std::size_t ch = 0;
    std::size_t sampleBytes = 0;   // flat formats: container bytes per sample
    std::size_t stride = 0;        // flat formats: bytes per frame
    std::size_t hdr = 0;           // ADPCM: block header bytes
    std::uint64_t spb = 0;         // ADPCM: frames in a full block
    std::uint64_t fullBlocks = 0;  // ADPCM: whole blocks in the data
    std::uint64_t lastLen = 0;     // ADPCM: bytes of the short block at the end (0 when there is none)
    std::uint64_t lastFrames = 0;  // ADPCM: frames in that short block
    std::uint64_t frames = 0;      // frames in the whole data chunk
    bool Adpcm() const { return enc == Encoding::MsAdpcm || enc == Encoding::ImaAdpcm; }
    std::uint64_t Blocks() const { return fullBlocks + (lastFrames ? 1 : 0); }
    std::size_t BlockLen(std::uint64_t blk) const { return std::size_t(blk < fullBlocks ? f.blockAlign : lastLen); }
};

// Walks the chunks once and remembers the first "fmt " and the first "data" (either order). `read(offset, dst, n)`
// copies up to n bytes of the file and returns how many: memory for DecodeWav, a byte source for WavStream.
template <class ReadAt>
bool ReadLayout(ReadAt&& read, std::uint64_t size, const Limits& L, Layout& lay, std::vector<std::string>& warnings,
                std::string* error) {
    unsigned char head[12];
    if (size < 12 || read(0, head, 12) != 12) return Fail(error, "not a RIFF/WAVE file");
    if (std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0) return Fail(error, "not a RIFF/WAVE file");
    bool haveFmt = false, haveData = false;
    Fmt& f = lay.f;
    std::uint64_t pos = 12;
    int chunks = 0;
    while (pos + 8 <= size) {
        if (++chunks > L.maxChunks) return Fail(error, "too many chunks");
        unsigned char ck[8];
        if (read(pos, ck, 8) != 8) break;   // a byte source that shrank: the chunk list ends here
        const std::uint32_t sz = U32(ck + 4);
        const std::uint64_t body = pos + 8;
        if (std::memcmp(ck, "fmt ", 4) == 0 && !haveFmt) {
            if (sz < 16 || body + 16 > size) return Fail(error, "fmt chunk too short");
            const std::uint64_t avail = std::min<std::uint64_t>(sz, size - body);
            unsigned char p[kFmtMax] = {};
            const std::size_t n = std::size_t(std::min<std::uint64_t>(avail, kFmtMax));
            if (read(body, p, n) != n) return Fail(error, "fmt chunk too short");
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
                    const unsigned nc = U16(p + 20);
                    if (nc > 256 || avail < 22 + std::uint64_t(nc) * 4) return Fail(error, "MS ADPCM coefficient table cut");
                    f.numCoef = int(nc); f.haveCoef = nc > 0;
                    for (unsigned i = 0; i < nc; ++i) { f.coef[i][0] = S16(p + 22 + i * 4); f.coef[i][1] = S16(p + 24 + i * 4); }
                } else if (f.tag == 0x0011 && avail >= 20) {
                    f.samplesPerBlock = U16(p + 18);
                }
            }
            haveFmt = true;
        } else if (std::memcmp(ck, "data", 4) == 0 && !haveData) {
            lay.dataOff = body;
            lay.dataLen = (body + sz <= size) ? sz : size - body;
            if (body + sz > size) warnings.push_back("data chunk longer than the file: cut at the end of the file");
            haveData = true;
        }
        pos = body + sz + (sz & 1u);
    }
    if (!haveFmt) return Fail(error, "missing fmt chunk");
    if (!haveData) return Fail(error, "missing data chunk");
    return true;
}

// Checks the format against what this decoder reads and counts the frames. No output budget here: DecodeWav applies
// it before allocating; WavStream holds one block at a time, so the length of the file does not matter to it.
bool CheckFormat(Layout& lay, const Limits& L, std::vector<std::string>& warnings, std::string* error) {
    const Fmt& f = lay.f;
    if (f.channels < 1) return Fail(error, "zero channels");
    if (f.channels > L.maxChannels) return Fail(error, "too many channels");
    if (f.rate < 1 || f.rate > L.maxSampleRate) return Fail(error, "sample rate out of range");
    const std::size_t ch = std::size_t(f.channels);
    lay.ch = ch;
    lay.bits = f.bits;
    const std::uint64_t dataLen = lay.dataLen;

    if (f.tag == 0x0001 || f.tag == 0x0003 || f.tag == 0x0006 || f.tag == 0x0007) {
        const bool isFloat = f.tag == 0x0003, isA = f.tag == 0x0006, isMu = f.tag == 0x0007;
        lay.enc = isFloat ? Encoding::Float : isA ? Encoding::ALaw : isMu ? Encoding::MuLaw : Encoding::Pcm;
        if (isFloat) { if (f.bits != 32 && f.bits != 64) return Fail(error, "float WAV must be 32 or 64 bit"); }
        else if (isA || isMu) { if (f.bits != 8) return Fail(error, "A-law and mu-law WAV must be 8 bit"); }
        else if (f.bits < 1 || f.bits > 32) return Fail(error, "unsupported PCM bit depth");
        lay.sampleBytes = std::size_t((f.bits + 7) / 8);
        lay.stride = f.blockAlign;
        if (lay.stride < lay.sampleBytes * ch) {                // missing or wrong block align: use the natural one
            if (f.blockAlign != 0) warnings.push_back("block align smaller than one frame: ignored");
            lay.stride = lay.sampleBytes * ch;
        }
        lay.frames = dataLen / lay.stride;
        if (dataLen % lay.stride) warnings.push_back("half a frame at the end of the data dropped");
        return true;
    }

    if (f.tag == 0x0002) {   // Microsoft ADPCM
        lay.enc = Encoding::MsAdpcm;
        lay.bits = f.bits ? f.bits : 4;
        if (ch > 2) return Fail(error, "MS ADPCM supports one or two channels");
        if (f.bits != 4) return Fail(error, "MS ADPCM must be 4 bit");
        lay.hdr = 7 * ch;
        if (f.blockAlign < lay.hdr || f.blockAlign > 65535) return Fail(error, "bad MS ADPCM block align");
        lay.spb = std::uint64_t(f.blockAlign - lay.hdr) * 2 / ch + 2;   // frames in a full block
        if (f.samplesPerBlock && f.samplesPerBlock != lay.spb) warnings.push_back("samplesPerBlock disagrees with the block align: block align wins");
        lay.fullBlocks = dataLen / f.blockAlign;
        const std::uint64_t rest = dataLen % f.blockAlign;
        if (rest >= lay.hdr) { lay.lastLen = rest; lay.lastFrames = 2 + (rest - lay.hdr) * 2 / ch; }
        lay.frames = lay.fullBlocks * lay.spb + lay.lastFrames;
        return true;
    }

    if (f.tag == 0x0011) {   // IMA / DVI ADPCM
        lay.enc = Encoding::ImaAdpcm;
        lay.bits = f.bits ? f.bits : 4;
        if (f.bits != 4) return Fail(error, "IMA ADPCM must be 4 bit");
        if (ch > 32) return Fail(error, "IMA ADPCM supports up to 32 channels here");
        lay.hdr = 4 * ch;
        if (f.blockAlign < lay.hdr || f.blockAlign > 65535 || (f.blockAlign - lay.hdr) % lay.hdr != 0)
            return Fail(error, "bad IMA ADPCM block align");
        lay.spb = 1 + 8 * std::uint64_t((f.blockAlign - lay.hdr) / lay.hdr);
        if (f.samplesPerBlock && f.samplesPerBlock != lay.spb) warnings.push_back("samplesPerBlock disagrees with the block align: block align wins");
        lay.fullBlocks = dataLen / f.blockAlign;
        const std::uint64_t rest = dataLen % f.blockAlign;
        if (rest >= lay.hdr) { lay.lastLen = rest; lay.lastFrames = 1 + 8 * ((rest - lay.hdr) / lay.hdr); }
        lay.frames = lay.fullBlocks * lay.spb + lay.lastFrames;
        return true;
    }

    return Fail(error, "unsupported WAV format tag");
}

// `frames` frames of a flat (non-ADPCM) encoding starting at `d` to interleaved floats.
void DecodeFlatFrames(const Layout& lay, const unsigned char* d, std::uint64_t frames, float* out) {
    const std::size_t ch = lay.ch, sb = lay.sampleBytes, stride = lay.stride;
    const Encoding e = lay.enc;
    for (std::uint64_t i = 0; i < frames; ++i) {
        for (std::size_t c = 0; c < ch; ++c) {
            const unsigned char* s = d + std::size_t(i) * stride + c * sb;
            float v = 0.0f;
            if (e == Encoding::Float) {
                if (sb == 4) { float x; std::memcpy(&x, s, 4); v = Sane(x); }
                else { double x; std::memcpy(&x, s, 8); v = std::isfinite(x) ? float(x) : 0.0f; }
            } else if (e == Encoding::ALaw) v = float(ALawToLinear(s[0])) / 32768.0f;
            else if (e == Encoding::MuLaw) v = float(MuLawToLinear(s[0])) / 32768.0f;
            else if (sb == 1) v = (int(s[0]) - 128) / 128.0f;
            else {
                // Little-endian, left-justified in the container, signed; scale by the container width.
                std::int64_t x = 0;
                for (std::size_t b = 0; b < sb; ++b) x |= std::int64_t(s[b]) << (8 * b);
                if (x & (std::int64_t(1) << (8 * sb - 1))) x -= std::int64_t(1) << (8 * sb);
                v = float(double(x) / double(std::int64_t(1) << (8 * sb - 1)));
            }
            out[std::size_t(i) * ch + c] = v;
        }
    }
}

// One ADPCM block of `len` bytes (a full block, or the short one at the end) to interleaved floats. `out` has room for
// lay.spb frames. Sets `frames`; counts IMA step indexes above 88 that were clamped in `clamped`.
bool DecodeAdpcmBlock(const Layout& lay, const unsigned char* b, std::size_t len, float* out, std::size_t& frames,
                 int& clamped, std::string* error) {
    const Fmt& f = lay.f;
    const std::size_t ch = lay.ch, hdr = lay.hdr;
    frames = 0;
    if (len < hdr) return true;
    if (lay.enc == Encoding::MsAdpcm) {
        const int nco = f.haveCoef ? f.numCoef : 7;
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
        std::size_t k0 = 0;
        for (std::size_t c = 0; c < ch; ++c) out[k0++] = float(s2[c]) / 32768.0f;
        for (std::size_t c = 0; c < ch; ++c) out[k0++] = float(s1[c]) / 32768.0f;
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
            out[k0++] = float(smp) / 32768.0f;
        }
        frames = 2 + std::size_t(nib / ch);
        return true;
    }
    // IMA / DVI ADPCM
    const std::size_t groups = (len - hdr) / hdr;
    int pred[32], idx[32];
    for (std::size_t c = 0; c < ch; ++c) {
        pred[c] = S16(b + 4 * c);
        idx[c] = b[4 * c + 2];
        if (idx[c] > 88) { idx[c] = 88; ++clamped; }
        out[c] = float(pred[c]) / 32768.0f;
    }
    for (std::size_t g = 0; g < groups; ++g) {
        for (std::size_t c = 0; c < ch; ++c) {
            const unsigned char* gb = b + hdr + (g * ch + c) * 4;
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
                out[(1 + g * 8 + std::size_t(j)) * ch + c] = float(pred[c]) / 32768.0f;
            }
        }
    }
    frames = 1 + groups * 8;
    return true;
}

const char* const kImaClampWarning = "IMA step index above 88 clamped";

} // namespace

bool DecodeWav(const unsigned char* data, std::size_t size, WavAudio& out, std::string* error, const Limits* lim) {
    const Limits L = lim ? *lim : Limits();
    out = WavAudio();
    if (!data || size < 12) return Fail(error, "not a RIFF/WAVE file");
    if (size > L.maxInputBytes) return Fail(error, "input larger than the size budget");
    auto read = [data, size](std::uint64_t off, unsigned char* dst, std::size_t n) -> std::size_t {
        if (off >= size) return 0;
        n = std::size_t(std::min<std::uint64_t>(n, size - off));
        std::memcpy(dst, data + off, n);
        return n;
    };
    Layout lay;
    if (!ReadLayout(read, size, L, lay, out.warnings, error)) return false;
    if (!CheckFormat(lay, L, out.warnings, error)) return false;
    out.sampleRate = int(lay.f.rate);
    out.channels = lay.f.channels;
    out.encoding = lay.enc;
    out.bitsPerSample = lay.bits;
    // Output size checked against the budget BEFORE allocating.
    if (lay.frames > L.maxOutputSamples / lay.ch) return Fail(error, "decoded size over the budget");
    out.samples.assign(std::size_t(lay.frames) * lay.ch, 0.0f);
    const unsigned char* d = data + lay.dataOff;
    if (!lay.Adpcm()) { DecodeFlatFrames(lay, d, lay.frames, out.samples.data()); return true; }
    std::size_t outFrame = 0;
    for (std::uint64_t blk = 0; blk < lay.Blocks(); ++blk) {
        std::size_t got = 0;
        int clamped = 0;
        if (!DecodeAdpcmBlock(lay, d + std::size_t(blk) * lay.f.blockAlign, lay.BlockLen(blk), out.samples.data() + outFrame * lay.ch,
                         got, clamped, error))
            return false;
        for (int i = 0; i < clamped; ++i) out.warnings.push_back(kImaClampWarning);
        outFrame += got;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// WavStream: the same layout and decode functions, fed from a byte source a window (flat formats) or a block (ADPCM)
// at a time.
// ---------------------------------------------------------------------------------------------------------------------

struct WavStream::Impl {
    std::shared_ptr<AudioByteSource> src;
    Layout lay;
    std::vector<std::string> warnings;
    std::string error;
    std::vector<unsigned char> raw;     // one read window (flat) or one block (ADPCM)
    std::vector<float> pcm;             // ADPCM: the decoded current block
    std::uint64_t curBlock = ~std::uint64_t(0);
    std::size_t curFrames = 0;
    std::uint64_t pos = 0;
    bool stopped = false;               // the source ended early or a block was damaged: nothing more until a Seek
    bool warnedClamp = false;

    static constexpr std::size_t kWindowBytes = 64 * 1024;

    std::size_t WindowFrames() const { return std::max<std::size_t>(1, kWindowBytes / lay.stride); }

    void Stop(const char* why) { stopped = true; error = why; }

    bool LoadBlock(std::uint64_t blk) {
        const std::size_t len = lay.BlockLen(blk);
        if (src->Read(lay.dataOff + blk * lay.f.blockAlign, raw.data(), len) != len) { Stop("the file ended before its data chunk did"); return false; }
        int clamped = 0;
        std::string e;
        if (!DecodeAdpcmBlock(lay, raw.data(), len, pcm.data(), curFrames, clamped, &e)) { curBlock = ~std::uint64_t(0); stopped = true; error = e; return false; }
        if (clamped && !warnedClamp) { warnings.push_back(kImaClampWarning); warnedClamp = true; }
        curBlock = blk;
        return true;
    }

    std::size_t Read(float* dst, std::size_t maxFrames) {
        const std::size_t ch = lay.ch;
        std::size_t done = 0;
        while (done < maxFrames && pos < lay.frames && !stopped) {
            if (!lay.Adpcm()) {
                const std::size_t n = std::size_t(std::min<std::uint64_t>(std::min(maxFrames - done, WindowFrames()), lay.frames - pos));
                const std::size_t got = src->Read(lay.dataOff + pos * lay.stride, raw.data(), n * lay.stride);
                const std::size_t frames = got / lay.stride;
                DecodeFlatFrames(lay, raw.data(), frames, dst + done * ch);
                done += frames;
                pos += frames;
                if (frames < n) Stop("the file ended before its data chunk did");
                continue;
            }
            const std::uint64_t blk = pos / lay.spb;
            if (blk != curBlock && !LoadBlock(blk)) break;
            const std::size_t off = std::size_t(pos - blk * lay.spb);
            if (off >= curFrames) { Stop("ADPCM block shorter than its header says"); break; }
            const std::size_t n = std::min(maxFrames - done, curFrames - off);
            std::memcpy(dst + done * ch, pcm.data() + off * ch, n * ch * sizeof(float));
            done += n;
            pos += n;
        }
        return done;
    }
};

WavStream::WavStream() = default;
WavStream::~WavStream() = default;
WavStream::WavStream(WavStream&&) noexcept = default;
WavStream& WavStream::operator=(WavStream&&) noexcept = default;

bool WavStream::Open(std::shared_ptr<AudioByteSource> src, std::string* error, const Limits* limits) {
    m_.reset();
    if (!src) return Fail(error, "no data");
    const Limits L = limits ? *limits : Limits();
    std::unique_ptr<Impl> s(new Impl);
    s->src = std::move(src);
    AudioByteSource& bs = *s->src;
    auto read = [&bs](std::uint64_t off, unsigned char* dst, std::size_t n) -> std::size_t { return bs.Read(off, dst, n); };
    if (!ReadLayout(read, bs.Size(), L, s->lay, s->warnings, error)) return false;
    if (!CheckFormat(s->lay, L, s->warnings, error)) return false;
    if (s->lay.Adpcm()) {
        s->raw.resize(s->lay.f.blockAlign);
        s->pcm.resize(std::size_t(s->lay.spb) * s->lay.ch);
    } else {
        s->raw.resize(s->WindowFrames() * s->lay.stride);
    }
    m_ = std::move(s);
    return true;
}

bool WavStream::IsOpen() const { return m_ != nullptr; }
int WavStream::SampleRate() const { return m_ ? int(m_->lay.f.rate) : 0; }
int WavStream::Channels() const { return m_ ? m_->lay.f.channels : 0; }
Encoding WavStream::GetEncoding() const { return m_ ? m_->lay.enc : Encoding::Pcm; }
int WavStream::BitsPerSample() const { return m_ ? m_->lay.bits : 0; }
std::uint64_t WavStream::TotalFrames() const { return m_ ? m_->lay.frames : 0; }
std::uint64_t WavStream::Position() const { return m_ ? m_->pos : 0; }

std::size_t WavStream::Read(float* dst, std::size_t maxFrames) {
    if (!m_ || !dst) return 0;
    return m_->Read(dst, maxFrames);
}

bool WavStream::Seek(std::uint64_t frame) {
    if (!m_) return false;
    m_->pos = std::min(frame, m_->lay.frames);
    m_->stopped = false;
    m_->error.clear();
    return true;
}

const std::string& WavStream::Error() const {
    static const std::string none;
    return m_ ? m_->error : none;
}
const std::vector<std::string>& WavStream::Warnings() const {
    static const std::vector<std::string> none;
    return m_ ? m_->warnings : none;
}
std::size_t WavStream::MemoryBytes() const {
    if (!m_) return sizeof(*this);
    return sizeof(*this) + sizeof(Impl) + m_->raw.capacity() + m_->pcm.capacity() * sizeof(float);
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

const int* ImaAdpcmSteps() { return kImaStep; }
const int* ImaAdpcmIndexAdjust() { return kImaIndexAdjust; }

} // namespace wav
} // namespace okay
