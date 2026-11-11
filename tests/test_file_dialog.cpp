// shared/FileDialog: the pure helpers (path normalising, glob patterns, extension enforcing, start-path
// splitting), the argument lists for zenity / kdialog / osascript (user text stays ONE argument, there
// is no shell), result parsing, the built-in browser's model (listing, filter, selection, results, replace
// check), the callback plumbing (queue, cancel, answer) and a real run through a fake `zenity` / `kdialog`
// program on PATH (output, cancel, a broken helper falling back to the built-in browser, no injection).
// No window, nothing outside temp folders.
#include "test_framework.hpp"
#include "FileDialog.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace okay::filedialog;
using V = std::vector<std::string>;

static bool Has(const V& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
#if !defined(_WIN32)
static std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
#endif
static void Touch(const fs::path& p, const std::string& body = "x") { std::ofstream(p, std::ios::binary) << body; }

int main() {
    RUN_SUITE("paths");
    CHECK(NormalizePath("") == "");
    CHECK(NormalizePath("a\\b\\c.txt") == "a/b/c.txt");
    CHECK(NormalizePath("/a//b/./c/") == "/a/b/c");
    CHECK(NormalizePath("/a/b/../c") == "/a/c");
    CHECK(NormalizePath("/../..") == "/");
    CHECK(NormalizePath("../x") == "../x");
    CHECK(NormalizePath("a/..") == ".");
    CHECK(NormalizePath("C:\\Users\\me\\") == "C:/Users/me");
    CHECK(NormalizePath("C:\\") == "C:/");
    CHECK(NormalizePath("//server/share/x") == "//server/share/x");
    CHECK(FileNameOf("/a/b/c.png") == "c.png");
    CHECK(FileNameOf("c.png") == "c.png");
    CHECK(FileNameOf("/") == "");
    CHECK(ParentOf("/a/b/c.png") == "/a/b");
    CHECK(ParentOf("/a") == "/");
    CHECK(ParentOf("/") == "/");
    CHECK(ParentOf("C:/a") == "C:/");
    CHECK(ParentOf("name.txt") == "");
    CHECK(ExtensionOf("/x/Photo.PNG") == ".png");
    CHECK(ExtensionOf("/x/.bashrc") == "");
    CHECK(ExtensionOf("/x/noext") == "");
    CHECK(ExtensionOf("/x/a.tar.gz") == ".gz");
    CHECK(ExtensionOf("/x/trailing.") == "");

    RUN_SUITE("patterns");
    CHECK((ParsePatterns("*.png;*.jpg") == V{"*.png", "*.jpg"}));
    CHECK((ParsePatterns(" *.png , *.jpg  *.gif ") == V{"*.png", "*.jpg", "*.gif"}));
    CHECK(ParsePatterns("").empty());
    CHECK(MatchPattern("*.png", "a.PNG"));
    CHECK(MatchPattern("*.png", ".png"));
    CHECK(!MatchPattern("*.png", "a.pngx"));
    CHECK(!MatchPattern("*.png", "png"));
    CHECK(MatchPattern("data?.csv", "data1.csv"));
    CHECK(!MatchPattern("data?.csv", "data.csv"));
    CHECK(MatchPattern("*", "anything"));
    CHECK(MatchPattern("*.*", "noext"));
    CHECK(MatchPattern("a*b*c", "aXXbYYc"));
    CHECK(!MatchPattern("a*b*c", "aXXbYY"));
    CHECK(MatchPattern("*.okayscene", "Level 1.okayscene"));
    CHECK(MatchesAny({}, "x"));
    CHECK(MatchesAny({"*.a", "*.b"}, "z.B"));
    CHECK(!MatchesAny({"*.a", "*.b"}, "z.c"));
    Filter img{"Images", {"*.png", "*.jpg"}};
    CHECK(MatchesFilter(img, "x.jpg"));
    CHECK(!MatchesFilter(img, "x.txt"));
    CHECK(MatchesFilter(Filter{}, "x.txt"));
    CHECK(LiteralExtension("*.png") == ".png");
    CHECK(LiteralExtension("*.PNG") == ".png");
    CHECK(LiteralExtension("*.tar.gz") == ".tar.gz");
    CHECK(LiteralExtension("*") == "");
    CHECK(LiteralExtension("*.*") == "");
    CHECK(LiteralExtension("data*.png") == "");
    CHECK(LiteralExtension("*.p?g") == "");

    RUN_SUITE("extension enforcing");
    Filter scene{"OkaySpace scene", {"*.okayscene"}};
    CHECK(EnforceExtension("/a/Level", &scene) == "/a/Level.okayscene");
    CHECK(EnforceExtension("/a/Level.okayscene", &scene) == "/a/Level.okayscene");
    CHECK(EnforceExtension("/a/Level.txt", &scene) == "/a/Level.txt");
    CHECK(EnforceExtension("/a/Level.", &scene) == "/a/Level.okayscene");
    CHECK(EnforceExtension("/a/Level", &img) == "/a/Level");   // two patterns: no guess
    CHECK(EnforceExtension("/a/Level", nullptr) == "/a/Level");
    CHECK(EnforceExtension("", &scene) == "");
    CHECK(EnforceExtension("/a/.hidden", &scene) == "/a/.hidden.okayscene");
    Filter all{"All", {"*"}};
    CHECK(EnforceExtension("/a/x", &all) == "/a/x");

    RUN_SUITE("start path");
    CHECK(SplitStart("", false).dir == "" && SplitStart("", false).name == "");
    CHECK(SplitStart("/home/me/", false).dir == "/home/me" && SplitStart("/home/me/", false).name == "");
    CHECK(SplitStart("/home/me", true).dir == "/home/me");
    CHECK(SplitStart("/home/me/clips.okayanim", false).dir == "/home/me");
    CHECK(SplitStart("/home/me/clips.okayanim", false).name == "clips.okayanim");
    CHECK(SplitStart("heightmap.png", false).dir == "");
    CHECK(SplitStart("heightmap.png", false).name == "heightmap.png");
    CHECK(SplitStart("C:\\Users\\me\\x.csv", false).dir == "C:/Users/me");

    RUN_SUITE("zenity arguments");
    {
        Request r; r.kind = Kind::OpenFile; r.title = "Pick $(touch /tmp/pwned) `x` ; rm -rf /"; r.startPath = "/home/me/"; r.multi = true;
        r.filters = {Filter{"Images", {"*.png", "*.jpg"}}};
        V a = ZenityArgs(r);
        CHECK(a[0] == "zenity" && a[1] == "--file-selection");
        CHECK(Has(a, "--title=Pick $(touch /tmp/pwned) `x` ; rm -rf /"));   // the whole text is one element
        CHECK(Has(a, "--multiple"));
        CHECK(Has(a, "--separator=\n"));
        CHECK(Has(a, "--filename=/home/me/"));
        CHECK(Has(a, "--file-filter=Images | *.png *.PNG *.jpg *.JPG"));
        CHECK(Has(a, "--file-filter=All files | *"));
        for (const auto& s : a) CHECK(s.find(' ') == std::string::npos || s.rfind("--", 0) == 0);   // free text only ever follows an option name
        Request s; s.kind = Kind::SaveFile; s.title = "Save"; s.startPath = "/tmp/out/clip.okayanim"; s.filters = {Filter{"Anim", {"*.okayanim"}}};
        V b = ZenityArgs(s);
        CHECK(Has(b, "--save")); CHECK(Has(b, "--confirm-overwrite")); CHECK(Has(b, "--filename=/tmp/out/clip.okayanim"));
        CHECK(!Has(b, "--multiple"));
        Request f; f.kind = Kind::Folder; f.title = "Dir"; f.startPath = "/srv";
        V c = ZenityArgs(f);
        CHECK(Has(c, "--directory")); CHECK(Has(c, "--filename=/srv/"));
        for (const auto& x : c) CHECK(x.rfind("--file-filter", 0) != 0);
        Request nf; nf.kind = Kind::OpenFile; V d = ZenityArgs(nf);
        for (const auto& x : d) CHECK(x.rfind("--file-filter", 0) != 0 && x.rfind("--title", 0) != 0);
        Request pipe; pipe.kind = Kind::OpenFile; pipe.filters = {Filter{"A|B\nC", {"*.x"}}};
        CHECK(Has(ZenityArgs(pipe), "--file-filter=A B C | *.x *.X"));   // a '|' or newline in a name cannot split the filter
    }

    RUN_SUITE("kdialog arguments");
    {
        Request r; r.kind = Kind::OpenFile; r.title = "-evil $(id)"; r.startPath = "-rf/"; r.multi = true; r.filters = {Filter{"Audio", {"*.wav", "*.ogg"}}};
        V a = KdialogArgs(r);
        CHECK(a[0] == "kdialog");
        CHECK(Has(a, "--multiple")); CHECK(Has(a, "--separate-output")); CHECK(Has(a, "--getopenfilename"));
        CHECK(Has(a, " -evil $(id)"));                      // a leading dash cannot become an option
        CHECK(Has(a, "./-rf/"));                            // nor can the start folder
        CHECK(Has(a, "*.wav *.WAV *.ogg *.OGG|Audio\n*|All files"));
        Request s; s.kind = Kind::SaveFile; s.startPath = "x.csv"; V b = KdialogArgs(s);
        CHECK(Has(b, "--getsavefilename")); CHECK(Has(b, "x.csv"));
        Request f; f.kind = Kind::Folder; V c = KdialogArgs(f);
        CHECK(Has(c, "--getexistingdirectory")); CHECK(Has(c, "."));
    }

    RUN_SUITE("osascript arguments");
    {
        CHECK(AppleScriptQuote("a\"b\\c\nd") == "\"a\\\"b\\\\c d\"");
        Request r; r.kind = Kind::OpenFile; r.title = "Say \"hi\" & quit"; r.startPath = "/Users/me/"; r.filters = {Filter{"Images", {"*.png", "*.jpg"}}};
        V a = OsascriptArgs(r);
        CHECK(a[0] == "osascript" && a[1] == "-e");
        CHECK(a.size() == 3);   // one script line; user text is inside a quoted literal, not a separate command
        CHECK(a[2].find("choose file with prompt \"Say \\\"hi\\\" & quit\"") != std::string::npos);
        CHECK(a[2].find("of type {\"png\", \"jpg\"}") != std::string::npos);
        CHECK(a[2].find("POSIX file \"/Users/me\"") != std::string::npos);
        Request m = r; m.multi = true; V b = OsascriptArgs(m);
        CHECK(b.size() > 3);
        std::string all; for (const auto& x : b) all += x + "\n";
        CHECK(all.find("multiple selections allowed") != std::string::npos);
        Request s; s.kind = Kind::SaveFile; s.title = "Save"; s.startPath = "/tmp/a.png"; V c = OsascriptArgs(s);
        CHECK(c[2].find("choose file name") != std::string::npos && c[2].find("default name \"a.png\"") != std::string::npos);
        Request f; f.kind = Kind::Folder; V d = OsascriptArgs(f);
        CHECK(d[2].find("choose folder") != std::string::npos);
        Request w; w.kind = Kind::OpenFile; w.filters = {Filter{"x", {"data*.png"}}};
        CHECK(OsascriptArgs(w)[2].find("of type") == std::string::npos);   // a pattern a type list cannot express: no type restriction
    }

    RUN_SUITE("result parsing");
    CHECK((ParseOutput("/a/b.png\n", false) == V{"/a/b.png"}));
    CHECK((ParseOutput("/a/b.png\r\n/c/d.png\r\n", true) == V{"/a/b.png", "/c/d.png"}));
    CHECK((ParseOutput("/a/b.png\n/c/d.png\n", false) == V{"/a/b.png"}));
    CHECK(ParseOutput("", true).empty());
    CHECK(ParseOutput("\n\n", true).empty());
    CHECK((ParseOutput("/a/with space/x y.png", false) == V{"/a/with space/x y.png"}));
    CHECK((ParseOutput("/a/we|rd.png\n", true) == V{"/a/we|rd.png"}));   // '|' is a legal file name character
    {
        Request s; s.kind = Kind::SaveFile; s.filters = {scene};
        CHECK((FinishResults(s, {"/x/Level"}) == V{"/x/Level.okayscene"}));
        Request f; f.kind = Kind::Folder;
        CHECK((FinishResults(f, {"/x/dir/"}) == V{"/x/dir"}));
        CHECK((FinishResults(f, {"/"}) == V{"/"}));
        Request o; o.kind = Kind::OpenFile; o.filters = {scene};
        CHECK((FinishResults(o, {"/x/Level"}) == V{"/x/Level"}));   // open never renames
    }

    // ----- the built-in browser's model on a temp tree -----
    const fs::path root = fs::temp_directory_path() / ("okay_fdlg_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(root / "Assets" / "Sub", ec);
    fs::create_directories(root / "zeta", ec);
    fs::create_directories(root / ".hiddendir", ec);
    Touch(root / "b.png"); Touch(root / "A.PNG"); Touch(root / "c.txt"); Touch(root / ".secret.png"); Touch(root / "Assets" / "x.okayscene", "12345");
    const std::string R = NormalizePath(root.string());

    RUN_SUITE("browser model");
    {
        BrowserModel m;
        Request r; r.kind = Kind::OpenFile; r.title = "t"; r.startPath = R + "/"; r.filters = {Filter{"Images", {"*.png"}}};
        m.Begin(r);
        CHECK(m.Dir() == R);
        const auto& e = m.Entries();
        CHECK(e.size() == 4);   // Assets, zeta, A.PNG, b.png  (hidden and .txt are left out)
        CHECK(e[0].name == "Assets" && e[0].isDir);
        CHECK(e[1].name == "zeta" && e[1].isDir);
        CHECK(e[2].name == "A.PNG" && !e[2].isDir && e[2].size == 1);
        CHECK(e[3].name == "b.png");
        m.SetShowHidden(true);
        CHECK(m.Entries().size() == 6);
        m.SetShowHidden(false);
        m.SetFilterIndex(1);   // "All files"
        CHECK(m.Entries().size() == 5);
        m.SetFilterIndex(0);
        // nothing chosen
        CHECK(m.Result().empty() && !m.Why().empty());
        // selecting
        m.Select("b.png", false);
        CHECK((m.Result() == V{R + "/b.png"}));
        m.Select("A.PNG", true);   // not multi: replaces
        CHECK((m.Result() == V{R + "/A.PNG"}));
        m.Select("Assets", false);   // folders are entered, not chosen
        CHECK((m.Result() == V{R + "/A.PNG"}));
        // navigating
        CHECK(m.Enter("Assets"));
        CHECK(m.Dir() == R + "/Assets");
        CHECK(m.Selected().empty());
        CHECK(!m.Enter("x.okayscene"));   // a file selects
        CHECK(m.IsSelected("x.okayscene") == false);   // hidden by the *.png filter
        CHECK(m.Up());
        CHECK(m.Dir() == R);
        CHECK(!m.GoTo(R + "/nope"));
        CHECK(m.Dir() == R);
        CHECK(!m.GoTo(R + "/b.png"));   // a file is not a folder
        CHECK(m.GoTo(R + "/Assets/../zeta"));
        CHECK(m.Dir() == R + "/zeta");
        CHECK(m.Entries().empty());
        // typed name opens that file when nothing is selected
        m.GoTo(R);
        m.typed = "b.png";
        CHECK((m.Result() == V{R + "/b.png"}));
        m.typed = "missing.png";
        CHECK(m.Result().empty());
        m.typed = R + "/c.txt";
        CHECK((m.Result() == V{R + "/c.txt"}));
        CHECK(!m.Places().empty());
    }
    {
        BrowserModel m;
        Request r; r.kind = Kind::OpenFile; r.multi = true; r.startPath = R; r.filters = {Filter{"Images", {"*.png"}}};
        m.Begin(r);
        m.Select("A.PNG", false); m.Select("b.png", true);
        CHECK((m.Result() == V{R + "/A.PNG", R + "/b.png"}));
        m.Select("A.PNG", true);   // ctrl-click again deselects
        CHECK((m.Result() == V{R + "/b.png"}));
    }
    {
        BrowserModel m;   // a start path that does not exist walks up to the nearest folder that does
        Request r; r.kind = Kind::OpenFile; r.startPath = R + "/Assets/not/here/";
        m.Begin(r);
        CHECK(m.Dir() == R + "/Assets");
    }
    {
        BrowserModel m;   // save: suggested name, extension enforced, folder must exist, replace check
        Request r; r.kind = Kind::SaveFile; r.startPath = R + "/Level"; r.filters = {scene};
        m.Begin(r);
        CHECK(m.Dir() == R);
        CHECK(m.typed == "Level");
        CHECK((m.Result() == V{R + "/Level.okayscene"}));
        CHECK(!m.ResultExists());
        m.typed = "   ";
        CHECK(m.Result().empty());
        m.typed = "..";
        CHECK(m.Result().empty());
        m.typed = "Assets";
        CHECK(m.Result().empty());   // an existing folder
        m.typed = "nodir/x";
        CHECK(m.Result().empty());   // the folder does not exist
        m.GoTo(R + "/Assets");
        m.SetFilterIndex(1);   // All files: the typed name is kept as is
        m.typed = "x.okayscene";
        CHECK((m.Result() == V{R + "/Assets/x.okayscene"}));
        CHECK(m.ResultExists());
        m.SetFilterIndex(0);
        m.typed = "x";
        CHECK((m.Result() == V{R + "/Assets/x.okayscene"}));
        CHECK(m.ResultExists());
        m.typed = "new";
        CHECK(!m.ResultExists());
        // clicking a file in a save dialog fills the name
        m.SetFilterIndex(0);
        m.Select("x.okayscene", false);
        CHECK(m.typed == "x.okayscene");
    }
    {
        BrowserModel m;   // folder mode: only folders listed; the current folder is the answer unless one is selected
        Request r; r.kind = Kind::Folder; r.startPath = R;
        m.Begin(r);
        CHECK(m.Entries().size() == 2);
        CHECK((m.Result() == V{R}));
        m.Select("zeta", false);
        CHECK((m.Result() == V{R + "/zeta"}));
        m.Enter("Assets");
        CHECK((m.Result() == V{R + "/Assets"}));
    }

    {
        BrowserModel m;   // a relative start folder is made absolute (callers pass project-relative paths)
        const fs::path cwd = fs::current_path(ec);
        fs::current_path(root, ec);
        Request r; r.kind = Kind::SaveFile; r.startPath = "Assets/Level";
        m.Begin(r);
        CHECK(m.Dir() == R + "/Assets");
        CHECK(m.Dir()[0] == '/' || m.Dir()[1] == ':');
        CHECK(m.GoTo("zeta") && m.Dir() == R + "/zeta");
        fs::current_path(cwd, ec);
    }

    RUN_SUITE("callback plumbing (built-in)");
    {
#ifdef _WIN32
        _putenv_s("OKAY_FILE_DIALOG", "builtin");
#else
        setenv("OKAY_FILE_DIALOG", "builtin", 1);
#endif
        ResetBackendForTests();
        CHECK(CurrentBackend() == Backend::Builtin);
        CancelBuiltin();
        CHECK(!BuiltinOpen());
        std::vector<std::string> got; int calls = 0;
        OpenOne("Pick", R + "/", {Filter{"Images", {"*.png"}}}, [&](const std::string& p) { got.push_back(p); ++calls; });
        CHECK(BuiltinOpen());
        CHECK(BuiltinModel() && BuiltinModel()->Req().title == "Pick");
        CHECK(calls == 0);   // nothing happens until the user answers
        // a second request queues behind the first
        std::vector<std::string> got2;
        Open("Many", R + "/", {}, true, [&](const std::vector<std::string>& v) { got2 = v; });
        CHECK(BuiltinModel()->Req().title == "Pick");
        BuiltinModel()->Select("b.png", false);
        FinishBuiltin(true);   // accept
        CHECK(calls == 1 && got.size() == 1 && got[0] == R + "/b.png");
        CHECK(BuiltinOpen() && BuiltinModel()->Req().title == "Many");   // the queued one is up
        FinishBuiltin(false);   // cancel: no callback
        CHECK(got2.empty());
        CHECK(!BuiltinOpen());
        // answering by script
        FolderOne("Dir", R, [&](const std::string& p) { got.push_back(p); });
        CHECK(AnswerBuiltinForTests({"/some/dir"}));
        CHECK(got.back() == "/some/dir");
        CHECK(!AnswerBuiltinForTests({"/x"}));   // nothing showing
        // a callback may ask for the next dialog
        int chain = 0;
        OpenOne("first", R, {}, [&](const std::string&) { ++chain; OpenOne("second", R, {}, [&](const std::string&) { ++chain; }); });
        CHECK(AnswerBuiltinForTests({"/a"}));
        CHECK(chain == 1 && BuiltinOpen() && BuiltinModel()->Req().title == "second");
        CHECK(AnswerBuiltinForTests({"/b"}));
        CHECK(chain == 2 && !BuiltinOpen());
        // an empty answer is a cancel
        int c3 = 0; OpenOne("t", R, {}, [&](const std::string&) { ++c3; });
        CHECK(AnswerBuiltinForTests({}));
        CHECK(c3 == 0);
        CancelBuiltin();
        CHECK(!BuiltinOpen());
        CHECK(!RunNative(Request{}, got));   // the built-in backend never claims a request
    }

#if !defined(_WIN32)
    RUN_SUITE("helper programs (fake zenity / kdialog on PATH)");
    {
        const fs::path bin = root / "bin";
        fs::create_directories(bin, ec);
        const fs::path argLog = root / "args.log";
        auto script = [&](const std::string& name, const std::string& body) {
            std::ofstream(bin / name) << "#!/bin/sh\n" << body;
            chmod((bin / name).c_str(), 0755);
        };
        // Records each argument on its own line, then prints a canned answer.
        script("zenity", "for a in \"$@\"; do printf '%s\\n' \"$a\" >> '" + argLog.string() + "'; done\nprintf '/picked/one.png\\n/picked/two.png\\n'\nexit 0\n");
        const char* oldPath = std::getenv("PATH"); std::string savedPath = oldPath ? oldPath : "";
        const char* oldDisp = std::getenv("DISPLAY"); std::string savedDisp = oldDisp ? oldDisp : "";
        setenv("PATH", (bin.string() + ":" + savedPath).c_str(), 1);
        setenv("DISPLAY", ":99", 1);
        unsetenv("OKAY_FILE_DIALOG");
        ResetBackendForTests();
        CHECK(CurrentBackend() == Backend::Zenity);
        CHECK(FindOnPath("zenity"));
        CHECK(!FindOnPath("no-such-tool-okay-fdlg"));

        V res;
        Request r; r.kind = Kind::OpenFile; r.title = "Hello \"$(touch " + (root / "pwned").string() + ")\""; r.multi = true; r.startPath = R + "/";
        CHECK(RunNative(r, res));
        CHECK((res == V{"/picked/one.png", "/picked/two.png"}));
        const std::string log = Slurp(argLog);
        CHECK(log.find("--title=Hello \"$(touch ") != std::string::npos);   // arrived verbatim as one argument
        CHECK(!fs::exists(root / "pwned"));                                 // and nothing was executed
        // single select keeps only the first
        Request one; one.kind = Kind::OpenFile;
        CHECK(RunNative(one, res)); CHECK((res == V{"/picked/one.png"}));
        // the call-site API runs the callback straight away with a native dialog
        std::string cbGot;
        OpenOne("t", "", {}, [&](const std::string& p) { cbGot = p; });
        CHECK(cbGot == "/picked/one.png");
        CHECK(!BuiltinOpen());
        // cancel (exit 1): handled, empty, no callback
        script("zenity", "exit 1\n");
        int cbs = 0;
        CHECK(RunNative(one, res)); CHECK(res.empty());
        OpenOne("t", "", {}, [&](const std::string&) { ++cbs; });
        CHECK(cbs == 0 && !BuiltinOpen());
        // a helper that fails (not 0/1, e.g. no display) hands over to the built-in browser, once and for all
        script("zenity", "exit 255\n");
        ResetBackendForTests();
        CHECK(!RunNative(one, res));
        CHECK(CurrentBackend() == Backend::Builtin);
        OpenOne("t", R, {}, [&](const std::string&) { ++cbs; });
        CHECK(BuiltinOpen());
        CancelBuiltin();
        // kdialog when there is no zenity
        fs::remove(bin / "zenity");
        script("kdialog", "printf '/k/chosen.txt\\n'\nexit 0\n");
        ResetBackendForTests();
        CHECK(CurrentBackend() == Backend::Kdialog);
        CHECK(RunNative(one, res)); CHECK((res == V{"/k/chosen.txt"}));
        // save: the result gets its extension
        Request sv; sv.kind = Kind::SaveFile; sv.filters = {scene};
        script("kdialog", "printf '/k/Level\\n'\n");
        CHECK(RunNative(sv, res)); CHECK((res == V{"/k/Level.okayscene"}));
        // a folder result loses its trailing slash
        Request fo; fo.kind = Kind::Folder;
        script("kdialog", "printf '/k/dir/\\n'\n");
        CHECK(RunNative(fo, res)); CHECK((res == V{"/k/dir"}));
        // no tools and no display: built-in
        fs::remove(bin / "kdialog");
        ResetBackendForTests();
        setenv("PATH", bin.c_str(), 1);
        CHECK(CurrentBackend() == Backend::Builtin);
        setenv("PATH", (bin.string() + ":" + savedPath).c_str(), 1);
        script("zenity", "exit 0\n");
        unsetenv("DISPLAY"); unsetenv("WAYLAND_DISPLAY");
        ResetBackendForTests();
        CHECK(CurrentBackend() == Backend::Builtin);   // a helper is no use without a display
        setenv("PATH", savedPath.c_str(), 1);
        if (!savedDisp.empty()) setenv("DISPLAY", savedDisp.c_str(), 1);
        ResetBackendForTests();
    }
#endif

    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
