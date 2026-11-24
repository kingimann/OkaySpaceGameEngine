#pragma once
// "Experimental: try our own code": one table of the own replacements that can be switched on at RUN time, and the pure logic
// around it (docs/try_it/<id>-own-switch.md, docs/status/own_code_roadmap.md). Standard library only, so the launcher, the editor and
// the tests share it; it does not include any engine header.
//
// How it works, in plain words:
//  * Every item is ON by default (owner, 2026-11-23: "i want our stuff to be on by default, all of them"); the person can turn
//    any of them off. The choices are saved in one small text file, own_switches.cfg, in the person's own settings folder
//    (Windows: %LOCALAPPDATA%\OkaySpace; elsewhere $XDG_CONFIG_HOME/okayspace or ~/.config/okayspace), so the launcher and the
//    editor, which sit in different folders, read the same file. Format: a line `off=http` (ids the person turned off; an item
//    is on unless it is listed there, so an item added to the table later starts on) and, for older programs, a line
//    `on=okayui,http` (the ids that are on). No file, or a file without an `off=` line (written by the old off-by-default
//    version, where an id missing from `on=` only meant "never chosen"), means: nothing turned off, everything on.
//  * At the very top of main(), before anything reads its variable, Startup() sets the environment variable of each
//    chosen item inside the running program. Programs the launcher starts inherit it.
//  * A variable the person set themselves (set / $env: / export) always wins: the item then shows "set by an environment
//    variable" and cannot be changed from the screen. To tell our own variables from the person's, the variables we set are
//    listed in OKAY_OWN_SWITCHES_APPLIED (a child program sees which ones its parent set).
//  * Changes take effect after a restart (each backend is chosen once, on first use), and the screen says so.
//
// TO ADD AN ITEM LATER (okay::platform events, own ImGui renderer, the okayui host ...): add ONE line to Items() below.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace okay::ownswitches {

/// What an item needs from the build or the system. A screen passes what is true here; an unavailable item is hidden.
enum Needs : unsigned { kNone = 0, kNotWindows = 1, kNeedsOkayUi = 2 };

struct Item {
    const char* id;        // short stable name stored in the file ("http")
    const char* label;     // the words on the screen
    const char* desc;      // one plain line
    const char* env;       // the environment variable that switches it on
    const char* value;     // what the variable is set to when on
    unsigned needs;        // Needs bits
};

/// The table. Everything here exists on main today and is chosen at run time (compile-time-only options are not listed). Every item is on
/// unless the person turned it off (see the top).
inline const std::vector<Item>& Items() {
    static const std::vector<Item> t = {
        {"okayui",   "OkayUI screens",        "Draw the screens that have an OkayUI version (What's New, the editor Console) with our own toolkit instead of Dear ImGui.", "OKAY_UI_BACKEND",      "okayui", kNeedsOkayUi},
        {"inspector", "OkayUI Inspector",     "Draw the Inspector's component bodies with our own toolkit (a preview). Needs \"OkayUI screens\" on too; any body that has no OkayUI version, or a newer Dear ImGui one, stays Dear ImGui.", "OKAY_UI_INSPECTOR", "1", kNeedsOkayUi},
        {"ownrender", "OkayUI own renderer",  "Draw the OkayUI screens with OkayUI's own renderer instead of SDL's geometry calls: Direct3D 11 where the window uses SDL's Direct3D 11 renderer (the Windows default), OpenGL where it uses SDL's OpenGL renderer. Any other frame draws as before.", "OKAY_UI_OWN_RENDER", "1", kNeedsOkayUi},
        {"http",     "Our own web client",    "Download and check for updates with our own HTTP client instead of curl (Linux builds only; Windows uses its built-in WinHTTP for every download and web call).",   "OKAY_OWN_HTTP",        "1",      kNotWindows},
        {"platform", "Our own window layer",  "Open windows, read the keyboard and mouse and draw with our own okay::platform backend and OpenGL painter instead of SDL (Linux only; OKAY_OWN_PAINTER=software uses the CPU painter).",               "OKAY_PLATFORM_BACKEND", "own",   kNotWindows},
    };
    return t;
}

/// A second program built from the same sources against our own UI (CMake options OKAY_LAUNCHER_ON_OKAYUI / OKAY_EDITOR_ON_OKAYUI, off by default): the
/// Settings > Experimental page shows a row that restarts into it ("launch alternative exe" action; nothing is switched in a file, the other program is just started).
/// In the normal program the row offers the OkayUI one; in the OkayUI program (macro OKAY_ON_OKAYUI) it offers the way back.
struct AlternativeExe {
    const char* label; const char* desc;
    std::vector<std::string> launcherNames, editorNames;   // file names to look for next to the running program
};
inline const AlternativeExe& Alternative() {
#ifdef OKAY_ON_OKAYUI
    static const AlternativeExe a = {"Back to the normal UI (restart into OkaySpace.exe)",
        "You are running the launcher on our own UI (no Dear ImGui). This starts the normal launcher.",
        {"OkaySpace.exe", "OkaySpace", "okayspace-launcher"}, {"OkayEngine.exe", "OkayEngine", "okay-editor"}};
#else
    static const AlternativeExe a = {"Run on our own UI (restart into OkaySpaceOkayUI.exe)",
        "Starts the launcher built on our own UI toolkit (OkayUI) instead of Dear ImGui, and closes this one. Only there when OkaySpaceOkayUI sits next to the launcher. The editor follows: OkayEngineOkayUI is started from the launcher's Create page when it is next to it.",
        {"OkaySpaceOkayUI.exe", "OkaySpaceOkayUI", "okayspace-launcher-okayui"}, {"OkayEngineOkayUI.exe", "OkayEngineOkayUI", "okay-editor-okayui"}};
#endif
    return a;
}

/// What is true on this machine: used to hide items that cannot work here.
struct Context {
    bool windows = false;
    bool okayUi = false;
    static Context Here() {
        Context c;
#if defined(_WIN32)
        c.windows = true;
#endif
#if defined(OKAY_HAVE_OKAYUI)
        c.okayUi = true;
#endif
        return c;
    }
};

inline bool Available(const Item& it, const Context& c) {
    if ((it.needs & kNotWindows) && c.windows) return false;
    if ((it.needs & kNeedsOkayUi) && !c.okayUi) return false;
    return true;
}

// ---- the list of ids (file line and OKAY_OWN_SWITCHES_APPLIED share the format) -----------------------------------------

inline std::vector<std::string> SplitIds(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + ",") {
        if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    return out;
}
inline std::string JoinIds(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& i : v) { if (!s.empty()) s += ','; s += i; }
    return s;
}
inline bool Has(const std::vector<std::string>& v, const std::string& id) {
    for (const std::string& i : v) if (i == id) return true;
    return false;
}
inline const Item* Find(const std::string& id, const std::vector<Item>& items) {
    for (const Item& it : items) if (id == it.id) return &it;
    return nullptr;
}

/// The ids of `items` for which `pick(id)` is true, in table order.
template <class Pick> inline std::vector<std::string> Select(const std::vector<Item>& items, Pick pick) {
    std::vector<std::string> out;
    for (const Item& it : items) if (pick(std::string(it.id))) out.push_back(it.id);
    return out;
}

/// Read the file text and return the ids that are ON, in table order. Only the `off=` line decides: every item not listed
/// there is on. Without an `off=` line (no file, an empty or damaged file, or an old `on=`-only file) everything is on.
/// Unknown ids and repeats are dropped, so a hand-edited file cannot name anything that is not in the table.
inline std::vector<std::string> Parse(const std::string& text, const std::vector<Item>& items = Items()) {
    std::vector<std::string> off;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, 4, "off=") == 0) off = SplitIds(line.substr(4));
    }
    return Select(items, [&](const std::string& id) { return !Has(off, id); });
}
/// Write the choice: `off=` (what Parse reads) and `on=` (what the older off-by-default programs read, so they agree).
inline std::string Serialize(const std::vector<std::string>& on, const std::vector<Item>& items = Items()) {
    const auto isOn = [&](const std::string& id) { return Has(on, id); };
    return "# Experimental: our own code (set from Settings in the launcher or View > Rendering & Performance in the editor). On unless listed in off=\n"
           "off=" + JoinIds(Select(items, [&](const std::string& id) { return !isOn(id); })) + "\non=" + JoinIds(Select(items, isOn)) + "\n";
}

/// The default: everything on.
inline void AllOn(std::vector<std::string>& on, const std::vector<Item>& items = Items()) { on = Select(items, [](const std::string&) { return true; }); }
/// "Back to the third-party parts": everything off.
inline void AllOff(std::vector<std::string>& on) { on.clear(); }
inline void Set(std::vector<std::string>& on, const std::string& id, bool value) {
    std::vector<std::string> n;
    for (const std::string& i : on) if (i != id) n.push_back(i);
    if (value) n.push_back(id);
    on = n;
}

// ---- the environment -------------------------------------------------------------------------------------------------

inline const char* AppliedVar() { return "OKAY_OWN_SWITCHES_APPLIED"; }

inline void PutEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}
inline void DropEnv(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");   // (Windows: an empty value removes the variable)
#else
    unsetenv(name);
#endif
}
inline std::vector<std::string> AppliedNow() {
    const char* e = std::getenv(AppliedVar());
    return SplitIds(e ? e : "");
}

/// True when the person set the item's variable themselves (it is set and we did not set it).
inline bool SetByUser(const Item& it, const std::vector<std::string>& appliedByUs) {
    return std::getenv(it.env) != nullptr && !Has(appliedByUs, it.id);
}

/// Set the variables of the chosen (on), available items (not over a variable the person set), remove the ones a parent program set that
/// are no longer chosen, and record what we set in OKAY_OWN_SWITCHES_APPLIED. Returns the ids now on. Call before anything reads them.
inline std::vector<std::string> Apply(const std::vector<std::string>& on, const std::vector<Item>& items = Items(), const Context& ctx = Context::Here()) {
    const std::vector<std::string> before = AppliedNow();
    std::vector<std::string> applied;
    for (const Item& it : items) {
        const bool byUser = SetByUser(it, before);
        const bool want = Has(on, it.id) && Available(it, ctx);
        if (want && !byUser) { PutEnv(it.env, it.value); applied.push_back(it.id); }
        else if (!want && !byUser && Has(before, it.id)) DropEnv(it.env);
    }
    if (applied.empty()) DropEnv(AppliedVar()); else PutEnv(AppliedVar(), JoinIds(applied).c_str());
    return applied;
}

// ---- what the screen says ---------------------------------------------------------------------------------------------

enum class Status { Off, OnNow, OnAfterRestart, OffAfterRestart, SetByEnvironment };

/// `chosen`: the saved choice. `runningWith`: the ids that were on when this program started (the AppliedNow() taken in Startup).
inline Status StatusOf(const Item& it, const std::vector<std::string>& chosen, const std::vector<std::string>& runningWith, bool setByUser) {
    if (setByUser) return Status::SetByEnvironment;
    const bool want = Has(chosen, it.id), now = Has(runningWith, it.id);
    if (want && now) return Status::OnNow;
    if (want) return Status::OnAfterRestart;
    return now ? Status::OffAfterRestart : Status::Off;
}
inline const char* StatusText(Status s) {
    switch (s) {
        case Status::OnNow: return "on now";
        case Status::OnAfterRestart: return "on after restart";
        case Status::OffAfterRestart: return "off after restart";
        case Status::SetByEnvironment: return "set by an environment variable";
        default: return "off";
    }
}

/// Where the shared file lives (see the top). `home` overrides are for the tests; "" = ask the environment.
inline std::string DefaultPath() {
    namespace fs = std::filesystem;
    fs::path dir;
#if defined(_WIN32)
    if (const char* a = std::getenv("LOCALAPPDATA"); a && *a) dir = fs::path(a) / "OkaySpace";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) dir = fs::path(x) / "okayspace";
    else if (const char* h = std::getenv("HOME"); h && *h) dir = fs::path(h) / ".config" / "okayspace";
#endif
    return (dir / "own_switches.cfg").string();   // no folder known: a file in the working folder
}

// ---- the file ----------------------------------------------------------------------------------------------------------

inline std::vector<std::string> LoadFile(const std::string& path, const std::vector<Item>& items = Items()) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Parse(std::string(), items);   // no file: never chosen, everything on
    std::stringstream ss;
    ss << f.rdbuf();
    return Parse(ss.str(), items);
}
inline bool SaveFile(const std::string& path, const std::vector<std::string>& on, const std::vector<Item>& items = Items()) {
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << Serialize(on, items);
    return (bool)f;
}

/// What a running program keeps: the saved choice (what the screen edits) and what was applied at start.
struct State {
    std::string path;
    std::vector<std::string> chosen;
    std::vector<std::string> runningWith;
};
inline State& Global() { static State s; return s; }

/// The one call at the top of main(), before OwnStack Startup(): load the file, set the variables.
inline void Startup(const std::string& cfgPath = DefaultPath()) {
    State& g = Global();
    g.path = cfgPath;
    g.chosen = LoadFile(cfgPath);
    g.runningWith = Apply(g.chosen);
}
/// Save the screen's choice to the file Startup() read.
inline void SaveChosen() { State& g = Global(); if (!g.path.empty()) SaveFile(g.path, g.chosen); }

} // namespace okay::ownswitches
