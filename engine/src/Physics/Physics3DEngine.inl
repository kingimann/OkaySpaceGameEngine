// The Engine physics quality: our own physics engine core (okay::phys::World, engine/include/okay/Physics/Core) runs
// the scene. Included at the end of Physics3D.cpp, so it shares that file's helpers (BodyOfCollider, MakeCollision,
// DispatchCollision, DispatchTrigger, TriggerAccepts, Alive).
//
// Each step:
//   1. scene -> world: bodies, colliders and joints are created, updated or removed to match the components (the order
//      is the scene's component order, so the world is built the same way every run); a transform or velocity that
//      game code changed since the last step is copied in (a teleport); forces and torques are handed over;
//   2. world.Step(dt) (islands on the engine job system when it has workers: bit-identical results whatever the count);
//   3. world -> scene: poses, velocities and sleep state of every moving body;
//   4. events: OnCollisionEnter3D / Stay / Exit and OnTriggerEnter3D / Stay / Exit from the world's touching pairs.
// Written for this engine; see docs/physics/physics_core.md for what each part of the core was written from.

namespace okay {

namespace {

phys::V3 P3(const Vec3& v) { return {v.x, v.y, v.z}; }
Vec3 E3(const phys::V3& v) { return {v.x, v.y, v.z}; }
phys::Q PQ(const Quat& q) { return phys::NormalizeQ(phys::Q{q.x, q.y, q.z, q.w}); }
Quat EQ(const phys::Q& q) { return Quat{q.x, q.y, q.z, q.w}; }

// The rotation that turns the core's long axis (+Y) into collider axis 0 / 1 / 2.
phys::Q AxisRot(int axis) {
    const float h = 1.57079632679f;
    if (axis == 0) return phys::Q::AxisAngle(phys::V3{0, 0, 1}, -h);
    if (axis == 2) return phys::Q::AxisAngle(phys::V3{1, 0, 0}, h);
    return phys::Q{};
}

struct EnginePiece { phys::Shape shape; phys::Xf world; };

float MaxAbs(float a, float b) { return std::max(std::fabs(a), std::fabs(b)); }

// A LocalShape3D sitting in frame f (centre, rotation, scale) as a core shape with its world pose.
void PieceOfLocal(const LocalShape3D& s, const shape3d::Frame& f, std::vector<EnginePiece>& out) {
    const Vec3 sc{std::fabs(f.s.x), std::fabs(f.s.y), std::fabs(f.s.z)};
    const phys::Xf base{P3(f.c), PQ(f.r)};
    const int ax = s.axis < 0 || s.axis > 2 ? 1 : s.axis;
    const float along = ax == 0 ? sc.x : ax == 2 ? sc.z : sc.y;
    const float perp = ax == 0 ? MaxAbs(sc.y, sc.z) : ax == 2 ? MaxAbs(sc.x, sc.y) : MaxAbs(sc.x, sc.z);
    const phys::Xf axial{base.p, phys::NormalizeQ(base.q * AxisRot(ax))};
    using K = LocalShape3D::Kind;
    switch (s.kind) {
        case K::Box: out.push_back({phys::Shape::Box(phys::V3{s.half.x * sc.x, s.half.y * sc.y, s.half.z * sc.z}), base}); break;
        case K::Sphere: out.push_back({phys::Shape::Sphere(s.radius * std::max({sc.x, sc.y, sc.z})), base}); break;
        case K::Capsule: {
            const float r = s.radius * perp;
            out.push_back({phys::Shape::Capsule(r, std::max(0.0f, s.height * along * 0.5f - r)), axial});
            break;
        }
        case K::Cylinder: out.push_back({phys::Shape::Cylinder(s.radius * perp, s.height * along * 0.5f), axial}); break;
        case K::Cone: out.push_back({phys::Shape::Cone(s.radius * perp, s.height * along * 0.5f), axial}); break;
        case K::Hull: {
            std::vector<phys::V3> pts;
            pts.reserve(s.pts.size());
            for (const Vec3& p : s.pts) pts.push_back(phys::V3{p.x * sc.x, p.y * sc.y, p.z * sc.z});
            out.push_back({phys::Shape::Hull(pts), base});
            break;
        }
    }
}

// Triangles of a static mesh collider, in world space (the shape frame is the world: its body sits at the origin).
std::shared_ptr<phys::TriangleSource> MeshSourceOf(MeshCollider3D* mc) {
    if (!mc->HasTriangles()) return nullptr;
    const TriMesh& tm = mc->Tris();
    const Mat4 M = mc->WorldMatrix();
    std::vector<phys::Tri3> tris;
    tris.reserve((std::size_t)tm.TriangleCount());
    for (int i = 0; i < tm.TriangleCount(); ++i) {
        Vec3 a, b, c;
        tm.Triangle(i, a, b, c);
        tris.push_back({P3(M.MultiplyPoint(a)), P3(M.MultiplyPoint(b)), P3(M.MultiplyPoint(c))});
    }
    return std::make_shared<phys::TriMeshSource>(std::move(tris));
}

// The pieces a collider is made of, in world space. Returns false when the collider has no shape the engine can use.
bool PiecesOfCollider(Collider3D* c, bool dynamicBody, std::vector<EnginePiece>& out) {
    using S = Collider3D::Shape;
    const Quat rot = c->transform ? c->transform->Rotation() : Quat::Identity;
    switch (c->shape()) {
        case S::Box: {
            const Vec3 h = static_cast<BoxCollider3D*>(c)->HalfExtents();
            out.push_back({phys::Shape::Box(phys::V3{std::max(h.x, 1e-4f), std::max(h.y, 1e-4f), std::max(h.z, 1e-4f)}),
                           phys::Xf{P3(c->WorldCenter()), PQ(rot)}});
            return true;
        }
        case S::Sphere:
            out.push_back({phys::Shape::Sphere(std::max(static_cast<SphereCollider3D*>(c)->WorldRadius(), 1e-4f)),
                           phys::Xf{P3(c->WorldCenter()), PQ(rot)}});
            return true;
        case S::Capsule: {
            auto* cc = static_cast<CapsuleCollider3D*>(c);
            out.push_back({phys::Shape::Capsule(std::max(cc->WorldRadius(), 1e-4f), cc->SegmentHalf()),
                           phys::Xf{P3(c->WorldCenter()), phys::NormalizeQ(PQ(rot) * AxisRot(cc->axis))}});
            return true;
        }
        case S::Cylinder: PieceOfLocal(shape3d::CylinderShape(*static_cast<CylinderCollider3D*>(c)), shape3d::FrameOf(*c), out); return true;
        case S::Cone:
        case S::ConvexHull: PieceOfLocal(static_cast<ShapeCollider3D*>(c)->Local(), shape3d::FrameOf(*c), out); return true;
        case S::Compound: {
            auto* cc = static_cast<CompoundCollider3D*>(c);
            for (int i = 0; i < cc->PartCount(); ++i) PieceOfLocal(cc->Parts()[(std::size_t)i].shape, cc->PartFrame(i), out);
            return !out.empty();
        }
        case S::Plane: {
            const Vec3 p = c->transform ? c->transform->Position() : Vec3::Zero;
            out.push_back({phys::Shape::Plane(phys::V3{0, 1, 0}, 0.0f), phys::Xf{P3(p), PQ(rot)}});
            return true;
        }
        case S::Heightfield: {
            auto* hf = static_cast<HeightfieldCollider3D*>(c);
            const shape3d::Frame f = shape3d::FrameOf(*c);
            const int C = hf->Cols(), R = hf->Rows();
            const float cx = hf->CX() * std::fabs(f.s.x), cz = hf->CZ() * std::fabs(f.s.z), sy = f.s.y;
            std::vector<float> h((std::size_t)C * (std::size_t)R);
            for (int j = 0; j < R; ++j)
                for (int i = 0; i < C; ++i) h[(std::size_t)j * (std::size_t)C + (std::size_t)i] = hf->HeightAt(i, j) * sy;
            out.push_back({phys::Shape::Mesh(std::make_shared<phys::HeightGridSource>(C, R, -(float)(C - 1) * 0.5f * cx,
                                                                                     -(float)(R - 1) * 0.5f * cz, cx, cz, std::move(h))),
                           phys::Xf{P3(f.c), PQ(f.r)}});
            return true;
        }
        case S::Mesh: {
            auto* mc = static_cast<MeshCollider3D*>(c);
            if (dynamicBody) {   // a moving mesh collides as its convex hull (as in the other qualities)
                if (!mc->HasTriangles()) return false;
                const Mat4 M = mc->WorldMatrix();
                const Mesh& hull = mc->Hull();
                if (hull.vertices.size() < 4) return false;
                std::vector<phys::V3> pts;
                const Vec3 ctr = c->transform ? c->transform->Position() : Vec3::Zero;
                for (const Vec3& v : hull.vertices) {
                    const Vec3 w = M.MultiplyPoint(v) - ctr;
                    pts.push_back(P3(rot.Inverse() * w));
                }
                out.push_back({phys::Shape::Hull(pts), phys::Xf{P3(ctr), PQ(rot)}});
                return true;
            }
            auto src = MeshSourceOf(mc);
            if (!src) return false;
            out.push_back({phys::Shape::Mesh(src), phys::Xf{}});
            return true;
        }
    }
    return false;
}

// A terrain as a height grid (its world matrix's translation and rotation place it; its scale is baked in).
bool PieceOfTerrain(Terrain* t, EnginePiece& out) {
    if (!t->transform || t->heights.empty()) return false;
    const int D = t->Dim();
    const Vec3 s = t->transform->LossyScale();
    const float cell = t->CellSize(), half = t->size * 0.5f;
    std::vector<float> h((std::size_t)D * (std::size_t)D);
    for (int z = 0; z < D; ++z)
        for (int x = 0; x < D; ++x) h[(std::size_t)z * (std::size_t)D + (std::size_t)x] = t->GetHeight(x, z) * s.y;
    const float sx = std::fabs(s.x), sz = std::fabs(s.z);
    out.shape = phys::Shape::Mesh(std::make_shared<phys::HeightGridSource>(D, D, -half * sx, -half * sz, cell * sx, cell * sz, std::move(h)));
    out.world = phys::Xf{P3(t->transform->Position()), PQ(t->transform->Rotation())};
    return true;
}

std::uint64_t Mix(std::uint64_t h, const void* p, std::size_t n) {
    const unsigned char* c = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ULL; }
    return h;
}
template <class T> std::uint64_t MixV(std::uint64_t h, const T& v) { return Mix(h, &v, sizeof(T)); }
// Big height data: every byte up to 1 MB, beyond that 65,536 evenly spread samples (a 2049 x 2049 terrain would
// otherwise cost 16 MB of hashing every step; an edit that touches none of the samples is then picked up late).
std::uint64_t MixBig(std::uint64_t h, const void* p, std::size_t n) {
    h = MixV(h, n);
    if (n <= (1u << 20)) return Mix(h, p, n);
    const unsigned char* c = static_cast<const unsigned char*>(p);
    const std::size_t stride = n / 65536;
    for (std::size_t i = 0; i < n; i += stride) h = Mix(h, c + i, 1);
    return h;
}

// What a collider looks like (shape kind and sizes, scale, offset, trigger, layer, material), without building it.
std::uint64_t ColliderSig(Collider3D* c, const Rigidbody3D* body) {
    using S = Collider3D::Shape;
    std::uint64_t h = 1469598103934665603ULL;
    h = MixV(h, (int)c->shape());
    h = MixV(h, c->offset); h = MixV(h, c->isTrigger); h = MixV(h, c->layer);
    h = MixV(h, c->friction); h = MixV(h, c->bounciness);
    if (c->transform) h = MixV(h, c->transform->LossyScale());
    if (body) { h = MixV(h, body->friction); h = MixV(h, body->bounciness); h = MixV(h, body->rollingResistance); h = MixV(h, (int)body->bodyType); }
    switch (c->shape()) {
        case S::Box: h = MixV(h, static_cast<BoxCollider3D*>(c)->size); break;
        case S::Sphere: h = MixV(h, static_cast<SphereCollider3D*>(c)->radius); break;
        case S::Capsule:
        case S::Cylinder: {
            auto* cc = static_cast<CapsuleCollider3D*>(c);
            h = MixV(h, cc->radius); h = MixV(h, cc->height); h = MixV(h, cc->axis);
            break;
        }
        case S::Cone: {
            auto* cc = static_cast<ConeCollider3D*>(c);
            h = MixV(h, cc->radius); h = MixV(h, cc->height); h = MixV(h, cc->axis);
            break;
        }
        case S::ConvexHull: { const std::string& p = static_cast<ConvexHullCollider3D*>(c)->points; h = Mix(h, p.data(), p.size()); break; }
        case S::Compound:
            for (const auto& part : static_cast<CompoundCollider3D*>(c)->Parts()) {
                h = MixV(h, (int)part.shape.kind); h = MixV(h, part.shape.half); h = MixV(h, part.shape.radius);
                h = MixV(h, part.shape.height); h = MixV(h, part.shape.axis); h = MixV(h, part.pos); h = MixV(h, part.rot);
            }
            break;
        case S::Heightfield: {
            auto* hf = static_cast<HeightfieldCollider3D*>(c);
            h = MixV(h, hf->Cols()); h = MixV(h, hf->Rows()); h = MixV(h, hf->CX()); h = MixV(h, hf->CZ()); h = MixV(h, hf->heightScale);
            h = MixBig(h, hf->heights.data(), hf->heights.size());
            break;
        }
        case S::Mesh: {
            auto* mc = static_cast<MeshCollider3D*>(c);
            h = MixV(h, mc->BuildCount());
            const Mat4 M = mc->WorldMatrix();
            h = Mix(h, &M, sizeof(Mat4));
            break;
        }
        case S::Plane: break;
    }
    return h;
}

std::uint64_t TerrainSig(Terrain* t) {
    std::uint64_t h = 1469598103934665603ULL;
    h = MixV(h, t->resolution); h = MixV(h, t->size);
    if (t->transform) h = MixV(h, t->transform->LossyScale());
    if (!t->heights.empty()) h = MixBig(h, t->heights.data(), t->heights.size() * sizeof(float));
    return h;
}

} // namespace

struct Physics3D::EngineBridge {
    phys::World world;
    struct BodyRec {
        phys::BodyId id = phys::kInvalidId;
        Vec3 pos{0, 0, 0}, vel{0, 0, 0}, ang{0, 0, 0};
        Quat rot = Quat::Identity;
        bool sleeping = false;
        bool seen = false;
        bool driven = false;                      // a kinematic body game code moved this step (its speed was made up)
    };
    struct ColRec {
        std::vector<phys::ColliderId> ids;
        phys::BodyId owner = phys::kInvalidId;    // the body the pieces sit on
        bool ownStatic = false;                   // owner is a static body made for this collider alone
        const Rigidbody3D* body = nullptr;
        std::uint64_t sig = 0;
        Vec3 pos{0, 0, 0};                        // static: where the collider was when placed; body: its offset in the body
        Quat rot = Quat::Identity;
        bool seen = false;
    };
    struct JointRec {
        phys::JointId id = phys::kInvalidId;
        std::uint64_t sig = 0;
        const GameObject* partner = nullptr;
        bool seen = false;
    };
    std::unordered_map<const Rigidbody3D*, BodyRec> bodies;
    std::unordered_map<const Collider3D*, ColRec> colliders;
    std::unordered_map<const Terrain*, ColRec> terrains;
    std::unordered_map<const Joint3D*, JointRec> joints;
    std::vector<Collider3D*> back;                        // ColliderId -> its component (nullptr: a terrain)
    std::vector<std::uint64_t> touching;                  // pair keys touching after the last step (sorted)
    std::unordered_map<std::uint64_t, std::pair<Collider3D*, Collider3D*>> touchingPair;

    void Unmap(const ColRec& r) {
        for (phys::ColliderId id : r.ids) { world.RemoveCollider(id); if (id < back.size()) back[id] = nullptr; }
        if (r.ownStatic) world.DestroyBody(r.owner);
    }
};

Physics3D::~Physics3D() = default;

void Physics3D::Clear() {
    m_contacts.clear(); m_meshContacts.clear(); m_restContacts.clear();
    m_solver.reset();   // its remembered pushes point at colliders about to be freed
    m_engine.reset();   // (Engine quality: the world holds bodies for components about to be freed)
}

phys::World* Physics3D::EngineWorld() { return m_engine ? &m_engine->world : nullptr; }
std::uint32_t Physics3D::EngineBody(const Rigidbody3D* rb) const {
    if (!m_engine) return phys::kInvalidId;
    auto it = m_engine->bodies.find(rb);
    return it == m_engine->bodies.end() ? phys::kInvalidId : it->second.id;
}
std::uint32_t Physics3D::EngineJoint(const Joint3D* j) const {
    if (!m_engine) return phys::kInvalidId;
    auto it = m_engine->joints.find(j);
    return it == m_engine->joints.end() ? phys::kInvalidId : it->second.id;
}

void Physics3D::StepEngine(Scene& scene, float dt) {
    MeshCollider3D::AdvanceEpoch();
    if (!m_engine) m_engine = std::make_unique<EngineBridge>();
    EngineBridge& E = *m_engine;
    phys::World& W = E.world;
    m_stats = PhysicsStepStats{};
    W.gravity = P3(gravity);
    W.velocityIterations = std::min(std::max(solverIterations, 4), 30);
    W.positionIterations = 3;
    W.shouldCollide = [this, &E](phys::ColliderId a, phys::ColliderId b) {
        Collider3D* ca = a < E.back.size() ? E.back[a] : nullptr;
        Collider3D* cb = b < E.back.size() ? E.back[b] : nullptr;
        if (!ca || !cb) return true;   // a terrain collides with everything solid
        if (!LayersCollide(ca->layer, cb->layer)) return false;
        if ((ca->isTrigger || cb->isTrigger) && (!TriggerAccepts(ca, cb) || !TriggerAccepts(cb, ca))) return false;
        return true;
    };
    JobSystem& jobs = EngineJobs();
    if (jobs.WorkerCount() > 0) {
        W.SetParallelFor([&jobs](int count, const std::function<void(int, int)>& fn) {
            jobs.ParallelFor(0, (std::size_t)count, 8, [&fn](std::size_t b, std::size_t e) { fn((int)b, (int)e); });
        });
    } else {
        W.SetParallelFor(nullptr);
    }

    // ---- 1a. bodies ----
    for (auto& kv : E.bodies) kv.second.seen = false;
    const std::vector<Rigidbody3D*> rbs = CachedComponents<Rigidbody3D>(scene);
    for (Rigidbody3D* rb : rbs) {
        if (!rb->enabled || !rb->gameObject || !rb->gameObject->ActiveInHierarchy() || !rb->transform) continue;
        Transform* t = rb->transform;
        auto it = E.bodies.find(rb);
        const phys::BodyType type = rb->bodyType == Rigidbody3D::BodyType::Dynamic ? phys::BodyType::Dynamic
                                  : rb->bodyType == Rigidbody3D::BodyType::Kinematic ? phys::BodyType::Kinematic
                                  : phys::BodyType::Static;
        const Vec3 pos = t->Position();
        const Quat rot = t->Rotation();
        if (it == E.bodies.end()) {
            phys::BodyDef bd;
            bd.type = type;
            bd.position = P3(pos);
            bd.rotation = PQ(rot);
            bd.linearVelocity = P3(rb->velocity);
            bd.angularVelocity = P3(rb->angularVelocity);
            bd.awake = !rb->sleeping;
            EngineBridge::BodyRec r;
            r.id = W.CreateBody(bd);
            r.pos = pos; r.rot = rot; r.vel = rb->velocity; r.ang = rb->angularVelocity; r.sleeping = rb->sleeping;
            it = E.bodies.emplace(rb, r).first;
        }
        EngineBridge::BodyRec& r = it->second;
        r.seen = true;
        const phys::BodyId id = r.id;
        if (W.GetType(id) != type) W.SetType(id, type);
        // settings (cheap setters; they take effect from this step)
        W.SetDamping(id, std::max(rb->drag, 0.0f), std::max(rb->angularDrag, 0.0f));
        W.SetGravityScale(id, rb->gravityScale);
        W.SetLocks(id, (std::uint8_t)((rb->freezeX ? 1 : 0) | (rb->freezeY ? 2 : 0) | (rb->freezeZ ? 4 : 0)), rb->freezeRotation ? 7 : 0);
        W.SetBullet(id, rb->continuousCollision);
        W.SetAllowSleep(id, rb->allowSleep);
        if (type == phys::BodyType::Dynamic && std::fabs(W.GetMass(id) - rb->mass) > 1e-6f * std::max(1.0f, rb->mass))
            W.SetMass(id, rb->mass > 0.0f ? rb->mass : 1.0f);
        // what game code changed since the last step
        const bool moved = !(pos == r.pos) || !(rot.x == r.rot.x && rot.y == r.rot.y && rot.z == r.rot.z && rot.w == r.rot.w);
        r.driven = false;
        if (moved && type == phys::BodyType::Kinematic && dt > 0.0f) {
            // a kinematic body moved by game code (a lift, a moving platform): give it the speed that gets it there this
            // step instead of teleporting it, so what stands on it is carried and what it hits is pushed
            const phys::Xf now = W.GetTransform(id);
            W.SetLinearVelocity(id, (P3(pos) - now.p) * (1.0f / dt));
            W.SetAngularVelocity(id, phys::ToRotationVector(phys::NormalizeQ(PQ(rot) * phys::Conj(now.q))) * (1.0f / dt));
            r.driven = true;
        } else {
            if (moved) W.SetTransform(id, P3(pos), PQ(rot));
            if (!(rb->velocity == r.vel)) W.SetLinearVelocity(id, P3(rb->velocity));
            if (!(rb->angularVelocity == r.ang)) W.SetAngularVelocity(id, P3(rb->angularVelocity));
        }
        if (r.sleeping && !rb->sleeping) W.Wake(id);
        const Vec3 f = rb->ConsumeForce(), tq = rb->ConsumeTorque();
        if (f.SqrMagnitude() > 0.0f) W.ApplyForceAtCenter(id, P3(f));
        if (tq.SqrMagnitude() > 0.0f && !rb->freezeRotation) W.ApplyTorque(id, P3(tq));
    }

    // ---- 1b. colliders (static ones get a static body of their own, so a moved one can be moved alone) ----
    if (E.back.size() < 64) E.back.resize(64, nullptr);
    for (auto& kv : E.colliders) kv.second.seen = false;
    const std::vector<Collider3D*> cols = CachedComponents<Collider3D>(scene);
    std::vector<EnginePiece> pieces;
    for (Collider3D* c : cols) {
        if (!Alive(c) || !c->transform) continue;
        Rigidbody3D* rb = BodyOfCollider(c);
        auto bit = rb ? E.bodies.find(rb) : E.bodies.end();
        if (rb && bit == E.bodies.end()) continue;   // its body is switched off: so is the collider
        const bool onBody = bit != E.bodies.end();
        const std::uint64_t sig = ColliderSig(c, onBody ? rb : nullptr);
        auto it = E.colliders.find(c);
        // a collider under a body is placed relative to it; a static one keeps its own world pose
        Vec3 relPos{0, 0, 0};
        Quat relRot = Quat::Identity;
        if (onBody) {
            const Quat br = rb->transform->Rotation();
            relPos = br.Inverse() * (c->transform->Position() - rb->transform->Position());
            relRot = br.Inverse() * c->transform->Rotation();
        }
        const bool fresh = it == E.colliders.end();
        bool rebuild = fresh || it->second.sig != sig || it->second.body != (onBody ? rb : nullptr);
        if (!rebuild && onBody && (!(it->second.pos == relPos) || !(it->second.rot.x == relRot.x && it->second.rot.y == relRot.y &&
                                                                   it->second.rot.z == relRot.z && it->second.rot.w == relRot.w)))
            rebuild = true;   // moved inside its body (a child collider)
        if (!rebuild) {
            it->second.seen = true;
            if (!onBody && it->second.ownStatic) {   // a static collider moved by game code: move its body
                const Vec3 p = c->transform->Position();
                const Quat q = c->transform->Rotation();
                if (!(p == it->second.pos) || !(q.x == it->second.rot.x && q.y == it->second.rot.y && q.z == it->second.rot.z && q.w == it->second.rot.w)) {
                    // its pieces were placed relative to the old pose: the world pose of the body follows the transform
                    const phys::Xf oldX{P3(it->second.pos), PQ(it->second.rot)};
                    const phys::Xf newX{P3(p), PQ(q)};
                    const phys::Xf bodyX = W.GetTransform(it->second.owner);
                    // body pose = new transform * (old transform)^-1 * old body pose
                    const phys::Xf invOld{phys::InvRotate(oldX.q, -oldX.p), phys::Conj(oldX.q)};
                    const phys::Xf moved = newX * (invOld * bodyX);
                    W.SetTransform(it->second.owner, moved.p, moved.q);
                    it->second.pos = p;
                    it->second.rot = q;
                }
            }
            continue;
        }
        if (!fresh) { E.Unmap(it->second); E.colliders.erase(it); }
        pieces.clear();
        if (!PiecesOfCollider(c, onBody && rb->bodyType == Rigidbody3D::BodyType::Dynamic, pieces)) continue;
        EngineBridge::ColRec r;
        r.sig = sig;
        r.body = onBody ? rb : nullptr;
        r.seen = true;
        phys::Xf bodyX;
        if (onBody) {
            r.owner = bit->second.id;
            bodyX = W.GetTransform(r.owner);
            r.pos = relPos;
            r.rot = relRot;
        } else {
            phys::BodyDef bd;
            bd.type = phys::BodyType::Static;
            bd.position = P3(c->transform->Position());
            bd.rotation = PQ(c->transform->Rotation());
            r.owner = W.CreateBody(bd);
            r.ownStatic = true;
            bodyX = W.GetTransform(r.owner);
            r.pos = c->transform->Position();
            r.rot = c->transform->Rotation();
        }
        const phys::Xf invBody{phys::InvRotate(bodyX.q, -bodyX.p), phys::Conj(bodyX.q)};
        for (const EnginePiece& p : pieces) {
            phys::ColliderDef cd;
            cd.shape = p.shape;
            cd.local = invBody * p.world;
            cd.sensor = c->isTrigger;
            cd.layer = (std::uint32_t)(c->layer & 31);
            cd.mask = CollisionMask(c->layer);
            cd.friction = c->friction >= 0.0f ? c->friction : (rb ? rb->friction : 0.5f);
            cd.restitution = c->bounciness >= 0.0f ? c->bounciness : (rb ? rb->bounciness : 0.0f);
            cd.rollingResistance = rb ? std::min(std::max(rb->rollingResistance, 0.0f), 1.0f) : 0.0f;
            const phys::ColliderId id = W.AddCollider(r.owner, cd);
            if (id == phys::kInvalidId) continue;
            if (E.back.size() <= id) E.back.resize((std::size_t)id * 2 + 1, nullptr);
            E.back[id] = c;
            r.ids.push_back(id);
        }
        if (onBody && rb->bodyType == Rigidbody3D::BodyType::Dynamic && rb->mass > 0.0f) W.SetMass(r.owner, rb->mass);
        E.colliders.emplace(c, std::move(r));
    }
    // terrains
    for (auto& kv : E.terrains) kv.second.seen = false;
    for (Terrain* t : CachedComponents<Terrain>(scene)) {
        if (!t->enabled || !t->gameObject || !t->gameObject->ActiveInHierarchy() || !t->transform) continue;
        const std::uint64_t sig = TerrainSig(t);
        const Vec3 p = t->transform->Position();
        const Quat q = t->transform->Rotation();
        auto it = E.terrains.find(t);
        if (it != E.terrains.end() && it->second.sig == sig && it->second.pos == p &&
            it->second.rot.x == q.x && it->second.rot.y == q.y && it->second.rot.z == q.z && it->second.rot.w == q.w) {
            it->second.seen = true;
            continue;
        }
        if (it != E.terrains.end()) { E.Unmap(it->second); E.terrains.erase(it); }
        EnginePiece piece;
        if (!PieceOfTerrain(t, piece)) continue;
        EngineBridge::ColRec r;
        r.sig = sig; r.seen = true; r.pos = p; r.rot = q; r.ownStatic = true;
        phys::BodyDef bd;
        bd.type = phys::BodyType::Static;
        bd.position = piece.world.p;
        bd.rotation = piece.world.q;
        r.owner = W.CreateBody(bd);
        phys::ColliderDef cd;
        cd.shape = piece.shape;
        cd.friction = 0.6f;
        const phys::ColliderId id = W.AddCollider(r.owner, cd);
        if (id != phys::kInvalidId) {
            if (E.back.size() <= id) E.back.resize((std::size_t)id * 2 + 1, nullptr);
            E.back[id] = nullptr;
            r.ids.push_back(id);
        }
        E.terrains.emplace(t, std::move(r));
    }

    // ---- 1c. joints ----
    for (auto& kv : E.joints) kv.second.seen = false;
    for (Joint3D* j : CachedComponents<Joint3D>(scene)) {
        if (!j->enabled || !j->gameObject || !j->gameObject->ActiveInHierarchy() || !j->transform || j->broken) continue;
        Rigidbody3D* ra = j->gameObject->GetComponent<Rigidbody3D>();
        auto bodyIt = ra ? E.bodies.find(ra) : E.bodies.end();
        if (bodyIt == E.bodies.end()) continue;   // the joint moves THIS body
        GameObject* partner = j->connectedBody.empty() ? nullptr : CachedPartner(scene, j);
        Rigidbody3D* rbB = partner ? partner->GetComponent<Rigidbody3D>() : nullptr;
        auto partnerIt = rbB ? E.bodies.find(rbB) : E.bodies.end();
        const phys::BodyId other = partnerIt != E.bodies.end() ? partnerIt->second.id : phys::kInvalidId;
        const Joint3D::Mode mode = (Joint3D::Mode)j->mode;
        std::uint64_t sig = 1469598103934665603ULL;
        sig = MixV(sig, j->mode); sig = MixV(sig, partner); sig = MixV(sig, other); sig = MixV(sig, j->axis);
        sig = MixV(sig, j->distance); sig = MixV(sig, j->autoConfigure); sig = MixV(sig, j->anchor);
        auto it = E.joints.find(j);
        if (it != E.joints.end() && it->second.sig == sig && W.IsValidJoint(it->second.id)) {
            it->second.seen = true;
        } else {
            if (it != E.joints.end()) { W.DestroyJoint(it->second.id); E.joints.erase(it); }
            // build the joint from the poses right now (the start pose)
            const phys::Xf xa = W.GetTransform(bodyIt->second.id);
            const phys::Xf xo = other != phys::kInvalidId ? W.GetTransform(other) : phys::Xf{};
            const Vec3 pa = j->transform->Position();
            const Vec3 pb = partner && partner->transform ? partner->transform->Position() : j->anchor;
            auto toOther = [&](const phys::V3& w) { return other != phys::kInvalidId ? xo.ApplyInv(w) : w; };
            auto rotOther = [&](const phys::Q& w) { return other != phys::kInvalidId ? phys::NormalizeQ(phys::Conj(xo.q) * w) : w; };
            phys::JointDef d;
            d.bodyA = other;                       // the core's A is the partner (or the world), B is this body
            d.bodyB = bodyIt->second.id;
            d.collideConnected = false;
            // a joint frame whose X axis is `axis` (hinge, slider, twist)
            const phys::V3 ax = phys::Normalize(P3(j->axis), phys::V3{0, 0, 1});
            phys::V3 t1, t2;
            phys::Basis(ax, t1, t2);
            phys::M33 fr;
            for (int k = 0; k < 3; ++k) { fr.m[k][0] = ax[k]; fr.m[k][1] = t1[k]; fr.m[k][2] = t2[k]; }
            // quaternion of the frame [ax t1 t2] (rotation taking X, Y, Z to them)
            auto quatOf = [](const phys::M33& m) {
                const float tr = m.m[0][0] + m.m[1][1] + m.m[2][2];
                phys::Q q;
                if (tr > 0.0f) {
                    const float s = std::sqrt(tr + 1.0f) * 2.0f;
                    q = phys::Q{(m.m[2][1] - m.m[1][2]) / s, (m.m[0][2] - m.m[2][0]) / s, (m.m[1][0] - m.m[0][1]) / s, 0.25f * s};
                } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
                    const float s = std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2.0f;
                    q = phys::Q{0.25f * s, (m.m[0][1] + m.m[1][0]) / s, (m.m[0][2] + m.m[2][0]) / s, (m.m[2][1] - m.m[1][2]) / s};
                } else if (m.m[1][1] > m.m[2][2]) {
                    const float s = std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2.0f;
                    q = phys::Q{(m.m[0][1] + m.m[1][0]) / s, 0.25f * s, (m.m[1][2] + m.m[2][1]) / s, (m.m[0][2] - m.m[2][0]) / s};
                } else {
                    const float s = std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2.0f;
                    q = phys::Q{(m.m[0][2] + m.m[2][0]) / s, (m.m[1][2] + m.m[2][1]) / s, 0.25f * s, (m.m[1][0] - m.m[0][1]) / s};
                }
                return phys::NormalizeQ(q);
            };
            const phys::Q frameW = quatOf(fr);
            phys::V3 pivot = P3(pb);   // the pivot of a hinge / ball / slider: the partner's origin or the anchor
            switch (mode) {
                case Joint3D::Mode::Distance:
                case Joint3D::Mode::Spring: {
                    d.type = phys::JointType::Distance;
                    const float rest = j->autoConfigure ? Vec3::Distance(pa, pb) : j->distance;
                    d.frameA.p = toOther(P3(pb));
                    d.frameB.p = xa.ApplyInv(P3(pa));
                    d.minLength = d.maxLength = std::max(rest, 0.0f);
                    if (mode == Joint3D::Mode::Spring) { d.minLength = 0.0f; d.distanceStiffness = std::max(j->spring, 0.0f); d.distanceDamper = std::max(j->damper, 0.0f); }
                    break;
                }
                case Joint3D::Mode::Pin:
                    d.type = phys::JointType::Fixed;
                    d.frameA = phys::Xf{toOther(xa.p), rotOther(xa.q)};
                    d.frameB = phys::Xf{};
                    break;
                case Joint3D::Mode::Hinge:
                case Joint3D::Mode::Ball:
                case Joint3D::Mode::ConeTwist:
                    if (mode != Joint3D::Mode::Hinge) pivot = P3(j->anchor);   // ball and cone twist: `anchor` is the world pivot
                    d.type = mode == Joint3D::Mode::Hinge ? phys::JointType::Hinge : mode == Joint3D::Mode::Ball ? phys::JointType::Ball : phys::JointType::ConeTwist;
                    d.frameA = phys::Xf{toOther(pivot), rotOther(frameW)};
                    d.frameB = phys::Xf{xa.ApplyInv(pivot), phys::NormalizeQ(phys::Conj(xa.q) * frameW)};
                    break;
                case Joint3D::Mode::Slider:
                    d.type = phys::JointType::Slider;
                    pivot = P3(pa);
                    d.frameA = phys::Xf{toOther(pivot), rotOther(frameW)};
                    d.frameB = phys::Xf{phys::V3{}, phys::NormalizeQ(phys::Conj(xa.q) * frameW)};
                    break;
            }
            phys::SetupJoint(d);
            EngineBridge::JointRec r;
            r.id = W.CreateJoint(d);
            r.sig = sig;
            r.partner = partner;
            r.seen = true;
            if (r.id == phys::kInvalidId) continue;
            j->initialized = true;
            it = E.joints.emplace(j, r).first;
        }
        // limits, motors, springs and drives: edited in place every step (cheap; scripts change them while playing)
        phys::JointDef& d = W.EditJoint(it->second.id);
        const float deg = 3.14159265f / 180.0f;
        switch (mode) {
            case Joint3D::Mode::Spring:
                d.distanceStiffness = std::max(j->spring, 0.0f); d.distanceDamper = std::max(j->damper, 0.0f);
                break;
            case Joint3D::Mode::Hinge:
                d.angular[0].mode = j->useLimits ? phys::AxisMode::Limited : phys::AxisMode::Free;
                d.angular[0].lower = std::min(j->minAngle, j->maxAngle) * deg;
                d.angular[0].upper = std::max(j->minAngle, j->maxAngle) * deg;
                d.angular[0].motor = j->useMotor;
                d.angular[0].motorSpeed = j->motorSpeed * deg;
                d.angular[0].motorMaxForce = std::max(j->maxMotorTorque, 0.0f);
                break;
            case Joint3D::Mode::Slider:
                d.linear[0].mode = j->useLimits ? phys::AxisMode::Limited : phys::AxisMode::Free;
                d.linear[0].lower = std::min(j->minAngle, j->maxAngle);
                d.linear[0].upper = std::max(j->minAngle, j->maxAngle);
                d.linear[0].motor = j->useMotor;
                d.linear[0].motorSpeed = j->motorSpeed;
                d.linear[0].motorMaxForce = std::max(j->maxMotorTorque, 0.0f);
                break;
            case Joint3D::Mode::Ball:
            case Joint3D::Mode::ConeTwist:
                if (mode == Joint3D::Mode::ConeTwist) {
                    d.swingSpan1 = std::max(j->swingLimit1, 0.5f) * deg;
                    d.swingSpan2 = std::max(j->swingLimit2, 0.5f) * deg;
                    d.angular[0].mode = j->useLimits ? phys::AxisMode::Limited : phys::AxisMode::Free;
                    d.angular[0].lower = std::min(j->minAngle, j->maxAngle) * deg;
                    d.angular[0].upper = std::max(j->minAngle, j->maxAngle) * deg;
                }
                d.drive = j->useDrive;
                d.driveTarget = PQ(Quat::Euler(j->driveTarget.x, j->driveTarget.y, j->driveTarget.z));
                d.driveStiffness = std::max(j->driveStiffness, 0.0f);
                d.driveDamping = std::max(j->driveDamping, 0.0f);
                d.driveFrequency = std::max(j->driveFrequency, 0.0f);
                d.driveDampingRatio = std::max(j->driveDampingRatio, 0.0f);
                d.driveMaxTorque = std::max(j->driveMaxTorque, 0.0f);
                break;
            default: break;
        }
    }

    // ---- 1d. remove what is gone (in id order, so the world changes the same way every run) ----
    {
        std::vector<std::pair<phys::JointId, const Joint3D*>> deadJ;
        for (auto& kv : E.joints) if (!kv.second.seen) deadJ.emplace_back(kv.second.id, kv.first);
        std::sort(deadJ.begin(), deadJ.end());
        for (auto& d : deadJ) { W.DestroyJoint(d.first); E.joints.erase(d.second); }
        std::vector<std::pair<phys::ColliderId, const Collider3D*>> deadC;
        for (auto& kv : E.colliders) if (!kv.second.seen) deadC.emplace_back(kv.second.ids.empty() ? 0u : kv.second.ids[0], kv.first);
        std::sort(deadC.begin(), deadC.end());
        for (auto& d : deadC) { auto it = E.colliders.find(d.second); E.Unmap(it->second); E.colliders.erase(it); }
        std::vector<std::pair<phys::ColliderId, const Terrain*>> deadT;
        for (auto& kv : E.terrains) if (!kv.second.seen) deadT.emplace_back(kv.second.ids.empty() ? 0u : kv.second.ids[0], kv.first);
        std::sort(deadT.begin(), deadT.end());
        for (auto& d : deadT) { auto it = E.terrains.find(d.second); E.Unmap(it->second); E.terrains.erase(it); }
        std::vector<std::pair<phys::BodyId, const Rigidbody3D*>> deadB;
        for (auto& kv : E.bodies) if (!kv.second.seen) deadB.emplace_back(kv.second.id, kv.first);
        std::sort(deadB.begin(), deadB.end());
        for (auto& d : deadB) {
            // colliders still on it lose their body: rebuild them next step as static ones
            for (auto& kv : E.colliders) if (kv.second.body == d.second) kv.second.sig = 0;
            W.DestroyBody(d.first);
            E.bodies.erase(d.second);
        }
    }

    // ---- 2. step ----
    W.Step(dt);

    // ---- 3. world -> scene ----
    for (Rigidbody3D* rb : rbs) {
        auto it = E.bodies.find(rb);
        if (it == E.bodies.end()) continue;
        EngineBridge::BodyRec& r = it->second;
        const phys::BodyId id = r.id;
        const bool dynamic = rb->bodyType == Rigidbody3D::BodyType::Dynamic;
        if (rb->bodyType != Rigidbody3D::BodyType::Static) {
            const phys::Xf x = W.GetTransform(id);
            Transform* t = rb->transform;
            const Vec3 p = E3(x.p);
            const Quat q = EQ(x.q);
            if (!t->Parent()) { t->localPosition = p; t->localRotation = q; }
            else { t->SetPosition(p); t->SetRotation(q); }
            if (r.driven) {   // (the made-up speed was for this step only: the body keeps the speed game code gave it)
                W.SetLinearVelocity(id, P3(rb->velocity));
                W.SetAngularVelocity(id, P3(rb->angularVelocity));
            }
            Vec3 v = E3(W.GetLinearVelocity(id));
            if (dynamic && rb->maxFallSpeed > 0.0f && v.y < -rb->maxFallSpeed) { v.y = -rb->maxFallSpeed; W.SetLinearVelocity(id, P3(v)); }
            rb->velocity = v;
            rb->angularVelocity = E3(W.GetAngularVelocity(id));
            rb->sleeping = dynamic && !W.IsAwake(id);
            r.pos = t->Position();
            r.rot = t->Rotation();
        } else {
            r.pos = rb->transform->Position();
            r.rot = rb->transform->Rotation();
        }
        r.vel = rb->velocity;
        r.ang = rb->angularVelocity;
        r.sleeping = rb->sleeping;
        if (dynamic) {
            ++m_stats.bodies;
            if (rb->sleeping) ++m_stats.asleep; else ++m_stats.awake;
        }
    }
    // breakable joints break when their two anchors drift apart by more than breakForce (the Joint3D meaning)
    for (auto& kv : E.joints) {
        Joint3D* j = const_cast<Joint3D*>(kv.first);
        if (!j->breakable || j->broken) continue;
        const phys::JointDef& d = W.GetJoint(kv.second.id);
        const phys::V3 a = d.bodyA != phys::kInvalidId ? W.GetTransform(d.bodyA).Apply(d.frameA.p) : d.frameA.p;
        const phys::V3 b = W.GetTransform(d.bodyB).Apply(d.frameB.p);
        float stretch = phys::Len(b - a);
        if (d.type == phys::JointType::Distance) stretch = std::fabs(phys::Len(b - a) - d.maxLength);
        const float force = phys::Len(W.JointForce(kv.second.id));
        if (stretch > j->breakForce || (d.type != phys::JointType::Distance && force > j->breakForce * 1000.0f)) j->broken = true;
    }
    const phys::WorldStats& ws = W.Stats();
    m_stats.contacts = ws.points;
    m_stats.manifolds = ws.touching;
    m_stats.islands = ws.islands;
    m_stats.warmStarted = ws.warmPoints;

    // ---- 4. events ----
    Scene::RemovalWatch gone(scene);
    std::vector<std::uint64_t> now;
    std::vector<phys::ContactEvent> touchingNow;
    W.ForEachTouching([&](const phys::ContactEvent& e) { touchingNow.push_back(e); });
    for (const phys::ContactEvent& e : touchingNow) {
        Collider3D* a = e.a < E.back.size() ? E.back[e.a] : nullptr;
        Collider3D* b = e.b < E.back.size() ? E.back[e.b] : nullptr;
        if (!a || !b || a == b) continue;   // terrain contacts raise no callbacks (as before)
        const std::uint64_t key = ((std::uint64_t)e.a << 32) | e.b;
        // a compound collider has several pieces: report each collider pair once
        bool dup = false;
        for (std::uint64_t k : now) {
            const auto& pr = E.touchingPair[k];
            if ((pr.first == a && pr.second == b) || (pr.first == b && pr.second == a)) { dup = true; break; }
        }
        if (dup) continue;
        now.push_back(key);
        E.touchingPair[key] = {a, b};
    }
    std::sort(now.begin(), now.end());
    auto wasTouching = [&](std::uint64_t key) { return std::binary_search(E.touching.begin(), E.touching.end(), key); };
    for (const phys::ContactEvent& e : touchingNow) {
        const std::uint64_t key = ((std::uint64_t)e.a << 32) | e.b;
        if (!std::binary_search(now.begin(), now.end(), key)) continue;
        Collider3D* a = E.back[e.a];
        Collider3D* b = E.back[e.b];
        if (gone.Gone(a) || gone.Gone(b)) continue;
        const Vec3 n = E3(e.normal), pt = E3(e.point);
        const bool was = wasTouching(key);
        if (PhysicsDebug::Collect() && (int)m_debugContacts.size() < PhysicsDebug::kMaxContacts) m_debugContacts.push_back({pt, n, e.sensor});
        if (e.sensor) {
            auto fn = was ? &Component::OnTriggerStay3D : &Component::OnTriggerEnter3D;
            DispatchTrigger(a, fn, b, pt, n, gone);
            if (!gone.Gone(a) && !gone.Gone(b)) DispatchTrigger(b, fn, a, pt, -n, gone);
        } else {
            phys::Manifold man;
            float pen = 0.0f;
            if (W.GetManifold(e.a, e.b, man)) for (int k = 0; k < man.count; ++k) pen = std::max(pen, -man.pts[k].separation);
            const phys::BodyId ba = e.bodyA, bb = e.bodyB;
            auto velAt = [&](phys::BodyId id) {
                return W.GetLinearVelocity(id) + phys::Cross(W.GetAngularVelocity(id), P3(pt) - W.GetCenterOfMass(id));
            };
            const Vec3 relBA = E3(velAt(bb) - velAt(ba));
            Collision3D forA = MakeCollision(b, n, pen, pt, relBA, a);
            Collision3D forB = MakeCollision(a, -n, pen, pt, -relBA, b);
            if (!was) {   // the closing speed when it began (the solver has already stopped it)
                forA.impactSpeed = forB.impactSpeed = e.approachSpeed;
            }
            auto fn = was ? &Component::OnCollisionStay3D : &Component::OnCollisionEnter3D;
            DispatchCollision(a->gameObject, fn, forA, gone);
            if (!gone.Gone(a) && !gone.Gone(b)) DispatchCollision(b->gameObject, fn, forB, gone);
        }
    }
    for (std::uint64_t key : E.touching) {
        if (std::binary_search(now.begin(), now.end(), key)) continue;
        auto pit = E.touchingPair.find(key);
        if (pit == E.touchingPair.end()) continue;
        Collider3D* a = pit->second.first;
        Collider3D* b = pit->second.second;
        E.touchingPair.erase(pit);
        if (gone.Gone(a) || gone.Gone(b)) continue;
        if (E.colliders.find(a) == E.colliders.end() || E.colliders.find(b) == E.colliders.end()) continue;   // removed
        if (!Alive(a) || !Alive(b)) continue;
        if (a->isTrigger || b->isTrigger) {
            const Vec3 mid = (a->WorldCenter() + b->WorldCenter()) * 0.5f;
            const Vec3 n = (b->WorldCenter() - a->WorldCenter()).Normalized();
            DispatchTrigger(a, &Component::OnTriggerExit3D, b, mid, n, gone);
            if (!gone.Gone(a) && !gone.Gone(b)) DispatchTrigger(b, &Component::OnTriggerExit3D, a, mid, -n, gone);
        } else {
            Collision3D forA = MakeCollision(b, Vec3::Zero, 0.0f, Vec3::Zero, Vec3::Zero, a);
            Collision3D forB = MakeCollision(a, Vec3::Zero, 0.0f, Vec3::Zero, Vec3::Zero, b);
            DispatchCollision(a->gameObject, &Component::OnCollisionExit3D, forA, gone);
            if (!gone.Gone(a) && !gone.Gone(b)) DispatchCollision(b->gameObject, &Component::OnCollisionExit3D, forB, gone);
        }
    }
    E.touching = now;
    // forget pairs that are no longer touching (keys of pairs still touching are kept)
    for (auto it = E.touchingPair.begin(); it != E.touchingPair.end();) {
        if (!std::binary_search(now.begin(), now.end(), it->first)) it = E.touchingPair.erase(it);
        else ++it;
    }
}

} // namespace okay
