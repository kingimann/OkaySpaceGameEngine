// Native FLAC decoder, written from the FLAC format specification (xiph.org/flac/format.html):
// the fLaC marker, metadata blocks, frame headers, constant / verbatim / fixed / LPC
// subframes, rice residuals and stereo decorrelation. See FlacDecoder.hpp.
#include "okay/Audio/FlacDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace okay {

namespace {

constexpr int kMaxChannels = 8;
constexpr int kMaxBlock = 65535;
constexpr int kMaxOrder = 32;
constexpr std::uint64_t kMaxFrameBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxSamples = std::size_t(1) << 28;
constexpr int kMaxUnary = 1 << 22;

struct Info {
    int rate = 0;
    int channels = 0;
    int bps = 0;
    std::int64_t total = 0;     ///< sample frames, 0 when the file does not say
    std::uint64_t audioAt = 0;
};

class Bits {
public:
    Bits(AudioByteSource& src, std::uint64_t size) : src_(src), size_(size) {}
    bool Ok() const { return ok_; }
    void Fail() { ok_ = false; }
    std::uint64_t BytePos() const { return byte_; }
    void Seek(std::uint64_t pos) { byte_ = pos; bit_ = 0; ok_ = pos <= size_; have_ = false; }

    void Align() {
        if (!ok_ || bit_ == 0) return;
        bit_ = 0;
        ++byte_;
        have_ = false;
    }

    std::uint32_t ReadBits(int n) {
        if (n <= 0) return 0;
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            if (!ok_) return 0;
            if (!Have()) { ok_ = false; return 0; }
            const int b = (cur_ >> (7 - bit_)) & 1;
            v = (v << 1) | static_cast<std::uint32_t>(b);
            if (++bit_ == 8) { bit_ = 0; ++byte_; have_ = false; }
        }
        return v;
    }
    bool ReadBit() { return ReadBits(1) != 0; }

    std::int32_t ReadSigned(int n) {
        if (n <= 0) return 0;
        if (n >= 32) return static_cast<std::int32_t>(ReadBits(32));
        const std::uint32_t u = ReadBits(n);
        const std::uint32_t sign = 1u << (n - 1);
        if (u & sign) return static_cast<std::int32_t>(u | ~(sign - 1u));
        return static_cast<std::int32_t>(u);
    }
    /// The bits left in the current byte must be zero. Afterwards the next
    /// read starts on a byte.
    bool PadToByte() {
        if (!ok_ || bit_ == 0) return ok_;
        const int left = 8 - bit_;
        if (ReadBits(left) != 0) { ok_ = false; return false; }
        return ok_;
    }

private:
    bool Have() {
        if (have_) return true;
        if (byte_ >= size_) return false;
        unsigned char c = 0;
        if (src_.Read(byte_, &c, 1) != 1) return false;
        cur_ = c;
        have_ = true;
        return true;
    }
    AudioByteSource& src_;
    std::uint64_t size_ = 0;
    std::uint64_t byte_ = 0;
    int bit_ = 0;
    unsigned char cur_ = 0;
    bool have_ = false;
    bool ok_ = true;
};

std::uint8_t Crc8Byte(std::uint8_t crc, std::uint8_t data) {
    crc = static_cast<std::uint8_t>(crc ^ data);
    for (int i = 0; i < 8; ++i)
        crc = (crc & 0x80) ? static_cast<std::uint8_t>((crc << 1) ^ 0x07) : static_cast<std::uint8_t>(crc << 1);
    return crc;
}
std::uint16_t Crc16Byte(std::uint16_t crc, std::uint8_t data) {
    crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(data) << 8));
    for (int i = 0; i < 8; ++i)
        crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x8005) : static_cast<std::uint16_t>(crc << 1);
    return crc;
}

std::uint16_t Crc16Range(AudioByteSource& src, std::uint64_t begin, std::uint64_t end) {
    std::uint16_t crc = 0;
    unsigned char buf[256];
    for (std::uint64_t at = begin; at < end; ) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(sizeof buf, end - at));
        const std::size_t got = src.Read(at, buf, n);
        if (got == 0) break;
        for (std::size_t i = 0; i < got; ++i) crc = Crc16Byte(crc, buf[i]);
        at += got;
    }
    return crc;
}

std::uint8_t Crc8Range(AudioByteSource& src, std::uint64_t begin, std::uint64_t end) {
    std::uint8_t crc = 0;
    for (std::uint64_t at = begin; at < end; ++at) {
        unsigned char c = 0;
        if (src.Read(at, &c, 1) != 1) break;
        crc = Crc8Byte(crc, c);
    }
    return crc;
}

std::int64_t ArithShr(std::int64_t v, int n) {
    if (n <= 0) return v;
    if (n >= 63) return v < 0 ? -1 : 0;
    const std::uint64_t u = static_cast<std::uint64_t>(v) >> n;
    if (v >= 0) return static_cast<std::int64_t>(u);
    const std::uint64_t fill = ~std::uint64_t(0) << (64 - n);
    return static_cast<std::int64_t>(u | fill);
}

int BlockSizeFromCode(int code) {
    if (code == 1) return 192;
    if (code >= 2 && code <= 5) return 576 << (code - 2);
    if (code >= 8 && code <= 15) return 256 << (code - 8);
    return 0;   // 0, 6 and 7 are reserved or given in extra bytes
}

int RateFromCode(int code) {
    static const int kRates[] = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
    if (code >= 0 && code <= 11) return kRates[code];
    return 0;
}

int BpsFromCode(int code) {
    static const int kBps[] = {0, 8, 12, 0, 16, 20, 24, 32};
    if (code >= 0 && code <= 7) return kBps[code];
    return 0;
}

std::uint64_t ReadUtf8(Bits& b) {
    const std::uint32_t first = b.ReadBits(8);
    if (!b.Ok()) return 0;
    if (first < 0x80) return first;
    int extra = 0;
    std::uint64_t v = 0;
    if ((first & 0xE0) == 0xC0) { extra = 1; v = first & 0x1F; }
    else if ((first & 0xF0) == 0xE0) { extra = 2; v = first & 0x0F; }
    else if ((first & 0xF8) == 0xF0) { extra = 3; v = first & 0x07; }
    else if ((first & 0xFC) == 0xF8) { extra = 4; v = first & 0x03; }
    else if ((first & 0xFE) == 0xFC) { extra = 5; v = first & 0x01; }
    else if (first == 0xFE) { extra = 6; v = 0; }
    else { b.Fail(); return 0; }
    for (int i = 0; i < extra; ++i) {
        const std::uint32_t c = b.ReadBits(8);
        if ((c & 0xC0) != 0x80) { b.Fail(); return 0; }
        v = (v << 6) | (c & 0x3F);
    }
    return v;
}

bool ReadResidual(Bits& b, int block, int order, std::int32_t* out) {
    const int method = static_cast<int>(b.ReadBits(2));
    const int porder = static_cast<int>(b.ReadBits(4));
    if (!b.Ok() || method > 1) return false;
    const int paramBits = method == 0 ? 4 : 5;
    const int escape = (1 << paramBits) - 1;
    if (porder > 15) return false;
    const int parts = 1 << porder;
    if (porder > 0 && (block >> porder) <= order) return false;
    int at = 0;
    const int need = block - order;
    for (int p = 0; p < parts; ++p) {
        int n = porder == 0 ? block - order : (block >> porder);
        if (porder > 0 && p == 0) n -= order;
        if (n < 0 || at + n > need) return false;
        const int param = static_cast<int>(b.ReadBits(paramBits));
        if (!b.Ok()) return false;
        if (param == escape) {
            const int width = static_cast<int>(b.ReadBits(5));
            if (!b.Ok() || width > 31) return false;
            for (int i = 0; i < n; ++i) out[at++] = width == 0 ? 0 : b.ReadSigned(width);
        } else {
            for (int i = 0; i < n; ++i) {
                // Quotient is unary zeros, stopped by a one.
                int q = 0;
                while (!b.ReadBit()) {
                    if (++q > kMaxUnary || !b.Ok()) return false;
                }
                if (!b.Ok()) return false;
                const std::uint32_t r = param > 0 ? b.ReadBits(param) : 0;
                const std::uint64_t u = (static_cast<std::uint64_t>(q) << param) | r;
                if (u > 0xFFFFFFFFull) return false;
                const std::uint32_t uu = static_cast<std::uint32_t>(u);
                out[at++] = (uu & 1u) ? -static_cast<std::int32_t>(uu >> 1) - 1 : static_cast<std::int32_t>(uu >> 1);
            }
        }
        if (!b.Ok()) return false;
    }
    return at == need;
}

bool ReadSubframe(Bits& b, int block, int bps, std::int32_t* out) {
    if (b.ReadBit()) return false;   // the leading bit is reserved and must be 0
    const int kind = static_cast<int>(b.ReadBits(6));
    int wasted = 0;
    if (b.ReadBit()) {
        // k-1 zeros, then a one. k itself is the wasted-bit count.
        int k = 0;
        while (!b.ReadBit()) {
            if (++k > 30 || !b.Ok()) return false;
        }
        wasted = k + 1;
    }
    if (!b.Ok() || wasted >= bps) return false;
    const int width = bps - wasted;
    int order = 0;
    int coef[kMaxOrder];
    int shift = 0;
    if (kind == 0) {                         // constant
        const std::int32_t s = b.ReadSigned(width);
        if (!b.Ok()) return false;
        std::fill(out, out + block, s);
    } else if (kind == 1) {                  // verbatim
        for (int i = 0; i < block; ++i) out[i] = b.ReadSigned(width);
        if (!b.Ok()) return false;
    } else if ((kind & 0x38) == 0x08 && (kind & 7) <= 4) {   // fixed predictor
        order = kind & 7;
        static const int kFixed[5][4] = {
            {0, 0, 0, 0}, {1, 0, 0, 0}, {2, -1, 0, 0}, {3, -3, 1, 0}, {4, -6, 4, -1},
        };
        if (order > block) return false;
        for (int i = 0; i < order; ++i) out[i] = b.ReadSigned(width);
        std::vector<std::int32_t> residual(static_cast<std::size_t>(block - order));
        if (!ReadResidual(b, block, order, residual.data())) return false;
        for (int j = 0; j < order; ++j) coef[j] = kFixed[order][j];
        shift = 0;
        for (int i = order; i < block; ++i) {
            std::int64_t sum = 0;
            for (int j = 0; j < order; ++j) sum += static_cast<std::int64_t>(coef[j]) * out[i - 1 - j];
            out[i] = static_cast<std::int32_t>(sum + residual[static_cast<std::size_t>(i - order)]);
        }
    } else if (kind >= 32) {                 // LPC
        order = (kind & 31) + 1;
        if (order > block || order > kMaxOrder) return false;
        for (int i = 0; i < order; ++i) out[i] = b.ReadSigned(width);
        const int precRaw = static_cast<int>(b.ReadBits(4));
        if (precRaw == 15 || !b.Ok()) return false;
        const int prec = precRaw + 1;   // the field stores precision minus one
        const int rawShift = static_cast<int>(b.ReadBits(5));
        shift = rawShift >= 16 ? rawShift - 32 : rawShift;
        if (shift < 0) return false;     // the format forbids a negative shift
        for (int j = 0; j < order; ++j) coef[j] = b.ReadSigned(prec);
        std::vector<std::int32_t> residual(static_cast<std::size_t>(block - order));
        if (!b.Ok() || !ReadResidual(b, block, order, residual.data())) return false;
        if (shift < -31) return false;
        for (int i = order; i < block; ++i) {
            std::int64_t sum = 0;
            for (int j = 0; j < order; ++j)
                sum += static_cast<std::int64_t>(coef[j]) * out[i - 1 - j];
            const std::int64_t pred = shift >= 0 ? ArithShr(sum, shift) : (sum << (-shift));
            out[i] = static_cast<std::int32_t>(pred + residual[static_cast<std::size_t>(i - order)]);
        }
    } else {
        return false;
    }
    if (wasted > 0) {
        for (int i = 0; i < block; ++i)
            out[i] = static_cast<std::int32_t>(static_cast<std::uint32_t>(out[i]) << wasted);
    }
    return b.Ok();
}

struct Frame {
    int rate = 0;
    int channels = 0;
    int bps = 0;
    int block = 0;
    std::vector<float> pcm;
    std::uint64_t next = 0;
};

bool ReadFrame(AudioByteSource& src, std::uint64_t size, Bits& b, const Info& info, Frame& out) {
    const std::uint64_t start = b.BytePos();
    if (start + 6 > size) return false;
    if (b.ReadBits(15) != 0x7FFC) return false;          // sync + the reserved 0
    const int strategy = static_cast<int>(b.ReadBits(1));
    const int blockCode = static_cast<int>(b.ReadBits(4));
    const int rateCode = static_cast<int>(b.ReadBits(4));
    const int chanCode = static_cast<int>(b.ReadBits(4));
    const int bpsCode = static_cast<int>(b.ReadBits(3));
    if (b.ReadBits(1) != 0 || !b.Ok()) return false;     // reserved
    (void)strategy;
    (void)ReadUtf8(b);
    if (!b.Ok()) return false;
    int block = BlockSizeFromCode(blockCode);
    if (blockCode == 6) block = static_cast<int>(b.ReadBits(8)) + 1;
    else if (blockCode == 7) block = static_cast<int>(b.ReadBits(16)) + 1;
    int rate = RateFromCode(rateCode);
    if (rateCode == 12) rate = static_cast<int>(b.ReadBits(8)) * 1000;
    else if (rateCode == 13) rate = static_cast<int>(b.ReadBits(16));
    else if (rateCode == 14) rate = static_cast<int>(b.ReadBits(16)) * 10;
    int bps = BpsFromCode(bpsCode);
    if (bps == 0) bps = info.bps;
    if (rate == 0) rate = info.rate;
    if (!b.Ok() || block < 1 || block > kMaxBlock || rate < 1 || bps < 4 || bps > 32) return false;
    if (chanCode > 10) return false;
    const int channels = chanCode < 8 ? chanCode + 1 : 2;
    if (channels < 1 || channels > kMaxChannels) return false;
    if (info.channels != 0 && channels != info.channels) return false;
    if (info.rate != 0 && rate != info.rate) return false;
    const std::uint64_t crcAt = b.BytePos();
    const std::uint32_t crc8 = b.ReadBits(8);
    if (!b.Ok() || Crc8Range(src, start, crcAt) != crc8) return false;
    if (b.BytePos() > start + kMaxFrameBytes) return false;

    std::vector<std::int32_t> chans[kMaxChannels];
    for (int c = 0; c < channels; ++c) {
        int cbps = bps;
        if ((chanCode == 8 || chanCode == 10) && c == 1) cbps = bps + 1;
        if (chanCode == 9 && c == 0) cbps = bps + 1;
        if (cbps > 32) return false;
        chans[c].resize(static_cast<std::size_t>(block));
        if (!ReadSubframe(b, block, cbps, chans[c].data())) return false;
    }
    if (b.BytePos() > start + kMaxFrameBytes) return false;
    if (!b.PadToByte()) return false;   // unused bits up to the next byte are part of the CRC and must be 0
    const std::uint64_t crc16At = b.BytePos();
    const std::uint32_t stored = b.ReadBits(16);
    if (!b.Ok() || crc16At < start || crc16At - start > kMaxFrameBytes) return false;
    if (Crc16Range(src, start, crc16At) != stored) return false;

    if (chanCode == 8) {                     // left + side; side was the right channel
        for (int i = 0; i < block; ++i)
            chans[1][static_cast<std::size_t>(i)] =
                chans[0][static_cast<std::size_t>(i)] - chans[1][static_cast<std::size_t>(i)];
    } else if (chanCode == 9) {              // side + right
        for (int i = 0; i < block; ++i)
            chans[0][static_cast<std::size_t>(i)] =
                chans[1][static_cast<std::size_t>(i)] + chans[0][static_cast<std::size_t>(i)];
    } else if (chanCode == 10) {             // mid + side
        for (int i = 0; i < block; ++i) {
            const std::int64_t mid = chans[0][static_cast<std::size_t>(i)];
            const std::int64_t side = chans[1][static_cast<std::size_t>(i)];
            const std::int64_t t = (mid << 1) | (side & 1);
            chans[0][static_cast<std::size_t>(i)] = static_cast<std::int32_t>(ArithShr(t + side, 1));
            chans[1][static_cast<std::size_t>(i)] = static_cast<std::int32_t>(ArithShr(t - side, 1));
        }
    }

    const float scale = std::ldexp(1.0f, 1 - bps);
    out.pcm.resize(static_cast<std::size_t>(block) * static_cast<std::size_t>(channels));
    for (int i = 0; i < block; ++i)
        for (int c = 0; c < channels; ++c)
            out.pcm[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] =
                static_cast<float>(chans[c][static_cast<std::size_t>(i)]) * scale;
    out.rate = rate;
    out.channels = channels;
    out.bps = bps;
    out.block = block;
    out.next = b.BytePos();
    return true;
}

bool ParseInfo(AudioByteSource& src, std::uint64_t size, Info& info, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (size < 4) return fail("not a FLAC file");
    unsigned char mag[4] = {0, 0, 0, 0};
    std::uint64_t pos = 0;
    if (size >= 10 && src.Read(0, mag, 3) == 3 && std::memcmp(mag, "ID3", 3) == 0) {
        unsigned char h[10];
        if (src.Read(0, h, 10) != 10) return fail("not a FLAC file");
        const std::uint32_t tag = (static_cast<std::uint32_t>(h[6] & 0x7f) << 21) |
                                  (static_cast<std::uint32_t>(h[7] & 0x7f) << 14) |
                                  (static_cast<std::uint32_t>(h[8] & 0x7f) << 7) |
                                  static_cast<std::uint32_t>(h[9] & 0x7f);
        pos = 10u + tag + ((h[5] & 0x10) ? 10u : 0u);
        if (pos > size) return fail("not a FLAC file");
    }
    if (src.Read(pos, mag, 4) != 4 || std::memcmp(mag, "fLaC", 4) != 0) return fail("not a FLAC file");
    pos += 4;
    bool sawInfo = false;
    bool last = false;
    for (int guard = 0; !last && guard < 128; ++guard) {
        unsigned char hdr[4];
        if (pos + 4 > size || src.Read(pos, hdr, 4) != 4) return fail("truncated FLAC metadata");
        last = (hdr[0] & 0x80) != 0;
        const int type = hdr[0] & 0x7f;
        const std::uint32_t len = (static_cast<std::uint32_t>(hdr[1]) << 16) |
                                  (static_cast<std::uint32_t>(hdr[2]) << 8) | hdr[3];
        if (len > 16u * 1024u * 1024u || pos + 4 + len > size) return fail("truncated FLAC metadata");
        if (type == 0) {
            if (sawInfo || len < 34) return fail("bad FLAC stream info");
            unsigned char all[34];
            if (src.Read(pos + 4, all, 34) != 34) return fail("bad FLAC stream info");
            // 16+16+24+24 bits of block and frame sizes, then 20+3+5+36 bits.
            std::uint64_t word = 0;
            for (int i = 0; i < 8; ++i) word = (word << 8) | all[10 + i];
            info.rate = static_cast<int>(word >> 44);
            info.channels = static_cast<int>((word >> 41) & 7) + 1;
            info.bps = static_cast<int>((word >> 36) & 31) + 1;
            info.total = static_cast<std::int64_t>(word & ((std::uint64_t(1) << 36) - 1));
            if (info.rate < 1 || info.rate > 655350 || info.channels < 1 || info.channels > kMaxChannels ||
                info.bps < 4 || info.bps > 32)
                return fail("unsupported FLAC stream");
            sawInfo = true;
        }
        pos += 4 + len;
    }
    if (!sawInfo || !last) return fail("FLAC file has no stream info");
    info.audioAt = pos;
    return true;
}

class Engine {
public:
    explicit Engine(std::shared_ptr<AudioByteSource> src) : src_(std::move(src)), bits_(*src_, src_->Size()) {}

    bool Open(std::string* error) {
        if (!src_) { if (error) *error = "no audio data"; return false; }
        if (!ParseInfo(*src_, src_->Size(), info_, error)) return false;
        byte_ = info_.audioAt;
        if (!Pull()) { if (error) *error = "not a readable FLAC file"; return false; }
        Rewind();
        open_ = true;
        return true;
    }

    int Rate() const { return info_.rate; }
    int Channels() const { return info_.channels; }
    std::int64_t Total() const { return info_.total > 0 ? info_.total : -1; }

    std::size_t Read(float* dst, std::size_t maxFrames) {
        if (!open_ || !dst || maxFrames == 0) return 0;
        const int ch = info_.channels;
        std::size_t done = 0;
        while (done < maxFrames) {
            if (info_.total > 0 && served_ >= info_.total) break;
            if (pendPos_ >= pending_.size()) {
                if (!Pull()) break;
            }
            std::size_t have = (pending_.size() - pendPos_) / static_cast<std::size_t>(ch);
            if (info_.total > 0)
                have = std::min(have, static_cast<std::size_t>(info_.total - served_));
            const std::size_t n = std::min(maxFrames - done, have);
            if (n == 0) break;
            std::memcpy(dst + done * static_cast<std::size_t>(ch),
                        pending_.data() + pendPos_,
                        n * static_cast<std::size_t>(ch) * sizeof(float));
            pendPos_ += n * static_cast<std::size_t>(ch);
            served_ += static_cast<std::int64_t>(n);
            done += n;
        }
        return done;
    }

    bool Seek(std::int64_t frame) {
        if (!open_) return false;
        if (frame < 0) frame = 0;
        if (info_.total > 0 && frame > info_.total) frame = info_.total;
        std::uint64_t byte = info_.audioAt;
        std::int64_t sample = 0;
        for (const Mark& m : marks_) {
            if (m.sample <= frame && m.sample >= sample) { sample = m.sample; byte = m.byte; }
        }
        byte_ = byte;
        nextSample_ = sample;
        ended_ = false;
        pending_.clear();
        pendPos_ = 0;
        served_ = sample;
        while (served_ < frame) {
            if (pendPos_ >= pending_.size()) {
                if (!Pull()) break;
            }
            const std::size_t have = (pending_.size() - pendPos_) / static_cast<std::size_t>(info_.channels);
            const std::size_t need = static_cast<std::size_t>(frame - served_);
            const std::size_t n = std::min(have, need);
            pendPos_ += n * static_cast<std::size_t>(info_.channels);
            served_ += static_cast<std::int64_t>(n);
            if (n == 0) break;
        }
        return true;
    }

    std::size_t Memory() const {
        return sizeof(*this) + pending_.capacity() * sizeof(float) + marks_.capacity() * sizeof(Mark);
    }

    void DecodeAll(FlacAudio& out) {
        Rewind();
        out.sampleRate = info_.rate;
        out.channels = info_.channels;
        out.samples.clear();
        std::vector<float> chunk(static_cast<std::size_t>(4096) * static_cast<std::size_t>(info_.channels));
        for (;;) {
            if (out.samples.size() / static_cast<std::size_t>(info_.channels) >= kMaxSamples) break;
            const std::size_t n = Read(chunk.data(), 4096);
            if (n == 0) break;
            out.samples.insert(out.samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n * static_cast<std::size_t>(info_.channels)));
        }
    }

private:
    struct Mark { std::int64_t sample; std::uint64_t byte; };
    void Rewind() {
        byte_ = info_.audioAt;
        nextSample_ = 0;
        served_ = 0;
        ended_ = false;
        pending_.clear();
        pendPos_ = 0;
        marks_.clear();
    }
    bool Pull() {
        if (ended_) return false;
        if (byte_ + 4 > src_->Size()) { ended_ = true; return false; }
        const std::int64_t sampleAt = nextSample_;
        Frame frame;
        // A bad sync (or a CRC miss) steps one byte and tries again. A healthy
        // file matches on the first byte. Bound the scan so a damaged tail ends.
        const std::uint64_t limit = std::min(src_->Size(), byte_ + kMaxFrameBytes);
        bool got = false;
        while (byte_ + 4 <= limit) {
            bits_.Seek(byte_);
            if (ReadFrame(*src_, src_->Size(), bits_, info_, frame)) { got = true; break; }
            ++byte_;
        }
        if (!got) { ended_ = true; return false; }
        if (marks_.empty() || marks_.back().sample < sampleAt) marks_.push_back({sampleAt, byte_});
        pending_ = std::move(frame.pcm);
        pendPos_ = 0;
        nextSample_ += frame.block;
        byte_ = frame.next;
        return true;
    }

    std::shared_ptr<AudioByteSource> src_;
    Info info_;
    Bits bits_;
    std::uint64_t byte_ = 0;
    std::int64_t nextSample_ = 0;
    std::int64_t served_ = 0;
    bool ended_ = false;
    bool open_ = false;
    std::vector<float> pending_;
    std::size_t pendPos_ = 0;
    std::vector<Mark> marks_;
};

} // namespace

bool DecodeFlac(const unsigned char* data, std::size_t size, FlacAudio& out, std::string* error) {
    out = FlacAudio();
    if (!data || size == 0) { if (error) *error = "no audio data"; return false; }
    auto src = AudioByteSource::FromPointer(data, size);
    Engine engine(src);
    if (!engine.Open(error)) return false;
    engine.DecodeAll(out);
    if (out.FrameCount() == 0) { if (error) *error = "not a readable FLAC file"; return false; }
    return true;
}

struct FlacStream::Impl {
    Engine engine;
    explicit Impl(std::shared_ptr<AudioByteSource> src) : engine(std::move(src)) {}
};

FlacStream::FlacStream() = default;
FlacStream::~FlacStream() = default;

bool FlacStream::Open(std::shared_ptr<AudioByteSource> source, std::string* error) {
    m.reset(new Impl(std::move(source)));
    if (!m->engine.Open(error)) { m.reset(); return false; }
    return true;
}
bool FlacStream::IsOpen() const { return m != nullptr; }
int FlacStream::SampleRate() const { return m ? m->engine.Rate() : 0; }
int FlacStream::Channels() const { return m ? m->engine.Channels() : 0; }
std::int64_t FlacStream::TotalFrames() const { return m ? m->engine.Total() : -1; }
std::size_t FlacStream::Read(float* dst, std::size_t maxFrames) { return m ? m->engine.Read(dst, maxFrames) : 0; }
bool FlacStream::Seek(std::int64_t frame) { return m ? m->engine.Seek(frame) : false; }
std::size_t FlacStream::MemoryBytes() const { return m ? m->engine.Memory() + sizeof(*this) : 0; }

} // namespace okay
