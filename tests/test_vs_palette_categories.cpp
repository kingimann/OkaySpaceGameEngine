// Every visual-script node and every Actions step has a real topic.
// A missing topic, or a catch-all such as "Do something", fails the test.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include <fstream>
#include <set>
#include <string>

using namespace okay::editor::vsed;

namespace {
void CheckPlace(const char* what, const VsPlace& p) {
    CHECK(p.category != nullptr && p.category[0]);
    if (!p.category || !p.category[0]) { std::printf("  no category: %s\n", what); return; }
    CHECK(VsPaletteCategoryIndex(p.category) >= 0);
    if (VsPaletteCategoryIndex(p.category) < 0) std::printf("  %s is in unknown topic '%s'\n", what, p.category);
    CHECK(!VsCatchAllCategory(p.category));
    CHECK(!VsCatchAllCategory(p.subcategory));
    if (VsCatchAllCategory(p.category) || VsCatchAllCategory(p.subcategory))
        std::printf("  %s sits in a catch-all ('%s' / '%s')\n", what, p.category, p.subcategory ? p.subcategory : "");
}
} // namespace

int main() {
    RUN_SUITE("vs_palette_categories");
    CHECK(VsCatchAllCategory("Do something"));
    CHECK(VsCatchAllCategory("Check something"));
    CHECK(VsCatchAllCategory("Engine API"));
    CHECK(!VsCatchAllCategory("Events"));
    CHECK(!VsCatchAllCategory("Movement"));
    for (int i = 0; i < VsPaletteCategoryCount(); ++i) {
        CHECK(!VsCatchAllCategory(kPaletteCategories[i].name));
        CHECK(kPaletteCategories[i].name && kPaletteCategories[i].name[0]);
        CHECK(kPaletteCategories[i].blurb && kPaletteCategories[i].blurb[0]);
    }
    // The menu order is the documented order: common topics, then the rarer ones.
    CHECK(std::strcmp(kPaletteCategories[0].name, "Events") == 0);
    CHECK(std::strcmp(kPaletteCategories[1].name, "Flow") == 0);
    CHECK(std::strcmp(kPaletteCategories[VsPaletteCategoryCount() - 1].name, "Advanced") == 0);

    for (const VsDef& d : Registry()) {
        const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
        const VsPlace p = api ? VsPlaceForApi(api->apiType.c_str(), api->member.c_str())
                              : VsPlaceForNode(d.type);
        CheckPlace(d.type, p);
    }
    for (const okay::vs::VsApiEntry& e : okay::vs::VsApiEntries())
        CheckPlace(e.type.c_str(), VsPlaceForApi(e.apiType.c_str(), e.member.c_str()));

    // Mathf.Abs is the same job as Abs, so it is not a second browse entry.
    bool absHidden = false, absHasTopic = false;
    for (const okay::vs::VsApiEntry& e : okay::vs::VsApiEntries()) {
        if (e.apiType != "Mathf" || e.member != "Abs") continue;
        absHasTopic = VsPaletteCategoryIndex(VsPlaceForApi(e.apiType.c_str(), e.member.c_str()).category) == VsPaletteCategoryIndex("Math");
        for (const VsDef& h : Registry()) {
            if (std::strchr(h.type, '.')) continue;
            if (VsApiDuplicatesHandNode(e.member.c_str(), h.type)) absHidden = true;
        }
    }
    CHECK(absHasTopic);
    CHECK(absHidden);

    // Every Actions step group in the editor source maps to a real topic.
    std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp");
    CHECK(in.good());
    std::string line;
    bool inOps = false, seenInstr = false;
    int steps = 0;
    std::set<std::string> seen;
    while (std::getline(in, line)) {
        if (line.find("static const ActionOpInfo kCondOps[]") != std::string::npos) inOps = true;
        if (line.find("kInstrOps[]") != std::string::npos) seenInstr = true;
        if (seenInstr && line.size() >= 2 && line.find("};") != std::string::npos && line.find('{') == std::string::npos) break;
        if (!inOps) continue;
        const auto brace = line.find("{\"");
        const auto end = line.rfind("\"},");
        if (brace == std::string::npos || end == std::string::npos || end < brace) continue;
        const auto opQ = line.find('"', brace);
        const auto opE = line.find('"', opQ + 1);
        if (opQ == std::string::npos || opE == std::string::npos) continue;
        const std::string op = line.substr(opQ + 1, opE - opQ - 1);
        const auto gQ = line.rfind('"', end - 1);
        if (gQ == std::string::npos) continue;
        const std::string group = line.substr(gQ + 1, end - gQ - 1);
        if (group.empty() || group.size() > 40) continue;
        VsPlace p = VsPlaceForStep(op.c_str(), group.c_str());
        if (!p.category) std::printf("  step %s group '%s' has no topic\n", op.c_str(), group.c_str());
        CheckPlace((op + " [" + group + "]").c_str(), p);
        seen.insert(group);
        ++steps;
    }
    CHECK(steps > 200);
    CHECK(seen.count("Basic") || seen.count("Look") || seen.count("Move"));
    std::printf("  steps checked: %d across %d old groups\n", steps, (int)seen.size());

    TEST_MAIN_RESULT();
}
