// Controller audit and Simple view (claude/controller-1): every visible setting has a name, unit and tooltip, no two
// share a meaning, the default view is at most 8 settings, every Simple / Hidden name is a real registered property,
// reset / validate / save / load round trip, and the planar movement step is the same at 30, 60 and 144 fps.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ControllerTuning.hpp"
#include "okay/Components/ControllerFeel.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

using namespace okay;
using T = ControllerTuning;

template <class C> static void Audit(const char* type) {
    Scene sc("t"); GameObject* go = sc.CreateGameObject("g"); auto* c = go->AddComponent<C>();
    auto info = PropertyRegistry::FindFor(*c);
    CHECK(info != nullptr);
    if (!info) return;
    int adv = 0, hid = 0;
    for (const auto& p : info->properties) {
        auto lv = T::LevelOf(p.name);
        if (lv == T::Advanced) ++adv; else if (lv == T::Hidden) ++hid;
    }
    for (const char* n : {"walkSpeed", "runSpeed", "crouchSpeed", "airControl"}) CHECK(info->Find(n) != nullptr);
    for (const char* n : {"jumpForce", "walkAcceleration", "acceleration", "deceleration", "steerRate", "controllerPreset"})
        CHECK(info->Find(n) != nullptr);
    const int simple = (int)T::SimpleSettings<C>().size();
    std::printf("  %s: %zu registered; Simple view %d, Advanced %d, Hidden %d\n", type, info->properties.size(), simple, adv, hid);
    CHECK(simple <= 8);
}

static void Run(int fps, float& t90, float& stopDist) {
    float vx = 0, vz = 0; const float dt = 1.0f / fps; t90 = -1;
    for (int i = 0; i < fps * 2; ++i) {
        StepPlanarVelocity(vx, vz, 0, -5.5f, true, true, 130.0f, 90.0f, 260.0f, dt);
        if (t90 < 0 && std::hypot(vx, vz) >= 0.9f * 5.5f) t90 = (i + 1) * dt;
    }
    stopDist = 0;
    for (int i = 0; i < fps * 2 && std::hypot(vx, vz) > 0; ++i) {
        StepPlanarVelocity(vx, vz, 0, 0, false, true, 130.0f, 90.0f, 260.0f, dt);
        stopDist += std::hypot(vx, vz) * dt;
    }
}

int main() {
    RUN_SUITE("controller tuning");
    Audit<FirstPersonController>("FirstPersonController");
    Audit<ThirdPersonController>("ThirdPersonController");

    std::set<std::string> meanings, labels;
    for (const T::Setting& s : T::Settings()) {
        CHECK(s.label && *s.label); CHECK(s.unit && *s.unit); CHECK(s.tooltip && *s.tooltip); CHECK(s.hi > s.lo);
        CHECK(meanings.insert(s.meaning).second);
        CHECK(labels.insert(s.label).second);
    }
    CHECK(T::SimpleSettings<FirstPersonController>().size() == 7);
    CHECK(T::SimpleSettings<ThirdPersonController>().size() == 8);

    ThirdPersonController tp;
    CHECK(T::IsDefault(tp, "jumpHeight"));
    T::Set(tp, "jumpHeight", 1.2f);
    CHECK_NEAR(T::Get(tp, "jumpHeight"), 1.2f, 1e-3f);
    CHECK(!T::IsDefault(tp, "jumpHeight"));
    T::Reset(tp, "jumpHeight");
    CHECK(T::IsDefault(tp, "jumpHeight"));
    T::Set(tp, "speedUpTime", 0.25f); T::Set(tp, "stopTime", 0.1f);
    CHECK_NEAR(T::Get(tp, "speedUpTime"), 0.25f, 1e-3f);
    CHECK_NEAR(T::Get(tp, "stopTime"), 0.1f, 1e-3f);
    CHECK_NEAR(tp.walkAcceleration, tp.walkSpeed / 0.25f, 1e-3f);
    T::Set(tp, "walkSpeed", 99.0f);
    CHECK_NEAR(tp.walkSpeed, 12.0f, 1e-6f);
    FirstPersonController fp;
    CHECK(!T::Set(fp, "turnSpeed", 10.0f));
    CHECK(!T::Set(fp, "nonsense", 1.0f));

    FirstPersonController bad; bad.runSpeed = 1.0f; bad.walkSpeed = 2.0f; bad.crouchSpeed = 3.0f; bad.deceleration = 0.0f;
    auto issues = T::Validate(bad);
    CHECK(issues.size() >= 3);
    for (auto& i : issues) { CHECK(!i.message.empty()); CHECK(!i.fix.empty()); }
    CHECK(T::Validate(FirstPersonController{}).empty());
    CHECK(T::Validate(ThirdPersonController{}).empty());

    ThirdPersonController a; T::Set(a, "walkSpeed", 3.0f); T::Set(a, "runSpeed", 7.0f); T::Set(a, "jumpHeight", 1.4f);
    T::Set(a, "speedUpTime", 0.3f); T::Set(a, "stopTime", 0.2f); T::Set(a, "airControl", 0.4f); T::Set(a, "turnSpeed", 30.0f);
    const std::string text = T::Save(a);
    ThirdPersonController b;
    CHECK(T::Load(b, text) == 8);
    for (const T::Setting* s : T::SimpleSettings<ThirdPersonController>()) CHECK_NEAR(T::Get(b, s->id), T::Get(a, s->id), 2e-3f);
    FirstPersonController f2;
    CHECK(T::Load(f2, text) == 7);
    CHECK(T::Load(f2, "not a controller") == -1);
    CHECK(T::Load(f2, "okaycontroller 1\nwalkSpeed 2.5\nfutureThing 9\n") == 1);
    CHECK_NEAR(f2.walkSpeed, 2.5f, 1e-6f);

    float t[3], d[3]; const int fps[3] = {30, 60, 144};
    for (int i = 0; i < 3; ++i) { Run(fps[i], t[i], d[i]); std::printf("  %3d fps: 90%% speed in %.3f s, stops in %.3f m\n", fps[i], t[i], d[i]); }
    for (int i = 1; i < 3; ++i) { CHECK(std::fabs(t[i] - t[0]) < 0.06f); CHECK(std::fabs(d[i] - d[0]) < 0.10f); }
    // exact: the speed 0.1 s after letting go is the same at every frame rate (it was not, before StoppedSpeed)
    float sp[3];
    for (int i = 0; i < 3; ++i) {
        float vx = 0, vz = -5.5f; const float dt = 1.0f / fps[i];
        for (int k = 0; k < (int)std::lround(0.1f * fps[i]); ++k) StepPlanarVelocity(vx, vz, 0, 0, false, true, 130.0f, 90.0f, 260.0f, dt);
        sp[i] = std::hypot(vx, vz);
    }
    CHECK_NEAR(sp[1], sp[0], 0.05f); CHECK_NEAR(sp[2], sp[0], 0.05f);
    TEST_MAIN_RESULT();
}
