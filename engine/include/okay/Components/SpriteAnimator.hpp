#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include <string>
#include <vector>

namespace okay {

/// Flip-book sprite animation: cycles a SpriteRenderer's texture through a list
/// of image frames at a fixed frame rate. Add it next to a SpriteRenderer and
/// fill `frames` with image paths — the classic way to animate a 2D character.
class SpriteAnimator : public Behaviour {
public:
    std::vector<std::string> frames; // texture paths, played in order
    float fps = 8.0f;                // frames per second
    bool  loop = true;
    bool  playing = true;

    void Start() override { Apply(); }

    void Update(float dt) override {
        if (!playing || frames.empty() || fps <= 0.0f) return;
        m_time += dt;
        int frame = static_cast<int>(m_time * fps);
        if (loop) {
            frame %= static_cast<int>(frames.size());
        } else if (frame >= static_cast<int>(frames.size())) {
            frame = static_cast<int>(frames.size()) - 1;
            playing = false;
        }
        if (frame != m_frame) { m_frame = frame; Apply(); }
    }

    int CurrentFrame() const { return m_frame; }
    void Restart() { m_time = 0.0f; m_frame = 0; playing = true; Apply(); }

private:
    void Apply() {
        if (frames.empty() || !gameObject) return;
        if (auto* sr = gameObject->GetComponent<SpriteRenderer>())
            sr->texture = frames[m_frame >= 0 && m_frame < (int)frames.size() ? m_frame : 0];
    }

    float m_time = 0.0f;
    int   m_frame = 0;
};

} // namespace okay
