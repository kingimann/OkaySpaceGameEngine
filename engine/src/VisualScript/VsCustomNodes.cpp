// Custom visual-script nodes written in a script (okay/VisualScript/VsCustomNodes.hpp). Written for this engine from the
// owner's request (2026-11-28) and the engine's own script interfaces (IScriptVM, NodeGraph); no outside code.
#include "okay/VisualScript/VsCustomNodes.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/Nodes.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include "okay/Core/PathSafety.hpp"
#include "okay/Core/StringUtil.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Debug/DebugUtil.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace okay::vs {

namespace fs = std::filesystem;

namespace {

// ---- small text helpers (the shared ones: Core/StringUtil.hpp) --------------------------------------------------------
using str::LowerAscii;
using str::Trim;
std::vector<std::string> CommaList(const std::string& s) { return str::SplitTrimmed(s, ','); }
bool StartsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
std::vector<std::string> TextLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
    }
    return out;
}
bool IsIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool IsIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

/// A pin type from the word a script uses for it: the visual-script names (PinTypeFromName: Number, float, Text, string,
/// Vector3...) and a few more a script writes (double, vector, Transform, array, table, value). False when unknown.
bool PinTypeFromWord(const std::string& wordIn, PinType& out) {
    const std::string w = LowerAscii(Trim(wordIn));
    if (w == "double" || w == "num") { out = PinType::Number; return true; }
    if (w == "str") { out = PinType::Text; return true; }
    if (w == "vector") { out = PinType::Vec3; return true; }
    if (w == "transform") { out = PinType::Object; return true; }
    if (w == "array" || w == "table") { out = PinType::List; return true; }
    if (w == "dict") { out = PinType::Map; return true; }
    if (w == "value") { out = PinType::Any; return true; }
    return PinTypeFromName(w, out);
}

/// "Value: Number = 1" -> a pin. False with `why` for a bad one.
bool ParsePin(const std::string& textIn, VsPinSpec& pin, std::string& why) {
    std::string text = Trim(textIn);
    const std::size_t eq = text.find('=');
    if (eq != std::string::npos) {
        pin.def = Trim(text.substr(eq + 1));
        if (pin.def.size() >= 2 && pin.def.front() == '"' && pin.def.back() == '"') pin.def = pin.def.substr(1, pin.def.size() - 2);
        text = Trim(text.substr(0, eq));
    }
    const std::size_t colon = text.find(':');
    std::string type = "Any";
    if (colon != std::string::npos) { type = Trim(text.substr(colon + 1)); text = Trim(text.substr(0, colon)); }
    pin.name = text;
    if (pin.name.empty()) { why = "a pin needs a name (@in Name: Type)"; return false; }
    if (!PinTypeFromWord(type, pin.type)) {
        why = "'" + type + "' is not a pin type (Number, Bool, Text, Vector3, Object, List, Map, Color or Any)";
        return false;
    }
    return true;
}

/// A typed-in default ("1", "true", "1,2,3", "hello") as a value for a pin of `type`.
VsValue DefaultValue(const VsPinSpec& p) {
    const std::string& d = p.def;
    switch (p.type) {
        case PinType::Number: { float f = 0; return VsParseFinite(d, f) ? VsValue(f) : VsValue(0.0f); }
        case PinType::Bool: return VsValue(LowerAscii(d) == "true" || d == "1");
        case PinType::Text: return VsValue(d);
        case PinType::Vec3: {
            const std::vector<std::string> parts = CommaList(d);
            Vec3 v = Vec3::Zero;
            if (parts.size() != 3 || !VsParseFinite(parts[0], v.x) || !VsParseFinite(parts[1], v.y) || !VsParseFinite(parts[2], v.z))
                v = Vec3::Zero;
            return VsValue(v);
        }
        default: {
            if (d.empty()) return VsValue{};
            float f = 0;
            if (VsParseFinite(d, f)) return VsValue(f);
            if (d == "true" || d == "false") return VsValue(d == "true");
            return VsValue(d);
        }
    }
}

// ---- the OkayScript / Lua comment header ---------------------------------------------------------------------------------
// The function a header sits on: "function name(", "local function name(", "function T:name(" (Lua), "float name(" (C-style).
bool FunctionOnLine(const std::string& lineIn, bool lua, std::string& name, std::string& why) {
    std::string line = Trim(lineIn);
    if (lua && StartsWith(line, "local ")) {
        why = "a local function can't be called by the engine: drop `local`";
        return false;
    }
    std::size_t paren = line.find('(');
    if (paren == std::string::npos) { why = "a @node header must sit right above the function it describes"; return false; }
    std::string head = Trim(line.substr(0, paren));
    // The last word before '(' is the name (after "T:" in Lua's method form).
    std::size_t end = head.size();
    std::size_t start = end;
    while (start > 0 && IsIdentChar(head[start - 1])) --start;
    if (start == end || !IsIdentStart(head[start])) { why = "no function name before '('"; return false; }
    name = head.substr(start, end - start);
    const std::string before = Trim(head.substr(0, start));
    if (lua) {
        if (!StartsWith(before, "function")) { why = "a @node header must sit right above `function name(...)`"; return false; }
        if (!before.empty() && before.back() == '.') {
            why = "write `function T:" + name + "(...)` (with a colon) so the engine passes the table";
            return false;
        }
    } else if (before.empty()) {
        why = "a @node header must sit right above `function name(...) { ... }`";
        return false;
    }
    return true;
}

void ParseCommentHeaders(const std::string& text, bool lua, CustomNodeScript& out) {
    const std::vector<std::string> lines = TextLines(text);
    const std::string mark = lua ? "--" : "//";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string t = Trim(lines[i]);
        if (!StartsWith(t, mark) || Trim(t.substr(mark.size())).rfind("@node", 0) != 0) continue;
        CustomNodeDecl d;
        d.info.language = lua ? "lua" : "okayscript";
        d.info.source = out.path;
        bool flow = false, bad = false;
        std::string lastTag;
        const std::size_t first = i;
        std::size_t j = i;
        for (; j < lines.size(); ++j) {
            const std::string l = Trim(lines[j]);
            if (!StartsWith(l, mark)) break;
            std::string body = Trim(l.substr(mark.size()));
            if (body.empty() || body[0] != '@') {   // a plain comment line: more help text after @help
                if (lastTag == "help" && !body.empty()) d.info.help += (d.info.help.empty() ? "" : " ") + body;
                continue;
            }
            std::size_t sp = 1;
            while (sp < body.size() && !std::isspace((unsigned char)body[sp])) ++sp;
            const std::string tag = LowerAscii(body.substr(1, sp - 1));
            const std::string val = Trim(body.substr(sp));
            lastTag = tag;
            std::string why;
            auto problem = [&](const std::string& m) { out.problems.push_back("line " + std::to_string(j + 1) + ": " + m); bad = true; };
            if (tag == "node") d.info.title = val;
            else if (tag == "category") d.info.category = val.empty() ? "Custom" : val;
            else if (tag == "help") d.info.help += (d.info.help.empty() ? "" : " ") + val;
            else if (tag == "flow") flow = true;
            else if (tag == "exits") { d.info.exits = CommaList(val); flow = true; }
            else if (tag == "in" || tag == "out") {
                VsPinSpec p;
                if (!ParsePin(val, p, why)) problem(why);
                else (tag == "in" ? d.info.ins : d.info.outs).push_back(p);
            } else if (tag == "base") {
                const std::size_t s2 = val.find_first_of(" \t");
                d.info.base = Trim(val.substr(0, s2));
                d.baseArg = s2 == std::string::npos ? std::string() : Trim(val.substr(s2));
                if (d.baseArg.size() >= 2 && d.baseArg.front() == '"' && d.baseArg.back() == '"') d.baseArg = d.baseArg.substr(1, d.baseArg.size() - 2);
            } else problem("unknown tag '@" + tag + "' (use @node, @category, @help, @in, @out, @flow, @exits, @base)");
        }
        // The function: the next line that is not blank.
        while (j < lines.size() && Trim(lines[j]).empty()) ++j;
        std::string why;
        if (j >= lines.size() || !FunctionOnLine(lines[j], lua, d.function, why)) {
            out.problems.push_back("line " + std::to_string(first + 1) + ": " + (j >= lines.size() ? std::string("a @node header with no function after it") : why));
            i = j;
            continue;
        }
        d.info.line = (int)j + 1;
        if (d.info.title.empty()) d.info.title = d.function;
        d.info.execIn = flow;
        if (flow && d.info.exits.empty()) d.info.exits = {""};
        d.type = std::string(CustomNodePrefix()) + d.function;
        if (!bad) out.nodes.push_back(d);
        i = j;
    }
}

// ---- the C# attribute -------------------------------------------------------------------------------------------------
// Blank text out with spaces (newlines kept), so the VM's line and column numbers still match the file.
void Blank(std::string& s, std::size_t a, std::size_t b) {
    for (std::size_t k = a; k < b && k < s.size(); ++k) if (s[k] != '\n') s[k] = ' ';
}
// The index just past the ')' that closes the '(' at `open`, skipping strings. npos when unclosed.
std::size_t CloseParen(const std::string& s, std::size_t open) {
    int depth = 0;
    for (std::size_t k = open; k < s.size(); ++k) {
        const char c = s[k];
        if (c == '"') { for (++k; k < s.size() && s[k] != '"'; ++k) if (s[k] == '\\') ++k; continue; }
        if (c == '(') ++depth;
        else if (c == ')') { if (--depth == 0) return k + 1; }
    }
    return std::string::npos;
}
// The pieces between top-level commas.
std::vector<std::string> TopLevelArgs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (std::size_t k = 0; k < s.size(); ++k) {
        const char c = s[k];
        if (c == '"') {
            std::size_t e = k + 1;
            while (e < s.size() && s[e] != '"') { if (s[e] == '\\') ++e; ++e; }
            cur += s.substr(k, e + 1 - k);
            k = e;
            continue;
        }
        if (c == '(' || c == '<' || c == '[') ++depth;
        if (c == ')' || c == '>' || c == ']') --depth;
        if (c == ',' && depth == 0) { out.push_back(Trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    if (!Trim(cur).empty()) out.push_back(Trim(cur));
    return out;
}
std::string Unquote(const std::string& v) {
    std::string t = Trim(v);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        std::string o;
        for (std::size_t k = 1; k + 1 < t.size(); ++k) {
            if (t[k] == '\\' && k + 2 < t.size()) { ++k; o += t[k] == 'n' ? '\n' : t[k]; }
            else o += t[k];
        }
        return o;
    }
    return t;
}
// A C# literal as pin default text: "1.5f" -> "1.5", "\"hi\"" -> "hi", "new Vector3(1, 2, 3)" -> "1,2,3".
std::string CsLiteral(const std::string& litIn) {
    std::string lit = Trim(litIn);
    if (!lit.empty() && lit.front() == '"') return Unquote(lit);
    if (StartsWith(lit, "new Vector3(") && lit.back() == ')') {
        std::string inside = lit.substr(12, lit.size() - 13);
        std::string o;
        for (const std::string& p : TopLevelArgs(inside)) { if (!o.empty()) o += ","; o += CsLiteral(p); }
        return o;
    }
    if (!lit.empty() && (lit.back() == 'f' || lit.back() == 'F') && lit.size() > 1 && std::isdigit((unsigned char)lit[lit.size() - 2])) lit.pop_back();
    return lit;
}
// A pin label from a parameter name: "value" -> "Value".
std::string LabelFromName(std::string n) {
    if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
    return n;
}

void ParseCsAttributes(const std::string& text, CustomNodeScript& out) {
    std::string src = text;
    auto lineOf = [&](std::size_t at) { return 1 + (int)std::count(text.begin(), text.begin() + (long)std::min(at, text.size()), '\n'); };
    std::size_t at = 0;
    while ((at = text.find("[Node(", at)) != std::string::npos) {
        // Skip one in a // comment.
        const std::size_t ls = text.rfind('\n', at);
        const std::string before = text.substr(ls == std::string::npos ? 0 : ls + 1, at - (ls == std::string::npos ? 0 : ls + 1));
        if (before.find("//") != std::string::npos) { at += 6; continue; }
        const std::size_t open = at + 5;
        const std::size_t close = CloseParen(text, open);
        const int line = lineOf(at);
        auto problem = [&](const std::string& m) { out.problems.push_back("line " + std::to_string(line) + ": " + m); };
        if (close == std::string::npos) { problem("the [Node(...)] attribute is not closed"); break; }
        std::size_t rb = close;
        while (rb < text.size() && std::isspace((unsigned char)text[rb])) ++rb;
        if (rb >= text.size() || text[rb] != ']') { problem("the [Node(...)] attribute needs its ']'"); at = close; continue; }
        CustomNodeDecl d;
        d.info.language = "cs";
        d.info.source = out.path;
        bool flow = false, bad = false;
        std::vector<std::string> inputLabels;
        std::string outputLabel = "Result";
        const std::vector<std::string> args = TopLevelArgs(text.substr(open + 1, close - open - 2));
        for (std::size_t k = 0; k < args.size(); ++k) {
            const std::string& a = args[k];
            const std::size_t eq = a.find('=');
            if (eq == std::string::npos || (!a.empty() && a.front() == '"')) {
                if (k == 0) d.info.title = Unquote(a);
                else { problem("only the first value of [Node] can be unnamed (the title)"); bad = true; }
                continue;
            }
            const std::string key = LowerAscii(Trim(a.substr(0, eq)));
            const std::string val = Unquote(a.substr(eq + 1));
            if (key == "category") d.info.category = val.empty() ? "Custom" : val;
            else if (key == "help") d.info.help = val;
            else if (key == "flow") flow = LowerAscii(val) == "true";
            else if (key == "exits") { d.info.exits = CommaList(val); flow = true; }
            else if (key == "inputs") inputLabels = CommaList(val);
            else if (key == "output") outputLabel = val;
            else if (key == "base") {
                const std::size_t s2 = val.find_first_of(" \t");
                d.info.base = Trim(val.substr(0, s2));
                d.baseArg = s2 == std::string::npos ? std::string() : Trim(val.substr(s2));
            } else { problem("[Node] has no field '" + Trim(a.substr(0, eq)) + "' (Category, Help, Flow, Exits, Inputs, Output, Base)"); bad = true; }
        }
        Blank(src, at, rb + 1);
        // The method after it (other attributes may come between).
        std::size_t m = rb + 1;
        for (;;) {
            while (m < text.size() && std::isspace((unsigned char)text[m])) ++m;
            if (m < text.size() && text[m] == '[') { const std::size_t e = text.find(']', m); if (e == std::string::npos) break; m = e + 1; continue; }
            break;
        }
        const std::size_t pOpen = text.find('(', m);
        const std::size_t pClose = pOpen == std::string::npos ? std::string::npos : CloseParen(text, pOpen);
        if (pClose == std::string::npos) { problem("[Node] must sit right above a method"); at = rb + 1; continue; }
        std::vector<std::string> words;
        {
            std::istringstream ws(text.substr(m, pOpen - m));
            std::string w;
            while (ws >> w) words.push_back(w);
        }
        while (!words.empty() && (words.front() == "public" || words.front() == "private" || words.front() == "static")) words.erase(words.begin());
        if (words.size() != 2) { problem("[Node] must sit right above a method: `float Name(float value) { ... }`"); at = rb + 1; continue; }
        const std::string ret = words[0];
        d.function = words[1];
        d.info.line = lineOf(m + text.substr(m, pOpen - m).find(d.function));
        // Parameters: the inputs (one named `original` is the original node's result, not a pin). The typed language
        // has no default values yet, so `= literal` is blanked out of what the VM loads and the engine fills it in.
        {
            int depth = 0;
            for (std::size_t k = pOpen + 1; k + 1 < pClose; ++k) {
                const char c = text[k];
                if (c == '"') { for (++k; k + 1 < pClose && text[k] != '"'; ++k) if (text[k] == '\\') ++k; continue; }
                if (c == '(' || c == '<' || c == '[') ++depth;
                else if (c == ')' || c == '>' || c == ']') --depth;
                else if (c == '=' && depth == 0) {
                    std::size_t e = k + 1;
                    int d2 = 0;
                    for (; e + 1 < pClose; ++e) {
                        const char x = text[e];
                        if (x == '"') { for (++e; e + 1 < pClose && text[e] != '"'; ++e) if (text[e] == '\\') ++e; continue; }
                        if (x == '(' || x == '<' || x == '[') ++d2;
                        else if (x == ')' || x == '>' || x == ']') --d2;
                        else if (x == ',' && d2 == 0) break;
                    }
                    Blank(src, k, e);
                    k = e - 1;
                }
            }
        }
        const std::string inside = text.substr(pOpen + 1, pClose - pOpen - 2);
        std::size_t index = 0;
        for (const std::string& p : TopLevelArgs(inside)) {
            std::string decl = p, lit;
            const std::size_t eq = p.find('=');
            if (eq != std::string::npos) { decl = Trim(p.substr(0, eq)); lit = Trim(p.substr(eq + 1)); }
            std::istringstream ps(decl);
            std::string ptype, pname;
            ps >> ptype >> pname;
            if (pname == "original") continue;
            VsPinSpec pin;
            if (!PinTypeFromWord(ptype, pin.type) || pin.type == PinType::Any) {
                problem("parameter '" + pname + "': a C# node input is float, int, bool, string, Vector3 or GameObject");
                bad = true;
                continue;
            }
            pin.name = index < inputLabels.size() ? inputLabels[index] : LabelFromName(pname);
            pin.def = CsLiteral(lit);
            d.info.ins.push_back(pin);
            ++index;
        }
        if (!d.info.exits.empty()) {
            if (ret != "int" && ret != "string" && ret != "bool") { problem("a node with Exits returns the exit: int, string or bool"); bad = true; }
        } else if (ret == "void") {
            flow = true;
        } else {
            VsPinSpec o;
            if (!PinTypeFromWord(ret, o.type) || o.type == PinType::Any) {
                problem("return type '" + ret + "': a C# node returns float, int, bool, string, Vector3, GameObject or void");
                bad = true;
            }
            o.name = outputLabel;
            d.info.outs.push_back(o);
        }
        d.info.execIn = flow;
        if (flow && d.info.exits.empty()) d.info.exits = {""};
        if (d.info.title.empty()) d.info.title = d.function;
        d.type = std::string(CustomNodePrefix()) + d.function;
        if (!bad) out.nodes.push_back(d);
        at = pClose;
    }
    out.vmSource = src;
}

// ---- the loaded files ----------------------------------------------------------------------------------------------------
/// One node's program: what a running node needs (shared by every node of the type; replaced on reload).
struct CustomProgram {
    CustomNodeDecl decl;
    std::string vmLanguage, vmSource;
};
struct LoadedFile {
    std::string path, text;
    long long stamp = 0;
    std::vector<std::string> types;
};
struct Library {
    debugutil::SpinLock lock;
    std::map<std::string, std::shared_ptr<const CustomProgram>> programs;   // type -> program
    std::vector<LoadedFile> files;
    std::chrono::steady_clock::time_point lastPoll{};
};
Library& Lib() { static Library l; return l; }
std::atomic<int> g_libRevision{0};

std::shared_ptr<const CustomProgram> CurrentProgram(const std::string& type) {
    Library& l = Lib();
    debugutil::SpinGuard g(l.lock);
    auto it = l.programs.find(type);
    return it == l.programs.end() ? nullptr : it->second;
}

long long FileStamp(const std::string& path) {
    std::error_code ec;
    const fs::path p = fs::u8path(path);
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto size = fs::file_size(p, ec);
    return (long long)t.time_since_epoch().count() ^ (ec ? 0LL : (long long)size << 1);
}
bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
std::string FileNameOf(const std::string& path) {
    const std::size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

// ---- the running node ----------------------------------------------------------------------------------------------------
// The values a body gives back (or `original` carries), packed the way the header comment says.
VsValue Pack(const VsNodeInfo& info, const std::vector<VsValue>& outs, int exit) {
    const bool exits = info.execIn && info.exits.size() > 1;
    if (exits) {
        if (outs.empty()) return VsValue((float)exit);
        VsValue list = VsValue::MakeArray();
        list.AsArray()->push_back(VsValue((float)exit));
        for (const VsValue& v : outs) list.AsArray()->push_back(v);
        return list;
    }
    if (outs.empty()) return VsValue{};
    if (outs.size() == 1) return outs[0];
    VsValue list = VsValue::MakeArray();
    for (const VsValue& v : outs) list.AsArray()->push_back(v);
    return list;
}
int ExitOf(const VsNodeInfo& info, const VsValue& v) {
    const int n = (int)info.exits.size();
    if (v.IsBool()) return v.AsBool() ? 0 : (n > 1 ? 1 : 0);
    if (v.IsString()) {
        const std::string s = LowerAscii(Trim(v.AsString()));
        for (int k = 0; k < n; ++k) if (LowerAscii(info.exits[(std::size_t)k]) == s) return k;
        return -1;
    }
    if (v.IsNumber()) {
        const int k = (int)v.AsFloat();
        return k >= 0 && k < n ? k : -1;
    }
    return -1;
}
void Unpack(const VsNodeInfo& info, const VsValue& r, std::vector<VsValue>& outs, int& exit) {
    const std::size_t k = info.outs.size();
    outs.assign(k, VsValue{});
    exit = 0;
    const bool exits = info.execIn && info.exits.size() > 1;
    std::vector<VsValue> items;
    if (r.IsArray() && r.AsArray()) items = *r.AsArray();
    if (exits) {
        if (k == 0) { exit = ExitOf(info, r.IsArray() && !items.empty() ? items[0] : r); return; }
        if (items.empty()) { exit = ExitOf(info, r); return; }
        exit = ExitOf(info, items[0]);
        for (std::size_t i = 0; i < k && i + 1 < items.size(); ++i) outs[i] = items[i + 1];
        return;
    }
    if (k == 1) { outs[0] = r; return; }
    for (std::size_t i = 0; i < k && i < items.size(); ++i) outs[i] = items[i];
}

const char* VmLanguageFor(const std::string& language) {
    if (language == "lua") return "lua";
    if (language == "cs") return "okayscript-next";
    return "okayscript";
}

class CustomScriptNode : public VsNode {
public:
    CustomScriptNode(std::string type, std::shared_ptr<const CustomProgram> prog)
        : m_type(std::move(type)), m_prog(std::move(prog)), m_seen(g_libRevision.load()) {
        m_exitCount = m_prog && m_prog->decl.info.execIn ? (int)m_prog->decl.info.exits.size() : 0;
    }
    int ExecOutCount() const override { return m_exitCount; }
    VsValue Eval(int pin, NodeGraph& g, VsContext& ctx) override {
        Refresh();
        if (m_prog && m_prog->decl.info.execIn) return pin >= 0 && pin < (int)m_last.size() ? m_last[(std::size_t)pin] : VsValue{};
        std::vector<VsValue> outs;
        int exit = 0;
        if (!RunBody(g, ctx, outs, exit)) return VsValue{};
        return pin >= 0 && pin < (int)outs.size() ? outs[(std::size_t)pin] : VsValue{};
    }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        Refresh();
        std::vector<VsValue> outs;
        int exit = 0;
        if (!RunBody(g, ctx, outs, exit)) return -1;
        m_last = outs;
        if (exit < 0) {
            g.Error(this, ctx, Who() + ": the body returned an exit this node doesn't have (it has " +
                                   std::to_string(m_exitCount) + ")");
            return -1;
        }
        return exit;
    }

private:
    std::string m_type;
    std::shared_ptr<const CustomProgram> m_prog;
    int m_seen = 0;
    int m_exitCount = 0;
    std::unique_ptr<IScriptVM> m_vm;
    const CustomProgram* m_vmFor = nullptr;   // the program m_vm loaded (or failed to load)
    std::string m_loadError;
    ScriptHost m_host;
    std::vector<VsValue> m_last;
    std::unique_ptr<NodeGraph> m_base;        // the original node, for a custom version (@base)
    int m_baseId = -1;
    bool m_running = false;

    std::string Who() const {
        const std::string title = m_prog ? m_prog->decl.info.title : m_type;
        const std::string file = m_prog ? FileNameOf(m_prog->decl.info.source) : std::string();
        return "custom node '" + title + "'" + (file.empty() ? std::string() : " (" + file + ")");
    }
    // Hot reload: a newer program for this type replaces the one the node runs.
    void Refresh() {
        const int rev = g_libRevision.load();
        if (rev == m_seen) return;
        m_seen = rev;
        std::shared_ptr<const CustomProgram> cur = CurrentProgram(m_type);
        if (cur == m_prog) return;
        m_prog = std::move(cur);
        m_vm.reset();
        m_vmFor = nullptr;
        m_base.reset();
        m_baseId = -1;
    }
    bool EnsureVm(NodeGraph& g, VsContext& ctx) {
        if (m_vmFor == m_prog.get() && m_vm) return true;
        if (m_vmFor == m_prog.get() && !m_loadError.empty()) { g.Error(this, ctx, m_loadError); return false; }
        m_vmFor = m_prog.get();
        m_loadError.clear();
        m_vm = CreateScriptVM(m_prog->vmLanguage);
        if (!m_vm) { m_loadError = Who() + ": this build has no " + m_prog->decl.info.language + " scripts"; g.Error(this, ctx, m_loadError); return false; }
        m_host.gameObject = ctx.gameObject;
        m_host.transform = ctx.transform;
        m_host.deltaTime = ctx.deltaTime;
        m_vm->Bind(&m_host);
        std::string err;
        if (!m_vm->Load(m_prog->vmSource, &err)) {
            m_vm.reset();
            m_loadError = Who() + ": its script doesn't load: " + err;
            g.Error(this, ctx, m_loadError);
            return false;
        }
        m_vm->Bind(&m_host);
        return true;
    }
    bool RunBase(NodeGraph& g, VsContext& ctx, const std::vector<VsValue>& ins, VsValue& original) {
        const VsNodeInfo& info = m_prog->decl.info;
        if (!m_base) {
            m_base = std::make_unique<NodeGraph>();
            m_base->debugKey = g.debugKey;
            std::string e;
            std::vector<std::string> args;
            if (!m_prog->decl.baseArg.empty()) args.push_back(m_prog->decl.baseArg);
            std::unique_ptr<VsNode> n = MakeNode(info.base, args, &e);
            if (!n) { m_base.reset(); g.Error(this, ctx, Who() + ": its original node '" + info.base + "' can't be made: " + e); return false; }
            VsNode* a = m_base->AddNode(std::move(n));
            a->type = info.base;
            a->fileId = fileId;
            a->targetPin = TargetPinFor(info.base);
            m_baseId = a->id;
        }
        VsNode* b = m_base->Node(m_baseId);
        // Only the inputs this node is given: some nodes act on how many they get (a builtin's arguments).
        b->inDefault.assign(ins.size(), VsValue{});
        b->hasDefault.assign(ins.size(), 0);
        for (std::size_t i = 0; i < ins.size(); ++i)
            if (HasInput((int)i) || !info.ins[i].def.empty()) b->SetDefault((int)i, ins[i]);
        const int errorsBefore = m_base->ErrorCount();
        int exit = 0;
        if (info.execIn) {
            exit = b->Exec(*m_base, ctx);
            if (exit == NodeGraph::kSuspend) {
                b->OnSuspendRejected();
                g.Error(this, ctx, Who() + ": its original node '" + info.base + "' waits, and a custom node can't wait yet");
                return false;
            }
        }
        std::vector<VsValue> outs;
        for (std::size_t k = 0; k < info.outs.size(); ++k) outs.push_back(m_base->Pull(m_baseId, (int)k, ctx));
        if (m_base->ErrorCount() > errorsBefore) g.Error(this, ctx, Who() + ": its original node: " + m_base->LastError());
        original = Pack(info, outs, exit < 0 ? 0 : exit);
        return true;
    }
    bool RunBody(NodeGraph& g, VsContext& ctx, std::vector<VsValue>& outs, int& exit) {
        if (!m_prog) {
            g.Error(this, ctx, "custom node '" + m_type + "' is gone: its script no longer declares it");
            return false;
        }
        if (m_running) { g.Error(this, ctx, Who() + " used itself while it was running"); return false; }
        const VsNodeInfo& info = m_prog->decl.info;
        std::vector<VsValue> args;
        for (std::size_t i = 0; i < info.ins.size(); ++i) args.push_back(In((int)i, g, ctx, DefaultValue(info.ins[i])));
        std::vector<VsValue> ins = args;
        if (!info.base.empty()) {
            VsValue original;
            if (!RunBase(g, ctx, ins, original)) return false;
            args.push_back(original);
        }
        if (!EnsureVm(g, ctx)) return false;
        if (m_host.gameObject != ctx.gameObject || m_host.transform != ctx.transform) {
            m_host.gameObject = ctx.gameObject;
            m_host.transform = ctx.transform;
            m_vm->Bind(&m_host);
        }
        m_host.deltaTime = ctx.deltaTime;
        if (!m_vm->HasEvent(m_prog->decl.function)) {
            g.Error(this, ctx, Who() + ": its script has no function '" + m_prog->decl.function + "'");
            return false;
        }
        std::string err;
        m_running = true;
        const VsValue r = m_vm->CallEventArgs(m_prog->decl.function, args, &err);
        m_running = false;
        if (!err.empty()) {
            g.Error(this, ctx, Who() + ": " + err);
            return false;
        }
        Unpack(info, r, outs, exit);
        return true;
    }
};

// The placeholder for a custom node whose script is missing.
class MissingCustomNode : public VsNode {
public:
    explicit MissingCustomNode(std::string t) : m_missing(std::move(t)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override { Say(g, ctx); return VsValue{}; }
    int Exec(NodeGraph& g, VsContext& ctx) override { Say(g, ctx); return -1; }
    const std::string& Missing() const { return m_missing; }
private:
    std::string m_missing;
    void Say(NodeGraph& g, VsContext& ctx) {
        g.Error(this, ctx, "custom node '" + m_missing + "' is missing (its script was not found or no longer declares it), so this node does not run");
    }
};

void RegisterProgram(const std::shared_ptr<const CustomProgram>& prog) {
    const std::string type = prog->decl.type;
    RegisterNodeType(type, prog->decl.info, [type](const std::vector<std::string>&, std::string*) -> std::unique_ptr<VsNode> {
        return std::make_unique<CustomScriptNode>(type, CurrentProgram(type));
    });
}

} // namespace

// ---- public ------------------------------------------------------------------------------------------------------------------
bool IsCustomNodeType(const std::string& type) { return StartsWith(type, CustomNodePrefix()) && type.size() > 7; }

std::string CustomNodeLanguageFor(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return std::string();
    const std::string e = LowerAscii(path.substr(dot + 1));
    if (e == "okay" || e == "okayscript") return "okayscript";
    if (e == "lua") return "lua";
    if (e == "cs") return "cs";
    return std::string();
}

CustomNodeScript ParseCustomNodeScript(const std::string& text, const std::string& language, const std::string& path) {
    CustomNodeScript s;
    s.path = path;
    s.language = language;
    s.vmSource = text;
    if (language == "cs") ParseCsAttributes(text, s);
    else ParseCommentHeaders(text, language == "lua", s);
    // Two nodes with one function would be one type.
    std::set<std::string> seen;
    for (auto it = s.nodes.begin(); it != s.nodes.end();) {
        if (!seen.insert(it->type).second) {
            s.problems.push_back("line " + std::to_string(it->info.line) + ": a second node on function '" + it->function + "'");
            it = s.nodes.erase(it);
        } else ++it;
    }
    return s;
}

bool LoadCustomNodeText(const std::string& path, const std::string& text, std::string* err) {
    const std::string language = CustomNodeLanguageFor(path);
    if (language.empty()) { if (err) *err = path + " is not an OkayScript, Lua or C# file"; return false; }
    Library& l = Lib();
    {
        debugutil::SpinGuard g(l.lock);
        for (const LoadedFile& f : l.files) if (f.path == path && f.text == text) return !f.types.empty();
    }
    const CustomNodeScript s = ParseCustomNodeScript(text, language, path);
    for (const std::string& p : s.problems) Log::Warning("Custom nodes: ", FileNameOf(path), " ", p);
    std::vector<std::shared_ptr<const CustomProgram>> progs;
    std::vector<std::string> types, gone;
    for (const CustomNodeDecl& d : s.nodes) {
        auto p = std::make_shared<CustomProgram>();
        p->decl = d;
        p->vmLanguage = VmLanguageFor(language);
        p->vmSource = s.vmSource;
        progs.push_back(p);
        types.push_back(d.type);
    }
    {
        debugutil::SpinGuard g(l.lock);
        LoadedFile* f = nullptr;
        for (LoadedFile& x : l.files) if (x.path == path) f = &x;
        if (!f) { l.files.push_back(LoadedFile{}); f = &l.files.back(); f->path = path; }
        for (const std::string& t : f->types)
            if (std::find(types.begin(), types.end(), t) == types.end()) gone.push_back(t);
        f->text = text;
        f->stamp = FileStamp(path);
        f->types = types;
        for (const std::string& t : gone) l.programs.erase(t);
        for (const auto& p : progs) {
            auto it = l.programs.find(p->decl.type);
            if (it != l.programs.end() && it->second->decl.info.source != path)
                Log::Warning("Custom nodes: ", FileNameOf(path), " declares '", p->decl.type, "', which ",
                             FileNameOf(it->second->decl.info.source), " declared too; this file's one is used");
            l.programs[p->decl.type] = p;
        }
    }
    for (const std::string& t : gone) UnregisterNodeType(t);
    for (const auto& p : progs) RegisterProgram(p);
    ++g_libRevision;
    if (types.empty()) {
        if (err) *err = s.problems.empty() ? FileNameOf(path) + " declares no custom node" : s.problems.front();
        return false;
    }
    return true;
}

bool LoadCustomNodeScript(const std::string& path, std::string* err) {
    std::string text;
    if (!ReadFile(path, text)) { if (err) *err = "can't read " + path; return false; }
    return LoadCustomNodeText(path, text, err);
}

void UnloadCustomNodeScript(const std::string& path) {
    Library& l = Lib();
    std::vector<std::string> gone;
    {
        debugutil::SpinGuard g(l.lock);
        for (auto it = l.files.begin(); it != l.files.end(); ++it) {
            if (it->path != path) continue;
            gone = it->types;
            for (const std::string& t : gone) {
                auto p = l.programs.find(t);
                if (p != l.programs.end() && p->second->decl.info.source == path) l.programs.erase(p);
            }
            l.files.erase(it);
            break;
        }
    }
    for (const std::string& t : gone) UnregisterNodeType(t);
    ++g_libRevision;
}

int PollCustomNodeScripts(double everySeconds) {
    Library& l = Lib();
    std::vector<std::pair<std::string, long long>> files;
    {
        debugutil::SpinGuard g(l.lock);
        const auto now = std::chrono::steady_clock::now();
        if (everySeconds > 0 && l.lastPoll.time_since_epoch().count() != 0 &&
            std::chrono::duration<double>(now - l.lastPoll).count() < everySeconds) return 0;
        l.lastPoll = now;
        for (const LoadedFile& f : l.files) files.push_back({f.path, f.stamp});
    }
    int reloaded = 0;
    for (const auto& f : files) {
        const long long stamp = FileStamp(f.first);
        if (stamp == f.second) continue;
        std::string text;
        if (stamp == 0 || !ReadFile(f.first, text)) {   // deleted: its nodes go (graphs keep placeholders)
            UnloadCustomNodeScript(f.first);
            ++reloaded;
            continue;
        }
        std::string err;
        LoadCustomNodeText(f.first, text, &err);
        {   // the same text with a new time: remember the time
            debugutil::SpinGuard g(l.lock);
            for (LoadedFile& x : l.files) if (x.path == f.first) x.stamp = stamp;
        }
        ++reloaded;
    }
    return reloaded;
}

int ScanCustomNodeFolder(const std::string& folder) {
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(fs::u8path(folder), ec)) return 0;
    int loaded = 0;
    for (fs::recursive_directory_iterator it(fs::u8path(folder), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().u8string();
        if (it->is_directory(ec)) {
            if (!name.empty() && (name[0] == '.' || name == "build" || name == "Library")) it.disable_recursion_pending();
            continue;
        }
        const std::string path = it->path().generic_u8string();
        const std::string language = CustomNodeLanguageFor(path);
        if (language.empty() || it->file_size(ec) > (1u << 20)) continue;
        std::string text;
        if (!ReadFile(path, text)) continue;
        const bool declares = language == "cs" ? text.find("[Node(") != std::string::npos : text.find("@node") != std::string::npos;
        if (!declares) continue;
        if (LoadCustomNodeText(path, text, nullptr)) ++loaded;
    }
    return loaded;
}

std::vector<std::string> LoadedCustomNodeScripts() {
    Library& l = Lib();
    debugutil::SpinGuard g(l.lock);
    std::vector<std::string> out;
    for (const LoadedFile& f : l.files) out.push_back(f.path);
    return out;
}

int CustomNodesRevision() { return g_libRevision.load(); }

std::vector<std::string> CustomNodeLibsInGraphText(const std::string& graphText) {
    std::vector<std::string> out;
    for (const std::string& line : TextLines(graphText)) {
        const std::string t = Trim(line);
        if (!StartsWith(t, "nodelib") || t.size() < 9 || !std::isspace((unsigned char)t[7])) continue;
        // A graph embedded in a scene keeps its lines with the quotes escaped (\"): read both spellings.
        std::string rest = Trim(t.substr(8)), p;
        for (std::size_t k = 0; k < rest.size(); ++k) {
            if (rest[k] == '\\' && k + 1 < rest.size() && (rest[k + 1] == '"' || rest[k + 1] == '\\')) { p += rest[++k]; continue; }
            p += rest[k];
        }
        p = Trim(p);
        if (p.size() >= 2 && p.front() == '"') {
            const std::size_t q = p.find('"', 1);
            p = q == std::string::npos ? p.substr(1) : p.substr(1, q - 1);
        } else {
            const std::size_t sp = p.find_first_of(" \t#");
            if (sp != std::string::npos) p = p.substr(0, sp);
        }
        if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    return out;
}

std::string ResolveCustomNodeLib(const std::string& lib, const std::string& graphFile) {
    if (lib.empty() || !pathsafety::IsSafeRelative(lib)) return std::string();
    std::error_code ec;
    if (fs::is_regular_file(fs::u8path(lib), ec)) return lib;
    if (graphFile.empty()) return std::string();
    fs::path dir = fs::u8path(graphFile).parent_path();
    for (int up = 0; up < 16 && !dir.empty(); ++up) {
        const fs::path c = dir / fs::u8path(lib);
        if (fs::is_regular_file(c, ec)) return c.generic_u8string();
        const fs::path a = dir / "Assets" / fs::u8path(lib);   // a project folder: the path is relative to its Assets
        if (fs::is_regular_file(a, ec)) return a.generic_u8string();
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return std::string();
}

int LoadCustomNodeLibs(const std::string& graphText, const std::string& graphFile) {
    int n = 0;
    for (const std::string& lib : CustomNodeLibsInGraphText(graphText)) {
        std::string path = ResolveCustomNodeLib(lib, graphFile);
        if (path.empty()) {   // not found from here: a loaded file with that path at its end is the one
            for (const std::string& f : LoadedCustomNodeScripts()) {
                const std::string g = fs::u8path(f).generic_u8string();
                if (g == lib || (g.size() > lib.size() && g.compare(g.size() - lib.size(), lib.size(), lib) == 0 && g[g.size() - lib.size() - 1] == '/'))
                    path = f;
            }
        }
        if (path.empty()) { Log::Warning("Visual Script: custom-node script '", lib, "' was not found"); continue; }
        std::string err;
        if (LoadCustomNodeScript(path, &err)) ++n;
        else if (!err.empty()) Log::Warning("Visual Script: ", err);
    }
    return n;
}

std::unique_ptr<VsNode> MakeMissingCustomNode(const std::string& type) { return std::make_unique<MissingCustomNode>(type); }
bool IsMissingCustomNode(const VsNode* node) { return dynamic_cast<const MissingCustomNode*>(node) != nullptr; }

// ---- "Make a custom version" ---------------------------------------------------------------------------------------------
namespace {
bool Reserved(const std::string& w) {
    static const std::set<std::string> k = {
        "and", "or", "not", "end", "function", "local", "if", "then", "else", "elseif", "return", "nil", "true", "false",
        "in", "for", "while", "do", "repeat", "until", "break", "goto", "var", "new", "class", "default", "string", "float",
        "int", "bool", "object", "base", "this", "self", "null", "void", "static", "public", "private", "switch", "case",
        "const", "let", "original", "transform", "gameObject", "foreach", "is", "as", "out", "ref", "params", "try", "catch"};
    return k.count(w) > 0;
}
// A parameter name from a pin label: "Event Name" -> "event_name"; each once.
std::vector<std::string> ParamNames(const std::vector<VsPinSpec>& pins) {
    std::vector<std::string> out;
    for (const VsPinSpec& p : pins) {
        std::string n;
        for (char c : p.name) n += IsIdentChar(c) ? (char)std::tolower((unsigned char)c) : '_';
        while (n.find("__") != std::string::npos) n.erase(n.find("__"), 1);
        while (!n.empty() && n.back() == '_') n.pop_back();
        while (!n.empty() && n.front() == '_') n.erase(n.begin());
        if (n.empty()) n = "value";
        if (std::isdigit((unsigned char)n[0])) n = "v" + n;
        if (Reserved(n)) n += "_in";
        std::string u = n;
        for (int k = 2; std::find(out.begin(), out.end(), u) != out.end(); ++k) u = n + std::to_string(k);
        out.push_back(u);
    }
    return out;
}
const char* CsType(PinType t) {
    switch (t) {
        case PinType::Number: return "float";
        case PinType::Bool: return "bool";
        case PinType::Text: return "string";
        case PinType::Vec3: return "Vector3";
        case PinType::Object: return "GameObject";
        default: return nullptr;
    }
}
std::string OneLine(const std::string& s) {
    std::string o;
    for (char c : s) o += (c == '\n' || c == '\r') ? ' ' : c;
    return Trim(o);
}
std::string CsString(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o + "\"";
}
// The body expression when the language can do the node's job itself (the same rules the node uses), else "".
std::string BodyItDoes(const CustomVersionSpec& s, const std::string& language, const std::vector<std::string>& p) {
    const std::string& t = s.baseType;
    const bool lua = language == "lua";
    if (s.ins.size() == 2 && s.outs.size() == 1 && !s.execIn) {
        if (t == "Add") return p[0] + " + " + p[1];
        if (t == "Sub") return p[0] + " - " + p[1];
        if (t == "Mul") return p[0] + " * " + p[1];
        if (t == "And") return lua ? p[0] + " and " + p[1] : p[0] + " && " + p[1];
        if (t == "Or") return lua ? p[0] + " or " + p[1] : p[0] + " || " + p[1];
    }
    if (s.ins.size() == 1 && s.outs.size() == 1 && !s.execIn && t == "Not") return lua ? "not " + p[0] : "!" + p[0];
    return std::string();
}
// C#: an engine call that acts on this object, written the way a script writes it (VsApiEntry::code, with the
// parameter names of this node), when the node has no Target input.
std::string CsApiCall(const CustomVersionSpec& s, const std::vector<std::string>& p) {
    const VsApiEntry* e = VsApiFind(s.baseType);
    if (!e || e->hasTarget || e->code.empty() || e->ins.size() != s.ins.size()) return std::string();
    std::string code = e->code;
    // Rename each argument of the call to this node's parameter for that pin.
    const std::size_t open = code.find('(');
    if (open != std::string::npos && code.back() == ')') {
        std::string call = code.substr(0, open + 1);
        for (std::size_t i = 0; i < p.size(); ++i) call += (i ? ", " : "") + p[i];
        code = call + ")";
    } else if (code.find(" = ") != std::string::npos && p.size() == 1) {
        code = code.substr(0, code.find(" = ")) + " = " + p[0];
    } else if (!p.empty()) return std::string();
    return code;
}
} // namespace

std::string CustomVersionFunctionName(const std::string& title, const std::string& language) {
    std::string n;
    for (char c : title) {
        if (IsIdentChar(c)) n += (char)std::tolower((unsigned char)c);
        else if (!n.empty() && n.back() != '_') n += '_';
    }
    while (!n.empty() && n.back() == '_') n.pop_back();
    if (n.empty()) n = "node";
    if (std::isdigit((unsigned char)n[0])) n = "n" + n;
    n += "_custom";
    if (language == "cs") {   // C#: PascalCase method names
        std::string pc;
        bool up = true;
        for (char c : n) {
            if (c == '_') { up = true; continue; }
            pc += up ? (char)std::toupper((unsigned char)c) : c;
            up = false;
        }
        n = pc;
    }
    std::string u = n;
    for (int k = 2; FindRegisteredVsNode(std::string(CustomNodePrefix()) + u); ++k)
        u = n + (language == "cs" ? "" : "_") + std::to_string(k);
    return u;
}

bool MakeCustomVersionScript(const CustomVersionSpec& s, const std::string& language, const std::string& function,
                             std::string& text, std::string* why) {
    auto refuse = [&](const std::string& m) { if (why) *why = m; return false; };
    const std::vector<std::string> p = ParamNames(s.ins);
    const std::string title = OneLine(s.title.empty() ? s.baseType : s.title) + " (custom)";
    const std::string help = OneLine(s.help.empty() ? "A custom version of " + (s.title.empty() ? s.baseType : s.title) + "." : s.help);
    const std::string category = OneLine(s.category.empty() ? std::string("Custom") : s.category);
    const bool exits = s.execIn && s.exits.size() > 1;
    std::string own = BodyItDoes(s, language, p);
    std::string api = language == "cs" ? CsApiCall(s, p) : std::string();
    const bool base = own.empty() && api.empty();
    const std::string baseLine = s.baseType + (s.baseArg.empty() ? std::string() : " " + OneLine(s.baseArg));
    std::ostringstream o;
    if (language == "okayscript" || language == "lua") {
        const std::string c = language == "lua" ? "-- " : "// ";
        o << c << "A custom version of the built-in node \"" << OneLine(s.title.empty() ? s.baseType : s.title)
          << "\". Built-in nodes never change: this copy is yours to edit.\n";
        if (base)
            o << c << "The original node runs first and its result comes in as `original`: return something else to change\n"
              << c << "what this node gives, or delete the @base line (and `original`) to replace it completely.\n";
        o << c << "@node " << title << "\n" << c << "@category " << category << "\n" << c << "@help " << help << "\n";
        if (exits) {
            o << c << "@exits ";
            for (std::size_t i = 0; i < s.exits.size(); ++i) o << (i ? ", " : "") << (s.exits[i].empty() ? "Exit" + std::to_string(i) : s.exits[i]);
            o << "\n";
        } else if (s.execIn) o << c << "@flow\n";
        for (const VsPinSpec& pin : s.ins)
            o << c << "@in " << pin.name << ": " << PinTypeName(pin.type) << (pin.def.empty() ? "" : " = " + pin.def) << "\n";
        for (const VsPinSpec& pin : s.outs) o << c << "@out " << pin.name << ": " << PinTypeName(pin.type) << "\n";
        if (base) o << c << "@base " << baseLine << "\n";
        std::string params;
        for (std::size_t i = 0; i < p.size(); ++i) params += (i ? ", " : "") + p[i];
        if (base) params += (params.empty() ? "" : ", ") + std::string("original");
        const std::string ret = base ? "original" : own;
        const bool returns = !s.outs.empty() || exits;
        if (language == "lua") {
            o << "function " << function << "(" << params << ")\n";
            if (returns) o << "    return " << ret << "\n";
            o << "end\n";
        } else {
            o << "function " << function << "(" << params << ") {\n";
            if (returns) o << "    return " << ret << ";\n";
            o << "}\n";
        }
        text = o.str();
        return true;
    }
    if (language != "cs") return refuse("custom nodes are written in OkayScript, Lua or C#");
    // C# (the typed C#-style language): one output at most, and only the types it can take from the engine.
    if (s.outs.size() > 1) return refuse("a C# custom node has at most one output; make this one in OkayScript or Lua");
    if (exits && !s.outs.empty()) return refuse("a C# custom node with several exits can't also have outputs; use OkayScript or Lua");
    for (const VsPinSpec& pin : s.ins)
        if (!CsType(pin.type)) return refuse("input '" + pin.name + "' is " + PinTypeName(pin.type) + ", which a C# node can't take; use OkayScript or Lua");
    for (const VsPinSpec& pin : s.outs)
        if (!CsType(pin.type)) return refuse("output '" + pin.name + "' is " + PinTypeName(pin.type) + ", which a C# node can't give; use OkayScript or Lua");
    std::string cls;
    for (char ch : function) if (IsIdentChar(ch)) cls += ch;
    o << "// A custom version of the built-in node \"" << OneLine(s.title.empty() ? s.baseType : s.title)
      << "\". Built-in nodes never change: this copy is yours to edit.\n";
    if (base)
        o << "// The original node runs first and its result comes in as `original`: return something else to change\n"
          << "// what this node gives, or delete Base (and `original`) to replace it completely.\n";
    o << "class " << cls << "Node : Behaviour {\n";
    o << "    [Node(" << CsString(title) << ", Category = " << CsString(category) << ", Help = " << CsString(help);
    if (exits) {
        std::string ex;
        for (std::size_t i = 0; i < s.exits.size(); ++i) ex += (i ? "," : "") + (s.exits[i].empty() ? "Exit" + std::to_string(i) : s.exits[i]);
        o << ", Exits = " << CsString(ex);
    } else if (s.execIn && !s.outs.empty()) o << ", Flow = true";
    if (!s.ins.empty()) {
        std::string labels;
        for (std::size_t i = 0; i < s.ins.size(); ++i) labels += (i ? "," : "") + s.ins[i].name;
        o << ", Inputs = " << CsString(labels);
    }
    if (!s.outs.empty()) o << ", Output = " << CsString(s.outs[0].name);
    if (base) o << ", Base = " << CsString(baseLine);
    o << ")]\n";
    const std::string ret = exits ? "int" : s.outs.empty() ? "void" : CsType(s.outs[0].type);
    o << "    " << ret << " " << function << "(";
    for (std::size_t i = 0; i < p.size(); ++i) {
        o << (i ? ", " : "") << CsType(s.ins[i].type) << " " << p[i];
        if (!s.ins[i].def.empty()) {
            const std::string& d = s.ins[i].def;
            if (s.ins[i].type == PinType::Text) o << " = " << CsString(d);
            else if (s.ins[i].type == PinType::Vec3) o << " = new Vector3(" << d << ")";
            else o << " = " << d;
        }
    }
    if (base && ret != "void") o << (p.empty() ? "" : ", ") << ret << " original";
    o << ") {\n";
    if (base) { if (ret != "void") o << "        return original;\n"; }
    else if (!api.empty()) o << "        " << (ret == "void" ? "" : "return ") << api << ";\n";
    else o << "        return " << own << ";\n";
    o << "    }\n}\n";
    text = o.str();
    return true;
}

} // namespace okay::vs
