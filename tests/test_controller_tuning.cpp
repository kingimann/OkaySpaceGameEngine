// The controller Inspector (claude/controller-1). There is no Simple / Advanced split: one Inspector, at most about 15
// settings, each with a plain name, a unit and a tooltip, no two with the same meaning, a few short sections, rare
// features as ONE on/off box that reveals its numbers, and every old field still loadable. Also: reset, validation,
// save / load round trip, old-scene load, and the stop curve is the same at 30, 60 and 144 fps.
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
    // the Inspector: at most 15 always-visible settings, each complete, no shared meaning, few sections
    const auto base = T::Base<C>();
    std::set<std::string> meanings, labels, sections;
    for (const T::Setting* s : T::Shown<C>()) {
        CHECK(s->label && *s->label); CHECK(s->unit && *s->unit); CHECK(s->tooltip && *s->tooltip); CHECK(s->hi > s->lo);
        CHECK(meanings.insert(s->meaning).second);
        CHECK(labels.insert(s->label).second);
        sections.insert(s->section);
        if (*s->requires_) { const T::Setting* t = T::Find(s->requires_); CHECK(t && t->toggle); }
    }
    CHECK((int)base.size() <= 15);
    CHECK(sections.size() <= 5);
    // Visible() follows the on/off boxes: all off = base, all on = everything
    C off; for (const T::Setting* s : T::Shown<C>()) if (s->toggle) T::Set(off, s->id, 0.0f);
    CHECK(T::Visible(off).size() == base.size());
    C on; for (const T::Setting* s : T::Shown<C>()) if (s->toggle) T::Set(on, s->id, 1.0f);
    CHECK(T::Visible(on).size() == T::Shown<C>().size());
    CHECK(T::Visible(on).size() <= 20);
    // registry: shown properties are Simple with label, unit, tooltip; every other property is automatic and hidden
    int shown = 0, automatic = 0;
    for (const auto& p : info->properties) {
        if (p.level == PropertyLevel::Simple) { ++shown; CHECK(!p.label.empty()); CHECK(!p.unit.empty()); CHECK(!p.tooltip.empty()); CHECK(!p.IsHidden()); }
        else { ++automatic; CHECK(p.level == PropertyLevel::Hidden); CHECK(p.IsHidden()); CHECK(T::IsAutomatic(p.name)); }
    }
    std::printf("  %s: %zu registered properties -> %d shown, %d automatic; Inspector %zu settings with every box off, %zu with every box on\n",
                type, info->properties.size(), shown, automatic, base.size(), T::Visible(on).size());
    CHECK(shown >= 8);
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

    // defaults: a fresh controller passes its own validation and every setting reads back inside its range
    { FirstPersonController f; ThirdPersonController t;
      CHECK(T::Validate(f).empty()); CHECK(T::Validate(t).empty());
      for (const T::Setting* s : T::Shown<FirstPersonController>()) { float v = T::Get(f, s->id); CHECK(v >= s->lo - 1e-4f && v <= s->hi + 1e-4f); CHECK(T::IsDefault(f, s->id)); }
      for (const T::Setting* s : T::Shown<ThirdPersonController>()) { float v = T::Get(t, s->id); CHECK(v >= s->lo - 1e-4f && v <= s->hi + 1e-4f); CHECK(T::IsDefault(t, s->id)); } }

    // get / set / reset, merged settings write every raw field behind them
    ThirdPersonController tp;
    T::Set(tp, "jumpHeight", 1.2f);
    CHECK_NEAR(T::Get(tp, "jumpHeight"), 1.2f, 1e-3f);
    CHECK(!T::IsDefault(tp, "jumpHeight"));
    T::Reset(tp, "jumpHeight");
    CHECK(T::IsDefault(tp, "jumpHeight"));
    T::Set(tp, "speedUpTime", 0.25f); T::Set(tp, "stopTime", 0.1f);
    CHECK_NEAR(T::Get(tp, "speedUpTime"), 0.25f, 1e-3f);
    CHECK_NEAR(tp.walkAcceleration, tp.walkSpeed / 0.25f, 1e-3f);
    T::Set(tp, "jumpForgiveness", 0.2f);
    CHECK_NEAR(tp.coyoteTime, 0.2f, 1e-6f); CHECK_NEAR(tp.jumpBufferTime, 0.2f, 1e-6f);
    T::Reset(tp, "jumpForgiveness"); CHECK(T::IsDefault(tp, "jumpForgiveness")); CHECK_NEAR(tp.jumpBufferTime, ThirdPersonController{}.jumpBufferTime, 1e-6f);
    T::Set(tp, "canClimb", 1.0f);
    CHECK(tp.canVault && tp.canMantle && tp.canLedgeGrab);
    T::Set(tp, "canLean", 0.0f); CHECK(tp.leanLeftKey == 0 && tp.leanRightKey == 0);
    T::Reset(tp, "canLean"); CHECK(tp.leanLeftKey == 'q' && tp.leanRightKey == 'e');
    T::Set(tp, "canSlide", 0.0f); CHECK(!tp.canSlide && !tp.diveOnProneWhileSprinting);
    T::Set(tp, "canDodge", 1.0f); T::Set(tp, "dodgeSpeed", 9.0f); CHECK(tp.dodge.canDodge); CHECK_NEAR(tp.dodge.impulse, 9.0f, 1e-6f);
    T::Set(tp, "walkSpeed", 99.0f); CHECK_NEAR(tp.walkSpeed, 12.0f, 1e-6f);
    FirstPersonController fp;
    CHECK(!T::Set(fp, "turnSpeed", 10.0f)); CHECK(!T::Set(tp, "headBob", 1.0f)); CHECK(!T::Set(fp, "nonsense", 1.0f));
    T::Set(fp, "headBob", 0.0f); CHECK(!fp.headBob);

    // validation: a plain sentence and a fix
    FirstPersonController bad; bad.runSpeed = 1.0f; bad.walkSpeed = 2.0f; bad.crouchSpeed = 3.0f; bad.deceleration = 0.0f;
    auto issues = T::Validate(bad);
    CHECK(issues.size() >= 3);
    for (auto& i : issues) { CHECK(!i.message.empty()); CHECK(!i.fix.empty()); }

    // save / load round trip, a file from another project, and unknown or foreign keys
    ThirdPersonController a; T::Set(a, "walkSpeed", 3.0f); T::Set(a, "runSpeed", 7.0f); T::Set(a, "jumpHeight", 1.4f);
    T::Set(a, "speedUpTime", 0.3f); T::Set(a, "stopTime", 0.2f); T::Set(a, "airControl", 0.4f); T::Set(a, "turnSpeed", 30.0f);
    T::Set(a, "canDodge", 1.0f); T::Set(a, "dodgeSpeed", 12.0f); T::Set(a, "canLean", 0.0f); T::Set(a, "canClimb", 1.0f);
    const std::string text = T::Save(a);
    ThirdPersonController b;
    CHECK(T::Load(b, text) == (int)T::Shown<ThirdPersonController>().size());
    for (const T::Setting* s : T::Shown<ThirdPersonController>()) CHECK_NEAR(T::Get(b, s->id), T::Get(a, s->id), 2e-3f);
    FirstPersonController f2;
    CHECK(T::Load(f2, text) == (int)T::Shown<ThirdPersonController>().size() - 1);   // turn speed does not apply to first person
    CHECK(T::Load(f2, "not a controller") == -1);
    CHECK(T::Load(f2, "okaycontroller 1\nwalkSpeed 2.5\nfutureThing 9\n") == 1);
    CHECK_NEAR(f2.walkSpeed, 2.5f, 1e-6f);

    // an old scene still loads every saved raw field, including the automatic ones nobody sees any more
    {
        Scene old("old");
        GameObject* p = Templates::BuildPlayerBody(old, {0, 1, 0}, "Player");
        auto* f = p->AddComponent<FirstPersonController>();
        f->walkSpeed = 3.3f; f->jumpForce = 7.1f; f->acceleration = 77.0f; f->steerRate = 123.0f; f->slopeLimit = 33.0f;
        f->bobSway = 1.5f; f->coyoteTime = 0.31f; f->crouchEyeHeight = 1.05f; f->canVault = true;
        const std::string txt = SceneSerializer::Serialize(old);
        Scene back("back");
        CHECK(SceneSerializer::Deserialize(back, txt));
        auto fs = back.FindObjectsOfType<FirstPersonController>();
        CHECK(fs.size() == 1);
        if (!fs.empty()) {
            auto* g = fs[0];
            CHECK_NEAR(g->walkSpeed, 3.3f, 1e-4f); CHECK_NEAR(g->jumpForce, 7.1f, 1e-4f); CHECK_NEAR(g->acceleration, 77.0f, 1e-3f);
            CHECK_NEAR(g->steerRate, 123.0f, 1e-3f); CHECK_NEAR(g->slopeLimit, 33.0f, 1e-3f); CHECK_NEAR(g->bobSway, 1.5f, 1e-4f);
            CHECK_NEAR(g->coyoteTime, 0.31f, 1e-4f); CHECK_NEAR(g->crouchEyeHeight, 1.05f, 1e-4f); CHECK(g->canVault);
        }
    }

    float t[3], d[3]; const int fps[3] = {30, 60, 144};
    for (int i = 0; i < 3; ++i) { Run(fps[i], t[i], d[i]); std::printf("  %3d fps: 90%% speed in %.3f s, stops in %.3f m\n", fps[i], t[i], d[i]); }
    for (int i = 1; i < 3; ++i) { CHECK(std::fabs(t[i] - t[0]) < 0.06f); CHECK(std::fabs(d[i] - d[0]) < 0.10f); }
    float sp[3];
    for (int i = 0; i < 3; ++i) {
        float vx = 0, vz = -5.5f; const float dt = 1.0f / fps[i];
        for (int k = 0; k < (int)std::lround(0.1f * fps[i]); ++k) StepPlanarVelocity(vx, vz, 0, 0, false, true, 130.0f, 90.0f, 260.0f, dt);
        sp[i] = std::hypot(vx, vz);
    }
    CHECK_NEAR(sp[1], sp[0], 0.05f); CHECK_NEAR(sp[2], sp[0], 0.05f);
    TEST_MAIN_RESULT();
}
