// Dropdown lists, required-input badges, and which wires are refused.
#include "test_framework.hpp"
#include "VisualScriptChoices.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <string>
#include <vector>

using namespace okay::editor::vsed;
using namespace okay::vs;

int main() {
    RUN_SUITE("vs_choices");

    CHECK(VsArgChoice("PlayClip") == VsChoiceKind::Clip);
    CHECK(VsArgChoice("FindWithTag") == VsChoiceKind::Tag);
    CHECK(VsArgChoice("LoadScene") == VsChoiceKind::Scene);
    CHECK(VsArgChoice("GetVar") == VsChoiceKind::Variable);
    CHECK(VsArgChoice("SendEvent") == VsChoiceKind::Event);
    CHECK(VsArgChoice("MusicPlay") == VsChoiceKind::Sound);
    CHECK(VsArgChoice("AnimTrigger") == VsChoiceKind::AnimParam);
    CHECK(VsArgChoice("Print") == VsChoiceKind::None);

    {
        VsChoiceLists lists;
        lists.onTarget = {"Walk", "Run"};
        lists.fallback = {"idle", "Wave", "Run"};
        const auto rows = VsChoiceRows(VsChoiceKind::Clip, lists);
        CHECK(rows.size() == 2 && rows[0] == "Walk" && rows[1] == "Run");
        CHECK(!VsChoiceMissing("Run", rows));
        CHECK(VsChoiceMissing("Dance", rows));
        CHECK(!VsChoiceMissing("", rows));
        CHECK(!VsChoiceMissing("@anim:Assets/Run.okayanim", rows));
        lists.onTarget.clear();
        const auto all = VsChoiceRows(VsChoiceKind::Clip, lists);
        CHECK(all.size() == 3 && all[0] == "idle");
    }
    {
        VsChoiceLists tags;
        tags.onTarget = {"Player", "Enemy"};
        const auto rows = VsChoiceRows(VsChoiceKind::Tag, tags);
        CHECK(rows.size() == 2);
        CHECK(VsChoiceMissing("Boss", rows));
    }

    auto none = [](int) { return false; };
    auto cond = [](int pin) { return pin == 0; };
    std::string miss;
    CHECK(VsNodeReadiness("PlayClip", false, none, false, &miss) == VsReady::Missing);
    CHECK(miss == "Required: clip name");
    miss.clear();
    CHECK(VsNodeReadiness("PlayClip", true, none, false, &miss) == VsReady::Ready);
    CHECK(VsNodeReadiness("Branch", false, none, false, &miss) == VsReady::Missing);
    miss.clear();
    CHECK(VsNodeReadiness("Branch", false, cond, false, &miss) == VsReady::Ready);
    CHECK(VsNodeReadiness("Spawn", false, none, false, &miss) == VsReady::Missing);
    CHECK(VsNodeReadiness("OnUpdate", false, none, false, &miss) == VsReady::Ready);
    CHECK(VsNodeReadiness("PlayClip", true, none, true, &miss) == VsReady::Error);
    CHECK(std::string(VsReadyName(VsReady::Missing)) == "Missing");

    {
        const auto offer = VsOfferConvert((char)PinType::Text, (char)PinType::Number);
        CHECK(offer.type == "ToNumber" && offer.label.find("To Number") != std::string::npos);
        const auto text = VsOfferConvert((char)PinType::Number, (char)PinType::Text);
        CHECK(text.type == "ToString");
        const auto vec = VsOfferConvert((char)PinType::Vec3, (char)PinType::Number);
        CHECK(vec.type.empty() && vec.why.find("Get X") != std::string::npos);
        CHECK(VsOfferConvert((char)PinType::Number, (char)PinType::Number).type.empty());
    }

    {
        std::vector<std::pair<int, int>> edges{{1, 2}, {2, 3}};
        CHECK(VsDataWouldCycle(edges, 3, 1));
        CHECK(!VsDataWouldCycle(edges, 1, 4));
        CHECK(VsDataWouldCycle(edges, 2, 2));
    }

    {
        const auto v = ValidateGraphText(
            "node 1 OnStart\nnode 2 PlayClip\nnode 3 Branch\nnode 4 MoveTowards\n"
            "exec 1 0 2\nexec 2 0 3\nentry OnStart 1\n");
        int clip = 0, cond = 0, obj = 0;
        for (const auto& is : v) {
            if (is.message.find("clip name") != std::string::npos) ++clip;
            if (is.message.find("condition") != std::string::npos) ++cond;
            if (is.message.find("object to move toward") != std::string::npos) ++obj;
        }
        CHECK(clip == 1 && cond == 1 && obj == 1);
        const auto ok = ValidateGraphText(
            "node 1 OnStart\nnode 2 PlayClip Run\nexec 1 0 2\nentry OnStart 1\n");
        int still = 0;
        for (const auto& is : ok) if (is.message.find("clip name") != std::string::npos) ++still;
        CHECK(still == 0);
    }

    CHECK(VsPinExplain("Print", "Text", "Text", true, false).find("wire a value") != std::string::npos);
    CHECK(VsPinExplain("OnStart", "", "flow", false, true).find("what runs next") != std::string::npos);

    TEST_MAIN_RESULT();
}
