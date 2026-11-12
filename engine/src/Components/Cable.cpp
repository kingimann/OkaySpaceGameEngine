#include "okay/Components/Cable.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

std::vector<Vec3> Cable::Curve(const Vec3& a, const Vec3& b, float sag, int segments) {
    segments = std::min(200, std::max(2, segments));
    std::vector<Vec3> pts;
    pts.reserve((std::size_t)segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const float t = (float)i / (float)segments;
        Vec3 p = a + (b - a) * t;
        p.y -= 4.0f * sag * t * (1.0f - t);       // 0 at both ends, `sag` at the middle
        pts.push_back(p);
    }
    return pts;
}

bool Cable::Ends(Vec3& a, Vec3& b) const {
    if (!gameObject || !gameObject->scene() || !transform) return false;
    Scene* sc = gameObject->scene();
    const GameObject* sg = startObject.empty() ? gameObject : sc->Find(startObject);
    if (!sg || !sg->transform) return false;
    a = sg->transform->LocalToWorldMatrix().MultiplyPoint(startOffset);
    if (endObject.empty()) {
        b = sg->transform->LocalToWorldMatrix().MultiplyPoint(startOffset + endOffset);
        return true;
    }
    const GameObject* eg = sc->Find(endObject);
    if (!eg || !eg->transform) return false;
    b = eg->transform->LocalToWorldMatrix().MultiplyPoint(endOffset);
    return true;
}

bool Cable::Rebuild() {
    Vec3 a, b;
    if (!Ends(a, b)) return false;
    auto* lr = gameObject->GetComponent<LineRenderer>();
    if (!lr) lr = gameObject->AddComponent<LineRenderer>();
    lr->useWorldSpace = true;
    lr->spline.clear();
    lr->width = width;
    lr->color = color;
    lr->positions = Curve(a, b, sag, segments);
    return true;
}

float Cable::Length() const {
    Vec3 a, b;
    if (!Ends(a, b)) return 0.0f;
    const auto pts = Curve(a, b, sag, segments);
    float len = 0.0f;
    for (std::size_t i = 1; i < pts.size(); ++i) len += (pts[i] - pts[i - 1]).Magnitude();
    return len;
}

} // namespace okay
