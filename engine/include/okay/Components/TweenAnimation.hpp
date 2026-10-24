#pragma once
// ---------------------------------------------------------------------------
// TweenAnimation — a tween set up in the Inspector, no code (DOTween Pro's
// DOTweenAnimation). Pick what to animate (move, scale, rotate, fade, colour,
// UI position / size, punch, shake, jump), the end value, duration, delay, ease
// and loops; it plays when the scene starts, or when a script calls
// tween_play_id("<Id>"). Several can sit on one object (give them different Ids).
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Tween.hpp"
#include <memory>
#include <string>

namespace okay {

class TweenAnimation : public Behaviour {
public:
    enum class Type {
        Move,           ///< world position to End
        LocalMove,      ///< local position to End
        Scale,          ///< local scale to End
        Rotate,         ///< Euler angles to End (Relative: spin by them)
        Fade,           ///< colour alpha to End Alpha
        Color,          ///< colour to End Color
        UIMove,         ///< UI anchored position to End (x, y)
        UISize,         ///< UI size to End (x, y)
        PunchPosition,  ///< bump by End and settle back
        PunchScale,
        PunchRotation,
        ShakePosition,  ///< shake by up to End, fading out
        ShakeRotation,
        ShakeScale,
        Jump,           ///< hop to End, End Alpha high, Jumps times
        Count
    };
    static const char* TypeName(Type t) {
        static const char* k[] = {"Move", "Local Move", "Scale", "Rotate", "Fade", "Color", "UI Move", "UI Size",
                                  "Punch Position", "Punch Scale", "Punch Rotation", "Shake Position",
                                  "Shake Rotation", "Shake Scale", "Jump"};
        const int i = (int)t;
        return i >= 0 && i < (int)Type::Count ? k[i] : "Move";
    }

    int   type = (int)Type::LocalMove;
    Vec3  endValue{0.0f, 1.0f, 0.0f};
    Color endColor = Color::White;
    float endFloat = 0.0f;            ///< Fade: alpha. Jump: height.
    bool  relative = false;           ///< End is an offset from where it starts
    bool  from = false;               ///< play from End back to the current value
    float duration = 1.0f;
    float delay = 0.0f;
    int   ease = (int)Ease::QuadOut;
    int   loops = 1;                  ///< -1 = forever
    int   loopType = (int)LoopType::Restart;
    bool  autoPlay = true;            ///< start when the scene starts
    bool  autoKill = true;            ///< off: keep it after it ends, so it can replay
    bool  unscaled = false;           ///< ignore the time scale (pause menus)
    int   vibrato = 10;               ///< punch / shake
    float elasticity = 1.0f;          ///< punch
    int   jumps = 1;
    std::string id;                   ///< for tween_play_id("...") etc.

    void Start() override { if (autoPlay) Play(); }

    /// Play it (creating the tween the first time, or after it was killed).
    void Play() {
        if (auto t = m_tween.lock(); t && t->IsActive()) { t->Play(); return; }
        m_tween = CreateTween();
    }
    void Restart() {
        if (auto t = m_tween.lock(); t && t->IsActive()) { t->Restart(); return; }
        m_tween = CreateTween();
    }
    void Pause()  { if (auto t = m_tween.lock()) t->Pause(); }
    void Kill()   { if (auto t = m_tween.lock()) t->Kill(); }
    bool IsPlaying() const { auto t = m_tween.lock(); return t && t->IsPlaying(); }
    std::shared_ptr<Tweener> Current() const { return m_tween.lock(); }

    /// Build the tween from the settings (on this object). Used by Play and by the
    /// editor's Preview.
    std::shared_ptr<Tweener> CreateTween() {
        GameObject* go = gameObject;
        if (!go) return nullptr;
        std::shared_ptr<Tweener> t;
        const Vec3& e = endValue;
        switch ((Type)type) {
            case Type::Move:          t = DOTween::Move(go, e, duration); break;
            case Type::LocalMove:     t = DOTween::LocalMove(go, e, duration); break;
            case Type::Scale:         t = DOTween::Scale(go, e, duration); break;
            case Type::Rotate:        t = DOTween::Rotate(go, e, duration); break;
            case Type::Fade:          t = DOTween::Fade(go, endFloat, duration); break;
            case Type::Color:         t = DOTween::ColorTo(go, endColor, duration); break;
            case Type::UIMove:        t = DOTween::AnchorPos(go, {e.x, e.y}, duration); break;
            case Type::UISize:        t = DOTween::SizeDelta(go, {e.x, e.y}, duration); break;
            case Type::PunchPosition: t = DOTween::PunchPosition(go, e, duration, vibrato, elasticity); break;
            case Type::PunchScale:    t = DOTween::PunchScale(go, e, duration, vibrato, elasticity); break;
            case Type::PunchRotation: t = DOTween::PunchRotation(go, e, duration, vibrato, elasticity); break;
            case Type::ShakePosition: t = DOTween::ShakePosition(go, duration, e, vibrato); break;
            case Type::ShakeRotation: t = DOTween::ShakeRotation(go, duration, e, vibrato); break;
            case Type::ShakeScale:    t = DOTween::ShakeScale(go, duration, e, vibrato); break;
            case Type::Jump:          t = DOTween::Jump(go, e, endFloat, jumps, duration); break;
            default:                  t = DOTween::LocalMove(go, e, duration); break;
        }
        if (!t) return nullptr;
        const bool punchy = type >= (int)Type::PunchPosition && type <= (int)Type::ShakeScale;
        if (!punchy) t->SetEase((Ease)ease);
        t->SetDelay(delay).SetLoops(loops, (LoopType)loopType).SetAutoKill(autoKill).SetUpdate(unscaled);
        if (relative) t->SetRelative();
        if (from) t->From();
        if (!id.empty()) t->SetId(id);
        return t;
    }

    /// Play (or restart) every TweenAnimation in `scene` with this id that has no
    /// running tween yet. Returns how many started.
    static int PlayAll(Scene& scene, const std::string& tweenId, bool restart) {
        int n = 0;
        for (const auto& up : scene.Objects()) {
            if (!up) continue;
            for (TweenAnimation* ta : up->GetComponents<TweenAnimation>()) {
                if (!ta || ta->id != tweenId) continue;
                auto t = ta->m_tween.lock();
                if (t && t->IsActive()) continue;   // the manager already handled it
                restart ? ta->Restart() : ta->Play();
                ++n;
            }
        }
        return n;
    }

private:
    std::weak_ptr<Tweener> m_tween;
};

} // namespace okay
