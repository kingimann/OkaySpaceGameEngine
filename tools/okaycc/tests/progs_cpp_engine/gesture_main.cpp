// Drives the engine's own engine/src/Input/GestureRecognizer.cpp, compiled unchanged by okay-cc
// (round 20: `if (Finger *f = Find(id))`, vector::erase of a range with std::remove_if, lambdas,
// std::string members of a struct, atan2 and hypot-style math on Vec2).
#include "okay/Input/GestureRecognizer.hpp"

extern "C" int printf(const char *fmt, ...);

static const char *kindName(okay::GestureKind k) {
    switch (k) {
    case okay::GestureKind::Tap: return "tap";
    case okay::GestureKind::DoubleTap: return "double";
    case okay::GestureKind::LongPress: return "long";
    case okay::GestureKind::Swipe: return "swipe";
    case okay::GestureKind::Pinch: return "pinch";
    case okay::GestureKind::Rotate: return "rotate";
    }
    return "?";
}

static void show(std::vector<okay::GestureEvent> &out) {
    for (unsigned long i = 0; i < out.size(); ++i) {
        const okay::GestureEvent &e = out[i];
        printf("%s at %.2f %.2f dir %.3f %.3f [%s] amount %.4f delta %.4f\n", kindName(e.kind), e.position.x,
               e.position.y, e.direction.x, e.direction.y, e.directionName.c_str(), e.amount, e.delta);
    }
    out.clear();
}

static okay::TouchSample touch(long long id, int phase, float x, float y, double t) {
    okay::TouchSample s;
    s.id = id;
    s.phase = phase;
    s.x = x;
    s.y = y;
    s.time = t;
    return s;
}

int main() {
    okay::GestureRecognizer g;
    std::vector<okay::GestureEvent> out;
    // A tap, then a second tap close by: a double tap.
    g.Feed(touch(1, 0, 100, 100, 0.00), out);
    g.Feed(touch(1, 2, 101, 100, 0.10), out);
    g.Advance(0.20, out);
    g.Feed(touch(2, 0, 104, 102, 0.25), out);
    g.Feed(touch(2, 2, 104, 102, 0.30), out);
    g.Advance(1.0, out);
    show(out);
    // A swipe to the right and one upward.
    g.Feed(touch(3, 0, 10, 300, 2.00), out);
    g.Feed(touch(3, 1, 120, 302, 2.05), out);
    g.Feed(touch(3, 2, 260, 305, 2.10), out);
    g.Advance(3.0, out);
    g.Feed(touch(4, 0, 50, 400, 4.00), out);
    g.Feed(touch(4, 2, 55, 200, 4.10), out);
    show(out);
    // A long press.
    g.Feed(touch(5, 0, 70, 70, 5.00), out);
    g.Feed(touch(5, 1, 71, 70, 5.60), out);
    printf("tracking %d %d\n", g.Tracking(5), g.Tracking(99));
    g.Feed(touch(5, 2, 71, 70, 5.70), out);
    show(out);
    // Two fingers: a pinch outward with a twist.
    g.Feed(touch(6, 0, 100, 100, 6.0), out);
    g.Feed(touch(7, 0, 200, 100, 6.0), out);
    for (int i = 1; i <= 6; ++i) {
        float s = 50.0f + i * 12.0f;
        float a = i * 0.05f;
        g.Feed(touch(6, 1, 150 - s, 100 - s * a, 6.0 + i * 0.02), out);
        g.Feed(touch(7, 1, 150 + s, 100 + s * a, 6.0 + i * 0.02), out);
    }
    printf("pinching %d scale %.4f twist %.4f\n", g.Pinching(), g.PinchScale(), g.TwistRadians());
    g.Cancel(6);
    std::vector<long long> live;
    g.DropExcept(live);
    printf("after drop %d %d\n", g.Tracking(6), g.Tracking(7));
    show(out);
    okay::GestureSettings st;
    st.longPressSeconds = -1.0f;
    st.swipeMinDistance = 0.0f;
    g.SetSettings(st);
    g.Reset();
    g.Feed(touch(8, 0, 0, 0, 10.0), out);
    g.Feed(touch(8, 2, 0, -90, 10.05), out);
    g.Advance(11.0, out);
    show(out);
    return 0;
}
