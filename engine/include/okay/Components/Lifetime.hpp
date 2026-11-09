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
/// it is given. Zero or less ends it on the first frame; infinity and NaN never end it. The
/// seconds already lived are saved with the scene, so a saved game carries on. The time is summed in
/// double precision, so a lifetime of days still ends on time (a float stops counting small steps
/// past about 6 days).
class Lifetime : public Behaviour {
public:
    float seconds = 1.0f; // time-to-live from when the scene starts running

    void Update(float dt) override {
        if (std::isfinite(dt) && dt > 0.0f) m_elapsed += static_cast<double>(dt);   // a negative or NaN frame time is not time
        if (m_elapsed >= static_cast<double>(seconds) && gameObject && gameObject->scene())
            gameObject->scene()->Destroy(gameObject);
    }

    float Elapsed() const { return static_cast<float>(m_elapsed); }
    float Remaining() const { return static_cast<float>(static_cast<double>(seconds) - m_elapsed); }
    /// Set the time already lived (clamped to 0 or more; a NaN or infinity is ignored).
    void SetElapsed(float lived) { if (std::isfinite(lived)) m_elapsed = std::max(0.0, static_cast<double>(lived)); }
    /// Start the timer again.
    void Restart() { m_elapsed = 0.0; }

private:
    double m_elapsed = 0.0;
};

} // namespace okay
