// The console's host side (okay/Core/ConsoleHost.hpp): the typed line, --cvar, the standard cvars and the
// profiler commands. Written from this engine's own Console, Cvar, CommandLine, Time, DisplaySettings and Profiler
// interfaces; nothing else.
#include "okay/Core/ConsoleHost.hpp"
#include "okay/Core/CommandLine.hpp"
#include "okay/Core/Cvar.hpp"
#include "okay/Core/DisplaySettings.hpp"
#include "okay/Core/Profiler.hpp"
#include "okay/Core/Time.hpp"

#include <cstdio>

namespace okay {

// ---- ConsoleInput -----------------------------------------------------------------------------------------

void ConsoleInput::Type(const std::string& utf8) {
    for (char c : utf8) {
        const unsigned char u = (unsigned char)c;
        if (c == '`' || u < 0x20 || u == 0x7F) continue;
        text += c;
    }
}

void ConsoleInput::Backspace() {
    if (text.empty()) return;
    std::size_t n = text.size() - 1;
    while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) --n;   // back over UTF-8 continuation bytes
    text.erase(n);
}

void ConsoleInput::HistoryPrev() {
    const std::vector<std::string>& h = Console::History();
    if (h.empty()) return;
    if (m_hist < 0) { m_draft = text; m_hist = (int)h.size() - 1; }
    else if (m_hist > 0) --m_hist;
    if (m_hist >= (int)h.size()) m_hist = (int)h.size() - 1;
    text = h[(std::size_t)m_hist];
}

void ConsoleInput::HistoryNext() {
    if (m_hist < 0) return;
    const std::vector<std::string>& h = Console::History();
    if (m_hist + 1 < (int)h.size()) { ++m_hist; text = h[(std::size_t)m_hist]; return; }
    m_hist = -1;
    text = m_draft;
    m_draft.clear();
}

std::vector<std::string> ConsoleInput::Complete() {
    // Only the first word is completed (the command or cvar name); the rest of the line is kept.
    std::size_t start = text.find_first_not_of(' ');
    if (start == std::string::npos) start = text.size();
    const std::size_t end = text.find(' ', start);
    if (end != std::string::npos) return {};   // already past the name
    const std::string prefix = text.substr(start);
    std::vector<std::string> matches = Console::Complete(prefix);
    if (matches.empty()) return matches;
    if (matches.size() == 1) { text = text.substr(0, start) + matches[0] + " "; return matches; }
    std::string common = matches[0];
    for (const std::string& m : matches) {
        std::size_t i = 0;
        while (i < common.size() && i < m.size() && common[i] == m[i]) ++i;
        common.resize(i);
    }
    if (common.size() > prefix.size()) text = text.substr(0, start) + common;
    std::string list;
    for (const std::string& m : matches) { if (!list.empty()) list += "  "; list += m; }
    Console::Print(list);
    return matches;
}

Console::Result ConsoleInput::Submit() {
    const std::string line = text;
    text.clear();
    m_hist = -1;
    m_draft.clear();
    if (line.find_first_not_of(" \t") == std::string::npos) return Console::Result{};
    Console::Print("> " + line);
    return Console::Execute(line);
}

namespace consolehost {

void DeclareOptions(CommandLine& cl) {
    cl.Value("cvar", "NAME=VALUE", "set a console variable (repeatable)");
    cl.Flag("console", "allow the in-game console (the ` key)");
}

std::vector<std::string> ApplyCvarArgs(const std::vector<std::string>& assignments) {
    std::vector<std::string> warnings;
    for (const std::string& a : assignments) {
        std::string err;
        if (!Cvars::SetFromAssignment(a, &err)) warnings.push_back("--cvar " + a + ": " + err);
    }
    return warnings;
}

std::vector<std::string> ApplyCvarArgs(const CommandLine& cl) { return ApplyCvarArgs(cl.GetAll("cvar")); }

namespace {

std::string Ms(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

}  // namespace

void RegisterStandard() {
    if (!Cvars::Find("game.speed")) {
        Cvar& c = Cvars::Float("game.speed", Time::BaseTimeScale(), "gameplay time scale (1 = normal, 0.5 = half speed)",
                               0.0f, 100.0f);
        c.OnChange([](const Cvar& v) { Time::SetTimeScale(v.AsFloat()); });
    }
    if (!Cvars::Find("r.vsync")) {
        Cvar& c = Cvars::Bool("r.vsync", DisplaySettings::VSync(), "wait for the display's refresh before showing a frame");
        c.OnChange([](const Cvar& v) { DisplaySettings::SetVSync(v.AsBool()); });
    }
    Cvars::Bool("debug.show_fps", false, "show the frames-per-second label in the corner");
    Cvars::Bool("debug.profile", false, "record profiler sections every frame (see prof.save, prof.sections)");

    Console::Register("prof.save", [](const std::vector<std::string>& a) -> std::string {
        if (a.empty()) return "error: usage: prof.save <file.json>";
        const Profiler& p = Prof();
        if (p.Count() == 0) return "error: no profiled frames yet (turn on debug.profile first)";
        if (!p.ExportChromeTraceToFile(a[0], 0, p.Count() - 1)) return "error: could not write " + a[0];
        return "wrote " + std::to_string(p.Count()) + " frames to " + a[0];
    }, "save the profiler's recorded frames as a Chrome trace", "prof.save <file.json>");
    Console::Register("prof.sections", [](const std::vector<std::string>&) -> std::string {
        const Profiler& p = Prof();
        if (p.Count() == 0) return "error: no profiled frames yet (turn on debug.profile first)";
        const Profiler::Frame& f = p.FrameAt(p.Count() - 1);
        std::string out = "frame " + Ms(f.total) + " ms, cpu " + Ms(f.cpu) + " ms";
        for (const Profiler::Bucket& b : f.buckets)
            out += "\n  " + std::string((std::size_t)(b.depth > 0 ? b.depth : 0) * 2, ' ') + b.name + "  " +
                   Ms(b.inclusive) + " ms  self " + Ms(b.self) + "  x" + std::to_string(b.calls);
        return out;
    }, "the newest profiled frame's sections (ms, self ms, calls)", "prof.sections");
}

void SyncFromEngine() {
    if (Cvar* c = Cvars::Find("game.speed")) {
        const float s = Time::BaseTimeScale();
        if (c->AsFloat() != s) c->SetFloat(s);
    }
    if (Cvar* c = Cvars::Find("r.vsync")) {
        const bool v = DisplaySettings::VSync();
        if (c->AsBool() != v) c->SetBool(v);
    }
}

bool ProfilerWanted(bool viewOpen) {
    if (viewOpen) return true;
    const Cvar* c = Cvars::Find("debug.profile");
    return c && c->AsBool();
}

bool PlayerConsoleAllowed(bool developmentBuild, bool consoleFlag, const char* envValue) {
    if (developmentBuild || consoleFlag) return true;
    return envValue && envValue[0] && !(envValue[0] == '0' && envValue[1] == '\0');
}

std::vector<std::string> TailLines(std::size_t maxLines) {
    const std::vector<std::string>& all = Console::Lines();
    const std::size_t from = all.size() > maxLines ? all.size() - maxLines : 0;
    return std::vector<std::string>(all.begin() + (std::ptrdiff_t)from, all.end());
}

}  // namespace consolehost
}  // namespace okay
