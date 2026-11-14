// Naming a visual-script graph file (editor/VsGraphNaming.hpp, no ImGui): the suggested name for an untitled graph,
// de-duplication, every invalid name, reserved Windows names, the forced .okayvs extension, replacing an existing
// file (needs a confirm), the target folder, following moves, and renaming with the references that point at the file.
// Everything runs in temp folders.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "../editor/VsGraphNaming.hpp"
#include "../editor/ProjectBrowser.hpp"

#include <chrono>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay::editor::vsname;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_vsname_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path / "Assets", ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string assets() const { return (path / "Assets").generic_string(); }
    std::string at(const std::string& rel) const { return (path / "Assets" / rel).generic_string(); }
};
void Write(const std::string& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(fs::path(p).parent_path(), ec);
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
}
std::string Read(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool Has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
bool Exists(const std::string& p) { std::error_code ec; return fs::exists(p, ec); }
}  // namespace

int main() {
    RUN_SUITE("target folder");
    {
        CHECK(ScriptsDir("") == "Assets");
        CHECK(ScriptsDir("/proj/Game") == "/proj/Game/Assets");
    }

    RUN_SUITE("suggested names");
    {
        TempDir td;
        CHECK(SuggestName(td.assets(), "") == "Script");
        CHECK(SuggestName(td.assets(), "Player") == "PlayerGraph");
        CHECK(SuggestName(td.assets(), "Boss Door #2") == "Boss_Door__2Graph");   // only letters, digits, _ stay
        CHECK(SuggestName(td.assets(), "???") == "ObjectGraph");                  // nothing usable left
        CHECK(SuggestName(td.assets(), "CON") == "CONGraph");                     // "CONGraph" is not reserved
        CHECK(ValidateGraphName(SuggestName(td.assets(), "CON")).empty());
        CHECK(ValidateGraphName(SuggestName(td.assets(), std::string(400, 'x'))).empty());   // long object names are cut
        // De-duplication against files in the folder, case-insensitively, and against other pending names.
        Write(td.at("Script.okayvs"), "x");
        CHECK(SuggestName(td.assets(), "") == "Script2");
        Write(td.at("script2.OKAYVS"), "x");
        CHECK(SuggestName(td.assets(), "") == "Script3");
        Write(td.at("PlayerGraph.okayvs"), "x");
        CHECK(SuggestName(td.assets(), "Player") == "PlayerGraph2");
        CHECK(SuggestName(td.assets(), "Player", {"PlayerGraph2"}) == "PlayerGraph3");
        CHECK(SuggestName(td.assets() + "/missing_folder", "Player") == "PlayerGraph");   // no folder yet: nothing taken
        // A folder with the same name also blocks it.
        fs::create_directories(td.at("DoorGraph.okayvs"));
        CHECK(SuggestName(td.assets(), "Door") == "DoorGraph2");
    }

    RUN_SUITE("validating typed names");
    {
        CHECK(ValidateGraphName("Door").empty());
        CHECK(ValidateGraphName("Door.okayvs").empty());
        CHECK(ValidateGraphName("Door.OKAYVS").empty());
        CHECK(ValidateGraphName("Boss fight 2").empty());
        CHECK(ValidateGraphName("v1.2").empty());                                  // dots inside are fine
        CHECK(ValidateGraphName("\xC3\x9Cn\xC3\xAF").empty());                     // unicode is fine
        CHECK(ValidateGraphName(std::string(kMaxStem, 'a')).empty());
        CHECK(!ValidateGraphName(std::string(kMaxStem + 1, 'a')).empty());         // too long
        CHECK(!ValidateGraphName("").empty());
        CHECK(!ValidateGraphName("   ").empty());
        CHECK(!ValidateGraphName(".okayvs").empty());                              // nothing before the extension
        CHECK(!ValidateGraphName("a/b").empty());
        CHECK(!ValidateGraphName("a\\b").empty());
        CHECK(!ValidateGraphName("../x").empty());
        CHECK(!ValidateGraphName("C:x").empty());
        for (const char* bad : {"a*", "a?", "a\"", "a<", "a>", "a|", "a:"}) CHECK(!ValidateGraphName(bad).empty());
        CHECK(!ValidateGraphName(std::string("a\tb")).empty());
        CHECK(!ValidateGraphName(std::string("a\nb")).empty());
        CHECK(!ValidateGraphName(std::string("a") + char(1)).empty());
        CHECK(!ValidateGraphName(".hidden").empty());
        CHECK(!ValidateGraphName("..").empty());
        CHECK(!ValidateGraphName("name.").empty());                                // trailing dot
        CHECK(!ValidateGraphName("name. .okayvs").empty());                        // trailing space before the extension
        CHECK(!ValidateGraphName("name ").empty());                                // trailing space
        CHECK(!ValidateGraphName(" name").empty());                                // leading space
        for (const char* r : {"CON", "con", "PRN", "AUX", "NUL", "COM1", "com9", "LPT1", "lpt9", "nul.txt", "Con.okayvs", "COM3.x.y"})
            CHECK(!ValidateGraphName(r).empty());
        CHECK(ValidateGraphName("COM10").empty());                                 // only COM1..COM9
        CHECK(ValidateGraphName("CONSOLE").empty());
        CHECK(ValidateGraphName("COM").empty());
        // TrimSpaces is what the editor does before it validates.
        CHECK(TrimSpaces("  Door \t") == "Door");
        CHECK(MakeFileName("  Door ") == "Door.okayvs");
    }

    RUN_SUITE("the file extension is always .okayvs");
    {
        CHECK(MakeFileName("Door") == "Door.okayvs");
        CHECK(MakeFileName("Door.okayvs") == "Door.okayvs");                       // not doubled
        CHECK(MakeFileName("Door.OKAYVS") == "Door.okayvs");                       // the extension is always written in lower case
        CHECK(MakeFileName("Door.txt") == "Door.txt.okayvs");                      // another extension is not replaced, it is just a dot in the name
        CHECK(MakeFileName("v1.2") == "v1.2.okayvs");
        CHECK(StripExt("a.okayvs") == "a" && StripExt("a") == "a");
    }

    RUN_SUITE("agrees with the Project panel's name rules");
    {
        // Whatever ValidateGraphName accepts, the Project panel (okaypb::ValidateName) accepts too, once the extension is on.
        for (const char* n : {"Door", "Door.okayvs", "a b", "v1.2", "CON", "nul.txt", "x.", ".x", "a/b", "a:b", "", "  ", "Con.okayvs",
                              "COM1", "COM10", "a?b", "name ", "caf\xC3\xA9"}) {
            if (ValidateGraphName(n).empty()) CHECK(okaypb::ValidateName(MakeFileName(n)).empty());
            if (!okaypb::ValidateName(std::string(n) + kExt).empty() && std::string(n) != "") CHECK(!ValidateGraphName(n).empty() || std::string(n) == "   ");
        }
    }

    RUN_SUITE("choosing a target, replace needs a confirm");
    {
        TempDir td;
        Write(td.at("Door.okayvs"), "old");
        auto fresh = CheckTarget(td.assets(), "Gate");
        CHECK(fresh.ok && !fresh.needsConfirm && fresh.path == td.at("Gate.okayvs"));
        auto typedExt = CheckTarget(td.assets(), "Gate.okayvs");
        CHECK(typedExt.ok && typedExt.path == td.at("Gate.okayvs"));
        auto bad = CheckTarget(td.assets(), "a/b");
        CHECK(!bad.ok && !bad.needsConfirm && !bad.error.empty() && bad.path.empty());
        auto reserved = CheckTarget(td.assets(), "NUL");
        CHECK(!reserved.ok && !reserved.error.empty());
        auto clash = CheckTarget(td.assets(), "Door");
        CHECK(!clash.ok && clash.needsConfirm && !clash.message.empty() && clash.path == td.at("Door.okayvs"));
        auto clashCase = CheckTarget(td.assets(), "door.OKAYVS");                  // Windows treats case as the same file
        CHECK(!clashCase.ok && clashCase.needsConfirm);
        auto confirmed = CheckTarget(td.assets(), "Door", true);
        CHECK(confirmed.ok && !confirmed.needsConfirm);
        CHECK(Read(td.at("Door.okayvs")) == "old");                                // checking never writes
        auto self = CheckTarget(td.assets(), "Door", false, td.at("Door.okayvs"));  // saving onto its own file is not a replace
        CHECK(self.ok && !self.needsConfirm);
        fs::create_directories(td.at("Folder.okayvs"));
        auto dir = CheckTarget(td.assets(), "Folder", true);                       // a folder is never replaced
        CHECK(!dir.ok && !dir.error.empty());
        auto sp = CheckTarget(td.assets(), "  Gate  ");                            // spaces at the ends are trimmed
        CHECK(sp.ok && sp.path == td.at("Gate.okayvs"));
    }

    RUN_SUITE("replace gate (press twice to replace)");
    {
        TempDir td;
        Write(td.at("Door.okayvs"), "old");
        ReplaceGate gate;
        auto a = gate.Check(td.assets(), "Door");
        CHECK(!a.ok && a.needsConfirm);
        auto b = gate.Check(td.assets(), "Door");                                 // same name again: confirmed
        CHECK(b.ok && !b.needsConfirm && b.path == td.at("Door.okayvs"));
        auto c = gate.Check(td.assets(), "Door");                                 // the confirm is used up
        CHECK(!c.ok && c.needsConfirm);
        auto d = gate.Check(td.assets(), "Gate");                                 // another name resets it
        CHECK(d.ok);
        gate.Check(td.assets(), "Door");
        gate.Check(td.assets(), "door");                                          // same file in another case still counts
        auto e = gate.Check(td.assets(), "Door");
        CHECK(!e.ok && e.needsConfirm);                                           // (armed again after the one above passed)
        auto f = gate.Check(td.assets(), "a/b");                                  // an invalid name never arms or passes
        CHECK(!f.ok && !f.needsConfirm);
    }

    RUN_SUITE("following moves");
    {
        const std::vector<std::pair<std::string, std::string>> mv = {{"/p/Assets/A.okayvs", "/p/Assets/B.okayvs"},
                                                                     {"/p/Assets/Old", "/p/Assets/New"}};
        CHECK(RemapPath("/p/Assets/A.okayvs", mv) == "/p/Assets/B.okayvs");
        CHECK(RemapPath("/p/assets/a.OKAYVS", mv) == "/p/Assets/B.okayvs");
        CHECK(RemapPath("\\p\\Assets\\A.okayvs", mv) == "/p/Assets/B.okayvs");
        CHECK(RemapPath("/p/Assets/Old/x.okayvs", mv) == "/p/Assets/New/x.okayvs");   // inside a moved folder
        CHECK(RemapPath("/p/Assets/Older/x.okayvs", mv) == "/p/Assets/Older/x.okayvs");
        CHECK(RemapPath("/p/Assets/Other.okayvs", mv) == "/p/Assets/Other.okayvs");
        CHECK(RemapPath("", mv).empty());
    }

    RUN_SUITE("rename with references");
    {
        TempDir td;
        const std::string g = td.at("Scripts/door.okayvs");
        Write(g, "node 1 OnStart\n");
        Write(td.at("Scripts/gate.okayvs"), "node 1 OnStart\n");
        const std::string scene = td.at("Scenes/main.okayscene");
        Write(scene,
              "scene \"Main\"\n"
              "gameobject \"Door\"\n"
              "  visualscriptpath \"" + g + "\"\n"
              "gameobject \"Rel\"\n"
              "  visualscriptpath \"Assets/Scripts/door.okayvs\"\n"
              "gameobject \"Other\"\n"
              "  visualscriptpath \"" + td.at("Scripts/gate.okayvs") + "\"\n");
        okaypb::Browser b(td.assets());
        CHECK(b.Sync());

        // Refused names change nothing.
        for (const char* bad : {"", "a/b", "NUL", "bad.", "gate", "gate.okayvs"}) {
            auto r = RenameGraph(b, g, bad);
            CHECK(!r.ok && !r.error.empty());
        }
        CHECK(Exists(g) && Has(Read(scene), "door.okayvs"));

        auto r = RenameGraph(b, g, "entrance");                                     // no extension typed: .okayvs is added
        CHECK(r.ok && r.paths.size() == 1 && r.paths[0] == td.at("Scripts/entrance.okayvs"));
        CHECK(!Exists(g) && Exists(td.at("Scripts/entrance.okayvs")));
        CHECK(Read(td.at("Scripts/entrance.okayvs")) == "node 1 OnStart\n");
        CHECK(r.moved.size() == 1 && r.moved[0].first == g && r.moved[0].second == td.at("Scripts/entrance.okayvs"));
        CHECK(RemapPath(g, r.moved) == td.at("Scripts/entrance.okayvs"));           // a graph tab would follow
        CHECK(r.referencesRewritten == 2 && r.filesRewritten == 1);
        const std::string sc = Read(scene);
        CHECK(Has(sc, "visualscriptpath \"" + td.at("Scripts/entrance.okayvs") + "\""));   // absolute stays absolute
        CHECK(Has(sc, "visualscriptpath \"Assets/Scripts/entrance.okayvs\""));             // project-relative stays so
        CHECK(!Has(sc, "door.okayvs"));
        CHECK(Has(sc, "gate.okayvs"));                                                    // other graphs are untouched
        CHECK(Has(sc, "gameobject \"Door\""));                                            // an object NAME is never touched

        // A rename to the same name does nothing and succeeds (and adds nothing to the Project panel's Undo).
        auto same = RenameGraph(b, td.at("Scripts/entrance.okayvs"), "entrance");
        CHECK(same.ok && Exists(td.at("Scripts/entrance.okayvs")) && same.moved.empty());

        // Undo from the Project panel puts it all back.
        CHECK(b.Undo().ok);
        CHECK(Exists(g) && Has(Read(scene), "Assets/Scripts/door.okayvs"));
    }

    TEST_MAIN_RESULT();
}
