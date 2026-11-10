// Every node in the visual-script palette has a description, pin docs and requirements.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"

#include <cstdlib>
#include <fstream>
#include <string>

using namespace okay::editor::vsed;

int main() {
    RUN_SUITE("vs_node_docs");
    std::vector<std::string> order;
    int documented = 0;
    for (const auto& def : Registry()) {
        order.push_back(def.type);
        const VsNodeDoc* doc = VsNodeDocFor(def.type);
        if (!doc || doc->description.size() < 12 || doc->requirements.empty() || doc->example.empty()) {
            std::printf("  missing doc: %s\n", def.type);
            CHECK(false);
            continue;
        }
        const char* bad[] = {"TODO", "TBD", "placeholder", "FIXME", "lorem"};
        for (const char* b : bad) {
            if (doc->description.find(b) != std::string::npos || doc->requirements.find(b) != std::string::npos) {
                std::printf("  placeholder in %s\n", def.type);
                CHECK(false);
            }
        }
        if (def.execIn && !VsPinDocFor(*doc, true, true, "")) {
            std::printf("  %s: no exec-in doc\n", def.type);
            CHECK(false);
        }
        for (const char* name : def.execOut) {
            if (!VsPinDocFor(*doc, false, true, name ? name : "")) {
                std::printf("  %s: no exec-out doc '%s'\n", def.type, name ? name : "");
                CHECK(false);
            }
        }
        for (const char* name : def.dataIn) {
            const VsPinDoc* p = VsPinDocFor(*doc, true, false, name ? name : "");
            if (!p || p->meaning.size() < 8 || p->type.empty()) {
                std::printf("  %s: input '%s' is not documented\n", def.type, name ? name : "");
                CHECK(false);
            }
        }
        for (const char* name : def.dataOut) {
            const VsPinDoc* p = VsPinDocFor(*doc, false, false, name ? name : "");
            if (!p || p->meaning.size() < 8 || p->type.empty()) {
                std::printf("  %s: output '%s' is not documented\n", def.type, name ? name : "");
                CHECK(false);
            }
        }
        if (def.param && doc->setting.empty()) {
            std::printf("  %s: the node's setting is not documented\n", def.type);
            CHECK(false);
        }
        ++documented;
    }
    CHECK(documented == (int)Registry().size());
    std::printf("  documented %d nodes\n", documented);

    const VsNodeDoc* force = VsNodeDocFor("AddForce");
    CHECK(force && force->component.find("Rigidbody3D") != std::string::npos);

    const std::string page = VsNodeDocsMarkdown(order);
    CHECK(page.find("### On Start") != std::string::npos);
    CHECK(page.find("### Add Force") != std::string::npos);
    if (const char* w = std::getenv("OKAY_WRITE_VS_DOCS")) {
        std::ofstream f(w);
        f << page;
        std::printf("  wrote %s\n", w);
    }
    TEST_MAIN_RESULT();
}
