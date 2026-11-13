#include "okay/UI/UIFrameTransform.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIFrame.hpp"

#include <cmath>

namespace okay {
namespace {

struct Aff {
    float a = 1.0f, b = 0.0f, tx = 0.0f;
    float c = 0.0f, d = 1.0f, ty = 0.0f;
    Vec2 Apply(Vec2 p) const { return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty}; }
};

Aff Mul(const Aff& parent, const Aff& local) {
    return {parent.a * local.a + parent.b * local.c,
            parent.a * local.b + parent.b * local.d,
            parent.a * local.tx + parent.b * local.ty + parent.tx,
            parent.c * local.a + parent.d * local.c,
            parent.c * local.b + parent.d * local.d,
            parent.c * local.tx + parent.d * local.ty + parent.ty};
}

Aff Translate(float x, float y) { return {1.0f, 0.0f, x, 0.0f, 1.0f, y}; }

// Scale in the frame's own axes, then turn. Positive degrees are clockwise on
// screen, because Y grows downward: (1, 0) at +90° lands on (0, 1).
Aff RotateScaleAround(Vec2 pivot, float degrees, Vec2 scale) {
    constexpr float kDeg = 0.017453292519943295f;
    const float rad = degrees * kDeg;
    const float co = std::cos(rad), si = std::sin(rad);
    const float a = co * scale.x;
    const float b = -si * scale.y;
    const float c = si * scale.x;
    const float d = co * scale.y;
    return {a, b, pivot.x - (a * pivot.x + b * pivot.y),
            c, d, pivot.y - (c * pivot.x + d * pivot.y)};
}

bool Invert(const Aff& m, Aff& out) {
    const float det = m.a * m.d - m.b * m.c;
    if (!(std::fabs(det) > 1.0e-8f) || !std::isfinite(det)) return false;
    const float inv = 1.0f / det;
    const float ia = m.d * inv, ib = -m.b * inv, ic = -m.c * inv, id = m.a * inv;
    out.a = ia;
    out.b = ib;
    out.tx = -(ia * m.tx + ib * m.ty);
    out.c = ic;
    out.d = id;
    out.ty = -(ic * m.tx + id * m.ty);
    return std::isfinite(out.a) && std::isfinite(out.tx) && std::isfinite(out.ty);
}

void SpinOf(GameObject* g, float& deg, Vec2& scale, Vec2& pivot) {
    deg = 0.0f;
    scale = {1.0f, 1.0f};
    pivot = {0.0f, 0.0f};
    auto* f = g ? g->GetComponent<UIFrame>() : nullptr;
    if (!f) return;
    if (std::isfinite(f->rotation)) deg = f->rotation;
    if (std::isfinite(f->scale.x) && std::isfinite(f->scale.y)) scale = f->scale;
    if (std::isfinite(f->pivot.x) && std::isfinite(f->pivot.y)) pivot = f->pivot;
}

// Widget-local pixels (origin at the unscaled top-left) to screen pixels.
Aff LocalToScreen(GameObject* go, float sw, float sh, int depth) {
    if (!go || depth > 64) return {};
    Vec2 o, sz;
    if (!GetUIScreenRect(go, sw, sh, o, sz)) return {};
    float deg = 0.0f;
    Vec2 scale{1.0f, 1.0f}, pivot{0.0f, 0.0f};
    SpinOf(go, deg, scale, pivot);
    GameObject* parent = OwningUIParent(go);
    Vec2 parentO{0.0f, 0.0f};
    bool hasParent = false;
    if (parent) {
        Vec2 psz;
        if (GetUIScreenRect(parent, sw, sh, parentO, psz)) hasParent = true;
    }
    const Vec2 layoutOrigin = hasParent ? (o - parentO) : o;
    const Aff spin = RotateScaleAround(Vec2{sz.x * pivot.x, sz.y * pivot.y}, deg, scale);
    const Aff toParent = Mul(Translate(layoutOrigin.x, layoutOrigin.y), spin);
    if (!hasParent) return toParent;
    return Mul(LocalToScreen(parent, sw, sh, depth + 1), toParent);
}

} // namespace

bool UIFrameTransformed(GameObject* go) {
    for (int n = 0; go && n < 64; ++n, go = OwningUIParent(go)) {
        auto* f = go->GetComponent<UIFrame>();
        if (!f) continue;
        if (f->rotation != 0.0f || f->scale.x != 1.0f || f->scale.y != 1.0f) return true;
    }
    return false;
}

bool UIFrameLayoutToScreen(GameObject* go, float sw, float sh, UIFrameAffine& out) {
    out = {};
    if (!go) return false;
    Vec2 o, sz;
    if (!GetUIScreenRect(go, sw, sh, o, sz)) return false;
    if (!UIFrameTransformed(go)) return true;
    const Aff m = Mul(LocalToScreen(go, sw, sh, 0), Translate(-o.x, -o.y));
    out.a = m.a; out.b = m.b; out.tx = m.tx;
    out.c = m.c; out.d = m.d; out.ty = m.ty;
    return true;
}

bool UIFramePointToLocal(GameObject* go, const Vec2& screen, float sw, float sh, Vec2& local) {
    if (!go) return false;
    Vec2 o, sz;
    if (!GetUIScreenRect(go, sw, sh, o, sz)) return false;
    Aff inv;
    if (!Invert(LocalToScreen(go, sw, sh, 0), inv)) return false;
    local = inv.Apply(screen);
    return std::isfinite(local.x) && std::isfinite(local.y);
}

bool UIFrameCorners(GameObject* go, float sw, float sh, Vec2 corners[4]) {
    if (!go || !corners) return false;
    Vec2 o, sz;
    if (!GetUIScreenRect(go, sw, sh, o, sz)) return false;
    const Aff m = LocalToScreen(go, sw, sh, 0);
    corners[0] = m.Apply(Vec2{0.0f, 0.0f});
    corners[1] = m.Apply(Vec2{sz.x, 0.0f});
    corners[2] = m.Apply(Vec2{sz.x, sz.y});
    corners[3] = m.Apply(Vec2{0.0f, sz.y});
    return true;
}

} // namespace okay
