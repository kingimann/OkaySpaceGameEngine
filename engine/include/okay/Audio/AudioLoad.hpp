#pragma once
#include "okay/Audio/AudioClip.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Audio/AudioStream.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cstdlib>
#include <fstream>
#include <string>

namespace okay {

/// Folder relative audio paths are tried under (the project in the editor, the
/// game's data folder in a build). LoadSceneAudio sets it.
inline std::string& AudioBaseDir() { static std::string dir; return dir; }

/// The path a load will actually open: `path` when that file is there, otherwise
/// the same name under AudioBaseDir(), otherwise `path` unchanged.
inline std::string ResolveAudioPath(const std::string& path) {
    if (path.empty()) return path;
    auto exists = [](const std::string& p) { std::ifstream in(p, std::ios::binary); return static_cast<bool>(in); };
    if (exists(path)) return path;
    const std::string& base = AudioBaseDir();
    if (base.empty()) return path;
    const char last = base.back();
    const std::string full = base + ((last == '/' || last == '\\') ? "" : "/") + path;
    return exists(full) ? full : path;
}

/// True when the file's import sidecar says to read it while it plays.
inline bool AudioImportWantsStream(const std::string& path) {
    return ReadAudioImportOptions(ResolveAudioPath(path)).stream;
}

/// Long sounds stream by default (asset streaming, feature map 2.13): a file that plays for at least this many seconds
/// is read in small pieces while it plays instead of being decoded whole when the scene loads (a 3 minute stereo song
/// is about 30 MB decoded, a stream holds well under 1 MB). 30 seconds; OKAY_AUDIO_STREAM_SECONDS changes it (0 = never
/// by length). A file whose import settings say "Always load the whole file" (`set loadMode memory` in its .okaymeta)
/// is always decoded; the source's Stream box or the "Read it while it plays" import setting always streams.
inline float& AudioStreamThresholdSeconds() {
    static float s = [] {
        const char* e = std::getenv("OKAY_AUDIO_STREAM_SECONDS");
        const float v = e ? static_cast<float>(std::atof(e)) : 30.0f;
        return v >= 0.0f ? v : 30.0f;
    }();
    return s;
}

/// The length of a sound file in seconds without decoding it (0 when unknown).
inline float AudioFileSeconds(const std::string& path) {
    auto st = AudioStream::OpenFile(ResolveAudioPath(path));
    return st ? st->Duration() : 0.0f;
}

/// Whether `path` should play as a stream on `au`: the Stream box, the file's "Stream" import setting, or (unless
/// the import settings say "memory") a length of at least AudioStreamThresholdSeconds().
inline bool AudioShouldStream(const AudioSource& au, const std::string& path) {
    if (au.stream) return true;
    const std::string resolved = ResolveAudioPath(path);
    if (ReadAudioImportOptions(resolved).stream) return true;
    if (ImportLower(ReadImportSetting(resolved, "audio", "loadMode")) == "memory") return false;
    const float limit = AudioStreamThresholdSeconds();
    return limit > 0.0f && AudioFileSeconds(resolved) >= limit;
}

/// How AttachAudioFile left the source.
enum class AudioAttach { Failed, Clip, Stream };

/// Put `path` on `au`. Streams when the source's Stream box is on, the file's
/// import sidecar says to read it while it plays, or it is long (AudioShouldStream);
/// otherwise decodes the whole file. Mix down to one channel comes from the sidecar inside the loaders.
inline AudioAttach AttachAudioFile(AudioSource& au, const std::string& path, std::string* error = nullptr) {
    if (path.empty()) { if (error) *error = "no audio file"; return AudioAttach::Failed; }
    if (AudioShouldStream(au, path))
        return au.OpenStream(path, error) ? AudioAttach::Stream : AudioAttach::Failed;
    au.UseStream(nullptr);
    return au.clip.Load(path, error) ? AudioAttach::Clip : AudioAttach::Failed;
}

/// Decode `path` (WAV, OGG, MP3 or FLAC) at the mixer's rate, trying it as given and
/// then under AudioBaseDir(). Shared and decoded once (AudioClip::LoadCached).
/// Mix down to one channel in the file's sidecar is applied. A sidecar that says
/// to read while playing does not change this call: the caller wanted the samples.
inline std::shared_ptr<const AudioClip> LoadAudioFile(const std::string& path, int rate = 0) {
    if (path.empty()) return nullptr;
    if (rate <= 0) rate = AudioMixer::sampleRate;
    auto clip = AudioClip::LoadCached(path, rate);
    const std::string& base = AudioBaseDir();
    if (!clip && !base.empty()) {
        const char last = base.back();
        clip = AudioClip::LoadCached(base + ((last == '/' || last == '\\') ? "" : "/") + path, rate);
    }
    return clip;
}

/// Open `path` (WAV, OGG, MP3 or FLAC) as a stream, trying it as given and then under
/// AudioBaseDir(). A stream of its own each call (position is per source).
inline std::shared_ptr<AudioStream> OpenAudioStream(const std::string& path) {
    if (path.empty()) return nullptr;
    auto st = AudioStream::OpenFile(path);
    const std::string& base = AudioBaseDir();
    if (!st && !base.empty()) {
        const char last = base.back();
        st = AudioStream::OpenFile(base + ((last == '/' || last == '\\') ? "" : "/") + path);
    }
    return st;
}

/// Load the file of every AudioSource in `scene` that names one, resampled to
/// `rate` (sources with `stream` set open a stream instead), and remember `baseDir` for later loads (MusicPlayer tracks).
/// Returns how many sources got their clip; `failed` collects the paths that
/// could not be read.
inline int LoadSceneAudio(Scene& scene, const std::string& baseDir, int rate,
                          std::vector<std::string>* failed = nullptr) {
    AudioBaseDir() = baseDir;
    int loaded = 0;
    for (AudioSource* au : scene.FindObjectsOfType<AudioSource>()) {
        if (au->clipPath.empty()) continue;
        // The source's Stream box, the file's own "read it while it plays" import setting, or a long file.
        if (AudioShouldStream(*au, au->clipPath)) {
            if (auto st = OpenAudioStream(au->clipPath)) { au->UseStream(std::move(st)); ++loaded; }
            else if (failed) failed->push_back(au->clipPath);
            continue;
        }
        if (auto clip = LoadAudioFile(au->clipPath, rate)) {
            au->UseSharedClip(clip);   // one decoded copy for every source on this file: no per-source copy of the samples
            ++loaded;
        }
        else if (failed) failed->push_back(au->clipPath);
    }
    return loaded;
}

} // namespace okay
