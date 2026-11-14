#pragma once
// A pure gesture recogniser. It does not read the window or the input devices:
// the caller feeds a stream of finger samples (tests, or Gestures::Tick from the
// real touch list). Screen positions use the same pixels as Input::MousePosition,
// with y growing downward.
#include "okay/Math/Vec2.hpp"
#include <string>
#include <vector>

namespace okay {

/// One sample in a touch stream. `phase` is 0 down, 1 moved, 2 up.
struct TouchSample {
    long long id = 0;
    int phase = 0;
    float x = 0.0f;
    float y = 0.0f;
    double time = 0.0;   ///< seconds, from the same clock as the other samples
};

enum class GestureKind { Tap, DoubleTap, LongPress, Swipe, Pinch, Rotate };

/// One recognised gesture. `direction` is a unit vector with y up (a swipe toward
/// the top of the screen has a positive y). `directionName` is "up", "down",
/// "left" or "right" for a swipe, and empty otherwise. `amount` is the swipe
/// distance in pixels, the pinch scale (1 = fingers as far apart as when the
/// pinch began), or the twist so far in radians. `delta` is how much the pinch
/// scale or the twist changed since the previous report of that gesture.
struct GestureEvent {
    GestureKind kind = GestureKind::Tap;
    Vec2 position;
    Vec2 direction;
    std::string directionName;
    float amount = 0.0f;
    float delta = 0.0f;
};

/// Distances are pixels. Times are seconds. A value that would make a gesture
/// impossible (a negative time, a zero swipe) is clamped when it is used.
struct GestureSettings {
    float moveSlop = 24.0f;            ///< a finger that stays inside this is still a tap or a long press
    float tapMaxSeconds = 0.30f;
    float doubleTapSeconds = 0.35f;    ///< the longest gap between the two lifts of a double tap
    float doubleTapSlop = 48.0f;       ///< how far apart those two lifts may be
    float longPressSeconds = 0.50f;
    float swipeMinDistance = 64.0f;
    float swipeMinSpeed = 350.0f;      ///< pixels per second
    float pinchStep = 0.06f;           ///< scale change that reports another pinch (1.06 is 6% wider)
    float rotateStep = 0.09f;          ///< radians of twist that report another rotate (about 5 degrees)
};

/// Tap, double tap, long press, swipe (with a direction), pinch and two-finger
/// rotate. Feed samples in time order. Call Advance with a later time so a tap
/// that is waiting to see if it is a double tap, and a finger that is held still,
/// can finish without another sample.
class GestureRecognizer {
public:
    void Reset();
    void SetSettings(const GestureSettings& s) { m_settings = s; }
    const GestureSettings& Settings() const { return m_settings; }

    /// Append every gesture this sample produces.
    void Feed(const TouchSample& sample, std::vector<GestureEvent>& out);
    /// Finish gestures that depend on time passing (a held long press, a tap whose
    /// double-tap window has closed). `time` is the same clock as the samples.
    void Advance(double time, std::vector<GestureEvent>& out);

    bool Tracking(long long id) const;
    /// Drop a finger without reporting a gesture (a control took it).
    void Cancel(long long id);
    /// Drop every finger whose id is not in `live` (a touch the list lost without an up).
    void DropExcept(const std::vector<long long>& live);

    /// Pinch scale of the fingers that are down (1 when they have not pinched).
    /// Stays at the last scale after they lift, until the next two-finger gesture.
    float PinchScale() const { return m_pinchScale; }
    /// Twist of the current or last two-finger gesture, in radians. Positive is
    /// counter-clockwise when y points up.
    float TwistRadians() const { return m_twist; }
    bool Pinching() const { return m_pinching; }

private:
    struct Finger {
        long long id = 0;
        float x = 0, y = 0, sx = 0, sy = 0;
        double t0 = 0, last = 0;
        bool moved = false;
        bool multi = false;
        bool longFired = false;
    };

    Finger* Find(long long id);
    const Finger* Find(long long id) const;
    void OnDown(const TouchSample& s, std::vector<GestureEvent>& out);
    void OnMove(const TouchSample& s, std::vector<GestureEvent>& out);
    void OnUp(const TouchSample& s, std::vector<GestureEvent>& out);
    void BeginPair();
    void UpdatePair(std::vector<GestureEvent>& out);
    void FlushTap(double time, std::vector<GestureEvent>& out);
    void Emit(std::vector<GestureEvent>& out, GestureKind kind, Vec2 position, Vec2 direction,
              const char* directionName, float amount, float delta);

    GestureSettings m_settings;
    std::vector<Finger> m_fingers;
    bool m_pending = false;
    float m_pendingX = 0, m_pendingY = 0;
    double m_pendingT = 0;
    bool m_pinching = false;
    float m_pinchScale = 1.0f;
    float m_twist = 0.0f;
    float m_lastPinchEmit = 1.0f;
    float m_lastTwistEmit = 0.0f;
    float m_pairAngle = 0.0f;
    float m_pairDist = 0.0f;
    long long m_pairA = 0, m_pairB = 0;
};

} // namespace okay
