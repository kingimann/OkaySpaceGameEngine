// Automation: script.run, a script run inside the host (the editor, or okay mcp's headless host), the way Blender's MCP
// runs Python inside Blender. It uses the engine's own language runtimes through the one script interface every
// backend implements (okay/Scripting/ScriptVM.hpp: OkayScriptVM, LuaScriptVM, JavaScriptVM), so a script here is a
// script of the game's own languages, with their own sandboxes (Lua and JavaScript here have no file, network or
// process access; nothing is added to them but the one call below).
//
//   - Output: everything the run logs (print, console.log, Debug.Log) is caught from the engine log (LogSince).
//   - The value: Lua and JavaScript `return` it from the top of the script; OkayScript has no top-level return, so it
//     is the script's top-level variable `result`.
//   - Errors carry their line: the script's text is run as written (the code wrapped around it starts on its first
//     line and is added after its last), so the line numbers are the script's own.
//   - A budget stops a runaway script: Lua's and JavaScript's instruction budget (SetBudget), OkayScript's step budget
//     (okayscript::StepBudget: every loop turn and call). No threads (CLAUDE.md rule 21a).
//   - One undo step: script.run is Undoable, and the automation commands the script calls are part of the same step.
//   - automation_call(name, params) runs an automation command from the script, the same table an agent calls: the
//     shared script table's Automation.Call(name, json) (automation_call_json in OkayScript), wrapped per language so
//     tables and objects go in and come out. It works only while script.run runs one (a game script gets an error).
//
// Our own code, written on those interfaces and on each language's manual (Lua 5.4, ECMAScript) for the few lines of
// script added around the person's text.
#include "okay/Automation/Automation.hpp"

#include "okay/Core/Log.hpp"
#include "okay/Lua/LuaScriptVM.hpp"
#include "okay/JavaScript/JavaScriptVM.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Scripting/ScriptVM.hpp"
#include "okayscript/Runtime.hpp"   // okayscript::StepBudget

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace okay::automation {

using json::Value;

namespace {

/// The run in progress (script.run sets it; automation_call reads it). One at a time: commands run on one thread.
struct Running {
    Host* host = nullptr;
    int depth = 0;
};
Running& Current() { static Running r; return r; }

// ---- What is added around the script, per language (after its last line, so its lines keep their numbers) ----------

const char* kOkayScriptAfter =
    "\nautomation_call(name, params) {\n"
    "    return from_json(automation_call_json(name, to_json(params)))\n"
    "}\n";

const char* kJavaScriptAfter =
    "\n}\n"
    "function automation_call(name, params) {\n"
    "    return JSON.parse(Automation.Call(name, JSON.stringify(params === undefined ? {} : params)));\n"
    "}\n";

// Lua has no JSON in its standard library: a small encoder and decoder, enough for command parameters and answers.
const char* kLuaAfter =
    "\nend\n"
    "local __aj = {}\n"
    "local __esc = { ['\\\\'] = '\\\\\\\\', ['\"'] = '\\\\\"', ['\\n'] = '\\\\n', ['\\r'] = '\\\\r', ['\\t'] = '\\\\t' }\n"
    "function __aj.enc(v)\n"
    "    local t = type(v)\n"
    "    if t == 'nil' then return 'null' end\n"
    "    if t == 'boolean' then return v and 'true' or 'false' end\n"
    "    if t == 'number' then return tostring(v) end\n"
    "    if t == 'string' then return '\"' .. (v:gsub('[\\\\\"\\n\\r\\t]', __esc)) .. '\"' end\n"
    "    if t == 'table' then\n"
    "        local n, parts, isArr = #v, {}, true\n"
    "        for k in pairs(v) do if type(k) ~= 'number' then isArr = false break end end\n"
    "        if isArr then\n"
    "            for i = 1, n do parts[#parts + 1] = __aj.enc(v[i]) end\n"
    "            return '[' .. table.concat(parts, ',') .. ']'\n"
    "        end\n"
    "        for k, x in pairs(v) do parts[#parts + 1] = __aj.enc(tostring(k)) .. ':' .. __aj.enc(x) end\n"
    "        return '{' .. table.concat(parts, ',') .. '}'\n"
    "    end\n"
    "    error('automation_call: a ' .. t .. ' cannot be sent as JSON')\n"
    "end\n"
    "function __aj.dec(s)\n"
    "    local pos = 1\n"
    "    local unesc = { n = '\\n', t = '\\t', r = '\\r', b = '\\b', f = '\\f' }\n"
    "    local function ws() pos = s:find('[^ \\t\\r\\n]', pos) or (#s + 1) end\n"
    "    local function str()\n"
    "        local out = {}\n"
    "        pos = pos + 1\n"
    "        while true do\n"
    "            local c = s:sub(pos, pos)\n"
    "            if c == '' then error('automation_call: the answer is not JSON') end\n"
    "            if c == '\"' then pos = pos + 1 break end\n"
    "            if c == '\\\\' then\n"
    "                local e = s:sub(pos + 1, pos + 1)\n"
    "                if e == 'u' then out[#out + 1] = utf8.char(tonumber(s:sub(pos + 2, pos + 5), 16)) pos = pos + 6\n"
    "                else out[#out + 1] = unesc[e] or e pos = pos + 2 end\n"
    "            else out[#out + 1] = c pos = pos + 1 end\n"
    "        end\n"
    "        return table.concat(out)\n"
    "    end\n"
    "    local val\n"
    "    val = function()\n"
    "        ws()\n"
    "        local c = s:sub(pos, pos)\n"
    "        if c == '{' then\n"
    "            local o = {}\n"
    "            pos = pos + 1 ws()\n"
    "            if s:sub(pos, pos) == '}' then pos = pos + 1 return o end\n"
    "            while true do\n"
    "                ws() local k = str() ws() pos = pos + 1\n"
    "                o[k] = val() ws()\n"
    "                local d = s:sub(pos, pos) pos = pos + 1\n"
    "                if d == '}' then return o end\n"
    "            end\n"
    "        elseif c == '[' then\n"
    "            local a = {}\n"
    "            pos = pos + 1 ws()\n"
    "            if s:sub(pos, pos) == ']' then pos = pos + 1 return a end\n"
    "            while true do\n"
    "                a[#a + 1] = val() ws()\n"
    "                local d = s:sub(pos, pos) pos = pos + 1\n"
    "                if d == ']' then return a end\n"
    "            end\n"
    "        elseif c == '\"' then return str()\n"
    "        elseif s:sub(pos, pos + 3) == 'true' then pos = pos + 4 return true\n"
    "        elseif s:sub(pos, pos + 4) == 'false' then pos = pos + 5 return false\n"
    "        elseif s:sub(pos, pos + 3) == 'null' then pos = pos + 4 return nil end\n"
    "        local a, b = s:find('^-?[%d%.eE+-]+', pos)\n"
    "        if not a then error('automation_call: the answer is not JSON') end\n"
    "        pos = b + 1\n"
    "        return tonumber(s:sub(a, b))\n"
    "    end\n"
    "    return val()\n"
    "end\n"
    "function automation_call(name, params) return __aj.dec(Automation.Call(name, __aj.enc(params))) end\n";

constexpr const char* kMain = "__automation_main";

int CountLines(const std::string& s) {
    if (s.empty()) return 0;
    int n = 1;
    for (char c : s) if (c == '\n') ++n;
    if (s.back() == '\n') --n;
    return n;
}

/// The first line number in an error message ("script:12: ...", "line 12: ...", "[string]:12:"), 0 when none.
int LineOf(const std::string& msg) {
    for (std::size_t i = 0; i < msg.size(); ++i) {
        const bool afterColon = msg[i] == ':' && i + 1 < msg.size() && std::isdigit((unsigned char)msg[i + 1]);
        const bool afterWord = msg.compare(i, 5, "line ") == 0 && i + 5 < msg.size() && std::isdigit((unsigned char)msg[i + 5]);
        if (!afterColon && !afterWord) continue;
        std::size_t j = i + (afterColon ? 1 : 5);
        int n = 0;
        while (j < msg.size() && std::isdigit((unsigned char)msg[j])) n = n * 10 + (msg[j++] - '0');
        if (afterWord || (j < msg.size() && msg[j] == ':')) return n;
    }
    return 0;
}

std::string LanguageOfFile(const std::string& path) {
    return ScriptLanguageForExtension(std::filesystem::u8path(path).extension().u8string());
}

bool ReadText(const std::string& path, std::string& out) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool IsBudgetMessage(const std::string& msg) {
    return msg.find("budget") != std::string::npos;
}

Result RunScript(Call& c) {
    Host& host = c.host();
    if (!host.ScriptsAllowed())
        return Result::Fail(code::Refused, "Scripts from Automation are switched off in this editor (View > Automation Link: Allow scripts from Automation).");
    if (Current().depth > 0) return Result::Fail(code::Refused, "A script cannot start another script (script.run inside script.run).");
    std::string source = c.Str("source"), language = c.Str("language"), file;
    if (c.Has("file")) {
        if (c.Has("source")) return Result::Fail(code::InvalidParams, "Give the script's text (source) or a file, not both.");
        file = c.Str("file");
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::u8path(file), ec) && !host.projectDir().empty()) {
            for (const char* sub : {"/", "/Assets/"})
                if (std::filesystem::exists(std::filesystem::u8path(host.projectDir() + sub + file), ec)) { file = host.projectDir() + sub + file; break; }
        }
        if (!ReadText(file, source)) return Result::Fail(code::NotFound, "The script file " + c.Str("file") + " cannot be read.");
        if (language.empty()) language = LanguageOfFile(file);
    } else if (!c.Has("source")) {
        return Result::Fail(code::InvalidParams, "Give the script's text (source) or a file.");
    }
    if (language.empty()) language = "okayscript";
    const long long budget = c.Int("budget", 5000000);
    if (budget < 1000 || budget > 500000000) return Result::Fail(code::InvalidParams, "budget must be 1000 to 500000000 steps.");

    std::unique_ptr<IScriptVM> vm = CreateScriptVM(language);
    if (!vm) return Result::Fail(code::InvalidParams, "There is no script language '" + language + "' here (okayscript, lua, javascript).");
    // Each language's own budget; OkayScript's step budget is the calling thread's, set for this run only.
    if (auto* lua = dynamic_cast<LuaScriptVM*>(vm.get())) lua->SetBudget((std::uint64_t)budget);
    if (auto* js = dynamic_cast<JavaScriptVM*>(vm.get())) js->SetBudget((std::uint64_t)budget);
    struct BudgetScope {
        long long was;
        explicit BudgetScope(long long n) : was(okayscript::StepBudget()) { okayscript::StepBudget() = n; }
        ~BudgetScope() { okayscript::StepBudget() = was; }
    };

    const bool isOkayScript = language == "okayscript";
    std::string text;
    if (isOkayScript) text = source + kOkayScriptAfter;
    else if (language == "lua") text = std::string("function ") + kMain + "() " + source + kLuaAfter;
    else text = std::string("function ") + kMain + "() { " + source + kJavaScriptAfter;

    InstallLogRing();
    const std::vector<LogLine> before = LogSince(0, 1, 0);
    const std::uint64_t mark = before.empty() ? 0 : before.back().seq;
    const int lines = CountLines(source);
    host.NoteScriptRun(language, lines);

    ScriptHost scriptHost;
    std::string error;
    vs::VsValue value;
    bool ran = false;
    {
        Current().host = &host;
        ++Current().depth;
        struct Leave { ~Leave() { --Current().depth; if (Current().depth == 0) Current().host = nullptr; } } leave;
        BudgetScope scope(isOkayScript ? budget : -1);
        vm->Bind(&scriptHost);
        ran = vm->Load(text, &error);
        if (ran && !isOkayScript) value = vm->CallEventArgs(kMain, {}, &error);
        if (ran && isOkayScript) value = vm->GetGlobal("result");
        if (!error.empty()) ran = false;
    }
    Value out = Value::Arr();
    for (const LogLine& l : LogSince(mark, 0, 0)) {
        if (!ran && l.text.find(error) != std::string::npos && !error.empty()) continue;   // the error itself comes back as the error
        out.Push(Value::Str(l.text));
    }
    if (!ran) {
        const bool stopped = IsBudgetMessage(error);
        Value details = Value::Obj().Set("language", Value::Str(language)).Set("line", Value::Num(LineOf(error)))
                            .Set("stopped", Value::Boolean(stopped)).Set("output", std::move(out));
        return Result::Fail(code::Failed, (stopped ? "The script was stopped: it used up its budget of " + std::to_string(budget) + " steps. " : std::string("The script failed: ")) + error,
                            std::move(details));
    }
    return Result::Ok(Value::Obj().Set("language", Value::Str(language)).Set("lines", Value::Num(lines))
                          .Set("output", std::move(out)).Set("value", ScriptValueToJson(value)));
}

} // namespace

namespace detail {

void AddScriptRunCommands(Registry& r) {
    Command run = MakeCommand("script.run", "Run a script inside the host (the editor or the headless host), as Blender's MCP runs Python inside Blender: OkayScript, Lua or JavaScript text or a file, on the engine's own runtimes with their own sandboxes. It returns what it printed, its value (Lua and JavaScript: what the script returns; OkayScript: its variable result) and errors with their line; a budget stops a runaway script; its scene changes are one undo step; automation_call(name, params) inside it runs any automation command.",
              {ArgChoice("language", {"okayscript", "lua", "javascript"}, "The language (default: from the file's extension, else okayscript).", nullptr),
               Arg("source", ParamType::String, "The script's text."),
               Arg("file", ParamType::String, "A script file instead (absolute, or inside the project or its Assets)."),
               ArgDefault("budget", ParamType::Integer, "Steps the script may take before it is stopped (Lua and JavaScript: instructions; OkayScript: loop turns and calls).", Value::Num(5000000))},
              "{language, lines, output: [lines], value}", flag::Undoable,
              [](Call& c) { return RunScript(c); }, {code::Refused, code::NotFound, code::Failed});
    run.undoName = "Automation Script";
    r.Add(std::move(run));
}

/// The shared script table's automation_call_json / Automation.Call (ScriptApiCore.cpp adds it to the table).
void AddScriptRunApi(std::vector<ScriptApiEntry>& out) {
    ScriptApiEntry e;
    e.name = "automation_call_json";
    e.typedType = "Automation";
    e.typedName = "Call";
    e.params = {ScriptApiParam{"command", ScriptApiType::String, false, {}}, ScriptApiParam{"params", ScriptApiType::String, true, vs::VsValue(std::string("{}"))}};
    e.ret = ScriptApiType::String;
    e.doc = "Run an automation command (its name and its parameters as JSON text) and give its answer as JSON text "
            "{ok, data | error}. Only inside a script that the automation command script.run runs; scripts use automation_call(name, params).";
    e.fn = [](GameObject*, std::vector<vs::VsValue>& a) -> vs::VsValue {
        Host* host = Current().host;
        if (!host) throw ScriptApiError("automation_call works only in a script run by the automation command script.run");
        const std::string name = a[0].AsString();
        if (name == "script.run") throw ScriptApiError("a script cannot start another script");
        Value params;
        std::string why;
        const std::string text = a.size() > 1 ? a[1].AsString() : std::string("{}");
        if (!text.empty() && !json::Parse(text, params, &why)) throw ScriptApiError("automation_call: the parameters are not JSON: " + why);
        if (params.IsArray() && params.array.empty()) params = Value::Obj();   // an empty Lua table
        return vs::VsValue(json::Write(Commands().Run(*host, name, params).ToJson()));
    };
    out.push_back(std::move(e));
}

} // namespace detail

} // namespace okay::automation
