#pragma once
#include "okay/Audio/AudioClip.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <string>

namespace okay {

/// Folder relative audio paths are tried under (the project in the editor, the
/// game's data folder in a build). LoadSceneAudio sets it.
inline std::string& AudioBaseDir() { static std::string dir; return dir; }

/// Decode `path` (WAV, OGG or MP3) at the mixer's rate, trying it as given and
/// then under AudioBaseDir(). Shared and decoded once (AudioClip::LoadCached).
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

/// Load the file of every AudioSource in `scene` that names one, resampled to
/// `rate`, and remember `baseDir` for later loads (MusicPlayer tracks).
/// Returns how many sources got their clip; `failed` collects the paths that
/// could not be read.
inline int LoadSceneAudio(Scene& scene, const std::string& baseDir, int rate,
                          std::vector<std::string>* failed = nullptr) {
    AudioBaseDir() = baseDir;
    int loaded = 0;
    for (AudioSource* au : scene.FindObjectsOfType<AudioSource>()) {
        if (au->clipPath.empty()) continue;
        if (auto clip = LoadAudioFile(au->clipPath, rate)) { au->clip = *clip; ++loaded; }
        else if (failed) failed->push_back(au->clipPath);
    }
    return loaded;
}

} // namespace okay
