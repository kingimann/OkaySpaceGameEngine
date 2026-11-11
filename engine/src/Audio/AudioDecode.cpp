// OGG Vorbis and MP3 decoding for AudioClip, plus Load() by file extension and
// the shared decode cache, and the OGG decoder the streaming code uses. OGG goes through the vendored stb_vorbis (public
// domain, compiled only in this file with warnings off, see
// engine/CMakeLists.txt); MP3 goes through the engine's own decoder
// (Mp3Decoder.cpp).
#include "okay/Audio/AudioClip.hpp"
#include "okay/Audio/Mp3Decoder.hpp"
#include "okay/Core/Log.hpp"
#include "AudioInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
// stb_vorbis leaks single-letter macros (C, L, R) into the rest of the file.
#undef C
#undef L
#undef R

namespace okay {

namespace {

bool ReadFile(const std::string& path, std::vector<unsigned char>& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

std::string LowerExt(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

} // namespace

bool AudioClip::LoadOGG(const std::string& path, std::string* error, bool downmix) {
    samples.clear();
    channels = 1;
    std::vector<unsigned char> buf;
    if (!ReadFile(path, buf, error)) return false;
    int fileCh = 0, rate = 0;
    short* pcm = nullptr;
    const int frames = stb_vorbis_decode_memory(buf.data(), (int)buf.size(), &fileCh, &rate, &pcm);
    if (frames <= 0 || !pcm || fileCh <= 0) {
        if (pcm) std::free(pcm);
        if (error) *error = "not a readable OGG Vorbis file";
        return false;
    }
    std::vector<float> f(static_cast<std::size_t>(frames) * fileCh);
    for (std::size_t i = 0; i < f.size(); ++i) f[i] = pcm[i] / 32768.0f;
    std::free(pcm);
    const int outCh = audio_detail::KeptChannels(fileCh, downmix);
    if (outCh == fileCh) samples = std::move(f);
    else {
        samples.resize(static_cast<std::size_t>(frames));
        audio_detail::DownmixToMono(f.data(), (std::size_t)frames, fileCh, samples.data());
    }
    channels = outCh;
    sampleRate = rate;
    return true;
}

bool AudioClip::LoadMP3(const std::string& path, std::string* error, bool downmix) {
    samples.clear();
    channels = 1;
    std::vector<unsigned char> buf;
    if (!ReadFile(path, buf, error)) return false;
    Mp3Audio audio;
    if (!DecodeMp3(buf.data(), buf.size(), audio, nullptr) || audio.FrameCount() == 0) {
        if (error) *error = "not a readable MP3 file";
        return false;
    }
    const int outCh = audio_detail::KeptChannels(audio.channels, downmix);
    if (outCh == audio.channels) samples = std::move(audio.samples);
    else {
        samples.resize(audio.FrameCount());
        audio_detail::DownmixToMono(audio.samples.data(), audio.FrameCount(), audio.channels, samples.data());
    }
    channels = outCh;
    sampleRate = audio.sampleRate;
    return true;
}

bool AudioClip::Load(const std::string& path, std::string* error, bool downmix) {
    const std::string ext = LowerExt(path);
    if (ext == "ogg") return LoadOGG(path, error, downmix);
    if (ext == "mp3") return LoadMP3(path, error, downmix);
    if (ext == "wav") return LoadWAV(path, error, downmix);
    // Unknown extension: try each format in turn.
    if (LoadWAV(path, nullptr, downmix) || LoadOGG(path, nullptr, downmix) || LoadMP3(path, nullptr, downmix)) return true;
    if (error) *error = "unsupported audio file (use WAV, OGG or MP3): " + path;
    return false;
}

bool AudioClip::IsAudioFile(const std::string& path) {
    const std::string ext = LowerExt(path);
    return ext == "wav" || ext == "ogg" || ext == "mp3";
}

std::shared_ptr<const AudioClip> AudioClip::LoadCached(const std::string& path, int rate, std::string* error, bool downmix) {
    static std::mutex mu;
    static std::map<std::pair<std::string, int>, std::shared_ptr<const AudioClip>> cache;
    const auto key = std::make_pair(path + (downmix ? "\n:mono" : ""), rate);
    {
        std::lock_guard<std::mutex> lock(mu);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    AudioClip c;
    std::string loadErr;
    if (!c.Load(path, &loadErr, downmix)) {
        Log::Warning("[asset] cannot load audio '", path, "': ", loadErr);
        if (error) *error = loadErr;
        return nullptr;
    }
    auto shared = std::make_shared<const AudioClip>(rate > 0 && c.sampleRate != rate ? c.Resampled(rate) : std::move(c));
    std::lock_guard<std::mutex> lock(mu);
    cache[key] = shared;
    return shared;
}

// ---------------------------------------------------------------------------
// Streaming OGG decoder (stb_vorbis reads from disk or memory as it goes)
// ---------------------------------------------------------------------------

namespace audio_detail {
namespace {

class OggPcmDecoder : public PcmDecoder {
public:
    ~OggPcmDecoder() override { if (v_) stb_vorbis_close(v_); }

    bool Open(const std::string& path, std::shared_ptr<const std::vector<unsigned char>> mem, std::string* error) {
        int err = 0;
        if (!path.empty()) {
            std::FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) { if (error) *error = "cannot open " + path; return false; }
            v_ = stb_vorbis_open_file(f, 1, &err, nullptr);   // closes f with the decoder, also when opening fails
        } else if (mem && !mem->empty() && mem->size() < 0x7fffffffu) {
            mem_ = std::move(mem);
            v_ = stb_vorbis_open_memory(mem_->data(), static_cast<int>(mem_->size()), &err, nullptr);
        }
        if (!v_) { if (error) *error = "not a readable OGG Vorbis file"; return false; }
        const stb_vorbis_info info = stb_vorbis_get_info(v_);
        fileCh_ = info.channels;
        if (fileCh_ < 1 || fileCh_ > 16 || info.sample_rate == 0) {
            if (error) *error = "not a readable OGG Vorbis file";
            return false;
        }
        sampleRate = static_cast<int>(info.sample_rate);
        channels = fileCh_ > 2 ? 1 : fileCh_;
        const unsigned len = stb_vorbis_stream_length_in_samples(v_);
        totalFrames = len > 0 ? static_cast<std::int64_t>(len) : -1;
        setupBytes_ = info.setup_memory_required + info.setup_temp_memory_required + info.temp_memory_required;
        tmp_.resize(static_cast<std::size_t>(kChunk) * static_cast<std::size_t>(fileCh_));
        return true;
    }

    std::size_t Read(float* dst, std::size_t maxFrames) override {
        std::size_t done = 0;
        while (done < maxFrames) {
            const std::size_t want = std::min<std::size_t>(maxFrames - done, kChunk);
            const int n = stb_vorbis_get_samples_short_interleaved(v_, fileCh_, tmp_.data(), static_cast<int>(want) * fileCh_);
            if (n <= 0) break;
            float* out = dst + done * static_cast<std::size_t>(channels);
            if (fileCh_ <= 2) {
                for (std::size_t i = 0; i < static_cast<std::size_t>(n) * static_cast<std::size_t>(fileCh_); ++i) out[i] = tmp_[i] / 32768.0f;
            } else {
                // Same arithmetic as the whole-file path: scale each sample, then average.
                std::vector<float> f(static_cast<std::size_t>(n) * static_cast<std::size_t>(fileCh_));
                for (std::size_t i = 0; i < f.size(); ++i) f[i] = tmp_[i] / 32768.0f;
                DownmixToMono(f.data(), static_cast<std::size_t>(n), fileCh_, out);
            }
            done += static_cast<std::size_t>(n);
        }
        return done;
    }

    bool Seek(std::int64_t frame) override {
        if (frame < 0) frame = 0;
        if (frame == 0) return stb_vorbis_seek_start(v_) != 0;
        return stb_vorbis_seek(v_, static_cast<unsigned>(frame)) != 0;
    }

    std::size_t MemoryBytes() const override {
        return sizeof(*this) + tmp_.capacity() * sizeof(short) + setupBytes_ + 4096 /* stdio buffer */;
    }

private:
    static constexpr int kChunk = 1024;
    stb_vorbis* v_ = nullptr;
    std::shared_ptr<const std::vector<unsigned char>> mem_;
    int fileCh_ = 0;
    std::size_t setupBytes_ = 0;
    std::vector<short> tmp_;
};

} // namespace

std::unique_ptr<PcmDecoder> MakeOggDecoder(const std::string& path, std::shared_ptr<const std::vector<unsigned char>> mem,
                                           std::string* error) {
    std::unique_ptr<OggPcmDecoder> d(new OggPcmDecoder);
    if (!d->Open(path, std::move(mem), error)) return nullptr;
    return d;
}

} // namespace audio_detail

} // namespace okay
