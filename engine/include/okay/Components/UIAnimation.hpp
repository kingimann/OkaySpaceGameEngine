#pragma once
// ---------------------------------------------------------------------------
// UIAnimation — make any UI widget move: an intro when it appears (fade, slide
// in from a side, pop, drop), an idle loop while shown (pulse, bob, sway,
// blink, glow), and an outro to hide it. Works on every widget type through
// its rect (position + size) and a fade that also covers its children.
//
// Drop it on a Button / Panel / Image / Text...; it plays the intro on start
// and whenever the object is shown again. Scripts and graphs call Play(),
// PlayOut() (optionally hiding the object afterwards) or Stop().
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Easing.hpp"
#include <cstdint>

namespace okay {

class UIAnimation : public Behaviour {
public:
    enum class Intro { None, FadeIn, SlideLeft, SlideRight, SlideTop, SlideBottom, Pop, Drop, Grow };
    enum class Loop  { None, Pulse, Bob, Sway, Blink, Breathe };
    enum class Outro { FadeOut, SlideLeft, SlideRight, SlideTop, SlideBottom, Shrink };

    Intro intro = Intro::FadeIn;
    Loop  loop  = Loop::None;
    Outro outro = Outro::FadeOut;
    float duration = 0.35f;   ///< intro / outro length (s)
    float delay    = 0.0f;    ///< wait before the intro (stagger a list of buttons)
    float distance = 160.0f;  ///< slide distance (design px)
    Ease  ease     = Ease::CubicOut;
    float loopSpeed  = 1.0f;  ///< loop cycles per second
    float loopAmount = 0.06f; ///< pulse/breathe: scale amount; bob/sway: px / 100
    bool  playOnStart   = true;   ///< run the intro when the scene starts
    bool  replayOnShow  = true;   ///< and again whenever the object is shown after being hidden
    bool  hideAfterOutro = true;  ///< PlayOut() deactivates the object when it finishes

    /// Start (or restart) the intro.
    void Play();
    /// Animate out; hides the object afterwards when hideAfterOutro is set.
    void PlayOut();
    /// Snap back to the authored layout and stop every effect.
    void Stop();
    bool Playing() const { return m_phase == Phase::Intro || m_phase == Phase::Outro; }

    /// The fade this animation applies right now (1 = opaque). UIOpacity()
    /// multiplies it in for this widget and every widget under it.
    float Alpha() const { return m_alpha; }

    void Start() override;
    void Update(float dt) override;
    void OnDestroy() override { Restore(); }

private:
    enum class Phase { Idle, Waiting, Intro, Loop, Outro };
    void Capture();          // remember the authored position/size
    void Restore();          // put them back
    void Apply(float dx, float dy, float scale, float alpha);   // offset + scale about the centre
    Phase m_phase = Phase::Idle;
    float m_t = 0.0f, m_loopT = 0.0f, m_alpha = 1.0f;
    bool  m_captured = false;
    Vec2  m_basePos{0, 0}, m_baseSize{0, 0};
    std::uint64_t m_lastFrame = 0;
};

} // namespace okay
