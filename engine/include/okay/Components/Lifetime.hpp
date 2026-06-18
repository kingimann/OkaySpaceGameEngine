#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {

/// Destroys its GameObject after a fixed number of seconds. The classic way to
/// clean up bullets, explosions, and other short-lived objects without scripts.
class Lifetime : public Behaviour {
public:
    float seconds = 1.0f; // time-to-live from when the scene starts running

    void Update(float dt) override {
        m_elapsed += dt;
        if (m_elapsed >= seconds && gameObject && gameObject->scene())
            gameObject->scene()->Destroy(gameObject);
    }

    float Elapsed() const { return m_elapsed; }
    float Remaining() const { return seconds - m_elapsed; }

private:
    float m_elapsed = 0.0f;
};

} // namespace okay
