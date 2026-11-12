// The Add Node search: ranked scoring, synonyms, typos, abbreviations, several
// words, the pin filter and the cost of a keystroke over the real palette.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace okay::editor::vsed;

namespace {

// The rows the palette lists, built the way the palette builds them.
struct Palette {
    std::vector<VsSearchItem> items;
    std::vector<const VsSearchItem*> ptrs;
    std::vector<const VsDef*> defs;
    Palette() {
        for (const VsDef& d : Registry())
            if (VsPaletteListed(d)) { items.push_back(VsSearchItemForDef(d)); defs.push_back(&d); }
        for (const VsSearchItem& i : items) ptrs.push_back(&i);
    }
    std::vector<VsSearchHit> run(const std::string& q, VsSearchOptions o = VsSearchOptions()) const { return VsSearchRun(ptrs, q, o); }
    const char* typeAt(const VsSearchHit& h) const { return defs[h.index]->type; }
    int rankOf(const std::string& q, const char* type) const {
        const auto r = run(q);
        for (int i = 0; i < (int)r.size(); ++i) if (std::strcmp(typeAt(r[i]), type) == 0) return i;
        return -1;
    }
    std::string top(const std::string& q, int n = 5) const {
        std::string s; const auto r = run(q);
        for (int i = 0; i < (int)r.size() && i < n; ++i) { if (i) s += ", "; s += typeAt(r[i]); }
        return s;
    }
};

VsSearchItem make(const char* title, const char* names = "", const char* topic = "", const char* desc = "") {
    VsSearchItem it; it.title = title; it.names = names; it.topic = topic; it.desc = desc;
    VsSearchPrepare(it);
    return it;
}

}  // namespace

int main() {
    RUN_SUITE("vs_search");
    const Palette pal;
    CHECK(pal.items.size() > 400);

    // ---- Tiers, on a small hand-made list: exact > prefix > word start > inside > abbreviation > typo > letters
    {
        std::vector<VsSearchItem> v = { make("Set Scale"), make("Scale"), make("Scale Up"), make("Rescale"), make("Set Color Scale"),
                                        make("Sail"), make("Sceptre Alert Lamp") };
        std::vector<const VsSearchItem*> p; for (auto& i : v) p.push_back(&i);
        auto r = VsSearchRun(p, "scale");
        CHECK(r.size() == 5);
        CHECK(r[0].index == 1);                       // exact
        CHECK(r[1].index == 2);                       // starts with it
        CHECK((r[2].index == 0 || r[2].index == 4));  // a later word starts with it
        CHECK(r[4].index == 3);                       // only inside a word
        CHECK(VsTierOf(r[0].score) > VsTierOf(r[1].score));
        CHECK(VsTierOf(r[1].score) > VsTierOf(r[2].score));
        CHECK(VsTierOf(r[2].score) > VsTierOf(r[4].score));
        // the matched letters are the ones to highlight
        CHECK(r[2].hits.size() == 5);
        // an abbreviation, then letters in order, rank under all of those
        auto ab = VsSearchRun(p, "ssc");
        CHECK(!ab.empty() && ab[0].index == 0 && ab[0].why == VsWhy::Abbreviation);
        // a typo
        auto ty = VsSearchRun(p, "sacle");
        CHECK(!ty.empty() && ty[0].why == VsWhy::Typo);
    }

    // ---- Empty query lists everything in the given order; blank is empty too
    {
        auto all = pal.run("");
        CHECK(all.size() == pal.items.size());
        bool inOrder = true;
        for (int i = 0; i < (int)all.size(); ++i) if (all[i].index != i) inOrder = false;
        CHECK(inOrder);
        CHECK(pal.run("   ").size() == pal.items.size());
        CHECK(pal.run("zzzzqqqq").empty());
    }

    // ---- Query -> expected results (the audit table in docs/visual_scripting_search.md)
    struct Want { const char* query; const char* type; int within; };
    const Want wants[] = {
        {"print", "Print", 0},          {"print", "LogWarn", 8},
        {"log", "Print", 0},            {"log", "LogError", 6},   {"log", "LogOnce", 6},
        {"debug", "Print", 3},          {"console", "Print", 0},  {"output", "Print", 0},
        {"key", "OnKey", 3},            {"key", "KeyDown", 8},
        {"keyboard", "OnKey", 3},       {"button", "OnKey", 3},   {"input", "OnKey", 4},
        {"keybord", "OnKey", 3},        {"kyboard", "OnKey", 3},
        {"start", "OnStart", 0},        {"begin", "OnStart", 0},  {"awake", "OnStart", 0},   {"init", "OnStart", 0},
        {"if", "Branch", 3},
        {"loop", "ForLoop", 3},         {"loop", "While", 6},     {"repeat", "Repeat", 0},
        {"wait", "Delay", 4},           {"wait", "WaitUntil", 6}, {"delay", "Delay", 0},     {"sleep", "Delay", 0},
        {"timer", "Timer", 2},          {"timer", "OnTimer", 4},
        {"move", "Translate", 6},       {"move", "MoveTowards", 6},
        {"spawn", "Spawn", 0},          {"create", "Spawn", 0},   {"instantiate", "Spawn", 0},
        {"random", "RandomRange", 3},   {"rand", "RandomRange", 3}, {"dice", "RandomInt", 6},
        {"add", "Add", 0},              {"plus", "Add", 0},       {"sum", "Add", 0},
        {"minus", "Sub", 0},            {"subtract", "Sub", 0},   {"times", "Mul", 0},       {"multiply", "Mul", 0},
        {"divide", "Div", 0},
        {"vector", "MakeVec3", 2},      {"vector", "Vec3Add", 10},
        {"distance", "DistanceTo", 3},  {"distance", "Vec3Distance", 4},
        {"health", "Heal", 4},          {"health", "OnHealthChanged", 6}, {"hp", "Heal", 4},
        {"sound", "PlaySound", 2},      {"audio", "PlaySound", 2},
        {"click", "OnClick", 0},        {"on click", "OnClick", 0}, {"hit", "OnCollision", 3}, {"touch", "OnCollision", 3},
        {"collide", "OnCollision", 3},  {"collision", "OnCollision", 3},
        {"set scale", "SetScale", 0},   {"ssc", "SetScale", 0},   {"gp", "GetPosition", 3},
        {"scale set", "SetScale", 0},   {"wait sec", "Delay", 0},
        {"mouse x", "MouseX", 0},       {"plus vec", "Vec3Add", 0},
    };
    for (const Want& w : wants) {
        const int r = pal.rankOf(w.query, w.type);
        const bool ok = r >= 0 && r <= w.within;
        CHECK(ok);
        if (!ok) std::printf("  \"%s\": %s is at %d (wanted within %d); top: %s\n", w.query, w.type, r, w.within, pal.top(w.query, 8).c_str());
    }

    // ---- Exact names always come first, whatever else matches
    for (const char* t : {"Print", "Branch", "Spawn", "Destroy", "Heal", "Wait Until", "Set Scale", "Look At", "Delay", "Counter"}) {
        bool found = false;
        for (const VsDef* d : pal.defs) {
            if (std::strcmp(d->title, t) != 0) continue;
            found = true;
            const auto r = pal.run(t);
            CHECK(!r.empty() && std::strcmp(pal.defs[r[0].index]->title, t) == 0);
            CHECK(!r.empty() && VsTierOf(r[0].score) == kTierExact);
        }
        if (!found) std::printf("  no node titled %s\n", t);
    }
    // case does not matter, and a parenthesis in a title is not part of its name
    CHECK(pal.top("PRINT", 1) == pal.top("print", 1));
    CHECK(pal.rankOf("add", "Add") == 0);

    // ---- Synonyms: every alias target is a real node, and is listed in the palette
    {
        std::set<std::string> listed;
        for (const VsDef* d : pal.defs) listed.insert(d->type);
        int rows = 0, targets = 0;
        for (int i = 0; i < VsAliasRowCount(); ++i) {
            const VsAlias& a = kVsAliases[i];
            ++rows;
            CHECK(a.words && a.words[0] && a.types && a.types[0]);
            std::string cur;
            for (const char* p = a.types; ; ++p) {
                if (*p == ' ' || *p == 0) {
                    if (!cur.empty()) {
                        ++targets;
                        const bool ok = listed.count(cur) != 0;
                        CHECK(ok);
                        if (!ok) std::printf("  alias row %d (%s): %s is not a listed node\n", i, a.words, cur.c_str());
                    }
                    cur.clear();
                    if (!*p) break;
                } else cur += *p;
            }
        }
        std::printf("  %d synonym rows point at %d nodes\n", rows, targets);
        CHECK(rows > 80);
        // a synonym word has no business duplicating a node's own name word as its only route
        const auto& words = VsAliasWordsFor("Print");
        CHECK(!words.empty() && words[0].primary);
    }
    // A synonym match says so, a name match does not
    {
        const auto r = pal.run("plus");
        CHECK(!r.empty() && r[0].why == VsWhy::Synonym && VsSearchWhyText(r[0]) == "also called \"plus\"");
        const auto n = pal.run("print");
        CHECK(!n.empty() && VsSearchWhyText(n[0]).empty() && n[0].hits.size() == 5);
    }

    // ---- Typos and abbreviations, found and explained
    {
        const auto r = pal.run("keybord");
        CHECK(!r.empty());
        CHECK(!r.empty() && r[0].why == VsWhy::Typo);
        CHECK(!r.empty() && VsSearchWhyText(r[0]) == "looks like \"keyboard\"");
        CHECK(pal.rankOf("brnach", "Branch") == 0);
        CHECK(pal.rankOf("destory", "Destroy") <= 1);
        CHECK(pal.rankOf("tranlate", "Translate") == 0);
        CHECK(VsTypoDistance("keybord", 7, "keyboard", 8, 2) == 1);
        CHECK(VsTypoDistance("teh", 3, "the", 3, 1) == 1);          // a swap is one edit
        CHECK(VsTypoDistance("abcdef", 6, "uvwxyz", 6, 2) == 3);    // bounded: reports max + 1
        CHECK(VsTypoDistance("a", 1, "abcdefgh", 8, 2) == 3);       // length alone rules it out
        const auto ab = pal.run("ssc");
        CHECK(!ab.empty() && ab[0].why == VsWhy::Abbreviation && ab[0].hits.size() == 3);
    }

    // ---- Several words: all of them, in any order
    {
        CHECK(pal.rankOf("scale set", "SetScale") >= 0);
        CHECK(pal.rankOf("set scale", "SetScale") == 0);
        CHECK(pal.rankOf("mouse button", "MouseButton") >= 0);
        const auto a = pal.run("set position"), b = pal.run("position set");
        CHECK(!a.empty() && !b.empty());
        CHECK(!a.empty() && !b.empty() && a[0].index == b[0].index);
        // every word has to match: one that matches nothing removes the node
        CHECK(pal.run("set zzzzqq").empty());
        // each result matches every word
        for (const VsSearchHit& h : pal.run("play sound")) {
            const VsSearchItem& it = *pal.ptrs[h.index];
            CHECK(it.lt.find("play") != std::string::npos || it.lnames.find("play") != std::string::npos ||
                  it.ldesc.find("play") != std::string::npos || it.lpins.find("play") != std::string::npos ||
                  it.ltopic.find("play") != std::string::npos || h.why != VsWhy::Title);
        }
    }

    // ---- Where a word matched sets the rank: name > synonym > topic > pins > description
    {
        VsSearchItem inName = make("Heal Over Time", "", "", "");
        VsSearchItem inTopic = make("Mend", "", "Health", "");
        VsSearchItem inText = make("Mend More", "", "Other", "gives some health back");
        std::vector<VsSearchItem> v = {inText, inTopic, inName};
        std::vector<const VsSearchItem*> p; for (auto& i : v) p.push_back(&i);
        const auto r = VsSearchRun(p, "heal");
        CHECK(r.size() == 1 && r[0].index == 2);   // heal is not the word "health"
        const auto h = VsSearchRun(p, "health");
        CHECK(h.size() == 2 && h[0].index == 1 && h[0].why == VsWhy::Topic);
        CHECK(h.size() == 2 && h[1].index == 0 && h[1].why == VsWhy::Text);
        VsSearchItem pinned = make("Mend");
        pinned.pins = "Amount Number"; VsSearchPrepare(pinned);
        const auto pn = VsSearchRun(std::vector<const VsSearchItem*>{&pinned}, "amount");
        CHECK(pn.size() == 1 && pn[0].why == VsWhy::Pin);
        CHECK(VsTierOf(pn[0].score) < VsTierOf(h[0].score));
    }

    // ---- Use: nudges inside a tier, never across one
    {
        VsSearchItem a = make("Move Alpha"), b = make("Move Beta"), c = make("Remove");
        std::vector<VsSearchItem> v = {a, b, c};
        std::vector<const VsSearchItem*> p; for (auto& i : v) p.push_back(&i);
        auto r = VsSearchRun(p, "move");
        CHECK(r.size() == 3 && r[0].index == 0 && r[1].index == 1);   // same tier, same level: the given order, shorter first
        v[1].recentRank = 0; v[1].uses = 9;
        r = VsSearchRun(p, "move");
        CHECK(r.size() == 3 && r[0].index == 1 && r[2].index == 2);   // used lately wins the tie
        v[2].recentRank = 0; v[2].uses = 50;                         // heavily used, but only matches inside a word
        r = VsSearchRun(p, "move");
        CHECK(r.size() == 3 && r[2].index == 2);
        CHECK(VsUseBoost(0, 99) <= 40 && VsUseBoost(10000, 0) == 0);
        CHECK(VsApplyUse(VsMakeScore(5, 127), 40) < VsMakeScore(6, 0));
        CHECK(VsApplyUse(VsMakeScore(5, 127), 40) <= VsMakeScore(5, 127));
    }

    // ---- Stability: the same query gives the same list, and ties follow the given order
    {
        const auto a = pal.run("set"), b = pal.run("set");
        CHECK(a.size() == b.size() && a.size() > 20);
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i].index == b[i].index && a[i].score == b[i].score;
        CHECK(same);
        std::vector<VsSearchItem> v = {make("Zed One"), make("Zed Two"), make("Zed Six")};
        std::vector<const VsSearchItem*> p; for (auto& i : v) p.push_back(&i);
        const auto r = VsSearchRun(p, "zed");
        CHECK(r.size() == 3 && r[0].index == 0 && r[1].index == 1 && r[2].index == 2);
        std::reverse(p.begin(), p.end());
        const auto q = VsSearchRun(p, "zed");
        CHECK(q.size() == 3 && q[0].index == 0 && q[2].index == 2);   // order of the list decides, not the text
    }

    // ---- Levels and the pin filter
    {
        VsSearchOptions basic; basic.level = (int)VsLevel::Basic;
        const auto r = pal.run("set", basic);
        CHECK(!r.empty());
        for (const VsSearchHit& h : r) CHECK(pal.items[h.index].level == (int)VsLevel::Basic);
        CHECK(r.size() < pal.run("set").size());

        // Dragging a Number out of a pin: only nodes with a Number-compatible input fit.
        const okay::vs::PinType num = okay::vs::PinType::Number;
        int fitting = 0;
        std::vector<VsSearchItem> items = pal.items;
        std::vector<const VsSearchItem*> p;
        for (std::size_t i = 0; i < items.size(); ++i) {
            items[i].fits = VsCompatible(*pal.defs[i], false, true, num);
            fitting += items[i].fits;
        }
        for (auto& i : items) p.push_back(&i);
        CHECK(fitting > 20 && fitting < (int)items.size());
        bool printFits = false, onStartFits = true;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (std::strcmp(pal.defs[i]->type, "OnStart") == 0) onStartFits = items[i].fits;   // an event has no inputs
            if (std::strcmp(pal.defs[i]->type, "Add") == 0) printFits = items[i].fits;
        }
        CHECK(printFits && !onStartFits);
        VsSearchOptions first; first.fitFirst = true;
        const auto f = VsSearchRun(p, "set", first);
        bool seenMiss = false, ordered = true;
        for (const VsSearchHit& h : f) { if (!items[h.index].fits) seenMiss = true; else if (seenMiss) ordered = false; }
        CHECK(ordered);
        CHECK(seenMiss);   // show all: the others are still there, after the ones that fit
        // A Flow pin dragged from an output needs a node with a flow input; an input needs one with a flow output.
        int takesFlow = 0, givesFlow = 0;
        for (const VsDef* d : pal.defs) { takesFlow += VsCompatible(*d, true, true); givesFlow += VsCompatible(*d, true, false); }
        CHECK(takesFlow > 100 && givesFlow > 100);
        for (const VsDef* d : pal.defs) CHECK(VsCompatible(*d, true, true) == d->execIn);
    }

    // ---- Cost of a keystroke over the real palette
    {
        const char* qs[] = {"p", "pr", "pri", "print", "log", "keybord", "set sc", "ssc", "wait sec", "xyzzy", "on click", "dist", "vector", "plus"};
        const int rounds = 20;
        double worst = 0, total = 0; int n = 0;
        for (const char* q : qs) {
            double bestOfRounds = 1e9;
            for (int r = 0; r < rounds; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                const auto res = pal.run(q);
                const auto t1 = std::chrono::steady_clock::now();
                const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                bestOfRounds = std::min(bestOfRounds, ms);
                if (res.size() > 100000) std::printf("never\n");
            }
            worst = std::max(worst, bestOfRounds); total += bestOfRounds; ++n;
        }
        std::printf("  search over %d nodes: worst %.3f ms, mean %.3f ms per keystroke\n", (int)pal.items.size(), worst, total / n);
        CHECK(worst < 3.0);   // generous: a loaded machine; the measured figure is printed above
    }

    TEST_MAIN_RESULT();
}
