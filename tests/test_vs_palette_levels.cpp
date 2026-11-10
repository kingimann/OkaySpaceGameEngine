// Every visual-script node has a level and a one-line help sentence.
// The level filter and the search order are checked here too.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include <cstring>
#include <string>

using namespace okay::editor::vsed;

int main() {
    RUN_SUITE("vs_palette_levels");

    CHECK(VsLevelPasses(VsLevel::Basic, VsLevelFilter::All));
    CHECK(VsLevelPasses(VsLevel::Advanced, VsLevelFilter::All));
    CHECK(VsLevelPasses(VsLevel::Basic, VsLevelFilter::Basic));
    CHECK(!VsLevelPasses(VsLevel::Intermediate, VsLevelFilter::Basic));
    CHECK(!VsLevelPasses(VsLevel::Advanced, VsLevelFilter::Basic));
    CHECK(VsLevelPasses(VsLevel::Intermediate, VsLevelFilter::Intermediate));
    CHECK(!VsLevelPasses(VsLevel::Basic, VsLevelFilter::Intermediate));
    CHECK(VsLevelPasses(VsLevel::Advanced, VsLevelFilter::Advanced));
    CHECK(!VsLevelPasses(VsLevel::Basic, VsLevelFilter::Advanced));

    // Closest match wins, then the easier level, then the one used more recently.
    CHECK(VsSearchRanksBefore(20, VsLevel::Advanced, 5, 10, VsLevel::Basic, 0));
    CHECK(VsSearchRanksBefore(10, VsLevel::Basic, 9, 10, VsLevel::Advanced, 0));
    CHECK(VsSearchRanksBefore(10, VsLevel::Basic, 0, 10, VsLevel::Basic, 4));
    CHECK(!VsSearchRanksBefore(10, VsLevel::Advanced, 0, 10, VsLevel::Basic, 9));
    CHECK(VsBrowseRanksBefore(0, VsLevel::Basic, 1, 0, VsLevel::Intermediate, 0));
    CHECK(VsBrowseRanksBefore(0, VsLevel::Intermediate, 0, 1, VsLevel::Basic, 0));

    CHECK(VsStarterCount() >= 20 && VsStarterCount() <= 30);
    for (int i = 0; i < VsStarterCount(); ++i) {
        const char* type = kStarterNodes[i];
        CHECK(VsLevelForNode(type) == VsLevel::Basic);
        const char* help = VsNodeHelp(type);
        CHECK(help && help[0]);
        if (!help || !help[0]) std::printf("  starter %s has no help\n", type);
        const char* example = VsNodeExample(type);
        CHECK(example && example[0]);
        bool inRegistry = false;
        for (const VsDef& d : Registry()) if (std::strcmp(d.type, type) == 0) inRegistry = true;
        CHECK(inRegistry);
        if (!inRegistry) std::printf("  starter %s is not in the registry\n", type);
    }

    int hand = 0, api = 0;
    for (const VsDef& d : Registry()) {
        const int lv = (int)d.level;
        CHECK(lv >= 0 && lv <= 2);
        // Fn.print has a dot but is a text-script builtin, not an engine binding.
        const okay::vs::VsApiEntry* apiEntry = std::strchr(d.type, '.') ? okay::vs::VsApiFind(d.type) : nullptr;
        CHECK(d.level == (apiEntry ? VsLevelForApi(apiEntry->apiType.c_str(), apiEntry->member.c_str())
                                   : VsLevelForNode(d.type)));
        if (apiEntry) {
            const bool help = !apiEntry->doc.empty() || (VsNodeHelp(d.type) && VsNodeHelp(d.type)[0]);
            CHECK(help);
            if (!help) std::printf("  no help: %s\n", d.type);
            ++api;
        } else {
            const char* help = VsNodeHelp(d.type);
            CHECK(help && help[0]);
            if (!help || !help[0]) std::printf("  no help: %s\n", d.type);
            ++hand;
        }
    }
    CHECK(hand > 300);
    CHECK(api > 50);
    std::printf("  nodes with a level and help: %d hand-written, %d script bindings\n", hand, api);

    // The logic-runtime branch's new nodes share this table, so they get a level too.
    CHECK(VsSharedLevelCount() == 84);
    for (int i = 0; i < VsSharedLevelCount(); ++i) {
        CHECK(kSharedLevels[i].type && kSharedLevels[i].type[0]);
        CHECK(VsLevelForNode(kSharedLevels[i].type) == kSharedLevels[i].level);
    }
    CHECK(VsLevelForNode("OnInteract") == VsLevel::Basic);
    CHECK(VsLevelForNode("TweenPosition") == VsLevel::Intermediate);
    CHECK(VsLevelForNode("RunGraph") == VsLevel::Advanced);
    CHECK(VsLevelForNode("CallScript") == VsLevel::Advanced);
    CHECK(VsLevelForStep("set_var", "Variables") == VsLevel::Basic);
    CHECK(VsLevelForStep("net_host", "Multiplayer") == VsLevel::Advanced);
    CHECK(VsLevelForStep("tween_move", "Juice") == VsLevel::Intermediate);

    // The logic library is leveled on purpose: beginners, the everyday toolbox, and specialist tools.
    CHECK(VsLevelForNode("LbApproach") == VsLevel::Basic);
    CHECK(VsLevelForNode("LbSmoothStep") == VsLevel::Intermediate);
    CHECK(VsLevelForNode("LbSpringStep") == VsLevel::Advanced);
    CHECK(VsLevelForNode("LbRunAndCheck") == VsLevel::Basic);
    CHECK(VsLevelForNode("LbTrySeveral") == VsLevel::Intermediate);
    CHECK(VsLevelForNode("LbRepeatUntilSuccess") == VsLevel::Advanced);
    CHECK(VsLevelForNode("LbWeighPick") == VsLevel::Advanced);
    CHECK(VsLevelForNode("WaitUntil") == VsLevel::Basic);
    CHECK(VsLevelForNode("LineOfSight") == VsLevel::Advanced);
    int lbBasic = 0, lbMid = 0, lbAdv = 0;
    for (const VsDef& d : Registry()) {
        if (!d.type || std::strncmp(d.type, "Lb", 2) != 0) continue;
        if (d.level == VsLevel::Basic) ++lbBasic;
        else if (d.level == VsLevel::Intermediate) ++lbMid;
        else ++lbAdv;
        const char* help = VsNodeHelp(d.type);
        CHECK(help && help[0] && std::strchr(help, '\n') == nullptr);
        CHECK(help && std::strlen(help) < 180);
        CHECK(d.level == VsLevelForNode(d.type));
    }
    CHECK(lbBasic >= 20);
    CHECK(lbMid >= 20);
    CHECK(lbAdv >= 20);
    std::printf("  logic library levels: %d basic, %d intermediate, %d advanced\n", lbBasic, lbMid, lbAdv);

    TEST_MAIN_RESULT();
}
