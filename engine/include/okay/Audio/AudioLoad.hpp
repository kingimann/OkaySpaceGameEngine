#pragma once
#include "okay/Audio/AudioClip.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <string>

namespace okay {

/// Load the file of every AudioSource in `scene` that names one (WAV, OGG or
/// MP3), resampled to `rate`. A relative path is tried as is, then under
/// `baseDir`. Files are decoded once and shared (AudioClip::LoadCached).
/// Returns how many sources got their clip; `failed` collects the paths that
/// could not be read.
inline int LoadSceneAudio(Scene& scene, const std::string& baseDir, int rate,
                          std::vector<std::string>* failed = nullptr) {
    int loaded = 0;
    for (AudioSource* au : scene.FindObjectsOfType<AudioSource>()) {
        if (au->clipPath.empty()) continue;
        auto clip = AudioClip::LoadCached(au->clipPath, rate);
        if (!clip && !baseDir.empty()) {
            const char last = baseDir.back();
            const std::string sep = (last == '/' || last == '\\') ? "" : "/";
            clip = AudioClip::LoadCached(baseDir + sep + au->clipPath, rate);
        }
        if (clip) { au->clip = *clip; ++loaded; }
        else if (failed) failed->push_back(au->clipPath);
    }
    return loaded;
}

} // namespace okay
