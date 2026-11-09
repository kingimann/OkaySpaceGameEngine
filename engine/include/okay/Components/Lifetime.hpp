#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

/// Destroys its GameObject after a fixed number of seconds. The classic way to
/// clean up bullets, explosions, and other short-lived objects without scripts.
/// Time counts only while the component runs: disabling it (or its object) pauses the
/// timer, and the scene's time scale and pauses apply because it counts the frame times
/// it is given. Zero or less ends it on the first frame; infinity never ends it. The
/// seconds already lived are saved with the scene, so a saved game carries on.
class Lifetime : public Behaviour {
public:
    float seconds = 1.0f; // time-to-live from when the scene starts running

    void Update(float dt) override {
        if (std::isfinite(dt) && dt > 0.0f) m_elapsed += dt;   // a negative or NaN frame time is not time
        if (m_elapsed >= seconds && gameObject && gameObject->scene())
            gameObject->scene()->Destroy(gameObject);
    }

    float Elapsed() const { return m_elapsed; }
    float Remaining() const { return seconds - m_elapsed; }
    /// Set the time already lived (clamped to 0 or more; a NaN or infinity is ignored).
    void SetElapsed(float lived) { if (std::isfinite(lived)) m_elapsed = std::max(0.0f, lived); }
    /// Start the timer again.
    void Restart() { m_elapsed = 0.0f; }

private:
    float m_elapsed = 0.0f;
};

} // namespace okay
