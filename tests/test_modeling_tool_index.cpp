// The Modeling window as data (editor/ModelingToolIndex.hpp): every tool listed once, pages grouped by job, search by
// name and alias. Also reads editor/main.cpp so a new button that is not in the index fails here.
#include "ModelingToolIndex.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
using namespace oked::modeltools;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static bool Has(const std::vector<Hit>& h, const char* name) { for (const Hit& x : h) if (std::string(x.tool->name) == name) return true; return false; }

int main() {
    // Plain page names for a 10-year-old: the exact titles, no 3D jargon in a title or in its one help line.
    {
        const char* want[][2] = {{"Easy", "8"}, {"Add shapes", "0"}, {"Step by step", "7"}, {"Make shapes", "4"}, {"Change the shape", "1"},
                                 {"Bend and cut", "2"}, {"Fix the surface", "5"}, {"Effects", "3"}, {"Bring in a model", "6"}};
        CHECK(Pages().size() == 9);
        for (const auto& w : want) { const Page* p = FindPage(std::atoi(w[1])); CHECK(p && std::string(p->name) == w[0]); }
        for (const Page& p : Pages()) {
            const std::string all = std::string(p.name) + " " + p.desc + " " + p.job;
            for (const char* bad : {"Edit Mesh", "Modifiers", "Generate", "Guided", "Import", "Mesh", "Primitive", "Vertex", "Topology"}) CHECK(all.find(bad) == std::string::npos);
            CHECK(std::string(p.name).size() <= 18);   // fits the sidebar on one line
        }
    }
    // The window's own page table (main.cpp) uses the same titles as the index, so the title line, the search rows and the list agree.
    {
        std::ifstream mi(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp"); std::stringstream ms; ms << mi.rdbuf();
        const std::string m = ms.str();
        for (const Page& p : Pages()) CHECK(m.find(std::string("{\"") + p.name + "\",") != std::string::npos);
    }
    // Pages: the "Easy" page first (the simple view, the default), then the eight old pages, each once, each under a job heading, in job order.
    CHECK(Pages().front().legacy == 8 && std::string(Pages().front().name) == "Easy" && std::string(Pages().front().job) == "Start here");
    for (const Page& p : Pages()) { CHECK(std::string(p.name).find("dvanced") == std::string::npos); CHECK(std::string(p.desc).find("dvanced") == std::string::npos); CHECK(std::string(p.job).find("dvanced") == std::string::npos); }
    std::set<int> pg; std::string lastJob; std::set<std::string> jobsSeen;
    for (const Page& p : Pages()) {
        CHECK(pg.insert(p.legacy).second);
        CHECK(p.legacy >= 0 && p.legacy <= 8);
        if (p.job != lastJob) { CHECK(jobsSeen.insert(p.job).second); lastJob = p.job; }   // a job heading never comes back twice
        CHECK(std::string(p.desc).size() > 8 && std::string(p.desc).size() <= 26);   // fits the sidebar without being cut
    }
    CHECK(pg.size() == 9 && jobsSeen.size() == 6);

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
    CHECK(FindPage(Search("loop cut")[0].tool->page) != nullptr && std::string(FindPage(Search("loop cut")[0].tool->page)->name) == "Fix the surface");
    CHECK(Has(Search("fix the surface", 300), "Loop Cut"));                                 // the page name is searchable too
    CHECK(Has(Search("edit mesh", 300), "Loop Cut") && Has(Search("modifiers", 300), "Union"));   // ... and so is the old name (people who know it)

    // Sections are the headings the window really shows (the old Edit page put Mirror and Spherify under "UV Unwrap").
    for (const Tool& t : Tools()) { if (std::string(t.name) == "Mirror X") CHECK(std::string(t.section) == "Symmetry, deform and shell"); if (std::string(t.name) == "Bevel") CHECK(std::string(t.section) == "Edge loops and joins"); if (std::string(t.name) == "Extrude Faces") CHECK(std::string(t.section) == "Faces"); }
    // Highlight lookup by the ImGui label.
    CHECK(FindByLabel(5, "Bevel##me") != nullptr && FindByLabel(2, "Bevel##me") == nullptr);
    CHECK(HighlightLabel(5, "Bevel##me", "bevel") && !HighlightLabel(5, "Knife##me", "bevel") && !HighlightLabel(5, "Bevel##me", ""));
    CHECK(HighlightLabel(5, "Bevel##me", "round"));

    // No Advanced switch is left in the window code, and the page list is the only way between pages.
    {
        std::ifstream mi(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp"); std::stringstream ms; ms << mi.rdbuf();
        const std::string m = ms.str();
        CHECK(m.find("modelingAdvanced") == std::string::npos && m.find("Back to the simple view") == std::string::npos);
        CHECK(m.find("DrawEasyModeling(h);") != std::string::npos);        // the simple view is a page of the window
        CHECK(m.find("s_mdlPage == 8") != std::string::npos);
    }
    // Every tool is still in the index: the tool list did not shrink and the search finds a tool from each old page.
    for (int pgn = 0; pgn <= 7; ++pgn) { int n = 0; for (const Tool& t : Tools()) if (t.page == pgn) ++n; CHECK(n > 0); }
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
        if (lab.rfind("##", 0) == 0 || lab.find("##mdlfind") != std::string::npos) continue;   // window chrome, not tools
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
