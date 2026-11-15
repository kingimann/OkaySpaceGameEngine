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
#include "okay/Debug/DebugUtil.hpp"
#include "AudioInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
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

namespace {
// The shared decode cache. An entry remembers the file's size and write time when it was decoded; a hit whose
// file has changed since is decoded again (so an edited or replaced sound never plays stale).
struct CacheEntry { std::shared_ptr<const AudioClip> clip; std::uintmax_t size = 0; std::int64_t time = 0; std::uint64_t used = 0; };
debugutil::SpinLock& CacheLock() { static debugutil::SpinLock l; return l; }
std::map<std::pair<std::string, int>, CacheEntry>& CacheMap() { static std::map<std::pair<std::string, int>, CacheEntry> m; return m; }
std::uint64_t& CacheTick() { static std::uint64_t t = 0; return t; }

std::string CacheName(const std::string& path) {   // one key for "a/./b.wav", "a/b.wav" and an absolute spelling of it
    std::error_code ec;
    const auto p = std::filesystem::weakly_canonical(std::filesystem::u8path(path), ec);
    return ec ? path : p.generic_u8string();
}
void FileStamp(const std::string& path, std::uintmax_t& size, std::int64_t& time) {
    std::error_code ec;
    const auto fp = std::filesystem::u8path(path);
    size = std::filesystem::file_size(fp, ec); if (ec) size = 0;
    const auto t = std::filesystem::last_write_time(fp, ec);
    time = ec ? 0 : (std::int64_t)t.time_since_epoch().count();
}
std::size_t EntryBytes(const CacheEntry& e) { return e.clip ? e.clip->samples.size() * sizeof(float) : 0; }
}  // namespace

std::shared_ptr<const AudioClip> AudioClip::LoadCached(const std::string& path, int rate, std::string* error, bool downmix) {
    const bool mono = downmix || ReadAudioImportOptions(path).forceMono;
    const auto key = std::make_pair(CacheName(path) + (mono ? "\n:mono" : ""), rate);
    std::uintmax_t size = 0; std::int64_t time = 0;
    FileStamp(path, size, time);
    {
        debugutil::SpinGuard lock(CacheLock());
        auto& cache = CacheMap();
        auto it = cache.find(key);
        if (it != cache.end()) {
            if (it->second.size == size && it->second.time == time) { it->second.used = ++CacheTick(); return it->second.clip; }
            cache.erase(it);   // the file changed: decode it again (clips already handed out keep playing their copy)
        }
    }
    AudioClip c;
    std::string loadErr;
    if (!c.Load(path, &loadErr, mono)) {
        Log::Warning("[asset] cannot load audio '", path, "': ", loadErr);
        if (error) *error = loadErr;
        return nullptr;
    }
    auto shared = std::make_shared<const AudioClip>(rate > 0 && c.sampleRate != rate ? c.Resampled(rate) : std::move(c));
    debugutil::SpinGuard lock(CacheLock());
    auto& cache = CacheMap();
    auto it = cache.find(key);   // another thread may have decoded it meanwhile: keep one shared copy
    if (it != cache.end() && it->second.size == size && it->second.time == time) { it->second.used = ++CacheTick(); return it->second.clip; }
    cache[key] = CacheEntry{shared, size, time, ++CacheTick()};
    return shared;
}

void AudioClip::ClearCache() {
    debugutil::SpinGuard lock(CacheLock());
    CacheMap().clear();
}

std::size_t AudioClip::CacheBytes() {
    debugutil::SpinGuard lock(CacheLock());
    std::size_t n = 0;
    for (const auto& kv : CacheMap()) n += EntryBytes(kv.second);
    return n;
}

std::size_t AudioClip::CacheCount() {
    debugutil::SpinGuard lock(CacheLock());
    return CacheMap().size();
}

void AudioClip::TrimCache(std::size_t maxBytes) {
    debugutil::SpinGuard lock(CacheLock());
    auto& cache = CacheMap();
    std::size_t total = 0;
    for (const auto& kv : cache) total += EntryBytes(kv.second);
    while (total > maxBytes) {   // drop the least recently used clip that no source or voice is holding
        auto victim = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (it->second.clip.use_count() == 1 && (victim == cache.end() || it->second.used < victim->second.used)) victim = it;
        if (victim == cache.end()) break;   // everything left is in use
        total -= EntryBytes(victim->second);
        cache.erase(victim);
    }
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
