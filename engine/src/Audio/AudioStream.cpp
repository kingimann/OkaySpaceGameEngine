// Streaming audio: the byte sources, the WAV and MP3 PcmDecoders and the ring
// buffer AudioStream. (The OGG decoder wrapper lives in AudioDecode.cpp; the decoder is VorbisStream.)
#include "okay/Audio/AudioStream.hpp"
#include "okay/Audio/AudioByteSource.hpp"
#include "okay/Audio/FlacDecoder.hpp"
#include "okay/Audio/Mp3Decoder.hpp"
#include "okay/Core/ImportSidecar.hpp"
#include "AudioInternal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace okay {

// ---------------------------------------------------------------------------
// Byte sources
// ---------------------------------------------------------------------------

namespace {

class PointerSource : public AudioByteSource {
public:
    PointerSource(const unsigned char* d, std::size_t n) : d_(d), n_(d ? n : 0) {}
    std::uint64_t Size() const override { return n_; }
    std::size_t Read(std::uint64_t off, void* dst, std::size_t n) override {
        if (off >= n_ || !dst) return 0;
        n = static_cast<std::size_t>(std::min<std::uint64_t>(n, n_ - off));
        std::memcpy(dst, d_ + off, n);
        return n;
    }
private:
    const unsigned char* d_;
    std::size_t n_;
};

class MemorySource : public PointerSource {
public:
    explicit MemorySource(std::shared_ptr<const std::vector<unsigned char>> b)
        : PointerSource(b ? b->data() : nullptr, b ? b->size() : 0), keep_(std::move(b)) {}
private:
    std::shared_ptr<const std::vector<unsigned char>> keep_;
};

class FileSource : public AudioByteSource {
public:
    ~FileSource() override { if (f_) std::fclose(f_); }
    bool Open(const std::string& path, std::string* error) {
        f_ = std::fopen(path.c_str(), "rb");
        if (!f_) { if (error) *error = "cannot open " + path; return false; }
        if (std::fseek(f_, 0, SEEK_END) != 0) { if (error) *error = "cannot read " + path; return false; }
        const long len = std::ftell(f_);
        if (len < 0 || len >= 0x7fffffffL) { if (error) *error = "file is empty or over 2 GB: " + path; return false; }
        size_ = static_cast<std::uint64_t>(len);
        return true;
    }
    std::uint64_t Size() const override { return size_; }
    std::size_t Read(std::uint64_t off, void* dst, std::size_t n) override {
        if (!f_ || off >= size_ || !dst) return 0;
        n = static_cast<std::size_t>(std::min<std::uint64_t>(n, size_ - off));
        if (std::fseek(f_, static_cast<long>(off), SEEK_SET) != 0) return 0;
        return std::fread(dst, 1, n, f_);
    }
private:
    std::FILE* f_ = nullptr;
    std::uint64_t size_ = 0;
};

} // namespace

std::shared_ptr<AudioByteSource> AudioByteSource::FromPointer(const unsigned char* data, std::size_t size) {
    return std::make_shared<PointerSource>(data, size);
}
std::shared_ptr<AudioByteSource> AudioByteSource::FromMemory(std::shared_ptr<const std::vector<unsigned char>> bytes) {
    return std::make_shared<MemorySource>(std::move(bytes));
}
std::shared_ptr<AudioByteSource> AudioByteSource::FromFile(const std::string& path, std::string* error) {
    auto f = std::make_shared<FileSource>();
    if (!f->Open(path, error)) return nullptr;
    return f;
}

// ---------------------------------------------------------------------------
// Decoders
// ---------------------------------------------------------------------------

namespace audio_detail {
namespace {

class WavPcmDecoder : public PcmDecoder {
public:
    bool Open(std::shared_ptr<AudioByteSource> src, std::string* error) {
        src_ = std::move(src);
        if (const char* err = ParseWav(*src_, w_)) { if (error) *error = err; return false; }
        if (w_.channels > 16 || w_.rate == 0 || w_.rate > 768000) {
            if (error) *error = "unsupported WAV format";
            return false;
        }
        sampleRate = static_cast<int>(w_.rate);
        channels = KeptChannels(w_.channels, false);
        totalFrames = static_cast<std::int64_t>(w_.Frames());
        raw_.resize(kChunk * w_.BlockBytes());
        return true;
    }
    std::size_t Read(float* dst, std::size_t maxFrames) override {
        std::size_t done = 0;
        const std::uint64_t total = w_.Frames();
        while (done < maxFrames && pos_ < total) {
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(
                std::min<std::size_t>(maxFrames - done, kChunk), total - pos_));
            const std::size_t got = src_->Read(w_.dataOffset + pos_ * w_.BlockBytes(), raw_.data(), n * w_.BlockBytes());
            const std::size_t frames = got / w_.BlockBytes();
            if (frames == 0) { pos_ = total; break; }   // the file shrank under us: end
            ConvertWavFrames(w_, raw_.data(), frames, dst + done * static_cast<std::size_t>(channels), channels);
            done += frames;
            pos_ += frames;
        }
        return done;
    }
    bool Seek(std::int64_t frame) override {
        pos_ = static_cast<std::uint64_t>(std::max<std::int64_t>(0, std::min<std::int64_t>(frame, totalFrames)));
        return true;
    }
    std::size_t MemoryBytes() const override { return sizeof(*this) + raw_.capacity(); }
private:
    static constexpr std::size_t kChunk = 1024;
    std::shared_ptr<AudioByteSource> src_;
    WavInfo w_;
    std::vector<unsigned char> raw_;
    std::uint64_t pos_ = 0;
};

class FlacPcmDecoder : public PcmDecoder {
public:
    bool Open(std::shared_ptr<AudioByteSource> src, std::string* error) {
        if (!flac_.Open(std::move(src), error)) return false;
        sampleRate = flac_.SampleRate();
        channels = flac_.Channels();
        totalFrames = flac_.TotalFrames();
        return channels >= 1 && channels <= 8 && sampleRate > 0;
    }
    std::size_t Read(float* dst, std::size_t maxFrames) override { return flac_.Read(dst, maxFrames); }
    bool Seek(std::int64_t frame) override { return flac_.Seek(frame); }
    std::size_t MemoryBytes() const override { return sizeof(*this) + flac_.MemoryBytes(); }
private:
    FlacStream flac_;
};

/// Averages a stereo (or wider) decoder down to one channel.
class ForceMonoDecoder : public PcmDecoder {
public:
    bool Open(std::unique_ptr<PcmDecoder> inner) {
        if (!inner || inner->channels < 2) return false;
        inner_ = std::move(inner);
        sampleRate = inner_->sampleRate;
        channels = 1;
        totalFrames = inner_->totalFrames;
        return true;
    }
    std::size_t Read(float* dst, std::size_t maxFrames) override {
        const int ch = inner_->channels;
        std::size_t done = 0;
        tmp_.resize(kChunk * static_cast<std::size_t>(ch));
        while (done < maxFrames) {
            const std::size_t n = std::min(maxFrames - done, kChunk);
            const std::size_t got = inner_->Read(tmp_.data(), n);
            if (got == 0) break;
            DownmixToMono(tmp_.data(), got, ch, dst + done);
            done += got;
        }
        return done;
    }
    bool Seek(std::int64_t frame) override { return inner_->Seek(frame); }
    std::size_t MemoryBytes() const override {
        return sizeof(*this) + (inner_ ? inner_->MemoryBytes() : 0) + tmp_.capacity() * sizeof(float);
    }
private:
    static constexpr std::size_t kChunk = 1024;
    std::unique_ptr<PcmDecoder> inner_;
    std::vector<float> tmp_;
};

class Mp3PcmDecoder : public PcmDecoder {
public:
    bool Open(std::shared_ptr<AudioByteSource> src, std::string* error) {
        if (!mp3_.Open(std::move(src), error, nullptr, true)) return false;
        sampleRate = mp3_.SampleRate();
        channels = mp3_.Channels();
        totalFrames = mp3_.TotalFrames();
        return true;
    }
    std::size_t Read(float* dst, std::size_t maxFrames) override { return mp3_.Read(dst, maxFrames); }
    bool Seek(std::int64_t frame) override { return mp3_.Seek(frame); }
    std::size_t MemoryBytes() const override { return sizeof(*this) + mp3_.MemoryBytes(); }
private:
    Mp3Stream mp3_;
};

std::unique_ptr<PcmDecoder> MakeDecoder(std::shared_ptr<AudioByteSource> src, std::string* error, bool downmix) {
    unsigned char head[12] = {0};
    const std::size_t got = src->Read(0, head, 12);
    std::string err;
    std::unique_ptr<PcmDecoder> d;
    if (got >= 12 && std::memcmp(head, "RIFF", 4) == 0 && std::memcmp(head + 8, "WAVE", 4) == 0) {
        std::unique_ptr<WavPcmDecoder> w(new WavPcmDecoder);
        if (w->Open(src, &err)) d = std::move(w);
    } else if (got >= 4 && std::memcmp(head, "OggS", 4) == 0) {
        d = MakeOggDecoder(src, &err);
    } else if (got >= 4 && std::memcmp(head, "fLaC", 4) == 0) {
        std::unique_ptr<FlacPcmDecoder> f(new FlacPcmDecoder);
        if (f->Open(src, &err)) d = std::move(f);
    } else {
        std::unique_ptr<Mp3PcmDecoder> m(new Mp3PcmDecoder);
        if (m->Open(src, &err)) d = std::move(m);
        else {
            // FLAC with an ID3 tag in front does not start with fLaC.
            std::unique_ptr<FlacPcmDecoder> f(new FlacPcmDecoder);
            if (f->Open(src, &err)) d = std::move(f);
            else err = "unsupported audio file (use WAV, OGG, MP3 or FLAC)";
        }
    }
    if (d && d->channels > 1 && (downmix || d->channels > 2)) {
        std::unique_ptr<ForceMonoDecoder> mono(new ForceMonoDecoder);
        if (mono->Open(std::move(d))) d = std::move(mono);
    }
    if (!d && error) *error = err;
    return d;
}

} // namespace
} // namespace audio_detail

// ---------------------------------------------------------------------------
// AudioStream
// ---------------------------------------------------------------------------

AudioStream::AudioStream() = default;
AudioStream::~AudioStream() = default;

bool AudioStream::Init(std::unique_ptr<audio_detail::PcmDecoder> dec) {
    if (!dec || dec->sampleRate <= 0 || dec->channels < 1 || dec->channels > 2) return false;
    m_dec = std::move(dec);
    m_rate = m_dec->sampleRate;
    m_channels = m_dec->channels;
    m_total = m_dec->totalFrames;
    m_ring.assign(kRingFrames * static_cast<std::size_t>(m_channels), 0.0f);
    return true;
}

std::shared_ptr<AudioStream> AudioStream::OpenFile(const std::string& path, std::string* error) {
    auto src = AudioByteSource::FromFile(path, error);
    if (!src) return nullptr;
    std::string err;
    const bool mono = ReadAudioImportOptions(path).forceMono;
    auto dec = audio_detail::MakeDecoder(src, &err, mono);
    std::shared_ptr<AudioStream> s(new AudioStream);
    if (!dec || !s->Init(std::move(dec))) {
        if (error) *error = err.empty() ? "not a readable audio file" : err;
        return nullptr;
    }
    return s;
}

std::shared_ptr<AudioStream> AudioStream::OpenMemory(std::shared_ptr<const std::vector<unsigned char>> bytes,
                                                     std::string* error) {
    if (!bytes || bytes->empty()) { if (error) *error = "no audio data"; return nullptr; }
    auto src = AudioByteSource::FromMemory(bytes);
    std::string err;
    auto dec = audio_detail::MakeDecoder(src, &err, false);
    std::shared_ptr<AudioStream> s(new AudioStream);
    if (!dec || !s->Init(std::move(dec))) {
        if (error) *error = err.empty() ? "not a readable audio file" : err;
        return nullptr;
    }
    return s;
}

std::size_t AudioStream::Pump() {
    if (!m_dec) return 0;
    const std::size_t cap = kRingFrames;
    const std::size_t ch = static_cast<std::size_t>(m_channels);
    const std::size_t budget = decodeBudget ? decodeBudget : static_cast<std::size_t>(-1);
    std::size_t decoded = 0;
    while (!m_ended && m_count < cap && decoded < budget) {
        const std::size_t writeIdx = (m_readIdx + m_count) % cap;
        std::size_t n = std::min({cap - m_count, cap - writeIdx, kChunkFrames, budget - decoded});
        const std::size_t got = m_dec->Read(m_ring.data() + writeIdx * ch, n);
        if (got == 0) {
            // End of the data: wrap to the start for a loop, unless the last wrap produced nothing
            // (an empty or unreadable file must not spin).
            if (loop && m_total != 0 && m_sinceRewind > 0 && m_dec->Seek(0)) { m_sinceRewind = 0; continue; }
            m_ended = true;
            break;
        }
        m_count += got;
        m_next += got;
        m_sinceRewind += got;
        decoded += got;
    }
    return decoded;
}

bool AudioStream::Frame(std::uint64_t frame, float* out) const {
    if (!Has(frame) || !out) return false;
    const std::size_t idx = (m_readIdx + static_cast<std::size_t>(frame - m_head)) % kRingFrames;
    const std::size_t ch = static_cast<std::size_t>(m_channels);
    for (std::size_t c = 0; c < ch; ++c) out[c] = m_ring[idx * ch + c];
    return true;
}

void AudioStream::Release(std::uint64_t frame) {
    if (frame <= m_head) return;
    const std::uint64_t drop = std::min(frame, m_next) - m_head;
    m_head += drop;
    m_readIdx = (m_readIdx + static_cast<std::size_t>(drop)) % kRingFrames;
    m_count -= static_cast<std::size_t>(drop);
}

bool AudioStream::Seek(std::int64_t frame) {
    if (!m_dec) return false;
    if (frame < 0) frame = 0;
    if (m_total > 0) frame = loop ? frame % m_total : std::min(frame, m_total);
    bool ok = m_dec->Seek(frame);
    if (!ok) { frame = 0; m_dec->Seek(0); }
    m_origin = frame;
    m_head = m_next = static_cast<std::uint64_t>(frame);
    m_readIdx = m_count = 0;
    m_sinceRewind = 0;
    m_ended = false;
    return ok;
}

std::size_t AudioStream::Read(float* dst, std::size_t frames) {
    if (!dst) return 0;
    const std::size_t ch = static_cast<std::size_t>(m_channels);
    std::size_t done = 0;
    while (done < frames) {
        if (m_count == 0) {
            Pump();
            if (m_count == 0) break;
        }
        const std::size_t n = std::min(frames - done, m_count);
        for (std::size_t i = 0; i < n; ++i) Frame(m_head + i, dst + (done + i) * ch);
        Release(m_head + n);
        done += n;
    }
    return done;
}

std::int64_t AudioStream::Position() const {
    const std::int64_t unrolled = static_cast<std::int64_t>(m_head);
    return (m_total > 0 && loop) ? unrolled % m_total : unrolled;
}

std::size_t AudioStream::MemoryBytes() const {
    return sizeof(*this) + m_ring.capacity() * sizeof(float) + (m_dec ? m_dec->MemoryBytes() : 0);
}

} // namespace okay
