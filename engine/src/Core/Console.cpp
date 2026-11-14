#include "okay/Core/Console.hpp"
#include "okay/Core/Cvar.hpp"
#include "okay/Core/Paths.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace okay {

namespace {
struct Entry { Console::Command fn; std::string help, usage; bool builtin = false; };
std::map<std::string, Entry>& Commands() { static std::map<std::string, Entry> c; return c; }
std::vector<std::string>& Hist() { static std::vector<std::string> h; return h; }
std::vector<std::string>& OutLines() { static std::vector<std::string> l; return l; }
std::size_t g_histLimit = 100;
const std::size_t kLineLimit = 500;
const int kMaxDepth = 32;     // Execute calls inside Execute calls (a command that runs a line, a script that runs itself)
int g_depth = 0;
bool g_builtins = false;

std::string Err(const std::string& s) { return "error: " + s; }

// "name = value", with a note when the new value only takes effect after a restart.
std::string Shown(const Cvar& c) {
    std::string s = c.Name() + " = " + c.AsString();
    if (c.RestartPending()) s += "  (takes effect after a restart)";
    return s;
}

std::string Describe(const Cvar& c) {
    std::string s = c.Name() + " = " + c.AsString() + "  [" + c.TypeName();
    if (c.HasRange()) {
        auto num = [&](double v) { std::string t = std::to_string(v);
                                   if (c.GetType() == Cvar::Type::Int) return std::to_string((long long)v);
                                   while (t.size() > 1 && t.back() == '0') t.pop_back();
                                   if (!t.empty() && t.back() == '.') t.pop_back();
                                   return t; };
        s += " " + num(c.Min()) + ".." + num(c.Max());
    }
    s += ", default " + c.DefaultString();
    if (c.ReadOnly()) s += ", read only";
    if (c.Cheat()) s += ", cheat";
    if (c.RestartRequired()) s += ", restart required";
    s += "]";
    if (!c.Description().empty()) s += " - " + c.Description();
    return s;
}

void EnsureBuiltins() {
    if (g_builtins) return;
    g_builtins = true;
    auto add = [](const char* name, Console::Command fn, const char* help, const char* usage) {
        Entry e; e.fn = std::move(fn); e.help = help; e.usage = usage; e.builtin = true;
        Commands()[name] = std::move(e);
    };
    add("help", [](const std::vector<std::string>& a) -> std::string {
        if (!a.empty()) {
            auto it = Commands().find(a[0]);
            if (it != Commands().end())
                return a[0] + (it->second.usage.empty() ? "" : "  usage: " + it->second.usage) +
                       (it->second.help.empty() ? "" : "\n  " + it->second.help);
            if (Cvar* c = Cvars::Find(a[0])) return Describe(*c);
            return Err("no command or variable named '" + a[0] + "'");
        }
        std::string out = "Commands:";
        for (auto& kv : Commands())
            out += "\n  " + kv.first + (kv.second.help.empty() ? "" : " - " + kv.second.help);
        out += "\nType 'list' to see the variables; 'help <name>' for details.";
        return out;
    }, "list commands, or describe one command or variable", "help [name]");
    add("list", [](const std::vector<std::string>& a) -> std::string {
        const std::string prefix = a.empty() ? "" : a[0];
        std::string out;
        for (Cvar* c : Cvars::All(prefix)) { if (!out.empty()) out += "\n"; out += Describe(*c); }
        return out.empty() ? "(no variables" + (prefix.empty() ? std::string() : " starting with '" + prefix + "'") + ")" : out;
    }, "list console variables (optionally those starting with a prefix)", "list [prefix]");
    add("set", [](const std::vector<std::string>& a) -> std::string {
        if (a.size() < 2) return Err("usage: set <name> <value>");
        std::string v = a[1]; for (std::size_t i = 2; i < a.size(); ++i) v += " " + a[i];
        std::string e;
        if (!Cvars::Set(a[0], v, &e)) return Err(e);
        return Shown(*Cvars::Find(a[0]));
    }, "set a variable", "set <name> <value>");
    add("get", [](const std::vector<std::string>& a) -> std::string {
        if (a.empty()) return Err("usage: get <name>");
        Cvar* c = Cvars::Find(a[0]);
        return c ? Describe(*c) : Err("unknown variable '" + a[0] + "'");
    }, "show a variable", "get <name>");
    add("toggle", [](const std::vector<std::string>& a) -> std::string {
        if (a.empty()) return Err("usage: toggle <name>");
        Cvar* c = Cvars::Find(a[0]);
        if (!c) return Err("unknown variable '" + a[0] + "'");
        if (c->GetType() != Cvar::Type::Bool) return Err(a[0] + " is not a bool");
        std::string why;
        if (!c->CanSetFromText(&why)) return Err(why);
        c->SetBool(!c->AsBool());
        return Shown(*c);
    }, "flip a bool variable", "toggle <name>");
    add("reset", [](const std::vector<std::string>& a) -> std::string {
        if (a.empty()) return Err("usage: reset <name>");
        Cvar* c = Cvars::Find(a[0]);
        if (!c) return Err("unknown variable '" + a[0] + "'");
        std::string why;
        if (!c->CanSetFromText(&why)) return Err(why);
        c->Reset();
        return Shown(*c);
    }, "set a variable back to its default", "reset <name>");
    add("history", [](const std::vector<std::string>&) -> std::string {
        std::string out;
        for (std::size_t i = 0; i < Hist().size(); ++i) { if (i) out += "\n"; out += std::to_string(i + 1) + "  " + Hist()[i]; }
        return out;
    }, "show the lines you ran", "history");
    add("exec", [](const std::vector<std::string>& a) -> std::string {
        if (a.empty()) return Err("usage: exec <file>");
        std::string path = a[0];
        for (std::size_t i = 1; i < a.size(); ++i) path += " " + a[i];
        std::ifstream in(Paths::Resolve(path), std::ios::binary);
        if (!in) in.open(path, std::ios::binary);               // also as given (the working directory)
        if (!in) return Err("cannot read '" + path + "'");
        std::ostringstream buf; buf << in.rdbuf();
        const std::string text = buf.str();
        int lines = 0, failed = 0;
        std::size_t pos = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;   // a byte order mark from a text editor
        while (pos <= text.size()) {
            const std::size_t nl = text.find('\n', pos);
            const std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            const std::size_t first = line.find_first_not_of(" \t\r");
            const bool blank = first == std::string::npos || line[first] == '#' || line.compare(first, 2, "//") == 0;
            const Console::Result r = Console::Execute(line);
            if (!blank) { ++lines; if (!r.ok) ++failed; }   // blank lines and comments are not counted
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        return "ran " + std::to_string(lines) + " line" + (lines == 1 ? "" : "s") +
               (failed ? " (" + std::to_string(failed) + " failed)" : std::string());
    }, "run the lines of a script file (an autoexec); relative paths are under the asset folder", "exec <file>");
    add("echo", [](const std::vector<std::string>& a) -> std::string {
        std::string out;
        for (std::size_t i = 0; i < a.size(); ++i) { if (i) out += " "; out += a[i]; }
        return out;
    }, "print text", "echo <text>");
}

void Trim(std::vector<std::string>& v, std::size_t cap) {
    if (v.size() > cap) v.erase(v.begin(), v.begin() + (v.size() - cap));
}
}

void Console::Register(const std::string& name, Command fn, const std::string& help, const std::string& usage) {
    if (name.empty() || !fn) return;
    EnsureBuiltins();
    Entry e; e.fn = std::move(fn); e.help = help; e.usage = usage;
    Commands()[name] = std::move(e);
}
bool Console::Unregister(const std::string& name) { EnsureBuiltins(); return Commands().erase(name) > 0; }
bool Console::HasCommand(const std::string& name) { EnsureBuiltins(); return Commands().count(name) > 0; }

std::vector<std::string> Console::Tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur; bool inQuote = false, have = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuote) {
            if (c == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) cur += line[++i];
            else if (c == '"') inQuote = false;
            else cur += c;
        } else if (c == '"') { inQuote = true; have = true; }
        else if (c == ' ' || c == '\t') { if (have || !cur.empty()) { out.push_back(cur); cur.clear(); have = false; } }
        else cur += c;
    }
    if (have || !cur.empty()) out.push_back(cur);
    return out;
}

Console::Result Console::Execute(const std::string& rawLine) {
    EnsureBuiltins();
    Result r;
    struct Depth { Depth() { ++g_depth; } ~Depth() { --g_depth; } } depth;
    if (g_depth > kMaxDepth) {   // a command that runs itself would otherwise overflow the stack
        r.ok = false;
        r.output = Err("lines are nested too deeply (more than " + std::to_string(kMaxDepth) + ")");
        return r;
    }
    std::size_t a = rawLine.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return r;
    std::size_t b = rawLine.find_last_not_of(" \t\r\n");
    const std::string line = rawLine.substr(a, b - a + 1);
    if (line[0] == '#' || line.compare(0, 2, "//") == 0) return r;

    if (Hist().empty() || Hist().back() != line) { Hist().push_back(line); Trim(Hist(), g_histLimit); }

    std::vector<std::string> words = Tokenize(line);
    if (words.empty()) return r;
    const std::string name = words[0];
    words.erase(words.begin());

    auto it = Commands().find(name);
    if (it != Commands().end()) {
        Command fn = it->second.fn;   // copy: the command may Register/Unregister
        r.output = fn(words);
    } else if (Cvar* c = Cvars::Find(name)) {
        if (words.empty()) r.output = Describe(*c);
        else {
            std::string v = words[0]; for (std::size_t i = 1; i < words.size(); ++i) v += " " + words[i];
            std::string e;
            if (c->Set(v, &e)) r.output = Shown(*c);
            else r.output = Err(e);
        }
    } else {
        r.output = Err("unknown command '" + name + "' (type 'help')");
    }
    r.ok = r.output.compare(0, 6, "error:") != 0;
    if (!r.output.empty()) Print(r.output);
    return r;
}

std::vector<Console::Result> Console::ExecuteScript(const std::string& text) {
    std::vector<Result> out;
    std::size_t pos = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;   // a byte order mark from a text editor
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        const std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        out.push_back(Execute(line));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return out;
}

const std::vector<std::string>& Console::History() { return Hist(); }
void Console::SetHistoryLimit(std::size_t n) { g_histLimit = n ? n : 1; Trim(Hist(), g_histLimit); }
void Console::ClearHistory() { Hist().clear(); }

void Console::Print(const std::string& text) {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        OutLines().push_back(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    Trim(OutLines(), kLineLimit);
}
const std::vector<std::string>& Console::Lines() { return OutLines(); }
void Console::ClearLines() { OutLines().clear(); }

std::vector<std::string> Console::Complete(const std::string& prefix) {
    EnsureBuiltins();
    std::set<std::string> names;
    for (auto& kv : Commands()) if (kv.first.compare(0, prefix.size(), prefix) == 0) names.insert(kv.first);
    for (Cvar* c : Cvars::All(prefix)) names.insert(c->Name());
    return std::vector<std::string>(names.begin(), names.end());
}

void Console::Reset() { Commands().clear(); g_builtins = false; Hist().clear(); OutLines().clear(); g_histLimit = 100; }

} // namespace okay
