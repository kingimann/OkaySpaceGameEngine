#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace okay {

/// One named animation for a SpriteAnimator ("idle", "run", "jump", ...). It plays
/// its own `frames`, or — when that list is empty — one row of the atlas.
struct SpriteClip {
    std::string name;
    std::vector<std::string> frames; // texture paths; empty = use atlas row `row`
    int   row  = 0;                  // atlas row (when frames is empty)
    float fps  = 8.0f;
    bool  loop = true;
};

/// Flip-book sprite animation: cycles a SpriteRenderer's texture through a list
/// of image frames at a fixed frame rate. Add it next to a SpriteRenderer and
/// fill `frames` with image paths — the classic way to animate a 2D character.
/// Optionally add named `clips` and switch between them with Play(name); until a
/// clip is played, the base fields below drive the animation exactly as before.
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
    // Force a specific atlas row (e.g. a facing direction) rather than auto-walking
    // rows; -1 = automatic. A top-down controller sets this from its 4-way facing.
    int rowOverride = -1;

    std::vector<SpriteClip> clips;   // named clips, switched with Play(name)

    void Start() override { Apply(); }

    void Update(float dt) override {
        int count = FrameCount();
        float rate = Fps();
        if (!playing || count <= 0 || rate <= 0.0f) return;
        m_time += dt;
        int frame = static_cast<int>(m_time * rate);
        if (Loop()) {
            frame %= count;
            // Keep the clock inside one cycle so it never grows without bound.
            float len = count / rate;
            if (m_time >= len) m_time = std::fmod(m_time, len);
        } else if (frame >= count) {
            frame = count - 1;
            playing = false;
        }
        if (frame != m_frame) { m_frame = frame; Apply(); }
    }

    /// Switch to the named clip (restarting it unless it's already playing).
    /// Returns false when there's no clip with that name.
    bool Play(const std::string& name) {
        int idx = FindClip(name);
        if (idx < 0) return false;
        if (idx == m_clip && playing) return true;
        m_clip = idx;
        Restart();
        return true;
    }
    bool HasClip(const std::string& name) const { return FindClip(name) >= 0; }
    /// Name of the clip playing now ("" = the base frames / atlas).
    const std::string& CurrentClip() const {
        static const std::string none;
        const SpriteClip* c = Clip();
        return c ? c->name : none;
    }

    int CurrentFrame() const { return m_frame; }
    int FrameCount() const {
        if (const SpriteClip* c = Clip()) {
            if (!c->frames.empty()) return static_cast<int>(c->frames.size());
            if (atlasColumns <= 0) return 0;
            return atlasCount > 0 && atlasCount < atlasColumns ? atlasCount : atlasColumns;
        }
        if (atlasColumns > 0) {
            int total = atlasColumns * (atlasRows > 0 ? atlasRows : 1);
            return atlasCount > 0 ? (atlasCount < total ? atlasCount : total) : total;
        }
        return static_cast<int>(frames.size());
    }
    void Restart() { m_time = 0.0f; m_frame = 0; playing = true; Apply(); }
    /// Hold the first frame (e.g. a walk cycle while standing still).
    void Stop() { m_time = 0.0f; m_frame = 0; playing = false; Apply(); }
    /// While set, the current clip plays once and holds its last frame (a jump
    /// that lasts longer than the clip). Cleared by the next clip that should cycle.
    void SetHoldAtEnd(bool hold) { m_hold = hold; }

private:
    const SpriteClip* Clip() const {
        return (m_clip >= 0 && m_clip < (int)clips.size()) ? &clips[m_clip] : nullptr;
    }
    int FindClip(const std::string& name) const {
        for (std::size_t i = 0; i < clips.size(); ++i)
            if (clips[i].name == name) return (int)i;
        return -1;
    }
    float Fps() const  { const SpriteClip* c = Clip(); return c ? c->fps : fps; }
    bool Loop() const { if (m_hold) return false; const SpriteClip* c = Clip(); return c ? c->loop : loop; }

    void Apply() {
        if (!gameObject) return;
        auto* sr = gameObject->GetComponent<SpriteRenderer>();
        if (!sr) return;
        const SpriteClip* c = Clip();
        if (c && !c->frames.empty()) {
            int i = m_frame >= 0 && m_frame < (int)c->frames.size() ? m_frame : 0;
            sr->texture = c->frames[i];
        } else if (atlasColumns > 0) {
            int rows = atlasRows > 0 ? atlasRows : 1;
            int col = m_frame % atlasColumns;
            int row = c ? (((c->row % rows) + rows) % rows)
                    : (rowOverride >= 0) ? (rowOverride % rows) : ((m_frame / atlasColumns) % rows);
            float uw = 1.0f / atlasColumns, vh = 1.0f / rows;
            sr->uvMin = {col * uw, row * vh};
            sr->uvMax = {(col + 1) * uw, (row + 1) * vh};
        } else if (!c && !frames.empty()) {
            int i = m_frame >= 0 && m_frame < (int)frames.size() ? m_frame : 0;
            sr->texture = frames[i];
        }
    }

    float m_time = 0.0f;
    int   m_frame = 0;
    int   m_clip = -1;   // index into clips; -1 = the base frames / atlas
    bool  m_hold = false;
};

} // namespace okay
