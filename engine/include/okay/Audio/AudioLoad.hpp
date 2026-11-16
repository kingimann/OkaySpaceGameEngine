#pragma once
#include "okay/Audio/AudioClip.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Audio/AudioStream.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Core/ImportSidecar.hpp"
#include "okay/Core/Paths.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <fstream>
#include <string>

namespace okay {

/// Folder relative audio paths are tried under (the project in the editor, the
/// game's data folder in a build). LoadSceneAudio sets it.
inline std::string& AudioBaseDir() { static std::string dir; return dir; }

/// The path a load will actually open: `path` when that file is there (or it is absolute), otherwise
/// the same name under AudioBaseDir(), otherwise `path` unchanged. The rule textures and fonts use too
/// (Paths::FindFile), with the audio folder as the base.
inline std::string ResolveAudioPath(const std::string& path) {
    return Paths::FindFile(path, AudioBaseDir());
}

/// True when the file's import sidecar says to read it while it plays.
inline bool AudioImportWantsStream(const std::string& path) {
    return ReadAudioImportOptions(ResolveAudioPath(path)).stream;
}

/// How AttachAudioFile left the source.
enum class AudioAttach { Failed, Clip, Stream };

/// Put `path` on `au`. Streams when the source's Stream box is on or the file's
/// import sidecar says to read it while it plays; otherwise decodes the whole
/// file. Mix down to one channel comes from the sidecar inside the loaders.
inline AudioAttach AttachAudioFile(AudioSource& au, const std::string& path, std::string* error = nullptr) {
    if (path.empty()) { if (error) *error = "no audio file"; return AudioAttach::Failed; }
    const std::string file = ResolveAudioPath(path);   // as written, else under AudioBaseDir()
    if (au.stream || AudioImportWantsStream(path))
        return au.OpenStream(file, error) ? AudioAttach::Stream : AudioAttach::Failed;
    au.UseStream(nullptr);
    return au.clip.Load(file, error) ? AudioAttach::Clip : AudioAttach::Failed;
}

/// Decode `path` (WAV, OGG, MP3 or FLAC) at the mixer's rate: the file as given when it is
/// there, else under AudioBaseDir() (ResolveAudioPath). Shared and decoded once (AudioClip::LoadCached).
/// One load of the file that exists, so a clip found under the base folder logs no "cannot load audio"
/// line; a file missing everywhere logs one, naming the path as written.
/// Mix down to one channel in the file's sidecar is applied. A sidecar that says
/// to read while playing does not change this call: the caller wanted the samples.
inline std::shared_ptr<const AudioClip> LoadAudioFile(const std::string& path, int rate = 0) {
    if (path.empty()) return nullptr;
    if (rate <= 0) rate = AudioMixer::sampleRate;
    return AudioClip::LoadCached(ResolveAudioPath(path), rate);
}

/// Open `path` (WAV, OGG, MP3 or FLAC) as a stream: the file as given when it is there, else
/// under AudioBaseDir() (ResolveAudioPath). A stream of its own each call (position is per source).
inline std::shared_ptr<AudioStream> OpenAudioStream(const std::string& path) {
    if (path.empty()) return nullptr;
    return AudioStream::OpenFile(ResolveAudioPath(path));
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
        // The source's Stream box, or the file's own "read it while it plays" import setting.
        if (au->stream || AudioImportWantsStream(au->clipPath)) {
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
