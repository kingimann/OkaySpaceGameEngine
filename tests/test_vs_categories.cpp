// Where every visual-script node sits in the Add Node menu. The topics, the subcategories and the rules that place a
// node are in editor/VisualScriptCategories.hpp and editor/VisualScriptPlaceRules.hpp; the rules for adding a node are
// in docs/visual_scripting_naming.md. Each check below failed on the old 19-topic menu (one topic, "Advanced", held 515
// nodes) or fails if the tree is broken again.
//
//   test_vs_categories              run the checks
//   test_vs_categories --dump FILE  write every palette row and where it sits (scripts/vs_category_report.py reads it)
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/VisualScript/VsApi.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay::editor::vsed;

namespace {

// One row of the Add Node menu: a node, an engine-API node, or an Actions step.
struct Row {
    std::string kind;     // node, library (Lb), gameplay (Gl), builtin (Fn.), api, step, check
    std::string id;       // the type stored in a graph file, or the step's short name
    std::string title;
    std::string group;    // the old step group (steps only)
    std::string api;      // "Type.member" (engine-API rows only)
    std::string help;
    std::string hidden;   // "", dup, untested, listonly: the menu does not list a row that has a reason here
    std::string oldcat;   // the very old category of the registry row (Events, Flow, Engine API ...)
    VsPlace place, was;
    VsLevel level = VsLevel::Intermediate;
    const VsDef* def = nullptr;
    bool listed() const { return hidden.empty(); }
    bool browse() const { return listed() && place.browse; }
    // What the rule tables match on, and which table.
    int table = 0;        // 0 nodes, 1 builtins, 2 api, 3 steps
    std::string key;
};

std::string Clean(std::string s) {
    for (char& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

// ---- the Actions steps live in editor/main.cpp (two tables); read them the way the editor builds its catalogue ----
struct Step { std::string op, label, group, desc; bool cond = false; };

bool ParseEntry(const std::string& src, std::size_t& i, std::vector<std::string>& lits) {
    lits.clear();
    ++i;   // past '{'
    while (i < src.size()) {
        const char c = src[i];
        if (c == '}') { ++i; return true; }
        if (c == '"') {
            std::string cur;
            while (true) {   // adjacent string literals are one string
                ++i;
                while (i < src.size() && src[i] != '"') {
                    if (src[i] == '\\' && i + 1 < src.size()) {
                        const char n = src[i + 1];
                        cur += n == 'n' ? '\n' : n == 't' ? '\t' : n;
                        i += 2;
                    } else cur += src[i++];
                }
                ++i;
                std::size_t j = i;
                while (j < src.size() && std::isspace((unsigned char)src[j])) ++j;
                if (j < src.size() && src[j] == '"') { i = j; continue; }
                break;
            }
            lits.push_back(cur);
            continue;
        }
        ++i;
    }
    return false;
}

void ReadSteps(const std::string& src, const char* array, bool cond, std::vector<Step>& out) {
    const std::size_t a = src.find(std::string("static const ActionOpInfo ") + array + "[] = {");
    if (a == std::string::npos) return;
    std::size_t i = src.find('{', src.find('=', a)) + 1;
    while (i < src.size()) {
        while (i < src.size() && (std::isspace((unsigned char)src[i]) || src[i] == ',')) ++i;
        if (src.compare(i, 2, "//") == 0) { while (i < src.size() && src[i] != '\n') ++i; continue; }
        if (src.compare(i, 2, "/*") == 0) { i = src.find("*/", i) + 2; continue; }
        if (i >= src.size() || src[i] == '}') break;
        if (src[i] != '{') { ++i; continue; }
        std::vector<std::string> l;
        if (!ParseEntry(src, i, l)) break;
        if (l.size() >= 5) out.push_back({l[0], l[1], l[4], l[3], cond});   // op, label, group, desc
    }
}

// The step names the graph menu leaves out (they only make sense inside an Action Steps list): editor/main.cpp's kListOnly.
std::set<std::string> ReadListOnly(const std::string& src) {
    std::set<std::string> out;
    const std::size_t a = src.find("static const std::set<std::string> kListOnly = {");
    if (a == std::string::npos) return out;
    const std::size_t b = src.find("};", a);
    std::size_t i = a;
    while (i < b) {
        i = src.find('"', i);
        if (i == std::string::npos || i >= b) break;
        const std::size_t e = src.find('"', i + 1);
        out.insert(src.substr(i + 1, e - i - 1));
        i = e + 1;
    }
    return out;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<Row> CollectRows(std::vector<Step>* stepsOut = nullptr) {
    static const char* oldCat[] = {"Events", "Flow", "Variables", "Math", "Logic", "Input", "Transform", "Actions", "Data",
                                   "Multiplayer & Steam", "Engine API", "Script builtins"};
    std::vector<Row> rows;
    for (const VsDef& d : Registry()) {
        Row r;
        const okay::vs::VsApiEntry* api = okay::vs::VsApiFind(d.type);
        r.id = d.type;
        r.title = Clean(d.title ? d.title : "");
        r.def = &d;
        r.level = d.level;
        r.oldcat = d.cat >= 0 && d.cat < CAT_COUNT ? oldCat[d.cat] : "";
        if (api) {
            r.kind = "api";
            r.api = api->apiType + "." + api->member;
            r.key = r.api;
            r.table = 2;
            r.place = VsPlaceForApi(api->apiType.c_str(), api->member.c_str());
            r.was = VsOldPlaceForApi(api->apiType.c_str(), api->member.c_str());
        } else {
            const bool builtin = r.id.compare(0, 3, "Fn.") == 0;
            const bool lib = r.id.size() > 2 && r.id.compare(0, 2, "Lb") == 0 && r.id[2] >= 'A' && r.id[2] <= 'Z';
            const bool gl = r.id.size() > 2 && r.id.compare(0, 2, "Gl") == 0 && r.id[2] >= 'A' && r.id[2] <= 'Z';
            r.kind = builtin ? "builtin" : lib ? "library" : gl ? "gameplay" : "node";
            r.table = builtin ? 1 : 0;
            r.key = builtin ? r.id.substr(3) : r.id;
            r.place = VsPlaceForNode(d.type);
            r.was = VsOldPlaceForNode(d.type);
        }
        if (VsPaletteHidesAsDuplicate(d)) r.hidden = "dup";
        else if (VsPaletteHiddenUntilTested(d.type)) r.hidden = "untested";
        if (const char* what = VsNodeHelp(d.type)) r.help = what;
        else if (api && !api->doc.empty()) r.help = api->doc;
        else if (d.title && d.title[0]) r.help = std::string(d.title) + ".";
        r.help = Clean(r.help);
        rows.push_back(r);
    }
    std::vector<Step> steps;
    const std::string src = ReadFile(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp");
    ReadSteps(src, "kCondOps", true, steps);
    ReadSteps(src, "kInstrOps", false, steps);
    const std::set<std::string> listOnly = ReadListOnly(src);
    for (const Step& s : steps) {
        Row r;
        r.kind = s.cond ? "check" : "step";
        r.id = s.op;
        r.title = Clean(s.label);
        r.group = s.group;
        r.help = Clean(s.desc);
        r.table = 3;
        r.key = s.op;
        r.place = VsPlaceForStep(s.op.c_str(), s.group.c_str());
        r.was = VsOldPlaceForStep(s.op.c_str(), s.group.c_str());
        r.level = VsLevelForStep(s.op.c_str(), s.group.c_str());
        if (!s.cond && listOnly.count(s.op)) r.hidden = "listonly";
        else if (VsPaletteStepIsDuplicate(s.label)) r.hidden = "dup";
        rows.push_back(r);
    }
    if (stepsOut) *stepsOut = steps;
    return rows;
}

bool PlainSentence(const char* s, std::size_t maxLen = 130) {
    if (!s || !s[0]) return false;
    const std::string t = s;
    if (t.size() < 12 || t.size() > maxLen) return false;
    if (!std::isupper((unsigned char)t[0])) return false;
    if (t.back() != '.' && t.back() != '?') return false;
    for (const char* bad : {"TODO", "TBD", "FIXME", "lorem", "placeholder"})
        if (t.find(bad) != std::string::npos) return false;
    for (char c : t) if (c == '`' || c == '<' || c == '>' || c == '*' || c == '_' || c == '\t' || c == '\n') return false;
    return true;
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Words of a text, lower case, lightly stemmed (splines -> spline, building -> build), to compare a name with a description.
std::set<std::string> StemmedWords(const std::string& text) {
    std::set<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        if (cur.size() > 5 && cur.compare(cur.size() - 3, 3, "ing") == 0) cur.resize(cur.size() - 3);
        else if (cur.size() > 3 && cur.back() == 's' && cur[cur.size() - 2] != 's') cur.pop_back();
        out.insert(cur);
        cur.clear();
    };
    for (char c : text) {
        if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
        else flush();
    }
    flush();
    return out;
}

std::string PlaceName(const VsPlace& p) {
    return std::string(p.category ? p.category : "(none)") + (p.subcategory && p.subcategory[0] ? std::string(" / ") + p.subcategory : "");
}

// The table row (if any) that wins for this key, and every row that matches it.
int FirstMatch(const VsRule* rules, std::size_t n, const char* key) {
    for (std::size_t i = 0; i < n; ++i) if (VsRuleMatches(rules[i].match, key)) return (int)i;
    return -1;
}

// The topic list that docs/visual_scripting_naming.md shows (between the two markers there). One source: this test
// prints it with --topic-table and checks the page against it, so the page cannot go stale.
std::string TopicTable() {
    std::string out = "| Section | Topic | What goes here | Subcategories |\n|---|---|---|---|\n";
    std::string lastSection;
    for (int i = 0; i < VsPaletteCategoryCount(); ++i) {
        const VsPaletteCategory& c = kPaletteCategories[i];
        std::string subs;
        for (int k = 0; k < VsPaletteSubCount(); ++k)
            if (std::strcmp(kPaletteSubs[k].category, c.name) == 0) subs += (subs.empty() ? "" : ", ") + std::string(kPaletteSubs[k].name);
        const std::string section = lastSection == c.section ? "" : std::string("**") + c.section + "**";
        lastSection = c.section;
        out += "| " + section + " | " + c.name + " | " + c.blurb + " | " + subs + " |\n";
    }
    return out;
}

int RunDump(const char* path) {
    const std::vector<Row> rows = CollectRows();
    std::ofstream out(path);
    if (!out) { std::fprintf(stderr, "cannot write %s\n", path); return 2; }
    for (int i = 0; i < VsPaletteCategoryCount(); ++i)
        out << "#cat\t" << kPaletteCategories[i].name << "\t" << kPaletteCategories[i].section << "\t" << kPaletteCategories[i].blurb << "\n";
    for (int i = 0; i < VsPaletteSubCount(); ++i)
        out << "#sub\t" << kPaletteSubs[i].category << "\t" << kPaletteSubs[i].name << "\t" << kPaletteSubs[i].blurb << "\n";
    for (int i = 0; i < VsLegacyCategoryCount(); ++i)
        out << "#legacycat\t" << kLegacyPaletteCategories[i].name << "\t" << kLegacyPaletteCategories[i].blurb << "\n";
    out << "kind\tid\ttitle\tcategory\tsubcategory\tbrowse\tlevel\toldcat\tapi\thidden\tgroup\thelp\twas_category\twas_subcategory\n";
    for (const Row& r : rows)
        out << r.kind << '\t' << r.id << '\t' << r.title << '\t' << (r.place.category ? r.place.category : "") << '\t'
            << (r.place.subcategory ? r.place.subcategory : "") << '\t' << (r.place.browse ? 1 : 0) << '\t' << VsLevelName(r.level) << '\t'
            << r.oldcat << '\t' << r.api << '\t' << r.hidden << '\t' << r.group << '\t' << r.help << '\t'
            << (r.was.category ? r.was.category : "") << '\t' << (r.was.subcategory ? r.was.subcategory : "") << '\n';
    std::printf("wrote %d rows to %s\n", (int)rows.size(), path);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--dump") return RunDump(argv[2]);
    if (argc >= 2 && std::string(argv[1]) == "--topic-table") { std::fputs(TopicTable().c_str(), stdout); return 0; }
    RUN_SUITE("vs_categories");

    // ======================================================================================================
    // 1. The tree itself
    // ======================================================================================================
    const int topicCount = VsPaletteCategoryCount();
    std::set<std::string> sections;
    CHECK(topicCount >= 35);            // the menu is many specific topics, not a few big ones
    CHECK(topicCount <= 80);
    {
        std::set<std::string> names;
        std::string lastSection;
        std::set<std::string> closedSections;
        for (int i = 0; i < topicCount; ++i) {
            const VsPaletteCategory& c = kPaletteCategories[i];
            CHECK(c.name && c.name[0]);
            CHECK(c.section && c.section[0]);
            if (!c.name || !c.section) continue;
            CHECK(names.insert(c.name).second);                  // one topic, one row
            CHECK(!VsCatchAllCategory(c.name));                  // no "Advanced", no "Misc"
            CHECK(!VsCatchAllCategory(c.section));
            CHECK(PlainSentence(c.blurb));
            if (!PlainSentence(c.blurb)) std::printf("  topic '%s' has no plain blurb\n", c.name);
            if (lastSection != c.section) {                      // topics of one section sit together
                CHECK(closedSections.insert(c.section).second);
                if (!closedSections.count(c.section)) std::printf("  section '%s' is split\n", c.section);
                lastSection = c.section;
            }
            sections.insert(c.section);
            CHECK(VsPaletteCategoryIndex(c.name) == i);
            CHECK(VsPaletteSectionIndex(i) >= 0);
        }
        CHECK(sections.size() >= 8 && sections.size() <= 20);
        // Common topics first: the events, then flow, then data, then math.
        CHECK(std::string(kPaletteCategories[0].name) == "Game Events");
        for (int i = 0; i < 5; ++i) CHECK(std::string(kPaletteCategories[i].section) == "Events");
        CHECK(std::string(kPaletteCategories[5].section) == "Flow");
        int firstData = -1, firstMath = -1, firstOnline = -1;
        for (int i = 0; i < topicCount; ++i) {
            if (firstData < 0 && std::string(kPaletteCategories[i].section) == "Variables, Lists & Saving") firstData = i;
            if (firstMath < 0 && std::string(kPaletteCategories[i].section) == "Math") firstMath = i;
            if (firstOnline < 0 && std::string(kPaletteCategories[i].section) == "Online") firstOnline = i;
        }
        CHECK(firstData > 5 && firstMath > firstData && firstOnline > firstMath);
        CHECK(VsCatchAllCategory("Advanced") && VsCatchAllCategory("Misc") && VsCatchAllCategory("Do something"));
        CHECK(!VsCatchAllCategory("Game Events") && !VsCatchAllCategory("Movement"));

        // Subcategories: declared once, under a real topic, with a plain blurb, unique across topics.
        std::set<std::string> subNames;
        int lastTopic = -1;
        std::set<int> closedTopics;
        for (int i = 0; i < VsPaletteSubCount(); ++i) {
            const VsPaletteSub& s = kPaletteSubs[i];
            const int ti = VsPaletteCategoryIndex(s.category);
            CHECK(ti >= 0);
            if (ti < 0) { std::printf("  subcategory '%s' is under an unknown topic '%s'\n", s.name, s.category); continue; }
            CHECK(s.name && s.name[0]);
            CHECK(subNames.insert(s.name).second);               // the name alone identifies it (VsSubOrder relies on this)
            CHECK(!names.count(s.name));                         // and it is not also a topic
            CHECK(!VsCatchAllCategory(s.name));
            CHECK(PlainSentence(s.blurb));
            if (!PlainSentence(s.blurb)) std::printf("  subcategory '%s' has no plain blurb\n", s.name);
            if (ti != lastTopic) {                               // the subs of one topic are listed together
                CHECK(closedTopics.insert(ti).second);
                lastTopic = ti;
            }
            CHECK(VsPaletteSubIndex(s.category, s.name) == i);
            CHECK(VsSubOrder(s.name) == i);
            CHECK(VsPaletteSubBlurb(s.name) == s.blurb);
        }
        CHECK(VsSubOrder("") == -1);
        CHECK(VsSubOrder("No Such Subcategory") == 1000);
        // The description of a topic (the hover text) mentions everything inside it: each subcategory shares a word with
        // its topic's name or description, so nobody finds a subcategory the hover text never hinted at.
        for (int i = 0; i < VsPaletteSubCount(); ++i) {
            const int ti = VsPaletteCategoryIndex(kPaletteSubs[i].category);
            if (ti < 0) continue;
            const std::set<std::string> topicWords = StemmedWords(std::string(kPaletteCategories[ti].name) + " " + kPaletteCategories[ti].blurb);
            bool shared = false;
            for (const std::string& w : StemmedWords(kPaletteSubs[i].name)) if (topicWords.count(w)) shared = true;
            CHECK(shared);
            if (!shared) std::printf("  the description of '%s' does not mention its subcategory '%s'\n", kPaletteCategories[ti].name, kPaletteSubs[i].name);
        }
    }

    // ======================================================================================================
    // 2. Every row has exactly one valid place
    // ======================================================================================================
    std::vector<Step> steps;
    const std::vector<Row> rows = CollectRows(&steps);
    CHECK(steps.size() > 250);                          // the step tables were read
    {
        std::set<std::string> seen;
        int unplaced = 0, badSub = 0;
        for (const Row& r : rows) {
            CHECK(seen.insert(r.kind + ":" + r.id).second);          // no row twice
            const VsPlace& p = r.place;
            if (!p.category || !p.category[0]) {
                std::printf("  no place: %s %s\n", r.kind.c_str(), r.id.c_str());
                ++unplaced;
                CHECK(false);
                continue;
            }
            if (VsPaletteCategoryIndex(p.category) < 0) {
                std::printf("  %s %s is in unknown topic '%s'\n", r.kind.c_str(), r.id.c_str(), p.category);
                CHECK(false);
                continue;
            }
            CHECK(!VsCatchAllCategory(p.category));
            CHECK(!VsCatchAllCategory(p.subcategory));
            if (p.subcategory && p.subcategory[0] && VsPaletteSubIndex(p.category, p.subcategory) < 0) {
                std::printf("  %s %s is in '%s / %s', which is not a declared subcategory\n", r.kind.c_str(), r.id.c_str(), p.category, p.subcategory);
                ++badSub;
                CHECK(false);
            }
            // The old place is still computable (it feeds the search words) and is one of the 19 old topics.
            bool oldOk = false;
            for (int i = 0; i < VsLegacyCategoryCount(); ++i)
                if (r.was.category && std::strcmp(r.was.category, kLegacyPaletteCategories[i].name) == 0) oldOk = true;
            CHECK(oldOk);
        }
        std::printf("  %d palette rows (%d listed in the menu); %d without a place, %d in an undeclared subcategory\n",
                    (int)rows.size(), (int)std::count_if(rows.begin(), rows.end(), [](const Row& r) { return r.listed(); }), unplaced, badSub);
        CHECK(unplaced == 0 && badSub == 0);
    }
    // Every engine-API entry too (the registry holds them, but a new entry must not slip past).
    for (const okay::vs::VsApiEntry& e : okay::vs::VsApiEntries()) {
        const VsPlace p = VsPlaceForApi(e.apiType.c_str(), e.member.c_str());
        CHECK(p.category && VsPaletteCategoryIndex(p.category) >= 0);
    }
    CHECK(VsPlaceForNode(nullptr).category == nullptr);
    CHECK(VsPlaceForNode("NoSuchNodeType").category == nullptr);   // an unknown type has no place (tests fail on it)

    // ======================================================================================================
    // 3. Every topic and every subcategory is used; none is too big
    // ======================================================================================================
    {
        std::map<std::string, int> direct, total;
        std::map<std::pair<std::string, std::string>, int> inSub;
        for (const Row& r : rows) {
            if (!r.browse() || !r.place.category) continue;
            const std::string c = r.place.category, s = r.place.subcategory ? r.place.subcategory : "";
            ++total[c];
            if (s.empty()) ++direct[c]; else ++inSub[{c, s}];
        }
        int worstDirect = 0, worstSub = 0;
        for (int i = 0; i < topicCount; ++i) {
            const std::string name = kPaletteCategories[i].name;
            CHECK(total[name] > 0);                          // no empty topic
            if (!total[name]) std::printf("  topic '%s' is empty\n", name.c_str());
            CHECK(direct[name] <= kMaxDirectPerTopic);       // the first level stays short; the rest sit in subcategories
            if (direct[name] > kMaxDirectPerTopic) std::printf("  topic '%s' has %d nodes directly in it (cap %d)\n", name.c_str(), direct[name], kMaxDirectPerTopic);
            worstDirect = std::max(worstDirect, direct[name]);
        }
        for (int i = 0; i < VsPaletteSubCount(); ++i) {
            const int n = inSub[{kPaletteSubs[i].category, kPaletteSubs[i].name}];
            CHECK(n > 0);                                    // no empty subcategory
            if (!n) std::printf("  subcategory '%s / %s' is empty\n", kPaletteSubs[i].category, kPaletteSubs[i].name);
            CHECK(n <= kMaxPerSubcategory);
            if (n > kMaxPerSubcategory) std::printf("  subcategory '%s / %s' has %d nodes (cap %d)\n", kPaletteSubs[i].category, kPaletteSubs[i].name, n, kMaxPerSubcategory);
            worstSub = std::max(worstSub, n);
        }
        // The old menu had a 515-node bucket; nothing like it may come back.
        CHECK(worstDirect <= kMaxDirectPerTopic && worstSub <= kMaxPerSubcategory);
        std::printf("  %d topics in %d sections, %d subcategories; the biggest first level has %d nodes, the biggest subcategory %d\n",
                    topicCount, (int)sections.size(), VsPaletteSubCount(), worstDirect, worstSub);
    }

    // ======================================================================================================
    // 4. The rule tables are tidy: every row matters, none hides another
    // ======================================================================================================
    {
        struct Table { const char* name; const VsRule* rules; std::size_t n; };
        const Table tables[4] = {{"nodes", kNodeRules, VsRuleCount(kNodeRules)}, {"builtins", kBuiltinRules, VsRuleCount(kBuiltinRules)},
                                 {"api", kApiRules, VsRuleCount(kApiRules)}, {"steps", kStepRules, VsRuleCount(kStepRules)}};
        for (int t = 0; t < 4; ++t) {
            std::vector<int> wins(tables[t].n, 0), matches(tables[t].n, 0);
            std::set<std::string> patterns;
            for (std::size_t i = 0; i < tables[t].n; ++i) {
                const VsRule& r = tables[t].rules[i];
                const std::string pat = r.match;
                CHECK(!pat.empty());
                CHECK(patterns.insert(pat).second);                  // a pattern twice means the second can never win
                if (!patterns.count(pat)) std::printf("  %s: pattern '%s' is listed twice\n", tables[t].name, r.match);
                const std::size_t star = pat.find('*', pat.empty() ? 0 : 1);
                CHECK(star == std::string::npos || star == pat.size() - 1);   // a '*' only at the start or the end
                CHECK(VsPaletteCategoryIndex(r.category) >= 0);
                if (VsPaletteCategoryIndex(r.category) < 0) std::printf("  %s: '%s' names the unknown topic '%s'\n", tables[t].name, r.match, r.category);
                CHECK(!r.sub[0] || VsPaletteSubIndex(r.category, r.sub) >= 0);
                if (r.sub[0] && VsPaletteSubIndex(r.category, r.sub) < 0) std::printf("  %s: '%s' names the unknown subcategory '%s / %s'\n", tables[t].name, r.match, r.category, r.sub);
            }
            for (const Row& r : rows) {
                if (r.table != t) continue;
                const int first = FirstMatch(tables[t].rules, tables[t].n, r.key.c_str());
                if (first >= 0) ++wins[(std::size_t)first];
                for (std::size_t i = 0; i < tables[t].n; ++i) if (VsRuleMatches(tables[t].rules[i].match, r.key.c_str())) ++matches[i];
            }
            for (std::size_t i = 0; i < tables[t].n; ++i) {
                const VsRule& r = tables[t].rules[i];
                if (matches[i] == 0) {
                    std::printf("  %s: rule '%s' matches no node (a typo, or the node was removed)\n", tables[t].name, r.match);
                    CHECK(false);
                } else if (wins[i] == 0) {
                    std::printf("  %s: rule '%s' never wins; an earlier row takes everything it matches\n", tables[t].name, r.match);
                    CHECK(false);
                }
            }
        }
        // The matcher itself.
        CHECK(VsRuleMatches("OnKey", "OnKey"));
        CHECK(!VsRuleMatches("OnKey", "OnKeyUp"));
        CHECK(VsRuleMatches("OnKey*", "OnKeyUp"));
        CHECK(!VsRuleMatches("OnKey*", "OnClick"));
        CHECK(VsRuleMatches("*Cast", "SphereCast"));
        CHECK(!VsRuleMatches("*Cast", "CastX"));
        CHECK(VsRuleMatches("*tween*", "do_tween_now"));
        CHECK(!VsRuleMatches("*tween*", "twee"));
        CHECK(!VsRuleMatches("", "x") && !VsRuleMatches("*", "x"));
    }

    // ======================================================================================================
    // 5. Events: every "On ..." node is in one of the five event topics
    // ======================================================================================================
    {
        const std::set<std::string> eventTopics = {"Game Events", "Input Events", "Collision & Trigger Events", "Inventory & Quest Events", "Network Events"};
        int events = 0;
        std::set<std::string> notEvents;
        for (const Row& r : rows) {
            if (!r.place.category) continue;
            // An event is a registry event (nothing flows in), or a hand-written type named On<Capital>... (On Object RPC
            // is raised by the network, not by the registry's event kind).
            const bool named = r.kind == "node" && r.id.size() > 2 && r.id[0] == 'O' && r.id[1] == 'n' && std::isupper((unsigned char)r.id[2]);
            const bool isEvent = (r.def && VsKindOf(*r.def) == VsKindFilter::Event) || named;
            const bool inFamily = eventTopics.count(r.place.category) > 0;
            if (isEvent) {
                ++events;
                CHECK(inFamily);
                if (!inFamily) std::printf("  event %s is in '%s', not in an event topic\n", r.id.c_str(), r.place.category);
            } else if (inFamily && r.browse()) {
                notEvents.insert(r.id);
            }
        }
        // An event topic holds events, plus the three readings of a collision and the script function On Key.
        const std::set<std::string> allowed = {"ContactPoint", "ContactNormal", "ImpactSpeed", "Fn.on_key"};
        CHECK(notEvents == allowed);
        if (notEvents != allowed) for (const std::string& s : notEvents) if (!allowed.count(s)) std::printf("  %s is in an event topic but is not an event\n", s.c_str());
        CHECK(events >= 60);
        std::printf("  %d event nodes, all in the event topics\n", events);
    }

    // ======================================================================================================
    // 6. People who knew the old place still find the node: the old topic becomes search words
    // ======================================================================================================
    {
        int moved = 0, kept = 0, wordsOk = 0;
        for (const Row& r : rows) {
            if (!r.place.category || !r.was.category) continue;
            const std::string words = VsMovedFromWords(r.was, r.place);
            const bool same = std::strcmp(r.was.category, r.place.category) == 0 && std::strcmp(r.was.subcategory, r.place.subcategory) == 0;
            if (same) {
                CHECK(words.empty());
                ++kept;
                continue;
            }
            ++moved;
            const std::string low = Lower(words);
            bool ok = !words.empty() && low.find(Lower(r.was.category)) != std::string::npos;
            if (r.was.subcategory[0]) ok = ok && low.find(Lower(r.was.subcategory)) != std::string::npos;
            CHECK(ok);
            if (ok) ++wordsOk;
            else std::printf("  %s %s: the old place '%s' is not in its search words\n", r.kind.c_str(), r.id.c_str(), PlaceName(r.was).c_str());
        }
        std::printf("  %d rows moved to a new place (found by their old topic name too), %d kept their place\n", moved, kept);
        CHECK(moved > 1500);                                   // the old menu really was reorganised
        CHECK(wordsOk == moved);
        // A few well-known ones, the way a person would type them.
        auto words = [&](const char* kind, const char* id) {
            for (const Row& r : rows) if (r.kind == kind && r.id == id) return Lower(VsMovedFromWords(r.was, r.place));
            return std::string("<missing>");
        };
        CHECK(words("builtin", "Fn.weather_set").find("scene") != std::string::npos);        // Weather used to be under Scene
        CHECK(words("node", "SetWeather").find("atmosphere") != std::string::npos);
        CHECK(words("node", "OnKey").find("input") != std::string::npos);                    // On Key used to be under Input / Keys
        CHECK(words("node", "OnKey").find("keys") != std::string::npos);
        CHECK(words("node", "OnCollision").find("collisions") != std::string::npos);
        CHECK(words("builtin", "Fn.bt_run").find("advanced") != std::string::npos);          // the old catch-all is a search word too
        CHECK(words("gameplay", "GlGetHealth").find("survival") != std::string::npos);
        CHECK(words("builtin", "Fn.net_host").find("online") != std::string::npos);
        CHECK(words("node", "Print").empty());                                               // Print kept its topic (Debug)
        // The palette really adds those words, for nodes and for steps (a source check: the palette needs ImGui to run).
        const std::string pal = ReadFile(std::string(OKAY_SOURCE_DIR) + "/editor/VisualScriptPalette.hpp");
        std::size_t n = 0, at = 0;
        while ((at = pal.find("VsMovedFromWords(was, place)", at)) != std::string::npos) { ++n; at += 10; }
        CHECK(n >= 2);
    }

    // ======================================================================================================
    // 7. Favourites and recents are remembered by node name, never by topic
    // ======================================================================================================
    {
        CHECK(VsPaletteNodeKey("OnKey") == "n:OnKey");
        CHECK(VsPaletteNodeKey("Fn.weather_set") == "n:Fn.weather_set");
        CHECK(VsPaletteStepKey(false, "set_var") == "a:set_var");
        CHECK(VsPaletteStepKey(true, "var_eq") == "c:var_eq");
        std::map<std::string, const Row*> byKey;
        for (const Row& r : rows) {
            const bool step = r.kind == "step" || r.kind == "check";
            const std::string key = step ? VsPaletteStepKey(r.kind == "check", r.id.c_str()) : VsPaletteNodeKey(r.id.c_str());
            CHECK(byKey.emplace(key, &r).second);                // every row has its own key, and the key has no topic in it
            CHECK(key == (step ? (r.kind == "check" ? "c:" : "a:") : "n:") + r.id);
        }
        // An okay_vsnodes.txt written before this change (keys of nodes that moved), read the way the palette reads it.
        std::istringstream old("lvl 2\nfav n:OnKey\nfav n:Fn.weather_set\nfav a:set_var\nrec c:var_eq\nrec n:GlGetHealth\nrec n:OnCollision\n");
        std::string kind, k;
        int resolved = 0, total = 0;
        while (old >> kind >> k) {
            if (kind == "lvl") continue;
            ++total;
            if (byKey.count(k)) ++resolved;
        }
        CHECK(total == 6 && resolved == 6);
        const std::string pal = ReadFile(std::string(OKAY_SOURCE_DIR) + "/editor/VisualScriptPalette.hpp");
        CHECK(pal.find("VsPaletteNodeKey(d.type)") != std::string::npos);
        CHECK(pal.find("VsPaletteStepKey(cond, si.op.c_str())") != std::string::npos);
        CHECK(pal.find("std::string(\"n:\")") == std::string::npos);   // no second way of making a key
    }

    // ======================================================================================================
    // 8. The old categories file really is the old menu
    // ======================================================================================================
    CHECK(VsLegacyCategoryCount() == 19);
    CHECK(std::string(kLegacyPaletteCategories[0].name) == "Events");
    CHECK(std::string(kLegacyPaletteCategories[18].name) == "Advanced");
    {
        // The old Advanced bucket was the catch-all: most of what the old code put there now sits in a real topic.
        int oldAdvanced = 0, stillThere = 0;
        for (const Row& r : rows) {
            if (!r.listed() || !r.was.category || std::strcmp(r.was.category, "Advanced") != 0 || r.was.subcategory[0]) continue;
            ++oldAdvanced;
            if (r.place.category && std::strcmp(r.place.category, "Advanced") == 0) ++stillThere;
        }
        CHECK(oldAdvanced > 400);
        CHECK(stillThere == 0);
    }

    // ======================================================================================================
    // 9. The docs say what the code says
    // ======================================================================================================
    {
        const std::string doc = ReadFile(std::string(OKAY_SOURCE_DIR) + "/docs/visual_scripting_naming.md");
        const std::string a = "<!-- topics:start -->\n", b = "<!-- topics:end -->";
        const std::size_t i = doc.find(a), j = doc.find(b);
        CHECK(i != std::string::npos && j != std::string::npos && j > i);
        if (i != std::string::npos && j != std::string::npos && j > i) {
            const std::string shown = doc.substr(i + a.size(), j - i - a.size());
            CHECK(shown == TopicTable());
            if (shown != TopicTable())
                std::printf("  docs/visual_scripting_naming.md lists other topics than the code. Run: test_vs_categories --topic-table\n"
                            "  and paste its output between the <!-- topics:start --> and <!-- topics:end --> markers.\n");
        }
        // The generated pages exist (scripts/vs_category_report.py writes them from test_vs_categories --dump).
        CHECK(!ReadFile(std::string(OKAY_SOURCE_DIR) + "/docs/visual_script_category_audit.md").empty());
        CHECK(!ReadFile(std::string(OKAY_SOURCE_DIR) + "/docs/visual_scripting_topics.md").empty());
    }

    TEST_MAIN_RESULT();
}
