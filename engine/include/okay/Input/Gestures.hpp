#pragma once
// What the latest touch stream did, for scripts and visual-script nodes.
// Gestures::Tick reads Input's finger list (the player fills that from SDL finger
// events) and runs it through GestureRecognizer. Fingers a touch control has
// claimed are not gestures, so dragging the on-screen stick is not also a swipe.
#include "okay/Math/Vec2.hpp"
#include <string>

namespace okay {

class ActionSet;

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
    /// The same by index (no text work: what a "gesture:" binding of an action reads every frame).
    static bool Happened(int index);
    /// A gesture name's index (any spelling the names above allow), or -1 for a name that is not a gesture.
    static int NameIndex(const std::string& name);
    /// The saved spelling ("swipeup") and the shown one ("Swipe Up") of an index; "" and "None" when out of range.
    static const char* NameKey(int index);
    static const char* NameLabel(int index);
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

    /// When `gesture` happens ("tap", "swipe up", "pinch", ...) that action is pressed for the frame. One gesture can
    /// drive several actions (each Bind adds one; the same pair twice is one binding). False for an unknown gesture
    /// name or an empty action. Not saved: for a binding saved with the input asset write "gesture:swipeup" as one
    /// of the action's bindings instead.
    static bool Bind(const std::string& gesture, const std::string& action);
    /// Remove one binding, or with action "" every action of that gesture.
    static void Unbind(const std::string& gesture, const std::string& action = "");
    static void ClearBinds();
    /// Push this frame's gestures into `set` (call after Tick, before the set's Update). Only for a set of your own:
    /// the engine's action table and the local players get them from ApplyBound.
    static void Apply(ActionSet& set);
    /// Push this frame's bound gestures into the engine's action table and every local player's set, once per Tick.
    /// InputActions::Tick calls it, so a host that ticks gestures and then actions (the player, the editor's Play)
    /// needs nothing else.
    static void ApplyBound();
};

} // namespace okay
