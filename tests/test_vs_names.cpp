// Friendly names: every node in a finished group has a title and pins that
// follow docs/visual_scripting_naming.md, the old title is still a search
// keyword, and the file token still builds a node. Groups land one at a time.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/VisualScript/NodeAliases.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include <Okay.hpp>
#include <cstring>
#include <string>

using namespace okay::editor::vsed;

namespace {
const int kGatedNameCategories[] = {CAT_EVENT};   // add the next group here when its rows land

bool Gated(int cat) {
    for (int c : kGatedNameCategories) if (c == cat) return true;
    return false;
}
} // namespace

int main() {
    RUN_SUITE("vs_names");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};

    // The rules themselves, including the examples on the naming page.
    CHECK(LintVsTitle("Wait Seconds", VsNameKind::Action).empty());
    CHECK(LintVsTitle("Is Touching", VsNameKind::Question).empty());
    CHECK(LintVsTitle("Move Towards", VsNameKind::Action).empty());
    CHECK(LintVsTitle("Get Position", VsNameKind::Reading).empty());
    CHECK(LintVsTitle("On Every Frame", VsNameKind::Event).empty());
    CHECK(!LintVsTitle("Delay (s)", VsNameKind::Action).empty());
    CHECK(!LintVsTitle("OverlapTest", VsNameKind::Question).empty());
    CHECK(!LintVsTitle("MoveTowardsVec3", VsNameKind::Action).empty());
    CHECK(!LintVsTitle("On Var Changed", VsNameKind::Event).empty());
    CHECK(!LintVsTitle("Add (+)", VsNameKind::Calculation).empty());
    CHECK(LintVsPin("Seconds").empty());
    CHECK(LintVsPin("Previous").empty());
    CHECK(LintVsPin("").empty());
    CHECK(!LintVsPin("Old").empty());
    CHECK(!LintVsPin("dt").empty());

    CHECK(okay::vs::CanonicalNodeType("OnStart") == "OnStart");
    CHECK(okay::vs::CanonicalNodeType("NoSuchNode") == "NoSuchNode");

    int gated = 0;
    for (const VsDef& d : Registry()) {
        if (!Gated(d.cat)) continue;
        ++gated;
        const VsName* n = FindVsName(d.type);
        CHECK(n != nullptr);
        if (!n) { std::printf("  missing friendly name: %s\n", d.type); continue; }
        CHECK(n->kind == VsNameKind::Event);
        CHECK(std::strcmp(d.title, n->title) == 0);
        const std::string titleWhy = LintVsTitle(d.title, n->kind);
        CHECK(titleWhy.empty());
        if (!titleWhy.empty()) std::printf("  %s title \"%s\": %s\n", d.type, d.title, titleWhy.c_str());
        CHECK(n->oldTitle && n->oldTitle[0]);
        const std::string keys = VsSearchKeywords(d.type);
        CHECK(keys.find(n->oldTitle) != std::string::npos);
        for (const char* p : d.dataIn) {
            const std::string why = LintVsPin(p);
            CHECK(why.empty());
            if (!why.empty()) std::printf("  %s pin \"%s\": %s\n", d.type, p ? p : "", why.c_str());
        }
        for (const char* p : d.dataOut) {
            const std::string why = LintVsPin(p);
            CHECK(why.empty());
            if (!why.empty()) std::printf("  %s pin \"%s\": %s\n", d.type, p ? p : "", why.c_str());
        }
        if (n->outPins) {
            CHECK(n->outCount == (int)d.dataOut.size());
            for (int i = 0; i < n->outCount && i < (int)d.dataOut.size(); ++i)
                CHECK(std::strcmp(d.dataOut[(std::size_t)i], n->outPins[i]) == 0);
        }
        std::string err;
        CHECK(okay::vs::MakeNode(d.type, {}, &err) != nullptr);
        if (!err.empty() && err.find("unknown") != std::string::npos)
            std::printf("  %s did not load: %s\n", d.type, err.c_str());
    }
    CHECK(gated == (int)(sizeof kVsNames / sizeof kVsNames[0]));
    std::printf("  events named: %d\n", gated);

    // Every row already in the table follows the rules, gated or not.
    for (const VsName& n : kVsNames) {
        const std::string why = LintVsTitle(n.title, n.kind);
        CHECK(why.empty());
        if (!why.empty()) std::printf("  table %s \"%s\": %s\n", n.type, n.title, why.c_str());
        for (int i = 0; i < n.inCount; ++i) {
            const std::string pinWhy = LintVsPin(n.inPins[i]);
            CHECK(pinWhy.empty());
        }
        for (int i = 0; i < n.outCount; ++i) {
            const std::string pinWhy = LintVsPin(n.outPins[i]);
            CHECK(pinWhy.empty());
        }
    }

    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
