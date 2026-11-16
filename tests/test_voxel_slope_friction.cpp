#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>

using namespace okay;

// Friction on voxel terrain (Physics3D section 5, the voxel contact pass).
//
// Bug: in the Sandbox template the player crept downhill at about 0.2 m/s while
// standing still on a slope. Box and mesh contacts apply Coulomb friction; the
// voxel pass only removed the velocity into the surface, so the slope's pull
// stayed as sideways speed, and it never filled Rigidbody3D::groundNormal.
//
// Written from Coulomb's law of friction (a body on a plane holds while
// tan(angle) <= mu, else slides with a = g (sin - mu cos)) and from this engine's
// own contact code: the mesh-collider pass in Physics3D.cpp treats the static
// surface as friction 0.6 and combines it with the body's as sqrt(a * b).

namespace {

const float kDt = 1.0f / 60.0f;
const float kDeg = 3.14159265358979f / 180.0f;

// A planar voxel slope rising toward +x at `deg` degrees, filled the way
// VoxelTerrain::Generate fills the Sandbox hills: density = surface height - y,
// clamped, so everything below the surface is solid.
VoxelTerrain* VoxelSlope(Scene& s, float deg, int nx, int ny, float baseY) {
    GameObject* go = s.CreateGameObject("Slope");
    auto* v = go->AddComponent<VoxelTerrain>();
    v->voxelSize = 1.0f;
    v->Resize(nx, ny, 13);
    const float c = v->Clamp(), k = std::tan(deg * kDeg);
    for (int z = 0; z < v->nz; ++z)
        for (int y = 0; y < v->ny; ++y)
            for (int x = 0; x < v->nx; ++x) {
                Vec3 p = v->GridToLocal(x, y, z);
                float d = baseY + p.x * k - p.y;
                v->Set(x, y, z, d < -c ? -c : (d > c ? c : d));
            }
    v->Apply();
    return v;
}

// A capsule body (radius 0.3, 1.8 tall) with its feet at `feet`. Default
// Rigidbody3D: friction 0.5, rotation frozen.
GameObject* CapsuleBody(Scene& s, Vec3 feet) {
    GameObject* b = s.CreateGameObject("Capsule");
    b->transform->localPosition = feet;
    b->AddComponent<Rigidbody3D>();
    auto* cc = b->AddComponent<CapsuleCollider3D>();
    cc->radius = 0.3f; cc->height = 1.8f; cc->offset = {0, 0.9f, 0};
    return b;
}

// The Sandbox template's player (Templates::VoxelSandbox): a part rig, a box
// collider body and the first-person Player Controller.
GameObject* SandboxPlayer(Scene& s, Vec3 feet) {
    GameObject* player = s.CreateGameObject("Player");
    player->transform->localPosition = feet;
    { auto* pc = player->AddComponent<Character>(); pc->Apply(); pc->separateParts = true; }
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    auto* col = player->AddComponent<BoxCollider3D>();
    col->size = {0.6f, 1.8f, 0.6f};
    col->offset = {0.0f, 0.9f, 0.0f};
    PlayerController::Add(player, PlayerController::ViewMode::First)->first()->zoomWithRightMouse = false;
    return player;
}

void Run(Scene& s, int frames) { for (int i = 0; i < frames; ++i) s.Update(kDt); }

float Horiz(const Vec3& a, const Vec3& b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// Outward normal of a slope rising toward +x: it leans downhill (-x).
Vec3 SlopeNormal(float deg) { return {-std::sin(deg * kDeg), std::cos(deg * kDeg), 0.0f}; }

struct Hold { float settleDrift, laterDrift; Vec3 normal; bool grounded; };

// Stand a body still on a voxel slope: 1 s to land and settle, then 2 s more.
Hold StandOnSlope(float deg, bool player) {
    Scene s("slope");
    s.Start();
    VoxelSlope(s, deg, 33, 25, 12.0f);
    GameObject* b = player ? SandboxPlayer(s, {0, 12.02f, 0}) : CapsuleBody(s, {0, 12.02f, 0});
    auto* rb = b->GetComponent<Rigidbody3D>();
    const Vec3 start = b->transform->Position();
    Run(s, 60);
    const Vec3 settled = b->transform->Position();
    Run(s, 120);
    const Vec3 end = b->transform->Position();
    Hold h{Horiz(start, settled), Horiz(settled, end), rb->groundNormal, rb->groundedOnTerrain};
    std::printf("  %s on %.0f deg voxels: settle drift %.4f m, then %.4f m in 2 s, normal (%.3f %.3f %.3f)%s%s\n",
                player ? "Sandbox player" : "capsule", deg, h.settleDrift, h.laterDrift,
                h.normal.x, h.normal.y, h.normal.z, h.grounded ? ", grounded" : "", rb->sleeping ? ", asleep" : "");
    return h;
}

// Slide a capsule down a 45 degree slope for 3 s from rest; how far it got along x.
float SlideVoxel(float deg) {
    Scene s("slide-voxel");
    s.Start();
    VoxelSlope(s, deg, 49, 49, 24.0f);
    const float x0 = 12.0f, y0 = 24.0f + x0 * std::tan(deg * kDeg);
    GameObject* b = CapsuleBody(s, {x0, y0 + 0.02f, 0});
    Run(s, 180);
    return x0 - b->transform->Position().x;
}
float SlideMesh(float deg) {
    Scene s("slide-mesh");
    s.Start();
    GameObject* ramp = s.CreateGameObject("Ramp");
    ramp->AddComponent<MeshRenderer>()->mesh = Mesh::Plane(60.0f);
    ramp->AddComponent<MeshCollider3D>();
    ramp->transform->localRotation = Quat::AngleAxis(deg, {0, 0, 1});   // rises toward +x
    const float x0 = 12.0f, t = std::tan(deg * kDeg), c = std::cos(deg * kDeg);
    GameObject* b = CapsuleBody(s, {x0, x0 * t + 0.3f / c - 0.3f + 0.02f, 0});
    Run(s, 180);
    return x0 - b->transform->Position().x;
}

// Shove a capsule along a flat floor at 3 m/s; how far it goes before it stops.
enum class Floor { Voxel, Box, Mesh };
float ShoveDistance(Floor f) {
    Scene s("shove");
    s.Start();
    float top = 0.0f;
    if (f == Floor::Voxel) {
        VoxelSlope(s, 0.0f, 33, 25, 12.0f);
        top = 12.0f;
    } else {
        GameObject* g = s.CreateGameObject("Floor");
        g->transform->localPosition = {0, -0.5f, 0};
        g->transform->localScale = {40, 1, 40};
        if (f == Floor::Box) g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        else { g->AddComponent<MeshRenderer>()->mesh = Mesh::Cube(1.0f); g->AddComponent<MeshCollider3D>(); }
    }
    GameObject* b = CapsuleBody(s, {-6.0f, top + 0.01f, 0});
    auto* rb = b->GetComponent<Rigidbody3D>();
    Run(s, 30);                                   // land
    const float x0 = b->transform->Position().x;
    rb->velocity = {3.0f, rb->velocity.y, 0.0f};
    rb->WakeUp();
    Run(s, 150);
    return b->transform->Position().x - x0;
}

} // namespace

int main() {
    RUN_SUITE("voxel slope friction");

    const float mu = std::sqrt(0.5f * 0.6f);                 // default body vs a static surface
    const float limitDeg = std::atan(mu) / kDeg;             // ~28.7 degrees
    std::printf("  friction limit: %.1f deg\n", limitDeg);

    // --- A capsule standing still on gentle voxel slopes stays put ----------
    for (float deg : {12.0f, 20.0f, 25.0f}) {
        Hold h = StandOnSlope(deg, false);
        CHECK(h.laterDrift < 0.01f);                         // no creep once settled
        CHECK(h.settleDrift < 0.05f);                        // and only a small nudge while landing
        CHECK(h.grounded);
        CHECK(h.normal.y < 0.995f);                          // the slope's normal, not the default up
        CHECK(Vec3::Dot(h.normal, SlopeNormal(deg)) > 0.995f);   // tilted the way the slope is
    }

    // --- The Sandbox player (Player Controller, no input) stays put too -----
    {
        Hold h = StandOnSlope(20.0f, true);
        CHECK(h.laterDrift < 0.01f);
        CHECK(h.grounded);
        CHECK(Vec3::Dot(h.normal, SlopeNormal(20.0f)) > 0.995f);
    }

    // --- The real Sandbox template: its player put on a hillside of the
    // generated terrain, above the lake, released with no input -------------
    {
        Scene s("sandbox");
        Templates::VoxelSandbox(s);
        GameObject* player = s.Find("Player");
        auto* vox = s.FindObjectOfType<VoxelTerrain>();
        GameObject* lake = s.Find("Water");
        CHECK(player && vox && lake);
        if (player && vox && lake) {
            const float waterY = lake->transform->Position().y;
            const Vec3 vp = vox->gameObject->transform->Position();
            // A spot tilted 14 to 22 degrees whose ground 1 m around tilts the same way.
            bool found = false; Vec3 spot, nrm;
            for (float z = -vox->HalfZ() + 10.0f; z <= vox->HalfZ() - 10.0f && !found; z += 1.0f)
                for (float x = -vox->HalfX() + 10.0f; x <= vox->HalfX() - 10.0f && !found; x += 1.0f) {
                    float y = 0.0f;
                    if (!vox->SurfaceY(x, z, y) || y + vp.y < waterY + 3.0f) continue;
                    const Vec3 n = vox->SurfaceNormal({x, y, z});
                    const float tilt = std::acos(n.y) / kDeg;
                    if (tilt < 14.0f || tilt > 22.0f) continue;
                    bool even = true;
                    for (int k = 0; k < 4 && even; ++k) {
                        const float ox = (k == 0) - (k == 1), oz = (k == 2) - (k == 3);
                        float y2 = 0.0f;
                        if (!vox->SurfaceY(x + ox, z + oz, y2)) { even = false; break; }
                        if (Vec3::Dot(vox->SurfaceNormal({x + ox, y2, z + oz}), n) < 0.985f) even = false;
                    }
                    if (even) { found = true; spot = Vec3{x, y, z} + vp; nrm = n; }
                }
            CHECK(found);
            if (found) {
                auto* rb = player->GetComponent<Rigidbody3D>();
                player->transform->localPosition = {spot.x, spot.y + 0.05f, spot.z};
                rb->velocity = Vec3::Zero;
                rb->WakeUp();
                Run(s, 60);
                const Vec3 settled = player->transform->Position();
                Run(s, 120);
                const float drift = Horiz(settled, player->transform->Position());
                std::printf("  Sandbox template player on a %.1f deg hillside at (%.1f, %.1f, %.1f): "
                            "%.4f m in 2 s after settling, normal (%.3f %.3f %.3f)%s\n",
                            std::acos(nrm.y) / kDeg, spot.x, spot.y, spot.z, drift,
                            rb->groundNormal.x, rb->groundNormal.y, rb->groundNormal.z,
                            rb->sleeping ? ", asleep" : "");
                CHECK(drift < 0.01f);
                CHECK(rb->groundedOnTerrain);
                CHECK(Vec3::Dot(rb->groundNormal, nrm) > 0.98f);
            }
        }
    }

    // --- Steeper than the friction limit: it still slides (friction is not
    // infinite), and about as far as on a mesh ramp of the same angle -------
    {
        const float deg = 45.0f;
        float voxel = SlideVoxel(deg), mesh = SlideMesh(deg);
        // Coulomb: a = g (sin - mu cos) along the slope; x travel = that x cos.
        const float s = std::sin(deg * kDeg), c = std::cos(deg * kDeg);
        const float coulomb = 0.5f * 9.81f * (s - mu * c) * 3.0f * 3.0f * c;
        const float frictionless = 0.5f * 9.81f * s * 3.0f * 3.0f * c;
        std::printf("  45 deg slide in 3 s: voxel %.2f m, mesh %.2f m, Coulomb %.2f m, frictionless %.2f m\n",
                    voxel, mesh, coulomb, frictionless);
        CHECK(voxel > 1.0f);                                 // slides
        CHECK(voxel < 0.8f * frictionless);                  // but friction slows it
        CHECK(std::fabs(voxel - mesh) < 0.2f * mesh);        // like on a mesh ramp
    }

    // --- Flat voxels: a body at rest does not move, the normal is up --------
    {
        Hold h = StandOnSlope(0.0f, false);
        CHECK(h.settleDrift < 0.001f);
        CHECK(h.laterDrift < 0.001f);
        CHECK(h.grounded);
        CHECK(h.normal.y > 0.999f);
    }
    {
        Scene s("flat-height");
        s.Start();
        VoxelSlope(s, 0.0f, 33, 25, 12.0f);
        GameObject* b = CapsuleBody(s, {0, 12.5f, 0});
        Run(s, 180);
        std::printf("  flat voxels: capsule feet at %.4f (surface 12)\n", b->transform->Position().y);
        CHECK_NEAR(b->transform->Position().y, 12.0f, 0.05f);   // rests on the surface
    }

    // --- A shoved body slides to a stop on flat voxels as it does on a box or
    // mesh floor (same friction model and values) ----------------------------
    {
        float vox = ShoveDistance(Floor::Voxel), box = ShoveDistance(Floor::Box), mesh = ShoveDistance(Floor::Mesh);
        // Coulomb: v^2 / (2 mu g)
        std::printf("  shoved at 3 m/s: voxel %.3f m, box %.3f m, mesh %.3f m, Coulomb %.3f m\n",
                    vox, box, mesh, 9.0f / (2.0f * mu * 9.81f));
        CHECK(vox > 0.3f);
        CHECK(std::fabs(vox - box) < 0.15f * box);
        CHECK(std::fabs(vox - mesh) < 0.15f * mesh);
    }

    TEST_MAIN_RESULT();
}
