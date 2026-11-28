// Link-and-run test of an engine file whose namespace-scope object has a constructor (Round 35):
// engine/src/Scene/ProjectLists.cpp keeps `const std::string kLayerKey = "layer_name_";` in an unnamed namespace, and
// every ApplyLine and Write reads it. okay-cl compiles that file and this one to separate objects; the object without
// main puts its start-up function in the .CRT$XCU table, which main runs first. Before Round 35 nothing ran it in a
// file without main: kLayerKey stayed empty, so Write gave "8=Enemy" and ApplyLine took keys that are not ours.
// The engine's own Tags and Layers (header-only) come along. Round 36: an inline function's static local (Tags::Custom's
// list, Layers::Names) is one object in the program (a COMDAT section), so this file reads and changes them directly and
// ProjectLists.cpp sees the same lists (before Round 36 each object had its own copy).
// Built by okay-cl (/O0 /O1 /O2) and by MinGW g++, run under Wine; the exit code and the printed text must be the same.
#include "mini.hpp"
#include "okay/Scene/ProjectLists.hpp"
#include "okay/Scene/Tags.hpp"
#include "okay/Scene/Layers.hpp"
#include <string>

using namespace okay;

// A global of this file with a constructor too: made from main's own table entry.
static const std::string kExpected = "tag=Boss\nstrict_tags=1\nlayer_name_8=Enemy\nlayer_name_9=Water2\n";

int main() {
    RUN_SUITE("scene_lists");
    ProjectLists::Reset();
    CHECK(ProjectLists::ApplyLine("layer_name_8", "Enemy"));
    CHECK(!ProjectLists::ApplyLine("8", "Enemy"));        // not one of our keys
    CHECK(!ProjectLists::ApplyLine("layer_name_99", "X"));  // no layer 99
    CHECK(ProjectLists::ApplyLine("tag", "Boss"));
    CHECK(ProjectLists::ApplyLine("tag", "Boss"));  // already in the list: taken, and written once
    const int taken = ProjectLists::Apply("strict_tags=1\r\nlayer_name_9=Water2\nother=1\n");
    CHECK(taken == 2);
    // What ProjectLists.cpp put in the shared lists is seen here, and what is added here is written there.
    CHECK(Tags::Contains("Boss"));
    CHECK(Layers::IsNamed(8) && Layers::IsNamed(9));
    const std::string text = ProjectLists::Write();
    CHECK(text == kExpected);
    CHECK(Tags::Add("Pickup"));
    const std::string more = ProjectLists::Write();
    CHECK(more.find("tag=Pickup\n") != std::string::npos);
    CHECK(Tags::Remove("Pickup"));
    std::printf("written %d bytes:\n%s", (int)text.size(), text.c_str());
    ProjectLists::Reset();
    CHECK(ProjectLists::Write().empty());
    TEST_MAIN_RESULT();
}
