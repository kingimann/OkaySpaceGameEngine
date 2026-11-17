#include "okay/Audio/WavDecoder.hpp"
#include "okay/Audio/WavPlayback.hpp"
#include "okay/Core/ThirdPartySwitch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

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

// A float64 sample as float: NaN and infinity become 0 (as Sane), and a finite value too large for a float becomes
// the largest float of its sign instead of infinity (before 2026-11-16 it became infinity in DecodeWav's output).
// Written as selects on bit patterns, without branches, so the loops over it vectorise: a float64 NaN or infinity
// (exponent all ones) gives +0; otherwise a float that came out infinite can only be an overflow, and becomes
// 0x7F7FFFFF (the largest float) with the sign kept.
inline float F64Sample(double x) {
    std::uint64_t u; std::memcpy(&u, &x, 8);
    const float f = float(x);
    std::uint32_t b; std::memcpy(&b, &f, 4);
    b = (b & 0x7F800000u) == 0x7F800000u ? (b & 0x80000000u) | 0x7F7FFFFFu : b;
    b = (u & 0x7FF0000000000000ull) == 0x7FF0000000000000ull ? 0u : b;
    float r; std::memcpy(&r, &b, 4);
    return r;
}

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
    // Our decoder is the default (owner's approval, 2026-11-16). A build with -DOKAY_OWN_WAV=OFF defines
    // OKAY_OWN_WAV_DEFAULT_OFF and keeps the old path as its default for one more release.
    bool v =
#ifdef OKAY_OWN_WAV_DEFAULT_OFF
        false;
#else
        true;
#endif
    if (okay::ThirdPartyForced()) v = false;   // OKAY_THIRD_PARTY=1: every old path (OKAY_OWN_WAV still wins when set)
    if (const char* e = std::getenv("OKAY_OWN_WAV")) { if (e[0] == '1') v = true; else if (e[0] == '0') v = false; }
    return v;
}

namespace {

// Everything the chunk walk and the format checks found, and how big the output is. Built once, then decoded into any
// of the sinks below (float for DecodeWav, 16-bit or float bytes for playback).
struct Plan {
    Fmt f;
    const unsigned char* d = nullptr;      // start of the data chunk (WavStream: its read window)
    std::uint64_t dataOff = 0;             // file offset of the data chunk's first byte
    std::uint64_t dataLen = 0;
    std::size_t ch = 0;
    int kind = 0;                          // 0: PCM / float / A-law / mu-law, 1: MS ADPCM, 2: IMA ADPCM
    bool isFloat = false, isA = false, isMu = false;
    std::size_t sb = 0, stride = 0;        // kind 0: container bytes per sample, bytes per frame
    std::uint64_t frames = 0;
    std::size_t hdr = 0;                   // ADPCM: block header bytes
    std::uint64_t spb = 0, full = 0, rest = 0, restFrames = 0, groupsFull = 0, restGroups = 0;
    bool restBlock = false;
    std::uint64_t total = 0;               // output samples (frames * channels)
};

// The file's bytes as the chunk walk sees them: one buffer, or (LoadForPlayback on a large file) two, the bytes before
// the data chunk's samples in `a` and the bytes from the samples to the end of the file in `b`. The walk goes forward,
// so every chunk header and "fmt " body before the data chunk lies wholly in `a` and every one after it in `b`.
struct View {
    const unsigned char* a = nullptr;
    const unsigned char* b = nullptr;
    std::uint64_t split = ~std::uint64_t(0);   // file offset of b[0]; never reached with one buffer
    const unsigned char* At(std::uint64_t pos, std::size_t = 0) const { return pos < split ? a + pos : b + (pos - split); }
};

// The header bytes as WavStream sees them: read from the source on demand. At(pos, n) gives `n` bytes at `pos`, or null
// when the source gives fewer (a file cut under us). A chunk header (8 bytes) and a "fmt " body go into separate
// buffers; MakePlan uses each pointer before it asks for the next one of the same kind.
struct SourceView {
    static constexpr std::size_t kFmtMax = 22 + 256 * 4;   // the largest "fmt " body MakePlan reads (MS ADPCM table)
    AudioByteSource* src = nullptr;
    mutable unsigned char hdr[8] = {};
    mutable unsigned char body[kFmtMax] = {};
    const unsigned char* At(std::uint64_t pos, std::size_t n) const {
        if (n == 0) return body;                           // the data chunk's position only: nothing is read
        unsigned char* b = n <= sizeof hdr ? hdr : body;
        if (n > kFmtMax) return nullptr;
        return src->Read(pos, b, n) == n ? b : nullptr;
    }
};

// The chunk walk and every check that comes before decoding. Fills the same fields of `out` in the same order as the
// decoder always has (so a refused file reports the same partial header). Never touches out.samples.
// A template on the byte access: View (memory) for DecodeWav and playback, SourceView (reads) for WavStream.
template <class V_>
bool MakePlan(const V_& V, std::uint64_t size, const Limits& L, WavAudio& out, Plan& P, std::string* error) {
    if (size < 12) return Fail(error, "not a RIFF/WAVE file");
    const unsigned char* data = V.At(0, 12);
    if (!data) return Fail(error, "not a RIFF/WAVE file");
    if (size > L.maxInputBytes) return Fail(error, "input larger than the size budget");
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return Fail(error, "not a RIFF/WAVE file");

    // Walk the chunks once: remember the first "fmt " and the first "data" (either order).
    bool haveFmt = false, haveData = false;
    Fmt& f = P.f;
    std::uint64_t dataOff = 0, dataLen = 0;
    std::uint64_t pos = 12;
    int chunks = 0;
    while (pos + 8 <= size) {
        if (++chunks > L.maxChunks) return Fail(error, "too many chunks");
        const unsigned char* hd = V.At(pos, 8);
        if (!hd) return Fail(error, "file shorter than its size");
        const std::uint32_t sz = U32(hd + 4);
        const std::uint64_t body = pos + 8;
        if (std::memcmp(hd, "fmt ", 4) == 0 && !haveFmt) {
            if (sz < 16 || body + 16 > size) return Fail(error, "fmt chunk too short");
            const std::uint64_t avail = std::min<std::uint64_t>(sz, size - body);
            const unsigned char* p = V.At(body, std::size_t(std::min<std::uint64_t>(avail, SourceView::kFmtMax)));
            if (!p) return Fail(error, "file shorter than its size");
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
        } else if (std::memcmp(hd, "data", 4) == 0 && !haveData) {
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

    P.d = V.At(dataOff, 0);
    P.dataOff = dataOff;
    P.dataLen = dataLen;
    const std::size_t ch = std::size_t(f.channels);
    P.ch = ch;
    out.sampleRate = int(f.rate);
    out.channels = f.channels;
    out.bitsPerSample = f.bits;

    // Output size by encoding, checked against the budget BEFORE anything is allocated.
    auto budgetOk = [&](std::uint64_t frames) { return frames <= L.maxOutputSamples / ch; };

    if (f.tag == 0x0001 || f.tag == 0x0003 || f.tag == 0x0006 || f.tag == 0x0007) {
        P.kind = 0;
        P.isFloat = f.tag == 0x0003; P.isA = f.tag == 0x0006; P.isMu = f.tag == 0x0007;
        out.encoding = P.isFloat ? Encoding::Float : P.isA ? Encoding::ALaw : P.isMu ? Encoding::MuLaw : Encoding::Pcm;
        if (P.isFloat) { if (f.bits != 32 && f.bits != 64) return Fail(error, "float WAV must be 32 or 64 bit"); }
        else if (P.isA || P.isMu) { if (f.bits != 8) return Fail(error, "A-law and mu-law WAV must be 8 bit"); }
        else if (f.bits < 1 || f.bits > 32) return Fail(error, "unsupported PCM bit depth");
        P.sb = std::size_t((f.bits + 7) / 8);
        P.stride = f.blockAlign;
        if (P.stride < P.sb * ch) {                             // missing or wrong block align: use the natural one
            if (f.blockAlign != 0) out.warnings.push_back("block align smaller than one frame: ignored");
            P.stride = P.sb * ch;
        }
        P.frames = dataLen / P.stride;
        if (dataLen % P.stride) out.warnings.push_back("half a frame at the end of the data dropped");
        if (!budgetOk(P.frames)) return Fail(error, "decoded size over the budget");
        P.total = P.frames * ch;
        return true;
    }

    if (f.tag == 0x0002) {   // Microsoft ADPCM
        P.kind = 1;
        out.encoding = Encoding::MsAdpcm;
        out.bitsPerSample = f.bits ? f.bits : 4;
        if (ch > 2) return Fail(error, "MS ADPCM supports one or two channels");
        if (f.bits != 4) return Fail(error, "MS ADPCM must be 4 bit");
        P.hdr = 7 * ch;
        if (f.blockAlign < P.hdr || f.blockAlign > 65535) return Fail(error, "bad MS ADPCM block align");
        P.spb = std::uint64_t(f.blockAlign - P.hdr) * 2 / ch + 2;   // frames in a full block
        if (f.samplesPerBlock && f.samplesPerBlock != P.spb) out.warnings.push_back("samplesPerBlock disagrees with the block align: block align wins");
        P.full = dataLen / f.blockAlign; P.rest = dataLen % f.blockAlign;
        if (P.rest >= P.hdr) P.restFrames = 2 + (P.rest - P.hdr) * 2 / ch;
        P.frames = P.full * P.spb + P.restFrames;
        if (!budgetOk(P.frames)) return Fail(error, "decoded size over the budget");
        P.total = P.frames * ch;
        return true;
    }

    if (f.tag == 0x0011) {   // IMA / DVI ADPCM
        P.kind = 2;
        out.encoding = Encoding::ImaAdpcm;
        out.bitsPerSample = f.bits ? f.bits : 4;
        if (f.bits != 4) return Fail(error, "IMA ADPCM must be 4 bit");
        if (ch > 32) return Fail(error, "IMA ADPCM supports up to 32 channels here");
        P.hdr = 4 * ch;
        if (f.blockAlign < P.hdr || f.blockAlign > 65535 || (f.blockAlign - P.hdr) % P.hdr != 0)
            return Fail(error, "bad IMA ADPCM block align");
        P.groupsFull = (f.blockAlign - P.hdr) / P.hdr;
        P.spb = 1 + 8 * P.groupsFull;
        if (f.samplesPerBlock && f.samplesPerBlock != P.spb) out.warnings.push_back("samplesPerBlock disagrees with the block align: block align wins");
        P.full = dataLen / f.blockAlign; P.rest = dataLen % f.blockAlign;
        P.restGroups = P.rest >= P.hdr ? (P.rest - P.hdr) / P.hdr : 0;
        P.restBlock = P.rest >= P.hdr;
        P.frames = P.full * P.spb + (P.restBlock ? 1 + 8 * P.restGroups : 0);
        if (!budgetOk(P.frames)) return Fail(error, "decoded size over the budget");
        P.total = P.frames * ch;
        return true;
    }

    return Fail(error, "unsupported WAV format tag");
}

// G.711 lookup tables, filled once from the functions above (256 entries each).
struct LawTables {
    std::int16_t a[256], mu[256];
    LawTables() { for (unsigned i = 0; i < 256; ++i) { a[i] = std::int16_t(ALawToLinear(i)); mu[i] = std::int16_t(MuLawToLinear(i)); } }
};
const LawTables& Laws() { static const LawTables t; return t; }

constexpr bool kLittleEndianHost =
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    true;
#else
    false;
#endif

// Round to the nearest (ties to even, as std::nearbyint does in the default rounding mode) and clamp to 16 bits, for
// v * 32768. The value is clamped first: anything at or past the ends gives the end, as rounding first and then
// clamping does. Inside the range |r| < 2^15, adding and taking away 1.5 * 2^23 leaves the float rounded to an
// integer (the sum lies in [2^23, 2^24), where floats have no fraction bits), with the hardware's own
// round-to-nearest-even. Needs strict IEEE float (no -ffast-math, SSE not x87); checked against std::nearbyint on every
// float value (docs/own_wav_decoder.md, "Speed and memory against SDL").
inline std::int16_t RoundS16(float v) {
    const float r = v * 32768.0f;
    if (r <= -32768.0f) return std::int16_t(-32768);
    if (r >= 32767.0f) return std::int16_t(32767);
    if (!(r == r)) return std::int16_t(0);   // NaN (the decoder never makes one: Sane() removes them)
    const float big = 12582912.0f;           // 1.5 * 2^23
    return std::int16_t(int((r + big) - big));
}

// RoundS16 without branches, for loops the compiler can vectorise: the clamp is a min and a max (the ends round to
// themselves), the rounding the same 1.5 * 2^23 trick. Equal to RoundS16 for every float except NaN, which it must
// not be given (checked on all 2^32 bit patterns, docs/own_wav_decoder.md).
inline std::int16_t RoundS16NoNaN(float v) {
    float r = v * 32768.0f;
    r = r < -32768.0f ? -32768.0f : r;
    r = r > 32767.0f ? 32767.0f : r;
    const float big = 12582912.0f;
    return std::int16_t(int((r + big) - big));
}

// 24-bit signed integer x to 16 bits, exactly what RoundS16(float(x / 2^23)) gives (that float is exact, and so is
// x / 256): round half to even on the integer, then clamp. No branches (the low byte of real audio is noise, so a
// branch on it is mispredicted half the time).
inline std::int16_t Round24To16(std::int32_t x) {
    std::int32_t q = x >> 8;                 // floor (arithmetic shift)
    const std::int32_t r = x & 0xFF;
    q += std::int32_t(r > 128) | (std::int32_t(r == 128) & q & 1);
    return std::int16_t(q > 32767 ? 32767 : q);
}

// Sinks: where decoded samples go. Int(k, v) takes an integer sample whose value is v / 32768 (|v| <= 32768);
// Flt(k, v) a float sample; Wide(k, x, sb) a 3- or 4-byte signed PCM sample.
struct FloatSink {
    float* o;
    void Int(std::size_t k, int v) { o[k] = float(v) / 32768.0f; }
    void Flt(std::size_t k, float v) { o[k] = v; }
    void Wide(std::size_t k, std::int64_t x, std::size_t sb) { o[k] = float(double(x) / double(std::int64_t(1) << (8 * sb - 1))); }
};
struct S16Sink {
    unsigned char* o;
    void Put(std::size_t k, std::int16_t s) { std::memcpy(o + 2 * k, &s, 2); }
    void Int(std::size_t k, int v) { Put(k, std::int16_t(v)); }
    void Flt(std::size_t k, float v) { Put(k, RoundS16(v)); }
    void Wide(std::size_t k, std::int64_t x, std::size_t sb) {
        if (sb == 3) Put(k, Round24To16(std::int32_t(x)));
        else Put(k, RoundS16(float(double(x) / double(std::int64_t(1) << (8 * sb - 1)))));
    }
};
struct F32Sink {
    unsigned char* o;
    void Int(std::size_t k, int v) { const float x = float(v) / 32768.0f; std::memcpy(o + 4 * k, &x, 4); }
    void Flt(std::size_t k, float v) { std::memcpy(o + 4 * k, &v, 4); }
    void Wide(std::size_t k, std::int64_t x, std::size_t sb) { Flt(k, float(double(x) / double(std::int64_t(1) << (8 * sb - 1)))); }
};

template <class Sink> struct IsS16 { static constexpr bool value = false; };
template <> struct IsS16<S16Sink> { static constexpr bool value = true; };
template <class Sink> struct IsF32 { static constexpr bool value = false; };
template <> struct IsF32<F32Sink> { static constexpr bool value = true; };

// A float32 bit pattern with non-finite values (exponent all ones: infinity, NaN) made +0, as Sane() does.
inline std::uint32_t SaneBits(std::uint32_t u) { return (u & 0x7F800000u) == 0x7F800000u ? 0u : u; }

// DecodeWav's float output for packed (block align = one frame) PCM and float: one plain loop per
// format with no per-sample calls, that the compiler can turn into vector code. Each gives exactly the value the
// general loop gives (FloatSink): every scale is a power of two, so multiplying by its inverse is exact, and
// float(x) * 2^-31 equals float(x / 2^31) because scaling by a power of two commutes with rounding to float.
// Checked bit for bit by bench_wav_decoder --digest (docs/own_wav_decoder.md).
bool DecodeFlatFloatPacked(const Plan& P, float* o);
// The same for the 16-bit playback output, for the formats whose general loop goes through Wide() or RoundS16:
// 24 and 32-bit PCM and float64. Same values as S16Sink; safe in place like DecodeFlat (each sample read before its
// output is written; the output is narrower than the input).
bool DecodeFlatS16Packed(const Plan& P, unsigned char* o);

// PCM, float, A-law and mu-law. Each sample is read before it is written and output sample k never ends past the
// start of input sample k + 1 when the output is no wider than the container, so this also works in place.
template <class Sink>
void DecodeFlat(const Plan& P, Sink& s, unsigned char* raw) {
    const unsigned char* d = P.d;
    const std::size_t ch = P.ch, sb = P.sb, stride = P.stride;
    const std::size_t frames = std::size_t(P.frames);
    const bool packed = stride == sb * ch;
    if constexpr (std::is_same<Sink, FloatSink>::value) {
        if (packed && DecodeFlatFloatPacked(P, s.o)) return;
    }
    if constexpr (std::is_same<Sink, S16Sink>::value) {
        if (packed && DecodeFlatS16Packed(P, s.o)) return;
    }
    if (P.isFloat) {
        if (sb == 4) {
            if (IsF32<Sink>::value && packed) {
                // Float32 to float32: the bytes as they are with non-finite values made +0, in one pass. In place
                // (raw == d) or front to back with raw before d (one buffer): sample k is read before it is written,
                // and writing it never reaches a sample not yet read.
                const std::size_t n = std::size_t(P.total);
                for (std::size_t k = 0; k < n; ++k) {
                    std::uint32_t u; std::memcpy(&u, d + 4 * k, 4);
                    u = SaneBits(u);
                    std::memcpy(raw + 4 * k, &u, 4);
                }
                return;
            }
            for (std::size_t i = 0; i < frames; ++i)
                for (std::size_t c = 0; c < ch; ++c) { float x; std::memcpy(&x, d + i * stride + c * 4, 4); s.Flt(i * ch + c, Sane(x)); }
        } else {
            for (std::size_t i = 0; i < frames; ++i)
                for (std::size_t c = 0; c < ch; ++c) {
                    double x; std::memcpy(&x, d + i * stride + c * 8, 8);
                    s.Flt(i * ch + c, F64Sample(x));
                }
        }
        return;
    }
    if (P.isA || P.isMu) {
        const std::int16_t* t = P.isA ? Laws().a : Laws().mu;
        if (packed) { for (std::size_t k = 0; k < std::size_t(P.total); ++k) s.Int(k, t[d[k]]); return; }
        for (std::size_t i = 0; i < frames; ++i)
            for (std::size_t c = 0; c < ch; ++c) s.Int(i * ch + c, t[d[i * stride + c]]);
        return;
    }
    if (sb == 1) {
        if (packed) { for (std::size_t k = 0; k < std::size_t(P.total); ++k) s.Int(k, (int(d[k]) - 128) * 256); return; }
        for (std::size_t i = 0; i < frames; ++i)
            for (std::size_t c = 0; c < ch; ++c) s.Int(i * ch + c, (int(d[i * stride + c]) - 128) * 256);
        return;
    }
    if (sb == 2) {
        if (IsS16<Sink>::value && packed && kLittleEndianHost) { if (raw != d) std::memmove(raw, d, std::size_t(P.total) * 2); return; }
        if (packed) { for (std::size_t k = 0; k < std::size_t(P.total); ++k) s.Int(k, S16(d + 2 * k)); return; }
        for (std::size_t i = 0; i < frames; ++i)
            for (std::size_t c = 0; c < ch; ++c) s.Int(i * ch + c, S16(d + i * stride + c * 2));
        return;
    }
    // 3 or 4 bytes: little-endian, left-justified in the container, signed; scaled by the container width.
    for (std::size_t i = 0; i < frames; ++i)
        for (std::size_t c = 0; c < ch; ++c) {
            const unsigned char* q = d + i * stride + c * sb;
            std::int64_t x;
            if (sb == 3) x = std::int32_t((std::uint32_t(q[0]) << 8) | (std::uint32_t(q[1]) << 16) | (std::uint32_t(q[2]) << 24)) >> 8;
            else x = std::int32_t(U32(q));
            s.Wide(i * ch + c, x, sb);
        }
}

bool DecodeFlatFloatPacked(const Plan& P, float* o) {
    const unsigned char* d = P.d;
    const std::size_t n = std::size_t(P.total);
    if (P.isFloat) {
        if (P.sb == 4) {
            for (std::size_t k = 0; k < n; ++k) {
                std::uint32_t u; std::memcpy(&u, d + 4 * k, 4);
                u = SaneBits(u);
                std::memcpy(o + k, &u, 4);
            }
            return true;
        }
        for (std::size_t k = 0; k < n; ++k) {   // float64: see F64Sample
            double x; std::memcpy(&x, d + 8 * k, 8);
            o[k] = F64Sample(x);
        }
        return true;
    }
    if (P.isA || P.isMu) return false;   // the general packed loop (16-bit table, then scale) measured faster than a float table
    switch (P.sb) {
    case 1:   // unsigned 8 bit: (v - 128) * 256 / 32768
        for (std::size_t k = 0; k < n; ++k) o[k] = float(int(d[k]) - 128) * (1.0f / 128.0f);
        return true;
    case 2:
        if (!kLittleEndianHost) return false;
        for (std::size_t k = 0; k < n; ++k) { std::int16_t v; std::memcpy(&v, d + 2 * k, 2); o[k] = float(v) * (1.0f / 32768.0f); }
        return true;
    case 3: {
        // Little-endian hosts read 4 bytes at once (the sample and the next sample's first byte, shifted out); the
        // last sample is read byte by byte so nothing past the data chunk is touched.
        std::size_t k = 0;
        if (kLittleEndianHost && n > 0)
            for (; k + 1 < n; ++k) {
                std::uint32_t u; std::memcpy(&u, d + 3 * k, 4);
                o[k] = float(std::int32_t(u << 8) >> 8) * (1.0f / 8388608.0f);
            }
        for (; k < n; ++k) {
            const unsigned char* q = d + 3 * k;
            const std::int32_t x = std::int32_t((std::uint32_t(q[0]) << 8) | (std::uint32_t(q[1]) << 16) | (std::uint32_t(q[2]) << 24)) >> 8;
            o[k] = float(x) * (1.0f / 8388608.0f);
        }
        return true;
    }
    case 4:
        if (!kLittleEndianHost) return false;
        for (std::size_t k = 0; k < n; ++k) { std::int32_t v; std::memcpy(&v, d + 4 * k, 4); o[k] = float(v) * (1.0f / 2147483648.0f); }
        return true;
    default:
        return false;
    }
}

bool DecodeFlatS16Packed(const Plan& P, unsigned char* o) {
    if (!kLittleEndianHost) return false;
    const unsigned char* d = P.d;
    const std::size_t n = std::size_t(P.total);
    if (P.isFloat) {
        if (P.sb != 8) return false;
        for (std::size_t k = 0; k < n; ++k) {   // float64: F64Sample, then as RoundS16
            double x; std::memcpy(&x, d + 8 * k, 8);
            const std::int16_t v = RoundS16NoNaN(F64Sample(x));
            std::memcpy(o + 2 * k, &v, 2);
        }
        return true;
    }
    if (P.isA || P.isMu) return false;
    if (P.sb == 3) {
        std::size_t k = 0;
        for (; k + 1 < n; ++k) {   // 4 bytes at once (see DecodeFlatFloatPacked); the last sample byte by byte
            std::uint32_t u; std::memcpy(&u, d + 3 * k, 4);
            const std::int16_t v = Round24To16(std::int32_t(u << 8) >> 8);
            std::memcpy(o + 2 * k, &v, 2);
        }
        for (; k < n; ++k) {
            const unsigned char* q = d + 3 * k;
            const std::int16_t v = Round24To16(std::int32_t((std::uint32_t(q[0]) << 8) | (std::uint32_t(q[1]) << 16) | (std::uint32_t(q[2]) << 24)) >> 8);
            std::memcpy(o + 2 * k, &v, 2);
        }
        return true;
    }
    if (P.sb == 4) {   // 32-bit container: float(x) / 2^31, then as RoundS16
        for (std::size_t k = 0; k < n; ++k) {
            std::int32_t x; std::memcpy(&x, d + 4 * k, 4);
            const std::int16_t v = RoundS16NoNaN(float(x) * (1.0f / 2147483648.0f));
            std::memcpy(o + 2 * k, &v, 2);
        }
        return true;
    }
    return false;
}

// Microsoft ADPCM. Writes samples in order; on a bad predictor index it stops and *written says how many it wrote.
template <class Sink>
bool DecodeMs(const Plan& P, Sink& s, std::size_t* written, std::string* error) {
    const Fmt& f = P.f;
    const std::size_t ch = P.ch, hdr = P.hdr;
    const int nco = f.haveCoef ? f.numCoef : 7;
    std::size_t k = 0;
    for (std::uint64_t blk = 0; blk < P.full + (P.restFrames ? 1 : 0); ++blk) {
        const unsigned char* b = P.d + std::size_t(blk) * f.blockAlign;
        const std::size_t len = blk < P.full ? f.blockAlign : std::size_t(P.rest);
        int c1[2], c2[2], delta[2], s1[2], s2[2];
        for (std::size_t c = 0; c < ch; ++c) {
            const unsigned pi = b[c];
            if (int(pi) >= nco) { *written = k; return Fail(error, "MS ADPCM predictor index out of range"); }
            c1[c] = f.haveCoef ? f.coef[pi][0] : kMsStdCoef[pi][0];
            c2[c] = f.haveCoef ? f.coef[pi][1] : kMsStdCoef[pi][1];
            delta[c] = S16(b + ch + 2 * c);
            s1[c] = S16(b + 3 * ch + 2 * c);
            s2[c] = S16(b + 5 * ch + 2 * c);
        }
        for (std::size_t c = 0; c < ch; ++c) s.Int(k++, s2[c]);
        for (std::size_t c = 0; c < ch; ++c) s.Int(k++, s1[c]);
        const std::size_t bytes = (len - hdr) * 2 / ch * ch / 2;   // whole frames only (in bytes; ch nibbles per frame)
        const std::size_t nib = (len - hdr) * 2 / ch * ch;
        const unsigned char* q = b + hdr;
        auto one = [&](std::size_t c, int n) {
            const int sn = n >= 8 ? n - 16 : n;
            const int pred = int((std::int64_t(s1[c]) * c1[c] + std::int64_t(s2[c]) * c2[c]) >> 8);
            const int smp = Clamp16(std::int64_t(pred) + std::int64_t(sn) * delta[c]);
            s2[c] = s1[c]; s1[c] = smp;
            const std::int64_t nd = (std::int64_t(kMsAdapt[n]) * delta[c]) >> 8;
            delta[c] = int(nd < 16 ? 16 : nd > 1000000 ? 1000000 : nd);
            s.Int(k++, smp);
        };
        if (ch == 2) {   // high nibble: left, low nibble: right
            for (std::size_t j = 0; j < bytes; ++j) { one(0, q[j] >> 4); one(1, q[j] & 15); }
        } else {
            for (std::size_t j = 0; j < nib; ++j) one(0, (j & 1) ? int(q[j / 2] & 15u) : int(q[j / 2] >> 4));
        }
    }
    *written = k;
    return true;
}

// The IMA difference for every step index and the three magnitude bits of a nibble, worked out once with the
// description's own shifts and adds: step/8, plus step/4, step/2 and step for bits 0, 1 and 2.
struct ImaDiffTable {
    int diff[89][8];
    ImaDiffTable() {
        for (int i = 0; i < 89; ++i)
            for (int n = 0; n < 8; ++n) {
                const int step = kImaStep[i];
                int d = step >> 3;
                if (n & 1) d += step >> 2;
                if (n & 2) d += step >> 1;
                if (n & 4) d += step;
                diff[i][n] = d;
            }
    }
};

// IMA / DVI ADPCM. Fills every output sample (its order inside a block is channel by channel).
template <class Sink>
void DecodeIma(const Plan& P, Sink& s, std::vector<std::string>* warnings) {
    const Fmt& f = P.f;
    const std::size_t ch = P.ch, hdr = P.hdr;
    static const ImaDiffTable ima;
    std::size_t outFrame = 0;
    for (std::uint64_t blk = 0; blk < P.full + (P.restBlock ? 1 : 0); ++blk) {
        const unsigned char* b = P.d + std::size_t(blk) * f.blockAlign;
        const std::uint64_t groups = blk < P.full ? P.groupsFull : P.restGroups;
        int pred[32], idx[32];
        for (std::size_t c = 0; c < ch; ++c) {
            pred[c] = S16(b + 4 * c);
            idx[c] = b[4 * c + 2];
            if (idx[c] > 88) { idx[c] = 88; if (warnings) warnings->push_back("IMA step index above 88 clamped"); }
            s.Int(outFrame * ch + c, pred[c]);
        }
        for (std::size_t c = 0; c < ch; ++c) {
            int p = pred[c], ix = idx[c];
            for (std::uint64_t g = 0; g < groups; ++g) {
                const unsigned char* gb = b + hdr + (std::size_t(g) * ch + c) * 4;
                std::size_t o = (outFrame + 1 + std::size_t(g) * 8) * ch + c;
                for (int j = 0; j < 8; ++j, o += ch) {
                    const int n = (j & 1) ? (gb[j / 2] >> 4) : (gb[j / 2] & 15);   // low nibble first
                    const int diff = ima.diff[ix][n & 7];
                    p = (n & 8) ? p - diff : p + diff;
                    p = p < -32768 ? -32768 : p > 32767 ? 32767 : p;
                    ix += kImaIndexAdjust[n & 7];
                    ix = ix < 0 ? 0 : ix > 88 ? 88 : ix;
                    s.Int(o, p);
                }
            }
        }
        outFrame += 1 + std::size_t(groups) * 8;
    }
}

template <class Sink>
bool DecodePlan(const Plan& P, Sink& s, unsigned char* raw, std::size_t* written, std::vector<std::string>* warnings, std::string* error) {
    *written = std::size_t(P.total);
    if (P.kind == 0) { DecodeFlat(P, s, raw); return true; }
    if (P.kind == 1) return DecodeMs(P, s, written, error);
    DecodeIma(P, s, warnings);
    return true;
}

// The playback conversion. `dst` (output bytes) may be the same memory as the file bytes when InPlaceOk.
bool PlaybackFloat(const Plan& P, const WavAudio& meta) { return meta.encoding == Encoding::Float && P.f.bits == 32; }
bool InPlaceOk(const Plan& P, const WavAudio& meta) { return P.kind == 0 && !P.isA && !P.isMu && P.sb >= (PlaybackFloat(P, meta) ? 4u : 2u); }

bool DecodePlaybackInto(const Plan& P, const WavAudio& meta, unsigned char* dst, std::string* error) {
    std::size_t written = 0;
    if (PlaybackFloat(P, meta)) { F32Sink s{dst}; return DecodePlan(P, s, dst, &written, nullptr, error); }
    S16Sink s{dst};
    return DecodePlan(P, s, dst, &written, nullptr, error);
}

void FillClip(const Plan& P, const WavAudio& meta, PlaybackClip& out) {
    out.format = PlaybackFloat(P, meta) ? PlaybackFormat::F32 : PlaybackFormat::S16;
    out.channels = meta.channels;
    out.sampleRate = meta.sampleRate;
}

} // namespace

bool DecodeWav(const unsigned char* data, std::size_t size, WavAudio& out, std::string* error, const Limits* lim) {
    const Limits L = lim ? *lim : Limits();
    out = WavAudio();
    Plan P;
    if (!MakePlan(View{data}, size, L, out, P, error)) return false;
    out.samples.resize(std::size_t(P.total));
    FloatSink s{out.samples.data()};
    std::size_t written = 0;
    if (!DecodePlan(P, s, nullptr, &written, &out.warnings, error)) { out.samples.resize(written); return false; }
    return true;
}

std::vector<std::int16_t> ToS16(const WavAudio& a) {
    std::vector<std::int16_t> o(a.samples.size());
    for (std::size_t i = 0; i < o.size(); ++i) o[i] = RoundS16(a.samples[i]);
    return o;
}

bool DecodeForPlayback(const unsigned char* data, std::size_t size, PlaybackClip& out, std::string* error) {
    out = PlaybackClip{};
    if (!data || size == 0) { if (error) *error = "empty file"; return false; }
    WavAudio meta;
    Plan P;
    if (!MakePlan(View{data}, size, Limits(), meta, P, error)) return false;
    if (P.total == 0) { if (error) *error = "no samples"; return false; }
    std::vector<std::uint8_t> bytes(std::size_t(P.total) * (PlaybackFloat(P, meta) ? 4 : 2));
    if (!DecodePlaybackInto(P, meta, bytes.data(), error)) return false;
    FillClip(P, meta, out);
    out.bytes = std::move(bytes);
    return true;
}

namespace detail {
// LoadForPlayback's decode. `head` holds the first bytes of the file (all of it when `rest` is empty). When `rest` is
// not empty, rest[restAt..] holds the file from offset `split` (the first byte of the data chunk's samples) to its end.
// The output goes into the buffer that holds the samples whenever that is safe, so no second buffer is needed:
//   - output no wider than the input samples (PCM 16, 24 and 32 bit, float 32 and 64): decoded in place, front to
//     back (output sample k never ends past the start of input sample k + 1);
//   - 8-bit PCM, A-law and mu-law (2 output bytes per input byte): LoadForPlayback put the samples at rest[restAt..]
//     with restAt >= the data length, so output sample k (bytes 2k and 2k + 1) never reaches input sample k + 1 at
//     restAt + k + 1 or later.
// Anything else (ADPCM, or a layout these do not fit) is decoded into a new buffer.
bool DecodeFileForPlayback(std::vector<std::uint8_t>& head, std::vector<std::uint8_t>& rest, std::size_t restAt,
                           std::uint64_t split, std::uint64_t fileSize, PlaybackClip& out, std::string* error) {
    out = PlaybackClip{};
    if (fileSize == 0 || head.empty()) { if (error) *error = "empty file"; return false; }
    const bool twoBuffers = !rest.empty();
    View V{head.data()};
    if (twoBuffers) { V.b = rest.data() + restAt; V.split = split; }
    WavAudio meta;
    Plan P;
    if (!MakePlan(V, std::size_t(fileSize), Limits(), meta, P, error)) return false;
    if (P.total == 0) { if (error) *error = "no samples"; return false; }
    const std::size_t outBytes = std::size_t(P.total) * (PlaybackFloat(P, meta) ? 4 : 2);
    std::vector<std::uint8_t>& samples = twoBuffers ? rest : head;
    if (twoBuffers) { std::vector<std::uint8_t>().swap(head); }   // the plan has what it needed from the header
    const bool narrowInPlace = InPlaceOk(P, meta) && (!twoBuffers || restAt == 0);
    const bool widenInPlace = twoBuffers && P.kind == 0 && P.sb == 1 && !PlaybackFloat(P, meta) &&
                              restAt >= P.dataLen && outBytes <= rest.size();
    if (narrowInPlace || widenInPlace) {
        if (!DecodePlaybackInto(P, meta, samples.data(), error)) return false;
        samples.resize(outBytes);
        FillClip(P, meta, out);
        out.bytes = std::move(samples);
        return true;
    }
    std::vector<std::uint8_t> bytes(outBytes);
    if (!DecodePlaybackInto(P, meta, bytes.data(), error)) return false;
    FillClip(P, meta, out);
    out.bytes = std::move(bytes);
    return true;
}

// Where the data chunk's samples start, found by the same forward chunk walk MakePlan does, looking only at the first
// `n` bytes of a file of `size` bytes. 0 when the data chunk header is not among them. *widens is set when the first
// "fmt " chunk, before the data chunk, names a format whose playback output is twice its input (8-bit PCM, A-law,
// mu-law); it is only a hint for the buffer layout: MakePlan still decides everything.
std::uint64_t FindDataStart(const unsigned char* p, std::size_t n, std::uint64_t size, bool* widens) {
    *widens = false;
    if (n < 12 || std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) return 0;
    std::uint64_t pos = 12;
    bool haveFmt = false;
    for (int chunks = 0; pos + 8 <= n && pos + 8 <= size && chunks < Limits().maxChunks; ++chunks) {
        const std::uint32_t sz = U32(p + pos + 4);
        if (std::memcmp(p + pos, "data", 4) == 0) return pos + 8;
        if (std::memcmp(p + pos, "fmt ", 4) == 0 && !haveFmt) {
            haveFmt = true;
            const unsigned char* f = p + pos + 8;
            if (sz >= 16 && pos + 8 + 16 <= n) {
                unsigned tag = U16(f);
                const unsigned bits = U16(f + 14);
                if (tag == 0xFFFE && sz >= 40 && pos + 8 + 40 <= n) tag = U16(f + 24);
                *widens = (tag == 0x0001 && bits >= 1 && bits <= 8) || tag == 0x0006 || tag == 0x0007;
            }
        }
        pos = pos + 8 + sz + (sz & 1u);
    }
    return 0;
}
} // namespace detail

const char* ReaderLogLine(bool own) {
    return own ? "audio: WAV files load with our own reader (set OKAY_OWN_WAV=0 for the old one)"
               : "audio: WAV files load with the OLD reader (OKAY_OWN_WAV=0, OKAY_THIRD_PARTY=1 or a -DOKAY_OWN_WAV=OFF build)";
}

Limits StreamLimits() {
    Limits L;
    L.maxInputBytes = ~std::size_t(0);
    L.maxOutputSamples = ~std::uint64_t(0);
    return L;
}

// ---------------------------------------------------------------------------------------------------------------------
// WavStream. The header plan comes from MakePlan (through SourceView), exactly as DecodeWav's. Reading reuses DecodeWav's
// own decode functions on a second plan, `piece`, that describes only the bytes in the read window:
//   - PCM, float, A-law, mu-law: frames are independent, so frames [pos, pos + n) are the window's bytes
//     [pos * stride, (pos + n) * stride) of the data chunk, decoded as a data chunk of n frames;
//   - MS and IMA ADPCM: each block starts from its own header, so block b is decoded alone as a one-block data chunk
//     (a full block, or the file's last, shorter block with the same rest fields DecodeWav uses). A seek into a block
//     decodes the block and skips the frames before the target.
// Each output sample therefore comes from the same bytes through the same code as in DecodeWav.
// ---------------------------------------------------------------------------------------------------------------------
struct WavStream::Impl {
    std::shared_ptr<AudioByteSource> src;
    Plan P;                              // the whole file, as DecodeWav plans it (P.d is not used)
    Plan piece;                          // what is in `raw` right now
    WavAudio meta;                       // rate, channels, encoding, bits and the header warnings (no samples)
    std::uint64_t pos = 0;               // next frame
    std::uint64_t limit = ~std::uint64_t(0);   // frames from here on are never given (damage found while reading)
    std::vector<unsigned char> raw;      // the read window (at most kWindowBytes, or one ADPCM block)
    std::vector<float> block;            // ADPCM: one decoded block
    std::uint64_t blockIdx = ~std::uint64_t(0);
    std::size_t blockFrames = 0;
    std::vector<std::string> scratchWarnings;
    bool failed = false;
    std::string err;

    // The read window grows to what a Read needs, up to this (AudioStream reads 2048 frames: 8 KiB of 16-bit stereo).
    static constexpr std::size_t kWindowBytes = 16 * 1024;

    void Stop(std::uint64_t at, const char* why) {
        limit = std::min(limit, at);
        failed = true;
        if (err.empty()) err = why;
    }

    std::size_t FramesInBlock(std::uint64_t b) const {
        if (b < P.full) return std::size_t(P.spb);
        return P.kind == 1 ? std::size_t(P.restFrames) : std::size_t(1 + 8 * P.restGroups);
    }

    // Decodes block b into `out` (FramesInBlock(b) * channels floats). False when it cannot be read or is damaged;
    // the stream then ends at the block's first frame.
    bool DecodeBlock(std::uint64_t b, float* out) {
        const std::size_t len = b < P.full ? std::size_t(P.f.blockAlign) : std::size_t(P.rest);
        const std::uint64_t first = b * P.spb;
        if (raw.size() < len) raw.resize(len);
        if (src->Read(P.dataOff + b * P.f.blockAlign, raw.data(), len) != len) { Stop(first, "file shorter than its size"); return false; }
        piece.d = raw.data();
        if (b < P.full) { piece.full = 1; piece.rest = 0; piece.restFrames = 0; piece.restBlock = false; piece.restGroups = 0; }
        else { piece.full = 0; piece.rest = P.rest; piece.restFrames = P.restFrames; piece.restBlock = P.restBlock; piece.restGroups = P.restGroups; }
        piece.frames = FramesInBlock(b);
        piece.total = piece.frames * P.ch;
        FloatSink s{out};
        if (P.kind == 1) {
            std::size_t written = 0;
            std::string e;
            if (!DecodeMs(piece, s, &written, &e)) { Stop(first, "MS ADPCM predictor index out of range"); return false; }
        } else {
            scratchWarnings.clear();
            DecodeIma(piece, s, &scratchWarnings);
        }
        return true;
    }

    std::size_t ReadFlat(float* dst, std::size_t want) {
        const std::size_t ch = P.ch, stride = P.stride;
        const std::size_t perWindow = std::max<std::size_t>(1, kWindowBytes / stride);
        std::size_t done = 0;
        while (done < want) {
            const std::uint64_t end = std::min(P.frames, limit);
            if (pos >= end) break;
            const std::size_t n = std::size_t(std::min<std::uint64_t>({want - done, perWindow, end - pos}));
            const std::size_t bytes = n * stride;
            if (raw.size() < bytes) raw.resize(bytes);
            const std::size_t got = src->Read(P.dataOff + pos * stride, raw.data(), bytes);
            const std::size_t frames = got / stride;
            if (frames > 0) {
                piece.d = raw.data();
                piece.frames = frames;
                piece.total = std::uint64_t(frames) * ch;
                FloatSink s{dst + done * ch};
                DecodeFlat(piece, s, nullptr);
                done += frames;
                pos += frames;
            }
            if (got < bytes) { Stop(pos, "file shorter than its size"); break; }
        }
        return done;
    }

    std::size_t ReadAdpcm(float* dst, std::size_t want) {
        const std::size_t ch = P.ch;
        std::size_t done = 0;
        while (done < want) {
            const std::uint64_t end = std::min(P.frames, limit);
            if (pos >= end) break;
            const std::uint64_t b = pos / P.spb;
            const std::size_t off = std::size_t(pos - b * P.spb);
            const std::size_t inBlock = FramesInBlock(b);
            if (b != blockIdx && off == 0 && want - done >= inBlock) {
                // A whole block that fits: straight into the caller's buffer.
                if (!DecodeBlock(b, dst + done * ch)) break;
                done += inBlock;
                pos += inBlock;
                continue;
            }
            if (b != blockIdx) {
                block.resize(std::size_t(P.spb) * ch);
                blockIdx = ~std::uint64_t(0);
                if (!DecodeBlock(b, block.data())) break;
                blockIdx = b;
                blockFrames = inBlock;
            }
            const std::size_t n = std::min(want - done, blockFrames - off);
            std::memcpy(dst + done * ch, block.data() + off * ch, n * ch * sizeof(float));
            done += n;
            pos += n;
        }
        return done;
    }
};

WavStream::WavStream() = default;
WavStream::~WavStream() = default;

bool WavStream::Open(std::shared_ptr<AudioByteSource> src, std::string* error, const Limits* limits) {
    m_.reset();
    if (!src) return Fail(error, "no source");
    std::unique_ptr<Impl> m(new Impl);
    m->src = std::move(src);
    const Limits L = limits ? *limits : StreamLimits();
    SourceView V;
    V.src = m->src.get();
    if (!MakePlan(V, m->src->Size(), L, m->meta, m->P, error)) return false;
    m->P.d = nullptr;
    m->piece = m->P;
    m_ = std::move(m);
    return true;
}

bool WavStream::IsOpen() const { return m_ != nullptr; }
int WavStream::SampleRate() const { return m_ ? m_->meta.sampleRate : 0; }
int WavStream::Channels() const { return m_ ? m_->meta.channels : 0; }
Encoding WavStream::GetEncoding() const { return m_ ? m_->meta.encoding : Encoding::Pcm; }
int WavStream::BitsPerSample() const { return m_ ? m_->meta.bitsPerSample : 0; }
std::uint64_t WavStream::TotalFrames() const { return m_ ? m_->P.frames : 0; }
const std::vector<std::string>& WavStream::Warnings() const {
    static const std::vector<std::string> none;
    return m_ ? m_->meta.warnings : none;
}
std::uint64_t WavStream::Position() const { return m_ ? m_->pos : 0; }
bool WavStream::Failed() const { return m_ && m_->failed; }
const std::string& WavStream::Error() const {
    static const std::string none;
    return m_ ? m_->err : none;
}

std::size_t WavStream::Read(float* dst, std::size_t maxFrames) {
    if (!m_ || !dst || maxFrames == 0) return 0;
    return m_->P.kind == 0 ? m_->ReadFlat(dst, maxFrames) : m_->ReadAdpcm(dst, maxFrames);
}

bool WavStream::Seek(std::uint64_t frame) {
    if (!m_) return false;
    m_->pos = std::min(frame, m_->P.frames);
    return true;
}

std::size_t WavStream::MemoryBytes() const {
    return sizeof(*this) + (m_ ? sizeof(Impl) + m_->raw.capacity() + m_->block.capacity() * sizeof(float) : 0);
}

} // namespace wav
} // namespace okay
