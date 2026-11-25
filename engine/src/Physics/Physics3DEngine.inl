// The 3D physics step: our own physics engine core (okay::phys::World, engine/include/okay/Physics/Core) runs
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
        case K::Hull: {   // the collider already wrapped its points (Mesh::ConvexHull): take its triangles as they are
            std::vector<phys::V3> pts;
            pts.reserve(s.pts.size());
            for (const Vec3& p : s.pts) pts.push_back(phys::V3{p.x * sc.x, p.y * sc.y, p.z * sc.z});
            out.push_back({phys::Shape::Hull(phys::HullData::FromTriangles(pts, s.tris)), base});
            break;
        }
    }
}

// One triangle tree per mesh asset (its triangles in the mesh's own space), shared by every collider that uses it; each
// collider sees it through its own scale (ScaledTriangleSource) and is placed by its position and rotation. The cache
// holds weak references: a tree goes when the last collider using it goes.
using MeshCache = std::unordered_map<std::uint64_t, std::weak_ptr<const phys::TriMeshSource>>;
std::shared_ptr<phys::TriangleSource> MeshSourceOf(MeshCollider3D* mc, MeshCache& cache) {
    if (!mc->HasTriangles()) return nullptr;
    const TriMesh& tm = mc->Tris();
    std::vector<phys::Tri3> tris;
    tris.reserve((std::size_t)tm.TriangleCount());
    for (int i = 0; i < tm.TriangleCount(); ++i) {
        Vec3 a, b, c;
        tm.Triangle(i, a, b, c);
        tris.push_back({P3(a), P3(b), P3(c)});
    }
    const std::uint64_t key = StringId::HashOf(std::string_view(reinterpret_cast<const char*>(tris.data()), tris.size() * sizeof(phys::Tri3)));
    std::shared_ptr<const phys::TriMeshSource> base = cache[key].lock();
    if (!base) {
        for (auto it = cache.begin(); it != cache.end();) {   // forget trees no collider uses any more
            if (it->second.expired()) it = cache.erase(it);
            else ++it;
        }
        base = std::make_shared<phys::TriMeshSource>(std::move(tris));
        cache[key] = base;
    }
    const Vec3 s = mc->transform ? mc->transform->LossyScale() : Vec3::One;
    return std::make_shared<phys::ScaledTriangleSource>(base, phys::V3{s.x, s.y, s.z});
}

// The pieces a collider is made of, in world space. Returns false when the collider has no shape the engine can use.
bool PiecesOfCollider(Collider3D* c, bool dynamicBody, std::vector<EnginePiece>& out, MeshCache& meshCache) {
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
            if (!mc->HasTriangles()) {   // no mesh (yet): it collides as its size box, as before
                const Vec3 h = mc->HalfExtents();
                out.push_back({phys::Shape::Box(phys::V3{std::max(h.x, 1e-4f), std::max(h.y, 1e-4f), std::max(h.z, 1e-4f)}),
                               phys::Xf{P3(c->WorldCenter()), PQ(rot)}});
                return true;
            }
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
                out.push_back({phys::Shape::Hull(phys::HullData::FromTriangles(pts, hull.triangles)), phys::Xf{P3(ctr), PQ(rot)}});
                return true;
            }
            auto src = MeshSourceOf(mc, meshCache);
            if (!src) return false;
            const Vec3 at = c->transform ? c->transform->Position() : Vec3::Zero;
            out.push_back({phys::Shape::Mesh(src), phys::Xf{P3(at), PQ(rot)}});
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

// Signatures: the bytes that describe a thing, hashed once with the engine's string hash (StringId::HashOf).
struct SigBytes {
    std::string b;
    std::uint64_t Done() const { return StringId::HashOf(b); }
};
SigBytes& Mix(SigBytes& h, const void* p, std::size_t n) { h.b.append(static_cast<const char*>(p), n); return h; }
template <class T> SigBytes& MixV(SigBytes& h, const T& v) { return Mix(h, &v, sizeof(T)); }
// A scale read back from a turning body's matrix wobbles in its last bits: compare it to 1 part in 100,000.
Vec3 Quantized(const Vec3& v) {
    auto q = [](float x) { return std::round(x * 100000.0f); };
    return {q(v.x), q(v.y), q(v.z)};
}
// Big height data: every byte up to 1 MB, beyond that 65,536 evenly spread samples (a 2049 x 2049 terrain would
// otherwise cost 16 MB of hashing every step; an edit that touches none of the samples is then picked up late).
SigBytes& MixBig(SigBytes& h, const void* p, std::size_t n) {
    MixV(h, n);
    if (n <= (1u << 20)) return Mix(h, p, n);
    const unsigned char* c = static_cast<const unsigned char*>(p);
    const std::size_t stride = n / 65536;
    for (std::size_t i = 0; i < n; i += stride) Mix(h, c + i, 1);
    return h;
}

// What a collider looks like (shape kind and sizes, scale, offset, trigger, layer, material), without building it.
std::uint64_t ColliderSig(Collider3D* c, const Rigidbody3D* body) {
    using S = Collider3D::Shape;
    SigBytes h;
    MixV(h, (int)c->shape());
    MixV(h, c->offset); MixV(h, c->isTrigger); MixV(h, c->layer);
    MixV(h, c->friction); MixV(h, c->bounciness); MixV(h, c->contactOffset);
    if (c->transform) MixV(h, Quantized(c->transform->LossyScale()));
    if (body) { MixV(h, body->friction); MixV(h, body->bounciness); MixV(h, body->rollingResistance); MixV(h, (int)body->bodyType); }
    switch (c->shape()) {
        case S::Box: MixV(h, static_cast<BoxCollider3D*>(c)->size); break;
        case S::Sphere: MixV(h, static_cast<SphereCollider3D*>(c)->radius); break;
        case S::Capsule:
        case S::Cylinder: {
            auto* cc = static_cast<CapsuleCollider3D*>(c);
            MixV(h, cc->radius); MixV(h, cc->height); MixV(h, cc->axis);
            break;
        }
        case S::Cone: {
            auto* cc = static_cast<ConeCollider3D*>(c);
            MixV(h, cc->radius); MixV(h, cc->height); MixV(h, cc->axis);
            break;
        }
        case S::ConvexHull: { const std::string& p = static_cast<ConvexHullCollider3D*>(c)->points; Mix(h, p.data(), p.size()); break; }
        case S::Compound:
            for (const auto& part : static_cast<CompoundCollider3D*>(c)->Parts()) {
                MixV(h, (int)part.shape.kind); MixV(h, part.shape.half); MixV(h, part.shape.radius);
                MixV(h, part.shape.height); MixV(h, part.shape.axis); MixV(h, part.pos); MixV(h, part.rot);
            }
            break;
        case S::Heightfield: {
            auto* hf = static_cast<HeightfieldCollider3D*>(c);
            MixV(h, hf->Cols()); MixV(h, hf->Rows()); MixV(h, hf->CX()); MixV(h, hf->CZ()); MixV(h, hf->heightScale);
            MixBig(h, hf->heights.data(), hf->heights.size());
            break;
        }
        case S::Mesh: {
            auto* mc = static_cast<MeshCollider3D*>(c);
            MixV(h, mc->BuildCount());   // (its pose is not part of it: a moved static mesh is moved, not rebuilt)
            MixV(h, mc->size);            // (the box it falls back to without a mesh)
            break;
        }
        case S::Plane: break;
    }
    return h.Done();
}

bool SameAxis(const phys::JointAxis& a, const phys::JointAxis& b) {
    return a.mode == b.mode && a.lower == b.lower && a.upper == b.upper && a.motor == b.motor && a.motorSpeed == b.motorSpeed &&
           a.motorMaxForce == b.motorMaxForce && a.springFrequency == b.springFrequency && a.springDamping == b.springDamping &&
           a.springTarget == b.springTarget;
}
// The settings the bridge edits every step (limits, motors, springs, cone, drive).
bool SameJointSettings(const phys::JointDef& a, const phys::JointDef& b) {
    for (int k = 0; k < 3; ++k)
        if (!SameAxis(a.linear[k], b.linear[k]) || !SameAxis(a.angular[k], b.angular[k])) return false;
    return a.swingSpan1 == b.swingSpan1 && a.swingSpan2 == b.swingSpan2 && a.distanceStiffness == b.distanceStiffness &&
           a.distanceDamper == b.distanceDamper && a.drive == b.drive && a.driveTarget == b.driveTarget &&
           a.driveStiffness == b.driveStiffness && a.driveDamping == b.driveDamping && a.driveFrequency == b.driveFrequency &&
           a.driveDampingRatio == b.driveDampingRatio && a.driveMaxTorque == b.driveMaxTorque;
}

std::uint64_t TerrainSig(Terrain* t) {
    SigBytes h;
    MixV(h, t->resolution); MixV(h, t->size);
    if (t->transform) MixV(h, Quantized(t->transform->LossyScale()));
    if (!t->heights.empty()) MixBig(h, t->heights.data(), t->heights.size() * sizeof(float));
    return h.Done();
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
        bool warnedBad = false;                   // a NaN / infinite pose or speed from game code was logged once
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
        float restLength = -1.0f;                 // Distance / Spring: the length it was built with (kept if rebuilt)
        Vec3 lastPartnerPos{0, 0, 0};             // where the partner last was (a joint that loses it holds there)
    };
    std::unordered_map<const Rigidbody3D*, BodyRec> bodies;
    std::unordered_map<const Collider3D*, ColRec> colliders;
    std::unordered_map<const Terrain*, ColRec> terrains;
    std::unordered_map<const Joint3D*, JointRec> joints;
    // ColliderId -> its component, by id slot; the full id is kept so a stale id never finds a newer collider
    std::vector<std::pair<phys::ColliderId, Collider3D*>> back;
    MeshCache meshCache;                                  // one triangle tree per mesh asset
    // collider pairs touching after the last step, as component pairs (a compound's pieces and a rebuilt collider are
    // still the same pair), in the order their callbacks were sent
    using CPair = std::pair<Collider3D*, Collider3D*>;
    struct CPairHash { std::size_t operator()(const CPair& p) const { return std::hash<const void*>()(p.first) * 31u ^ std::hash<const void*>()(p.second); } };
    std::vector<CPair> touching;
    std::unordered_set<CPair, CPairHash> touchingSet;
    // guests (Physics3D::EngineGuest, by GuestUser): engine code with bodies of its own in the world
    std::unordered_set<std::uint64_t> guests;
    std::shared_ptr<int> life = std::make_shared<int>(0);   // guests hold a weak reference: it expires with the world

    Physics3D::EngineGuest* GuestOf(phys::ColliderId id) const {
        if (guests.empty()) return nullptr;
        const std::uint64_t user = world.GetCollider(id).user;
        return user && guests.count(user) ? reinterpret_cast<Physics3D::EngineGuest*>((std::uintptr_t)user) : nullptr;
    }

    Collider3D* Back(phys::ColliderId id) const {
        const std::size_t i = phys::IdIndex(id);
        return i < back.size() && back[i].first == id ? back[i].second : nullptr;
    }
    bool Known(phys::ColliderId id) const {   // a live collider of ours (a terrain's maps to nullptr)
        const std::size_t i = phys::IdIndex(id);
        return i < back.size() && back[i].first == id;
    }
    void SetBack(phys::ColliderId id, Collider3D* c) {
        const std::size_t i = phys::IdIndex(id);
        if (back.size() <= i) back.resize(i * 2 + 1, {phys::kInvalidId, nullptr});
        back[i] = {id, c};
    }
    void Unmap(const ColRec& r) {
        for (phys::ColliderId id : r.ids) {   // (an id the world already dropped, with its body, is simply invalid now)
            world.RemoveCollider(id);
            const std::size_t i = phys::IdIndex(id);
            if (i < back.size() && back[i].first == id) back[i] = {phys::kInvalidId, nullptr};
        }
        if (r.ownStatic) world.DestroyBody(r.owner);
    }
};

Physics3D::~Physics3D() = default;

void Physics3D::Clear() {
    m_engine.reset();   // the world holds bodies for components about to be freed
}

void Physics3D::SetSleep(float linearSpeed, float angularSpeed, float seconds) {
    m_sleepSet = true;
    m_sleepLinear = std::max(linearSpeed, 0.0f);
    m_sleepAngular = std::max(angularSpeed, 0.0f);
    m_sleepTime = std::max(seconds, 0.0f);
}

phys::World* Physics3D::EngineWorld() { return m_engine ? &m_engine->world : nullptr; }
phys::World* Physics3D::JoinEngineWorld(EngineGuest* guest, std::weak_ptr<void>& alive) {
    if (!guest) return nullptr;
    if (!m_engine) m_engine = std::make_unique<EngineBridge>();
    m_engine->guests.insert(GuestUser(guest));
    alive = m_engine->life;
    return &m_engine->world;
}
void Physics3D::LeaveEngineWorld(EngineGuest* guest) {
    if (m_engine) m_engine->guests.erase(GuestUser(guest));
}
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
    if (m_sleepSet) {
        W.sleepLinear = m_sleepLinear;
        W.sleepAngular = m_sleepAngular;
        W.timeToSleep = std::max(m_sleepTime, 0.0f);
    }
    int iterations = solverIterations;
    for (std::uint64_t g : E.guests)   // a guest may need a stiffer solve while its bodies move (a ragdoll's light limbs)
        iterations = std::max(iterations, reinterpret_cast<const EngineGuest*>((std::uintptr_t)g)->GuestSolverIterations());
    W.velocityIterations = std::min(std::max(iterations, 4), 30);
    W.positionIterations = 3;
    W.shouldCollide = [this, &E](phys::ColliderId a, phys::ColliderId b) {
        Collider3D* ca = E.Back(a);
        Collider3D* cb = E.Back(b);
        if (!ca || !cb) {   // a terrain collides with everything solid; a guest's collider (a ragdoll limb) asks its guest
            if (ca || cb)
                if (EngineGuest* g = E.GuestOf(ca ? b : a)) return g->GuestCollides(ca ? ca : cb);
            return true;
        }
        if (!LayersCollide(ca->layer, cb->layer)) return false;
        return true;   // (a trigger's tag and layer filter is applied in the event pass: the filter can change while the
                       //  pair stays put, and a pair is only offered here when it starts to overlap)
    };
    // friction and bounce of a pair: each collider's own value (or its body's) and the stronger of the two combine rules
    // (PhysicsMaterial.hpp: Default = square root of the frictions, the larger bounce)
    // Materials of a pair: each collider's own friction / bounce (or its body's) joined by the stronger combine rule
    // (PhysicsMaterial.hpp; Default = square root of the frictions, the larger bounce); a Physics Material on either
    // side adds its still grip (times its slope grip) and its rolling friction, as the old step did.
    W.combineMaterials = [&E](phys::ColliderId a, phys::ColliderId b, phys::World::PairMaterial& pm) {
        Collider3D* ca = E.Back(a);
        Collider3D* cb = E.Back(b);
        if (!ca || !cb) return;   // a terrain: the default rule
        const phys::World& w = E.world;
        const float fa = w.GetCollider(a).friction, fb = w.GetCollider(b).friction;
        pm.friction = CombineSurface(fa, fb, ca->frictionCombine, cb->frictionCombine, true);
        pm.restitution = CombineSurface(w.GetCollider(a).restitution, w.GetCollider(b).restitution, ca->bounceCombine, cb->bounceCombine, false);
        const PhysicsMaterialData* ma = SurfaceOf(ca->gameObject);
        const PhysicsMaterialData* mb = SurfaceOf(cb->gameObject);
        if (ma || mb) {
            const float sa = ma ? ma->staticFriction * ma->slopeGrip : fa, sb = mb ? mb->staticFriction * mb->slopeGrip : fb;
            pm.staticFriction = CombineSurface(sa, sb, ca->frictionCombine, cb->frictionCombine, true);
            if (ma && ma->rollingFriction >= 0.0f) pm.rolling = std::max(ma->rollingFriction, mb && mb->rollingFriction >= 0.0f ? mb->rollingFriction : 0.0f);
            else if (mb && mb->rollingFriction >= 0.0f) pm.rolling = std::max(pm.rolling, mb->rollingFriction);
        }
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
        // what game code changed since the last step. A NaN or infinite pose, speed or force is refused (logged once):
        // only that body keeps its last good state, nothing else is touched by it.
        const bool goodPose = std::isfinite(pos.x + pos.y + pos.z) && std::isfinite(rot.x + rot.y + rot.z + rot.w);
        const bool goodVel = std::isfinite(rb->velocity.x + rb->velocity.y + rb->velocity.z) &&
                             std::isfinite(rb->angularVelocity.x + rb->angularVelocity.y + rb->angularVelocity.z);
        if ((!goodPose || !goodVel) && !r.warnedBad) {
            r.warnedBad = true;
            Log::Warning("Rigidbody3D on '", rb->gameObject->name, "': a NaN or infinite position, rotation or speed was set; "
                         "the physics keeps its last good state");
        }
        const bool moved = goodPose && (!(pos == r.pos) || !(rot.x == r.rot.x && rot.y == r.rot.y && rot.z == r.rot.z && rot.w == r.rot.w));
        r.driven = false;
        // a kinematic body that game code moved slower than this is driven (it carries and pushes); a bigger jump is a
        // teleport (no made-up speed that would shove what it lands next to)
        constexpr float kDriveMaxSpeed = 50.0f;
        const bool drive = moved && type == phys::BodyType::Kinematic && dt > 0.0f &&
                           (pos - E3(W.GetTransform(id).p)).Magnitude() <= kDriveMaxSpeed * dt;
        if (drive) {
            // a kinematic body moved by game code (a lift, a moving platform): give it the speed that gets it there this
            // step instead of teleporting it, so what stands on it is carried and what it hits is pushed
            const phys::Xf now = W.GetTransform(id);
            W.SetLinearVelocity(id, (P3(pos) - now.p) * (1.0f / dt));
            W.SetAngularVelocity(id, phys::ToRotationVector(phys::NormalizeQ(PQ(rot) * phys::Conj(now.q))) * (1.0f / dt));
            r.driven = true;
        } else {
            if (moved) W.SetTransform(id, P3(pos), PQ(rot));
            if (moved && type == phys::BodyType::Kinematic) { W.SetLinearVelocity(id, phys::V3{}); W.SetAngularVelocity(id, phys::V3{}); }
            if (goodVel && !(rb->velocity == r.vel)) W.SetLinearVelocity(id, P3(rb->velocity));
            if (goodVel && !(rb->angularVelocity == r.ang)) W.SetAngularVelocity(id, P3(rb->angularVelocity));
        }
        if (r.sleeping && !rb->sleeping) W.Wake(id);
        const Vec3 f = rb->ConsumeForce(), tq = rb->ConsumeTorque();
        if (f.SqrMagnitude() > 0.0f && std::isfinite(f.x + f.y + f.z)) W.ApplyForceAtCenter(id, P3(f));
        if (tq.SqrMagnitude() > 0.0f && std::isfinite(tq.x + tq.y + tq.z) && !rb->freezeRotation) W.ApplyTorque(id, P3(tq));
    }

    // ---- 1b. colliders (static ones get a static body of their own, so a moved one can be moved alone) ----
    for (auto& kv : E.colliders) kv.second.seen = false;
    const std::vector<Collider3D*> cols = CachedComponents<Collider3D>(scene);
    std::vector<EnginePiece> pieces;
    for (Collider3D* c : cols) {
        if (!Alive(c) || !c->transform) continue;
        Rigidbody3D* rb = BodyOfCollider(c);
        auto bit = rb ? E.bodies.find(rb) : E.bodies.end();
        if (rb && bit == E.bodies.end()) { rb = nullptr; bit = E.bodies.end(); }   // its body is switched off: a static collider
        const bool onBody = bit != E.bodies.end();
        const std::uint64_t sig = ColliderSig(c, onBody ? rb : nullptr);
        auto it = E.colliders.find(c);
        // a collider under a body is placed relative to it; a static one keeps its own world pose
        Vec3 relPos{0, 0, 0};
        Quat relRot = Quat::Identity;
        if (onBody && c->transform != rb->transform) {   // (a collider on the body's own object sits at the body origin)
            const Quat br = rb->transform->Rotation();
            relPos = br.Inverse() * (c->transform->Position() - rb->transform->Position());
            relRot = br.Inverse() * c->transform->Rotation();
        }
        const bool fresh = it == E.colliders.end();
        bool rebuild = fresh || it->second.sig != sig || it->second.body != (onBody ? rb : nullptr);
        if (!rebuild && onBody) {   // moved inside its body (a child collider); small rounding as the body turns is not a move
            const Quat& o = it->second.rot;
            const float dq = std::fabs(o.x - relRot.x) + std::fabs(o.y - relRot.y) + std::fabs(o.z - relRot.z) + std::fabs(o.w - relRot.w);
            if ((it->second.pos - relPos).SqrMagnitude() > 1e-8f || dq > 1e-5f) rebuild = true;
        }
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
        if (!PiecesOfCollider(c, onBody && rb->bodyType == Rigidbody3D::BodyType::Dynamic, pieces, E.meshCache)) continue;
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
            cd.contactOffset = std::max(c->contactOffset, 0.0f);
            const phys::ColliderId id = W.AddCollider(r.owner, cd);
            if (id == phys::kInvalidId) continue;
            E.SetBack(id, c);
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
            E.SetBack(id, nullptr);
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
        GameObject* partner = Joint3DPartner(scene, j);
        Rigidbody3D* rbB = partner ? partner->GetComponent<Rigidbody3D>() : nullptr;
        auto partnerIt = rbB ? E.bodies.find(rbB) : E.bodies.end();
        const phys::BodyId other = partnerIt != E.bodies.end() ? partnerIt->second.id : phys::kInvalidId;
        const Joint3D::Mode mode = (Joint3D::Mode)j->mode;
        SigBytes sigb;
        MixV(sigb, j->mode); MixV(sigb, partner); MixV(sigb, other); MixV(sigb, j->axis);
        MixV(sigb, j->distance); MixV(sigb, j->autoConfigure); MixV(sigb, j->anchor);
        const std::uint64_t sig = sigb.Done();
        auto it = E.joints.find(j);
        if (it != E.joints.end() && it->second.sig == sig && W.IsValidJoint(it->second.id)) {
            it->second.seen = true;
            if (partner && partner->transform) it->second.lastPartnerPos = partner->transform->Position();
        } else {
            // A joint that loses its partner (destroyed, or its body switched off) keeps holding where that partner last
            // was, at the length it had: it is not rebuilt around the world anchor at a new length.
            float keptLength = -1.0f;
            bool partnerLost = false;
            Vec3 lostAt{0, 0, 0};
            if (it != E.joints.end()) {
                partnerLost = it->second.partner && !partner;
                lostAt = it->second.lastPartnerPos;
                keptLength = it->second.restLength;
                W.DestroyJoint(it->second.id);   // (an id the world already dropped with its body is simply invalid)
                E.joints.erase(it);
            }
            // build the joint from the poses right now (the start pose)
            const phys::Xf xa = W.GetTransform(bodyIt->second.id);
            const phys::Xf xo = other != phys::kInvalidId ? W.GetTransform(other) : phys::Xf{};
            const Vec3 pa = j->transform->Position();
            const Vec3 pb = partner && partner->transform ? partner->transform->Position() : (partnerLost ? lostAt : j->anchor);
            auto toOther = [&](const phys::V3& w) { return other != phys::kInvalidId ? xo.ApplyInv(w) : w; };
            auto rotOther = [&](const phys::Q& w) { return other != phys::kInvalidId ? phys::NormalizeQ(phys::Conj(xo.q) * w) : w; };
            float builtLength = -1.0f;
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
                    const float rest = keptLength >= 0.0f ? keptLength : j->autoConfigure ? Vec3::Distance(pa, pb) : j->distance;
                    builtLength = rest;
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
            r.restLength = builtLength;
            r.lastPartnerPos = partner && partner->transform ? partner->transform->Position() : (partnerLost ? lostAt : j->anchor);
            if (r.id == phys::kInvalidId) continue;
            j->initialized = true;
            it = E.joints.emplace(j, r).first;
        }
        // limits, motors, springs and drives: scripts change them while playing. Worked out on a copy; the joint is only
        // edited (which wakes its bodies) when something really changed, so jointed bodies can fall asleep.
        phys::JointDef d = W.GetJoint(it->second.id);
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
        if (!SameJointSettings(d, W.GetJoint(it->second.id))) W.EditJoint(it->second.id) = d;
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
    // mud and sand (a Physics Material with damping) slow what touches them, as the old step did
    W.ForEachTouching([&](const phys::ContactEvent& e) {
        if (e.sensor) return;
        Collider3D* ca = E.Back(e.a);
        Collider3D* cb = E.Back(e.b);
        auto damp = [&](Collider3D* surface, phys::BodyId body) {
            const PhysicsMaterialData* m = surface ? SurfaceOf(surface->gameObject) : nullptr;
            if (!m || W.GetType(body) != phys::BodyType::Dynamic) return;
            if (m->linearDamping > 0.0f) W.SetLinearVelocity(body, W.GetLinearVelocity(body) * (1.0f / (1.0f + m->linearDamping * dt)));
            if (m->angularDamping > 0.0f) W.SetAngularVelocity(body, W.GetAngularVelocity(body) * (1.0f / (1.0f + m->angularDamping * dt)));
        };
        damp(ca, e.bodyB);
        damp(cb, e.bodyA);
    });

    // breakable joints break when their two anchors drift apart by more than breakForce (the Joint3D meaning)
    for (auto& kv : E.joints) {
        Joint3D* j = const_cast<Joint3D*>(kv.first);
        if (!j->breakable || j->broken) continue;
        const phys::JointDef& d = W.GetJoint(kv.second.id);
        const phys::V3 a = d.bodyA != phys::kInvalidId ? W.GetTransform(d.bodyA).Apply(d.frameA.p) : d.frameA.p;
        const phys::V3 b = W.GetTransform(d.bodyB).Apply(d.frameB.p);
        float stretch = phys::Len(b - a);
        if (d.type == phys::JointType::Distance) stretch = std::fabs(phys::Len(b - a) - d.maxLength);
        // The joint now holds firm, so its stretch stays small; what it had to undo this step is the push it gave times
        // dt^2 / mass: the stretch the old step measured before it pulled the two back (a hard yank breaks it, a weight
        // hanging still does not).
        float invMass = 0.0f;
        for (phys::BodyId id : {d.bodyA, d.bodyB})
            if (id != phys::kInvalidId && W.GetType(id) == phys::BodyType::Dynamic && W.GetMass(id) > 0.0f) invMass += 1.0f / W.GetMass(id);
        const float undone = phys::Len(W.JointForce(kv.second.id)) * dt * dt * invMass;
        if (std::max(stretch, undone) > j->breakForce) {
            j->broken = true;
            // A joint that breaks this step held nothing back: give each body the straight-line push the joint gave it
            // this step, so a yanked body flies on with its speed instead of stopping where the joint snapped.
            const phys::V3 F = W.JointForce(kv.second.id);
            if (d.bodyB != phys::kInvalidId && W.GetType(d.bodyB) == phys::BodyType::Dynamic && W.GetMass(d.bodyB) > 0.0f)
                W.SetLinearVelocity(d.bodyB, W.GetLinearVelocity(d.bodyB) - F * (dt / W.GetMass(d.bodyB)));
            if (d.bodyA != phys::kInvalidId && W.GetType(d.bodyA) == phys::BodyType::Dynamic && W.GetMass(d.bodyA) > 0.0f)
                W.SetLinearVelocity(d.bodyA, W.GetLinearVelocity(d.bodyA) + F * (dt / W.GetMass(d.bodyA)));
            W.DestroyJoint(kv.second.id);   // let go now
        }
    }

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
    const phys::WorldStats& ws = W.Stats();
    m_stats.contacts = ws.points;
    m_stats.manifolds = ws.touching;
    m_stats.islands = ws.islands;
    m_stats.warmStarted = ws.warmPoints;

    // ---- 4. events ----
    // Pairs are the two collider components (normalised), so a compound's several pieces touching the same collider
    // are one pair, and a collider rebuilt at run time (a friction or scale change) stays the same pair: no Exit/Enter.
    Scene::RemovalWatch gone(scene);
    auto norm = [](Collider3D* a, Collider3D* b) { return std::less<const void*>()(a, b) ? EngineBridge::CPair{a, b} : EngineBridge::CPair{b, a}; };
    std::vector<phys::ContactEvent> touchingNow;
    W.ForEachTouching([&](const phys::ContactEvent& e) { touchingNow.push_back(e); });
    std::vector<EngineBridge::CPair> nowList;
    std::unordered_set<EngineBridge::CPair, EngineBridge::CPairHash> nowSet;
    nowList.reserve(touchingNow.size());
    nowSet.reserve(touchingNow.size() * 2 + 1);
    for (const phys::ContactEvent& e : touchingNow) {
        Collider3D* a = E.Back(e.a);
        Collider3D* b = E.Back(e.b);
        if (!a || !b || a == b) continue;   // terrain contacts raise no callbacks (as before)
        const EngineBridge::CPair key = norm(a, b);
        if (nowSet.count(key)) continue;   // (another piece of the same pair already reported it)
        // A callback earlier in this pass may have removed a or b: only compare their pointers, never read them.
        const bool removed = gone.Gone(a) || gone.Gone(b);
        if (!removed && e.sensor && (!TriggerAccepts(a, b) || !TriggerAccepts(b, a))) continue;   // filtered out (Exit if it was in)
        nowSet.insert(key);
        nowList.push_back(key);
        if (removed) continue;
        const Vec3 n = E3(e.normal), pt = E3(e.point);
        const bool was = E.touchingSet.count(key) != 0;
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
            if (!was) {   // the closing speed when it began (the solver has already stopped it): as the old step reported it
                forA.impactSpeed = forB.impactSpeed = e.approachSpeed;
                const Vec3 tangential = relBA - n * Vec3::Dot(relBA, n);
                forA.relativeVelocity = tangential - n * e.approachSpeed;
                forB.relativeVelocity = -forA.relativeVelocity;
            }
            auto fn = was ? &Component::OnCollisionStay3D : &Component::OnCollisionEnter3D;
            DispatchCollision(a->gameObject, fn, forA, gone);
            if (!gone.Gone(a) && !gone.Gone(b)) DispatchCollision(b->gameObject, fn, forB, gone);
        }
    }
    for (const EngineBridge::CPair& key : E.touching) {   // (in the order they began: deterministic)
        if (nowSet.count(key)) continue;
        Collider3D* a = key.first;
        Collider3D* b = key.second;
        if (gone.Gone(a) || gone.Gone(b)) continue;
        if (E.colliders.find(a) == E.colliders.end() || E.colliders.find(b) == E.colliders.end()) continue;   // removed
        if (!Alive(a) || !Alive(b)) continue;
        DispatchExit(a, b, gone);
    }
    E.touching.swap(nowList);
    E.touchingSet.swap(nowSet);
}

// Distance from `point` to one engine piece. Meshes and heightfields use the triangle closest
// point (a zero-radius sphere only reports a hit when the point lies on a triangle). Convex pieces
// use GJK against a point. A plane is the half-space under its normal, not its huge bounding box.
float DistanceToPiece(const EnginePiece& piece, const Vec3& point, float maxDistance, Vec3& closest) {
    const phys::Shape& shape = piece.shape;
    if (shape.type == phys::ShapeType::Mesh && shape.mesh) {
        const phys::V3 local = piece.world.ApplyInv(P3(point));
        const float reach = std::max(maxDistance, 0.0f) + 1.0f;
        const phys::Aabb box{local - phys::V3{reach, reach, reach}, local + phys::V3{reach, reach, reach}};
        std::vector<phys::Tri3> tris;
        shape.mesh->Collect(box, tris);
        float best = 1e30f;
        Vec3 bestLocal = E3(local);
        const Vec3 at = E3(local);
        for (const phys::Tri3& t : tris) {
            Vec3 c = trimesh::ClosestPointOnTriangle(at, E3(t.a), E3(t.b), E3(t.c));
            float d2 = (at - c).SqrMagnitude();
            if (d2 < best) { best = d2; bestLocal = c; }
        }
        if (!(best < 1e29f)) return 1e30f;
        closest = E3(piece.world.Apply(P3(bestLocal)));
        return (point - closest).Magnitude();
    }
    if (shape.type == phys::ShapeType::Plane) {
        Vec3 n = E3(phys::Rotate(piece.world.q, shape.planeNormal));
        float mag = n.Magnitude();
        if (!(mag > 1e-8f)) return 1e30f;
        n = n * (1.0f / mag);
        float signedDist = Vec3::Dot(point - E3(piece.world.p), n) - shape.planeOffset;
        if (signedDist <= 0.0f) { closest = point; return 0.0f; }
        closest = point - n * signedDist;
        return signedDist;
    }
    phys::Shape probe = phys::Shape::Sphere(0.0f);
    phys::Xf at{P3(point), phys::Q{}};
    phys::DistanceResult dist = phys::GjkDistance(probe, at, shape, piece.world);
    const float rad = shape.CoreRadius();
    if (dist.overlap || dist.distance <= rad) { closest = point; return 0.0f; }
    if (!std::isfinite(dist.distance)) return 1e30f;
    Vec3 n = E3(dist.normal);
    float nm = n.Magnitude();
    if (nm < 1e-8f) {
        Vec3 away = point - E3(dist.pB);
        nm = away.Magnitude();
        if (!(nm > 1e-8f)) { closest = point; return 0.0f; }
        n = away * (1.0f / nm);
    } else {
        n = n * (1.0f / nm);
    }
    closest = E3(dist.pB) - n * rad;
    return dist.distance - rad;
}

void ConsiderSolid(NearestSolid3D& best, bool& any, float maxDistance, Collider3D* col, GameObject* go,
                   const Vec3& cp, float d) {
    if (!std::isfinite(d) || d > maxDistance) return;
    if (any && !(d < best.distance)) return;
    any = true;
    best.hit = true;
    best.collider = col;
    best.gameObject = go;
    best.point = cp;
    best.distance = d;
}

NearestSolid3D Physics3D::ClosestPoint(Scene& scene, const Vec3& point, float maxDistance,
                                        GameObject* ignore, std::uint32_t layerMask) {
    NearestSolid3D best;
    if (!(maxDistance >= 0.0f) || !std::isfinite(maxDistance) || !std::isfinite(point.x + point.y + point.z)) return best;
    bool any = false;
    MeshCache cache;
    for (Collider3D* col : scene.FindObjectsOfType<Collider3D>()) {
        if (!Passes(col, ignore, layerMask, /*queryTriggers*/ false)) continue;
        {   // a collider that cannot be nearer than what is found (or within reach) is skipped before any shape is built
            Vec3 mn, mx;
            col->WorldAABB(mn, mx);
            if (std::isfinite(mn.x + mn.y + mn.z + mx.x + mx.y + mx.z)) {
                const float dx = std::max(std::max(mn.x - point.x, point.x - mx.x), 0.0f);
                const float dy = std::max(std::max(mn.y - point.y, point.y - mx.y), 0.0f);
                const float dz = std::max(std::max(mn.z - point.z, point.z - mx.z), 0.0f);
                const float lowest = std::sqrt(dx * dx + dy * dy + dz * dz) - 1e-3f;
                if (lowest > maxDistance || (any && lowest >= best.distance)) continue;
            }
        }
        if (col->shape() == Collider3D::Shape::Mesh) {   // the mesh's own triangle tree (built once, kept by the collider)
            auto* mc = static_cast<MeshCollider3D*>(col);
            if (mc->HasTriangles()) {
                const trimesh::WorldView view(mc->Tris(), mc->WorldMatrix());
                const bool closed = mc->Tris().ClosedWinding() != 0;   // inside a closed mesh is 0 away, however far the skin is
                Vec3 cp = point;
                float d = 0.0f;
                bool inside = false;
                if (view.Nearest(point, closed ? 1e30f : maxDistance, cp, d, inside)) {
                    if (inside) { cp = point; d = 0.0f; }
                    ConsiderSolid(best, any, maxDistance, col, col->gameObject, cp, d);
                }
                continue;
            }
        }
        std::vector<EnginePiece> pieces;
        if (!PiecesOfCollider(col, /*dynamicBody*/ false, pieces, cache)) continue;
        for (const EnginePiece& piece : pieces) {
            Vec3 cp = point;
            const float d = DistanceToPiece(piece, point, maxDistance, cp);
            ConsiderSolid(best, any, maxDistance, col, col->gameObject, cp, d);
        }
    }
    for (Terrain* terr : scene.FindObjectsOfType<Terrain>()) {
        if (!TerrainPasses(terr, ignore, layerMask)) continue;
        EnginePiece piece;
        if (!PieceOfTerrain(terr, piece)) continue;
        Vec3 cp = point;
        const float d = DistanceToPiece(piece, point, maxDistance, cp);
        ConsiderSolid(best, any, maxDistance, nullptr, terr->gameObject, cp, d);
    }
    if (!any) return NearestSolid3D{};
    return best;
}

} // namespace okay
