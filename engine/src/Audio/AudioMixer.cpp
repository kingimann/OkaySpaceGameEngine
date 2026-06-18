#include "okay/Audio/AudioMixer.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

int AudioMixer::Render(Scene& scene, float* out, int frames) {
    for (int i = 0; i < frames; ++i) out[i] = 0.0f;

    int mixed = 0;
    for (AudioSource* src : scene.FindObjectsOfType<AudioSource>()) {
        if (!src->m_playing || !src->enabled) continue;
        if (!src->gameObject || !src->gameObject->active) continue;
        const auto& s = src->clip.samples;
        if (s.empty()) { src->m_playing = false; continue; }
        ++mixed;

        for (int i = 0; i < frames; ++i) {
            if (src->m_playhead >= s.size()) {
                if (src->loop) src->m_playhead = 0;
                else { src->m_playing = false; break; }
            }
            out[i] += s[src->m_playhead] * src->volume;
            ++src->m_playhead;
        }
    }

    for (int i = 0; i < frames; ++i) out[i] = Mathf::Clamp(out[i], -1.0f, 1.0f);
    return mixed;
}

} // namespace okay
