#include "okay/Input/GestureRecognizer.hpp"
#include <algorithm>
#include <cmath>

namespace okay {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float Dist(float ax, float ay, float bx, float by) {
    const float dx = ax - bx, dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}
float WrapPi(float d) {
    const float two = kPi * 2.0f;
    while (d > kPi) d -= two;
    while (d < -kPi) d += two;
    return d;
}
/// Angle of b relative to a, in radians, with y up. Positive is counter-clockwise.
float AngleUp(float ax, float ay, float bx, float by) {
    return std::atan2(-(by - ay), bx - ax);
}
const char* Cardinal(float dx, float dyDown) {
    if (std::fabs(dx) >= std::fabs(dyDown)) return dx < 0.0f ? "left" : "right";
    return dyDown < 0.0f ? "up" : "down";
}
float ClampPositive(float v, float fallback) { return v > 0.0f ? v : fallback; }

} // namespace

void GestureRecognizer::Reset() {
    m_fingers.clear();
    m_pending = false;
    m_pinching = false;
    m_pinchScale = 1.0f;
    m_twist = 0.0f;
    m_lastPinchEmit = 1.0f;
    m_lastTwistEmit = 0.0f;
    m_pairAngle = 0.0f;
    m_pairDist = 0.0f;
    m_pairA = m_pairB = 0;
}

GestureRecognizer::Finger* GestureRecognizer::Find(long long id) {
    for (Finger& f : m_fingers) if (f.id == id) return &f;
    return nullptr;
}
const GestureRecognizer::Finger* GestureRecognizer::Find(long long id) const {
    for (const Finger& f : m_fingers) if (f.id == id) return &f;
    return nullptr;
}
bool GestureRecognizer::Tracking(long long id) const { return Find(id) != nullptr; }

void GestureRecognizer::Cancel(long long id) {
    m_fingers.erase(std::remove_if(m_fingers.begin(), m_fingers.end(),
                                   [&](const Finger& f) { return f.id == id; }),
                    m_fingers.end());
    if (m_fingers.size() < 2) m_pinching = false;
}

void GestureRecognizer::DropExcept(const std::vector<long long>& live) {
    m_fingers.erase(std::remove_if(m_fingers.begin(), m_fingers.end(),
                                   [&](const Finger& f) {
                                       return std::find(live.begin(), live.end(), f.id) == live.end();
                                   }),
                    m_fingers.end());
    if (m_fingers.size() < 2) m_pinching = false;
}

void GestureRecognizer::Emit(std::vector<GestureEvent>& out, GestureKind kind, Vec2 position, Vec2 direction,
                             const char* directionName, float amount, float delta) {
    GestureEvent e;
    e.kind = kind;
    e.position = position;
    e.direction = direction;
    e.directionName = directionName ? directionName : "";
    e.amount = amount;
    e.delta = delta;
    out.push_back(std::move(e));
}

void GestureRecognizer::BeginPair() {
    if (m_fingers.size() < 2) { m_pinching = false; return; }
    for (Finger& f : m_fingers) f.multi = true;
    const Finger& a = m_fingers[0];
    const Finger& b = m_fingers[1];
    m_pairA = a.id;
    m_pairB = b.id;
    m_pairDist = Dist(a.x, a.y, b.x, b.y);
    if (m_pairDist < 1.0f) m_pairDist = 1.0f;
    m_pairAngle = AngleUp(a.x, a.y, b.x, b.y);
    m_pinchScale = 1.0f;
    m_twist = 0.0f;
    m_lastPinchEmit = 1.0f;
    m_lastTwistEmit = 0.0f;
    m_pinching = true;
    m_pending = false;   // a second finger is a pinch, not the second half of a double tap
}

void GestureRecognizer::UpdatePair(std::vector<GestureEvent>& out) {
    if (!m_pinching) return;
    const Finger* a = Find(m_pairA);
    const Finger* b = Find(m_pairB);
    if (!a || !b) return;
    const float dist = Dist(a->x, a->y, b->x, b->y);
    const float scale = dist / m_pairDist;
    const float ang = AngleUp(a->x, a->y, b->x, b->y);
    m_twist += WrapPi(ang - m_pairAngle);
    m_pairAngle = ang;
    m_pinchScale = scale;
    const Vec2 mid{(a->x + b->x) * 0.5f, (a->y + b->y) * 0.5f};
    const float pinchStep = ClampPositive(m_settings.pinchStep, 0.06f);
    const float rotateStep = ClampPositive(m_settings.rotateStep, 0.09f);
    if (std::fabs(scale - m_lastPinchEmit) >= pinchStep) {
        const float delta = scale - m_lastPinchEmit;
        m_lastPinchEmit = scale;
        Emit(out, GestureKind::Pinch, mid, {}, "", scale, delta);
    }
    if (std::fabs(m_twist - m_lastTwistEmit) >= rotateStep) {
        const float delta = m_twist - m_lastTwistEmit;
        m_lastTwistEmit = m_twist;
        Emit(out, GestureKind::Rotate, mid, {}, "", m_twist, delta);
    }
}

void GestureRecognizer::FlushTap(double time, std::vector<GestureEvent>& out) {
    if (!m_pending) return;
    const float gap = ClampPositive(m_settings.doubleTapSeconds, 0.35f);
    if (time - m_pendingT < gap) return;
    Emit(out, GestureKind::Tap, {m_pendingX, m_pendingY}, {}, "", 0.0f, 0.0f);
    m_pending = false;
}

void GestureRecognizer::OnDown(const TouchSample& s, std::vector<GestureEvent>&) {
    if (Find(s.id)) return;
    if (m_fingers.size() >= 8) return;
    Finger f;
    f.id = s.id;
    f.x = f.sx = s.x;
    f.y = f.sy = s.y;
    f.t0 = f.last = s.time;
    m_fingers.push_back(f);
    if (m_fingers.size() >= 2) BeginPair();
}

void GestureRecognizer::OnMove(const TouchSample& s, std::vector<GestureEvent>& out) {
    Finger* f = Find(s.id);
    if (!f) {
        TouchSample down = s;
        down.phase = 0;
        OnDown(down, out);
        f = Find(s.id);
        if (!f) return;
    }
    f->x = s.x;
    f->y = s.y;
    f->last = s.time;
    const float slop = ClampPositive(m_settings.moveSlop, 24.0f);
    if (Dist(f->x, f->y, f->sx, f->sy) > slop) f->moved = true;
    UpdatePair(out);
}

void GestureRecognizer::OnUp(const TouchSample& s, std::vector<GestureEvent>& out) {
    Finger* found = Find(s.id);
    if (!found) return;
    Finger f = *found;
    f.x = s.x;
    f.y = s.y;
    f.last = s.time;
    const bool wasMulti = f.multi || m_fingers.size() > 1;
    m_fingers.erase(std::remove_if(m_fingers.begin(), m_fingers.end(),
                                   [&](const Finger& e) { return e.id == s.id; }),
                    m_fingers.end());
    if (m_fingers.size() >= 2) BeginPair();
    else m_pinching = false;

    const float dist = Dist(f.x, f.y, f.sx, f.sy);
    const double dur = std::max(0.001, s.time - f.t0);
    const float slop = ClampPositive(m_settings.moveSlop, 24.0f);
    const float tapMax = ClampPositive(m_settings.tapMaxSeconds, 0.30f);
    const float longPress = ClampPositive(m_settings.longPressSeconds, 0.50f);
    const float swipeDist = ClampPositive(m_settings.swipeMinDistance, 64.0f);
    const float swipeSpeed = ClampPositive(m_settings.swipeMinSpeed, 350.0f);
    const bool moved = f.moved || dist > slop;

    if (!wasMulti && !f.longFired && !moved && dur >= longPress) {
        Emit(out, GestureKind::LongPress, {f.x, f.y}, {}, "", (float)dur, 0.0f);
        m_pending = false;
    } else if (!wasMulti && !f.longFired && !moved && dur <= tapMax) {
        const float gap = ClampPositive(m_settings.doubleTapSeconds, 0.35f);
        const float reach = ClampPositive(m_settings.doubleTapSlop, 48.0f);
        if (m_pending && (s.time - m_pendingT) <= gap && Dist(f.x, f.y, m_pendingX, m_pendingY) <= reach) {
            Emit(out, GestureKind::DoubleTap, {f.x, f.y}, {}, "", 0.0f, 0.0f);
            m_pending = false;
        } else {
            m_pending = true;
            m_pendingX = f.x;
            m_pendingY = f.y;
            m_pendingT = s.time;
        }
    } else if (!wasMulti && dist >= swipeDist && (float)(dist / dur) >= swipeSpeed) {
        const float dx = f.x - f.sx, dy = f.y - f.sy;
        const float len = std::sqrt(dx * dx + dy * dy);
        Vec2 dir = len > 0.0f ? Vec2{dx / len, -dy / len} : Vec2{};
        Emit(out, GestureKind::Swipe, {f.x, f.y}, dir, Cardinal(dx, dy), dist, dist);
        m_pending = false;
    }
}

void GestureRecognizer::Feed(const TouchSample& sample, std::vector<GestureEvent>& out) {
    FlushTap(sample.time, out);
    if (sample.phase == 0) OnDown(sample, out);
    else if (sample.phase == 2) OnUp(sample, out);
    else OnMove(sample, out);
    // A finger that has been held still long enough, reported on the sample that
    // crosses the time (a stream with no extra "still held" sample).
    if (sample.phase != 2) {
        if (Finger* f = Find(sample.id)) {
            const float longPress = ClampPositive(m_settings.longPressSeconds, 0.50f);
            if (!f->longFired && !f->moved && !f->multi && m_fingers.size() == 1 &&
                sample.time - f->t0 >= longPress) {
                f->longFired = true;
                Emit(out, GestureKind::LongPress, {f->x, f->y}, {}, "", (float)(sample.time - f->t0), 0.0f);
                m_pending = false;
            }
        }
    }
}

void GestureRecognizer::Advance(double time, std::vector<GestureEvent>& out) {
    FlushTap(time, out);
    if (m_fingers.size() != 1) return;
    Finger& f = m_fingers[0];
    const float longPress = ClampPositive(m_settings.longPressSeconds, 0.50f);
    if (!f.longFired && !f.moved && !f.multi && time - f.t0 >= longPress) {
        f.longFired = true;
        Emit(out, GestureKind::LongPress, {f.x, f.y}, {}, "", (float)(time - f.t0), 0.0f);
        m_pending = false;
    }
}

} // namespace okay
