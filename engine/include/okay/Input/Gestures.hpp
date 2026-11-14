#pragma once
// What the latest touch stream did, for scripts and visual-script nodes.
// Gestures::Tick reads Input's finger list (the player fills that from SDL finger
// events) and runs it through GestureRecognizer. Fingers a touch control has
// claimed are not gestures, so dragging the on-screen stick is not also a swipe.
#include "okay/Math/Vec2.hpp"
#include <string>

namespace okay {

class TouchClaims {
public:
    static void Clear();
    static void Claim(long long fingerId);
    static bool Claimed(long long fingerId);
};

/// Names are not case-sensitive and ignore spaces, '_' and '-':
/// "tap", "double tap", "long press", "swipe", "swipe up", "swipe down",
/// "swipe left", "swipe right", "pinch", "rotate" (also "twist").
class Gestures {
public:
    /// `timeSeconds` is a steady clock (the player passes the SDL timer). Call
    /// once a frame after the touches for that frame have been fed and after
    /// touch controls have claimed the fingers they own.
    static void Tick(double timeSeconds);
    static void Reset();

    /// True when that gesture was reported this frame.
    static bool Happened(const std::string& name);
    /// "up", "down", "left" or "right" when a swipe happened this frame, else "".
    static std::string SwipeDirection();
    /// Unit swipe direction, y up. Zero when this frame had no swipe.
    static Vec2 SwipeVector();
    /// How far apart the two fingers are compared with where the pinch began
    /// (1 = unchanged). The last pinch stays readable after the fingers lift.
    static float PinchScale();
    /// How much the pinch scale changed this frame (positive = fingers moved apart).
    static float PinchDelta();
    /// Two-finger twist so far, in degrees. Positive is counter-clockwise (y up).
    static float TwistDegrees();
    /// How many degrees the twist changed this frame.
    static float TwistDeltaDegrees();
};

} // namespace okay
