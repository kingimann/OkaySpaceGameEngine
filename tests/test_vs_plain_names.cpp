// Plain words: every node in the add-node palette has a short unique name, a one-line description and an example,
// and none of them uses a programming word a beginner would not know.
#include "test_framework.hpp"
#include "VisualScriptModel.hpp"
#include "VisualScriptGlossary.hpp"
#include "VisualScriptPanelWords.hpp"
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

    // The left panel: every label and sentence is plain, short, and free of the old jargon.
    {
        int bad = 0, n = 0;
        for (const char* w : kPanelAllWords) {
            ++n;
            const std::string text = w;
            std::string lower;
            for (char c : text) lower += (char)std::tolower((unsigned char)c);
            const std::string v = VsGlossaryViolation(text);
            bool old = false;
            for (const char* j : {"declar", "struct", "variable", "parameter", "function", "get var", "set var"})
                if (lower.find(j) != std::string::npos) old = true;
            // One short sentence: no more than one full stop, and not long.
            int stops = 0;
            for (std::size_t i = 0; i + 1 < text.size(); ++i) if (text[i] == '.' && text[i + 1] == ' ') ++stops;
            const bool tooLong = text.size() > 80;
            if (!v.empty() || old || stops > 0 || tooLong) { ++bad; std::printf("  panel word '%s' (banned '%s', old %d, stops %d, long %d)\n", w, v.c_str(), (int)old, stops, (int)tooLong); }
        }
        CHECK(n >= 30 && bad == 0);
        for (VsVarScope sc : {VsVarScope::Object, VsVarScope::Scene, VsVarScope::Global,
                                        VsVarScope::Local, VsVarScope::Story, VsVarScope::OtherObject})
            CHECK(VsGlossaryViolation(VsVarScopeName(sc)).empty());
        std::printf("  left panel: %d labels and sentences checked\n", n);
    }
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
