// The 2D physics step: 2D bodies run on our own physics core (okay::phys::World, docs/physics/physics_core.md), the same
// solver 3D uses. Included at the end of Physics2D.cpp, so it shares that file's helpers (PieceMass, PiecesInertia,
// ClosestOnPoly, MakeCollision, DispatchCollision, DispatchTrigger, TriggerAccepts, Alive).
//
// A 2D body is a core body that cannot leave the x/y plane and turns about z only (the core's axis locks). A 2D shape is
// the same outline pushed out along z: a box is a box, a circle a ball, a capsule a capsule, a convex polygon a prism, an
// edge segment a strip 2 mm thick. Every shape is deep along z (at least twice its reach), so two shapes that overlap in
// the plane always overlap further along z and the contact normal stays in the plane. A collider whose object has no
// Rigidbody2D gets a kinematic body of its own that never moves by itself.
//
// Each step: scene -> world (bodies, colliders, joints made, changed or removed to match the components; a pose or speed
// game code changed is copied in), world.Step, world -> scene (pose, speed, sleeping), then the Enter / Stay / Exit
// callbacks from the world's touching pairs. Written for this engine.

namespace okay {

namespace {

constexpr float kEdgeHalfThickness = 0.001f;   // an edge line collides as a strip this thick on each side
constexpr float kSmallestSize = 1e-4f;         // a shape is never thinner than this
constexpr float kMaxSolveStep = 1.0f / 45.0f;  // a longer step is solved in equal slices (as the 3D step does)
constexpr float kKinematicCarrySpeed = 50.0f;  // a kinematic body moved by game code faster than this is a teleport

phys::V3 Flat(const Vec2& v) { return phys::V3{v.x, v.y, 0.0f}; }
Vec2 InPlane(const phys::V3& v) { return Vec2{v.x, v.y}; }
phys::Q TurnZ(float radians) { return phys::Q::AxisAngle(phys::V3{0.0f, 0.0f, 1.0f}, radians); }
// The angle of a rotation about z, in radians.
float TurnOf(const phys::Q& q) { return std::atan2(2.0f * q.w * q.z, q.w * q.w - q.z * q.z); }
// Half the depth along z of a shape that reaches `reach` from its centre in the plane.
float HalfDepth(float reach) { return std::max(0.5f, 2.0f * reach); }

struct CorePiece {
    phys::Shape shape;
    phys::Xf world;
    float area = 0.0f;   // in the plane: the piece weighs this share of its body
};

// Four corners that make a rectangle: its centre, half sizes and turn.
bool AsRectangle(const std::vector<Vec2>& v, Vec2& centre, Vec2& half, float& turn) {
    if (v.size() != 4) return false;
    const Vec2 along = v[1] - v[0], up = v[3] - v[0];
    const float w = along.Magnitude(), h = up.Magnitude();
    if (w < kSmallestSize || h < kSmallestSize) return false;
    if (std::fabs(Vec2::Dot(along, up)) > 1e-4f * w * h) return false;          // the two sides are square to each other
    if ((v[0] + along + up - v[2]).Magnitude() > 1e-4f * (w + h)) return false;  // and the far corner closes the shape
    centre = (v[0] + v[2]) * 0.5f;
    half = {w * 0.5f, h * 0.5f};
    turn = std::atan2(along.y, along.x);
    return true;
}

// A 2D collider as core shapes in world space (its convex pieces, without the contact offset).
void CorePiecesOf(const Collider2D* c, std::vector<CorePiece>& out) {
    std::vector<shape2d::Piece> pieces;
    shape2d::PiecesOf(c, pieces, false);
    for (const shape2d::Piece& p : pieces) {
        bool finite = std::isfinite(p.r);
        for (const Vec2& q : p.v) finite = finite && std::isfinite(q.x) && std::isfinite(q.y);
        if (!finite || p.v.empty()) continue;
        CorePiece cp;
        PieceMassProps mass;
        if (PieceMass(p, mass)) cp.area = (float)mass.area;
        if (p.v.size() == 1) {
            const float r = std::max(p.r, kSmallestSize);
            cp.shape = phys::Shape::Sphere(r);
            cp.world = phys::Xf{Flat(p.v[0]), phys::Q{}};
        } else if (p.v.size() == 2) {
            const Vec2 d = p.v[1] - p.v[0];
            const float len = d.Magnitude();
            const float turn = len > 1e-6f ? std::atan2(d.y, d.x) : 0.0f;
            const Vec2 mid = (p.v[0] + p.v[1]) * 0.5f;
            if (p.r > 0.0f) {   // a capsule: the core's capsule lies along its own +Y
                cp.shape = phys::Shape::Capsule(std::max(p.r, kSmallestSize), len * 0.5f);
                cp.world = phys::Xf{Flat(mid), TurnZ(turn - 1.57079632679f)};
            } else {            // an edge segment: a thin strip
                cp.shape = phys::Shape::Box(phys::V3{std::max(len * 0.5f, kSmallestSize), kEdgeHalfThickness, HalfDepth(len * 0.5f)});
                cp.world = phys::Xf{Flat(mid), TurnZ(turn)};
                cp.area = len * 2.0f * kEdgeHalfThickness;
            }
        } else {
            Vec2 centre, half;
            float turn = 0.0f;
            if (AsRectangle(p.v, centre, half, turn)) {
                cp.shape = phys::Shape::Box(phys::V3{half.x, half.y, HalfDepth(std::max(half.x, half.y))});
                cp.world = phys::Xf{Flat(centre), TurnZ(turn)};
            } else {            // a convex polygon: a prism around the average of its corners
                Vec2 mid{0.0f, 0.0f};
                for (const Vec2& q : p.v) mid = mid + q;
                mid = mid * (1.0f / (float)p.v.size());
                float reach = 0.0f;
                for (const Vec2& q : p.v) reach = std::max(reach, (q - mid).Magnitude());
                const float depth = HalfDepth(reach);
                std::vector<phys::V3> corners;
                corners.reserve(p.v.size() * 2);
                for (const Vec2& q : p.v) {
                    corners.push_back(phys::V3{q.x - mid.x, q.y - mid.y, -depth});
                    corners.push_back(phys::V3{q.x - mid.x, q.y - mid.y, depth});
                }
                cp.shape = phys::Shape::Hull(corners);
                if (!cp.shape.hull) continue;
                cp.world = phys::Xf{Flat(mid), phys::Q{}};
            }
        }
        out.push_back(std::move(cp));
    }
}

template <class T> void PutBytes(std::string& s, const T& v) { s.append(reinterpret_cast<const char*>(&v), sizeof(T)); }

// Everything that decides how a collider is built, as bytes: when they change the collider is built again.
void ColliderSignature(const Collider2D* c, const Rigidbody2D* body, std::uint32_t layerMask, std::string& s) {
    using S = Collider2D::Shape;
    s.clear();
    const S kind = c->shape();
    PutBytes(s, kind);
    PutBytes(s, c->offset); PutBytes(s, c->isTrigger); PutBytes(s, c->layer); PutBytes(s, layerMask);
    PutBytes(s, c->friction); PutBytes(s, c->bounciness); PutBytes(s, c->contactOffset);
    if (c->transform) {   // (a scale read back from a turning object wobbles in its last bits: 1 part in 100,000 counts)
        const Vec3 sc = c->transform->LossyScale();
        for (float v : {sc.x, sc.y}) PutBytes(s, std::round(v * 100000.0f));
    }
    if (body) { PutBytes(s, body->friction); PutBytes(s, body->bounciness); PutBytes(s, body->rollingResistance); }
    switch (kind) {
        case S::Box: PutBytes(s, static_cast<const BoxCollider2D*>(c)->size); break;
        case S::Circle: PutBytes(s, static_cast<const CircleCollider2D*>(c)->radius); break;
        case S::Capsule: {
            auto* cap = static_cast<const CapsuleCollider2D*>(c);
            PutBytes(s, cap->size); PutBytes(s, cap->direction);
            break;
        }
        case S::Edge:
        case S::Polygon:
            for (const Vec2& p : static_cast<const PolyShapeCollider2D*>(c)->points) PutBytes(s, p);
            break;
        case S::Composite: s += static_cast<const CompositeCollider2D*>(c)->parts; break;
    }
}

// Things of a map that were not seen this step, dropped in id order (so the world changes the same way every run).
template <class Map, class Drop> void DropUnseen(Map& map, Drop drop) {
    std::vector<std::pair<std::uint32_t, typename Map::key_type>> dead;
    for (auto& kv : map)
        if (!kv.second.seen) dead.emplace_back(kv.second.FirstId(), kv.first);
    std::sort(dead.begin(), dead.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    for (const auto& d : dead) {
        auto it = map.find(d.second);
        drop(it->second);
        map.erase(it);
    }
}

} // namespace

struct Physics2D::EngineBridge {
    phys::World world;
    struct BodyRec {
        phys::BodyId id = phys::kInvalidId;
        Vec2 pos{0, 0};                 // the transform's position and turn (degrees) after the last step
        float angle = 0.0f;
        Vec2 vel{0, 0};                 // the component's speed and spin after the last step
        float spin = 0.0f;
        phys::V3 corePos;               // the world's pose that was last written to the transform
        phys::Q coreRot;
        float mass = -1.0f;             // the mass handed to the world
        bool sleeping = false, seen = false;
        bool carried = false;           // a kinematic body game code moved this step (its speed was made up)
        bool shapeChanged = true;       // a collider was built or removed: work out the turning inertia again
        bool warned = false;            // a NaN or infinite pose or speed from game code was logged once
        bool badPose = false;           // the transform holds a NaN or infinite pose: the last good one is written back
        std::uint32_t FirstId() const { return id; }
    };
    struct ColRec {
        std::vector<phys::ColliderId> ids;
        phys::BodyId owner = phys::kInvalidId;
        bool ownBody = false;           // owner is a body made for this collider alone (its object has no Rigidbody2D)
        const Rigidbody2D* body = nullptr;
        std::string sig;
        Vec2 pos{0, 0};                 // an own body: the transform's position and turn (radians) it was placed at
        float angle = 0.0f;
        Vec2 centerLocal{0, 0};         // the collider's centre in its body's frame (the one-way test)
        bool oneWay = false;            // a one-way edge: its points and blocked side in the world, this step
        std::vector<Vec2> edge;
        Vec2 edgeNormal{0, 1};
        bool seen = false;
        std::uint32_t FirstId() const { return ids.empty() ? 0u : ids[0]; }
    };
    struct JointRec {
        phys::JointId id = phys::kInvalidId;
        std::string sig;
        Vec2 partnerPos{0, 0};          // where a partner without a body was (the joint follows it)
        bool seen = false;
        std::uint32_t FirstId() const { return id; }
    };
    std::unordered_map<const Rigidbody2D*, BodyRec> bodies;
    std::unordered_map<const Collider2D*, ColRec> colliders;
    std::unordered_map<const Joint2D*, JointRec> joints;
    // collider id -> its component and record, by id slot; the full id is kept so a stale id finds nothing
    struct Back { phys::ColliderId id = phys::kInvalidId; Collider2D* component = nullptr; const ColRec* rec = nullptr; };
    std::vector<Back> back;
    // component pairs touching after the last step, in the order their callbacks were sent, and the same as a set
    using Pair = std::pair<Collider2D*, Collider2D*>;
    struct PairHash { std::size_t operator()(const Pair& p) const { return std::hash<const void*>()(p.first) * 31u ^ std::hash<const void*>()(p.second); } };
    std::vector<Pair> touching;
    std::unordered_set<Pair, PairHash> touchingSet;
    static Pair Key(Collider2D* a, Collider2D* b) { return std::less<const void*>()(a, b) ? Pair{a, b} : Pair{b, a}; }

    const Back* Find(phys::ColliderId id) const {
        const std::size_t i = phys::IdIndex(id);
        return i < back.size() && back[i].id == id ? &back[i] : nullptr;
    }
    Collider2D* Component(phys::ColliderId id) const { const Back* b = Find(id); return b ? b->component : nullptr; }
    void Map(phys::ColliderId id, Collider2D* c, const ColRec* rec) {
        const std::size_t i = phys::IdIndex(id);
        if (back.size() <= i) back.resize(i * 2 + 1);
        back[i] = Back{id, c, rec};
    }
    void Unmap(const ColRec& r) {
        for (phys::ColliderId id : r.ids) {
            world.RemoveCollider(id);
            const std::size_t i = phys::IdIndex(id);
            if (i < back.size() && back[i].id == id) back[i] = Back{};
        }
        if (r.ownBody) world.DestroyBody(r.owner);
    }
    // A one-way edge lets through what is on its far side: true when the pair must not push this step. (Called from the
    // narrow phase, maybe on several threads: it only reads the records and the world.)
    bool PassesThrough(phys::ColliderId a, phys::ColliderId b) const {
        const Back* ba = Find(a);
        const Back* bb = Find(b);
        if (!ba || !bb) return false;
        for (int k = 0; k < 2; ++k) {
            const ColRec* platform = k == 0 ? ba->rec : bb->rec;
            const ColRec* other = k == 0 ? bb->rec : ba->rec;
            if (!platform->oneWay) continue;
            const Vec2 centre = InPlane(world.GetTransform(other->owner).Apply(Flat(other->centerLocal)));
            const Vec2 nearest = ClosestOnPoly(platform->edge, false, centre);
            if (Vec2::Dot(centre - nearest, platform->edgeNormal) < 0.0f) return true;
        }
        return false;
    }
};

Physics2D::Physics2D() { ResetLayerCollisions(); }
Physics2D::~Physics2D() = default;

void Physics2D::Clear() {
    m_engine.reset();   // the world holds bodies for components about to be freed
}

void Physics2D::Step(Scene& scene, float dt) {
    if (!(dt > 0.0f) || !std::isfinite(dt)) return;
    if (!m_engine) m_engine = std::make_unique<EngineBridge>();
    EngineBridge& E = *m_engine;
    phys::World& W = E.world;
    m_stats = PhysicsStepStats{};
    W.gravity = Flat(gravity);
    W.velocityIterations = std::min(std::max(solverIterations, 4), 30);
    W.restitutionThreshold = 0.5f;   // slower hits do not bounce (the 2D rule)
    W.maxLinearSpeed = 10000.0f;     // 2D games often count in pixels: the 3D speed cap is far too low for them
    W.reportStillPairs = true;       // two solid colliders that overlap raise OnCollision even when neither can be pushed
    W.shouldCollide = [this, &E](phys::ColliderId a, phys::ColliderId b) {
        const Collider2D* ca = E.Component(a);
        const Collider2D* cb = E.Component(b);
        // two colliders of one object never meet; the layer matrix decides the rest (a trigger's tag and layer filter is
        // applied in the event pass, because it can change while the two stay put)
        return ca && cb && ca->gameObject != cb->gameObject && LayersCollide(ca->layer, cb->layer);
    };
    W.combineMaterials = [&E](phys::ColliderId a, phys::ColliderId b, phys::World::PairMaterial& pm) {
        const Collider2D* ca = E.Component(a);
        const Collider2D* cb = E.Component(b);
        if (!ca || !cb) return;
        const phys::ColliderDef& da = E.world.GetCollider(a);
        const phys::ColliderDef& db = E.world.GetCollider(b);
        pm.friction = CombineSurface(da.friction, db.friction, ca->frictionCombine, cb->frictionCombine, true);
        pm.restitution = CombineSurface(da.restitution, db.restitution, ca->bounceCombine, cb->bounceCombine, false);
    };
    JobSystem& jobs = EngineJobs();
    if (jobs.WorkerCount() > 0)
        W.SetParallelFor([&jobs](int count, const std::function<void(int, int)>& fn) {
            jobs.ParallelFor(0, (std::size_t)count, 8, [&fn](std::size_t from, std::size_t to) { fn((int)from, (int)to); });
        });
    else
        W.SetParallelFor(nullptr);

    // A kinematic body that game code moved (a lift, a moving platform) gets the speed that takes it to its new place
    // in this step, so what stands on it is carried and what it meets is pushed. False (nothing done) when the jump is
    // too fast to be a move: that is a teleport.
    auto driveTo = [&W, dt](phys::BodyId id, const Vec2& pos, float radians) {
        const phys::Xf now = W.GetTransform(id);
        if ((pos - InPlane(now.p)).Magnitude() > kKinematicCarrySpeed * dt) return false;
        const float turn = std::remainder(radians - TurnOf(now.q), 6.28318530718f);
        W.SetLinearVelocity(id, (Flat(pos) - now.p) * (1.0f / dt));
        W.SetAngularVelocity(id, phys::V3{0.0f, 0.0f, turn / dt});
        return true;
    };

    // ---- 1a. bodies ----
    struct Push { phys::BodyId id; Vec2 force; float torque; };
    std::vector<Push> pushes;   // forces and torques handed over this step (every slice applies them)
    for (auto& kv : E.bodies) kv.second.seen = false;
    const std::vector<Rigidbody2D*> bodies = CachedComponents<Rigidbody2D>(scene);
    for (Rigidbody2D* rb : bodies) {
        if (!rb->enabled || !rb->gameObject || !rb->gameObject->ActiveInHierarchy() || !rb->transform) continue;
        Transform* t = rb->transform;
        const Vec3 at = t->Position();
        const Vec2 pos{at.x, at.y};
        const float angle = t->Rotation().ToEuler().z;
        const bool goodPose = std::isfinite(pos.x + pos.y + angle);
        const bool goodSpeed = std::isfinite(rb->velocity.x + rb->velocity.y + rb->angularVelocity);
        const float spin = rb->freezeRotation ? 0.0f : rb->angularVelocity;   // a frozen body keeps its number but never turns
        phys::BodySettings set;
        set.type = rb->bodyType == Rigidbody2D::BodyType::Dynamic ? phys::BodyType::Dynamic
                 : rb->bodyType == Rigidbody2D::BodyType::Kinematic ? phys::BodyType::Kinematic : phys::BodyType::Static;
        set.lockLinear = 4;                              // stays in the plane,
        set.lockAngular = rb->freezeRotation ? 7 : 3;    // turns about z only (or not at all)
        set.linearDamping = std::max(rb->drag, 0.0f);
        set.angularDamping = std::max(rb->angularDrag, 0.0f);
        set.gravityScale = rb->gravityScale;
        set.allowSleep = rb->allowSleep;
        set.bullet = rb->continuousCollision;
        const phys::BodyType type = set.type;
        auto it = E.bodies.find(rb);
        if (it == E.bodies.end()) {
            if (!goodPose) continue;   // nowhere to put it
            phys::BodyDef bd;
            bd.type = type;
            bd.position = Flat(pos);
            bd.rotation = TurnZ(angle * Mathf::Deg2Rad);
            if (goodSpeed) { bd.linearVelocity = Flat(rb->velocity); bd.angularVelocity = phys::V3{0.0f, 0.0f, spin * Mathf::Deg2Rad}; }
            bd.awake = !rb->sleeping;
            bd.lockLinear = 4;
            bd.lockAngular = 3;
            EngineBridge::BodyRec fresh;
            fresh.id = W.CreateBody(bd);
            fresh.pos = pos; fresh.angle = angle; fresh.vel = rb->velocity; fresh.spin = spin; fresh.sleeping = rb->sleeping;
            fresh.corePos = bd.position; fresh.coreRot = W.GetTransform(fresh.id).q;
            it = E.bodies.emplace(rb, fresh).first;
        }
        EngineBridge::BodyRec& r = it->second;
        r.seen = true;
        const phys::BodyId id = r.id;
        W.Configure(id, set);
        const float mass = rb->mass > 0.0f && std::isfinite(rb->mass) ? rb->mass : 1.0f;
        if (mass != r.mass) { W.SetMass(id, mass); r.mass = mass; r.shapeChanged = true; }
        if ((!goodPose || !goodSpeed) && !r.warned) {
            r.warned = true;
            Log::Warning("Rigidbody2D on '", rb->gameObject->name, "': a NaN or infinite position, rotation or speed was set; "
                         "the physics keeps its last good state");
        }
        r.badPose = !goodPose;
        const bool moved = goodPose && (!(pos == r.pos) || angle != r.angle);
        r.carried = moved && type == phys::BodyType::Kinematic && driveTo(id, pos, angle * Mathf::Deg2Rad);
        if (!r.carried) {
            if (moved) {
                W.SetTransform(id, Flat(pos), TurnZ(angle * Mathf::Deg2Rad));
                r.corePos = Flat(pos);
                r.coreRot = W.GetTransform(id).q;
            }
            if (goodSpeed && !(rb->velocity == r.vel)) W.SetLinearVelocity(id, Flat(rb->velocity));
            if (goodSpeed && spin != r.spin) W.SetAngularVelocity(id, phys::V3{0.0f, 0.0f, spin * Mathf::Deg2Rad});
        }
        if (r.sleeping && !rb->sleeping) W.Wake(id);
        Push push{id, rb->ConsumeForce(), rb->ConsumeTorque()};
        if (!std::isfinite(push.force.x + push.force.y)) push.force = Vec2::Zero;
        if (!std::isfinite(push.torque) || rb->freezeRotation) push.torque = 0.0f;
        if (type == phys::BodyType::Dynamic && (push.force.SqrMagnitude() > 0.0f || push.torque != 0.0f)) pushes.push_back(push);
    }

    // ---- 1b. colliders (one without a Rigidbody2D gets a body of its own, so a moved one can be moved alone) ----
    std::vector<phys::BodyId> drivenOwn;   // own bodies game code moved this step
    for (auto& kv : E.colliders) kv.second.seen = false;
    const std::vector<Collider2D*> cols = CachedComponents<Collider2D>(scene);
    std::vector<CorePiece> pieces;
    std::string sig;
    bool anyOneWay = false;
    for (Collider2D* c : cols) {
        if (!Alive(c) || !c->transform) continue;
        const Vec3 at = c->transform->Position();
        const Vec2 pos{at.x, at.y};
        const float angle = c->WorldRotation();
        if (!std::isfinite(pos.x + pos.y + angle)) continue;
        Rigidbody2D* rb = c->gameObject->GetComponent<Rigidbody2D>();
        auto bodyIt = rb ? E.bodies.find(rb) : E.bodies.end();
        if (bodyIt != E.bodies.end() && !bodyIt->second.seen) bodyIt = E.bodies.end();
        if (bodyIt == E.bodies.end()) rb = nullptr;   // no body, or it is switched off: a static collider
        ColliderSignature(c, rb, CollisionMask(c->layer), sig);
        auto it = E.colliders.find(c);
        if (it != E.colliders.end() && (it->second.sig != sig || it->second.body != rb)) {
            if (it->second.body) {
                auto old = E.bodies.find(it->second.body);
                if (old != E.bodies.end()) old->second.shapeChanged = true;
            }
            E.Unmap(it->second);
            E.colliders.erase(it);
            it = E.colliders.end();
        }
        if (it == E.colliders.end()) {
            pieces.clear();
            CorePiecesOf(c, pieces);
            if (pieces.empty()) continue;
            EngineBridge::ColRec r;
            r.sig = sig;
            r.body = rb;
            r.pos = pos;
            r.angle = angle;
            if (rb) {
                r.owner = bodyIt->second.id;
                bodyIt->second.shapeChanged = true;
            } else {
                phys::BodyDef bd;
                bd.type = phys::BodyType::Kinematic;   // it never moves by itself; game code moving its object drives it
                bd.position = Flat(pos);
                bd.rotation = TurnZ(angle);
                bd.awake = false;
                r.owner = W.CreateBody(bd);
                r.ownBody = true;
            }
            const phys::Xf bodyX = W.GetTransform(r.owner);
            const phys::Xf toBody{phys::InvRotate(bodyX.q, -bodyX.p), phys::Conj(bodyX.q)};
            for (const CorePiece& p : pieces) {
                phys::ColliderDef cd;
                cd.shape = p.shape;
                cd.local = toBody * p.world;
                // a piece weighs its area in the plane, whatever its depth along z
                const float volume = p.shape.Mass(1.0f).mass;
                cd.density = volume > 1e-12f && p.area > 0.0f ? p.area / volume : 1.0f;
                cd.sensor = c->isTrigger;
                cd.layer = (std::uint32_t)(c->layer & 31);
                cd.mask = CollisionMask(c->layer);
                cd.friction = std::max(SurfaceFriction(c, rb, 0.4f), 0.0f);
                cd.restitution = std::max(SurfaceBounce(c, rb), 0.0f);
                cd.rollingResistance = rb ? std::min(std::max(rb->rollingResistance, 0.0f), 1.0f) : 0.0f;
                cd.contactOffset = std::max(c->contactOffset, 0.0f);
                const phys::ColliderId id = W.AddCollider(r.owner, cd);
                if (id != phys::kInvalidId) r.ids.push_back(id);
            }
            if (r.ids.empty()) {
                if (r.ownBody) W.DestroyBody(r.owner);
                continue;
            }
            r.centerLocal = InPlane(bodyX.ApplyInv(Flat(c->WorldCenter())));
            it = E.colliders.emplace(c, std::move(r)).first;
            for (phys::ColliderId id : it->second.ids) E.Map(id, c, &it->second);
        }
        EngineBridge::ColRec& r = it->second;
        r.seen = true;
        if (r.ownBody && (!(pos == r.pos) || angle != r.angle)) {
            // a collider without a Rigidbody2D, moved by game code (a platform on a tween): it is driven like a kinematic body
            if (driveTo(r.owner, pos, angle)) drivenOwn.push_back(r.owner);
            else W.SetTransform(r.owner, Flat(pos), TurnZ(angle));
            r.pos = pos;
            r.angle = angle;
        }
        r.oneWay = c->shape() == Collider2D::Shape::Edge && static_cast<EdgeCollider2D*>(c)->oneWay;
        if (r.oneWay) {
            auto* edge = static_cast<EdgeCollider2D*>(c);
            r.edge = edge->WorldPoints();
            r.edgeNormal = Vec2::Rotate(edge->oneWayNormal.Normalized(), angle * Mathf::Rad2Deg);   // the blocked side turns with the edge
            anyOneWay = true;
        }
    }
    W.contactEnabled = nullptr;
    if (anyOneWay) W.contactEnabled = [&E](phys::ColliderId a, phys::ColliderId b) { return !E.PassesThrough(a, b); };

    // ---- 1c. joints ----
    struct LiveJoint { Joint2D* joint; phys::JointId id; };
    std::vector<LiveJoint> liveJoints;
    for (auto& kv : E.joints) kv.second.seen = false;
    for (Joint2D* j : CachedComponents<Joint2D>(scene)) {
        if (j->broken || !j->enabled || !j->gameObject || !j->transform) continue;
        Rigidbody2D* self = j->gameObject->GetComponent<Rigidbody2D>();
        auto selfIt = self ? E.bodies.find(self) : E.bodies.end();
        if (selfIt == E.bodies.end() || !selfIt->second.seen) continue;   // the joint moves THIS body
        const phys::BodyId mine = selfIt->second.id;
        GameObject* partner = j->connectedBody.empty() ? nullptr : CachedPartner(scene, j);
        Rigidbody2D* partnerBody = partner ? partner->GetComponent<Rigidbody2D>() : nullptr;
        auto partnerIt = partnerBody ? E.bodies.find(partnerBody) : E.bodies.end();
        const phys::BodyId other = partnerIt != E.bodies.end() && partnerIt->second.seen && partnerIt->second.id != mine
                                 ? partnerIt->second.id : phys::kInvalidId;
        const Joint2D::Mode mode = j->mode == (int)Joint2D::Mode::Spring || j->mode == (int)Joint2D::Mode::Pin ||
                                   j->mode == (int)Joint2D::Mode::Hinge ? (Joint2D::Mode)j->mode : Joint2D::Mode::Distance;
        // where the partner end is: the partner object, or the world anchor
        Vec2 partnerPos = j->anchor;
        if (partner && partner->transform) { const Vec3 p = partner->transform->Position(); partnerPos = {p.x, p.y}; }
        sig.clear();
        PutBytes(sig, mode); PutBytes(sig, partner); PutBytes(sig, other); PutBytes(sig, j->autoConfigure);
        if (!j->autoConfigure) PutBytes(sig, j->distance);
        if (!partner) PutBytes(sig, j->anchor);
        auto it = E.joints.find(j);
        if (it != E.joints.end() && (it->second.sig != sig || !W.IsValidJoint(it->second.id) || !j->initialized)) {
            W.DestroyJoint(it->second.id);
            E.joints.erase(it);
            it = E.joints.end();
        }
        if (it == E.joints.end()) {
            // built from the poses right now (the start pose). The core's body A is the partner (or the world), B this body.
            const phys::Xf mineX = W.GetTransform(mine);
            const phys::Xf otherX = W.GetTransform(other);   // (the identity for the world)
            const Vec3 here = j->transform->Position();
            const Vec2 selfPos{here.x, here.y};
            j->pinOffset = selfPos - partnerPos;
            j->hingeLocalA = partnerPos - selfPos;
            j->refAngleA = j->transform->localRotation.ToEuler().z;
            j->restLen = j->autoConfigure ? (selfPos - partnerPos).Magnitude() : j->distance;
            phys::JointDef d;
            d.bodyA = other;
            d.bodyB = mine;
            if (mode == Joint2D::Mode::Pin) {            // a weld: this body keeps its place and turn against the partner
                d.type = phys::JointType::Fixed;
                d.frameA = phys::Xf{otherX.ApplyInv(mineX.p), phys::NormalizeQ(phys::Conj(otherX.q) * mineX.q)};
            } else if (mode == Joint2D::Mode::Hinge) {   // a pivot at the partner end; the hinge axis (the frame's X) is world z
                d.type = phys::JointType::Hinge;
                const phys::Q axisZ = phys::Q::AxisAngle(phys::V3{0.0f, 1.0f, 0.0f}, -1.57079632679f);
                d.frameA = phys::Xf{otherX.ApplyInv(Flat(partnerPos)), phys::NormalizeQ(phys::Conj(otherX.q) * axisZ)};
                d.frameB = phys::Xf{mineX.ApplyInv(Flat(partnerPos)), phys::NormalizeQ(phys::Conj(mineX.q) * axisZ)};
            } else {                                     // a rod, or a spring about the rest length
                d.type = phys::JointType::Distance;
                d.frameA.p = otherX.ApplyInv(Flat(partnerPos));
                d.frameB.p = mineX.ApplyInv(Flat(selfPos));
                d.minLength = d.maxLength = std::max(j->restLen, 0.0f);
                if (mode == Joint2D::Mode::Spring) d.minLength = 0.0f;
            }
            phys::SetupJoint(d);
            EngineBridge::JointRec r;
            r.id = W.CreateJoint(d);
            if (r.id == phys::kInvalidId) continue;
            r.sig = sig;
            r.partnerPos = partnerPos;
            j->initialized = true;
            it = E.joints.emplace(j, std::move(r)).first;
        }
        EngineBridge::JointRec& r = it->second;
        r.seen = true;
        liveJoints.push_back({j, r.id});
        // what game code changes while playing: spring, limits, motor, and a partner without a body that moved. Worked out
        // on a copy; the joint is only edited (which wakes its bodies) when something changed, so jointed bodies can sleep.
        phys::JointDef d = W.GetJoint(r.id);
        bool changed = false;
        auto put = [&changed](auto& field, const auto& value) { if (!(field == value)) { field = value; changed = true; } };
        if (other == phys::kInvalidId && !(partnerPos == r.partnerPos)) {
            put(d.frameA.p, d.frameA.p + Flat(partnerPos - r.partnerPos));
            r.partnerPos = partnerPos;
        }
        if (mode == Joint2D::Mode::Spring) {
            put(d.distanceStiffness, std::max(j->spring, 0.0f));
            put(d.distanceDamper, std::max(j->damper, 0.0f));
        } else if (mode == Joint2D::Mode::Hinge) {
            phys::JointAxis& turn = d.angular[0];
            put(turn.mode, j->useLimits ? phys::AxisMode::Limited : phys::AxisMode::Free);
            put(turn.lower, std::min(j->minAngle, j->maxAngle) * Mathf::Deg2Rad);
            put(turn.upper, std::max(j->minAngle, j->maxAngle) * Mathf::Deg2Rad);
            put(turn.motor, j->useMotor);
            put(turn.motorSpeed, j->motorSpeed * Mathf::Deg2Rad);
            put(turn.motorMaxForce, std::max(j->maxMotorTorque, 0.0f));
        }
        if (changed) W.EditJoint(r.id) = d;
    }

    // ---- 1d. remove what is gone ----
    DropUnseen(E.joints, [&W](EngineBridge::JointRec& r) { W.DestroyJoint(r.id); });
    DropUnseen(E.colliders, [&E](EngineBridge::ColRec& r) {
        if (r.body) {
            auto owner = E.bodies.find(r.body);
            if (owner != E.bodies.end()) owner->second.shapeChanged = true;
        }
        E.Unmap(r);
    });
    DropUnseen(E.bodies, [&W](EngineBridge::BodyRec& r) { W.DestroyBody(r.id); });

    // ---- 1e. turning inertia: the real 2D figure of the body's solid shapes (a disc, not a ball; a prism's is its outline's) ----
    for (Rigidbody2D* rb : bodies) {
        auto it = E.bodies.find(rb);
        if (it == E.bodies.end() || !it->second.shapeChanged) continue;
        it->second.shapeChanged = false;
        std::vector<shape2d::Piece> solid;
        for (Collider2D* c : rb->gameObject->GetComponents<Collider2D>()) {
            auto ci = E.colliders.find(c);
            if (ci != E.colliders.end() && ci->second.body == rb && !c->isTrigger) shape2d::PiecesOf(c, solid, false);
        }
        const float perUnitMass = PiecesInertia(solid);
        const float I = perUnitMass * it->second.mass;
        W.SetInertia(it->second.id, I > 1e-8f ? phys::M33::Diagonal(phys::V3{I, I, I}) : phys::M33::Zero());
    }

    // ---- 2. step ----
    const int slices = dt > kMaxSolveStep * 1.001f ? (int)std::ceil(dt / kMaxSolveStep - 0.001f) : 1;
    for (int i = 0; i < slices; ++i) {
        for (const Push& p : pushes) {
            if (p.force.SqrMagnitude() > 0.0f) W.ApplyForceAtCenter(p.id, Flat(p.force));
            if (p.torque != 0.0f) W.ApplyTorque(p.id, phys::V3{0.0f, 0.0f, p.torque});
        }
        W.Step(dt / (float)slices);
    }
    for (phys::BodyId id : drivenOwn) {   // their made-up speed was for this step only
        W.SetLinearVelocity(id, phys::V3{});
        W.SetAngularVelocity(id, phys::V3{});
    }

    // A breakable joint breaks when its two ends were pulled further apart than breakForce (a length, the Joint2D
    // meaning). A firm joint hides the stretch it undid, so that is worked out from the push it gave: push * dt^2 / mass.
    for (const LiveJoint& lj : liveJoints) {
        Joint2D* j = lj.joint;
        if (!j->breakable || !W.IsValidJoint(lj.id)) continue;
        const phys::JointDef& d = W.GetJoint(lj.id);
        const phys::V3 endA = W.IsValid(d.bodyA) ? W.GetTransform(d.bodyA).Apply(d.frameA.p) : d.frameA.p;
        const phys::V3 endB = W.GetTransform(d.bodyB).Apply(d.frameB.p);
        float stretch = phys::Len(endB - endA);
        if (d.type == phys::JointType::Distance) stretch = std::fabs(stretch - d.maxLength);
        float give = 0.0f;   // 1 / mass of the two ends
        for (phys::BodyId b : {d.bodyA, d.bodyB})
            if (W.IsValid(b) && W.GetType(b) == phys::BodyType::Dynamic && W.GetMass(b) > 0.0f) give += 1.0f / W.GetMass(b);
        stretch = std::max(stretch, phys::Len(W.JointForce(lj.id)) * dt * dt * give);
        if (stretch > j->breakForce) {
            j->broken = true;
            W.DestroyJoint(lj.id);
        }
    }

    // ---- 3. world -> scene ----
    for (Rigidbody2D* rb : bodies) {
        auto it = E.bodies.find(rb);
        if (it == E.bodies.end()) continue;
        EngineBridge::BodyRec& r = it->second;
        Transform* t = rb->transform;
        const bool dynamic = rb->bodyType == Rigidbody2D::BodyType::Dynamic;
        if (rb->bodyType != Rigidbody2D::BodyType::Static) {
            const phys::Xf x = W.GetTransform(r.id);
            if (r.badPose) {   // game code left a NaN or infinite pose: the object goes back to the last good one, flat
                if (!std::isfinite(t->localPosition.z)) t->localPosition.z = 0.0f;
                t->localRotation = Quat::Identity;
            }
            if (!(x.p == r.corePos) || r.badPose) {
                if (!t->Parent()) { t->localPosition.x = x.p.x; t->localPosition.y = x.p.y; }
                else t->SetPosition(Vec3{x.p.x, x.p.y, r.badPose ? 0.0f : t->Position().z});
                r.corePos = x.p;
            }
            if (!(x.q == r.coreRot) || r.badPose) {   // only z turns; the object's other angles stay as they were
                const bool parented = t->Parent() != nullptr;
                Vec3 e = (parented ? t->Rotation() : t->localRotation).ToEuler();
                e.z = TurnOf(x.q) * Mathf::Rad2Deg;
                if (parented) t->SetRotation(Quat::Euler(e));
                else t->localRotation = Quat::Euler(e);
                r.coreRot = x.q;
            }
            if (r.carried) {   // the made-up speed was for this step only: the body keeps the speed game code gave it
                W.SetLinearVelocity(r.id, Flat(rb->velocity));
                W.SetAngularVelocity(r.id, phys::V3{0.0f, 0.0f, (rb->freezeRotation ? 0.0f : rb->angularVelocity) * Mathf::Deg2Rad});
            }
            rb->velocity = InPlane(W.GetLinearVelocity(r.id));
            if (!rb->freezeRotation) rb->angularVelocity = W.GetAngularVelocity(r.id).z * Mathf::Rad2Deg;
            rb->sleeping = dynamic && !W.IsAwake(r.id);
        }
        const Vec3 at = t->Position();
        r.pos = {at.x, at.y};
        r.angle = t->Rotation().ToEuler().z;
        r.vel = rb->velocity;
        r.spin = rb->freezeRotation ? 0.0f : rb->angularVelocity;
        r.sleeping = rb->sleeping;
        if (dynamic) {
            ++m_stats.bodies;
            ++(rb->sleeping ? m_stats.asleep : m_stats.awake);
        }
    }
    const phys::WorldStats& ws = W.Stats();
    m_stats.contacts = ws.points;
    m_stats.manifolds = ws.touching;
    m_stats.islands = ws.islands;
    m_stats.warmStarted = ws.warmPoints;

    // ---- 4. callbacks ----
    // A pair is two collider components, so the several pieces of a polygon or composite touching one collider are one
    // pair, and a collider built again while playing (a friction or scale change) stays the same pair.
    Scene::RemovalWatch gone(scene);   // a callback may remove a collider: every pointer is checked before it is read
    struct Touch { Collider2D* a; Collider2D* b; phys::ContactEvent e; };
    std::vector<Touch> touches;
    W.ForEachTouching([&](const phys::ContactEvent& e) {
        Collider2D* a = E.Component(e.a);
        Collider2D* b = E.Component(e.b);
        if (a && b && a != b) touches.push_back({a, b, e});
    });
    std::vector<EngineBridge::Pair> nowList;
    std::unordered_set<EngineBridge::Pair, EngineBridge::PairHash> nowSet;
    for (const Touch& touch : touches) {
        Collider2D* a = touch.a;
        Collider2D* b = touch.b;
        const phys::ContactEvent& e = touch.e;
        const EngineBridge::Pair key = EngineBridge::Key(a, b);
        if (nowSet.count(key)) continue;   // another piece of the same pair already reported it
        const bool removed = gone.Gone(a) || gone.Gone(b);
        // a trigger's tag and layer filter: a filtered-out object passes through silently (Exit if it was inside)
        if (!removed && e.sensor && (!TriggerAccepts(a, b) || !TriggerAccepts(b, a))) continue;
        nowSet.insert(key);
        nowList.push_back({a, b});
        if (removed) continue;
        const bool stay = E.touchingSet.count(key) != 0;
        Vec2 n = InPlane(e.normal);
        if (n.SqrMagnitude() > 1e-12f) n = n.Normalized();
        const Vec2 point = InPlane(e.point);
        if (PhysicsDebug::Collect() && (int)m_debugContacts.size() < PhysicsDebug::kMaxContacts)
            m_debugContacts.push_back({point, n, e.sensor});
        if (e.sensor) {
            const auto fn = stay ? &Component::OnTriggerStay2D : &Component::OnTriggerEnter2D;
            DispatchTrigger(a, fn, b, point, n, gone);
            if (!gone.Gone(a) && !gone.Gone(b)) DispatchTrigger(b, fn, a, point, -n, gone);
            continue;
        }
        float depth = 0.0f;
        phys::Manifold manifold;
        if (W.GetManifold(e.a, e.b, manifold))
            for (int k = 0; k < manifold.count; ++k) depth = std::max(depth, -manifold.pts[k].separation);
        auto speedAt = [&](phys::BodyId body) {
            return W.GetLinearVelocity(body) + phys::Cross(W.GetAngularVelocity(body), e.point - W.GetCenterOfMass(body));
        };
        Vec2 closing = InPlane(speedAt(e.bodyB) - speedAt(e.bodyA));   // b's speed minus a's at the contact
        // when it begins the solver has already stopped the two: report the speed they met with
        if (!stay) closing = closing - n * Vec2::Dot(closing, n) - n * e.approachSpeed;
        const auto fn = stay ? &Component::OnCollisionStay2D : &Component::OnCollisionEnter2D;
        DispatchCollision(a->gameObject, fn, MakeCollision(b, n, depth, point, closing), gone);
        if (!gone.Gone(a) && !gone.Gone(b))
            DispatchCollision(b->gameObject, fn, MakeCollision(a, -n, depth, point, -closing), gone);
    }
    // Exit for pairs that no longer touch (in the order they began). A collider that was destroyed gets none; one that
    // was only switched off, or whose object was, still does.
    std::unordered_set<const Collider2D*> exists;
    for (const EngineBridge::Pair& pair : E.touching) {
        if (nowSet.count(EngineBridge::Key(pair.first, pair.second))) continue;
        if (exists.empty()) exists.insert(cols.begin(), cols.end());
        Collider2D* a = pair.first;
        Collider2D* b = pair.second;
        if (!exists.count(a) || !exists.count(b) || gone.Gone(a) || gone.Gone(b)) continue;
        if (a->isTrigger || b->isTrigger) {
            // no overlap any more: report the midpoint between the two centres
            const Vec2 ca = a->WorldCenter(), cb = b->WorldCenter();
            const Vec2 n = (cb - ca).Normalized();
            DispatchTrigger(a, &Component::OnTriggerExit2D, b, (ca + cb) * 0.5f, n, gone);
            if (!gone.Gone(a) && !gone.Gone(b)) DispatchTrigger(b, &Component::OnTriggerExit2D, a, (ca + cb) * 0.5f, -n, gone);
        } else {
            DispatchCollision(a->gameObject, &Component::OnCollisionExit2D, MakeCollision(b, Vec2::Zero, 0.0f, Vec2::Zero, Vec2::Zero), gone);
            if (!gone.Gone(a) && !gone.Gone(b))
                DispatchCollision(b->gameObject, &Component::OnCollisionExit2D, MakeCollision(a, Vec2::Zero, 0.0f, Vec2::Zero, Vec2::Zero), gone);
        }
    }
    // pairs whose collider a callback removed are forgotten, so no freed pointer is kept
    E.touching.clear();
    E.touchingSet.clear();
    for (const EngineBridge::Pair& pair : nowList) {
        if (gone.Gone(pair.first) || gone.Gone(pair.second)) continue;
        E.touching.push_back(pair);
        E.touchingSet.insert(EngineBridge::Key(pair.first, pair.second));
    }
}

} // namespace okay
