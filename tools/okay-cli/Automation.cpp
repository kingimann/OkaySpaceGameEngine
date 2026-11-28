// okay mcp / okay automation: the engine's automation commands (okay/Automation/Automation.hpp) from outside.
//   okay mcp [--scene S] [--editor | --headless]
//       An MCP server on stdin / stdout (one JSON-RPC message per line), for an AI agent that starts it. With a running
//       editor whose View > Automation Link is on, it passes every message to that editor (the agent works on what the
//       person sees); otherwise, or with --headless, it loads the project with no window and serves it itself.
//   okay automation list [prefix] [--json] | describe <command> | call <command> [JSON] [--scene S] |
//                   run <file.jsonl | -> [--scene S] | link [--seconds N]
//       The same commands for scripts and tests: list them, describe one, run one, run a list of them in order, or keep
//       a windowless host with the automation link on (okay mcp then talks to it as to a running editor).
// The headless host is the editor's own EditorState with no window (editor/AutomationEditor.hpp: EditorHost), so a
// command does here exactly what it does in the editor. This file is built into the okay program only (it needs the
// editor's state, which the editor itself links once); okay_cli_lib reaches it through AutomationEntry(). Our own code.
#include "Cli.hpp"
#include "CliInternal.hpp"

#include "AutomationEditor.hpp"   // editor/: EditorHost and the editor's commands
#include "okay/Automation/Link.hpp"
#include "okay/Automation/Mcp.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Platform/Account/AccountDetail.hpp"   // detail::NapMs: a short sleep without <thread> (rule 21a)

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace okaycli {

void AddAutomationCommands(okay::automation::Registry& r);   // AutomationCli.cpp

namespace {

using okay::json::Value;

/// The project and scene loaded into a windowless EditorState.
struct Headless {
    okay::editor::EditorState ed;
    std::unique_ptr<oked::automation::EditorHost> host;
    std::string note;
};

bool OpenHeadless(Headless& h, const std::string& project, const std::string& scene, const Io& io, bool loadScene = true) {
    okay::automation::InstallLogRing();
    oked::automation::RegisterEditorCommands();
    okay::automation::AddProvider("cli", &AddAutomationCommands);
    const std::string root = FindProjectRoot(project.empty() ? "." : project);
    if (!root.empty()) h.ed.setProjectDir(root);
    std::string start = loadScene ? scene : std::string();
    if (loadScene && start.empty() && !root.empty()) start = MainScene(root);
    if (!start.empty()) {
        std::string path = start;
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::u8path(path), ec) && !root.empty()) {
            for (const char* sub : {"/", "/Assets/"})
                if (std::filesystem::exists(std::filesystem::u8path(root + sub + start), ec)) { path = root + sub + start; break; }
        }
        std::string err;
        if (!h.ed.Load(path, &err)) { io.Warn("The scene " + start + " could not be opened: " + err); return false; }
    }
    h.host = std::make_unique<oked::automation::EditorHost>(h.ed, "headless");
    h.note = root.empty() ? std::string("no project") : "project " + root;
    if (!h.ed.path().empty()) h.note += ", scene " + h.ed.path();
    return true;
}

/// While it lives the engine log goes to the log ring only: stdout carries the protocol or the JSON answer.
struct LogOffConsole {
    bool was;
    LogOffConsole() : was(okay::Log::console) { okay::Log::console = false; }
    ~LogOffConsole() { okay::Log::console = was; }
};

/// True when `line` is a JSON-RPC message the other side answers (a request, or a batch with one; text that is not JSON
/// is answered with a parse error).
bool ExpectsAnswer(const std::string& line) {
    Value v;
    if (!okay::json::Parse(line, v)) return true;
    auto one = [](const Value& m) { return m.IsObject() && m.Get("method") && m.Get("id"); };
    if (v.IsArray()) { for (const Value& m : v.array) if (one(m)) return true; return v.array.empty(); }
    if (v.IsObject() && !v.Get("method")) return !(v.Get("result") || v.Get("error"));   // an invalid request is answered
    return one(v);
}

/// The ids a message asked about, for the errors a lost link answers with.
std::vector<Value> RequestIds(const std::string& line) {
    std::vector<Value> ids;
    Value v;
    if (!okay::json::Parse(line, v)) return ids;
    auto one = [&](const Value& m) { if (m.IsObject() && m.Get("method") && m.Get("id")) ids.push_back(*m.Get("id")); };
    if (v.IsArray()) for (const Value& m : v.array) one(m); else one(v);
    return ids;
}

/// okay mcp in front of a running editor: every line from stdin goes to the editor, every answer to stdout.
int Bridge(okay::automation::LinkClient& link) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        const bool answer = ExpectsAnswer(line);
        bool closed = false;
        std::string reply;
        if (link.Send(line) && (!answer || link.Receive(reply, -1, {}, &closed))) {
            if (answer) { std::cout << reply << '\n'; std::cout.flush(); }
            continue;
        }
        // The editor went away: answer what was asked, say so, and end (the agent starts okay mcp again).
        for (const Value& id : RequestIds(line)) {
            Value e = Value::Obj().Set("code", Value::Num(-32603)).Set("message", Value::Str("The editor closed its automation link."));
            std::cout << okay::json::Write(Value::Obj().Set("jsonrpc", Value::Str("2.0")).Set("id", id).Set("error", std::move(e))) << '\n';
        }
        std::cout.flush();
        std::cerr << "okay mcp: the editor closed its automation link.\n";
        return 1;
    }
    return 0;
}

int CmdMcp(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Value("scene", "S", "The scene to open (default: the project's start scene).");
    cl.Flag("editor", "Only talk to a running editor (fail when none has its automation link on).");
    cl.Flag("headless", "Never talk to a running editor: load the project with no window.");
    if (!detail::ParseArgs(cl, args, "mcp", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("mcp")); return 0; }
    if (cl.Has("editor") && cl.Has("headless")) { io.Warn("Choose --editor or --headless, not both."); return 2; }
    if (!cl.Has("headless")) {
        okay::automation::LinkInfo info;
        std::string why;
        if (okay::automation::ReadLinkInfo(okay::automation::LinkInfoPath(), info, &why)) {
            okay::automation::LinkClient link;
            if (link.Connect(info.port, info.token, &why)) {
                std::cerr << "okay mcp: talking to the running editor" << (info.project.empty() ? std::string() : " (project " + info.project + ")") << ".\n";
                return Bridge(link);
            }
        }
        if (cl.Has("editor")) { io.Warn("okay mcp --editor: " + why + " Turn on View > Automation Link in the editor."); return 1; }
    }
    LogOffConsole quiet;
    Headless h;
    if (!OpenHeadless(h, project, cl.Get("scene"), io)) return 1;
    okay::automation::McpOptions o;
    o.serverVersion = EngineVersion();
    okay::automation::McpServer server(okay::automation::Commands(), *h.host, o);
    std::cerr << "okay mcp: headless, " << h.note << ", " << okay::automation::Commands().Size() << " commands.\n";
    return okay::automation::ServeStdio(server, std::cin, std::cout);
}

int CmdAutomation(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    const std::string sub = args.empty() ? std::string() : args[0];
    const std::vector<std::string> rest(args.begin() + (args.empty() ? 0 : 1), args.end());
    okay::CommandLine cl;
    cl.Flag("json", "Print JSON.");
    cl.Value("scene", "S", "The scene to open first (default: the project's start scene).");
    cl.Value("seconds", "N", "link: stop after this many seconds (default: run until stopped).");
    if (sub.empty() || sub == "--help" || sub == "-h") { io.Say(HelpText("automation")); return sub.empty() ? 2 : 0; }
    if (!detail::ParseArgs(cl, rest, "automation " + sub, io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("automation " + sub)); return 0; }
    const std::vector<std::string>& words = cl.Positionals();
    LogOffConsole quiet;
    Headless h;
    // list and describe need no scene; call and run open the project's.
    const bool runs = sub == "call" || sub == "run" || sub == "link";
    if (!OpenHeadless(h, project, cl.Get("scene"), io, runs)) return 1;
    const okay::automation::Registry& reg = okay::automation::Commands();
    if (sub == "list") {
        const std::string prefix = words.empty() ? std::string() : words[0];
        if (cl.Has("json")) {
            Value all = Value::Arr();
            for (const auto* c : reg.List(prefix)) all.Push(okay::automation::Registry::Describe(*c));
            io.Say(okay::json::Write(Value::Obj().Set("count", Value::Num((double)all.array.size())).Set("commands", std::move(all))));
        } else {
            for (const auto* c : reg.List(prefix)) io.Say(detail::Pad(c->name, 26) + " " + c->summary);
            io.Say(std::to_string(reg.List(prefix).size()) + " commands. okay automation describe <command> gives its parameters.");
        }
        return 0;
    }
    if (sub == "describe") {
        if (words.empty()) { io.Warn("Name the command: okay automation describe scene.hierarchy"); return 2; }
        const auto* c = reg.Find(words[0]);
        if (!c) { io.Warn("There is no command called " + words[0] + " (okay automation list)."); return 1; }
        io.Say(okay::json::Write(okay::automation::Registry::Describe(*c)));
        return 0;
    }
    auto runOne = [&](const std::string& name, const Value& params) {
        const okay::automation::Result r = reg.Run(*h.host, name, params);
        io.Say(okay::json::Write(r.ToJson()));
        return r.ok;
    };
    if (sub == "call") {
        if (words.empty()) { io.Warn("Name the command: okay automation call scene.hierarchy '{\"depth\":1}'"); return 2; }
        Value params;
        if (words.size() > 1) {
            std::string why;
            if (!okay::json::Parse(words[1], params, &why)) { io.Warn("The parameters are not JSON: " + why); return 2; }
        }
        return runOne(words[0], params) ? 0 : 1;
    }
    if (sub == "run") {
        // One command per line: {"command": "...", "params": {...}}. Every answer is printed; the exit code is 1 when any failed.
        if (words.empty()) { io.Warn("Name a file of commands (one JSON object per line), or - for standard input."); return 2; }
        std::unique_ptr<std::istream> file;
        std::istream* in = &std::cin;
        if (words[0] != "-") {
            file = std::make_unique<std::ifstream>(std::filesystem::u8path(words[0]));
            if (!*file) { io.Warn("The file " + words[0] + " cannot be read."); return 1; }
            in = file.get();
        }
        bool all = true;
        std::string line;
        while (std::getline(*in, line)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            Value v;
            std::string why;
            const Value* name = nullptr;
            if (!okay::json::Parse(line, v, &why) || !(name = v.Get("command")) || !name->IsString()) {
                io.Say(okay::json::Write(okay::automation::Result::Fail(okay::automation::code::InvalidParams, "A line must be {\"command\": ..., \"params\": {...}}" + (why.empty() ? std::string() : ": " + why)).ToJson()));
                all = false;
                continue;
            }
            const Value* p = v.Get("params");
            if (!runOne(name->string, p ? *p : Value())) all = false;
        }
        return all ? 0 : 1;
    }
    if (sub == "link") {
        // A windowless host with the automation link on, as the editor's View > Automation Link: several okay mcp
        // processes (or agents) then share one scene. Runs until stopped, or for --seconds.
        const double seconds = cl.GetFloat("seconds", 0.0f);
        if (!oked::automation::LinkPoll(h.ed, true, "headless")) { io.Warn(oked::automation::LinkStatus()); return 1; }
        io.Say(okay::json::Write(Value::Obj().Set("link", Value::Str(okay::automation::LinkInfoPath())).Set("status", Value::Str(oked::automation::LinkStatus()))));
        const auto start = std::chrono::steady_clock::now();
        while (seconds <= 0.0 || std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds) {
            oked::automation::LinkPoll(h.ed, true, "headless");
            okay::account::detail::NapMs(2);
        }
        oked::automation::LinkShutdown();
        return 0;
    }
    io.Warn("okay automation has no " + sub + " (list, describe, call, run, link).");
    return 2;
}

} // namespace

int AutomationMain(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    if (args.empty()) return 2;
    if (detail::RunningInsideHost()) {
        io.Warn("okay " + args[0] + " runs from a terminal. Inside the editor, turn on View > Automation Link instead.");
        return 1;
    }
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    if (args[0] == "mcp") return CmdMcp(rest, project, io);
    return CmdAutomation(rest, project, io);
}

} // namespace okaycli
