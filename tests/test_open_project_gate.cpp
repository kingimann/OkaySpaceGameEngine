// The launcher asks the project-version question BEFORE it starts the editor (launcher/OpenProjectGate.hpp, header-only, no window,
// no disk). Checked here: which question comes for every combination of project version (older / same / newer / none / not a
// version), editor version (a release, another patch, a development build), installed versions, default editor, "Open with" choice
// and "is this a project at all"; the exact option list, its order and its default; what each answer starts (which editor, the exact
// argument list, quoting of paths with spaces, quotes and `$`); that the "upgrade" and "open anyway" flags are never produced for the
// wrong question or without a launcher handshake; that the wording is the editor's own (kept as literal text below, so an
// accidental edit of the shared sentences shows up); and, on temp folders, that a scene that is not part of a project never asks.
#include "test_framework.hpp"
#include "OpenProjectGate.hpp"
#include "ProjectRoot.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace okayopen;
namespace qs = okayver::question;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_opengate_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};
void Put(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string Get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

using Strings = std::vector<std::string>;

const char* kScene = "/p/My Game/Assets/main.okayscene";

// An older project, opened with this launcher's editor v2.875.0; v2.850.1 and v2.860.0 are installed; launcher process 4242.
Input Base() {
    Input in;
    in.scene = kScene;
    in.projectName = "MyGame";
    in.projectRoot = "/p/My Game";
    in.projectVersion = "2.850.0";
    in.editorVersion = "2.875.0";
    in.installed = {"2.860.0", "2.850.1"};
    in.launcherPid = 4242;
    return in;
}

Strings Labels(const Prompt& p) { Strings s; for (const Option& o : p.options) s.push_back(o.label); return s; }
std::vector<Answer> Answers(const Prompt& p) { std::vector<Answer> a; for (const Option& o : p.options) a.push_back(o.answer); return a; }
int Primaries(const Prompt& p) { int n = 0; for (const Option& o : p.options) if (o.primary) ++n; return n; }
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
// An option by position; an empty one when there is none, so a wrong answer is a failed check and not a crash.
const Option& Opt(const Prompt& p, std::size_t i) { static const Option none; return i < p.options.size() ? p.options[i] : none; }
std::string LabelAt(const Prompt& p, std::size_t i) { return Opt(p, i).label; }
bool NoOutcome(const Outcome& o) { return !o.launch.start && o.launch.args.empty() && o.next == Next::None; }
bool AnyArgStartsWith(const Strings& args, const char* prefix) {
    for (const std::string& a : args) if (a.rfind(prefix, 0) == 0) return true;
    return false;
}
bool IsDev(const std::string& v) { return !okayver::ParseVersion(v).valid; }

}  // namespace

int main() {
    RUN_SUITE("open_project_gate");

    // ---- which question comes -----------------------------------------------------------------------------------------------------
    {
        struct Row {
            const char* why;
            std::string project, editor, chosen;
            Strings installed;
            std::string defaultEditor;
            bool isProject;
            Question expect;
        };
        const Strings none, inst = {"2.850.1", "2.700.0"};
        const std::vector<Row> rows = {
            {"same version",                          "2.875.0", "2.875.0", "", none, "", true, Question::None},
            {"same version, other spelling",          "v2.875",  "2.875.0", "", none, "", true, Question::None},
            {"a newer patch of this line",            "2.875.3", "2.875.0", "", none, "", true, Question::None},
            {"an older patch of this line",           "2.875.0", "2.875.3", "", none, "", true, Question::None},
            {"older line",                            "2.850.0", "2.875.0", "", none, "", true, Question::Older},
            {"older line, a patch of it",             "2.850.9", "2.875.0", "", none, "", true, Question::Older},
            {"one line older",                        "2.874.0", "2.875.0", "", none, "", true, Question::Older},
            {"first release",                         "1.0.0",   "2.875.0", "", none, "", true, Question::Older},
            {"older, other spelling",                 "v2.850",  "2.875.0", "", none, "", true, Question::Older},
            {"one line newer",                        "2.876.0", "2.875.0", "", none, "", true, Question::Newer},
            {"far newer",                             "2.880.0", "2.875.0", "", none, "", true, Question::Newer},
            {"a newer major version",                 "3.0.0",   "2.875.0", "", none, "", true, Question::Newer},
            {"no version recorded",                   "",        "2.875.0", "", none, "", true, Question::Unversioned},
            {"the stamp is not a version",            "garbage", "2.875.0", "", none, "", true, Question::Unversioned},
            {"development build, older project",      "2.850.0", "dev",     "", none, "", true, Question::None},
            {"development build, newer project",      "2.880.0", "dev",     "", none, "", true, Question::None},
            {"development build, no version",         "",        "dev",     "", none, "", true, Question::None},
            {"editor with no version at all",         "2.850.0", "",        "", none, "", true, Question::None},
            {"not a project, older",                  "2.850.0", "2.875.0", "", none, "", false, Question::None},
            {"not a project, newer",                  "2.880.0", "2.875.0", "", none, "", false, Question::None},
            {"not a project, no version",             "",        "2.875.0", "", none, "", false, Question::None},
            {"Open with: the project's line",         "2.850.0", "2.875.0", "2.850.1", inst, "", true, Question::None},
            {"Open with: no version recorded",        "",        "2.875.0", "2.850.1", inst, "", true, Question::None},
            {"Open with: another line, older project","2.850.0", "2.875.0", "2.860.0", inst, "", true, Question::None},
            {"Open with: project newer than editor",  "2.875.0", "2.875.0", "2.850.1", inst, "", true, Question::Newer},
            {"Open with: project newer, far",         "2.880.0", "2.875.0", "2.860.0", inst, "", true, Question::Newer},
            {"Open with: not a project",              "2.880.0", "2.875.0", "2.850.1", inst, "", false, Question::None},
            {"no version + default editor installed", "",        "2.875.0", "", inst, "2.850.1", true, Question::None},
            {"no version + default not installed",    "",        "2.875.0", "", inst, "2.700.7", true, Question::Unversioned},
            {"no version + default is this editor",   "",        "2.875.0", "", inst, "2.875.0", true, Question::Unversioned},
            {"no version, nothing installed",         "",        "2.875.0", "", none, "2.850.1", true, Question::Unversioned},
            {"older + default editor installed",      "2.850.0", "2.875.0", "", inst, "2.850.1", true, Question::Older},
            {"newer + default editor installed",      "2.880.0", "2.875.0", "", inst, "2.850.1", true, Question::Newer},
            {"older, other editors installed",        "2.850.0", "2.875.0", "", inst, "", true, Question::Older},
        };
        for (const Row& r : rows) {
            Input in = Base();
            in.projectVersion = r.project; in.editorVersion = r.editor; in.chosenVersion = r.chosen;
            in.installed = r.installed; in.defaultEditor = r.defaultEditor;
            in.projectRoot = r.isProject ? "/p/My Game" : "";
            const Prompt p = Ask(in);
            if (p.question != r.expect) std::cerr << "  row: " << r.why << "\n";
            CHECK(p.question == r.expect);
            CHECK((p.question == Question::None) == p.options.empty());
        }
    }

    // ---- the exact options, their order and the default -----------------------------------------------------------------------
    {
        // Older, an editor of the project's line is installed
        Input in = Base();
        Prompt p = Ask(in);
        CHECK(p.question == Question::Older);
        CHECK(Labels(p) == (Strings{"Upgrade project to v2.875.0", "Open with v2.850.x", "Cancel"}));
        CHECK(Answers(p) == (std::vector<Answer>{Answer::Upgrade, Answer::OpenWithOld, Answer::Cancel}));
        CHECK(Primaries(p) == 1 && p.options.size() == 3 && Opt(p, 0).primary);
        CHECK(p.eyebrow == "PROJECT VERSION" && p.root == "/p/My Game" && !p.warn);
        CHECK(p.heading == "MyGame was made with v2.850.0. This editor is v2.875.0.");
        CHECK(p.paragraphs == (Strings{
            "Upgrading brings the project's scenes and prefabs up to date for v2.875.0. A complete backup copy of the project is made first, next to it, so you can always go back.",
            "Opening it with v2.850.x does not touch the project at all: it opens with that version's editor instead."}));
        CHECK(p.status == "v2.850.1 is installed and would open it." && p.statusGood);

        // the newest installed editor of the line is the one named, whatever order the list is in
        in.installed = {"2.850.1", "2.850.2", "2.850.0", "2.860.0"};
        p = Ask(in);
        CHECK(p.status == "v2.850.2 is installed and would open it." && p.statusGood && LabelAt(p, 1) == "Open with v2.850.x");

        // Older, no editor of that line is installed: say so, and offer to install it
        for (const Strings& installed : {Strings{}, Strings{"2.860.0", "2.700.0"}, Strings{"2.875.0"}}) {
            in = Base();
            in.installed = installed;
            p = Ask(in);
            CHECK(Labels(p) == (Strings{"Upgrade project to v2.875.0", "Install v2.850.x", "Cancel"}));
            CHECK(Answers(p) == (std::vector<Answer>{Answer::Upgrade, Answer::InstallOld, Answer::Cancel}));
            CHECK(Primaries(p) == 1 && Opt(p, 0).primary && p.status == "v2.850.x isn't installed on this computer." && !p.statusGood);
            CHECK(!Opt(p, 1).tip.empty() && Has(Opt(p, 1).tip, "Versions"));
        }

        // no version recorded: only upgrading is offered (the old version can't be known)
        in = Base();
        in.projectVersion = "";
        p = Ask(in);
        CHECK(p.question == Question::Unversioned);
        CHECK(Labels(p) == (Strings{"Upgrade project to v2.875.0", "Cancel"}));
        CHECK(Answers(p) == (std::vector<Answer>{Answer::Upgrade, Answer::Cancel}) && Primaries(p) == 1 && Opt(p, 0).primary);
        CHECK(p.heading == "MyGame has no version recorded (made before this feature). This editor is v2.875.0.");
        CHECK(p.paragraphs == (Strings{
            "There is no way to tell which version made it, so only upgrading is offered. A complete backup copy of the project is made first, next to it, "
            "then its scenes and prefabs are brought up to date for v2.875.0 and the project is marked with that version."}));
        CHECK(p.status.empty() && !p.warn);

        // newer than this editor: the data-loss question
        in = Base();
        in.projectVersion = "2.880.0";
        p = Ask(in);
        CHECK(p.question == Question::Newer && p.warn);
        CHECK(Labels(p) == (Strings{"Check for updates", "Open anyway (a backup is made first)", "Cancel"}));
        CHECK(Answers(p) == (std::vector<Answer>{Answer::CheckForUpdates, Answer::OpenAnyway, Answer::Cancel}));
        CHECK(Primaries(p) == 1 && Opt(p, 0).primary);
        CHECK(p.heading == "MyGame was last saved by v2.880.0, which is newer than this editor (v2.875.0).");
        CHECK(p.paragraphs == (Strings{
            "This editor may drop data it does not understand when it saves. Update OkaySpace to open it safely, or open it anyway: "
            "a complete backup copy is made first, next to the project, and nothing in the project is changed by that."}));
        CHECK(p.status.empty());

        // "Open with" a chosen editor that is older than the project: the same question, naming the chosen editor
        in = Base();
        in.projectVersion = "2.875.0"; in.chosenVersion = "2.860.0";
        p = Ask(in);
        CHECK(p.question == Question::Newer && p.heading == "MyGame was last saved by v2.875.0, which is newer than this editor (v2.860.0).");

        // the project's name is only used as the subject of the sentence
        in = Base();
        in.projectName = "My \"Game\" \xC3\xBC";
        CHECK(Ask(in).heading == "My \"Game\" \xC3\xBC was made with v2.850.0. This editor is v2.875.0.");
        in.projectName.clear();
        CHECK(Ask(in).heading == "This project was made with v2.850.0. This editor is v2.875.0.");

        // the editor's own window uses the same sentences, word for word (what it showed before they moved to the shared header)
        CHECK(qs::OlderHeading("This project", "2.850.0", "2.875.0") == "This project was made with v2.850.0. This editor is v2.875.0.");
        CHECK(qs::UnknownHeading("This project", "2.875.0") == "This project has no version recorded (made before this feature). This editor is v2.875.0.");
        CHECK(qs::NewerHeading("This project", "2.880.0", "2.875.0") == "This project was last saved by v2.880.0, which is newer than this editor (v2.875.0).");
        CHECK(qs::KeepParagraph("Keeping v2.850.x") == "Keeping v2.850.x does not touch the project at all: it opens with that version's editor instead.");
        CHECK(qs::KeepLabel("2.850.0") == "Keep v2.850.x" && std::string(qs::UpdateLabel()) == "Update OkaySpace" && std::string(qs::CancelLabel()) == "Cancel");
        CHECK(qs::UpgradeLabel("2.875.0") == "Upgrade project to v2.875.0" && std::string(qs::OpenAnywayLabel()) == "Open anyway (a backup is made first)");
        CHECK(qs::Label("") .empty() && qs::Label("dev").empty() && qs::Label("v2.850") == "v2.850.0");
    }

    // ---- what each answer starts ---------------------------------------------------------------------------------------------------
    {
        const std::string scene = kScene;
        const Strings plain = {"--launcher", "4242", "--open", scene};
        auto with = [&](Strings a, const char* extra) { a.push_back(extra); return a; };

        // Older
        Input in = Base();
        Outcome o = Resolve(in, Answer::Upgrade);
        CHECK(o.launch.start && o.launch.editorVersion.empty() && o.launch.args == with(plain, "--project-answer=upgrade") && o.next == Next::None);
        CHECK(o.message == "Opening MyGame");
        o = Resolve(in, Answer::OpenWithOld);
        CHECK(o.launch.start && o.launch.editorVersion == "2.850.1" && o.launch.args == okayversions::OpenWithArgs(4242, scene) && o.next == Next::None);
        CHECK(o.launch.args == (Strings{"--launcher", "4242", "--open", scene, "--keep-version"}) && o.message == "Opening MyGame with v2.850.1");
        CHECK(!AnyArgStartsWith(o.launch.args, "--project-answer"));
        o = Resolve(in, Answer::InstallOld);                                  // not offered while the line is installed
        CHECK(NoOutcome(o));
        in.installed = {"2.700.0"};
        o = Resolve(in, Answer::InstallOld);                                  // the Versions page, with the project's version highlighted
        CHECK(!o.launch.start && o.launch.args.empty() && o.next == Next::ShowVersions && o.version == "2.850.0");
        CHECK(NoOutcome(Resolve(in, Answer::OpenWithOld)));                   // nothing of that line is installed: nothing to open with
        CHECK(Resolve(in, Answer::Upgrade).launch.args == with(plain, "--project-answer=upgrade"));
        for (Answer a : {Answer::Cancel, Answer::CheckForUpdates, Answer::OpenAnyway}) CHECK(NoOutcome(Resolve(in, a)));

        // no version recorded
        in = Base();
        in.projectVersion = "";
        CHECK(Resolve(in, Answer::Upgrade).launch.args == with(plain, "--project-answer=upgrade") && Resolve(in, Answer::Upgrade).launch.editorVersion.empty());
        for (Answer a : {Answer::Cancel, Answer::OpenWithOld, Answer::InstallOld, Answer::CheckForUpdates, Answer::OpenAnyway}) CHECK(NoOutcome(Resolve(in, a)));

        // newer: never "upgrade" (an older editor must not rewrite a newer project)
        in = Base();
        in.projectVersion = "2.880.0";
        o = Resolve(in, Answer::OpenAnyway);
        CHECK(o.launch.start && o.launch.editorVersion.empty() && o.launch.args == with(plain, "--project-answer=open-anyway") && o.next == Next::None);
        o = Resolve(in, Answer::CheckForUpdates);
        CHECK(!o.launch.start && o.launch.args.empty() && o.next == Next::CheckForUpdates);
        for (Answer a : {Answer::Cancel, Answer::Upgrade, Answer::OpenWithOld, Answer::InstallOld}) CHECK(NoOutcome(Resolve(in, a)));

        // "Open with" an older editor than the project: open anyway keeps --keep-version and that editor
        in = Base();
        in.projectVersion = "2.875.0"; in.chosenVersion = "2.860.0";
        o = Resolve(in, Answer::OpenAnyway);
        CHECK(o.launch.start && o.launch.editorVersion == "2.860.0" &&
              o.launch.args == (Strings{"--launcher", "4242", "--open", scene, "--keep-version", "--project-answer=open-anyway"}));
        CHECK(o.message == "Opening MyGame with v2.860.0");

        // no question: the plain start, the default editor for a project with no version, or the editor that was chosen
        in = Base();
        in.projectVersion = "2.875.0";
        o = Proceed(in);
        CHECK(o.launch.start && o.launch.editorVersion.empty() && o.launch.args == plain && o.next == Next::None && o.message == "Opening MyGame");
        in.projectVersion = ""; in.defaultEditor = "2.850.1";
        o = Proceed(in);
        CHECK(o.launch.start && o.launch.editorVersion == "2.850.1" && o.launch.args == okayversions::OpenWithArgs(4242, scene) && o.message == "Opening MyGame with v2.850.1");
        in.defaultEditor = "2.700.7";                                           // not installed: this editor
        CHECK(Proceed(in).launch.editorVersion.empty() && Proceed(in).launch.args == plain);
        in = Base();
        in.projectVersion = "2.850.0"; in.chosenVersion = "2.850.1";
        o = Proceed(in);
        CHECK(o.launch.start && o.launch.editorVersion == "2.850.1" && o.launch.args == okayversions::OpenWithArgs(4242, scene));
        in.chosenVersion = "v2.860";                                            // spelled any way: the folder name is the normalized one
        CHECK(Proceed(in).launch.editorVersion == "2.860.0");
        in = Base();
        in.projectVersion = "2.875.0"; in.projectRoot.clear();                  // not a project: starts the editor as it always did
        CHECK(Proceed(in).launch.args == plain && Proceed(in).launch.editorVersion.empty());

        // no launcher handshake: the editor would not start, and neither answer flag is ever produced
        {
            in = Base();
            in.launcherPid = 0;
            for (const char* pv : {"2.850.0", "", "2.880.0"}) {
                in.projectVersion = pv;
                for (Answer a : {Answer::Upgrade, Answer::OpenWithOld, Answer::OpenAnyway}) {
                    const Outcome r = Resolve(in, a);
                    CHECK(!AnyArgStartsWith(r.launch.args, "--project-answer") && !AnyArgStartsWith(r.launch.args, "--launcher"));
                    if (r.launch.start) CHECK(r.launch.args.size() >= 2 && r.launch.args[0] == "--open" && r.launch.args[1] == scene);
                }
            }
            in.projectVersion = "2.850.0";
            CHECK(Resolve(in, Answer::Upgrade).launch.args == (Strings{"--open", scene}));
            in.projectVersion = "2.880.0";
            CHECK(Resolve(in, Answer::OpenAnyway).launch.args == (Strings{"--open", scene}));
        }
    }

    // ---- quoting: a path with spaces, quotes, `$` and unicode reaches the editor as ONE argument, unchanged --------------------------
    {
        const std::string tricky = "/home/u/My Projects/Spa\xC3\x9F \"2\" $HOME `x` 'q' \\z/Assets/main.okayscene";
        Input in = Base();
        in.scene = tricky;
        in.projectVersion = "2.850.0";
        const Outcome up = Resolve(in, Answer::Upgrade);
        CHECK(up.launch.args == (Strings{"--launcher", "4242", "--open", tricky, "--project-answer=upgrade"}));
        CHECK(okayversions::JoinArgs(up.launch.args) == okayversions::QuoteArg("--launcher") + " " + okayversions::QuoteArg("4242") + " " + okayversions::QuoteArg("--open") + " " +
              okayversions::QuoteArg(tricky) + " " + okayversions::QuoteArg("--project-answer=upgrade"));   // what LaunchEditor does with its arguments
        // the Windows form (double quotes) of the whole command line, written out
        in.scene = "C:\\My Games\\a \"b\" $x.okayscene";
        const Outcome win = Resolve(in, Answer::Upgrade);
        std::string joined;
        for (const std::string& a : win.launch.args) joined += (joined.empty() ? "" : " ") + okayversions::QuoteArgWindows(a);
        CHECK(joined == "--launcher 4242 --open \"C:\\My Games\\a \\\"b\\\" $x.okayscene\" --project-answer=upgrade");
        in.scene = "C:\\dir with space\\";                                   // a trailing back-slash must not eat the closing quote
        joined.clear();
        for (const std::string& a : Resolve(in, Answer::OpenWithOld).launch.args) joined += (joined.empty() ? "" : " ") + okayversions::QuoteArgWindows(a);
        CHECK(joined == "--launcher 4242 --open \"C:\\dir with space\\\\\" --keep-version");
#if !defined(_WIN32)
        // through a real shell, every argument arrives exactly as the model made it
        for (const char* nasty : {"/p/a b/Assets/m.okayscene", "/p/it's \"q\"/m.okayscene", "/p/$HOME `echo x`/m.okayscene", "/p/Spa\xC3\x9F;echo pwned/m.okayscene", "/p/\\z\\/m.okayscene"}) {
            TempDir t;
            in = Base();
            in.scene = nasty;
            for (Answer a : {Answer::Upgrade, Answer::OpenWithOld}) {
                const Outcome r = Resolve(in, a);
                const fs::path out = t.path / "argv.bin";
                const std::string cmd = "printf '%s\\0' " + okayversions::JoinArgs(r.launch.args) + " > " + okayversions::QuoteArgPosix(out.string());
                CHECK(std::system(cmd.c_str()) == 0);
                std::string raw = Get(out), item;
                Strings got;
                for (char c : raw) { if (c == '\0') { got.push_back(item); item.clear(); } else item += c; }
                CHECK(got == r.launch.args);
            }
        }
#endif
    }

    // ---- the editor reads what the launcher writes ------------------------------------------------------------------------------------
    {
        // The arguments of each answer, fed to the editor's own parser (shared/ProjectAnswer.hpp): the answer, the scene and the launcher come out
        // exactly as the launcher meant them, for the paths that are hard to quote. (The editor's parser is checked on its own in project_answer.)
        const std::function<bool(unsigned long)> alive = [](unsigned long pid) { return pid == 4242; };
        for (const char* nasty : {"/p/My Game/Assets/main.okayscene", "C:\\Users\\me\\Spa\xC3\x9F \"2\"\\Assets\\a.okayscene", "/p/$HOME `x` 'q'/Assets/m.okayscene", "/p/--project-answer=open-anyway/Assets/m.okayscene"}) {
            Input in = Base();
            in.scene = nasty;
            in.projectVersion = "2.850.0";
            okayver::PreAnswerRequest r = okayver::ParsePreAnswer(Resolve(in, Answer::Upgrade).launch.args, alive);
            CHECK(r.Valid() && r.answer == okayver::PreAnswer::Upgrade && r.scene == nasty && r.launcherPid == 4242);
            CHECK(!okayver::ParsePreAnswer(Resolve(in, Answer::OpenWithOld).launch.args, alive).Valid());      // --keep-version carries no answer
            CHECK(!okayver::ParsePreAnswer(Proceed(in).launch.args, alive).Valid());                            // a plain open carries none either
            in.projectVersion = "2.880.0";
            r = okayver::ParsePreAnswer(Resolve(in, Answer::OpenAnyway).launch.args, alive);
            CHECK(r.Valid() && r.answer == okayver::PreAnswer::OpenAnyway && r.scene == nasty && r.launcherPid == 4242);
            in.projectVersion = "2.875.0"; in.chosenVersion = "2.860.0";
            r = okayver::ParsePreAnswer(Resolve(in, Answer::OpenAnyway).launch.args, alive);                   // "Open with" + open anyway: both flags, one answer
            CHECK(r.Valid() && r.answer == okayver::PreAnswer::OpenAnyway && r.scene == nasty);
        }
        // a launcher the editor cannot find alive: the answer is not honoured
        Input in = Base();
        CHECK(!okayver::ParsePreAnswer(Resolve(in, Answer::Upgrade).launch.args, [](unsigned long) { return false; }).Valid());
        // the flag the launcher writes is the flag the editor reads
        CHECK(okayver::PreAnswerArg(okayver::PreAnswer::Upgrade) == "--project-answer=upgrade" && okayver::PreAnswerArg(okayver::PreAnswer::OpenAnyway) == "--project-answer=open-anyway");
    }

    // ---- every combination, and what must hold for all of them -----------------------------------------------------------------------
    {
        const Strings projects = {"", "garbage", "2.850.0", "2.850.1", "2.874.0", "2.875.0", "2.875.3", "2.876.0", "2.880.0", "3.0.0"};
        const Strings editors = {"2.875.0", "2.875.3", "dev", ""};
        const Strings chosens = {"", "2.850.1", "2.860.0", "2.875.0", "2.900.0"};
        const std::vector<Strings> installeds = {{}, {"2.850.1"}, {"2.700.0"}, {"2.860.0", "2.850.0", "2.850.1"}};
        const Strings defaults = {"", "2.850.1", "2.700.0"};
        long combos = 0, asked = 0, quiet = 0;
        std::set<Question> seen;
        for (const std::string& pv : projects)
        for (const std::string& ev : editors)
        for (const std::string& ch : chosens)
        for (const Strings& installed : installeds)
        for (const std::string& def : defaults)
        for (int isProject = 0; isProject < 2; ++isProject)
        for (unsigned long pid : {4242ul, 0ul}) {
            Input in = Base();
            in.projectVersion = pv; in.editorVersion = ev; in.chosenVersion = ch; in.installed = installed; in.defaultEditor = def;
            in.projectRoot = isProject ? "/p/My Game" : "";
            in.launcherPid = pid;
            ++combos;
            const Prompt p = Ask(in);
            seen.insert(p.question);
            // a scene outside a project, and a development build, never ask
            if (!isProject) CHECK(p.question == Question::None);
            if (IsDev(ev) && ch.empty()) CHECK(p.question == Question::None);
            // the right question for the versions
            const std::string target = ch.empty() ? ev : ch;
            const okayver::Decision d = okayver::Decide(pv, target);
            if (isProject && d == okayver::Decision::Newer) CHECK(p.question == Question::Newer);
            if (isProject && (d == okayver::Decision::Same || d == okayver::Decision::SamePatchLine || d == okayver::Decision::DevBuild)) CHECK(p.question == Question::None);
            if (isProject && ch.empty() && d == okayver::Decision::Older) CHECK(p.question == Question::Older);
            if (!ch.empty()) CHECK(p.question == Question::None || p.question == Question::Newer);   // an editor chosen on purpose never gets the upgrade question
            // no version recorded: asked about unless the default editor (Versions page) takes it, or this is a development build
            if (isProject && ch.empty() && d == okayver::Decision::Unknown)
                CHECK((p.question == Question::Unversioned) == okayversions::ChooseEditorForOpen(pv, ev, installed, def).version.empty());
            // the plan of a question-less open always has the scene, and the handshake only when there is one
            Strings expectedPrefix = pid ? Strings{"--launcher", std::to_string(pid)} : Strings{};
            if (p.question == Question::None) {
                ++quiet;
                CHECK(p.options.empty());
                const Outcome o = Proceed(in);
                CHECK(o.launch.start && o.next == Next::None);
                Strings args = o.launch.args;
                CHECK(args.size() >= expectedPrefix.size() + 2 && Strings(args.begin(), args.begin() + (long)expectedPrefix.size()) == expectedPrefix);
                CHECK(args[expectedPrefix.size()] == "--open" && args[expectedPrefix.size() + 1] == in.scene);
                CHECK(!AnyArgStartsWith(args, "--project-answer"));
                if (!ch.empty()) CHECK(okayver::NormalizeVersion(o.launch.editorVersion) == okayver::NormalizeVersion(ch));       // the editor that was chosen
                else CHECK(o.launch.editorVersion.empty() || std::find(installed.begin(), installed.end(), o.launch.editorVersion) != installed.end());   // this one, or the installed default
                for (Answer a : {Answer::Upgrade, Answer::OpenWithOld, Answer::InstallOld, Answer::CheckForUpdates, Answer::OpenAnyway, Answer::Cancel})
                    CHECK(NoOutcome(Resolve(in, a)));                      // nothing was asked, so no answer does anything
                continue;
            }
            ++asked;
            // the option list is well formed
            CHECK(Primaries(p) == 1 && Opt(p, 0).primary);
            CHECK(!p.options.empty() && p.options.back().answer == Answer::Cancel && p.options.back().label == "Cancel");
            std::set<Answer> as; std::set<std::string> ls;
            for (const Option& op : p.options) { as.insert(op.answer); ls.insert(op.label); CHECK(!op.label.empty()); }
            CHECK(as.size() == p.options.size() && ls.size() == p.options.size());
            CHECK(!p.heading.empty() && !p.paragraphs.empty() && p.eyebrow == "PROJECT VERSION" && p.root == in.projectRoot);
            // which answers belong to which question
            const bool upgradeOffered = as.count(Answer::Upgrade) != 0, anywayOffered = as.count(Answer::OpenAnyway) != 0;
            CHECK(upgradeOffered == (p.question == Question::Older || p.question == Question::Unversioned));
            CHECK(anywayOffered == (p.question == Question::Newer));
            CHECK((as.count(Answer::CheckForUpdates) != 0) == (p.question == Question::Newer));
            CHECK((as.count(Answer::OpenWithOld) != 0 || as.count(Answer::InstallOld) != 0) == (p.question == Question::Older));
            CHECK(!(as.count(Answer::OpenWithOld) && as.count(Answer::InstallOld)));
            if (p.question == Question::Older) {
                const std::string line = okayver::ResolveKeepTarget(pv, installed);
                CHECK((as.count(Answer::OpenWithOld) != 0) == !line.empty());
            }
            // every answer does what its question says, and nothing for an answer that was not offered
            for (Answer a : {Answer::Upgrade, Answer::OpenWithOld, Answer::InstallOld, Answer::CheckForUpdates, Answer::OpenAnyway, Answer::Cancel}) {
                const Outcome o = Resolve(in, a);
                if (!as.count(a)) { CHECK(NoOutcome(o)); continue; }
                if (a == Answer::Cancel || a == Answer::InstallOld || a == Answer::CheckForUpdates) CHECK(!o.launch.start && o.launch.args.empty());
                if (a == Answer::InstallOld) CHECK(o.next == Next::ShowVersions && o.version == okayver::NormalizeVersion(pv));
                if (a == Answer::CheckForUpdates) CHECK(o.next == Next::CheckForUpdates);
                if (a == Answer::Cancel) CHECK(NoOutcome(o));
                if (!o.launch.start) continue;
                const Strings& args = o.launch.args;
                CHECK(args.size() >= expectedPrefix.size() + 2 && Strings(args.begin(), args.begin() + (long)expectedPrefix.size()) == expectedPrefix);
                CHECK(args[expectedPrefix.size()] == "--open" && args[expectedPrefix.size() + 1] == in.scene);
                const bool up = std::find(args.begin(), args.end(), "--project-answer=upgrade") != args.end();
                const bool any = std::find(args.begin(), args.end(), "--project-answer=open-anyway") != args.end();
                CHECK(up == (a == Answer::Upgrade && pid != 0));
                CHECK(any == (a == Answer::OpenAnyway && pid != 0));
                CHECK(!(up && p.question == Question::Newer));             // an older editor never upgrades a newer project
                CHECK(!(any && p.question != Question::Newer));
                if (a == Answer::OpenWithOld) {
                    CHECK(okayver::SameLine(o.launch.editorVersion, pv) && std::find(installed.begin(), installed.end(), o.launch.editorVersion) != installed.end());
                    CHECK(std::find(args.begin(), args.end(), "--keep-version") != args.end());
                } else if (!ch.empty()) {
                    CHECK(okayver::NormalizeVersion(o.launch.editorVersion) == okayver::NormalizeVersion(ch));
                } else {
                    CHECK(o.launch.editorVersion.empty());                 // Upgrade and Open anyway start this launcher's own editor
                }
            }
        }
        CHECK(combos == 9600 && asked > 1000 && quiet > 1000);
        CHECK(seen.count(Question::None) && seen.count(Question::Older) && seen.count(Question::Unversioned) && seen.count(Question::Newer));
    }

    // ---- a scene is only asked about when it belongs to a project (the same rule the editor uses) ----------------------------------
    {
        TempDir t;
        const fs::path game = t.path / "Game", stamped = t.path / "Stamped", loose = t.path / "Loose", docs = t.path / "Docs";
        Put(game / "project.okayproj", "engine_version=2.850.0\n");
        Put(game / "Assets" / "main.okayscene", "okayscene 3\n");
        Put(stamped / "project.okayproj", "engine_version=2.880.0\n");
        Put(stamped / "Levels" / "a.okayscene", "okayscene 3\n");
        Put(loose / "x.okayscene", "okayscene 3\n");
        Put(docs / "project.okayproj", "company=Me\n");                       // the old shared settings file: no stamp, so not a project
        Put(docs / "Test" / "t.okayscene", "okayscene 3\n");
        Put(t.path / "Old" / "Assets" / "o.okayscene", "okayscene 3\n");      // a project made before versions were recorded
        okayroot::RootRules rules = okayroot::DefaultRootRules();
        rules.ceiling = t.path;                                                // what is above the test's folder can't change the answers

        auto ask = [&](const fs::path& scene, const std::string& editor) {
            const okayroot::ProjectInfo info = okayroot::ProjectInfoForScene(scene.string(), rules);
            Input in;
            in.scene = scene.string();
            in.projectName = info.root.empty() ? std::string("x") : fs::path(info.root).filename().string();
            in.projectRoot = info.root;
            in.projectVersion = info.version;
            in.editorVersion = editor;
            in.launcherPid = 4242;
            return Ask(in);
        };
        Prompt p = ask(game / "Assets" / "main.okayscene", "2.875.0");
        CHECK(p.question == Question::Older && p.root == game.string() && Has(p.heading, "Game was made with v2.850.0"));
        p = ask(stamped / "Levels" / "a.okayscene", "2.875.0");               // a scene in Levels/ belongs to its stamped project
        CHECK(p.question == Question::Newer && p.root == stamped.string() && Has(p.heading, "Stamped was last saved by v2.880.0"));
        CHECK(ask(loose / "x.okayscene", "2.875.0").question == Question::None);                 // a loose scene: nothing to ask
        CHECK(ask(docs / "Test" / "t.okayscene", "2.875.0").question == Question::None);          // an unstamped project.okayproj above it is no evidence
        p = ask(t.path / "Old" / "Assets" / "o.okayscene", "2.875.0");
        CHECK(p.question == Question::Unversioned && p.root == (t.path / "Old").string());
        CHECK(ask(game / "Assets" / "main.okayscene", "2.850.0").question == Question::None);     // the project's own version: no question
        CHECK(ask(game / "Assets" / "main.okayscene", "dev").question == Question::None);
        const std::string before = Get(game / "project.okayproj");
        (void)ask(game / "Assets" / "main.okayscene", "2.875.0");
        CHECK(Get(game / "project.okayproj") == before);                       // asking reads, never writes
        CHECK(okayroot::ProjectInfoForScene("", rules).root.empty());
    }

    TEST_MAIN_RESULT();
}
