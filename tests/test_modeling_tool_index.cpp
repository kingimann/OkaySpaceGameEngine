// The advanced Modeling window as data (editor/ModelingToolIndex.hpp): every tool listed once, pages grouped by job, search by
// name and alias. Also reads editor/main.cpp so a new button that is not in the index fails here.
#include "ModelingToolIndex.hpp"
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
using namespace oked::modeltools;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static bool Has(const std::vector<Hit>& h, const char* name) { for (const Hit& x : h) if (std::string(x.tool->name) == name) return true; return false; }

int main() {
    // Pages: the eight old pages, each once, each under a job heading, in job order.
    std::set<int> pg; std::string lastJob; std::set<std::string> jobsSeen;
    for (const Page& p : Pages()) {
        CHECK(pg.insert(p.legacy).second);
        CHECK(p.legacy >= 0 && p.legacy <= 7);
        if (p.job != lastJob) { CHECK(jobsSeen.insert(p.job).second); lastJob = p.job; }   // a job heading never comes back twice
        CHECK(std::string(p.desc).size() > 8 && std::string(p.desc).size() <= 26);   // fits the sidebar without being cut
    }
    CHECK(pg.size() == 8 && jobsSeen.size() == 5);

    // Tools: unique ids, real pages, names, no empty hints on anything with an alias, no duplicate (page, name).
    std::set<std::string> ids, pageName;
    for (const Tool& t : Tools()) {
        CHECK(ids.insert(t.id).second);
        CHECK(FindPage(t.page) != nullptr);
        CHECK(std::string(t.name).size() > 0 && std::string(t.section).size() > 0);
        CHECK(pageName.insert(std::to_string(t.page) + "|" + t.name).second);   // two buttons never share a name on a page
    }
    CHECK(Tools().size() > 150);

    // Search: by name, by alias, by section; the page is named; ranking puts the exact name first.
    auto h = Search("bevel"); CHECK(!h.empty() && std::string(h[0].tool->name) == "Bevel");
    CHECK(Has(Search("round"), "Bevel") && Has(Search("round"), "Smooth"));          // plain word finds the technical tool
    CHECK(Has(Search("push out"), "Extrude Faces"));
    CHECK(Has(Search("boolean"), "Union") && Has(Search("boolean"), "Subtract") && Has(Search("boolean"), "Intersect"));
    CHECK(Has(Search("uv"), "Unwrap"));
    CHECK(Has(Search("EXTRUDE"), "Extrude Faces") && Has(Search("  extrude "), "Extrude Edges"));
    CHECK(Has(Search("mirror"), "Mirror X"));
    CHECK(Has(Search("fence"), "Fence") && Has(Search("fence"), "Guided designs"));
    CHECK(Has(Search("sculpt"), "Sculpt Brush (drag on mesh)"));
    CHECK(Search("zzzqq").empty() && Search("").empty() && Search("   ").empty());
    CHECK(Search("e", 5).size() <= 5);
    CHECK(FindPage(Search("loop cut")[0].tool->page) != nullptr && std::string(FindPage(Search("loop cut")[0].tool->page)->name) == "Edit Mesh");
    CHECK(Has(Search("edit mesh", 300), "Loop Cut"));                                       // the page name is searchable too

    // Sections are the headings the window really shows (the old Edit page put Mirror and Spherify under "UV Unwrap").
    for (const Tool& t : Tools()) { if (std::string(t.name) == "Mirror X") CHECK(std::string(t.section) == "Symmetry, deform and shell"); if (std::string(t.name) == "Bevel") CHECK(std::string(t.section) == "Edge loops and joins"); if (std::string(t.name) == "Extrude Faces") CHECK(std::string(t.section) == "Faces"); }
    // Highlight lookup by the ImGui label.
    CHECK(FindByLabel(5, "Bevel##me") != nullptr && FindByLabel(2, "Bevel##me") == nullptr);
    CHECK(HighlightLabel(5, "Bevel##me", "bevel") && !HighlightLabel(5, "Knife##me", "bevel") && !HighlightLabel(5, "Bevel##me", ""));
    CHECK(HighlightLabel(5, "Bevel##me", "round"));

    // Every button label in DrawModeling is in the index (so a tool added later cannot be forgotten).
    std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp"); std::stringstream ss; ss << in.rdbuf(); const std::string src = ss.str();
    const std::size_t a = src.find("\nvoid DrawModeling(EditorState& ed) {");
    CHECK(a != std::string::npos);
    const std::size_t b = src.find("\n}\n", a);
    const std::string body = src.substr(a, b - a);
    std::regex re("(ImGui::Button|ToolButton|SmallButton|AccentToggleButton|ImGui::Checkbox)\\(\\s*\"([^\"]+)\"");
    int found = 0;
    for (std::sregex_iterator it(body.begin(), body.end(), re), end; it != end; ++it) {
        const std::string lab = (*it)[2];
        if (lab.rfind("##", 0) == 0 || lab.find("Back to the simple") == 0 || lab.find("##mdlfind") != std::string::npos) continue;   // window chrome, not tools
        ++found;
        bool ok = false;
        for (const Tool& t : Tools()) { const std::string id = t.id; if (id.size() > lab.size() && id.compare(id.size() - lab.size(), lab.size(), lab) == 0 && id[id.size() - lab.size() - 1] == ':') ok = true; }
        if (!ok) std::printf("  not in the index: %s\n", lab.c_str());
        CHECK(ok);
    }
    CHECK(found > 120);
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("modeling_tool_index: all passed (%d buttons checked)\n", found);
    return 0;
}
