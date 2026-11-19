// The Improved 3D step: iterated contacts, box faces with up to four points, real inertia, bodies that sleep and wake as
// a group, swept fast bodies. Included by Physics3D.cpp (it uses the contact helpers in that file's anonymous namespace).
// The maths that does not need the scene lives in Physics/ContactSolver.hpp and Physics/BoxManifold.hpp so it can be tested
// without a scene. See docs/physics_plan.md for what each piece was written from.
//
// How a step runs in Improved quality (the Classic step is untouched):
//   1. speeds are updated (gravity, forces, drag, spin from torque) but nothing moves yet;
//   2. every touching pair is found at the current poses, with a small skin so a body at rest stays "touching";
//      a box on a box gets up to four contact points, a capsule on a box two, everything else one;
//   3. the contact solver repeats until the pushes settle, starting from last step's pushes;
//   4. bodies move by their solved speed (fast ones are swept so they cannot pass through a thin wall) and turn about their
//      centre of mass;
//   5. terrain, mesh, joints and the other passes run as before;
//   6. bodies that touch rest and wake together: a group sleeps when every body in it has been still.

namespace {

// ---- shared tuning (metres, seconds) ----
constexpr float kImpSkin        = 0.006f;   // contact reaches this far past touching, so resting bodies stay in contact
constexpr float kImpWakeSpeed   = 0.05f;    // an awake body slower than this does not wake a sleeping neighbour
constexpr float kImpSleepLinear = 0.05f;    // m/s
constexpr float kImpSleepAngular = 0.05f;   // rad/s
constexpr float kImpSleepTime   = 0.5f;     // s of stillness before a group sleeps

inline Mat3 ZeroM() { return contactsolver::ZeroMat(); }
inline Mat3 DiagInWorld(const Vec3& d, const Quat& q) {
    Mat3 R = Mat3::FromQuat(q);
    Mat3 D = ZeroM(); D.at(0, 0) = d.x; D.at(1, 1) = d.y; D.at(2, 2) = d.z;
    return R * D * R.Transpose();
}

/// The body a collider belongs to: the Rigidbody3D on its object, else (only when that ancestor asks for it with
/// `compoundChildren`) the nearest ancestor's body. nullptr = a static collider.
Rigidbody3D* BodyOfCollider(Collider3D* c) {
    GameObject* g = c ? c->gameObject : nullptr;
    if (!g) return nullptr;
    if (Rigidbody3D* r = g->GetComponent<Rigidbody3D>()) return r;
    if (!g->transform) return nullptr;
    for (Transform* t = g->transform->Parent(); t; t = t->Parent()) {
        GameObject* pg = t->gameObject;
        if (!pg) break;
        if (Rigidbody3D* r = pg->GetComponent<Rigidbody3D>()) return r->compoundChildren ? r : nullptr;
    }
    return nullptr;
}

// ---- mass properties (centre of mass and turning inertia of everything a body is made of) ----
struct Part { Vec3 c{0, 0, 0}; float vol = 0.0f; Mat3 iu = contactsolver::ZeroMat(); };   // iu: inertia about its own centre for unit mass

bool PartOfCollider(Collider3D* c, Part& p) {
    using S = Collider3D::Shape;
    Quat q = c->transform ? c->transform->Rotation() : Quat::Identity;
    p.c = c->WorldCenter();
    {   // The exact solids (Cylinder, Cone, Convex Hull, Compound) know their own volume, centre of mass and inertia.
        Vec3 com, in; float vol;
        if (c->MassShape(com, vol, in)) { p.c = com; p.vol = vol; p.iu = DiagInWorld(in, q); return true; }
        if (IsGenericShape(c)) return false;                    // Plane / Heightfield: endless or hollow, no mass
    }
    const float kPi = 3.14159265f;
    switch (c->shape()) {
    case S::Sphere: {
        float r = static_cast<SphereCollider3D*>(c)->WorldRadius();
        if (!(r > 0.0f)) return false;
        p.vol = 4.0f / 3.0f * kPi * r * r * r;
        float i = 0.4f * r * r;
        p.iu = DiagInWorld({i, i, i}, q);
        return true;
    }
    case S::Capsule: {
        auto* cap = static_cast<CapsuleCollider3D*>(c);
        float r = cap->WorldRadius();
        Vec3 a, b; cap->Segment(a, b);
        float L = (b - a).Magnitude();
        if (!(r > 0.0f)) return false;
        Vec3 d;
        float iAxis, iTrans;
        {
            float vc = kPi * r * r * L, vh = 4.0f / 3.0f * kPi * r * r * r;   // tube and the two end caps together
            p.vol = vc + vh;
            float fc = vc / p.vol, fh = vh / p.vol;
            float dd = 0.5f * L;
            iAxis = fc * 0.5f * r * r + fh * 0.4f * r * r;
            iTrans = fc * (3.0f * r * r + L * L) / 12.0f + fh * (0.4f * r * r + dd * dd + 0.75f * r * dd);
        }
        d = {cap->axis == 0 ? iAxis : iTrans, cap->axis == 1 ? iAxis : iTrans, cap->axis == 2 ? iAxis : iTrans};
        p.iu = DiagInWorld(d, q);
        return true;
    }
    case S::Mesh: {                                               // the box around its triangles, as a stand-in
        Vec3 mn, mx; c->WorldAABB(mn, mx);
        Vec3 h = (mx - mn) * 0.5f;
        p.c = (mn + mx) * 0.5f;
        p.vol = std::max(8.0f * h.x * h.y * h.z, 1e-6f);
        p.iu = DiagInWorld({(h.y * h.y + h.z * h.z) / 3.0f, (h.x * h.x + h.z * h.z) / 3.0f, (h.x * h.x + h.y * h.y) / 3.0f}, Quat::Identity);
        return true;
    }
    case S::Box: default: {
        Vec3 h = static_cast<BoxCollider3D*>(c)->HalfExtents();
        p.vol = std::max(8.0f * h.x * h.y * h.z, 1e-6f);
        p.iu = DiagInWorld({(h.y * h.y + h.z * h.z) / 3.0f, (h.x * h.x + h.z * h.z) / 3.0f, (h.x * h.x + h.y * h.y) / 3.0f}, q);
        return true;
    }
    }
}

/// The colliders on the child objects of `t` that belong to the body (children without a Rigidbody of their own).
void CollectChildColliders(Transform* t, std::vector<Collider3D*>& out) {
    std::vector<Transform*> stack;
    for (Transform* k : t->Children()) stack.push_back(k);
    while (!stack.empty()) {
        Transform* k = stack.back(); stack.pop_back();
        if (!k || !k->gameObject) continue;
        GameObject* g = k->gameObject;
        if (g->GetComponent<Rigidbody3D>()) continue;             // another body: not part of this one
        for (Collider3D* c : g->GetComponents<Collider3D>()) out.push_back(c);
        for (Transform* kk : k->Children()) stack.push_back(kk);
    }
}

struct MassProps {
    Vec3 com{0, 0, 0};                 // world
    Mat3 I = contactsolver::ZeroMat(); // world inertia about the centre of mass
    bool fromColliders = false;
    bool offCenter = false;            // the centre of mass is not at the object's origin
};

MassProps ComputeMassProps(Rigidbody3D* rb) {
    MassProps mp;
    Transform* t = rb->transform;
    if (!t || !rb->gameObject) return mp;
    std::vector<Collider3D*> cols = rb->gameObject->GetComponents<Collider3D>();
    if (rb->compoundChildren) CollectChildColliders(t, cols);
    Part parts[8]; int n = 0; float vol = 0.0f;
    for (Collider3D* c : cols) {
        if (n >= 8) break;
        if (!c->enabled || c->isTrigger || !c->gameObject || !c->gameObject->ActiveInHierarchy()) continue;
        if (PartOfCollider(c, parts[n])) { vol += parts[n].vol; ++n; }
    }
    const float M = rb->mass > 0.0f ? rb->mass : 1.0f;
    const Vec3 origin = t->Position();
    if (n == 0) {                                                 // nothing to size it from: a ball of radius 0.5
        float i = 0.4f * M * 0.25f;
        mp.com = origin; mp.I = ZeroM(); mp.I.at(0, 0) = mp.I.at(1, 1) = mp.I.at(2, 2) = i;
        return mp;
    }
    Vec3 com{0, 0, 0};
    float share[8];
    for (int i = 0; i < n; ++i) { share[i] = vol > 0.0f ? M * parts[i].vol / vol : M / (float)n; com = com + parts[i].c * share[i]; }
    com = com * (1.0f / M);
    Mat3 I = ZeroM();
    for (int i = 0; i < n; ++i) {
        Vec3 d = parts[i].c - com;
        Mat3 shifted = ZeroM();
        float dd = d.SqrMagnitude();
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b)
            shifted.at(a, b) = share[i] * ((a == b ? dd : 0.0f) - d[a] * d[b]);
        I = I + parts[i].iu * share[i] + shifted;
    }
    const float floorI = 1e-6f * M;                               // never exactly zero (a flat body)
    for (int a = 0; a < 3; ++a) I.at(a, a) += floorI;
    mp.com = com; mp.I = I; mp.fromColliders = true;
    mp.offCenter = (com - origin).SqrMagnitude() > 1e-8f;
    return mp;
}

Mat3 InverseInertia(const Mat3& I) {
    float det = I.Determinant();
    if (!(det > 1e-18f)) return ZeroM();
    return I.Inverse();
}

/// The centre of mass of a spinning body, only when it is not at the object's origin (offset colliders, compound shapes).
bool CenterOfMassOffOrigin(Rigidbody3D* rb, Vec3& com) {
    if (!rb->gameObject) return false;
    if (!rb->compoundChildren) {
        auto* only = rb->gameObject->GetComponent<Collider3D>();
        if (!only) return false;
        // The common case: one collider sitting on the origin. (Two or more colliders, or an offset, need the full sum.)
        if (only->offset.SqrMagnitude() == 0.0f && rb->gameObject->GetComponents<Collider3D>().size() == 1) return false;
    }
    MassProps mp = ComputeMassProps(rb);
    com = mp.com;
    return mp.offCenter;
}

// ---- contact generation with a skin ----
struct ImpManifold {
    int count = 0;
    Vec3 pos[4];
    float depth[4] = {0, 0, 0, 0};
    Vec3 n{0, 1, 0};                  // A toward B
};

void ManifoldFromContact(const Contact& c, ImpManifold& m) {
    m.count = 1; m.pos[0] = c.point; m.depth[0] = c.penetration; m.n = c.normal;
}

Vec3 ClosestOnOBBPoint(const OBB3& o, const Vec3& p) {
    Vec3 d = p - o.c;
    Vec3 l{Vec3::Dot(d, o.ax), Vec3::Dot(d, o.ay), Vec3::Dot(d, o.az)};
    l = ClampVec(l, {-o.h.x, -o.h.y, -o.h.z}, o.h);
    return o.c + o.ax * l.x + o.ay * l.y + o.az * l.z;
}

boxmanifold::Box ToManifoldBox(Collider3D* col) {
    OBB3 o = MakeOBB3(static_cast<BoxCollider3D*>(col));
    boxmanifold::Box b; b.c = o.c; b.h = o.h; b.ax[0] = o.ax; b.ax[1] = o.ay; b.ax[2] = o.az;
    return b;
}

/// Box against a capsule or cylinder: one point per end of the capsule's inner segment when both touch the same face (a
/// barrel lying on the floor), else the deepest single point. `boxIsA` says which side the box is on (the normal goes A to B).
bool CapsuleOnBox(Collider3D* boxCol, Collider3D* capCol, float skin, bool boxIsA, ImpManifold& m, Contact& out) {
    OBB3 o = MakeOBB3(static_cast<BoxCollider3D*>(boxCol));
    auto* cap = static_cast<CapsuleCollider3D*>(capCol);
    Vec3 a0, a1; cap->Segment(a0, a1);
    const float r = cap->WorldRadius();
    Contact c0 = TestOBB3Sphere(o, a0, r + skin), c1 = TestOBB3Sphere(o, a1, r + skin);   // normals: box toward capsule
    const float flip = boxIsA ? 1.0f : -1.0f;
    if (c0.hit && c1.hit && (a1 - a0).SqrMagnitude() > 1e-8f && Vec3::Dot(c0.normal, c1.normal) > 0.9f) {
        Vec3 n = (c0.normal + c1.normal).Normalized() * flip;
        m.count = 2; m.n = n;
        m.pos[0] = c0.point; m.depth[0] = c0.penetration - skin;
        m.pos[1] = c1.point; m.depth[1] = c1.penetration - skin;
        out.hit = true; out.normal = n;
        const int d = m.depth[0] >= m.depth[1] ? 0 : 1;
        out.penetration = m.depth[d]; out.point = m.pos[d];
        return true;
    }
    Contact c;
    if (c0.hit || c1.hit) c = (c0.hit && (!c1.hit || c0.penetration >= c1.penetration)) ? c0 : c1;
    else {                                                        // neither end touches: the middle might (an edge)
        Vec3 p = ClosestOnSegment(o.c, a0, a1);
        for (int it = 0; it < 3; ++it) p = ClosestOnSegment(ClosestOnOBBPoint(o, p), a0, a1);
        c = TestOBB3Sphere(o, p, r + skin);
        if (!c.hit) return false;
    }
    m.count = 1; m.n = c.normal * flip; m.pos[0] = c.point; m.depth[0] = c.penetration - skin;
    out.hit = true; out.normal = m.n; out.penetration = m.depth[0]; out.point = c.point;
    return true;
}

/// The contact test of the Improved step: like TestColliders, but a pair that is apart by less than `skin` still touches
/// (negative depth), and the result is a set of contact points (`m`), not one. Triangle meshes and hulls keep the classic test.
Contact ImprovedContact(Collider3D* a, Collider3D* b, float skin, ImpManifold& m) {
    using S = Collider3D::Shape;
    Contact out;
    const S sa = a->shape(), sb = b->shape();
    const bool special = ExactMesh(a) || ExactMesh(b) || UsesHull(a) || UsesHull(b) || IsGenericShape(a) || IsGenericShape(b) ||
                         a->contactOffset > 0.0f || b->contactOffset > 0.0f ||
                         (sa == S::Mesh && static_cast<MeshCollider3D*>(a)->IsDynamicConflict()) ||
                         (sb == S::Mesh && static_cast<MeshCollider3D*>(b)->IsDynamicConflict());
    if (special) {
        Contact c = TestColliders(a, b);
        if (c.hit) ManifoldFromContact(c, m);
        return c;
    }
    const bool aBox = IsBoxLike(sa), bBox = IsBoxLike(sb);
    if (aBox && bBox) {
        boxmanifold::Result r = boxmanifold::Collide(ToManifoldBox(a), ToManifoldBox(b), skin);
        if (!r.hit || r.count == 0) return out;
        m.count = r.count; m.n = r.normal;
        int deepest = 0;
        for (int i = 0; i < r.count; ++i) { m.pos[i] = r.pts[i].pos; m.depth[i] = r.pts[i].depth; if (r.pts[i].depth > r.pts[deepest].depth) deepest = i; }
        out.hit = true; out.normal = r.normal; out.penetration = r.pts[deepest].depth; out.point = r.pts[deepest].pos;
        return out;
    }
    if (aBox || bBox) {
        Collider3D* box = aBox ? a : b;
        Collider3D* other = aBox ? b : a;
        if (IsCapsuleLike(other->shape())) { CapsuleOnBox(box, other, skin, aBox, m, out); return out; }
        auto* sp = static_cast<SphereCollider3D*>(other);
        Contact c = TestOBB3Sphere(MakeOBB3(static_cast<BoxCollider3D*>(box)), sp->WorldCenter(), sp->WorldRadius() + skin);
        if (!c.hit) return out;
        if (!aBox) c.normal = c.normal * -1.0f;                   // the box-to-sphere normal, turned to point A toward B
        c.penetration -= skin;
        ManifoldFromContact(c, m);
        return c;
    }
    // sphere / capsule against sphere / capsule
    Vec3 ac, bc; float ar, br;
    if (IsCapsuleLike(sa) && IsCapsuleLike(sb)) {
        auto* ca = static_cast<CapsuleCollider3D*>(a); auto* cb = static_cast<CapsuleCollider3D*>(b);
        Vec3 a0, a1, b0, b1; ca->Segment(a0, a1); cb->Segment(b0, b1);
        ClosestSegSeg(a0, a1, b0, b1, ac, bc);
        ar = ca->WorldRadius(); br = cb->WorldRadius();
    } else {
        AsSphere(a, b->WorldCenter(), ac, ar);
        AsSphere(b, a->WorldCenter(), bc, br);
    }
    Contact c = TestSphereSphere(ac, ar + skin, bc, br);
    if (!c.hit) return out;
    c.penetration -= skin;
    c.point = ac + c.normal * ar;
    ManifoldFromContact(c, m);
    return c;
}

} // namespace

// ============================================================================================================
// What the Improved step keeps between steps and while a step runs.
// ============================================================================================================
struct Physics3D::Solver {
    // Last step's pushes per touching pair, so a stack starts the step already balanced (warm start).
    struct Cached {
        int count = 0;
        Vec3 local[4];                      // contact points in collider A's frame
        float jn[4] = {0, 0, 0, 0}, jt1[4] = {0, 0, 0, 0}, jt2[4] = {0, 0, 0, 0};
        Vec3 roll{0, 0, 0};
        std::uint64_t stamp = 0;
    };
    struct KeyHash {
        std::size_t operator()(const std::pair<Collider3D*, Collider3D*>& k) const {
            std::size_t a = reinterpret_cast<std::size_t>(k.first), b = reinterpret_cast<std::size_t>(k.second);
            return (a * 0x9E3779B97F4A7C15ull) ^ (b + 0x7F4A7C15ull + (a << 6) + (a >> 2));
        }
    };
    std::unordered_map<std::pair<Collider3D*, Collider3D*>, Cached, KeyHash> cache;
    std::uint64_t stepNo = 0;

    // This step.
    std::vector<contactsolver::Body> bodies;       // slot 0 is the immovable world
    std::vector<Rigidbody3D*> slotRb;
    std::vector<Vec3> v0, w0;                      // each slot's speeds when it was made (a callback that sets a speed before the solve is kept)
    std::vector<contactsolver::Manifold> manifolds;
    struct Meta { Collider3D* a; Collider3D* b; Rigidbody3D* ra; Rigidbody3D* rb; };
    std::vector<Meta> meta;
    struct Damp { Rigidbody3D* rb; float lin, ang; };
    std::vector<Damp> damp;                        // surface damping to apply to a body after the solve
    std::vector<Collider3D*> staticList;           // colliders that are not dynamic bodies (swept by automatic continuous collision)
    std::vector<Collider3D*> movingList;           // colliders of dynamic bodies (a fast body is also swept against the thin ones)
    bool staticListBuilt = false;
    contactsolver::Settings set;
    float dt = 0.02f;
    int warmCount = 0;

    void Begin(const std::vector<Rigidbody3D*>& all, float stepDt, int iterations) {
        dt = stepDt; ++stepNo;
        bodies.clear(); slotRb.clear(); v0.clear(); w0.clear(); manifolds.clear(); meta.clear(); damp.clear();
        staticList.clear(); movingList.clear(); staticListBuilt = false;
        warmCount = 0;
        bodies.emplace_back(); slotRb.push_back(nullptr); v0.emplace_back(); w0.emplace_back();   // slot 0: the world
        for (Rigidbody3D* rb : all) rb->m_solverIndex = -1;
        set = contactsolver::Settings{};
        set.velocityIterations = std::max(4, std::min(30, iterations));
        set.alternateOrder = true;
    }

    /// The solver slot of a body (made on first use). Static bodies and body-less colliders share slot 0.
    int SlotOf(Rigidbody3D* rb) {
        if (!rb || rb->bodyType == Rigidbody3D::BodyType::Static || !rb->transform) return 0;
        if (rb->m_solverIndex >= 0) return rb->m_solverIndex;
        contactsolver::Body b;
        b.v = rb->velocity; b.w = rb->angularVelocity;
        if (rb->bodyType == Rigidbody3D::BodyType::Dynamic && rb->InvMass() > 0.0f) {
            MassProps mp = ComputeMassProps(rb);
            b.pos = mp.com;
            b.invMass = rb->InvMass();
            b.invI = rb->freezeRotation ? ZeroM() : InverseInertia(mp.I);
        } else {
            b.pos = rb->transform->Position();                     // kinematic: carries things, is not moved by them
        }
        rb->m_solverIndex = (int)bodies.size();
        bodies.push_back(b); slotRb.push_back(rb); v0.push_back(b.v); w0.push_back(b.w);
        return rb->m_solverIndex;
    }

    /// Queue one touching pair for the solve. The caller has already decided it is solid and that at least one side is awake.
    void Queue(Collider3D* a, Collider3D* b, Rigidbody3D* ra, Rigidbody3D* rb, const ImpManifold& im) {
        contactsolver::Manifold m;
        m.a = SlotOf(ra); m.b = SlotOf(rb);
        if (m.a == 0 && m.b == 0) return;                          // nothing here can move
        m.n = im.n;
        m.count = std::min(4, im.count);
        for (int i = 0; i < m.count; ++i) { m.pts[i].pos = im.pos[i]; m.pts[i].depth = im.depth[i]; }
        const float fa = SurfaceFriction(a, ra, 0.6f), fb = SurfaceFriction(b, rb, 0.6f);
        m.friction = CombineSurface(fa, fb, a->frictionCombine, b->frictionCombine, true);
        m.restitution = CombineSurface(SurfaceBounce(a, ra), SurfaceBounce(b, rb), a->bounceCombine, b->bounceCombine, false);
        m.rolling = std::max(ra && ra->bodyType == Rigidbody3D::BodyType::Dynamic ? ra->rollingResistance : 0.0f,
                             rb && rb->bodyType == Rigidbody3D::BodyType::Dynamic ? rb->rollingResistance : 0.0f);
        // A Physics Material on either collider adds what a collider has no field for: grip while still, rolling friction,
        // damping, slope grip. (No material = nothing changes, so old scenes play as before.)
        const PhysicsMaterialData* ma = SurfaceOf(a->gameObject);
        const PhysicsMaterialData* mb = SurfaceOf(b->gameObject);
        if (ma || mb) {
            const float sa = ma ? ma->staticFriction * ma->slopeGrip : fa, sb = mb ? mb->staticFriction * mb->slopeGrip : fb;
            m.staticFriction = CombineSurface(sa, sb, a->frictionCombine, b->frictionCombine, true);
            if (ma && ma->rollingFriction >= 0.0f) m.rolling = std::max(ma->rollingFriction, mb && mb->rollingFriction >= 0.0f ? mb->rollingFriction : 0.0f);
            else if (mb && mb->rollingFriction >= 0.0f) m.rolling = std::max(m.rolling, mb->rollingFriction);
            // Damping: a body touching mud or sand is slowed by the surface it touches.
            if (ma && (ma->linearDamping > 0.0f || ma->angularDamping > 0.0f) && SlotOf(rb) > 0) damp.push_back({rb, ma->linearDamping, ma->angularDamping});
            if (mb && (mb->linearDamping > 0.0f || mb->angularDamping > 0.0f) && SlotOf(ra) > 0) damp.push_back({ra, mb->linearDamping, mb->angularDamping});
        }
        // Warm start from last step's pushes for the same pair: match points by place (a lone point carries over whole).
        auto it = cache.find({a, b});
        Transform* ta = a->transform;
        // (only a pair that touched in the step before: a collider freed and made again at the same address must not inherit a push)
        if (it != cache.end() && it->second.count > 0 && it->second.stamp + 1 >= stepNo && ta) {
            const Cached& c = it->second;
            const Quat inv = ta->Rotation().Inverse();
            const Vec3 pa = ta->Position();
            for (int i = 0; i < m.count; ++i) {
                int best = -1; float bd = 0.06f * 0.06f;
                if (m.count == 1 && c.count == 1) best = 0;
                else {
                    const Vec3 l = inv * (m.pts[i].pos - pa);
                    for (int k = 0; k < c.count; ++k) {
                        float d2 = (c.local[k] - l).SqrMagnitude();
                        if (d2 < bd) { bd = d2; best = k; }
                    }
                }
                if (best >= 0) {
                    m.pts[i].jn = c.jn[best]; m.pts[i].jt1 = c.jt1[best]; m.pts[i].jt2 = c.jt2[best];
                    ++warmCount;
                }
            }
            m.rollImpulse = c.roll;
        }
        manifolds.push_back(m);
        meta.push_back({a, b, ra, rb});
    }

    /// Bigger groups of touching bodies get more passes: a push has to cross every body of a tower, so a tower of ten needs about
    /// twice that many passes, and a lone crate needs the few it always did. The setting is the least a group gets; the most
    /// is 50 for a group of up to 24 bodies and 24 for a bigger one, and a group only pays for what it needs, so a pile of separate crates costs
    /// what it did. Groups are found with union-find over the bodies that can move.
    void ScaleIterations() {
        const std::size_t n = bodies.size();
        std::vector<int> parent(n), count(n, 1);
        for (std::size_t i = 0; i < n; ++i) parent[i] = (int)i;
        auto find = [&](int x) { while (parent[(std::size_t)x] != x) { parent[(std::size_t)x] = parent[(std::size_t)parent[(std::size_t)x]]; x = parent[(std::size_t)x]; } return x; };
        for (const contactsolver::Manifold& m : manifolds) {
            if (m.a <= 0 || m.b <= 0 || bodies[(std::size_t)m.a].invMass <= 0.0f || bodies[(std::size_t)m.b].invMass <= 0.0f) continue;
            int ra = find(m.a), rb = find(m.b);
            if (ra != rb) { parent[(std::size_t)ra] = rb; count[(std::size_t)rb] += count[(std::size_t)ra]; }
        }
        const int base = set.velocityIterations;
        int most = base;
        for (contactsolver::Manifold& m : manifolds) {
            int root = find(m.a > 0 && bodies[(std::size_t)m.a].invMass > 0.0f ? m.a : m.b);
            int k = root > 0 ? count[(std::size_t)root] : 1;
            int want = std::max(base, std::min(k <= 24 ? 50 : 24, 2 * k + 2));   // (a big pile is wide, not tall: it does not get the tower's passes)
            m.iters = want; most = std::max(most, want);
        }
        set.velocityIterations = most;
    }

    /// Visit the lowest contacts first (lowest = furthest along gravity). A push has to travel up a stack from the floor, and a
    /// pass that starts at the floor carries it all the way in one sweep instead of one crate per pass; with the pass order
    /// alternating, the weight also travels down. Written from the usual Gauss-Seidel observation that ordering the rows along
    /// the direction information flows converges faster. Ties keep their order, so the result is the same every run.
    void SortBottomUp(const Vec3& gravity) {
        Vec3 down = gravity;
        const float gl = down.Magnitude();
        down = gl > 1e-4f ? down * (1.0f / gl) : Vec3{0, -1, 0};
        const std::size_t n = manifolds.size();
        std::vector<float> key(n);
        for (std::size_t i = 0; i < n; ++i) {
            const contactsolver::Manifold& m = manifolds[i];
            float k = 0.0f;
            for (int p = 0; p < m.count; ++p) k += Vec3::Dot(m.pts[p].pos, down);
            key[i] = m.count > 0 ? k / (float)m.count : 0.0f;
        }
        std::vector<std::size_t> order(n);
        for (std::size_t i = 0; i < n; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return key[a] > key[b]; });   // most "down" first
        std::vector<contactsolver::Manifold> m2; m2.reserve(n);
        std::vector<Meta> t2; t2.reserve(n);
        for (std::size_t i : order) { m2.push_back(manifolds[i]); t2.push_back(meta[i]); }
        manifolds.swap(m2); meta.swap(t2);
    }

    /// Solve, store the pushes and hand the new speeds back to the rigidbodies.
    void Solve(const Scene::RemovalWatch& gone, const Vec3& gravity) {
        // A game script may change a body's speed between the moment its contacts were found and now (a jump pad's launch, a
        // blast in OnCollisionEnter). That change must survive the solve: take the new speed in as this step's start.
        for (std::size_t i = 1; i < bodies.size(); ++i) {
            Rigidbody3D* rb = slotRb[i];
            if (!rb || gone.Gone(rb) || rb->bodyType != Rigidbody3D::BodyType::Dynamic) continue;
            if (rb->velocity.x != v0[i].x || rb->velocity.y != v0[i].y || rb->velocity.z != v0[i].z) bodies[i].v = rb->velocity;
            if (rb->angularVelocity.x != w0[i].x || rb->angularVelocity.y != w0[i].y || rb->angularVelocity.z != w0[i].z) bodies[i].w = rb->angularVelocity;
        }
        if (manifolds.size() > 1) SortBottomUp(gravity);
        if (!manifolds.empty()) ScaleIterations();
        if (!manifolds.empty()) {
            contactsolver::Prepare(bodies, manifolds, dt, set);
            contactsolver::SolveVelocities(bodies, manifolds, set);
            contactsolver::SolvePositions(bodies, manifolds, set);
        }
        for (const Damp& d : damp) {                               // mud and sand slow what touches them
            if (!d.rb || gone.Gone(d.rb) || d.rb->m_solverIndex <= 0) continue;
            contactsolver::Body& b = bodies[(std::size_t)d.rb->m_solverIndex];
            if (d.lin > 0.0f) b.v = b.v * (1.0f / (1.0f + d.lin * dt));
            if (d.ang > 0.0f) b.w = b.w * (1.0f / (1.0f + d.ang * dt));
        }
        for (std::size_t i = 1; i < bodies.size(); ++i) {
            Rigidbody3D* rb = slotRb[i];
            if (!rb || gone.Gone(rb) || rb->bodyType != Rigidbody3D::BodyType::Dynamic) continue;
            rb->velocity = bodies[i].v;
            rb->angularVelocity = bodies[i].w;
            if (rb->freezeX) rb->velocity.x = 0.0f;
            if (rb->freezeY) rb->velocity.y = 0.0f;
            if (rb->freezeZ) rb->velocity.z = 0.0f;
        }
        for (std::size_t k = 0; k < manifolds.size(); ++k) {
            const contactsolver::Manifold& m = manifolds[k];
            const Meta& mt = meta[k];
            if (gone.Gone(mt.a) || gone.Gone(mt.b) || !mt.a->transform) continue;
            Cached& c = cache[{mt.a, mt.b}];
            c.count = m.count; c.stamp = stepNo; c.roll = m.rollImpulse;
            const Quat inv = mt.a->transform->Rotation().Inverse();
            const Vec3 pa = mt.a->transform->Position();
            for (int i = 0; i < m.count; ++i) {
                c.local[i] = inv * (m.pts[i].pos - pa);
                c.jn[i] = m.pts[i].jn; c.jt1[i] = m.pts[i].jt1; c.jt2[i] = m.pts[i].jt2;
            }
        }
        if ((stepNo & 63u) == 0) {                                 // forget pairs not seen for a while
            for (auto it = cache.begin(); it != cache.end();)
                it = (stepNo - it->second.stamp > 120) ? cache.erase(it) : std::next(it);
        }
    }

    /// Keep a pair's remembered pushes alive while both sides sleep (they are not solved, so not refreshed).
    void Touch(Collider3D* a, Collider3D* b) {
        auto it = cache.find({a, b});
        if (it != cache.end()) it->second.stamp = stepNo;
    }

    /// Move everything after the solve (defined below).
    void Move(Physics3D& phys, Scene& scene, const std::vector<Rigidbody3D*>& all, float stepDt,
              const Scene::RemovalWatch& gone, int& sweeps);
    /// Group sleeping (defined below).
    void Sleep(const std::vector<Rigidbody3D*>& all, float stepDt, const Scene::RemovalWatch& gone, PhysicsStepStats& st);
};

Physics3D::Physics3D() { for (int i = 0; i < 32; ++i) m_layerMask[i] = 0xFFFFFFFFu; }
Physics3D::~Physics3D() = default;

void Physics3D::Clear() {
    m_contacts.clear(); m_meshContacts.clear(); m_restContacts.clear();
    m_solver.reset();   // its remembered pushes point at colliders about to be freed
}

Vec3 Collider3D::WorldCenter() const {
    Vec3 p = transform ? transform->Position() : Vec3::Zero;
    if (offset.x == 0.0f && offset.y == 0.0f && offset.z == 0.0f) return p;
    if (transform && gameObject) {
        if (const Scene* sc = gameObject->scene()) {
            if (sc->physics3D().quality == PhysicsQuality::Improved) {
                const Vec3 s = transform->LossyScale();
                return p + transform->Rotation() * Vec3{offset.x * s.x, offset.y * s.y, offset.z * s.z};
            }
        }
    }
    return p + offset;
}

namespace {   // (these two helpers are used by the pair loop and by the Solver methods below)

/// An awake body that moves enough to wake a sleeping body it touches (a moving platform, a crate sliding into a stack).
bool CanWakeOthers(Rigidbody3D* mover) {
    if (!mover || mover->sleeping || mover->bodyType == Rigidbody3D::BodyType::Static) return false;
    if (mover->velocity.SqrMagnitude() > kImpWakeSpeed * kImpWakeSpeed) return true;
    if (mover->angularVelocity.SqrMagnitude() > kImpSleepAngular * kImpSleepAngular) return true;
    if (mover->bodyType == Rigidbody3D::BodyType::Kinematic && mover->hasPrevPos && mover->transform &&
        (mover->transform->Position() - mover->prevPos).SqrMagnitude() > 1e-8f) return true;   // moved by script or animation
    return false;
}

} // namespace

/// Move everything after the solve: bodies go by their solved speed plus the direct position correction; fast bodies are
/// swept so they stop at a thin wall; spinning bodies turn about their centre of mass.
void Physics3D::Solver::Move(Physics3D& phys, Scene& scene, const std::vector<Rigidbody3D*>& all, float dt,
                             const Scene::RemovalWatch& gone, int& sweeps) {
    Solver& S = *this;
    for (Rigidbody3D* rb : all) {
        if (gone.Gone(rb)) continue;
        if (!rb->enabled || !rb->gameObject || !rb->gameObject->ActiveInHierarchy()) continue;
        if (rb->bodyType == Rigidbody3D::BodyType::Static) continue;
        if (rb->bodyType == Rigidbody3D::BodyType::Dynamic && rb->sleeping) continue;
        Transform* t = rb->transform;
        if (!t) continue;
        Vec3 motion = rb->velocity * dt;
        if (rb->m_solverIndex > 0 && rb->m_solverIndex < (int)S.bodies.size()) motion = motion + S.bodies[(std::size_t)rb->m_solverIndex].dp;
        if (rb->freezeX) motion.x = 0.0f;
        if (rb->freezeY) motion.y = 0.0f;
        if (rb->freezeZ) motion.z = 0.0f;
        bool sweep = rb->continuousCollision;
        bool onlyStatics = false;
        if (!sweep && phys.autoContinuous && rb->bodyType == Rigidbody3D::BodyType::Dynamic) {
            // Fast = moves farther in a step than half the radius of the sphere that fits in the body.
            Collider3D* self = rb->gameObject->GetComponent<Collider3D>();
            const float len2 = motion.SqrMagnitude();
            if (self && len2 > 1e-8f) {
                Vec3 mn, mx; self->WorldAABB(mn, mx);
                float r = 0.5f * std::min(mx.x - mn.x, std::min(mx.y - mn.y, mx.z - mn.z));
                if (r < 0.01f) r = 0.01f;
                if (len2 > 0.25f * r * r) { sweep = true; onlyStatics = true; }
            }
        }
        if (sweep) { ++sweeps; phys.SweepBodyBy(scene, rb, motion, onlyStatics); }
        else MoveBodyWorld(t, motion);
        if (!rb->freezeRotation && rb->angularVelocity.SqrMagnitude() > 1e-12f) {
            Vec3 com;
            if (rb->bodyType == Rigidbody3D::BodyType::Dynamic && CenterOfMassOffOrigin(rb, com)) {
                const Vec3 origin = t->Position();
                const Quat before = t->Rotation();
                SpinBodyWorld(t, rb->angularVelocity, dt);
                const Quat dq = t->Rotation() * before.Inverse();
                MoveBodyWorld(t, (com + dq * (origin - com)) - origin);
            } else {
                SpinBodyWorld(t, rb->angularVelocity, dt);
            }
        }
    }
}

namespace {
int FindRoot(std::vector<int>& parent, int x) {
    while (parent[(std::size_t)x] != x) { parent[(std::size_t)x] = parent[(std::size_t)parent[(std::size_t)x]]; x = parent[(std::size_t)x]; }
    return x;
}
} // namespace

/// Sleeping for the Improved step: bodies that touch rest and wake together. A body counts as still when it is slow; a group
/// of touching bodies goes to sleep when every body in it has been still for half a second, and never while one of its
/// members cannot sleep or rests on a platform that is moving.
void Physics3D::Solver::Sleep(const std::vector<Rigidbody3D*>& all, float dt, const Scene::RemovalWatch& gone,
                              PhysicsStepStats& st) {
    Solver& S = *this;
    for (Rigidbody3D* rb : all) {
        if (gone.Gone(rb) || !rb->enabled || !rb->gameObject || !rb->gameObject->ActiveInHierarchy()) continue;
        if (rb->bodyType != Rigidbody3D::BodyType::Dynamic) continue;
        if (!rb->allowSleep) { rb->WakeUp(); continue; }
        if (rb->sleeping) continue;
        const bool calm = rb->velocity.SqrMagnitude() < kImpSleepLinear * kImpSleepLinear &&
                          rb->angularVelocity.SqrMagnitude() < kImpSleepAngular * kImpSleepAngular;
        rb->m_sleepTimer = calm ? rb->m_sleepTimer + dt : 0.0f;
    }
    const std::size_t n = S.bodies.size();
    std::vector<int> parent(n);
    for (std::size_t i = 0; i < n; ++i) parent[i] = (int)i;
    std::vector<char> blocked(n, 0);
    auto dynamicSlot = [&](int s) { return s > 0 && S.slotRb[(std::size_t)s] && S.slotRb[(std::size_t)s]->bodyType == Rigidbody3D::BodyType::Dynamic; };
    for (const contactsolver::Manifold& m : S.manifolds) {
        const bool da = dynamicSlot(m.a), db = dynamicSlot(m.b);
        if (da && db) {
            int ra = FindRoot(parent, m.a), rb = FindRoot(parent, m.b);
            if (ra != rb) parent[(std::size_t)ra] = rb;
        } else if (da && m.b > 0 && !gone.Gone(S.slotRb[(std::size_t)m.b]) && CanWakeOthers(S.slotRb[(std::size_t)m.b])) {
            blocked[(std::size_t)m.a] = 1;         // resting on a platform that is moving
        } else if (db && m.a > 0 && !gone.Gone(S.slotRb[(std::size_t)m.a]) && CanWakeOthers(S.slotRb[(std::size_t)m.a])) {
            blocked[(std::size_t)m.b] = 1;
        }
    }
    std::vector<float> minTimer(n, 1e30f);
    std::vector<char> groupBlocked(n, 0);
    for (std::size_t s = 1; s < n; ++s) {
        Rigidbody3D* rb = S.slotRb[s];
        if (!rb || gone.Gone(rb) || rb->bodyType != Rigidbody3D::BodyType::Dynamic || rb->sleeping) continue;
        const int root = FindRoot(parent, (int)s);
        minTimer[(std::size_t)root] = std::min(minTimer[(std::size_t)root], rb->m_sleepTimer);
        if (blocked[s] || !rb->allowSleep) groupBlocked[(std::size_t)root] = 1;
    }
    for (Rigidbody3D* rb : all) {
        if (gone.Gone(rb) || !rb->enabled || !rb->gameObject || !rb->gameObject->ActiveInHierarchy()) continue;
        if (rb->bodyType != Rigidbody3D::BodyType::Dynamic) continue;
        if (!rb->allowSleep || rb->sleeping) continue;
        bool sleepNow;
        if (rb->m_solverIndex > 0 && rb->m_solverIndex < (int)n) {
            const int root = FindRoot(parent, rb->m_solverIndex);
            sleepNow = !groupBlocked[(std::size_t)root] && minTimer[(std::size_t)root] >= kImpSleepTime;
        } else {
            sleepNow = rb->m_sleepTimer >= kImpSleepTime;
        }
        if (sleepNow) { rb->sleeping = true; rb->velocity = Vec3::Zero; rb->angularVelocity = Vec3::Zero; }
    }
    int roots = 0, awake = 0, asleep = 0;
    for (std::size_t s = 1; s < n; ++s) if (FindRoot(parent, (int)s) == (int)s) ++roots;
    for (Rigidbody3D* rb : all) {
        if (gone.Gone(rb) || rb->bodyType != Rigidbody3D::BodyType::Dynamic || !rb->enabled) continue;
        if (rb->sleeping) ++asleep; else ++awake;
    }
    st.islands = roots; st.awake = awake; st.asleep = asleep;
}
