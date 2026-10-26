#pragma once
#include "okay/Input/Input.hpp"
#include "okay/Math/Vec2.hpp"
#include <cmath>

namespace okay {

/// Finger drag-scrolling for Scroll Views and List Views: a finger that lands in
/// the view and moves past Input::kTouchDragPixels drags the content with it, and
/// a flick keeps it gliding for a moment after the lift. Step() returns the change
/// to add to the scroll offset this frame (content follows the finger, so moving
/// the finger up scrolls down).
struct UITouchScroll {
    long long id = 0;
    bool  tracking = false;   // a finger that landed here is down
    bool  dragging = false;   // ...and has moved far enough to be a drag
    Vec2  velocity{0.0f, 0.0f};
    float friction = 4.0f;    // fling slow-down rate (1/s)

    /// `starts(p)`: a finger landing at screen point p begins a drag here (inside the
    /// view, not on a scrollbar). `toLocal(p)`: map a screen point into the view's
    /// own frame, so drags inside a scaled canvas move the content exactly with the finger.
    template <class Starts, class ToLocal>
    Vec2 Step(float dt, Starts starts, ToLocal toLocal) {
        if (!tracking)
            for (int i = 0; i < Input::TouchCount(); ++i) {
                const Input::Touch& t = Input::GetTouch(i);
                if (t.phase == Input::Touch::Phase::Began && starts(t.position)) {
                    id = t.id; tracking = true; dragging = false; velocity = {0.0f, 0.0f};
                    break;
                }
            }
        if (tracking) {
            const Input::Touch* t = nullptr;
            for (int i = 0; i < Input::TouchCount(); ++i)
                if (Input::GetTouch(i).id == id) t = &Input::GetTouch(i);
            if (!t) { tracking = false; return {0.0f, 0.0f}; }
            Vec2 d{0.0f, 0.0f};
            if (dragging) {
                d = -(toLocal(t->position) - toLocal(t->position - t->delta));
            } else if ((t->position - t->start).SqrMagnitude() > Input::kTouchDragPixels * Input::kTouchDragPixels) {
                dragging = true;   // catch up the distance moved before it counted as a drag
                d = -(toLocal(t->position) - toLocal(t->start));
            }
            if (dragging && dt > 0.0f) velocity = velocity * 0.5f + (d / dt) * 0.5f;
            if (t->phase == Input::Touch::Phase::Ended) {
                tracking = false;
                if (!dragging) velocity = {0.0f, 0.0f};
            }
            return d;
        }
        if (velocity.SqrMagnitude() < 25.0f) { velocity = {0.0f, 0.0f}; return velocity; }
        const Vec2 d = velocity * dt;
        velocity = velocity * std::exp(-friction * dt);
        return d;
    }

    /// Stop a fling (the content hit its end).
    void Stop() { velocity = {0.0f, 0.0f}; }
};

} // namespace okay
