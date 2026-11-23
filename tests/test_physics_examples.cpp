// The physics example scenes (examples/physics/physics_upgrade, examples/physics_toys, examples/physics_materials) load and
// do what their labels say when they are played for a few seconds, headless.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/PhysicsToys.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace okay;
namespace fs = std::filesystem;
namespace okay { void RegisterPhysicsToyProperties(); namespace detail { void RegisterPhysicsToySerializers(); } }

namespace {
std::string FindExample(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int i = 0; i < 8; ++i, p = p.parent_path()) { if (fs::exists(p / "examples" / rel)) return (p / "examples" / rel).string(); if (!p.has_parent_path()) break; }
    return {};
}
bool Load(Scene& s, const std::string& rel) {
    const std::string path = FindExample(rel);
    if (path.empty()) return false;
    std::string err;
    if (!SceneSerializer::LoadFromFile(s, path, &err)) { std::cerr << "  load failed: " << err << "\n"; return false; }
    return true;
}
void Play(Scene& s, float seconds) { s.Start(); s.UseFixedStepPhysics(true); s.fixedTimeStep = 0.02f; const int k = (int)(seconds / 0.02f); for (int i = 0; i < k; ++i) s.Update(0.02f); }
GameObject* F(Scene& s, const std::string& n) { return s.Find(n); }
Vec3 P(GameObject* o) { return o ? o->transform->Position() : Vec3{1e9f, 1e9f, 1e9f}; }
float Tilt(GameObject* o) { Vec3 up = o->transform->Rotation() * Vec3{0, 1, 0}; return std::acos(std::max(-1.0f, std::min(1.0f, up.y))) * 57.29578f; }
}

int main() {
    RUN_SUITE("physics_examples");
    RegisterPhysicsToyProperties(); detail::RegisterPhysicsToySerializers();

    {   // ---- physics_materials: ice slides, rubber and sticky hold; a trampoline throws the ball higher than stone does
        Scene s;
        CHECK(Load(s, "physics_materials/physics_materials.okayscene"));
        auto start = [&](const char* n) { return P(F(s, n)); };
        const Vec3 ice0 = start("Ice crate"), rubber0 = start("Rubber crate"), sticky0 = start("Sticky crate"), slip0 = start("Slippery crate");
        float trampPeak = 0, stonePeak = 0; bool tUp = false, sUp = false;
        s.Start(); s.UseFixedStepPhysics(true); s.fixedTimeStep = 0.02f;
        for (int i = 0; i < 300; ++i) {
            s.Update(0.02f);
            GameObject* tb = F(s, "Trampoline ball"); GameObject* sb = F(s, "Stone ball");
            if (tb) { auto* r = tb->GetComponent<Rigidbody3D>(); if (r->velocity.y > 2.0f) tUp = true; if (tUp) trampPeak = std::max(trampPeak, tb->transform->Position().y); }
            if (sb) { auto* r = sb->GetComponent<Rigidbody3D>(); if (r->velocity.y > 1.0f) sUp = true; if (sUp) stonePeak = std::max(stonePeak, sb->transform->Position().y); }
        }
        const float dIce = (P(F(s, "Ice crate")) - ice0).Magnitude(), dRub = (P(F(s, "Rubber crate")) - rubber0).Magnitude();
        const float dSticky = (P(F(s, "Sticky crate")) - sticky0).Magnitude(), dSlip = (P(F(s, "Slippery crate")) - slip0).Magnitude();
        std::cout << "  crate moved: Ice " << dIce << ", Slippery " << dSlip << ", Rubber " << dRub << ", Sticky " << dSticky << "; ball peak: Trampoline " << trampPeak << ", Stone " << stonePeak << "\n";
        CHECK(dIce > 2.0f); CHECK(dSlip > 2.0f); CHECK(dRub < 0.3f); CHECK(dSticky < 0.3f);
        CHECK(trampPeak > stonePeak + 0.8f);
        for (auto& o : s.Objects()) if (o) { Vec3 p = o->transform->Position(); CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)); }
    }
    {   // ---- physics_toys: the belt carries, the pad bounces, the fields pull, the bomb breaks the crates
        Scene s;
        CHECK(Load(s, "physics_toys/physics_toys.okayscene"));
        CHECK(F(s, "Conveyor Belt") && F(s, "Conveyor Belt")->GetComponent<ConveyorBelt>() != nullptr);   // the toys were saved and loaded
        CHECK(F(s, "Moving Platform") && F(s, "Moving Platform")->GetComponent<MovingPlatform>() != nullptr);
        const float z0 = P(F(s, "Belt crate")).z;
        float padPeak = 0, floatY = 0, normalY = 0, tramp = 0; int pieces = 0; bool broke = false;
        const Vec3 ball0 = P(F(s, "Float ball"));
        s.Start(); s.UseFixedStepPhysics(true); s.fixedTimeStep = 0.02f;
        for (int i = 0; i < 400; ++i) {
            s.Update(0.02f);
            if (GameObject* b = F(s, "Pad ball")) padPeak = std::max(padPeak, b->transform->Position().y);
            int n = 0; for (auto& o : s.Objects()) if (o && o->name.find("piece") != std::string::npos) ++n;
            pieces = std::max(pieces, n);
        }
        (void)broke; (void)tramp;
        floatY = P(F(s, "Float ball")).y; normalY = P(F(s, "Normal ball")).y;
        std::cout << "  belt crate moved z " << P(F(s, "Belt crate")).z - z0 << ", pad ball peak " << padPeak << ", float ball y " << floatY << " (start " << ball0.y << "), normal ball y " << normalY << ", most pieces " << pieces << "\n";
        CHECK(P(F(s, "Belt crate")).z - z0 > 3.0f);
        CHECK(padPeak > 5.0f);
        CHECK(std::fabs(floatY - ball0.y) < 0.5f);          // zero-g: still floating
        CHECK(normalY < 1.0f);                               // normal gravity: on the ground
        CHECK(pieces >= 8);                                  // the bomb broke at least one crate
    }
    {   // ---- physics_upgrade: the tower stands, the dominoes fall, the wrecking ball scatters the wall, the ramps differ
        Scene s;
        CHECK(Load(s, "physics/physics_upgrade.okayscene"));
        const Vec3 wall0 = P(F(s, "Wall crate")), ice0 = P(F(s, "Ice crate")), rub0 = P(F(s, "Rubber crate"));
        Play(s, 15.0f);
        float topY = 0; int towers = 0; for (auto& o : s.Objects()) if (o && o->name.rfind("Tower crate", 0) == 0) { topY = std::max(topY, P(o.get()).y); ++towers; }
        int fallen = 0, doms = 0; for (auto& o : s.Objects()) if (o && o->name.rfind("Domino", 0) == 0 && o->name.find("ramp") == std::string::npos && o->name.find("ball") == std::string::npos) { ++doms; fallen += Tilt(o.get()) > 60.0f; }
        float wallMoved = 0; for (auto& o : s.Objects()) if (o && o->name.rfind("Wall crate", 0) == 0) wallMoved = std::max(wallMoved, std::fabs(P(o.get()).x - 25.0f));
        const float dIce = (P(F(s, "Ice crate")) - ice0).Magnitude(), dRub = (P(F(s, "Rubber crate")) - rub0).Magnitude();
        std::cout << "  tower top y " << topY << " of " << towers << " crates, dominoes fallen " << fallen << "/" << doms << ", wall crate max move " << wallMoved << ", ice crate " << dIce << ", rubber crate " << dRub << "\n";
        (void)wall0;
        CHECK(towers == 10); CHECK_NEAR(topY, 9.5, 0.1);
        CHECK(doms == 16); CHECK(fallen >= 14);
        CHECK(wallMoved > 1.5f);
        CHECK(dIce > 2.0f); CHECK(dRub < 0.3f);
    }
    {   // ---- physics_engine (Engine quality): tower and pyramid stand, limbs stay attached, the arm is held, no shot passes the wall
        Scene s;
        CHECK(Load(s, "physics_engine/physics_engine.okayscene"));
        CHECK(s.physics3D().quality == PhysicsQuality::Engine);
        Play(s, 6.0f);
        float topY = 0; int towers = 0; for (auto& o : s.Objects()) if (o && o->name.rfind("Tower crate", 0) == 0) { topY = std::max(topY, P(o.get()).y); ++towers; }
        int shotsThrough = 0, shots = 0; for (auto& o : s.Objects()) if (o && o->name.rfind("Shot", 0) == 0) { ++shots; shotsThrough += P(o.get()).x > 30.0f; }
        float worstLimb = 0; for (auto& o : s.Objects()) if (o && o->name.rfind("Limb", 0) == 0) worstLimb = std::max(worstLimb, std::fabs(P(o.get()).y - 10.0f));
        GameObject* arm = F(s, "Powered arm");
        const float armTip = arm ? (arm->transform->Position() + arm->transform->Rotation() * Vec3{1.0f, 0, 0}).y : -1e9f;
        std::cout << "  engine: tower top y " << topY << " of " << towers << ", shots through the wall " << shotsThrough << "/" << shots
                  << ", powered arm tip y " << armTip << " (5.2), limbs moved down at most " << worstLimb << " m\n";
        CHECK(towers == 12); CHECK_NEAR(topY, 11.5, 0.1);
        CHECK(shots == 6); CHECK(shotsThrough == 0);
        CHECK(std::fabs(armTip - 5.2f) < 0.15f);
        CHECK(worstLimb < 6.0f);   // hanging chains: no link fell away (a link that came loose falls to the ground, 10 m down)
    }
    TEST_MAIN_RESULT();
}
