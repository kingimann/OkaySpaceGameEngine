#pragma once

namespace okay {

class Scene;

/// Mixes all active AudioSources in a scene into a mono float buffer. Backends
/// (e.g. an SDL audio device) call Render() to pull audio; tests call it
/// directly. Assumes sources share the output sample rate.
struct AudioMixer {
    /// Sum active sources into `out` (length `frames`), clamped to [-1, 1], and
    /// advance each source's playhead. Returns the number of sources mixed.
    static int Render(Scene& scene, float* out, int frames);
};

} // namespace okay
