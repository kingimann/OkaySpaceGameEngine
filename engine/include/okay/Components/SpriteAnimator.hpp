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
    std::vector<std::string> frames; // texture paths, played in order (list mode)
    float fps = 8.0f;                // frames per second
    bool  loop = true;
    bool  playing = true;

    // Atlas mode: when atlasColumns > 0, animate by walking cells of the
    // SpriteRenderer's existing texture (a sprite sheet) instead of swapping
    // files. `atlasCount` (0 = columns*rows) caps the number of cells played.
    int atlasColumns = 0;
    int atlasRows = 1;
    int atlasCount = 0;

    void Start() override { Apply(); }

    void Update(float dt) override {
        int count = FrameCount();
        if (!playing || count <= 0 || fps <= 0.0f) return;
        m_time += dt;
        int frame = static_cast<int>(m_time * fps);
        if (loop) {
            frame %= count;
        } else if (frame >= count) {
            frame = count - 1;
            playing = false;
        }
        if (frame != m_frame) { m_frame = frame; Apply(); }
    }

    int CurrentFrame() const { return m_frame; }
    int FrameCount() const {
        if (atlasColumns > 0) {
            int total = atlasColumns * (atlasRows > 0 ? atlasRows : 1);
            return atlasCount > 0 ? (atlasCount < total ? atlasCount : total) : total;
        }
        return static_cast<int>(frames.size());
    }
    void Restart() { m_time = 0.0f; m_frame = 0; playing = true; Apply(); }

private:
    void Apply() {
        if (!gameObject) return;
        auto* sr = gameObject->GetComponent<SpriteRenderer>();
        if (!sr) return;
        if (atlasColumns > 0) {
            int rows = atlasRows > 0 ? atlasRows : 1;
            int col = m_frame % atlasColumns;
            int row = (m_frame / atlasColumns) % rows;
            float uw = 1.0f / atlasColumns, vh = 1.0f / rows;
            sr->uvMin = {col * uw, row * vh};
            sr->uvMax = {(col + 1) * uw, (row + 1) * vh};
        } else if (!frames.empty()) {
            int i = m_frame >= 0 && m_frame < (int)frames.size() ? m_frame : 0;
            sr->texture = frames[i];
        }
    }

    float m_time = 0.0f;
    int   m_frame = 0;
};

} // namespace okay
