// The Improved 2D step (see Physics3DImproved.inl and docs/physics/physics_plan.md). Included by Physics2D.cpp.

struct Physics2D::Solver {
    int placeholder = 0;
};

Physics2D::Physics2D() { for (int i = 0; i < 32; ++i) m_layerMask[i] = 0xFFFFFFFFu; }
Physics2D::~Physics2D() = default;

Vec2 Collider2D::WorldCenter() const {
    Vec3 p = transform ? transform->Position() : Vec3::Zero;
    if (offset.x == 0.0f && offset.y == 0.0f) return {p.x, p.y};
    if (transform && gameObject) {
        if (const Scene* sc = gameObject->scene()) {
            if (sc->physics().quality == PhysicsQuality::Improved) {
                // The offset turns and scales with the object, as a child at that local position would.
                const Vec3 s = transform->LossyScale();
                const Vec3 o = transform->Rotation() * Vec3{offset.x * s.x, offset.y * s.y, 0.0f};
                return {p.x + o.x, p.y + o.y};
            }
        }
    }
    return {p.x + offset.x, p.y + offset.y};
}
