#include "test_framework.hpp"
#include "EditorTabs.hpp"
#include <string>
#include <vector>

// The editor's panel tabs + layouts: saved-layout files, preset names, the
// Ctrl+Tab most-recently-used order and the overflow list's filter.
using namespace okay::edtabs;

int main() {
    RUN_SUITE("editor_tabs");

    {   // Presets: names round-trip, unknown names are -1.
        CHECK(kPresetCount == 6);
        for (int i = 0; i < kPresetCount; ++i) {
            CHECK(PresetFromName(PresetName(i)) == i);
            CHECK(PresetHint(i)[0] != '\0');
        }
        CHECK(PresetFromName("2 BY 3") == (int)Preset::TwoByThree);
        CHECK(PresetFromName("Mine") == -1);
        CHECK(std::string(PresetName(99)).empty());
    }

    {   // Layout names: trimmed, control characters out, capped at 40 chars.
        CHECK(CleanLayoutName("  My   Layout \n") == "My Layout");
        CHECK(CleanLayoutName("\t\r\n").empty());
        CHECK(CleanLayoutName(std::string(60, 'x')).size() == 40);
        // A cut through a 2-byte UTF-8 character drops the half character.
        std::string accents;
        for (int i = 0; i < 30; ++i) accents += "\xC3\xA9";   // "é" x30 = 60 bytes
        std::string c = CleanLayoutName(accents);
        CHECK(c.size() == 40);
        CHECK(((unsigned char)c.back() & 0xC0) == 0x80);   // ends on a whole character
        CHECK(CleanLayoutName("a" + accents).size() == 39);   // the 40th byte began an "é": dropped
    }

    {   // Save -> load keeps names, open panels and the exact ini bytes.
        std::vector<SavedLayout> v;
        SavedLayout a;
        a.name = "Big Scene";
        a.ini = "[Window][Hierarchy]\nPos=0,0\nSize=200,400\n\n[Docking][Data]\nDockSpace ID=0x1 Window=0x2 Pos=0,0\n";
        a.open = {"Hierarchy", "Inspector", "Console"};
        SavedLayout b;
        b.name = "Code";
        b.ini = "no trailing newline\nend\nlayout fake";   // looks like file syntax: must stay data
        b.open = {"Script Editor"};
        CHECK(!UpsertLayout(v, a));
        CHECK(!UpsertLayout(v, b));
        const std::string text = SerializeLayouts(v);
        std::vector<SavedLayout> r = ParseLayouts(text);
        CHECK(r.size() == 2);
        if (r.size() == 2) {
            CHECK(r[0].name == "Big Scene" && r[0].ini == a.ini && r[0].open == a.open);
            CHECK(r[1].name == "Code" && r[1].ini == b.ini && r[1].open == b.open);
        }
        // Windows line endings in the header lines are fine.
        std::string crlf = text;
        for (std::size_t p = crlf.find("okaylayouts 1\n"); p != std::string::npos; p = std::string::npos)
            crlf.replace(p, 14, "okaylayouts 1\r\n");
        CHECK(ParseLayouts(crlf).size() == 2);
        // A file cut short keeps the layouts before the damage.
        CHECK(ParseLayouts(text.substr(0, text.size() - 20)).size() == 1);
        // Not a layouts file / a future version: nothing.
        CHECK(ParseLayouts("hello\n").empty());
        CHECK(ParseLayouts("okaylayouts 9\nlayout X\nopen \nini 0\n\nend\n").empty());
        CHECK(ParseLayouts("").empty());
    }

    {   // Upsert replaces by name (any case); Remove drops it.
        std::vector<SavedLayout> v;
        UpsertLayout(v, {"Mine", "a", {}});
        CHECK(UpsertLayout(v, {"MINE", "b", {}}));
        CHECK(v.size() == 1 && v[0].ini == "b" && v[0].name == "MINE");
        CHECK(FindLayout(v, "mine") == 0);
        CHECK(!RemoveLayout(v, "other"));
        CHECK(RemoveLayout(v, "Mine"));
        CHECK(v.empty());
        // Panel labels that would break the format are left out.
        std::vector<SavedLayout> w = {{"W", "", {"Ok", "Bad|Name", "", "Also ok"}}};
        std::vector<SavedLayout> r = ParseLayouts(SerializeLayouts(w));
        CHECK(r.size() == 1 && r[0].open == std::vector<std::string>({"Ok", "Also ok"}));
    }

    {   // MRU: touched panels move to the front, duplicates collapse, capped.
        std::vector<std::string> mru;
        TouchMru(mru, "Scene"); TouchMru(mru, "Inspector"); TouchMru(mru, "Console");
        TouchMru(mru, "Scene");
        CHECK(mru == std::vector<std::string>({"Scene", "Console", "Inspector"}));
        TouchMru(mru, "");
        CHECK(mru.size() == 3);
        std::vector<std::string> big;
        for (int i = 0; i < 10; ++i) TouchMru(big, "P" + std::to_string(i), 4);
        CHECK(big.size() == 4 && big.front() == "P9");

        // Order of the open panels: recent ones first, the rest as given; closed
        // panels in the MRU are skipped.
        std::vector<std::string> open = {"Hierarchy", "Inspector", "Scene", "Project"};
        std::vector<std::string> ord = MruOrder(mru, open);
        CHECK(ord == std::vector<std::string>({"Scene", "Inspector", "Hierarchy", "Project"}));
        CHECK(MruOrder({}, open) == open);
        CHECK(MruOrder(mru, {}).empty());
    }

    {   // Cycling wraps both ways.
        CHECK(CycleIndex(0, 3, 1) == 1);
        CHECK(CycleIndex(2, 3, 1) == 0);
        CHECK(CycleIndex(0, 3, -1) == 2);
        CHECK(CycleIndex(1, 3, -4) == 0);
        CHECK(CycleIndex(0, 0, 1) == -1);
        CHECK(CycleIndex(0, 1, 1) == 0);
    }

    {   // Filter: every word, any case.
        CHECK(MatchesFilter("Script Editor", ""));
        CHECK(MatchesFilter("Script Editor", "script"));
        CHECK(MatchesFilter("Script Editor", "ED scr"));
        CHECK(!MatchesFilter("Script Editor", "visual"));
        CHECK(MatchesFilter("Asset Manager", "  man  "));
    }

    {   // Overflow: needs 2+ tabs, and stays on until clearly fitting again.
        CHECK(!TabsOverflow(500, 300, 1, false));
        CHECK(TabsOverflow(310, 300, 3, false));
        CHECK(!TabsOverflow(300, 300, 3, false));
        CHECK(TabsOverflow(297, 300, 3, true));    // within the slack: keep showing
        CHECK(!TabsOverflow(290, 300, 3, true));
        CHECK(!TabsOverflow(100, 0, 3, false));
    }

    TEST_MAIN_RESULT();
}
