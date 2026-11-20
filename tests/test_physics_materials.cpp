// Physics Material asset: the .okayphysmat file, the built-in presets, a PhysicsSurface on an object, and what the solver
// does with it (ice slides, rubber holds, bounce height follows bounciness, density gives mass, a change at run time works,
// a contact tells scripts which two materials met). The numbers are in docs/physics/physics_materials.md.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/PhysicsSurface.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;

namespace {
struct World {
    Scene s{"w"}; float dt = 0.02f; int n = 0;
    World() { s.physics3D().quality = PhysicsQuality::Improved; }
    GameObject* Floor(const char* mat = nullptr) {
        auto* f = s.CreateGameObject("Floor"); f->transform->localPosition = {0, -0.5f, 0};
        f->AddComponent<BoxCollider3D>()->size = {200, 1, 200};
        if (mat) AssignPhysicsMaterial(*f, mat);
        return f;
    }
    /// A ramp of `deg` degrees rising toward +x, top surface through the origin-ish.
    GameObject* Ramp(float deg, const char* mat) {
        auto* r = s.CreateGameObject("Ramp"); r->transform->localPosition = {0, 0, 0};
        r->transform->localRotation = Quat::Euler(0, 0, deg);
        r->AddComponent<BoxCollider3D>()->size = {40, 1, 10};
        if (mat) AssignPhysicsMaterial(*r, mat);
        return r;
    }
    GameObject* Box(Vec3 p, const char* mat = nullptr) {
        auto* o = s.CreateGameObject("B" + std::to_string(n++)); o->transform->localPosition = p;
        o->AddComponent<BoxCollider3D>()->size = {1, 1, 1}; auto* r = o->AddComponent<Rigidbody3D>(); r->freezeRotation = true;
        if (mat) AssignPhysicsMaterial(*o, mat);
        return o;
    }
    void Run(float sec) { const int k = (int)std::lround(sec / dt); for (int i = 0; i < k; ++i) s.physics3D().Step(s, dt); }
};
Vec3 P(GameObject* o) { return o->transform->Position(); }
Rigidbody3D* R(GameObject* o) { return o->GetComponent<Rigidbody3D>(); }

// A box standing on the ramp surface, placed by hand at the surface point `along` metres up the slope.
GameObject* BoxOnRamp(World& w, float deg, float along, const char* boxMat) {
    const float a = deg * 3.14159265f / 180.0f;
    const Vec3 up{-std::sin(a), std::cos(a), 0}, dir{std::cos(a), std::sin(a), 0};
    GameObject* b = w.Box(dir * along + up * 1.0f + Vec3{0, 0.003f, 0}, boxMat);   // (the ramp is 1 m thick: its top is 0.5 m above its middle)
    b->transform->localRotation = Quat::Euler(0, 0, deg);
    return b;
}

struct Spy : Component {
    std::string seenOther, seenOwn;
    void OnCollisionEnter3D(const Collision3D& c) override {
        seenOther = c.surface ? c.surface->name : "none";
        seenOwn = c.ownSurface ? c.ownSurface->name : "none";
    }
};
}

int main() {
    RUN_SUITE("physics_materials");

    // ---- The file: written, read back, and rejected when it is not ours.
    {
        PhysicsMaterialData d;
        d.name = "Test Gel"; d.surface = "Gel"; d.staticFriction = 1.5f; d.dynamicFriction = 0.7f; d.bounciness = 0.35f;
        d.frictionCombine = CombineMode::Maximum; d.bounceCombine = CombineMode::Minimum; d.density = 1234.5f; d.rollingFriction = 0.2f;
        d.linearDamping = 0.5f; d.angularDamping = 1.5f; d.slopeGrip = 1.7f; d.impactSound = "splat"; d.slideSound = "squish"; d.hitEffect = "fx_goo";
        PhysicsMaterialData back;
        CHECK(PhysicsMaterials::FromText(PhysicsMaterials::ToText(d), back));
        CHECK(back.name == "Test Gel" && back.surface == "Gel" && back.impactSound == "splat" && back.slideSound == "squish" && back.hitEffect == "fx_goo");
        CHECK_NEAR(back.staticFriction, 1.5, 1e-6); CHECK_NEAR(back.dynamicFriction, 0.7, 1e-6); CHECK_NEAR(back.bounciness, 0.35, 1e-6);
        CHECK_NEAR(back.density, 1234.5, 1e-3); CHECK_NEAR(back.rollingFriction, 0.2, 1e-6); CHECK_NEAR(back.linearDamping, 0.5, 1e-6);
        CHECK_NEAR(back.angularDamping, 1.5, 1e-6); CHECK_NEAR(back.slopeGrip, 1.7, 1e-6);
        CHECK(back.frictionCombine == CombineMode::Maximum && back.bounceCombine == CombineMode::Minimum);
        CHECK(PhysicsMaterials::ToText(back) == PhysicsMaterials::ToText(d));   // exact round trip
        PhysicsMaterialData junk;
        CHECK(!PhysicsMaterials::FromText("hello\nname X\n", junk));              // not our file
        CHECK(!PhysicsMaterials::FromText("okayphysmat 99\nname X\n", junk));     // a newer version than we know
        CHECK(!PhysicsMaterials::FromText("okayphysmat 1\nbounciness 0.5\n", junk));   // no name
        PhysicsMaterialData tolerant;
        CHECK(PhysicsMaterials::FromText("okayphysmat 1\nname Odd\nbounciness 7\nfutureKey whatever\ndynamicFriction nan\n", tolerant));
        CHECK_NEAR(tolerant.bounciness, 1.0, 1e-6);                                // clamped
        CHECK_NEAR(tolerant.dynamicFriction, 0.5, 1e-6);                           // a bad number leaves the default
        // On disk, and found by a folder scan; a project material wins over a preset of the same name.
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_physmat_test";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir / "sub", ec);
        CHECK(PhysicsMaterials::SaveFile(d, (dir / "sub" / "gel.okayphysmat").string()));
        PhysicsMaterialData rubber2 = *PhysicsMaterials::Find("Rubber"); rubber2.bounciness = 0.11f;
        CHECK(PhysicsMaterials::SaveFile(rubber2, (dir / "rubber.okayphysmat").string()));
        CHECK(PhysicsMaterials::ScanFolder(dir.string()) == 2);
        CHECK(PhysicsMaterials::Find("test gel") != nullptr);
        CHECK_NEAR(PhysicsMaterials::Find("Rubber")->bounciness, 0.11, 1e-6);
        PhysicsMaterials::ClearProject();
        CHECK_NEAR(PhysicsMaterials::Find("Rubber")->bounciness, 0.60, 1e-6);   // the preset is back
        CHECK(PhysicsMaterials::Find("test gel") == nullptr);
        fs::remove_all(dir, ec);
    }
    // ---- The presets: all fourteen exist, have sane numbers, and rank the way people expect.
    {
        const char* names[] = {"Default", "Ice", "Rubber", "Bouncy Ball", "Metal", "Wood", "Stone", "Glass", "Mud", "Sand", "Sticky", "Slippery", "Trampoline", "Cloth"};
        for (const char* n : names) {
            const PhysicsMaterialData* m = PhysicsMaterials::Find(n);
            CHECK(m != nullptr);
            if (!m) continue;
            CHECK(m->staticFriction >= m->dynamicFriction);                // still grip is never less than sliding grip
            CHECK(m->bounciness >= 0.0f && m->bounciness <= 1.0f);
            CHECK(m->density > 100.0f && m->density < 20000.0f);
            CHECK(!PhysicsMaterials::SurfaceName(*m).empty());
        }
        auto d = [](const char* n) { return PhysicsMaterials::Find(n)->dynamicFriction; };
        CHECK(d("Ice") < d("Wood")); CHECK(d("Wood") < d("Rubber")); CHECK(d("Slippery") < d("Ice")); CHECK(d("Sticky") > d("Rubber"));
        CHECK(PhysicsMaterials::Find("Bouncy Ball")->bounciness > 0.85f); CHECK(PhysicsMaterials::Find("Stone")->bounciness < 0.15f);
        CHECK(PhysicsMaterials::Find("Metal")->density > PhysicsMaterials::Find("Wood")->density * 10.0f);
        CHECK(PhysicsMaterials::Find("ICE") == PhysicsMaterials::Find("ice"));      // any case
        CHECK(PhysicsMaterials::Find("nope") == nullptr);
    }
    // ---- Ice slides and rubber holds, on the same 20 degree ramp (Improved solver).
    {
        float slid[2];
        const char* mats[2] = {"Ice", "Rubber"};
        for (int i = 0; i < 2; ++i) {
            World w; w.Ramp(20.0f, mats[i]);
            GameObject* b = BoxOnRamp(w, 20.0f, 5.0f, nullptr);
            const Vec3 start = P(b);
            w.Run(2.0f);
            slid[i] = (P(b) - start).Magnitude();
            std::cout << "  20 degree ramp, " << mats[i] << ": moved " << slid[i] << " m in 2 s\n";
        }
        CHECK(slid[0] > 3.0f);        // ice: slides (free fall along the slope would be 6.7 m)
        CHECK(slid[1] < 0.05f);       // rubber: stays
    }
    // ---- Still grip is bigger than sliding grip: a crate set down on a 25 degree slope stays, a crate that is already
    // sliding keeps sliding (static 0.8, kinetic 0.3, the slope's tangent is 0.47).
    {
        PhysicsMaterialData g; g.name = "TestStatic"; g.staticFriction = 0.8f; g.dynamicFriction = 0.3f; g.frictionCombine = CombineMode::Minimum;
        PhysicsMaterials::Register(g);
        World a; a.Ramp(25.0f, "TestStatic"); GameObject* held = BoxOnRamp(a, 25.0f, 5.0f, "TestStatic"); const Vec3 s0 = P(held);
        a.Run(2.0f);
        World b; b.Ramp(25.0f, "TestStatic"); GameObject* sliding = BoxOnRamp(b, 25.0f, 5.0f, "TestStatic"); const Vec3 s1 = P(sliding);
        R(sliding)->velocity = Vec3{std::cos(25 * 0.0174533f), std::sin(25 * 0.0174533f), 0} * -1.5f;   // already sliding down
        b.Run(2.0f);
        std::cout << "  static 0.8 / kinetic 0.3 on 25 degrees: set down moved " << (P(held) - s0).Magnitude() << " m, sliding one moved " << (P(sliding) - s1).Magnitude() << " m\n";
        CHECK((P(held) - s0).Magnitude() < 0.05f);
        CHECK((P(sliding) - s1).Magnitude() > 2.0f);
        PhysicsMaterials::ClearProject();
    }
    // ---- Bounce height follows bounciness: from 2 m onto a floor of bounciness b the ball rises about b squared times 2 m.
    for (float bn : {0.5f, 0.8f}) {
        PhysicsMaterialData g; g.name = "TestBounce"; g.bounciness = bn; g.bounceCombine = CombineMode::Maximum; PhysicsMaterials::Register(g);
        World w; w.Floor("TestBounce");
        auto* ball = w.s.CreateGameObject("Ball"); ball->transform->localPosition = {0, 2.5f, 0};
        ball->AddComponent<SphereCollider3D>()->radius = 0.5f; auto* r = ball->AddComponent<Rigidbody3D>(); r->freezeRotation = true;
        float peak = 0.0f; bool hit = false;
        for (int i = 0; i < 150; ++i) { w.Run(0.02f); if (R(ball)->velocity.y > 0.5f) hit = true; if (hit) peak = std::max(peak, P(ball).y - 0.5f); if (hit && R(ball)->velocity.y < 0) break; }
        const float want = bn * bn * 2.0f;
        std::cout << "  bounciness " << bn << ": peak " << peak << " m (theory " << want << ")\n";
        CHECK(peak > want * 0.8f); CHECK(peak < want * 1.2f);
        PhysicsMaterials::ClearProject();
    }
    // ---- Density gives mass: 1 m cube of Metal = 7800 kg; a ball of radius 0.5 of Wood = 600 x 0.5236 kg.
    {
        World w;
        GameObject* cube = w.Box({0, 1, 0}); auto* s1 = cube->AddComponent<PhysicsSurface>(); s1->massFromDensity = true; s1->SetMaterial("Metal");
        CHECK_NEAR(R(cube)->mass, 7800.0, 1.0);
        auto* ball = w.s.CreateGameObject("Ball"); ball->AddComponent<SphereCollider3D>()->radius = 0.5f; ball->AddComponent<Rigidbody3D>();
        auto* s2 = ball->AddComponent<PhysicsSurface>(); s2->massFromDensity = true; s2->SetMaterial("Wood");
        CHECK_NEAR(R(ball)->mass, 600.0 * 0.523599, 1.0);
        auto* plain = w.Box({3, 1, 0}); CHECK_NEAR(R(plain)->mass, 1.0, 1e-6);       // no material, no change
    }
    // ---- Changing the material while the game runs: held on rubber, then made icy.
    {
        World w; w.Ramp(20.0f, "Rubber");
        GameObject* b = BoxOnRamp(w, 20.0f, 5.0f, nullptr);
        const Vec3 s0 = P(b);
        w.Run(1.0f);
        CHECK((P(b) - s0).Magnitude() < 0.05f);
        GameObject* ramp = w.s.Find("Ramp");
        CHECK(ramp && AssignPhysicsMaterial(*ramp, "Ice"));
        CHECK(ramp->GetComponent<PhysicsSurface>()->GetMaterial() == "Ice");
        CHECK(ramp->GetComponent<BoxCollider3D>()->friction < 0.05f);              // the collider took the numbers at once
        R(b)->WakeUp();
        w.Run(2.0f);
        std::cout << "  rubber ramp made icy: moved " << (P(b) - s0).Magnitude() << " m\n";
        CHECK((P(b) - s0).Magnitude() > 3.0f);
        CHECK(!ramp->GetComponent<PhysicsSurface>()->SetMaterial("No Such Material"));   // unknown name: nothing changes
        CHECK(ramp->GetComponent<PhysicsSurface>()->GetMaterial() == "Ice");
    }
    // ---- Mud slows a sliding crate more than plain ground does (damping + rolling).
    {
        float dist[2]; const char* mats[2] = {"Default", "Mud"};
        for (int i = 0; i < 2; ++i) {
            World w; w.Floor(nullptr); GameObject* f = w.s.Find("Floor");
            PhysicsMaterialData flat = *PhysicsMaterials::Find(mats[i]); flat.name = "TestFlat"; flat.dynamicFriction = 0.3f; flat.staticFriction = 0.3f; flat.frictionCombine = CombineMode::Minimum;
            PhysicsMaterials::Register(flat); AssignPhysicsMaterial(*f, "TestFlat");
            GameObject* b = w.Box({0, 0.5f, 0}); R(b)->velocity = {6, 0, 0}; w.Run(3.0f);
            dist[i] = P(b).x; PhysicsMaterials::ClearProject();
        }
        std::cout << "  slide distance: plain " << dist[0] << " m, mud " << dist[1] << " m\n";
        CHECK(dist[1] < dist[0] - 0.5f);
    }
    // ---- A contact tells a script which two materials met.
    {
        World w; w.Floor("Wood");
        GameObject* b = w.Box({0, 0.8f, 0}); AssignPhysicsMaterial(*b, "Metal");
        auto* spy = b->AddComponent<Spy>();
        w.Run(1.0f);
        std::cout << "  contact pair seen by the crate: own " << spy->seenOwn << ", other " << spy->seenOther << "\n";
        CHECK(spy->seenOwn == "Metal"); CHECK(spy->seenOther == "Wood");
    }
    // ---- Nothing goes wrong without a material: an object with no PhysicsSurface plays as before (friction field used).
    {
        World w; w.Floor(nullptr); GameObject* b = w.Box({0, 0.5f, 0}); R(b)->velocity = {3, 0, 0}; R(b)->friction = 0.5f; w.Run(4.0f);
        CHECK(P(b).x > 0.5f && P(b).x < 1.3f && std::isfinite(P(b).x));
    }
    // ---- A project material is found by name from the asset folder without any startup call (editor and player).
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_physmat_scan";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir / "Materials", ec);
        PhysicsMaterialData d; d.name = "ScanGel"; d.bounciness = 0.42f;
        CHECK(PhysicsMaterials::SaveFile(d, (dir / "Materials" / "gel.okayphysmat").string()));
        Paths::SetAssetBaseDir(dir.string());
        PhysicsSurface::RescanNextTime();
        World w; GameObject* o = w.Box({0, 1, 0});
        auto* ps = o->AddComponent<PhysicsSurface>(); ps->material = "ScanGel";
        CHECK(ps->Data() != nullptr);
        CHECK(ps->SetMaterial("ScanGel") && o->GetComponent<BoxCollider3D>()->bounciness > 0.4f);
        PhysicsMaterials::ClearProject(); Paths::SetAssetBaseDir(""); fs::remove_all(dir, ec);
    }
    TEST_MAIN_RESULT();
}
