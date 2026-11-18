// The "what happened" line under a block while the scene runs.
#include "test_framework.hpp"
#include "VisualScriptWhatHappened.hpp"
#include "VisualScriptGlossary.hpp"
#include "VisualScriptRecipes.hpp"
#include <string>
#include <vector>

using namespace okay::editor::vsed;

int main() {
    RUN_SUITE("vs_what_happened");
    CHECK(VsShortNumber("3") == "3" && VsShortNumber("3.000") == "3" && VsShortNumber("-2.5") == "-2.5");
    CHECK(VsShortNumber("hello") == "hello" && VsShortNumber("").empty());
    CHECK(VsWhatHappened("GlMoveSideways", "", {"3"}, 1) == "Moved 3 steps right");   // the example from the brief
    CHECK(VsWhatHappened("GlMoveSideways", "", {"-3"}, 1) == "Moved 3 steps left");
    CHECK(VsWhatHappened("GlMoveForward", "", {"5"}, 1) == "Moved 5 steps forward");
    CHECK(VsWhatHappened("ScoreAdd", "", {"1"}, 1) == "Added 1 to the score");
    CHECK(VsWhatHappened("SetActive", "", {"0"}, 1) == "Hid it");
    CHECK(VsWhatHappened("SetActive", "", {"1"}, 1) == "Showed it");
    CHECK(VsWhatHappened("SetText", "", {"Hello!"}, 1) == "Showed the text \"Hello!\"");
    CHECK(VsWhatHappened("Translate", "", {"0", "2", "0"}, 1) == "Moved by 0, 2, 0");
    CHECK(VsWhatHappened("MoveTowards", "Player", {"3"}, 1) == "Moved toward Player");
    CHECK(VsWhatHappened("GlJump", "", {}, 1) == "Jumped");
    CHECK(VsWhatHappened("Lose", "", {}, 1) == "The game is over: you lose");
    CHECK(VsWhatHappened("ScoreAdd", "", {}, 3) == "Added some to the score (3 times)");   // value unknown: still a sentence
    CHECK(VsWhatHappened("SomethingNew", "", {}, 1) == "Ran");
    CHECK(VsWhatHappened("OnKey", "space", {}, 1) == "This just happened");
    // Every recipe's blocks get a sentence in plain words (so a recipe played in the editor always says something).
    for (const VsRecipe& r : VsRecipes()) {
        std::size_t pos = 0;
        while ((pos = r.graph.find("node ", pos)) != std::string::npos) {
            const std::size_t e = r.graph.find('\n', pos);
            const std::string line = r.graph.substr(pos + 5, e - pos - 5);
            const std::size_t sp = line.find(' ');
            const std::size_t sp2 = line.find(' ', sp + 1);
            const std::string type = sp2 == std::string::npos ? line.substr(sp + 1) : line.substr(sp + 1, sp2 - sp - 1);
            const std::string s = VsWhatHappened(type, "", {"2", "3", "4"}, 2);
            CHECK(!s.empty());
            CHECK(VsGlossaryViolation(s).empty());
            pos = e;
        }
    }
    TEST_MAIN_RESULT();
}
