#pragma once
// Per-collider surface settings (friction and bounciness) and the rule for combining two of them.
//
// Written from the usual contact-model facts: a contact between two surfaces needs one friction number and one
// bounce number, so each surface offers its own and a rule picks the pair's value (average, smaller, larger,
// product). Nothing here is copied from another engine; the "Default" rule is this engine's own long-standing
// behaviour (friction: geometric mean of the two bodies, bounce: the larger).
#include <algorithm>
#include <cmath>

namespace okay {

/// How two colliders' friction (or bounciness) values become one number for their contact.
/// Default keeps the engine's original rule for that quantity (friction: geometric mean, bounce: larger). When the two
/// colliders ask for different explicit rules the stronger one wins, in this order: Average < Minimum < Multiply < Maximum.
enum class CombineMode : int { Default = 0, Average = 1, Minimum = 2, Multiply = 3, Maximum = 4 };

inline int CombineRank(CombineMode m) {
    switch (m) {
        case CombineMode::Average: return 1;
        case CombineMode::Minimum: return 2;
        case CombineMode::Multiply: return 3;
        case CombineMode::Maximum: return 4;
        default: return 0;
    }
}

/// Combine two values. `friction` picks the Default rule (true: geometric mean, false: the larger value).
inline float CombineSurface(float a, float b, CombineMode ma, CombineMode mb, bool friction) {
    a = a < 0.0f ? 0.0f : a; b = b < 0.0f ? 0.0f : b;
    CombineMode m = CombineRank(ma) >= CombineRank(mb) ? ma : mb;
    switch (m) {
        case CombineMode::Average:  return 0.5f * (a + b);
        case CombineMode::Minimum:  return std::min(a, b);
        case CombineMode::Multiply: return a * b;
        case CombineMode::Maximum:  return std::max(a, b);
        default: break;
    }
    return friction ? std::sqrt(a * b) : std::max(a, b);
}

/// The friction a collider offers: its own number when it has one (>= 0), else its body's, else `noBodyDefault`.
/// `Col` and `Body` are the 2D or 3D collider and rigidbody classes.
template <class Col, class Body>
float SurfaceFriction(const Col* c, const Body* b, float noBodyDefault) {
    if (c && c->friction >= 0.0f) return c->friction;
    return b ? b->friction : noBodyDefault;
}
/// The bounciness a collider offers: its own number when it has one (>= 0), else its body's, else 0.
template <class Col, class Body>
float SurfaceBounce(const Col* c, const Body* b) {
    if (c && c->bounciness >= 0.0f) return c->bounciness;
    return b ? b->bounciness : 0.0f;
}

} // namespace okay
