#include "okay/Audio/AudioMixer.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
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

        // 3D attenuation: linear rolloff from minDistance (full) to maxDistance
        // (silent), based on distance from the source to the listener.
        float vol = src->volume;
        if (src->spatial && src->transform) {
            float dist = Vec3::Distance(src->transform->Position(), listener);
            float span = src->maxDistance - src->minDistance;
            float atten = span > 1e-4f
                ? 1.0f - (dist - src->minDistance) / span
                : (dist <= src->minDistance ? 1.0f : 0.0f);
            vol *= Mathf::Clamp(atten, 0.0f, 1.0f);
        }

        for (int i = 0; i < frames; ++i) {
            if (src->m_playhead >= s.size()) {
                if (src->loop) src->m_playhead = 0;
                else { src->m_playing = false; break; }
            }
            out[i] += s[src->m_playhead] * vol;
            ++src->m_playhead;
        }
    }

    float gain = muted ? 0.0f : masterVolume;
    for (int i = 0; i < frames; ++i) out[i] = Mathf::Clamp(out[i] * gain, -1.0f, 1.0f);
    return mixed;
}

} // namespace okay
