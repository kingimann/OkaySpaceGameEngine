// New-project rules (shared/ProjectCreate.hpp): the name is checked, the location is checked, the
// folder is chosen (Name, Name-2, ...), the skeleton is made, and a failure takes back exactly what
// it made. Files are replaced whole or not at all. Temp folders only; nothing sleeps or reads the clock.
#include "test_framework.hpp"
#include "../shared/ProjectCreate.hpp"

#include <chrono>
#include <fstream>
#include <regex>
#include <set>

namespace fs = std::filesystem;
using namespace okayproj;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_projcreate_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::permissions(path, fs::perms::all, ec);
        fs::remove_all(path, ec);
    }
};
void Write(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary | std::ios::trunc) << s; }
std::string Read(const fs::path& p) { std::string s; ReadText(p.string(), s); return s; }
bool Has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }
std::size_t CountEntries(const fs::path& dir) {
    std::size_t n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) ++n;
    return n;
}
}  // namespace

int main() {
    RUN_SUITE("project_create");

    // ---- the templates: one list, and the launcher and editor agree with it -----------------------------------
    {
        const auto& all = Templates();
        CHECK(all.size() == 21);
        std::set<std::string> titles;
        for (const TemplateInfo& t : all) {
            CHECK(titles.insert(t.title).second);                                  // no title twice
            CHECK(t.group >= 1 && t.group < (int)TemplateGroups().size());
            CHECK(std::string(t.blurb).size() > 8 && std::string(t.blurb).size() < 80);
        }
        CHECK(FindTemplate("3D Scene") && !FindTemplate("No Such Template") && !FindTemplate(""));
        CHECK(std::string(Templates().front().title) == "3D Scene");                // the default
        int starters = 0;
        for (const TemplateInfo& t : all) if (IsStarterTemplate(t)) ++starters;
        CHECK(starters == 3);                                                       // the few large cards
        for (const TemplateInfo& t : all) {
            const char *a = "", *b = "", *c = "";
            TemplateFeatureLines(t.title, a, b, c);
            CHECK(a && b && c && a[0] && b[0] && c[0]);
        }
        // The launcher's Create page uses this list (it no longer keeps a second copy). The editor's
        // New Project names the same titles. A title one of them lacks falls back to the default scene.
        const fs::path root = fs::path(__FILE__).parent_path().parent_path();
        std::string launcher, editor;
        if (ReadText((root / "launcher" / "main.cpp").string(), launcher) && ReadText((root / "editor" / "main.cpp").string(), editor)) {
            CHECK(launcher.find("okayproj::Templates()") != std::string::npos);
            CHECK(launcher.find("kTpl[]") == std::string::npos);
            CHECK(launcher.find("More options") != std::string::npos);
            CHECK(launcher.find("BeginPopupModal(\"New Project\"") != std::string::npos);
            CHECK(launcher.find("+  New Project") != std::string::npos);
            CHECK(launcher.find("--project-version") != std::string::npos);
            CHECK(editor.find("--project-version") != std::string::npos);
            auto section = [](const std::string& text, const char* start) {
                const std::size_t a = text.find(start);
                return a == std::string::npos ? std::string() : text.substr(a, text.find("};", a) - a);
            };
            std::set<std::string> inEditor;
            const std::string es = section(editor, "static const Tpl tpls[] = {");
            const std::regex ere("\\{\"([^\"]+)\",\\s+&EditorState::\\w+\\}");
            for (std::sregex_iterator it(es.begin(), es.end(), ere), end; it != end; ++it) inEditor.insert((*it)[1]);
            CHECK(inEditor == titles);
        } else std::cout << "  (source files not found next to the tests: template cross-check skipped)\n";
    }

    // ---- names -------------------------------------------------------------------------------------------
    {
        CHECK(CheckName("MyGame").ok() && CheckName("MyGame").folder == "MyGame");
        CHECK(CheckName("  My Game  ").ok() && CheckName("  My Game  ").folder == "My Game");   // spaces inside are fine, the ends are trimmed
        CHECK(CheckName("caf\xC3\xA9 \xE2\x9C\x93").ok());                                       // UTF-8 is fine
        CHECK(CheckName("Level-1_final.v2").ok());
        CHECK(!CheckName("").ok() && Has(CheckName("").problem, "name"));
        CHECK(!CheckName("   ").ok() && CheckName("   ").folder.empty());
        CHECK(!CheckName("\t \t").ok());
        for (const char* bad : {"a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b"}) {
            const NameCheck c = CheckName(bad);
            CHECK(!c.ok());
            CHECK(Has(c.problem, "can't contain"));
        }
        CHECK(!CheckName("a\nb").ok() && Has(CheckName("a\nb").problem, "control"));
        CHECK(!CheckName(std::string("a\0b", 3)).ok());
        CHECK(!CheckName(".").ok() && !CheckName("..").ok());
        CHECK(!CheckName(".hidden").ok() && Has(CheckName(".hidden").problem, "dot"));
        CHECK(!CheckName("Game.").ok());
        for (const char* res : {"CON", "con", "Prn", "AUX", "NUL", "COM1", "com9", "LPT3", "CON.txt", "nul.x"})
            CHECK(!CheckName(res).ok() && Has(CheckName(res).problem, "reserved"));
        CHECK(CheckName("COM0").ok() && CheckName("COM10").ok() && CheckName("CONSOLE").ok() && CheckName("Connect").ok());
        CHECK(CheckName(std::string(kMaxNameChars, 'a')).ok());
        CHECK(!CheckName(std::string(kMaxNameChars + 1, 'a')).ok() && Has(CheckName(std::string(kMaxNameChars + 1, 'a')).problem, "too long"));
        {   // the limit counts characters, not bytes
            std::string multi;
            for (std::size_t i = 0; i < kMaxNameChars; ++i) multi += "\xC3\xA9";
            CHECK(CheckName(multi).ok());
            CHECK(!CheckName(multi + "\xC3\xA9").ok());
        }
        // Sanitize: always gives something CheckName accepts.
        CHECK(SanitizeName("a/b\\c") == "a-b-c");
        CHECK(SanitizeName("  x  ") == "x");
        CHECK(SanitizeName("") == "MyGame" && SanitizeName("...") == "MyGame" && SanitizeName("NUL") == "MyGame");
        CHECK(SanitizeName("Game. ") == "Game");
        for (const char* t : {"a/b", "..", "CON", "x:y?z", "  ", "\x01", ".git"}) CHECK(CheckName(SanitizeName(t)).ok());
        {
            std::string longName(200, 'z');
            CHECK(CheckName(SanitizeName(longName)).ok() && SanitizeName(longName).size() == kMaxNameChars);
            std::string longMulti;
            for (int i = 0; i < 100; ++i) longMulti += "\xE2\x9C\x93";
            CHECK(CheckName(SanitizeName(longMulti)).ok());   // cut on a character boundary
        }
    }

    // ---- locations ---------------------------------------------------------------------------------------
    {
        TempDir t;
        CHECK(CheckLocation(t.path).empty());                                  // exists, writable
        CHECK(CheckLocation(t.path / "does" / "not" / "exist" / "yet").empty());   // made on create
        CHECK(Has(CheckLocation(fs::path()), "Choose"));
        Write(t.path / "afile", "x");
        CHECK(Has(CheckLocation(t.path / "afile"), "file, not a folder"));
        CHECK(Has(CheckLocation(t.path / "afile" / "sub"), "is a file"));
        CHECK(!CheckLocation(t.path, ~(std::uintmax_t)0 - 1).empty());          // asking for more room than any disk has
        CHECK(Has(CheckLocation(t.path, ~(std::uintmax_t)0 - 1), "free space"));
        CHECK(CountEntries(t.path) == 1);                                        // the probe file was removed again
        // A read-only folder (skipped when the process may write anywhere, as an administrator can).
        fs::create_directories(t.path / "ro");
        std::error_code ec;
        fs::permissions(t.path / "ro", fs::perms::owner_read | fs::perms::owner_exec, ec);
        std::string why;
        if (CanWriteIn(t.path / "ro", &why)) std::cout << "  (read-only folder check skipped: this process can write anywhere)\n";
        else {
            CHECK(Has(CheckLocation(t.path / "ro"), "Can't write"));
            CHECK(Has(CheckLocation(t.path / "ro" / "newfolder"), "Can't write"));
        }
        fs::permissions(t.path / "ro", fs::perms::all, ec);
    }

    // ---- the plan ----------------------------------------------------------------------------------------
    {
        TempDir t;
        Plan p = PlanProject(t.path.string(), "MyGame");
        CHECK(p.ok() && p.folder == "MyGame" && !p.renamed && p.root == Absolute(t.path) / "MyGame");
        CHECK(!fs::exists(p.root));                                              // planning makes nothing
        CHECK(CountEntries(t.path) == 0);

        // The name is taken by a project: Name-2, then Name-3.
        fs::create_directories(t.path / "MyGame" / "Assets");
        Write(t.path / "MyGame" / "Assets" / "MyGame.okayscene", "name \"x\"\n");
        p = PlanProject(t.path.string(), "MyGame");
        CHECK(p.ok() && p.folder == "MyGame-2" && p.renamed);
        fs::create_directories(t.path / "MyGame-2");
        Write(t.path / "MyGame-2" / "notes.txt", "clutter");                    // a folder with other files in it also counts as used
        p = PlanProject(t.path.string(), "MyGame");
        CHECK(p.folder == "MyGame-3");
        // An empty folder, and one holding only an empty Assets (the launcher's shell), can be used as it is.
        fs::create_directories(t.path / "Empty");
        CHECK(PlanProject(t.path.string(), "Empty").folder == "Empty");
        fs::create_directories(t.path / "Shell" / "Assets");
        CHECK(PlanProject(t.path.string(), "Shell").folder == "Shell" && !PlanProject(t.path.string(), "Shell").renamed);
        Write(t.path / "Shell" / "other.txt", "x");
        CHECK(PlanProject(t.path.string(), "Shell").folder == "Shell-2");
        Write(t.path / "AFile", "x");                                            // a plain file in the way
        CHECK(PlanProject(t.path.string(), "AFile").folder == "AFile-2");

        // Problems, one at a time, name first.
        p = PlanProject(t.path.string(), "bad/name");
        CHECK(!p.ok() && Has(p.Problem().c_str(), "can't contain") && Has(p.nameProblem, "can't contain"));
        p = PlanProject("", "Fine");
        CHECK(!p.ok() && Has(p.locationProblem, "Choose") && p.nameProblem.empty());
        p = PlanProject((t.path / "AFile").string(), "Fine");
        CHECK(!p.ok() && Has(p.Problem(), "file"));
        // A path that is too long to be saved into.
        p = PlanProject(t.path.string(), std::string(60, 'n'), /*maxRoot=*/t.path.string().size() + 30);
        CHECK(!p.ok() && Has(p.pathProblem, "full path") && Has(p.Problem(), "shorter"));
        p = PlanProject(t.path.string(), "short", /*maxRoot=*/t.path.string().size() + 30);
        CHECK(p.ok());
        // Spaces in the name and in the location are fine.
        fs::create_directories(t.path / "My Projects");
        p = PlanProject((t.path / "My Projects").string(), "  Space Game ");
        CHECK(p.ok() && p.folder == "Space Game" && p.root.filename() == "Space Game");
        // A relative location is made absolute.
        CHECK(PlanProject("some/relative/dir", "X").location.is_absolute());
    }

    // ---- the skeleton, and undoing it -------------------------------------------------------------------
    {
        TempDir t;
        const fs::path root = t.path / "deep" / "er" / "Game";
        Made made;
        std::string err;
        CHECK(MakeSkeleton(root, made, err) && err.empty());
        CHECK(fs::is_directory(root / "Assets") && fs::is_directory(root / ".okay"));
        for (const std::string& f : StandardFolders()) CHECK(fs::is_directory(root / "Assets" / f));
        CHECK(StandardFolders().size() == 7);
        CHECK(!made.paths.empty());
        std::set<fs::path> uniq(made.paths.begin(), made.paths.end());
        CHECK(uniq.size() == made.paths.size());                                 // no folder recorded twice
        Undo(made);
        CHECK(!fs::exists(t.path / "deep"));                                      // everything it made is gone, including the parents it made
        CHECK(fs::exists(t.path));

        // Making it again over an existing, partly filled project changes nothing that is there.
        fs::create_directories(root / "Assets" / "Scripts");
        Write(root / "Assets" / "Scripts" / "keep.okay", "code");
        Made again;
        CHECK(MakeSkeleton(root, again, err));
        CHECK(Read(root / "Assets" / "Scripts" / "keep.okay") == "code");
        for (const fs::path& p : again.paths) CHECK(p != root / "Assets" && p != root / "Assets" / "Scripts");   // only new folders are recorded
        Undo(again);
        CHECK(Read(root / "Assets" / "Scripts" / "keep.okay") == "code");        // Undo never touches what was already there
        CHECK(fs::exists(root / "Assets" / "Scripts"));
        std::error_code ec;
        fs::remove_all(t.path / "deep", ec);

        // A failure half way: a FILE sits where Models should go. Nothing is left behind.
        const fs::path root2 = t.path / "Half";
        fs::create_directories(root2 / "Assets");
        Write(root2 / "Assets" / "Models", "i am a file");
        Made m2;
        CHECK(!MakeSkeleton(root2, m2, err));
        CHECK(Has(err, "isn't a folder") && Has(err, "Models"));
        CHECK(m2.paths.empty());                                                  // a failed call records nothing
        CHECK(!fs::exists(root2 / "Assets" / "Scenes") && !fs::exists(root2 / "Assets" / "Scripts") && !fs::exists(root2 / "Assets" / "Prefabs"));
        CHECK(fs::exists(root2 / "Assets" / "Models") && !fs::is_directory(root2 / "Assets" / "Models"));   // the file is untouched
        CHECK(!fs::exists(root2 / ".okay"));

        // Same on a fresh root: the root and Assets this call made are removed too.
        const fs::path root3 = t.path / "Fresh" / "Game";
        fs::create_directories(root3 / "Assets");   // pre-existing Assets with a file in Audio's place
        Write(root3 / "Assets" / "Audio", "i am a file");
        Made m3;
        CHECK(!MakeSkeleton(root3, m3, err));
        CHECK(fs::is_directory(root3 / "Assets") && CountEntries(root3) == 2);   // Assets and the file are left, nothing else
    }

    // ---- a shell left behind -------------------------------------------------------------------------------
    {
        TempDir t;
        fs::create_directories(t.path / "Shell" / "Assets");
        CHECK(IsEmptyShell(t.path / "Shell"));
        CHECK(RemoveEmptyShell(t.path / "Shell") && !fs::exists(t.path / "Shell"));
        fs::create_directories(t.path / "Used" / "Assets");
        Write(t.path / "Used" / "Assets" / "a.okayscene", "x");
        CHECK(!IsEmptyShell(t.path / "Used") && !RemoveEmptyShell(t.path / "Used") && fs::exists(t.path / "Used" / "Assets" / "a.okayscene"));
        fs::create_directories(t.path / "Other" / "Assets");
        Write(t.path / "Other" / "notes.txt", "x");
        CHECK(!RemoveEmptyShell(t.path / "Other") && fs::exists(t.path / "Other" / "notes.txt"));    // someone's files: never removed
        fs::create_directories(t.path / "Sub" / "Assets" / "Scenes");
        CHECK(!RemoveEmptyShell(t.path / "Sub"));                                                     // not empty: it has a folder in it
        CHECK(!RemoveEmptyShell(t.path / "Missing") && !RemoveEmptyShell(t.path / "Other" / "notes.txt"));
    }

    // ---- writing files whole ------------------------------------------------------------------------------
    {
        TempDir t;
        const std::string f = (t.path / "project.okayproj").string();
        std::string err;
        CHECK(WriteTextAtomic(f, "one\n", &err) && err.empty());
        CHECK(Read(f) == "one\n");
        CHECK(WriteTextAtomic(f, "two\nthree\n", &err));
        CHECK(Read(f) == "two\nthree\n");
        CHECK(!fs::exists(f + ".tmp"));                                           // the temporary file is gone
        CHECK(WriteTextAtomic(f, "", &err) && Read(f).empty() && fs::exists(f));   // an empty file is a file
        std::string big(300000, 'x');
        CHECK(WriteTextAtomic(f, big, &err) && Read(f) == big);
        // The folder is missing: a clear error, nothing created.
        CHECK(!WriteTextAtomic((t.path / "nope" / "x.txt").string(), "x", &err));
        CHECK(Has(err, "doesn't exist") && !fs::exists(t.path / "nope"));
        // A read-only file is refused and left exactly as it was.
        WriteTextAtomic(f, "precious\n", &err);
        std::error_code ec;
        fs::permissions(f, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read, ec);
        CHECK(!WriteTextAtomic(f, "overwritten\n", &err));
        CHECK(Has(err, "read-only") && Read(f) == "precious\n" && !fs::exists(f + ".tmp"));
        fs::permissions(f, fs::perms::owner_read | fs::perms::owner_write, ec);
        CHECK(WriteTextAtomic(f, "now ok\n", &err) && Read(f) == "now ok\n");
        // A directory is not a file to replace.
        fs::create_directories(t.path / "adir");
        CHECK(!WriteTextAtomic((t.path / "adir").string(), "x", &err));
        CHECK(fs::is_directory(t.path / "adir") && !fs::exists((t.path / "adir").string() + ".tmp"));
        CHECK(SettingsPath("").size() > 0 && SettingsPath((t.path).string()) == (t.path / "project.okayproj").string());
    }

    // ---- Create tab: version and keyboard ------------------------------------------------------------------
    {
        CHECK(CheckProjectVersion("1.0.0").empty());
        CHECK(CheckProjectVersion("  2.1.0-beta ").empty());
        CHECK(Has(CheckProjectVersion(""), "version"));
        CHECK(Has(CheckProjectVersion("   "), "version"));
        CHECK(Has(CheckProjectVersion("beta"), "number"));
        CHECK(Has(CheckProjectVersion("1.0/2"), "1.0.0"));
        CHECK(Has(CheckProjectVersion(std::string(32, '1')), "too long"));
        CHECK(CheckProjectVersion(std::string(31, '1')).empty());

        NewProjectWizard w;
        CHECK(WizardOpen(w) == WizardMove::Opened && w.open && w.step == 0 && !w.creating);
        CHECK(WizardCanAdvance(w));                                          // a template is already chosen
        CHECK(WizardNext(w) == WizardMove::Next && w.step == 1);
        w.detailsOk = false;
        CHECK(!WizardCanAdvance(w));
        CHECK(WizardNext(w) == WizardMove::None && w.step == 1);             // invalid details block Next
        CHECK(ApplyWizardInput(true, false, false, w) == WizardMove::None);
        w.detailsOk = true;
        CHECK(WizardNext(w) == WizardMove::Next && w.step == 2);
        CHECK(WizardNext(w) == WizardMove::None && w.step == 2);             // review has Create, not another Next
        CHECK(ApplyWizardInput(true, false, true, w) == WizardMove::None);   // another field owns Enter
        CHECK(!w.creating);
        w.moreOpen = true;
        CHECK(ApplyWizardInput(false, true, false, w) == WizardMove::Back);
        CHECK(!w.moreOpen && w.open && w.step == 2);
        CHECK(ApplyWizardInput(true, false, false, w) == WizardMove::Create && w.creating);
        CHECK(ApplyWizardInput(false, true, false, w) == WizardMove::None);  // Esc does not cancel mid-create
        w.creating = false;
        CHECK(WizardBack(w) == WizardMove::Back && w.step == 1);
        CHECK(WizardBack(w) == WizardMove::Back && w.step == 0);
        CHECK(WizardBack(w) == WizardMove::None && w.step == 0);
        CHECK(ApplyWizardInput(false, true, false, w) == WizardMove::Closed && !w.open);
    }

    // ---- edges the Create wizard relies on -------------------------------------------------------------------------
    {
        // Windows-illegal names, in every spelling the box can receive them
        for (const char* bad : {"aux", "Nul.txt", "COM1.log", "lpt9", "CON  ", "name.", "name ..", "a|b", "a\"b", "tab\there\x01"})
            CHECK(!CheckName(bad).ok());
        CHECK(!CheckName("a|b").ok() && !CheckName("lpt9").ok() && !CheckName("CON  ").ok() && !CheckName("name.").ok());
        // A failed pass through the wizard never left a half-made project: a project a person is using is not replaced
        TempDir t;
        Write(t.path / "Taken", "a file, not a folder");                                  // the name is a file
        Plan pf = PlanProject(t.path.string(), "Taken");
        CHECK(pf.ok() && pf.folder == "Taken-2" && pf.renamed);
        fs::create_directories(t.path / "Game" / "Assets");
        Write(t.path / "Game" / "Assets" / "x.okayscene", "x");
        fs::create_directories(t.path / "Game-2");
        Write(t.path / "Game-2" / "notes.txt", "mine");
        Plan pg = PlanProject(t.path.string(), "Game");
        CHECK(pg.ok() && pg.folder == "Game-3");                                          // skips every used folder
        CHECK(Read(t.path / "Game-2" / "notes.txt") == "mine");                           // and touches none of them
        // an empty shell left by the launcher is reused, not renamed around
        fs::create_directories(t.path / "Shell" / "Assets");
        Plan ps = PlanProject(t.path.string(), "Shell");
        CHECK(ps.ok() && ps.folder == "Shell" && !ps.renamed);
        // a location that is a file is refused and nothing is made
        Plan pl = PlanProject((t.path / "Taken").string(), "Fresh");
        CHECK(!pl.ok() && !pl.locationProblem.empty() && !fs::exists(t.path / "Taken" / "Fresh"));
        // MakeDirs below a file fails, records nothing it did not make, and Undo leaves the file alone
        Made m;
        std::string err;
        CHECK(!MakeDirs(t.path / "Taken" / "Sub" / "Deep", m, err) && !err.empty());
        Undo(m);
        CHECK(fs::is_regular_file(t.path / "Taken") && Read(t.path / "Taken") == "a file, not a folder");
        // version box: empty, spaces and quotes are all refused (the launcher passes it on a command line)
        CHECK(!CheckProjectVersion("").empty() && !CheckProjectVersion("1 0").empty() && !CheckProjectVersion("1.0\"").empty());
        CHECK(CheckProjectVersion(" 1.0.0 ").empty());
    }

    TEST_MAIN_RESULT();
}
