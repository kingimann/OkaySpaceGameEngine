// The guided first steps: which one-line hint shows, and when a step counts as done.
#include "test_framework.hpp"
#include "VisualScriptHints.hpp"
#include "VisualScriptGlossary.hpp"
#include <cstring>

using namespace okay::editor::vsed;

int main() {
    RUN_SUITE("vs_hints");
    unsigned done = 0;
    VsHintFacts f;

    // Empty canvas: pick a recipe.
    VsHintResult r = VsHintStep(done, f);
    CHECK(r.hint == VsHint::PickRecipe && r.done == 0);
    // One block: add the next one. Two blocks, no wire: connect them.
    f.nodeCount = 1; r = VsHintStep(done, f); done = r.done;
    CHECK(r.hint == VsHint::AddNext && (done & kHintDoneAdded));
    f.nodeCount = 2; r = VsHintStep(done, f);
    CHECK(r.hint == VsHint::Connect);
    // The person draws a wire: connect is done, next is Play.
    f.wireCount = 1; f.userWired = true; r = VsHintStep(done, f); done = r.done; f.userWired = false;
    CHECK(r.hint == VsHint::Play && (done & kHintDoneWired));
    // Press Play: next is the number hint, and the hint is not Play again after Play stops.
    f.playing = true; r = VsHintStep(done, f); done = r.done;
    CHECK(r.hint == VsHint::ChangeNumber && (done & kHintDonePlayed));
    f.playing = false; r = VsHintStep(done, f);
    CHECK(r.hint == VsHint::ChangeNumber);
    // Change a number: everything is done, the bar hides.
    f.editedNumber = true; r = VsHintStep(done, f); done = r.done; f.editedNumber = false;
    CHECK(r.hint == VsHint::None && done == kHintDoneAll);
    // Remembered: with all done even an empty canvas shows nothing.
    CHECK(VsHintStep(done, VsHintFacts{}).hint == VsHint::None);
    // Done bits are never lost.
    CHECK((VsHintStep(done, VsHintFacts{}).done & kHintDoneAll) == kHintDoneAll);

    // A recipe gives a wired graph: the bar goes straight to Play, and does not ask for a wire.
    f = VsHintFacts{}; f.nodeCount = 2; f.wireCount = 1;
    r = VsHintStep(0, f);
    CHECK(r.hint == VsHint::Play && !(r.done & kHintDoneWired));
    // Pressing Play on an empty canvas does not count as trying something.
    f = VsHintFacts{}; f.playing = true;
    CHECK(!(VsHintStep(0, f).done & kHintDonePlayed));
    // A wire the person did not draw (no userWired) does not mark the step.
    f = VsHintFacts{}; f.nodeCount = 2; f.wireCount = 1;
    CHECK(!(VsHintStep(0, f).done & kHintDoneWired));
    // Hints turned off: nothing shows, progress is still recorded.
    f = VsHintFacts{}; f.nodeCount = 1;
    r = VsHintStep(0, f, false);
    CHECK(r.hint == VsHint::None && (r.done & kHintDoneAdded));

    // Texts: every hint has one, in plain words, one line.
    for (VsHint h : {VsHint::PickRecipe, VsHint::AddNext, VsHint::Connect, VsHint::Play, VsHint::ChangeNumber}) {
        const char* t = VsHintText(h);
        CHECK(t && t[0]);
        CHECK(std::strchr(t, '\n') == nullptr);
        CHECK(VsGlossaryViolation(t).empty());
    }
    CHECK(VsHintText(VsHint::None)[0] == 0);
    CHECK(std::strstr(VsHintText(VsHint::PickRecipe), "Space") != nullptr);
    TEST_MAIN_RESULT();
}
