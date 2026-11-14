// OGG Vorbis, MP3 and FLAC decoding for AudioClip, plus Load() by file extension and
// the shared decode cache, and the OGG decoder the streaming code uses. OGG goes
// through the engine's own decoder (VorbisDecoder.cpp, whole file or
// incrementally through VorbisStream); MP3 through Mp3Decoder.cpp; FLAC through
// FlacDecoder.cpp.
#include "okay/Audio/AudioClip.hpp"
#include "okay/Audio/FlacDecoder.hpp"
#include "okay/Audio/Mp3Decoder.hpp"
#include "okay/Audio/VorbisDecoder.hpp"
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Core/Log.hpp"
#include "AudioInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

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
    VorbisAudio audio;
    if (!DecodeVorbis(buf.data(), buf.size(), audio, nullptr) || audio.FrameCount() == 0) {
        if (error) *error = "not a readable OGG Vorbis file";
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

bool AudioClip::LoadFLAC(const std::string& path, std::string* error, bool downmix) {
    samples.clear();
    channels = 1;
    std::vector<unsigned char> buf;
    if (!ReadFile(path, buf, error)) return false;
    FlacAudio audio;
    if (!DecodeFlac(buf.data(), buf.size(), audio, nullptr) || audio.FrameCount() == 0) {
        if (error) *error = "not a readable FLAC file";
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
    const bool mono = downmix || ReadAudioImportOptions(path).forceMono;
    const std::string ext = LowerExt(path);
    if (ext == "ogg") return LoadOGG(path, error, mono);
    if (ext == "mp3") return LoadMP3(path, error, mono);
    if (ext == "wav") return LoadWAV(path, error, mono);
    if (ext == "flac") return LoadFLAC(path, error, mono);
    // Unknown extension: try each format in turn.
    if (LoadWAV(path, nullptr, mono) || LoadOGG(path, nullptr, mono) || LoadMP3(path, nullptr, mono) ||
        LoadFLAC(path, nullptr, mono))
        return true;
    if (error) *error = "unsupported audio file (use WAV, OGG, MP3 or FLAC): " + path;
    return false;
}

bool AudioClip::IsAudioFile(const std::string& path) {
    const std::string ext = LowerExt(path);
    return ext == "wav" || ext == "ogg" || ext == "mp3" || ext == "flac";
}

std::shared_ptr<const AudioClip> AudioClip::LoadCached(const std::string& path, int rate, std::string* error, bool downmix) {
    static std::mutex mu;
    static std::map<std::pair<std::string, int>, std::shared_ptr<const AudioClip>> cache;
    const bool mono = downmix || ReadAudioImportOptions(path).forceMono;
    const auto key = std::make_pair(path + (mono ? "\n:mono" : ""), rate);
    {
        std::lock_guard<std::mutex> lock(mu);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    AudioClip c;
    std::string loadErr;
    if (!c.Load(path, &loadErr, mono)) {
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
// Streaming OGG decoder (our own VorbisStream: pages are read from the source as they are needed)
// ---------------------------------------------------------------------------

namespace audio_detail {
namespace {

class OggPcmDecoder : public PcmDecoder {
public:
    bool Open(std::shared_ptr<AudioByteSource> src, std::string* error) {
        if (!vorbis_.Open(std::move(src), error)) return false;
        fileCh_ = vorbis_.Channels();
        sampleRate = vorbis_.SampleRate();
        channels = fileCh_ > 2 ? 1 : fileCh_;
        totalFrames = vorbis_.TotalFrames();
        return true;
    }

    std::size_t Read(float* dst, std::size_t maxFrames) override {
        if (fileCh_ <= 2) return vorbis_.Read(dst, maxFrames);
        std::size_t done = 0;
        tmp_.resize(static_cast<std::size_t>(kChunk) * static_cast<std::size_t>(fileCh_));
        while (done < maxFrames) {
            const std::size_t want = std::min<std::size_t>(maxFrames - done, kChunk);
            const std::size_t n = vorbis_.Read(tmp_.data(), want);
            if (n == 0) break;
            DownmixToMono(tmp_.data(), n, fileCh_, dst + done);
            done += n;
        }
        return done;
    }

    bool Seek(std::int64_t frame) override { return vorbis_.Seek(frame); }
    std::size_t MemoryBytes() const override { return sizeof(*this) + vorbis_.MemoryBytes() + tmp_.capacity() * sizeof(float); }

private:
    static constexpr std::size_t kChunk = 1024;
    VorbisStream vorbis_;
    int fileCh_ = 0;
    std::vector<float> tmp_;
};

} // namespace

std::unique_ptr<PcmDecoder> MakeOggDecoder(std::shared_ptr<AudioByteSource> src, std::string* error) {
    std::unique_ptr<OggPcmDecoder> d(new OggPcmDecoder);
    if (!d->Open(std::move(src), error)) return nullptr;
    return d;
}

} // namespace audio_detail

} // namespace okay
