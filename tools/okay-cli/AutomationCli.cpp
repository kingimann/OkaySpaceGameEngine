// Automation commands that are okay command-line commands: the project, its files and assets, checks and builds
// (project.*, scene.list / scene.check, script.check, vs.list / vs.check, asset.*, build.*, cli.run). Each one runs the
// command line's own code in this process (okaycli::Main, the same as the editor's Command Line panel) with its --json
// output, and hands that back as the command's data, so the files are found, imported and checked exactly as `okay`
// and the editor do; nothing here reads a project file itself. Our own code.
#include "Cli.hpp"
#include "CliInternal.hpp"

#include "okay/Automation/Automation.hpp"

#include <algorithm>

namespace okaycli {

using okay::automation::Call;
using okay::automation::Command;
using okay::automation::Param;
using okay::automation::ParamType;
using okay::automation::Registry;
using okay::automation::Result;
using okay::json::Value;
namespace code = okay::automation::code;
namespace flag = okay::automation::flag;

namespace {

Value Lines(const std::string& text) {
    Value a = Value::Arr();
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        if (e > at) a.Push(Value::Str(text.substr(at, e - at)));
        at = e + 1;
    }
    return a;
}

/// Run `okay --project <dir> <args>` in this process and turn what it printed into the command's answer: its JSON when it
/// printed JSON, else its lines. `problemsOk`: exit code 1 means "the check found problems", still an answer.
Result RunCli(Call& c, std::vector<std::string> args, bool problemsOk) {
    const std::string project = c.host().projectDir();
    if (project.empty()) return Result::Fail(code::Unavailable, "No project is open (start okay mcp inside a project, or pass --project).");
    std::string out, err;
    Io io;
    io.out = &out;
    io.err = &err;
    args.insert(args.begin(), {"--project", project});
    int exitCode = 2;
    try {
        exitCode = Main(args, io);
    } catch (const Cancelled&) {
        return Result::Fail(code::Failed, "The command was stopped.");
    }
    Value data = Value::Obj();
    data.Set("exitCode", Value::Num(exitCode));
    Value parsed;
    const bool isJson = !out.empty() && (out[0] == '{' || out[0] == '[') && okay::json::Parse(out, parsed);
    data.Set("output", isJson ? parsed : Lines(out));
    if (!err.empty()) data.Set("messages", Lines(err));
    if (exitCode == 0 || (problemsOk && exitCode == 1)) return Result::Ok(std::move(data));
    std::string why = err.empty() ? out : err;
    while (!why.empty() && (why.back() == '\n' || why.back() == '\r')) why.pop_back();
    return Result::Fail(code::Failed, why.empty() ? "okay " + args[2] + " did not work (exit code " + std::to_string(exitCode) + ")." : why, std::move(data));
}

std::vector<std::string> Strings(Call& c, const char* name) {
    std::vector<std::string> v;
    if (const Value* a = c.Get(name)) for (const Value& x : a->array) if (x.IsString()) v.push_back(x.string);
    return v;
}

Command Cmd(const char* name, const char* summary, std::vector<Param> params, const char* result, unsigned flags,
            okay::automation::Handler h) {
    return okay::automation::MakeCommand(name, summary, std::move(params), result, flags, std::move(h), {code::Unavailable, code::Failed});
}

} // namespace

void AddAutomationCommands(Registry& r) {
    r.Add(Cmd("project.info", "The project's name, folder, engine version, game version and scenes (okay project info).",
              {}, "{exitCode, output: {name, folder, engineVersion, ...}}", 0, [](Call& c) { return RunCli(c, {"project", "info", "--json"}, false); }));
    r.Add(Cmd("project.settings", "List the project's settings (project.okayproj), read one, or change one (okay project settings).",
              {okay::automation::Arg("key", ParamType::String, "The setting to read or change."), okay::automation::Arg("value", ParamType::String, "Its new value (changes it).")},
              "{exitCode, output: [lines]}", flag::Mutates, [](Call& c) {
                  if (!c.Has("key")) return RunCli(c, {"project", "settings"}, false);
                  if (!c.Has("value")) return RunCli(c, {"project", "settings", "get", c.Str("key")}, false);
                  return RunCli(c, {"project", "settings", "set", c.Str("key"), c.Str("value")}, false);
              }));
    r.Add(Cmd("project.test", "Check that every scene and prefab of the project loads and has no merge clash marks (okay test).",
              {}, "{exitCode, output: [lines]}", 0, [](Call& c) { return RunCli(c, {"test"}, true); }));
    r.Add(Cmd("scene.list", "The project's scenes, the start scene marked (okay scene list).",
              {}, "{exitCode, output}", 0, [](Call& c) { return RunCli(c, {"scene", "list", "--json"}, false); }));
    r.Add(Cmd("scene.check", "Check scene files for problems: missing files, no camera, broken scripts (okay scene check; all scenes when none is named).",
              {okay::automation::Arg("scenes", ParamType::Array, "Scene files to check (default: every scene).")}, "{exitCode, output}", 0, [](Call& c) {
                  std::vector<std::string> a = {"scene", "check"};
                  for (const auto& s : Strings(c, "scenes")) a.push_back(s);
                  a.push_back("--json");
                  return RunCli(c, a, true);
              }));
    r.Add(Cmd("script.check", "Check OkayScript files for errors with line numbers (okay script check; all the project's when none is named).",
              {okay::automation::Arg("files", ParamType::Array, "Script files (default: every script).")}, "{exitCode, output: {files: [{file, problems}]}}", 0, [](Call& c) {
                  std::vector<std::string> a = {"script", "check"};
                  for (const auto& s : Strings(c, "files")) a.push_back(s);
                  a.push_back("--json");
                  return RunCli(c, a, true);
              }));
    r.Add(Cmd("vs.list", "The project's visual script graphs (okay vs list).", {}, "{exitCode, output}", 0,
              [](Call& c) { return RunCli(c, {"vs", "list", "--json"}, false); }));
    r.Add(Cmd("vs.check", "Check graph files as the editor's Problems button does (okay vs check; all graphs when none is named).",
              {okay::automation::Arg("graphs", ParamType::Array, "Graph files (default: every graph).")}, "{exitCode, output}", 0, [](Call& c) {
                  std::vector<std::string> a = {"vs", "check"};
                  for (const auto& s : Strings(c, "graphs")) a.push_back(s);
                  a.push_back("--json");
                  return RunCli(c, a, true);
              }));
    r.Add(Cmd("asset.list", "Every asset of the project, or one type: texture, model, audio, scene, prefab ... (okay asset list).",
              {okay::automation::Arg("type", ParamType::String, "Only this type.")}, "{exitCode, output}", 0, [](Call& c) {
                  std::vector<std::string> a = {"asset", "list"};
                  if (c.Has("type")) { a.push_back("--type"); a.push_back(c.Str("type")); }
                  a.push_back("--json");
                  return RunCli(c, a, false);
              }));
    r.Add(Cmd("asset.find", "Assets whose file name has these words in it (okay asset find).", {okay::automation::Arg("name", ParamType::String, "The words.", true)},
              "{exitCode, output}", 0, [](Call& c) { return RunCli(c, {"asset", "find", c.Str("name"), "--json"}, false); }));
    r.Add(Cmd("asset.used_by", "Which assets use a file (okay asset used-by).", {okay::automation::Arg("file", ParamType::String, "The file.", true)},
              "{exitCode, output}", 0, [](Call& c) { return RunCli(c, {"asset", "used-by", c.Str("file"), "--json"}, false); }));
    r.Add(Cmd("asset.uses", "Which files an asset uses (okay asset uses).", {okay::automation::Arg("file", ParamType::String, "The asset.", true)},
              "{exitCode, output}", 0, [](Call& c) { return RunCli(c, {"asset", "uses", c.Str("file"), "--json"}, false); }));
    r.Add(Cmd("asset.import", "Copy a file into the project's Assets under a free name with its .okaymeta, as the Project panel's import does (okay asset import).",
              {okay::automation::Arg("file", ParamType::String, "The file to bring in.", true), okay::automation::Arg("to", ParamType::String, "A folder inside Assets.")},
              "{exitCode, output}", flag::Mutates, [](Call& c) {
                  std::vector<std::string> a = {"asset", "import", c.Str("file")};
                  if (c.Has("to")) { a.push_back("--to"); a.push_back(c.Str("to")); }
                  return RunCli(c, a, false);
              }));
    r.Add(Cmd("build.targets", "The platforms Build Settings knows and which ones can be built on this computer (okay build --list-targets).",
              {}, "{exitCode, output}", 0, [](Call& c) { return RunCli(c, {"build", "--list-targets"}, false); }));
    r.Add(Cmd("build.run", "Build the game with the project's Build Settings (okay build: the editor's Build without a window). dryRun (the default) only says what would be built.",
              {okay::automation::Arg("target", ParamType::String, "The platform (default: this computer's)."), okay::automation::Arg("scene", ParamType::String, "Build only this start scene."),
               okay::automation::Arg("out", ParamType::String, "Where the game goes (default: <project>/build)."), okay::automation::Arg("name", ParamType::String, "The game's name."),
               okay::automation::Arg("zip", ParamType::Boolean, "Also make a .zip to hand out."), okay::automation::Arg("dev", ParamType::Boolean, "A development build."),
               okay::automation::Arg("dryRun", ParamType::Boolean, "Only say what would be built (default true).")},
              "{exitCode, output: [lines]}", flag::Mutates | flag::Slow, [](Call& c) {
                  std::vector<std::string> a = {"build"};
                  for (const char* k : {"target", "scene", "out", "name"}) if (c.Has(k)) { a.push_back(std::string("--") + k); a.push_back(c.Str(k)); }
                  if (c.Bool("zip", false)) a.push_back("--zip");
                  if (c.Bool("dev", false)) a.push_back("--dev");
                  if (c.Bool("dryRun", true)) a.push_back("--dry-run");
                  return RunCli(c, a, false);
              }));
    r.Add(Cmd("cli.run", "Run any okay command line in this project and get what it printed (okay help lists them: vc status, scene diff, script run ...). Commands that open a window (open, play) are refused.",
              {okay::automation::Arg("args", ParamType::Array, "The words after okay, such as [\"vc\", \"status\", \"--json\"].", true)},
              "{exitCode, output, messages}", flag::Mutates | flag::Slow, [](Call& c) {
                  std::vector<std::string> a = Strings(c, "args");
                  if (a.empty()) return Result::Fail(code::InvalidParams, "Give the command's words, such as [\"vc\", \"status\"].");
                  static const char* kRefused[] = {"open", "play", "mcp", "automation", "--project", "-C"};
                  for (const char* k : kRefused)
                      if (a[0] == k) return Result::Fail(code::Refused, std::string("okay ") + a[0] + " cannot run here (it opens a window, waits for it, or would run this server inside itself).");
                  return RunCli(c, a, true);
              }));
}

} // namespace okaycli
