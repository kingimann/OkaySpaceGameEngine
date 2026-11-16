// Walking must look like walking and running like running (docs/walk_vs_run.md).
//
// The owner's words: "the character walking is basically running". Measured on the code before the change (the same
// numbers this test prints with OKAY_OLD_GAIT=1): plain W moved the built-in character at 4.5 m/s, which is a jog,
// and the legs stepped about 270 times a minute, MORE often than the run (250), with the hips bobbing more than in the
// run. So walk and run looked alike.
//
// This test drives a real Character through DriveLocomotion the way the controllers do (60 frames a second, the speed
// measured from how far the body really moved), on both ways the engine poses it: with the default Animator graph (a new
// project) and with the Character's own procedural stride. It reads the feet from the skinned mesh. For the controllers'
// default walk and run speeds it asserts the natural targets for this rig (a leg 0.80 m long from hip to ankle) and a
// clear gap on EVERY measure: steps a minute, step length, flight, double support, arm swing, elbow bend, forward
// lean and hip bounce. It also checks the state does not flicker between walk and run, the planted foot does not skate,
// and the default speeds and the "Use natural walk and run speeds" step.
//
//   test_walk_vs_run          the natural gait (the default)
//   test_walk_vs_run --old    run with OKAY_OLD_GAIT=1 in the environment (CTest does): the old gait is back, and the
//                             walk is shown NOT to be clearly different from the run: the "before".
//   --table                   also print the whole speed table (what docs/walk_vs_run.md quotes; slower)
#include "test_framework.hpp"
#include "walk_vs_run_measure.hpp"
#include <cstdlib>
#include <cstring>

using namespace okay;
using namespace walkrun;

namespace {

struct Pair { const char* name; float walk, run; };

// ---- The natural targets (and why) -----------------------------------------------------------------------------
// A person walks 95 to 130 steps a minute (a stroll 85, an easy walk 110, a brisk one 125-130; past that they jog), takes
// steps of 0.6 to 0.9 m, always has a foot on the ground (about 10 to 25 % of the time both), swings the arms a little with
// the elbows nearly straight and stays upright. A run is 150 to 190 steps a minute with both feet off the ground a
// good part of every step, steps of 1.4 m and more, the elbows bent near a right angle, the arms pumping and the chest
// leaning forward. The hips rise and fall about as far as the legs' sweep makes them, so a run bobs more than a walk.
// The walk limit is 135 here (not 130) so a measurement of a brisk walk that lands a step over is not a failure.
struct Walk { static constexpr float spmLo = 90, spmHi = 135, stepHi = 1.0f, flightMax = 0.01f, doubleMin = 0.05f, armMax = 25, elbowMax = 25, leanMax = 4, bounceMax = 0.10f; };
struct Run  { static constexpr float spmLo = 150, spmHi = 200, stepLo = 1.4f, flightMin = 0.15f, doubleMax = 0.01f, armMin = 35, elbowMin = 70, leanMin = 7.5f; };
// And the gap between the two, so "different" is more than each one being inside its range.
struct Gap  { static constexpr float spm = 40, stepRatio = 1.6f, flight = 0.15f, armRatio = 1.5f, elbow = 45, lean = 4, bounce = 0.02f; };

void CheckPair(const char* label, bool animator, float walkSpeed, float runSpeed) {
    std::printf("\n-- %s: %s, walk %.2f m/s, run %.2f m/s --\n", label, animator ? "Animator (a new project)" : "procedural stride", walkSpeed, runSpeed);
    const GaitStats w = Measure(animator, walkSpeed, false, walkSpeed, runSpeed);
    const GaitStats r = Measure(animator, runSpeed, true, walkSpeed, runSpeed);
    PrintHeader(); PrintRow(w); PrintRow(r);
    CHECK(w.ok && r.ok);
    if (!(w.ok && r.ok)) return;
    std::printf("   gap run - walk: steps %+.0f/min, step %+.2f m (x%.1f), flight %+.0f%%, double %+.0f%%, arm %+.0f deg (x%.1f), elbow %+.0f, lean %+.1f, bounce %+.1f cm\n",
                r.stepsPerMin - w.stepsPerMin, r.stepLength - w.stepLength, r.stepLength / std::fmax(0.01f, w.stepLength),
                (r.flight - w.flight) * 100, (r.doubleSupport - w.doubleSupport) * 100, r.armSwing - w.armSwing,
                r.armSwing / std::fmax(0.01f, w.armSwing), r.elbow - w.elbow, r.lean - w.lean, (r.bounce - w.bounce) * 100);
    // The walk is a walk.
    CHECK(w.anim == 2);
    CHECK(w.stepsPerMin >= Walk::spmLo && w.stepsPerMin <= Walk::spmHi);
    CHECK(w.stepLength <= Walk::stepHi);
    CHECK(w.flight < Walk::flightMax);                 // always at least one foot on the ground
    CHECK(w.doubleSupport >= Walk::doubleMin);         // and a moment with both
    CHECK(w.armSwing <= Walk::armMax && w.elbow <= Walk::elbowMax && w.lean <= Walk::leanMax);
    CHECK(w.bounce <= Walk::bounceMax);
    CHECK(w.lowFoot > -0.02f);                         // no foot sinks into the floor
    // The run is a run.
    CHECK(r.anim == 3);
    CHECK(r.stepsPerMin >= Run::spmLo && r.stepsPerMin <= Run::spmHi);
    CHECK(r.stepLength >= Run::stepLo);
    CHECK(r.flight >= Run::flightMin);
    CHECK(r.doubleSupport < Run::doubleMax);
    CHECK(r.armSwing >= Run::armMin && r.elbow >= Run::elbowMin && r.lean >= Run::leanMin);
    CHECK(r.lowFoot > -0.02f);
    // And they are clearly different, on every measure.
    CHECK(r.stepsPerMin - w.stepsPerMin >= Gap::spm);
    CHECK(r.stepLength >= w.stepLength * Gap::stepRatio);
    CHECK(r.flight - w.flight >= Gap::flight);
    CHECK(w.doubleSupport > r.doubleSupport);
    CHECK(r.armSwing >= w.armSwing * Gap::armRatio);
    CHECK(r.elbow - w.elbow >= Gap::elbow);
    CHECK(r.lean - w.lean >= Gap::lean);
    CHECK(r.bounce - w.bounce >= Gap::bounce);
    // The planted foot does not skate: the lowest vertex of a foot on the ground moves over it less than a walk's speed.
    CHECK(w.footSlip < 0.15f * walkSpeed + 0.05f);
    CHECK(r.footSlip < 0.15f * runSpeed + 0.05f);
}

void PrintTable(bool animator, float walkSpeed, float runSpeed) {
    std::printf("\n== table: %s, controller walk %.2f / run %.2f m/s ==\n", animator ? "Animator (a new project)" : "procedural stride", walkSpeed, runSpeed);
    PrintHeader();
    for (float v : {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 5.5f, 6.0f, 7.0f, 8.0f, 9.0f}) {
        PrintRow(Measure(animator, v, false, walkSpeed, runSpeed, 6.0f, 2.0f));
        if (v >= walkSpeed * 0.9f) PrintRow(Measure(animator, v, true, walkSpeed, runSpeed, 6.0f, 2.0f));
    }
}

// How the old gait looked: at the old default speeds plain W was not a walk. These are the numbers measured on the code
// before the change (the baseline in docs/walk_vs_run.md), with room for the difference between runs.
void CheckOld(bool animator, float walkSpeed, float runSpeed) {
    std::printf("\n-- OLD gait: %s, walk %.2f m/s, run %.2f m/s --\n", animator ? "Animator (a new project)" : "procedural stride", walkSpeed, runSpeed);
    const GaitStats w = Measure(animator, walkSpeed, false, walkSpeed, runSpeed);
    const GaitStats r = Measure(animator, runSpeed, true, walkSpeed, runSpeed);
    PrintHeader(); PrintRow(w); PrintRow(r);
    CHECK(w.ok && r.ok);
    if (!(w.ok && r.ok)) return;
    // The old "walk" fails every part of the walk target that a person can see ...
    CHECK(w.stepsPerMin > Walk::spmHi + 60.0f);        // it stepped like a runner: far past a walk's 135 a minute
    CHECK(w.flight >= Run::flightMin);                 // both feet off the ground for a good part of each step: a run
    CHECK(w.doubleSupport < Walk::doubleMin);          // never both feet down
    // ... and its cadence was not clearly different from the run's (the target asks for 40 a minute more).
    CHECK(r.stepsPerMin - w.stepsPerMin < Gap::spm);
}

} // namespace

int main(int argc, char** argv) {
    bool oldMode = false, table = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--old") == 0) oldMode = true;
        if (std::strcmp(argv[i], "--table") == 0) table = true;   // the full speed table docs/walk_vs_run.md quotes (slower)
    }
    RUN_SUITE((oldMode ? "walk_vs_run (OKAY_OLD_GAIT=1: the old gait)" : "walk_vs_run"));
    if (oldMode) {
        CHECK(HumanoidMotion::OldGait());              // CTest sets OKAY_OLD_GAIT=1 for this run
        FirstPersonController fp; ThirdPersonController tp; ThirdPersonShooterController tps;
        CHECK(fp.walkSpeed == 4.5f && fp.runSpeed == 8.0f);
        CHECK(tp.walkSpeed == 4.5f && tp.runSpeed == 8.0f);
        CHECK(tps.walkSpeed == 4.5f && tps.runSpeed == 7.5f);
        CHECK(fp.crouchSpeed == 2.2f && fp.proneSpeed == 1.1f && tps.stanceControl.crouchSpeed == 2.2f);
        CHECK(Character::Locomotion().walkSpeed == 4.5f && Character::Locomotion().runSpeed == 8.0f);
        CheckOld(true, fp.walkSpeed, fp.runSpeed);
        CheckOld(false, fp.walkSpeed, fp.runSpeed);
        if (table) { PrintTable(true, fp.walkSpeed, fp.runSpeed); PrintTable(false, fp.walkSpeed, fp.runSpeed); }
        TEST_MAIN_RESULT();
    }
    CHECK(!HumanoidMotion::OldGait());

    // ---- The default speeds ---------------------------------------------------------------------------------------
    FirstPersonController fp; ThirdPersonController tp; ThirdPersonShooterController tps;
    CHECK(fp.walkSpeed == NaturalGait::kWalkSpeed && fp.runSpeed == NaturalGait::kRunSpeed);
    CHECK(tp.walkSpeed == NaturalGait::kWalkSpeed && tp.runSpeed == NaturalGait::kRunSpeed);
    CHECK(tps.walkSpeed == NaturalGait::kWalkSpeed && tps.runSpeed == NaturalGait::kShooterRunSpeed);
    CHECK(Character::Locomotion().walkSpeed == NaturalGait::kWalkSpeed && Character::Locomotion().runSpeed == NaturalGait::kRunSpeed);
    // Crouching and crawling stay well under the walk (the old 2.2 m/s crouch would pass the new 2.0 m/s walk).
    CHECK(fp.crouchSpeed < fp.walkSpeed * 0.7f && fp.proneSpeed < fp.crouchSpeed);
    CHECK(tp.crouchSpeed < tp.walkSpeed * 0.7f && tp.proneSpeed < tp.crouchSpeed);
    CHECK(tps.stanceControl.crouchSpeed < tps.walkSpeed * 0.7f && tps.stanceControl.proneSpeed < tps.stanceControl.crouchSpeed);
    // A walk is a walk: below the speed where the stride turns into a jog; a run is well above it.
    CHECK(fp.walkSpeed <= 2.4f && fp.runSpeed >= 5.0f && fp.runSpeed >= fp.walkSpeed * 2.0f);

    // ---- The walk and the run, on every measure ---------------------------------------------------------------------
    CheckPair("First Person defaults", true, fp.walkSpeed, fp.runSpeed);
    CheckPair("First Person defaults", false, fp.walkSpeed, fp.runSpeed);
    CheckPair("Third Person defaults", true, tp.walkSpeed, tp.runSpeed);
    CheckPair("Third Person Shooter defaults", true, tps.walkSpeed, tps.runSpeed);

    // ---- The full table (it is what docs/walk_vs_run.md quotes) ------------------------------------------------------
    if (table) { PrintTable(true, fp.walkSpeed, fp.runSpeed); PrintTable(false, fp.walkSpeed, fp.runSpeed); }

    // ---- The style follows the real speed, not the controller's settings ---------------------------------------------
    {
        Walker w(false);                      // procedural: the style is readable
        w.walkSpeed = fp.walkSpeed; w.runSpeed = fp.runSpeed;
        for (int i = 0; i < 180; ++i) w.Step(fp.walkSpeed, false);
        CHECK(w.ch->anim == 2 && w.ch->RunStyle() < 0.01f);            // W at the walk speed is a pure walk (it used to be a third of a run)
        for (int i = 0; i < 90; ++i) w.Step(fp.runSpeed, true);
        CHECK(w.ch->anim == 3 && w.ch->RunStyle() > 0.99f);            // Shift is a pure run
        // An older project whose "walk" is 4.5 m/s: the legs show the jog it really is, not a half-and-half.
        Walker o(false);
        o.walkSpeed = 4.5f; o.runSpeed = 8.0f;
        for (int i = 0; i < 180; ++i) o.Step(4.5f, false);
        CHECK(o.ch->anim == 2 && o.ch->RunStyle() > 0.99f);
        const GaitStats jog = Measure(false, 4.5f, false, 4.5f, 8.0f);
        std::printf("\n   an older project (walk 4.5): "); PrintRow(jog);
        CHECK(jog.ok && jog.stepsPerMin >= Run::spmLo && jog.stepsPerMin <= Run::spmHi && jog.flight >= Run::flightMin);   // a jog, not 270 steps a minute
    }

    // ---- No flicker between walk and run ----------------------------------------------------------------------------
    {
        // Speeding up and slowing down through the whole range: one change of state each way, and the look never goes back on itself.
        Walker w(false);
        w.walkSpeed = fp.walkSpeed; w.runSpeed = fp.runSpeed;
        for (int i = 0; i < 60; ++i) w.Step(0.0f, false);
        int changes = 0, last = w.ch->anim; float style = 0.0f; bool styleBack = false;
        for (int i = 0; i < 600; ++i) {                       // 0 -> 8 m/s over 10 s
            const float v = 8.0f * (float)i / 600.0f;
            w.Step(v, false);
            if (w.ch->anim != last) { ++changes; last = w.ch->anim; }
            if (w.ch->RunStyle() < style - 0.02f) styleBack = true;
            style = std::fmax(style, w.ch->RunStyle());
        }
        for (int i = 0; i < 600; ++i) {                       // and back down
            const float v = 8.0f * (1.0f - (float)i / 600.0f);
            w.Step(v, false);
            if (w.ch->anim != last) { ++changes; last = w.ch->anim; }
        }
        std::printf("\n   0 -> 8 -> 0 m/s: %d state changes\n", changes);
        CHECK(changes <= 4);                                   // idle -> walk -> run, and back: no more
        CHECK(!styleBack);
        // A speed hovering on the threshold does not flip back and forth.
        Walker h(false);
        h.walkSpeed = fp.walkSpeed; h.runSpeed = fp.runSpeed;
        for (int i = 0; i < 120; ++i) h.Step(fp.walkSpeed, false);
        int flips = 0; last = h.ch->anim;
        const float edge = fp.walkSpeed * 1.15f;               // where the run state starts (1.05 once running)
        for (int i = 0; i < 300; ++i) {
            h.Step(edge + ((i / 3) % 2 ? 0.06f : -0.06f) * fp.walkSpeed * 0.5f, false);
            if (h.ch->anim != last) { ++flips; last = h.ch->anim; }
        }
        std::printf("   hovering on the run threshold: %d flips\n", flips);
        CHECK(flips <= 1);
    }

    // ---- "Use natural walk and run speeds" (what the Inspector button does) ----------------------------------------------
    {
        float walk = 4.5f, run = 8.0f, crouch = 2.2f, prone = 1.1f;
        CHECK(NaturalGait::UseNaturalSpeeds(walk, run, crouch, prone, NaturalGait::kRunSpeed) == 4);
        CHECK(walk == NaturalGait::kWalkSpeed && run == NaturalGait::kRunSpeed && crouch == NaturalGait::kCrouchSpeed && prone == NaturalGait::kProneSpeed);
        CHECK(NaturalGait::UseNaturalSpeeds(walk, run, crouch, prone, NaturalGait::kRunSpeed) == 0);   // nothing left to change
        float w2 = 4.5f, r2 = 7.5f, c2 = 3.0f, p2 = 0.4f;                  // a crouch and prone speed somebody chose are theirs
        CHECK(NaturalGait::UseNaturalSpeeds(w2, r2, c2, p2, NaturalGait::kShooterRunSpeed) == 2);
        CHECK(w2 == NaturalGait::kWalkSpeed && r2 == NaturalGait::kShooterRunSpeed && c2 == 3.0f && p2 == 0.4f);
        // The hint under Walk Speed: only when the walk is really a jog.
        CHECK(NaturalGait::WalkSpeedHint(2.0f).empty() && NaturalGait::WalkSpeedHint(3.0f).empty());
        CHECK(!NaturalGait::WalkSpeedHint(3.1f).empty() && !NaturalGait::WalkSpeedHint(4.5f).empty());
        CHECK(NaturalGait::WalkSpeedHint(4.5f).find("jog") != std::string::npos);
        // The old defaults still count as untouched when a character model is attached.
        CHECK(NaturalGait::IsOldDefaultWalk(4.5f) && NaturalGait::IsOldDefaultRun(8.0f) && NaturalGait::IsOldDefaultRun(7.5f));
        CHECK(!NaturalGait::IsOldDefaultWalk(2.0f) && !NaturalGait::IsOldDefaultRun(5.5f));
    }

    TEST_MAIN_RESULT();
}
