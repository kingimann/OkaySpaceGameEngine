#include "okay/Components/UIAnimation.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>

namespace okay {

void UIAnimation::Capture() {
    UIRect r = GetUIRect(gameObject);
    if (!r.valid || !r.position) return;
    m_basePos = *r.position;
    m_baseSize = r.sizePtr ? *r.sizePtr : r.size;
    m_captured = true;
}

void UIAnimation::Restore() {
    if (!m_captured) return;
    UIRect r = GetUIRect(gameObject);
    if (r.valid && r.position) {
        *r.position = m_basePos;
        if (r.sizePtr) *r.sizePtr = m_baseSize;
    }
    m_alpha = 1.0f;
}

// Offset the widget by (dx, dy) design px, scale it about its centre and fade it.
// The anchor decides which way a position change moves the widget on screen, so
// each axis's direction is measured rather than assumed (right/bottom anchors
// count their offset the other way).
void UIAnimation::Apply(float dx, float dy, float scale, float alpha) {
    m_alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    UIRect r = GetUIRect(gameObject);
    if (!r.valid || !r.position || !m_captured) return;
    const float W = UICanvas::Width(), H = UICanvas::Height();
    Vec2 size = m_baseSize;
    if (r.sizePtr && !r.Stretches()) { size = m_baseSize * scale; *r.sizePtr = size; }
    Vec2 o0, s0, oX, sX, oY, sY, o1, s1;
    r.ResolveWith(m_basePos, m_baseSize, W, H, o0, s0);
    r.ResolveWith(m_basePos + Vec2{1.0f, 0.0f}, m_baseSize, W, H, oX, sX);
    r.ResolveWith(m_basePos + Vec2{0.0f, 1.0f}, m_baseSize, W, H, oY, sY);
    r.ResolveWith(m_basePos, size, W, H, o1, s1);
    const float sx = (oX.x - o0.x) < 0.0f ? -1.0f : 1.0f, sy = (oY.y - o0.y) < 0.0f ? -1.0f : 1.0f;
    // Where the (scaled) rect must sit so its centre stays put, plus the offset.
    const float wantX = (o0.x + s0.x * 0.5f) - (o1.x + s1.x * 0.5f) + dx;
    const float wantY = (o0.y + s0.y * 0.5f) - (o1.y + s1.y * 0.5f) + dy;
    *r.position = Vec2{m_basePos.x + wantX * sx, m_basePos.y + wantY * sy};
}

void UIAnimation::Play() {
    m_lastFrame = Time::FrameCount();   // (Update's "shown again" check must not re-trigger this)
    if (!m_captured) Capture(); else Restore();
    m_t = 0.0f; m_loopT = 0.0f;
    m_phase = delay > 0.0f ? Phase::Waiting : (intro == Intro::None ? Phase::Loop : Phase::Intro);
    if (m_phase == Phase::Waiting || m_phase == Phase::Intro) Update(0.0f);   // first frame already in its start pose
}

void UIAnimation::PlayOut() {
    if (!m_captured) Capture();
    m_t = 0.0f;
    m_phase = Phase::Outro;
}

void UIAnimation::Stop() {
    Restore();
    m_phase = Phase::Idle;
}

void UIAnimation::Start() {
    Capture();
    m_lastFrame = Time::FrameCount();
    if (playOnStart) Play();
    else m_phase = Phase::Loop;
}

void UIAnimation::Update(float dt) {
    // Shown again after being hidden (no Update ran in between): replay the intro.
    const std::uint64_t f = Time::FrameCount();
    if (replayOnShow && m_lastFrame != 0 && f > m_lastFrame + 1 && m_phase != Phase::Outro) Play();
    m_lastFrame = f;
    if (!m_captured) Capture();
    if (!m_captured) return;

    const float dur = duration > 0.001f ? duration : 0.001f;
    switch (m_phase) {
        case Phase::Idle: return;
        case Phase::Waiting: {
            m_t += dt;
            // Hold the intro's first frame while waiting (so nothing pops in early).
            if (m_t < delay) { float save = m_t; m_t = 0.0f; m_phase = Phase::Intro; Update(0.0f); m_phase = Phase::Waiting; m_t = save; return; }
            m_t = 0.0f; m_phase = intro == Intro::None ? Phase::Loop : Phase::Intro;
            return;
        }
        case Phase::Intro: {
            m_t += dt;
            float k = m_t / dur; if (k > 1.0f) k = 1.0f;
            float e = Easing::Evaluate(ease, k), inv = 1.0f - e;
            switch (intro) {
                case Intro::FadeIn:      Apply(0, 0, 1.0f, e); break;
                case Intro::SlideLeft:   Apply(-distance * inv, 0, 1.0f, e); break;
                case Intro::SlideRight:  Apply( distance * inv, 0, 1.0f, e); break;
                case Intro::SlideTop:    Apply(0, -distance * inv, 1.0f, e); break;
                case Intro::SlideBottom: Apply(0,  distance * inv, 1.0f, e); break;
                case Intro::Pop:         Apply(0, 0, 0.6f + 0.4f * Easing::Evaluate(Ease::BackOut, k), std::fmin(1.0f, k * 3.0f)); break;
                case Intro::Drop:        Apply(0, -distance * (1.0f - Easing::Evaluate(Ease::BounceOut, k)), 1.0f, std::fmin(1.0f, k * 4.0f)); break;
                case Intro::Grow:        Apply(0, 0, e < 0.02f ? 0.02f : e, 1.0f); break;
                case Intro::None:        break;
            }
            if (k >= 1.0f) { m_phase = Phase::Loop; m_loopT = 0.0f; Apply(0, 0, 1.0f, 1.0f); }
            return;
        }
        case Phase::Loop: {
            if (loop == Loop::None) return;
            m_loopT += dt * (loopSpeed > 0.0f ? loopSpeed : 1.0f);
            const float s = std::sin(m_loopT * 6.2831853f);
            switch (loop) {
                case Loop::Pulse:   Apply(0, 0, 1.0f + loopAmount * (0.5f + 0.5f * s), 1.0f); break;
                case Loop::Breathe: Apply(0, 0, 1.0f + loopAmount * 0.5f * s, 0.85f + 0.15f * (0.5f + 0.5f * s)); break;
                case Loop::Bob:     Apply(0, s * loopAmount * 100.0f, 1.0f, 1.0f); break;
                case Loop::Sway:    Apply(s * loopAmount * 100.0f, 0, 1.0f, 1.0f); break;
                case Loop::Blink:   Apply(0, 0, 1.0f, s > 0.0f ? 1.0f : 0.25f); break;
                case Loop::None:    break;
            }
            return;
        }
        case Phase::Outro: {
            m_t += dt;
            float k = m_t / dur; if (k > 1.0f) k = 1.0f;
            float e = Easing::Evaluate(Ease::CubicIn, k), inv = 1.0f - e;
            switch (outro) {
                case Outro::FadeOut:     Apply(0, 0, 1.0f, inv); break;
                case Outro::SlideLeft:   Apply(-distance * e, 0, 1.0f, inv); break;
                case Outro::SlideRight:  Apply( distance * e, 0, 1.0f, inv); break;
                case Outro::SlideTop:    Apply(0, -distance * e, 1.0f, inv); break;
                case Outro::SlideBottom: Apply(0,  distance * e, 1.0f, inv); break;
                case Outro::Shrink:      Apply(0, 0, inv < 0.02f ? 0.02f : inv, inv); break;
            }
            if (k >= 1.0f) {
                Restore();
                m_phase = Phase::Idle;
                if (hideAfterOutro && gameObject) gameObject->active = false;
                else m_alpha = 0.0f;
            }
            return;
        }
    }
}

} // namespace okay
