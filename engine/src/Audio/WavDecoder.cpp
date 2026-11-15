#include "okay/Audio/WavDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

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
std::uint64_t U64(const unsigned char* p) { return std::uint64_t(U32(p)) | (std::uint64_t(U32(p + 4)) << 32); }

// Everything the header says plus the block layout the decoders need.
struct Layout {
    unsigned tag = 0;          // effective format tag (sub-format for extensible)
    int channels = 0;
    std::size_t ch = 0;
    std::uint32_t rate = 0;
    unsigned blockAlign = 0;
    int bits = 0;
    bool haveCoef = false;
    int numCoef = 0;
    int coef[256][2] = {};
    unsigned samplesPerBlock = 0;
    std::uint64_t dataOff = 0, dataLen = 0;
    // PCM, float, A-law, mu-law
    bool isFloat = false, isA = false, isMu = false, adpcm = false;
    std::size_t sb = 0, stride = 0;
    // ADPCM: header bytes, frames of a full block, number of full blocks, bytes and frames of the last partial block
    std::size_t hdr = 0;
    std::uint64_t spb = 0, full = 0, rest = 0, restFrames = 0, groupsFull = 0;
    bool restBlock = false;
    std::uint64_t totalFrames = 0;
    std::uint64_t Units() const { return full + (restBlock ? 1 : 0); }
};

bool IsPcmTag(unsigned t) { return t == 0x0001 || t == 0x0003 || t == 0x0006 || t == 0x0007; }

// Chunk walk and format checks over a byte source (the whole file for DecodeWav, the file on disk for a stream).
// Nothing is read twice and nothing larger than a few KB is held (marker lists are bounded by Limits::maxMarkers).
bool ParseHeader(AudioByteSource& src, const Limits& L, WavMeta& m, Layout& f, std::string* error) {
    const std::uint64_t size = src.Size();
    unsigned char head[12];
    if (size < 12 || src.Read(0, head, 12) != 12) return Fail(error, "not a RIFF/WAVE file");
    if (size > L.maxInputBytes) return Fail(error, "input larger than the size budget");
    const bool isRiff = std::memcmp(head, "RIFF", 4) == 0;
    const bool isRf64 = std::memcmp(head, "RF64", 4) == 0 || std::memcmp(head, "BW64", 4) == 0;
    if ((!isRiff && !isRf64) || std::memcmp(head + 8, "WAVE", 4) != 0) return Fail(error, "not a RIFF/WAVE file");
    m.rf64 = isRf64;

    bool haveFmt = false, haveData = false, haveDs64 = false;
    std::uint64_t ds64Data = 0;
    struct Ds64Entry { unsigned char id[4]; std::uint64_t size; };
    std::vector<Ds64Entry> ds64Table;
    std::uint64_t pos = 12;
    int chunks = 0;
    std::vector<unsigned char> buf;
    while (pos <= size && size - pos >= 8) {
        if (++chunks > L.maxChunks) return Fail(error, "too many chunks");
        unsigned char ch8[8];
        if (src.Read(pos, ch8, 8) != 8) break;
        std::uint64_t sz = U32(ch8 + 4);
        const std::uint64_t body = pos + 8;
        const std::uint64_t room = size - body;   // bytes of the file after this chunk header
        const bool isFmt = std::memcmp(ch8, "fmt ", 4) == 0, isData = std::memcmp(ch8, "data", 4) == 0;
        if (isRf64 && sz == 0xFFFFFFFFu) {   // the real size is in ds64: data has its own field, the others are in the table
            if (isData) {
                if (!haveDs64) return Fail(error, "RF64 data chunk without a ds64 chunk before it");
                sz = ds64Data;
            } else {
                for (const auto& e : ds64Table) if (std::memcmp(e.id, ch8, 4) == 0) { sz = e.size; break; }
            }
        }
        const std::uint64_t avail = std::min<std::uint64_t>(sz, room);
        if (isRf64 && !haveDs64 && std::memcmp(ch8, "ds64", 4) == 0) {
            // riffSize(8) dataSize(8) sampleCount(8) tableLength(4), then tableLength * {id(4) size(8)}.
            // Only dataSize and the table are used: the RIFF size and the sample count are not trusted.
            if (sz < 28 || room < 28) return Fail(error, "ds64 chunk too short");
            const std::uint64_t want = std::min<std::uint64_t>(avail, 28 + 12 * std::uint64_t(1024));
            buf.resize(std::size_t(want));
            if (src.Read(body, buf.data(), buf.size()) != buf.size()) return Fail(error, "ds64 chunk too short");
            ds64Data = U64(buf.data() + 8);
            const std::uint64_t declared = U32(buf.data() + 24);
            const std::uint64_t fit = (want - 28) / 12;
            const std::uint64_t n = std::min(declared, fit);
            if (declared > n) m.warnings.push_back("ds64 size table cut");
            for (std::uint64_t i = 0; i < n; ++i) {
                Ds64Entry e;
                std::memcpy(e.id, buf.data() + 28 + 12 * i, 4);
                e.size = U64(buf.data() + 32 + 12 * i);
                ds64Table.push_back(e);
            }
            haveDs64 = true;
        } else if (isFmt && !haveFmt) {
            if (sz < 16 || room < 16) return Fail(error, "fmt chunk too short");
            const std::uint64_t av = std::min<std::uint64_t>(avail, 2048);
            buf.resize(std::size_t(av));
            if (src.Read(body, buf.data(), buf.size()) != buf.size()) return Fail(error, "fmt chunk too short");
            const unsigned char* p = buf.data();
            f.tag = U16(p); f.channels = U16(p + 2); f.rate = U32(p + 4); f.blockAlign = U16(p + 12); f.bits = U16(p + 14);
            if (av >= 18) {
                const unsigned cb = U16(p + 16);
                if (f.tag == 0xFFFE) {
                    if (cb < 22 || av < 40) return Fail(error, "extensible fmt chunk too short");
                    // The sub-format GUID is {tag as a 32-bit value, 0000-0010-8000-00AA00389B71}: bytes 2 to 15 are fixed.
                    static const unsigned char tail[14] = {0, 0, 0, 0, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
                    if (std::memcmp(p + 26, tail, 14) != 0) return Fail(error, "unknown extensible sub-format");
                    f.tag = U16(p + 24);
                    // p+18: valid bits per sample. The container size (f.bits) is what the data uses.
                } else if (f.tag == 0x0002 && av >= 22) {
                    f.samplesPerBlock = U16(p + 18);
                    const unsigned n = U16(p + 20);
                    if (n > 256 || av < 22 + std::uint64_t(n) * 4) return Fail(error, "MS ADPCM coefficient table cut");
                    f.numCoef = int(n); f.haveCoef = n > 0;
                    for (unsigned i = 0; i < n; ++i) { f.coef[i][0] = S16(p + 22 + i * 4); f.coef[i][1] = S16(p + 24 + i * 4); }
                } else if (f.tag == 0x0011 && av >= 20) {
                    f.samplesPerBlock = U16(p + 18);
                }
            }
            haveFmt = true;
        } else if (isData && !haveData) {
            f.dataOff = body;
            f.dataLen = avail;
            if (sz > room) m.warnings.push_back("data chunk longer than the file: cut at the end of the file");
            haveData = true;
        } else if (std::memcmp(ch8, "smpl", 4) == 0 && !m.sampler.present && sz >= 36 && room >= 36) {
            // manufacturer product samplePeriod unityNote pitchFraction smpteFormat smpteOffset loopCount samplerData,
            // then loopCount * {id type start end fraction playCount} (24 bytes each).
            const std::uint64_t fit = (std::min<std::uint64_t>(avail, std::uint64_t(36) + 24u * std::uint64_t(L.maxMarkers)) - 36) / 24;
            buf.resize(std::size_t(36 + 24 * fit));
            if (src.Read(body, buf.data(), buf.size()) == buf.size()) {
                const unsigned char* p = buf.data();
                WavSampler& s = m.sampler;
                s.present = true;
                s.manufacturer = U32(p); s.product = U32(p + 4); s.samplePeriodNs = U32(p + 8); s.midiUnityNote = U32(p + 12);
                s.midiPitchFraction = U32(p + 16); s.smpteFormat = U32(p + 20); s.smpteOffset = U32(p + 24);
                const std::uint64_t declared = U32(p + 28);
                if (declared > fit) m.warnings.push_back("smpl loop list cut (more loops declared than the chunk holds or the limit allows)");
                for (std::uint64_t i = 0; i < std::min(declared, fit); ++i) {
                    const unsigned char* q = p + 36 + 24 * i;
                    WavLoop lp;
                    lp.id = U32(q); lp.type = U32(q + 4); lp.start = U32(q + 8); lp.end = U32(q + 12); lp.fraction = U32(q + 16); lp.playCount = U32(q + 20);
                    s.loops.push_back(lp);
                }
            }
        } else if (std::memcmp(ch8, "cue ", 4) == 0 && m.cues.empty() && sz >= 4 && room >= 4) {
            // pointCount, then pointCount * {id position dataChunkId chunkStart blockStart sampleOffset} (24 bytes each).
            const std::uint64_t fit = (std::min<std::uint64_t>(avail, std::uint64_t(4) + 24u * std::uint64_t(L.maxMarkers)) - 4) / 24;
            buf.resize(std::size_t(4 + 24 * fit));
            if (src.Read(body, buf.data(), buf.size()) == buf.size()) {
                const unsigned char* p = buf.data();
                const std::uint64_t declared = U32(p);
                if (declared > fit) m.warnings.push_back("cue list cut (more points declared than the chunk holds or the limit allows)");
                for (std::uint64_t i = 0; i < std::min(declared, fit); ++i) {
                    const unsigned char* q = p + 4 + 24 * i;
                    WavCue c;
                    c.id = U32(q); c.position = U32(q + 4); std::memcpy(c.dataChunk, q + 8, 4);
                    c.chunkStart = U32(q + 12); c.blockStart = U32(q + 16); c.sampleOffset = U32(q + 20);
                    m.cues.push_back(c);
                }
            }
        }
        if (sz > room) break;               // this chunk runs to (or past) the end of the file: nothing follows
        pos = body + sz + (sz & 1u);
    }
    if (!haveFmt) return Fail(error, "missing fmt chunk");
    if (!haveData) return Fail(error, "missing data chunk");
    if (f.channels < 1) return Fail(error, "zero channels");
    if (f.channels > L.maxChannels) return Fail(error, "too many channels");
    if (f.rate < 1 || f.rate > L.maxSampleRate) return Fail(error, "sample rate out of range");

    const std::size_t ch = std::size_t(f.channels);
    f.ch = ch;
    m.sampleRate = int(f.rate);
    m.channels = f.channels;
    m.bitsPerSample = f.bits;
    const std::uint64_t dataLen = f.dataLen;

    if (IsPcmTag(f.tag)) {
        f.isFloat = f.tag == 0x0003; f.isA = f.tag == 0x0006; f.isMu = f.tag == 0x0007;
        m.encoding = f.isFloat ? Encoding::Float : f.isA ? Encoding::ALaw : f.isMu ? Encoding::MuLaw : Encoding::Pcm;
        if (f.isFloat) { if (f.bits != 32 && f.bits != 64) return Fail(error, "float WAV must be 32 or 64 bit"); }
        else if (f.isA || f.isMu) { if (f.bits != 8) return Fail(error, "A-law and mu-law WAV must be 8 bit"); }
        else if (f.bits < 1 || f.bits > 32) return Fail(error, "unsupported PCM bit depth");
        f.sb = std::size_t((f.bits + 7) / 8);   // container bytes per sample
        f.stride = f.blockAlign;
        if (f.stride < f.sb * ch) {             // missing or wrong block align: use the natural one
            if (f.blockAlign != 0) m.warnings.push_back("block align smaller than one frame: ignored");
            f.stride = f.sb * ch;
        }
        f.totalFrames = dataLen / f.stride;
        if (dataLen % f.stride) m.warnings.push_back("half a frame at the end of the data dropped");
        return true;
    }

    if (f.tag == 0x0002) {   // Microsoft ADPCM
        m.encoding = Encoding::MsAdpcm;
        m.bitsPerSample = f.bits ? f.bits : 4;
        if (ch > 2) return Fail(error, "MS ADPCM supports one or two channels");
        if (f.bits != 4) return Fail(error, "MS ADPCM must be 4 bit");
        f.hdr = 7 * ch;
        if (f.blockAlign < f.hdr || f.blockAlign > 65535) return Fail(error, "bad MS ADPCM block align");
        f.spb = std::uint64_t(f.blockAlign - f.hdr) * 2 / ch + 2;   // frames in a full block
        if (f.samplesPerBlock && f.samplesPerBlock != f.spb) m.warnings.push_back("samplesPerBlock disagrees with the block align: block align wins");
        f.full = dataLen / f.blockAlign; f.rest = dataLen % f.blockAlign;
        if (f.rest >= f.hdr) f.restFrames = 2 + (f.rest - f.hdr) * 2 / ch;
        f.restBlock = f.restFrames != 0;
        f.adpcm = true;
        f.totalFrames = f.full * f.spb + f.restFrames;
        return true;
    }

    if (f.tag == 0x0011) {   // IMA / DVI ADPCM
        m.encoding = Encoding::ImaAdpcm;
        m.bitsPerSample = f.bits ? f.bits : 4;
        if (f.bits != 4) return Fail(error, "IMA ADPCM must be 4 bit");
        if (ch > 32) return Fail(error, "IMA ADPCM supports up to 32 channels here");
        f.hdr = 4 * ch;
        if (f.blockAlign < f.hdr || f.blockAlign > 65535 || (f.blockAlign - f.hdr) % f.hdr != 0)
            return Fail(error, "bad IMA ADPCM block align");
        f.groupsFull = (f.blockAlign - f.hdr) / f.hdr;
        f.spb = 1 + 8 * f.groupsFull;
        if (f.samplesPerBlock && f.samplesPerBlock != f.spb) m.warnings.push_back("samplesPerBlock disagrees with the block align: block align wins");
        f.full = dataLen / f.blockAlign; f.rest = dataLen % f.blockAlign;
        f.restBlock = f.rest >= f.hdr;
        f.restFrames = f.restBlock ? 1 + 8 * ((f.rest - f.hdr) / f.hdr) : 0;
        f.adpcm = true;
        f.totalFrames = f.full * f.spb + f.restFrames;
        return true;
    }

    return Fail(error, "unsupported WAV format tag");
}

// Plain samples: `frames` frames of `d` (stride bytes apart) to floats.
void DecodePcm(const Layout& f, const unsigned char* d, std::size_t frames, float* out) {
    const std::size_t ch = f.ch, sb = f.sb;
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t c = 0; c < ch; ++c) {
            const unsigned char* s = d + i * f.stride + c * sb;
            float v = 0.0f;
            if (f.isFloat) {
                if (sb == 4) { float x; std::memcpy(&x, s, 4); v = Sane(x); }
                else { double x; std::memcpy(&x, s, 8); v = std::isfinite(x) ? float(x) : 0.0f; }
            } else if (f.isA) v = float(ALawToLinear(s[0])) / 32768.0f;
            else if (f.isMu) v = float(MuLawToLinear(s[0])) / 32768.0f;
            else if (sb == 1) v = (int(s[0]) - 128) / 128.0f;
            else {
                // Little-endian, left-justified in the container, signed; scale by the container width.
                std::int64_t x = 0;
                for (std::size_t b = 0; b < sb; ++b) x |= std::int64_t(s[b]) << (8 * b);
                if (x & (std::int64_t(1) << (8 * sb - 1))) x -= std::int64_t(1) << (8 * sb);
                v = float(double(x) / double(std::int64_t(1) << (8 * sb - 1)));
            }
            out[i * ch + c] = v;
        }
    }
}

// One ADPCM block of `len` bytes to frames (full blocks have len == blockAlign). Writes frames * ch floats to `out`
// (room for BlockSamples() is the caller's job) and returns the frame count in *frames. Null or an error text.
// `imaClampWarned` makes the "step index above 88" warning appear once however many blocks have it.
const char* DecodeAdpcmBlock(const Layout& f, const unsigned char* b, std::size_t len, float* out, std::size_t* frames,
                             std::vector<std::string>* warnings, bool* imaClampWarned) {
    const std::size_t ch = f.ch, hdr = f.hdr;
    if (f.tag == 0x0002) {
        int c1[2], c2[2], delta[2], s1[2], s2[2];
        const int nco = f.haveCoef ? f.numCoef : 7;
        for (std::size_t c = 0; c < ch; ++c) {
            const unsigned pi = b[c];
            if (int(pi) >= nco) return "MS ADPCM predictor index out of range";
            c1[c] = f.haveCoef ? f.coef[pi][0] : kMsStdCoef[pi][0];
            c2[c] = f.haveCoef ? f.coef[pi][1] : kMsStdCoef[pi][1];
            delta[c] = S16(b + ch + 2 * c);
            s1[c] = S16(b + 3 * ch + 2 * c);
            s2[c] = S16(b + 5 * ch + 2 * c);
        }
        std::size_t w = 0;
        for (std::size_t c = 0; c < ch; ++c) out[w++] = float(s2[c]) / 32768.0f;
        for (std::size_t c = 0; c < ch; ++c) out[w++] = float(s1[c]) / 32768.0f;
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
            out[w++] = float(smp) / 32768.0f;
        }
        *frames = w / ch;
        return nullptr;
    }
    // IMA
    const std::uint64_t groups = (len - hdr) / hdr;
    int pred[32], idx[32];
    for (std::size_t c = 0; c < ch; ++c) {
        pred[c] = S16(b + 4 * c);
        idx[c] = b[4 * c + 2];
        if (idx[c] > 88) {
            idx[c] = 88;
            if (!*imaClampWarned) { *imaClampWarned = true; warnings->push_back("IMA step index above 88 clamped"); }
        }
        out[c] = float(pred[c]) / 32768.0f;
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
                out[(1 + std::size_t(g) * 8 + std::size_t(j)) * ch + c] = float(pred[c]) / 32768.0f;
            }
        }
    }
    *frames = std::size_t(1 + groups * 8);
    return nullptr;
}

// Largest decoded block, in floats.
std::uint64_t BlockSamples(const Layout& f) {
    return std::max<std::uint64_t>(f.spb, f.restFrames) * f.ch;
}

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
    auto src = AudioByteSource::FromPointer(data, size);
    Layout f;
    if (!ParseHeader(*src, L, out, f, error)) return false;
    const std::size_t ch = f.ch;
    // Output size, checked against the budget BEFORE allocating.
    if (f.totalFrames > L.maxOutputSamples / ch) return Fail(error, "decoded size over the budget");
    const unsigned char* d = data + f.dataOff;
    out.samples.resize(std::size_t(f.totalFrames) * ch);
    if (!f.adpcm) {
        DecodePcm(f, d, std::size_t(f.totalFrames), out.samples.data());
        return true;
    }
    if (BlockSamples(f) > L.maxOutputSamples) return Fail(error, "ADPCM block over the budget");
    std::size_t at = 0;
    bool warned = false;
    for (std::uint64_t blk = 0; blk < f.Units(); ++blk) {
        const std::size_t len = blk < f.full ? f.blockAlign : std::size_t(f.rest);
        std::size_t frames = 0;
        const char* err = DecodeAdpcmBlock(f, d + std::size_t(blk) * f.blockAlign, len, out.samples.data() + at * ch, &frames,
                                           &out.warnings, &warned);
        if (err) return Fail(error, err);
        at += frames;
    }
    return true;
}

struct WavStream::Impl {
    std::shared_ptr<AudioByteSource> src;
    Limits lim;
    WavMeta meta;
    Layout f;
    bool open = false, failed = false, ended = false, imaWarned = false;
    std::string error;
    std::uint64_t pos = 0;                 // next frame to return
    // PCM: a read window of whole frames
    std::vector<unsigned char> raw;
    std::size_t chunkFrames = 0;
    // ADPCM: the raw block and the decoded block
    std::vector<float> block;
    std::uint64_t nextBlock = 0;           // next block to load
    std::size_t blockFrames = 0, blockPos = 0;

    bool LoadBlock() {
        if (nextBlock >= f.Units()) return false;
        const std::size_t len = nextBlock < f.full ? f.blockAlign : std::size_t(f.rest);
        const std::size_t got = src->Read(f.dataOff + nextBlock * f.blockAlign, raw.data(), len);
        if (got < len) { ended = true; return false; }   // the source shrank under us
        std::size_t frames = 0;
        const char* err = DecodeAdpcmBlock(f, raw.data(), len, block.data(), &frames, &meta.warnings, &imaWarned);
        if (err) { failed = true; error = err; return false; }
        ++nextBlock;
        blockFrames = frames;
        blockPos = 0;
        return true;
    }
};

WavStream::WavStream() : p_(new Impl) {}
WavStream::~WavStream() = default;

bool WavStream::Open(std::shared_ptr<AudioByteSource> src, std::string* error, const Limits* limits) {
    Impl& s = *p_;
    s = Impl();
    if (!src) return Fail(error, "no data");
    s.lim = limits ? *limits : Limits();
    s.src = std::move(src);
    if (!ParseHeader(*s.src, s.lim, s.meta, s.f, error)) { s.src.reset(); return false; }
    const Layout& f = s.f;
    if (f.adpcm) {
        if (BlockSamples(f) > s.lim.maxOutputSamples) { s.src.reset(); return Fail(error, "ADPCM block over the budget"); }
        s.raw.resize(f.blockAlign);
        s.block.resize(std::size_t(BlockSamples(f)));
    } else {
        s.chunkFrames = std::max<std::size_t>(1, std::min<std::size_t>(1024, 65536 / f.stride));
        s.raw.resize(s.chunkFrames * f.stride);
    }
    s.open = true;
    return true;
}

bool WavStream::IsOpen() const { return p_->open; }
const WavMeta& WavStream::Info() const { return p_->meta; }
std::uint64_t WavStream::TotalFrames() const { return p_->open ? p_->f.totalFrames : 0; }
std::uint64_t WavStream::Position() const { return p_->pos; }
bool WavStream::Failed() const { return p_->failed; }
const std::string& WavStream::Error() const { return p_->error; }
std::size_t WavStream::MemoryBytes() const {
    return sizeof(*this) + sizeof(Impl) + p_->raw.capacity() + p_->block.capacity() * sizeof(float);
}

std::size_t WavStream::Read(float* dst, std::size_t maxFrames) {
    Impl& s = *p_;
    if (!s.open || s.failed || !dst) return 0;
    const Layout& f = s.f;
    const std::size_t ch = f.ch;
    std::size_t done = 0;
    while (done < maxFrames && !s.ended && s.pos < f.totalFrames) {
        if (!f.adpcm) {
            const std::size_t n = std::size_t(std::min<std::uint64_t>(std::min<std::uint64_t>(maxFrames - done, s.chunkFrames), f.totalFrames - s.pos));
            const std::size_t got = s.src->Read(f.dataOff + s.pos * f.stride, s.raw.data(), n * f.stride);
            const std::size_t frames = got / f.stride;
            if (frames == 0) { s.ended = true; break; }   // the source shrank under us
            DecodePcm(f, s.raw.data(), frames, dst + done * ch);
            done += frames;
            s.pos += frames;
        } else {
            if (s.blockPos >= s.blockFrames && !s.LoadBlock()) break;
            const std::size_t n = std::min(maxFrames - done, s.blockFrames - s.blockPos);
            std::memcpy(dst + done * ch, s.block.data() + s.blockPos * ch, n * ch * sizeof(float));
            s.blockPos += n;
            done += n;
            s.pos += n;
        }
    }
    return done;
}

bool WavStream::Seek(std::uint64_t frame) {
    Impl& s = *p_;
    if (!s.open || s.failed) return false;
    const Layout& f = s.f;
    frame = std::min(frame, f.totalFrames);
    s.ended = false;
    s.pos = frame;
    s.blockFrames = s.blockPos = 0;
    if (f.adpcm && frame < f.totalFrames) {
        s.nextBlock = frame / f.spb;
        if (!s.LoadBlock()) return false;   // failed or shrunk: Failed() tells which
        s.blockPos = std::size_t(frame - (frame / f.spb) * f.spb);
    } else if (f.adpcm) {
        s.nextBlock = f.Units();
    }
    return true;
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
