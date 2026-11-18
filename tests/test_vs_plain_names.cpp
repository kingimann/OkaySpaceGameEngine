// Plain words: every node in the add-node palette has a short unique name, a one-line description and an example,
// and none of them uses a programming word a beginner would not know.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "VisualScriptGlossary.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include <cstring>
#include <map>
#include <string>

using namespace okay::editor::vsed;

int main() {
    RUN_SUITE("vs_plain_names");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};
    CHECK(VsGlossaryViolation("Set Variable") == "variable");
    CHECK(VsGlossaryViolation("Move Object").empty());
    int shown = 0, badName = 0, noDesc = 0, badDesc = 0, noEx = 0, dup = 0;
    std::map<std::string, int> titles;
    const bool list = std::getenv("VS_PLAIN_LIST") != nullptr;
    for (const VsDef& d : Registry()) {
        if (VsPaletteHidden(d)) continue;
        ++shown;
        titles[d.title ? d.title : ""]++;
        const std::string v = VsGlossaryViolation(d.title ? d.title : "");
        if (!v.empty()) { ++badName; if (list) std::printf("NAME %s '%s' -> %s\n", d.type, d.title, v.c_str()); }
        const char* help = VsNodeHelp(d.type);
        const VsNodeDoc* doc = VsNodeDocFor(d.type);
        const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
        std::string desc = help ? help : (api && !api->doc.empty()) ? VsPlainText(api->doc) : (doc ? VsPlainText(doc->description) : "");
        if (desc.empty()) { ++noDesc; if (list) std::printf("NODESC %s\n", d.type); }
        const std::string dv = VsGlossaryViolation(desc);
        if (!dv.empty()) { ++badDesc; if (list) std::printf("DESC %s '%s' -> %s\n", d.type, desc.c_str(), dv.c_str()); }
        const char* ex = VsNodeExample(d.type);
        if (!(ex && ex[0]) && !(doc && !doc->example.empty())) { ++noEx; }
    }
    for (auto& kv : titles) if (kv.second > 1) { ++dup; if (list) for (const VsDef& d : Registry()) if (!VsPaletteHidden(d) && d.title && kv.first == d.title) std::printf("DUPTYPE %s\n", d.type); if (list) std::printf("DUP '%s' x%d\n", kv.first.c_str(), kv.second); }
    std::printf("  palette nodes %d: bad names %d, no description %d, jargon descriptions %d, no example %d, duplicate names %d\n",
                shown, badName, noDesc, badDesc, noEx, dup);
    CHECK(badName == 0); CHECK(noDesc == 0); CHECK(badDesc == 0); CHECK(noEx == 0); CHECK(dup == 0);
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
