#include "test_framework.hpp"
#include "EditorTabs.hpp"
#include "okay/Core/Paths.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
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

    {   // What is kept between runs goes to the per-user folder, never the working folder (owner report, 2.903.8:
        // "the editor doesnt save layout or tabs i opened or closed").
        okay::Paths::Env w;
        w.appData = "C:/Users/u/AppData/Roaming"; w.localAppData = "C:/Users/u/AppData/Local"; w.home = "C:/Users/u";
        const std::string winDir = EditorConfigDir(okay::Paths::ConfigDirFor(okay::Paths::Platform::Windows, w, "OkaySpace"), nullptr);
        CHECK(winDir == "C:/Users/u/AppData/Roaming/OkaySpace/config/editor");
        CHECK(EditorConfigFile(winDir, "imgui.ini") == "C:/Users/u/AppData/Roaming/OkaySpace/config/editor/imgui.ini");
        CHECK(EditorConfigFile(winDir, "okay_settings.txt") == "C:/Users/u/AppData/Roaming/OkaySpace/config/editor/okay_settings.txt");
        okay::Paths::Env l; l.home = "/home/u";
        CHECK(EditorConfigDir(okay::Paths::ConfigDirFor(okay::Paths::Platform::Linux, l, "OkaySpace"), nullptr) == "/home/u/.config/OkaySpace/editor");
        CHECK(EditorConfigDir("/x/config/", nullptr) == "/x/config/editor");
        CHECK(EditorConfigDir("/x/config", "/tmp/t") == "/tmp/t");          // OKAY_EDITOR_CONFIG_DIR wins
        CHECK(EditorConfigDir("/x/config", "") == "/x/config/editor");      // an empty override is no override
        CHECK(EditorConfigDir("", nullptr).empty());                        // nothing known: the working folder, as before
        CHECK(EditorConfigFile("", "imgui.ini") == "imgui.ini");
        CHECK(EditorConfigFile("D:\\cfg\\", "a.txt") == "D:\\cfg\\a.txt");
    }

    {   // UserFile: the visual script editor's files (okay_vs_tabs.txt, okay_vsnodes.txt, ...) and the plugin list go to the
        // per-user folder too (docs/engine/project_layout.md). A file an older editor left in the working folder (or beside
        // the program) is copied over once, and the old one is left alone.
        namespace fs = std::filesystem;
        const fs::path base = fs::temp_directory_path() / "okay_editor_tabs_userfile";
        std::error_code ec;
        fs::remove_all(base, ec);
        fs::create_directories(base / "old", ec);
        { std::ofstream(base / "old" / "okay_vs_tabs.txt") << "0\nA.okayvs\n"; }
        const std::string saved = UserFilesDir();
        UserFilesDir() = (base / "user").string();
        const std::string p = UserFile("okay_vs_tabs.txt", (base / "old").string());
        CHECK(fs::path(p) == base / "user" / "okay_vs_tabs.txt");
        CHECK(fs::is_regular_file(p));                                        // copied over (the folder is made)
        CHECK(fs::is_regular_file(base / "old" / "okay_vs_tabs.txt"));        // never deleted
        { std::ofstream(p) << "1\nB.okayvs\n"; }
        { std::ofstream(base / "old" / "okay_vs_tabs.txt") << "stale\n"; }
        UserFile("okay_vs_tabs.txt", (base / "old").string());               // once: a newer file is not overwritten
        std::ifstream f(p); std::string first; std::getline(f, first);
        CHECK(first == "1");
        CHECK(fs::path(UserFile("none.txt", (base / "old").string())) == base / "user" / "none.txt");   // nothing to copy
        UserFilesDir() = "";
        CHECK(UserFile("okay_vs_tabs.txt") == "okay_vs_tabs.txt");            // no per-user folder known: the working folder, as before
        UserFilesDir() = saved;
        fs::remove_all(base, ec);
    }

    {   // Which panels are open: one "panel_<key> 0|1" pair per panel, read back to the same panels.
        CHECK(PanelSettingKey("Scene Manager") == "panel_scene_manager");
        CHECK(PanelSettingKey("Items, Loot & Recipes") == "panel_items_loot_recipes");
        CHECK(PanelSettingKey("UV Editor") == "panel_uv_editor");
        CHECK(PanelSettingKey("  Console ") == "panel_console");
        const std::vector<std::string> labels = {"Hierarchy", "Inspector", "Console", "Profiler", "Scene Manager", "Items, Loot & Recipes"};
        for (std::size_t i = 0; i < labels.size(); ++i)   // two panels never share one line
            for (std::size_t j = i + 1; j < labels.size(); ++j) CHECK(PanelSettingKey(labels[i]) != PanelSettingKey(labels[j]));
        // Closed the Console, opened the Profiler and Scene Manager: written, then read back the way LoadSettings reads.
        const std::vector<bool> open = {true, true, false, true, true, false};
        std::vector<std::pair<std::string, bool>> panels;
        for (std::size_t i = 0; i < labels.size(); ++i) panels.push_back({labels[i], open[i]});
        const std::string text = "settingsver 3\nshowgrid 1\n" + WritePanelOpenLines(panels) + "panel_unknown_panel 1\nconsolecollapse 0\n";
        std::vector<bool> back(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i) back[i] = !open[i];   // start from the opposite of what was saved
        std::istringstream in(text);
        std::string k; int v = 0, seen = 0, unknown = 0, after = 0;
        while (in >> k >> v) {
            int idx = -1;
            if (ReadPanelOpenLine(k, labels, idx)) { back[(std::size_t)idx] = v != 0; ++seen; }
            else if (k.rfind("panel_", 0) == 0) ++unknown;
            else if (k == "consolecollapse") ++after;
        }
        CHECK(seen == (int)labels.size());
        CHECK(unknown == 1);   // a panel this editor does not have is skipped
        CHECK(after == 1);     // and the lines after it are still read
        CHECK(back == open);
        int idx = 7;
        CHECK(!ReadPanelOpenLine("showgrid", labels, idx) && idx == 7);   // other settings are not panel lines
    }

    TEST_MAIN_RESULT();
}
