// Player audit probe. Not a test: it steps small headless scenes with the real player
// controllers and prints what the body did (numbers), one line per case, so docs/status/player_audit.md
// can say "ok / bug / limitation" with evidence. Run:  probe_player_audit [id-prefix ...]
// Every case builds its own Scene; nothing is shared between cases.
#include <Okay.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace okay;
using Mode = PlayerController::ViewMode;

namespace {

// ---------------------------------------------------------------- output
std::vector<std::string> g_filters;
bool Wanted(const char* id) {
    if (g_filters.empty()) return true;
    for (const auto& f : g_filters) if (std::strncmp(id, f.c_str(), f.size()) == 0) return true;
    return false;
}
void Out(const char* id, const char* verdict, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
void Out(const char* id, const char* verdict, const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    std::printf("%-8s %-6s %s\n", id, verdict, buf);
    std::fflush(stdout);
}

// ---------------------------------------------------------------- stats
struct Series {
    std::vector<double> v;
    void Add(double x) { v.push_back(x); }
    size_t N() const { return v.size(); }
    double Min() const { return v.empty() ? 0 : *std::min_element(v.begin(), v.end()); }
    double Max() const { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }
    double Mean() const { return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size(); }
    double Stdev() const {
        if (v.size() < 2) return 0; double m = Mean(), s = 0;
        for (double x : v) s += (x - m) * (x - m);
        return std::sqrt(s / (v.size() - 1));
    }
    double Last() const { return v.empty() ? 0 : v.back(); }
    double Median() const {
        if (v.empty()) return 0; std::vector<double> c = v; std::sort(c.begin(), c.end());
        return c[c.size() / 2];
    }
    // largest frame-to-frame jump
    double MaxStep() const { double m = 0; for (size_t i = 1; i < v.size(); ++i) m = std::max(m, std::fabs(v[i] - v[i - 1])); return m; }
};

// ---------------------------------------------------------------- rig
struct Opt {
    bool fixed = true;          // 50 Hz physics + interpolation, as the player and the editor's Play run
    float fps = 60.0f;
    Mode mode = Mode::First;
    bool capsule = false;       // capsule collider instead of the template's box
    float scale = 1.0f;         // player transform scale
    bool ground = true;         // a big flat floor, top at y = 0
    bool bare = false;          // no Character / animator (a bare body)
    bool camera = true;
};

struct Rig {
    std::unique_ptr<Scene> sc;
    Opt o;
    GameObject* p = nullptr;
    Rigidbody3D* rb = nullptr;
    FirstPersonController* fp = nullptr;
    ThirdPersonController* tp = nullptr;
    Camera* cam = nullptr;
    float dt = 1.0f / 60.0f;
    double time = 0;
    int frame = 0;

    explicit Rig(const Opt& opt) : o(opt) {
        sc = std::make_unique<Scene>("probe");
        sc->physicsEnabled = true;
        if (o.fixed) sc->UseFixedStepPhysics(true);
        dt = 1.0f / o.fps;
        if (o.ground) Box({0, -0.5f, 0}, {400, 1, 400});
    }

    GameObject* Box(Vec3 centre, Vec3 size, Quat rot = Quat::Identity, const char* name = "Box") {
        GameObject* g = sc->CreateGameObject(name);
        g->transform->localPosition = centre;
        g->transform->localRotation = rot;
        g->AddComponent<BoxCollider3D>()->size = size;
        return g;
    }
    GameObject* MeshBox(Vec3 centre, Vec3 size, Quat rot = Quat::Identity, const char* name = "MeshBox") {
        GameObject* g = sc->CreateGameObject(name);
        g->transform->localPosition = centre;
        g->transform->localRotation = rot;
        g->transform->localScale = size;
        g->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(1.0f);
        g->AddComponent<MeshCollider3D>();
        return g;
    }
    GameObject* MeshOf(const Mesh& m, Vec3 pos, Quat rot = Quat::Identity, Vec3 scale = Vec3::One, const char* name = "Mesh") {
        GameObject* g = sc->CreateGameObject(name);
        g->transform->localPosition = pos;
        g->transform->localRotation = rot;
        g->transform->localScale = scale;
        g->AddComponent<MeshRenderer>()->mesh = m;
        g->AddComponent<MeshCollider3D>();
        return g;
    }
    GameObject* Kinematic(Vec3 centre, Vec3 size, Vec3 velocity, const char* name = "Mover") {
        GameObject* g = Box(centre, size, Quat::Identity, name);
        auto* b = g->AddComponent<Rigidbody3D>();
        b->bodyType = Rigidbody3D::BodyType::Kinematic;
        b->velocity = velocity;
        b->gravityScale = 0.0f;
        return g;
    }
    GameObject* Crate(Vec3 centre, Vec3 size, float mass, const char* name = "Crate") {
        GameObject* g = Box(centre, size, Quat::Identity, name);
        auto* b = g->AddComponent<Rigidbody3D>();
        b->mass = mass;
        return g;
    }

    // The template's player: the blocky Character + animator + a 0.6 x 1.8 x 0.6 box (or a capsule), the Player Controller
    // in `mode`, and the shared eye camera. `feet` is where the soles are.
    void Spawn(Vec3 feet, PlayerView::View start = PlayerView::View::First) {
        p = sc->CreateGameObject("Player");
        p->transform->localPosition = feet;
        p->transform->localScale = Vec3{o.scale, o.scale, o.scale};
        if (!o.bare) { p->AddComponent<Character>()->Apply(); HumanoidLibrary::EnsureAnimator(p); }
        rb = p->AddComponent<Rigidbody3D>();
        if (o.capsule) {
            auto* c = p->AddComponent<CapsuleCollider3D>();
            c->radius = 0.3f; c->height = 1.8f; c->offset = {0, 0.9f, 0};
        } else {
            auto* c = p->AddComponent<BoxCollider3D>();
            c->size = {0.6f, 1.8f, 0.6f}; c->offset = {0, 0.9f, 0};
        }
        PlayerController::Add(p, o.mode, start);
        if (o.camera) {
            GameObject* cg = Templates::MountEyeCamera(*sc, p, "Main Camera");
            cam = cg->GetComponent<Camera>();
        }
        fp = p->GetComponent<FirstPersonController>();
        tp = p->GetComponent<ThirdPersonController>();
        if (const char* f = std::getenv("PROBE_FRICTION")) rb->friction = (float)std::atof(f);
        sc->Start();
    }

    Vec3 pos() const { return p->transform->Position(); }
    bool grounded() const { return fp && FirstPersonOwns(p) ? fp->IsGrounded() : (tp ? tp->IsGrounded() : (fp ? fp->IsGrounded() : true)); }
    void Look(float yawDeg) { if (fp) fp->yaw = yawDeg; if (tp) tp->yaw = yawDeg; }

    void Frame(const std::vector<char>& keys) {
        Input::FeedKeys(keys);
        sc->Update(dt);
        time += dt; ++frame;
    }
    void Frames(double seconds, const std::vector<char>& keys, const std::function<void(Rig&)>& each = nullptr) {
        const int n = (int)std::lround(seconds / dt);
        for (int i = 0; i < n; ++i) { Frame(keys); if (each) each(*this); }
    }
    void Release() { Input::FeedKeys({}); }
    ~Rig() { Input::FeedKeys({}); }
};

const char* FixedName(bool f) { return f ? "fixed50" : "varstep"; }
Vec3 Horiz(Vec3 v) { return {v.x, 0, v.z}; }
float Len2(Vec3 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
constexpr float kRun = 8.0f, kWalk = 4.5f;
const char kShift = Input::KeyShift, kCtrl = Input::KeyCtrl;

} // namespace

// =================================================================== E01..E04 walls
static void E01_WallPerpendicular() {
    for (bool fixed : {true, false}) for (float fps : {30.0f, 144.0f}) {
        Opt o; o.fixed = fixed; o.fps = fps;
        Rig r(o); r.Box({0, 2, -3.25f}, {10, 4, 0.5f}); r.Spawn({0, 0, 0});
        Series z, vz;
        r.Frames(3.0, {'w'}, [&](Rig& r) { if (r.time > 2.0) { z.Add(r.pos().z); vz.Add(r.rb->velocity.z); } });
        const double pen = std::max(0.0, -3.0 - (z.Min() - 0.3));
        char id[16]; std::snprintf(id, sizeof id, "E01");
        Out(id, pen < 0.03 && z.Stdev() < 0.005 ? "ok" : "check",
            "perpendicular wall, hold W, %s %.0ffps: stops at z=%.3f (pen %.3f m), z-jitter sd %.4f m, end vz %.3f",
            FixedName(fixed), fps, z.Last(), pen, z.Stdev(), vz.Last());
    }
}

static void E02_WallAngle() {
    for (float a : {15.0f, 30.0f, 45.0f, 60.0f, 80.0f}) for (bool box : {true, false}) {
        Opt o; Rig r(o);
        const Quat q = Quat::AngleAxis(a, Vec3::Up);
        const Vec3 n = q * Vec3{0, 0, 1}, t = q * Vec3{1, 0, 0};
        const Vec3 face{0, 0, -3.0f};
        if (box) r.Box(face - n * 0.25f + Vec3{0, 2, 0}, {30, 4, 0.5f}, q);
        else r.MeshBox(face - n * 0.25f + Vec3{0, 2, 0}, {30, 4, 0.5f}, q);
        r.Spawn({0, 0, 0});
        Vec3 p0{}, p1{};
        r.Frames(1.2, {'w'});
        p0 = r.pos();
        r.Frames(1.5, {'w'});
        p1 = r.pos();
        const Vec3 d = p1 - p0;
        const double along = d.x * t.x + d.z * t.z;           // metres slid along the wall
        const double speed = along / 1.5;
        const double expect = kWalk * std::sin(a * 0.0174532925f);
        // wall-face penetration of the box's nearest corner: signed distance of the player's centre from the face, minus its radius at that yaw
        Out("E02", speed > 0.8 * expect ? "ok" : (speed > 0.4 * expect ? "limit" : "BUG"),
            "wall at %2.0f deg (%s), hold W 4.5 m/s: slides %.2f m/s along it (a frictionless slide would be %.2f), %.0f%%",
            a, box ? "box" : "mesh", speed, expect, expect > 0 ? 100.0 * speed / expect : 0.0);
    }
}

static void E04_ThinWall() {
    for (bool fixed : {true, false}) for (float fps : {30.0f, 60.0f, 144.0f, 240.0f}) for (float thick : {0.02f, 0.1f, 0.5f}) {
        Opt o; o.fixed = fixed; o.fps = fps;
        Rig r(o);
        r.Box({0, 2, -4.0f - thick * 0.5f}, {10, 4, thick}); r.Spawn({0, 0, 0});
        r.Frames(2.5, {'w', kShift});
        const double z = r.pos().z;
        const bool through = z < -4.0 - thick;
        Out("E04", through ? "BUG" : "ok", "sprint at a %.2f m wall, %s %3.0ffps: end z=%.2f %s",
            thick, FixedName(fixed), fps, z, through ? "THROUGH THE WALL" : "held");
    }
}

// =================================================================== slopes
enum class Ground3 { Box, Mesh, Heightmap };
static const char* GName(Ground3 g) { return g == Ground3::Box ? "box" : g == Ground3::Mesh ? "mesh" : "heightmap"; }

// A ramp whose surface passes through the origin and rises toward -Z (the way the player faces by default).
static void BuildRamp(Rig& r, Ground3 g, float deg) {
    const float th = deg * 0.0174532925f;
    const Vec3 n{0, std::cos(th), std::sin(th)};
    const Quat q = Quat::AngleAxis(deg, Vec3::Right);
    if (g == Ground3::Box) r.Box(n * -0.25f, {40, 0.5f, 120}, q, "Ramp");
    else if (g == Ground3::Mesh) r.MeshBox(n * -0.25f, {40, 0.5f, 120}, q, "Ramp");
    else {
        GameObject* go = r.sc->CreateGameObject("Terrain");
        auto* t = go->AddComponent<Terrain>();
        t->resolution = 100; t->size = 100.0f; t->Resize(100); t->size = 100.0f;
        const float cell = t->CellSize();
        for (int iz = 0; iz <= 100; ++iz) for (int ix = 0; ix <= 100; ++ix) {
            const float z = iz * cell - 50.0f;
            t->SetHeight(ix, iz, -z * std::tan(th));
        }
    }
}

static void E05_Slopes() {
    for (Ground3 g : {Ground3::Heightmap, Ground3::Box, Ground3::Mesh})
        for (float deg : {10.0f, 25.0f, 40.0f, 49.0f, 55.0f, 65.0f, 75.0f}) {
            const float th = deg * 0.0174532925f;
            // (a) stand still, (b) walk uphill, (c) walk downhill, (d) hold sprint uphill
            double drift = 0, endSpeed = 0, up = 0, climb = 0, down = 0;
            {
                Opt o; o.ground = false; Rig r(o); BuildRamp(r, g, deg);
                r.Spawn({0, std::max(0.0f, 0.3f * std::tan(th)) + (g == Ground3::Heightmap ? 0.0f : 0.05f), 0});
                r.Frames(0.8, {});
                const Vec3 a = r.pos();
                r.Frames(3.0, {});
                const Vec3 b = r.pos();
                drift = Len2(b - a); endSpeed = r.rb->velocity.Magnitude();
            }
            {
                Opt o; o.ground = false; Rig r(o); BuildRamp(r, g, deg);
                r.Spawn({0, std::max(0.0f, 0.3f * std::tan(th)) + (g == Ground3::Heightmap ? 0.0f : 0.05f), 0});
                r.Frames(0.8, {});
                const Vec3 a = r.pos();
                r.Frames(3.0, {'w'});
                const Vec3 b = r.pos();
                up = -(b.z - a.z); climb = b.y - a.y;
            }
            {
                Opt o; o.ground = false; Rig r(o); BuildRamp(r, g, deg);
                r.Spawn({0, std::max(0.0f, 0.3f * std::tan(th)) + (g == Ground3::Heightmap ? 0.0f : 0.05f), 0});
                r.Look(180.0f);
                r.Frames(0.8, {});
                const Vec3 a = r.pos();
                r.Frames(2.0, {'w'});
                const Vec3 b = r.pos();
                down = (b.z - a.z); (void)down;
            }
            const bool limitOn = deg > 50.0f;
            const double expectUp = kWalk * std::cos(th);          // horizontal progress per second uphill if nothing resists
            const char* v = "ok";
            if (!limitOn && climb < 0.5 * expectUp * 3.0 * std::tan(th)) v = "limit";   // walkable slope that cannot be climbed
            if (limitOn && climb > 2.0) v = "BUG";                                      // too steep to walk, yet climbed
            if (drift > 0.5) v = "BUG";                                                 // slides while standing still
            Out("E05", v, "%-9s ramp %2.0f deg: stand 3 s drift %.2f m (v %.2f); hold W 3 s climbs %.2f m high (%.2f m forward of %.2f); limit 50",
                GName(g), deg, drift, endSpeed, climb, up, expectUp * 3.0);
        }
}

// =================================================================== steps
static void E06_Steps() {
    for (bool mesh : {false, true}) for (float h : {0.05f, 0.15f, 0.25f, 0.35f, 0.45f, 0.6f, 1.0f}) for (bool run : {false, true}) {
        Opt o; Rig r(o);
        if (mesh) r.MeshBox({0, h * 0.5f, -5.0f}, {6, h, 6}, Quat::Identity, "Step"); else r.Box({0, h * 0.5f, -5.0f}, {6, h, 6}, Quat::Identity, "Step");
        r.Spawn({0, 0, 0});
        double tTop = -1; Series y;
        r.Frames(3.0, run ? std::vector<char>{'w', kShift} : std::vector<char>{'w'}, [&](Rig& r) {
            y.Add(r.pos().y);
            if (tTop < 0 && r.pos().y > h - 0.06f && r.pos().z < -2.0f) tTop = r.time;
        });
        const bool climbed = r.pos().y > h - 0.06f && r.pos().z < -2.5f;
        const bool expectClimb = h <= 0.35f;
        const char* v = climbed == expectClimb ? "ok" : (expectClimb ? "BUG" : "info");
        Out("E06", v, "%s step %.2f m, %s: %s (end y %.2f z %.2f, reached top at %.2fs, max y %.2f)",
            mesh ? "mesh" : "box ", h, run ? "run " : "walk", climbed ? "climbed" : "blocked", r.pos().y, r.pos().z, tTop, y.Max());
    }
}

static void E07_Stairs() {
    for (bool mesh : {false, true}) for (bool capsule : {false, true}) {
        const int steps = 12; const float rise = 0.18f, tread = 0.30f;
        // ---- up
        double upTime = -1, airUp = 0, upN = 0; Series camY;
        {
            Opt o; o.capsule = capsule; Rig r(o);
            const Quat flip = Quat::AngleAxis(180.0f, Vec3::Up);
            if (mesh) r.MeshOf(Mesh::Stairs(steps, 3.0f, steps * rise, steps * tread), {0, 0, -2.0f}, flip, Vec3::One, "Stairs");
            else for (int i = 0; i < steps; ++i) r.Box({0, (i + 1) * rise * 0.5f, -2.0f - (i + 0.5f) * tread}, {3.0f, (i + 1) * rise, tread}, Quat::Identity, "Tread");
            r.Box({0, steps * rise - 0.5f, -2.0f - steps * tread - 4.0f}, {10, 1, 8}, Quat::Identity, "Landing");
            r.Spawn({0, 0, 0});
            r.Frames(5.0, {'w'}, [&](Rig& r) {
                if (upTime < 0 && r.pos().y > steps * rise - 0.08f) upTime = r.time;
                if (upTime < 0) { ++upN; if (!r.grounded()) ++airUp; if (r.cam) camY.Add(r.cam->transform->Position().y - r.pos().y); }
            });
        }
        // ---- down
        double downTime = -1, airDown = 0, downN = 0, maxVy = 0;
        {
            Opt o; o.capsule = capsule; Rig r(o);
            const Quat flip = Quat::AngleAxis(180.0f, Vec3::Up);
            if (mesh) r.MeshOf(Mesh::Stairs(steps, 3.0f, steps * rise, steps * tread), {0, 0, -2.0f}, flip, Vec3::One, "Stairs");
            else for (int i = 0; i < steps; ++i) r.Box({0, (i + 1) * rise * 0.5f, -2.0f - (i + 0.5f) * tread}, {3.0f, (i + 1) * rise, tread}, Quat::Identity, "Tread");
            r.Box({0, steps * rise - 0.5f, -2.0f - steps * tread - 4.0f}, {10, 1, 8}, Quat::Identity, "Landing");
            r.Spawn({0, steps * rise, -2.0f - steps * tread - 1.5f});
            r.Look(180.0f);
            r.Frames(0.6, {});
            r.Frames(3.0, {'w'}, [&](Rig& r) {
                if (downTime < 0 && r.pos().y < 0.08f) downTime = r.time;
                if (downTime < 0) { ++downN; if (!r.grounded()) ++airDown; maxVy = std::max(maxVy, (double)-r.rb->velocity.y); }
            });
        }
        const double flat = (steps * tread) / kWalk;
        Out("E07", "info", "%s stairs 12x(%.2f up, %.2f run), %s body: UP reaches top in %.2fs (flat %.2fs), not-grounded %3.0f%% of frames, eye-height sd %.3f; "
            "DOWN %.2fs, not-grounded %3.0f%% of frames, fastest fall %.1f m/s",
            mesh ? "mesh" : "box ", rise, tread, capsule ? "capsule" : "box    ", upTime, flat, upN > 0 ? 100.0 * airUp / upN : 0.0, camY.Stdev(),
            downTime, downN > 0 ? 100.0 * airDown / downN : 0.0, maxVy);
    }
}

// =================================================================== ledge
static void E08_Ledge() {
    for (bool capsule : {false, true}) {
        std::string line;
        for (float d : {0.0f, 0.15f, 0.29f, 0.31f, 0.45f}) {
            Opt o; o.ground = false; o.capsule = capsule; Rig r(o);
            r.Box({-25, -0.5f, 0}, {50, 1, 50});               // floor for x < 0, edge at x = 0
            r.Spawn({d, 0.02f, 0});
            r.Frames(3.0, {});
            char b[80]; std::snprintf(b, sizeof b, " centre %.2f past edge -> %s (y %.2f);", d, r.pos().y > -0.2f ? "STANDS" : "falls", r.pos().y);
            line += b;
        }
        Out("E08", "info", "ledge, %s body, idle 3 s:%s", capsule ? "capsule" : "box   ", line.c_str());
    }
}

// =================================================================== ceilings
static void E09_CrouchCeiling() {
    for (bool capsule : {false, true}) {
        Opt o; o.capsule = capsule; Rig r(o);
        r.Box({0, 1.5f, -6.0f}, {4, 0.4f, 8}, Quat::Identity, "Ceiling");   // underside at y = 1.3
        r.Spawn({0, 0, 0});
        r.Frames(0.4, {});
        r.Frames(0.1, {kCtrl}); r.Frames(0.7, {});                          // crouch
        const auto st0 = r.fp->stance();
        r.Frames(1.8, {'w'});                                               // walk (crouched) under it
        const double zIn = r.pos().z;
        r.Frames(0.1, {kCtrl}); r.Frames(0.8, {});                          // try to stand up
        const auto stUnder = r.fp->stance();
        const double hUnder = r.fp->stanceBody().curH;
        Series topY;
        r.Frames(0.0, {});
        r.Frames(4.5, {'w'}, [&](Rig& r) { topY.Add(r.pos().y); });          // walk out the far end
        r.Frames(1.0, {});
        const auto stOut = r.fp->stance();
        Out("E09", (stUnder == FirstPersonController::Stance::Crouch && stOut == FirstPersonController::Stance::Stand) ? "ok" : "BUG",
            "%s body: crouched=%d, under ceiling at z=%.2f: stand request -> %s (collider %.2f m of %.2f), after walking out -> %s",
            capsule ? "capsule" : "box    ", st0 == FirstPersonController::Stance::Crouch, zIn,
            stUnder == FirstPersonController::Stance::Crouch ? "refused" : "STOOD UP INSIDE IT", hUnder, r.fp->stanceBody().standH,
            stOut == FirstPersonController::Stance::Stand ? "stands" : "still crouched");
    }
}

static void E10_JumpCeiling() {
    for (int variant = 0; variant < 3; ++variant) {
        Opt o; Rig r(o);
        GameObject* ceil = nullptr;
        if (variant == 0) ceil = r.Box({0, 2.4f, 0}, {6, 0.4f, 6}, Quat::Identity, "Ceiling");               // underside 2.2
        else if (variant == 1) {                                                                           // thin ceiling whose pivot is BELOW the player's pivot
            ceil = r.sc->CreateGameObject("Ceiling");
            ceil->transform->localPosition = {0, -0.3f, 0};
            auto* c = ceil->AddComponent<BoxCollider3D>(); c->size = {6, 0.4f, 6}; c->offset = {0, 2.7f, 0};  // surface at y = 2.2 .. 2.6
        } else ceil = r.Box({0, 2.4f, 0}, {6, 0.4f, 6}, Quat::Identity, "Ceiling");
        r.Spawn({0, 0, 0});
        r.Frames(0.5, {});
        Series y; int air2 = 0; int jumpsHit = 0; double lastVy = 0;
        // press jump 6 times at 0.35 s intervals while pinned against the ceiling
        for (int k = 0; k < 6; ++k) {
            r.Frame({' '});
            r.Frame({' '});
            const double vyAfter = r.rb->velocity.y;
            if (r.pos().y > 0.25f && vyAfter > 4.0) ++jumpsHit;               // a jump fired while clearly off the ground
            r.Frames(0.35, {}, [&](Rig& r) { y.Add(r.pos().y); if (r.pos().y > 0.4 && !r.grounded()) ++air2; });
            lastVy = r.rb->velocity.y;
        }
        (void)lastVy;
        Out("E10", jumpsHit == 0 ? "ok" : "BUG", "jump under a ceiling (%s): max body top %.2f (ceiling underside 2.2), air jumps fired while off the ground %d",
            variant == 0 ? "slab, pivot above" : variant == 1 ? "slab, pivot BELOW player" : "slab again", y.Max() + 1.8, jumpsHit);
    }
}

// =================================================================== ground detection over seams
static void E11_Seams() {
    for (int kind = 0; kind < 4; ++kind) for (float speed : {kWalk, kRun}) {
        Opt o; o.ground = false; Rig r(o);
        const char* name = "";
        if (kind == 0) { name = "40 boxes, flush"; for (int i = 0; i < 40; ++i) r.Box({i * 1.0f - 5.0f, -0.5f, 0}, {1.0f, 1, 20}, Quat::Identity, "Seam"); }
        else if (kind == 1) { name = "40 boxes, 1 cm gaps"; for (int i = 0; i < 40; ++i) r.Box({i * 1.01f - 5.0f, -0.5f, 0}, {1.0f, 1, 20}, Quat::Identity, "Seam"); }
        else if (kind == 2) { name = "40 boxes, 2 mm lips"; for (int i = 0; i < 40; ++i) r.Box({i * 1.0f - 5.0f, -0.5f + ((i & 1) ? 0.002f : 0.0f), 0}, {1.0f, 1, 20}, Quat::Identity, "Seam"); }
        else { name = "mesh grid 1 m cells"; r.MeshOf(Mesh::Grid(40, 20, 40, 20), {15, 0, 0}, Quat::Identity, Vec3::One, "Grid"); }
        r.Spawn({-4.0f, 0.02f, 0});
        r.Look(-90.0f);                                                  // forward = +X
        r.Frames(0.6, {});
        Series y, vy; int air = 0, n = 0;
        r.Frames(5.0, speed > 5 ? std::vector<char>{'w', kShift} : std::vector<char>{'w'}, [&](Rig& r) {
            if (r.pos().x > 28) return;
            y.Add(r.pos().y); vy.Add(r.rb->velocity.y); ++n; if (!r.grounded()) ++air;
        });
        const bool ok = y.Max() - y.Min() < 0.01 && air == 0;
        Out("E11", ok ? "ok" : "BUG", "%-22s at %.1f m/s: grounded false on %d of %d frames, body height range %.4f m, |vy| max %.2f, x reached %.1f",
            name, speed, air, n, y.Max() - y.Min(), std::max(std::fabs(vy.Min()), std::fabs(vy.Max())), r.pos().x);
    }
}


// =================================================================== moving platforms
namespace {
struct ProbeMover : Component {
    Vec3 v{0, 0, 0};
    float spinDeg = 0;          // degrees per second about Y
    void Update(float dt) override {
        if (!transform) return;
        transform->localPosition = transform->localPosition + v * dt;
        if (spinDeg != 0.0f) transform->localRotation = Quat::AngleAxis(spinDeg * dt, Vec3::Up) * transform->localRotation;
    }
};
}

static void E12_Platforms() {
    struct K { const char* name; int how; Vec3 vel; float spin; float riderX; };
    const K cases[] = {
        {"kinematic body  +X 3 m/s ", 0, {3, 0, 0}, 0, 0},
        {"kinematic body  +X 8 m/s ", 0, {8, 0, 0}, 0, 0},
        {"kinematic body  +Y 2 m/s ", 0, {0, 2, 0}, 0, 0},
        {"script-moved    +X 3 m/s ", 1, {3, 0, 0}, 0, 0},
        {"script-moved    +Y 2 m/s ", 1, {0, 2, 0}, 0, 0},
        {"kinematic spin 57 deg/s  ", 0, {0, 0, 0}, 57.3f, 2.0f},
        {"script-moved spin 57 d/s ", 1, {0, 0, 0}, 57.3f, 2.0f},
    };
    for (bool fixed : {true, false}) for (float fps : {60.0f, 144.0f}) for (const K& k : cases) {
        Opt o; o.ground = false; o.fixed = fixed; o.fps = fps; Rig r(o);
        GameObject* plat = r.Box({0, -0.25f, 0}, {8, 0.5f, 8}, Quat::Identity, "Platform");
        if (k.how == 0) {
            auto* b = plat->AddComponent<Rigidbody3D>();
            b->bodyType = Rigidbody3D::BodyType::Kinematic; b->gravityScale = 0; b->velocity = k.vel;
            if (k.spin != 0) { b->freezeRotation = false; b->angularVelocity = {0, k.spin * 0.0174532925f, 0}; }
        } else {
            auto* m = plat->AddComponent<ProbeMover>(); m->v = k.vel; m->spinDeg = k.spin;
        }
        r.Spawn({k.riderX, 0.0f, 0});
        r.Frames(0.5, {});                                    // settle on the platform
        const Vec3 p0 = r.pos(); const Vec3 q0 = plat->transform->Position();
        const Quat plat0 = plat->transform->Rotation();
        Series rel, relY; int air = 0, n = 0; Vec3 inLocal0 = plat->transform->InverseTransformPoint(p0);
        r.Frames(2.5, {}, [&](Rig& r) {
            const Vec3 pl = plat->transform->InverseTransformPoint(r.pos());
            rel.Add(Len2(pl - inLocal0)); relY.Add(pl.y);
            ++n; if (!r.grounded()) ++air;
        });
        const double slip = rel.Last();
        const char* v = slip < 0.08 && relY.Stdev() < 0.01 && air == 0 ? "ok" : (slip < 0.5 && air == 0 ? "limit" : "BUG");
        char idb[16]; std::snprintf(idb, sizeof idb, "E12");
        Out(idb, v, "%s %-7s %3.0ffps: rider slips %.3f m in platform space after 2.5 s (y sd %.4f, min..max %.3f..%.3f), not-grounded %d/%d, rider yaw turned %.1f deg, platform %.1f",
            k.name, FixedName(fixed), fps, slip, relY.Stdev(), relY.Min(), relY.Max(), air, n,
            r.fp->yaw - 0.0f, std::fabs(TwistYawDeg(plat->transform->Rotation() * plat0.Inverse())));
        (void)q0;
    }
}

static void E12b_JumpOffPlatform() {
    for (int how = 0; how < 2; ++how) {
        Opt o; o.ground = false; Rig r(o);
        GameObject* plat = r.Box({0, -0.25f, 0}, {40, 0.5f, 8}, Quat::Identity, "Platform");
        if (how == 0) { auto* b = plat->AddComponent<Rigidbody3D>(); b->bodyType = Rigidbody3D::BodyType::Kinematic; b->gravityScale = 0; b->velocity = {3, 0, 0}; }
        else { plat->AddComponent<ProbeMover>()->v = {3, 0, 0}; }
        r.Spawn({-10, 0, 0});
        r.Frames(1.0, {});
        const double platV = 3.0;
        r.Frame({' '}); r.Frame({' '});
        const double vxAfterJump = r.rb->velocity.x;
        double vxApex = 0, apexT = 0;
        r.Frames(0.45, {}, [&](Rig& r) { if (r.rb->velocity.y < 0.4 && r.rb->velocity.y > -0.4 && apexT == 0) { vxApex = r.rb->velocity.x; apexT = r.time; } });
        r.Frames(1.2, {});
        const Vec3 rel = plat->transform->InverseTransformPoint(r.pos());
        Out("E12b", vxAfterJump > 0.8 * platV ? "ok" : "limit",
            "jump straight up from a platform moving +X 3 m/s (%s): world vx after the jump %.2f (platform %.1f), at apex %.2f; 1.65 s later the body is %.2f m behind the platform centre line",
            how == 0 ? "kinematic body" : "script-moved", vxAfterJump, platV, vxApex, -(rel.x + 10.0));
    }
}

static void E12c_Pushed() {
    for (float fps : {60.0f}) for (bool fixed : {true, false}) {
        Opt o; o.fixed = fixed; o.fps = fps; Rig r(o);
        r.Box({3.0f, 2, 0}, {1.0f, 4, 10}, Quat::Identity, "StaticWall");      // face at x = 2.5
        GameObject* mover = r.Box({-3.0f, 2, 0}, {1.0f, 4, 10}, Quat::Identity, "Squeezer");
        auto* b = mover->AddComponent<Rigidbody3D>();
        b->bodyType = Rigidbody3D::BodyType::Kinematic; b->gravityScale = 0; b->velocity = {2.0f, 0, 0};
        r.Spawn({0.0f, 0.0f, 0});
        Series x; double maxPen = 0;
        r.Frames(4.0, {}, [&](Rig& r) {
            x.Add(r.pos().x);
            maxPen = std::max(maxPen, (double)(r.pos().x + 0.3f - 2.5f));
        });
        Out("E12c", r.pos().x < 3.5 && maxPen < 0.1 ? "ok" : "BUG",
            "kinematic wall 2 m/s squeezes the player against a static wall (%s): max overlap of the static wall %.3f m, end x %.2f (static face 2.5, squeezer front %.2f)",
            FixedName(fixed), maxPen, r.pos().x, mover->transform->Position().x + 0.5f);
    }
}

// =================================================================== rigid bodies
static void E13_RigidBodies() {
    for (float mass : {1.0f, 10.0f, 100.0f, 1000.0f}) {
        Opt o; Rig r(o);
        GameObject* c = r.Crate({0, 0.5f, -2.0f}, {1, 1, 1}, mass);
        r.Spawn({0, 0, 0});
        r.Frames(0.5, {});
        const Vec3 p0 = r.pos(), c0 = c->transform->Position();
        r.Frames(3.0, {'w'});
        Out("E13", "info", "walk into a %6.0f kg crate for 3 s: crate moved %.2f m, player moved %.2f m (free walking 3 s = ~12 m), end speed %.2f",
            mass, -(c->transform->Position().z - c0.z), -(r.pos().z - p0.z), Len2(r.rb->velocity));
    }
    for (float mass : {10.0f, 100.0f}) for (float speed : {5.0f, 15.0f}) {
        Opt o; Rig r(o);
        GameObject* c = r.Crate({0, 0.5f, -8.0f}, {1, 1, 1}, mass);
        c->GetComponent<Rigidbody3D>()->velocity = {0, 0, speed};
        r.Spawn({0, 0, 0});
        double maxV = 0, maxD = 0;
        r.Frames(2.5, {}, [&](Rig& r) { maxV = std::max(maxV, (double)Len2(r.rb->velocity)); maxD = std::max(maxD, (double)r.pos().z); });
        Out("E13b", "info", "%.0f kg crate thrown at the idle player at %.0f m/s: player max speed %.2f m/s, pushed %.2f m, end z %.2f",
            mass, speed, maxV, maxD, r.pos().z);
    }
    {   // a heavy crate dropped on the player's head
        Opt o; Rig r(o);
        GameObject* c = r.Crate({0, 6.0f, 0}, {1, 1, 1}, 50.0f);
        r.Spawn({0, 0, 0});
        Series y, vy;
        r.Frames(3.0, {}, [&](Rig& r) { y.Add(r.pos().y); vy.Add(r.rb->velocity.y); });
        Out("E13c", y.Min() > -0.05 ? "ok" : "BUG", "50 kg crate dropped from 6 m onto the player's head: player min y %.3f (floor 0), end y %.3f, crate end y %.2f, |vy| max %.2f",
            y.Min(), y.Last(), c->transform->Position().y, std::max(std::fabs(vy.Min()), std::fabs(vy.Max())));
    }
    {   // standing on a sliding crate
        Opt o; Rig r(o);
        GameObject* c = r.Crate({0, 0.5f, 0}, {2, 1, 2}, 20.0f);
        c->GetComponent<Rigidbody3D>()->velocity = {3, 0, 0};
        r.Spawn({0, 1.0f, 0});
        r.Frames(1.5, {});
        Out("E13d", "info", "standing on a 20 kg crate pushed to 3 m/s: after 1.5 s crate x %.2f, player x %.2f (player rides it only by friction)", c->transform->Position().x, r.pos().x);
    }
}

// =================================================================== depenetration
static void E14_Depen() {
    for (float d : {0.1f, 0.5f, 0.9f, 1.5f, 3.0f}) {
        Opt o; Rig r(o); r.Spawn({0, -d, 0});
        Series y, vy; double tOut = -1;
        r.Frames(2.0, {}, [&](Rig& r) { y.Add(r.pos().y); vy.Add(r.rb->velocity.y); if (tOut < 0 && r.pos().y > -0.01) tOut = r.time; });
        Out("E14", y.Last() > -0.02 ? "ok" : "BUG", "start %.1f m inside the floor: out after %.2fs, peak speed %.2f m/s, peak height %.2f, end y %.3f",
            d, tOut, vy.Max(), y.Max(), y.Last());
    }
    for (float thick : {0.3f, 0.6f, 1.5f, 6.0f}) for (float off : {0.0f, 0.1f}) {
        Opt o; Rig r(o); r.Box({0, 2, 0}, {10, 4, thick}, Quat::Identity, "Wall");
        r.Spawn({0, 0.0f, off});                         // pivot inside the wall (off = how far from the middle)
        Series z, sp; double tOut = -1;
        r.Frames(2.5, {}, [&](Rig& r) { z.Add(r.pos().z); sp.Add(Len2(r.rb->velocity) + std::fabs(r.rb->velocity.y)); if (tOut < 0 && std::fabs(r.pos().z) > thick * 0.5 + 0.3 && std::fabs(r.pos().y) < 0.5) tOut = r.time; });
        Out("E14b", "info", "start inside a %.1f m thick wall (%.1f off centre): out after %.2fs on the %s side, final z %.2f y %.2f, peak speed %.2f",
            thick, off, tOut, r.pos().z > 0 ? "+Z" : "-Z", r.pos().z, r.pos().y, sp.Max());
    }
}

// =================================================================== falling fast
static void E15_Fall() {
    struct F { const char* name; int kind; float thick; };
    const F floors[] = {{"box 1.0 m", 0, 1.0f}, {"box 0.1 m", 0, 0.1f}, {"box 0.02 m", 0, 0.02f}, {"mesh plane", 1, 0}, {"heightmap", 2, 0}};
    for (bool fixed : {true, false}) for (float fps : {30.0f, 60.0f, 240.0f}) for (float h : {20.0f, 100.0f}) for (const F& f : floors) {
        Opt o; o.ground = false; o.fixed = fixed; o.fps = fps; Rig r(o);
        if (f.kind == 0) r.Box({0, -f.thick * 0.5f, 0}, {400, f.thick, 400}, Quat::Identity, "Floor");
        else if (f.kind == 1) r.MeshOf(Mesh::Plane(400.0f), {0, 0, 0}, Quat::Identity, Vec3::One, "Floor");
        else { GameObject* g = r.sc->CreateGameObject("Terrain"); auto* t = g->AddComponent<Terrain>(); t->Resize(32); t->size = 400.0f; t->Flatten(0.0f); }
        r.Spawn({0, h, 0});
        double maxFall = 0;
        r.Frames(h > 50 ? 8.0 : 5.0, {}, [&](Rig& r) { maxFall = std::max(maxFall, (double)-r.rb->velocity.y); });
        const bool through = r.pos().y < -1.0;
        char idb[16]; std::snprintf(idb, sizeof idb, "E15");
        Out(idb, through ? "BUG" : "ok", "fall %3.0f m onto %-10s, %s %3.0ffps: peak fall speed %.1f m/s, end y %.2f %s", h, f.name, FixedName(fixed), fps, maxFall, r.pos().y, through ? "TUNNELLED" : "landed");
    }
}

// =================================================================== frame-rate consistency
static void E17_FrameRate() {
    for (bool fixed : {true, false}) {
        double walkD[4], runD[4], apex[4], air[4], stop[4], t90[4]; int i = 0;
        for (float fps : {30.0f, 60.0f, 144.0f, 240.0f}) {
            Opt o; o.fixed = fixed; o.fps = fps;
            { Rig r(o); r.Spawn({0, 0, 0}); r.Frames(0.5, {}); const double z0 = r.pos().z; r.Frames(2.0, {'w'}); walkD[i] = z0 - r.pos().z; }
            { Rig r(o); r.Spawn({0, 0, 0}); r.Frames(0.5, {}); const double z0 = r.pos().z; double t = -1;
              r.Frames(2.0, {'w', kShift}, [&](Rig& r) { if (t < 0 && Len2(r.rb->velocity) > 0.9 * kRun) t = r.time - 0.5; });
              runD[i] = z0 - r.pos().z; t90[i] = t;
              const double z1 = r.pos().z; r.Frames(1.5, {}); stop[i] = z1 - r.pos().z; }
            { Rig r(o); r.Spawn({0, 0, 0}); r.Frames(0.6, {}); double ymax = 0; double ta = 0;
              r.Frame({' '}); r.Frame({' '});
              r.Frames(1.8, {}, [&](Rig& r) { ymax = std::max(ymax, (double)r.pos().y); if (r.pos().y > 0.02) ta += r.dt; });
              apex[i] = ymax; air[i] = ta + 2 * r.dt; }
            ++i;
        }
        auto spread = [](const double* a) { double lo = *std::min_element(a, a + 4), hi = *std::max_element(a, a + 4); return hi > 0 ? 100.0 * (hi - lo) / hi : 0.0; };
        Out("E17", "info", "%s: walk 2 s distance 30/60/144/240 fps = %.2f %.2f %.2f %.2f (spread %.1f%%)", FixedName(fixed), walkD[0], walkD[1], walkD[2], walkD[3], spread(walkD));
        Out("E17", "info", "%s: sprint 2 s distance            = %.2f %.2f %.2f %.2f (spread %.1f%%); stop distance %.2f %.2f %.2f %.2f; 90%% speed after %.2f %.2f %.2f %.2f s",
            FixedName(fixed), runD[0], runD[1], runD[2], runD[3], spread(runD), stop[0], stop[1], stop[2], stop[3], t90[0], t90[1], t90[2], t90[3]);
        Out("E17", "info", "%s: jump apex height               = %.3f %.3f %.3f %.3f (spread %.1f%%); airtime %.2f %.2f %.2f %.2f s (6 m/s, g 9.81: apex 1.835, air 1.22)",
            FixedName(fixed), apex[0], apex[1], apex[2], apex[3], spread(apex), air[0], air[1], air[2], air[3]);
    }
}


// =================================================================== third-person camera
static bool CameraInSolid(Rig& r, const Vec3& at, float radius = 0.12f) {
    Physics3D::IgnoreHierarchy own(r.p);
    for (Collider3D* c : r.sc->physics3D().OverlapSphere(*r.sc, at, radius, Physics3D::kAllLayers, false, r.p))
        if (c && !c->isTrigger && c->gameObject != r.p && c->gameObject->name != "Floor") return true;
    return false;
}

static void E18_Camera() {
    // (a) a wall right behind the player
    for (float wallZ : {0.8f, 1.5f, 3.0f}) for (bool fixed : {true, false}) {
        Opt o; o.mode = Mode::Third; o.fixed = fixed; Rig r(o);
        r.Box({0, 3, wallZ + 0.25f}, {20, 6, 0.5f}, Quat::Identity, "Wall");     // face at z = wallZ
        r.Spawn({0, 0, 0}, PlayerView::View::Third);
        Series gap, dist; int inside = 0;
        r.Frames(2.0, {}, [&](Rig& r) {
            const Vec3 c = r.cam->transform->Position();
            gap.Add(wallZ - c.z); dist.Add((c - (r.pos() + Vec3{0, 1.5f, 0})).Magnitude());
            if (CameraInSolid(r, c)) ++inside;
        });
        Out("E18a", gap.Min() > 0.05 && inside == 0 ? "ok" : "BUG", "3rd person, wall %.1f m behind the player (%s): lens ends %.2f m in front of the wall (min %.2f), %.2f m from the head, frames with the lens inside a solid %d",
            wallZ, FixedName(fixed), gap.Last(), gap.Min(), dist.Last(), inside);
    }
    // (b) strafing past a fence behind the player: does the camera pump in and out?
    for (float slat : {0.1f, 0.4f}) {
        Opt o; o.mode = Mode::Third; Rig r(o);
        for (int i = 0; i < 60; ++i) r.Box({-15.0f + i * (slat + 0.3f), 2, 3.0f}, {slat, 4, 0.1f}, Quat::Identity, "Slat");
        r.Spawn({-12, 0, 0}, PlayerView::View::Third);
        r.Look(-90.0f);                                              // forward +X: run along the fence
        r.Frames(0.5, {});
        Series d; int pops = 0;
        r.Frames(4.0, {'w'}, [&](Rig& r) {
            const double dd = (r.cam->transform->Position() - (r.pos() + Vec3{0, 1.5f, 0})).Magnitude();
            if (d.N() && std::fabs(dd - d.Last()) > 0.4) ++pops;
            d.Add(dd);
        });
        Out("E18b", pops < 4 ? "ok" : "limit", "run beside a fence of %.2f m slats 0.3 m apart (the camera is on the far side): arm length %.2f..%.2f m, sd %.2f, %d jumps over 0.4 m in 4 s",
            slat, d.Min(), d.Max(), d.Stdev(), pops);
    }
    // (c) looking up from low pitch near the floor; (d) in a 1.2 m wide corridor, camera behind
    {
        Opt o; o.mode = Mode::Third; Rig r(o);
        r.Box({-0.9f, 2, 0}, {0.6f, 4, 30}, Quat::Identity, "WallL"); r.Box({0.9f, 2, 0}, {0.6f, 4, 30}, Quat::Identity, "WallR");
        r.Box({0, 4.2f, 0}, {4, 0.4f, 30}, Quat::Identity, "Roof");
        r.Spawn({0, 0, 0}, PlayerView::View::Third);
        int inside = 0; Series d;
        r.Frames(3.0, {'w'}, [&](Rig& r) { const Vec3 c = r.cam->transform->Position(); d.Add((c - (r.pos() + Vec3{0, 1.5f, 0})).Magnitude()); if (CameraInSolid(r, c)) ++inside; });
        Out("E18c", inside == 0 ? "ok" : "BUG", "walk down a 1.2 m wide, 4 m high corridor: camera arm %.2f..%.2f m, frames with the lens inside a solid %d", d.Min(), d.Max(), inside);
    }
}

static void E26_ViewToggle() {
    for (bool wall : {false, true}) {
        Opt o; o.mode = Mode::Both; Rig r(o);
        if (wall) r.Box({0, 3, 1.5f + 0.25f}, {20, 6, 0.5f}, Quat::Identity, "Wall");
        r.Spawn({0, 0, 0}, PlayerView::View::Third);
        r.Frames(1.0, {});
        Series step; int inside = 0; Vec3 prev = r.cam->transform->Position();
        r.Frame({'v'});
        r.Frames(0.8, {}, [&](Rig& r) { const Vec3 c = r.cam->transform->Position(); step.Add((c - prev).Magnitude()); prev = c; if (CameraInSolid(r, c)) ++inside; });
        const bool first = FirstPersonOwns(r.p);
        Series step2; Vec3 prev2 = r.cam->transform->Position(); int inside2 = 0;
        r.Frame({'v'});
        r.Frames(0.8, {}, [&](Rig& r) { const Vec3 c = r.cam->transform->Position(); step2.Add((c - prev2).Magnitude()); prev2 = c; if (CameraInSolid(r, c)) ++inside2; });
        Out("E26", inside + inside2 == 0 && step.Max() < 1.2 ? "ok" : "check", "V toggle third->first->third%s: after 1st switch FirstPersonOwns=%d, biggest camera step per frame %.2f m (->eyes) / %.2f m (->orbit), frames with lens in a solid %d",
            wall ? " with a wall 1.5 m behind" : "", first, step.Max(), step2.Max(), inside + inside2);
    }
}

// =================================================================== water
static void E19_Water() {
    for (int shore = 0; shore < 3; ++shore) for (Mode m : {Mode::First, Mode::Third}) {
        Opt o; o.ground = false; o.mode = m; Rig r(o);
        GameObject* w = r.sc->CreateGameObject("Water");
        auto* wa = w->AddComponent<Water>(); wa->size = 20.0f; wa->depth = 4.0f; wa->waveHeight = 0.0f; wa->resolution = 8;
        r.Box({0, -4.5f, 0}, {20, 1, 20}, Quat::Identity, "Bed");
        if (shore == 0) r.Box({20.0f, -2.0f, 0}, {20, 4.6f, 20}, Quat::Identity, "Bank 0.3 m above water");        // face x = 10, top y = 0.3
        if (shore == 1) r.Box({20.0f, -2.0f, 0}, {20, 3.8f, 20}, Quat::Identity, "Bank flush with water");           // top y = -0.1
        if (shore == 2) r.MeshOf(Mesh::Wedge(1.0f), {16.0f, -1.9f, 0}, Quat::AngleAxis(-90.0f, Vec3::Up), {20.0f, 4.2f, 12.0f}, "Beach");   // a long beach rising toward +X
        r.Spawn({0, 1.5f, 0}, m == Mode::Third ? PlayerView::View::Third : PlayerView::View::First);
        r.Look(-90.0f);                                                  // +X
        Series chest; double tSwim = -1; bool swimming = false;
        r.Frames(2.5, {}, [&](Rig& r) {
            const bool sw = m == Mode::First ? r.fp->IsSwimming() : r.tp->IsSwimming();
            if (sw && tSwim < 0) tSwim = r.time; swimming = sw; chest.Add(r.pos().y + 1.15f);
        });
        const double yIdle = r.pos().y;
        double xReach = 0; Series sp;
        r.Frames(8.0, {'w'}, [&](Rig& r) { xReach = std::max(xReach, (double)r.pos().x); sp.Add(Len2(r.rb->velocity)); });
        const bool out = r.pos().x > 11.0 && r.pos().y > -0.3;
        Out("E19", shore == 2 ? (out ? "ok" : "limit") : "info",
            "%s person, %s: falls in and swims after %.2fs, floats with chest %.2f (surface 0), idle y %.2f; swims +X for 8 s: reached x %.1f, end y %.2f, swim speed %.2f -> %s",
            m == Mode::First ? "1st" : "3rd", shore == 0 ? "bank 0.3 m above the water" : shore == 1 ? "bank flush with the water" : "gradual beach", tSwim, chest.Last(), yIdle, xReach, r.pos().y, sp.Median(),
            out ? "gets out" : "STAYS IN THE WATER");
    }
}

// =================================================================== terrain
static void E20_Voxel() {
    for (int what = 0; what < 5; ++what) {
        Opt o; o.ground = false; Rig r(o);
        GameObject* g = r.sc->CreateGameObject("Voxels");
        auto* v = g->AddComponent<VoxelTerrain>();
        v->FillSlab(0.4f);
        float sy = 0; v->SurfaceY(0, 0, sy);
        r.sc->Start();                                                       // build the mesh
        r.Spawn({0, sy + 0.03f, 0});
        r.Frames(1.0, {});
        const double y0 = r.pos().y;
        if (what == 0) {
            Series y, vy; int air = 0, n = 0;
            r.Frames(5.0, {'w'}, [&](Rig& r) { y.Add(r.pos().y); vy.Add(r.rb->velocity.y); ++n; if (!r.grounded()) ++air; });
            Out("E20a", air == 0 ? "ok" : "BUG", "voxel flat: walk 5 s: %.1f m covered, body height range %.4f, not-grounded %d/%d frames", -r.pos().z, y.Max() - y.Min(), air, n);
        } else if (what == 1) {
            double tFall = -1; Series y;
            for (int k = 0; k < 20; ++k) v->Dig({0, sy, 0}, 2.0f, 1.0f);
            v->Apply();
            r.Frames(3.0, {}, [&](Rig& r) { y.Add(r.pos().y); if (tFall < 0 && r.pos().y < y0 - 0.3) tFall = r.time; });
            float sy2 = 0; v->SurfaceY(0, 0, sy2);
            Out("E20b", y.Last() < y0 - 0.5 ? "ok" : "BUG", "voxel: floor dug away under the idle player (radius 2): starts falling after %.2fs, end y %.2f (was %.2f, new surface %.2f)", tFall, y.Last(), y0, sy2);
        } else if (what == 2) {
            double tOut = -1; Series y;
            for (int k = 0; k < 3; ++k) v->Add({0, sy + 1.0f, 0}, 2.0f, 1.0f);   // a mound grows over the player
            v->Apply();
            float sy2 = 0; v->SurfaceY(0, 0, sy2);
            r.Frames(3.0, {}, [&](Rig& r) { y.Add(r.pos().y); if (tOut < 0 && r.pos().y > sy2 - 0.2) tOut = r.time; });
            Out("E20c", y.Last() > sy2 - 0.3 ? "ok" : "BUG", "voxel: mound added over the player (centre 1 m above the feet, radius 2): new surface %.2f, player end y %.2f after %.2fs (surfaces out %s)", sy2, y.Last(), tOut, tOut >= 0 ? "yes" : "NO, STILL BURIED");
        } else if (what == 3) {
            double tOut = -1; Series y;
            for (int k = 0; k < 6; ++k) v->Add({0, sy + 1.2f, 0}, 3.5f, 1.0f);   // a big mound that swallows the whole body
            v->Apply();
            float sy2 = 0; v->SurfaceY(0, 0, sy2);
            r.Frames(4.0, {}, [&](Rig& r) { y.Add(r.pos().y); if (tOut < 0 && r.pos().y > sy2 - 0.2) tOut = r.time; });
            Out("E20d", y.Last() > sy2 - 0.3 ? "ok" : "BUG", "voxel: big mound (radius 3.5) buries the body: new surface %.2f, player end y %.2f (peak speed seen: see y range %.2f..%.2f), out after %.2fs", sy2, y.Last(), y.Min(), y.Max(), tOut);
        } else {
            // a slope in the voxel field: raise a ramp by adding spheres along a line, then check walking up and the slope limit
            for (int i = 0; i < 24; ++i) { const float z = -i * 0.5f; v->Add({0, sy + i * 0.5f, z}, 1.8f, 1.0f); }
            v->Apply();
            Series y;
            r.Frames(5.0, {'w'}, [&](Rig& r) { y.Add(r.pos().y); });
            Out("E20e", "info", "voxel ramp (spheres added along a line): walk uphill 5 s: climbed %.2f m, end z %.2f, peak y %.2f", y.Last() - y0, r.pos().z, y.Max());
        }
    }
}

static void E27_FloorGone() {
    for (int how = 0; how < 3; ++how) {
        Opt o; o.ground = false; Rig r(o);
        GameObject* f = r.Box({0, -0.5f, 0}, {20, 1, 20}, Quat::Identity, "Floor");
        r.Spawn({0, 0, 0});
        r.Frames(2.0, {});                                               // settle: the idle body falls asleep
        const bool asleep = r.rb->sleeping;
        if (how == 0) r.sc->Destroy(f);
        else if (how == 1) f->GetComponent<Collider3D>()->enabled = false;
        else f->active = false;
        double tFall = -1;
        r.Frames(2.0, {}, [&](Rig& r) { if (tFall < 0 && r.pos().y < -0.3) tFall = r.time; });
        Out("E27", tFall >= 0 ? "ok" : "BUG", "box floor %s under an idle player (asleep=%d): %s (end y %.2f after 2 s)", how == 0 ? "destroyed" : how == 1 ? "collider disabled" : "object deactivated", asleep,
            tFall >= 0 ? "falls" : "HOVERS IN THE AIR", r.pos().y);
    }
}

static void E21_TerrainEdge() {
    Opt o; o.ground = false; Rig r(o);
    GameObject* g = r.sc->CreateGameObject("Terrain"); auto* t = g->AddComponent<Terrain>(); t->Resize(32); t->size = 50.0f; t->Flatten(0.0f);
    r.Spawn({0, 0, 0});
    r.Look(-90.0f);
    r.Frames(0.5, {});
    double tEdge = -1, tReset = -1, minY = 0; int flick = 0; bool prevG = true;
    r.Frames(14.0, {'w'}, [&](Rig& r) {
        if (tEdge < 0 && r.pos().x > 25.0f) tEdge = r.time;
        minY = std::min(minY, (double)r.pos().y);
        if (tReset < 0 && minY < -50 && r.pos().y > -1.0f) tReset = r.time;
        if (r.grounded() != prevG) ++flick; prevG = r.grounded();
    });
    Out("E21", tReset >= 0 ? "info" : "limit", "heightmap terrain 50 m wide: walks off the edge at %.1fs, falls to y=%.0f, respawned at the start after %.1fs (Fall Reset Y -100); grounded flips %d", tEdge, minY, tReset, flick);
}

// =================================================================== scale, teleport, remote proxies
static void E22_Scale() {
    for (float sc : {0.5f, 1.0f, 2.0f}) for (float h : {0.15f, 0.3f, 0.6f}) {
        Opt o; o.scale = sc; Rig r(o);
        r.Box({0, h * 0.5f, -5.0f * sc}, {6, h, 6}, Quat::Identity, "Step");
        r.Spawn({0, 0, 0});
        r.Frames(3.5, {'w'});
        const bool climbed = r.pos().y > h - 0.06f && r.pos().z < -2.0f * sc;
        Out("E22", "info", "player scale %.1f, %.2f m step (= %.2f body heights... of 1.8 m x scale): %s (end y %.2f)", sc, h, h / (1.8f * sc), climbed ? "climbs" : "blocked", r.pos().y);
    }
    for (float sc : {0.5f, 2.0f}) {
        Opt o; o.scale = sc; Rig r(o); r.Spawn({0, 0, 0}, PlayerView::View::First);
        r.Frames(1.0, {});
        const float eye = r.cam->transform->Position().y - r.pos().y;
        Out("E22b", "info", "player scale %.1f: eye height %.2f above the soles (body %.2f)", sc, eye, 1.8f * sc);
    }
}

static void E23_Teleport() {
    for (Mode m : {Mode::First, Mode::Third}) {
        Opt o; o.mode = m; Rig r(o); r.Spawn({0, 0, 0}, m == Mode::Third ? PlayerView::View::Third : PlayerView::View::First);
        r.Frames(1.0, {'w', kShift});
        r.p->transform->SetPosition({60, 0, 0});
        Series d; int bad = 0;
        r.Frames(0.7, {'w', kShift}, [&](Rig& r) { const double dd = (r.cam->transform->Position() - r.pos()).Magnitude(); d.Add(dd); if (dd > 8.0) ++bad; });
        Out("E23", bad == 0 ? "ok" : "BUG", "%s person: teleport 60 m while running: camera to body %.2f..%.2f m afterwards, frames with the camera >8 m away %d (velocity kept %.2f)",
            m == Mode::First ? "1st" : "3rd", d.Min(), d.Max(), bad, Len2(r.rb->velocity));
    }
    {   // fall reset
        Opt o; o.ground = false; Rig r(o); r.Spawn({3, 5, 4});
        r.Frames(0.2, {});
        r.rb->velocity = {0, 0, 0};
        r.p->transform->SetPosition({3, -120, 4});   // as if it fell through
        r.Frames(0.3, {});
        Out("E23b", "info", "fell below Fall Reset Y: returned to (%.1f, %.1f, %.1f) (spawn was 3,5,4), vy %.2f", r.pos().x, r.pos().y, r.pos().z, r.rb->velocity.y);
    }
}

static void E24_RemoteProxy() {
    // trial 0: remote third-person avatar in free space; 1: remote first-person avatar overlapping a wall;
    // 2: remote avatar with BOTH views (view toggle on) while the local player presses V
    for (int trial = 0; trial < 3; ++trial) {
        Opt o; o.mode = Mode::Third; Rig r(o);
        r.sc->CreateGameObject("Net")->AddComponent<NetworkManager>();
        GameObject* rem = Templates::BuildPlayerBody(*r.sc, {20, 0, 0}, "Remote");
        PlayerController::Add(rem, trial == 0 ? Mode::Third : trial == 1 ? Mode::First : Mode::Both, PlayerView::View::Third);
        auto* ns = rem->AddComponent<NetworkSync>(); ns->authority = NetworkSync::Authority::Manual; ns->owned = false;
        if (trial == 1) r.Box({20.2f, 2, 0}, {0.4f, 4, 4}, Quat::Identity, "WallInsideRemote");   // a wall overlapping the remote avatar
        r.Spawn({0, 0, 0}, PlayerView::View::Third);
        r.Frames(2.0, {});
        if (trial == 2) { r.Frame({'v'}); r.Frames(1.0, {}); }
        const double camToLocal = (r.cam->transform->Position() - (r.pos() + Vec3{0, 1.5f, 0})).Magnitude();
        const double camToRemote = (r.cam->transform->Position() - (rem->transform->Position() + Vec3{0, 1.5f, 0})).Magnitude();
        const Vec3 rp = rem->transform->Position();
        Out("E24", camToLocal < 8.0 && std::fabs(rp.x - 20.0) < 0.05 ? "ok" : "BUG",
            "%s: main camera %.2f m from the LOCAL player, %.2f m from the remote avatar; remote avatar now at (%.2f, %.2f, %.2f) from (20, 0, 0)",
            trial == 0 ? "remote third-person avatar, free space" : trial == 1 ? "remote first-person avatar overlapping a wall" : "remote both-views avatar, local player presses V",
            camToLocal, camToRemote, rp.x, rp.y, rp.z);
    }
}

// Floors whose pivot is not below the player's pivot: does a second jump work?
static void E28_PivotFloors() {
    for (int kind = 0; kind < 4; ++kind) for (Mode m : {Mode::First, Mode::Third}) {
        Opt o; o.ground = false; o.mode = m; Rig r(o);
        GameObject* f = r.sc->CreateGameObject("Floor");
        const char* name = "";
        if (kind == 0) { name = "pivot at the middle of a 1 m slab (normal)"; f->transform->localPosition = {0, -0.5f, 0}; f->AddComponent<BoxCollider3D>()->size = {40, 1, 40}; }
        else if (kind == 1) { name = "pivot 5 m ABOVE the surface (collider offset -5.5)"; f->transform->localPosition = {0, 5.0f, 0}; auto* c = f->AddComponent<BoxCollider3D>(); c->size = {40, 1, 40}; c->offset = {0, -5.5f, 0}; }
        else if (kind == 2) { name = "pivot level with the surface (a 1 m slab hung from its top)"; f->transform->localPosition = {0, 0.0f, 0}; auto* c = f->AddComponent<BoxCollider3D>(); c->size = {40, 1, 40}; c->offset = {0, -0.5f, 0}; }
        else { name = "mesh floor, pivot 3 m above"; f->transform->localPosition = {0, 3.0f, 0}; f->AddComponent<MeshRenderer>()->mesh = Mesh::Plane(40.0f); f->transform->localPosition = {0, 3.0f, 0}; auto* mc = f->AddComponent<MeshCollider3D>(); (void)mc; f->transform->localPosition = {0, 0.0f, 0}; }
        r.Spawn({0, 0.02f, 0}, m == Mode::Third ? PlayerView::View::Third : PlayerView::View::First);
        r.Frames(0.8, {});
        int jumps = 0;
        for (int k = 0; k < 4; ++k) {
            r.Frame({' '}); r.Frame({' '});
            if (r.rb->velocity.y > 4.0) ++jumps;
            r.Frames(1.4, {});
        }
        Out("E28", jumps == 4 ? "ok" : "BUG", "%s person, floor %s: 4 jumps tried, %d worked, standing y %.3f", m == Mode::First ? "1st" : "3rd", name, jumps, r.pos().y);
    }
}

// The sign and direction of the contact normal the controllers get in OnCollisionEnter/Stay3D.
namespace { struct NormalSpy : Component {
    Vec3 last{0, 0, 0}; int n = 0; Vec3 sum{0, 0, 0};
    void OnCollisionEnter3D(const Collision3D& c) override { last = c.normal; sum = sum + c.normal; ++n; }
    void OnCollisionStay3D(const Collision3D& c) override { last = c.normal; sum = sum + c.normal; ++n; }
}; }
static void E29_ContactNormals() {
    for (int what = 0; what < 6; ++what) for (bool capsule : {false, true}) {
        Opt o; o.ground = false; o.capsule = capsule; o.camera = false; o.bare = true; o.mode = Mode::First;
        Rig r(o);
        const char* name = "";
        if (what == 0) { name = "box floor (body above)"; r.Box({0, -0.5f, 0}, {40, 1, 40}); }
        else if (what == 1) { name = "mesh floor (body above)"; r.MeshBox({0, -0.5f, 0}, {40, 1, 40}); }
        else if (what == 2) { name = "box ceiling at 1.9 (body below)"; r.Box({0, 2.2f, 0}, {40, 0.6f, 40}); r.Box({0, -0.5f, 0}, {40, 1, 40}); }
        else if (what == 3) { name = "box wall at z=-0.4 (body beside)"; r.Box({0, 2, -0.7f}, {40, 4, 0.6f}); r.Box({0, -0.5f, 0}, {40, 1, 40}); }
        else if (what == 4) { name = "box ramp 30 deg"; r.Box(Vec3{0, -0.25f * std::cos(0.5236f), -0.25f * std::sin(0.5236f)}, {40, 0.5f, 120}, Quat::AngleAxis(30.0f, Vec3::Right)); }
        else { name = "mesh ramp 30 deg"; r.MeshBox(Vec3{0, -0.25f * std::cos(0.5236f), -0.25f * std::sin(0.5236f)}, {40, 0.5f, 120}, Quat::AngleAxis(30.0f, Vec3::Right)); }
        r.Spawn({0, what == 4 || what == 5 ? 0.3f * 0.5774f + 0.05f : 0.05f, 0});
        auto* spy = r.p->AddComponent<NormalSpy>();
        if (what == 2) r.rb->velocity = {0, 0, 0};
        r.Frames(1.0, {});
        if (what == 3) r.Frames(1.0, {'w'});
        if (what == 2) { r.Frame({' '}); r.Frames(1.0, {}); }
        Out("E29", "info", "%-8s %-34s: callbacks %3d, last normal (%.2f, %.2f, %.2f) [toward the other collider]", capsule ? "capsule" : "box", name, spy->n, spy->last.x, spy->last.y, spy->last.z);
    }
}

// =================================================================== main
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) g_filters.emplace_back(argv[i]);
    if (Wanted("E01")) E01_WallPerpendicular();
    if (Wanted("E02")) E02_WallAngle();
    if (Wanted("E04")) E04_ThinWall();
    if (Wanted("E05")) E05_Slopes();
    if (Wanted("E06")) E06_Steps();
    if (Wanted("E07")) E07_Stairs();
    if (Wanted("E08")) E08_Ledge();
    if (Wanted("E09")) E09_CrouchCeiling();
    if (Wanted("E10")) E10_JumpCeiling();
    if (Wanted("E11")) E11_Seams();
    if (Wanted("E12")) { E12_Platforms(); E12b_JumpOffPlatform(); E12c_Pushed(); }
    if (Wanted("E13")) E13_RigidBodies();
    if (Wanted("E14")) E14_Depen();
    if (Wanted("E15")) E15_Fall();
    if (Wanted("E17")) E17_FrameRate();
    if (Wanted("E18")) E18_Camera();
    if (Wanted("E19")) E19_Water();
    if (Wanted("E20")) E20_Voxel();
    if (Wanted("E21")) E21_TerrainEdge();
    if (Wanted("E22")) E22_Scale();
    if (Wanted("E23")) E23_Teleport();
    if (Wanted("E24")) E24_RemoteProxy();
    if (Wanted("E26")) E26_ViewToggle();
    if (Wanted("E28")) E28_PivotFloors();
    if (Wanted("E29")) E29_ContactNormals();
    if (Wanted("E27")) E27_FloorGone();
    return 0;
}
