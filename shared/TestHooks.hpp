#pragma once
// Scripted input and screenshots for the launcher and the editor, for tests that run them headless (no xdotool): the program reads a
// script file named by OKAY_TEST_SCRIPT and, at the times in it, PUSHES platform events into its own event queue (okay::platform::
// Backend::PushEvent), so every click, key and drop travels the same loop as real input, on either backend. It is compiled in only
// when OKAY_ENABLE_TEST_HOOKS is defined (CMake option OKAY_ENABLE_TEST_HOOKS, default OFF); a normal build has none of it.
//
// Script: one command per line, `#` starts a comment. `<ms> <command> [arguments]`, the time in milliseconds since the first frame:
//   move X Y             pointer to X, Y (window pixels)
//   click X Y [button]   move, press and release (button 1 left (default), 2 middle, 3 right)
//   down X Y [button] / up X Y [button]
//   wheel DY             mouse wheel, up = positive
//   type TEXT            text input (the rest of the line, UTF-8)
//   key NAME [down|up]   a key tap (or just the press / release); names: Return Escape Backspace Tab Space Left Right Up Down Home End
//                        Delete PageUp PageDown F1..F12, A..Z, 0..9; with `ctrl+` / `shift+` / `alt+` in front for the modifier
//   drop PATH            a file dropped on the window
//   quit                 the window system's quit request (EventType::Quit)
//   close                the window's close button (EventType::WindowClose)
//   shot PATH            save the frame just drawn as a PNG (taken before the present, so it is what the window would show)
//   exit [CODE]          leave the program's main loop at once (QuitNow()), for a script that does not want to click through a dialog
// The parser (ParseScript) is plain C++ and tested on its own (tests/test_test_hooks.cpp).
#include "okay/Platform/Platform.hpp"
#include "okay/Platform/Painter.hpp"
#include "okay/Graphics/Image.hpp"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace okay::testhooks {

struct Command {
    std::uint64_t atMs = 0;
    std::string verb;               // move click down up wheel type key drop quit close shot exit
    int x = 0, y = 0, button = 1;   // pointer commands; wheel uses y
    int key = 0; std::uint32_t mods = 0; bool keyDown = true, keyUp = true;
    std::string text;               // type text, drop / shot path
    int code = 0;                   // exit code
    int line = 0;
};

/// A key name to a platform key number (USB HID usage numbering); 0 when unknown.
inline int KeyFromName(const std::string& n) {
    using K = okay::platform::Key;
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return (int)K::A + (n[0] - 'a');
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return (int)K::A + (n[0] - 'A');
    if (n.size() == 1 && n[0] >= '1' && n[0] <= '9') return (int)K::N1 + (n[0] - '1');
    if (n == "0") return (int)K::N0;
    if (n.size() >= 2 && (n[0] == 'F' || n[0] == 'f')) { const int f = std::atoi(n.c_str() + 1); if (f >= 1 && f <= 12) return (int)K::F1 + f - 1; }
    static const struct { const char* n; K k; } t[] = {{"Return", K::Return}, {"Enter", K::Return}, {"Escape", K::Escape}, {"Esc", K::Escape}, {"Backspace", K::Backspace},
        {"Tab", K::Tab}, {"Space", K::Space}, {"Left", K::Left}, {"Right", K::Right}, {"Up", K::Up}, {"Down", K::Down}, {"Home", K::Home}, {"End", K::End},
        {"Delete", K::Delete}, {"PageUp", K::PageUp}, {"PageDown", K::PageDown}};
    for (const auto& e : t) if (n == e.n) return (int)e.k;
    return 0;
}

/// Parse a script. A bad line is reported in `errors` ("line N: why") and skipped. Commands come back sorted by time (stable).
inline std::vector<Command> ParseScript(const std::string& text, std::vector<std::string>* errors = nullptr) {
    std::vector<Command> out;
    std::istringstream in(text);
    std::string line;
    int ln = 0;
    auto bad = [&](const std::string& why) { if (errors) errors->push_back("line " + std::to_string(ln) + ": " + why); };
    while (std::getline(in, line)) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        { size_t f = line.find_first_not_of(" \t"); if (f != std::string::npos && line[f] == '#') line.clear(); }   // a comment is a line that starts with #; a typed "#1" survives
        std::istringstream ls(line);
        std::string t0, verb;
        if (!(ls >> t0)) continue;
        Command c; c.line = ln;
        char* e = nullptr;
        const long long ms = std::strtoll(t0.c_str(), &e, 10);
        if (*e || ms < 0) { bad("the time must be a number of milliseconds"); continue; }
        c.atMs = (std::uint64_t)ms;
        if (!(ls >> verb)) { bad("no command"); continue; }
        c.verb = verb;
        if (verb == "move" || verb == "click" || verb == "down" || verb == "up") {
            if (!(ls >> c.x >> c.y)) { bad(verb + " needs X Y"); continue; }
            if (verb != "move" && (ls >> c.button) && (c.button < 1 || c.button > 5)) { bad("button must be 1 to 5"); continue; }
        } else if (verb == "wheel") {
            if (!(ls >> c.y)) { bad("wheel needs a number"); continue; }
        } else if (verb == "type" || verb == "drop" || verb == "shot") {
            std::string rest; std::getline(ls, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            if (rest.empty()) { bad(verb + " needs an argument"); continue; }
            c.text = rest;
        } else if (verb == "key") {
            std::string name, mode;
            if (!(ls >> name)) { bad("key needs a name"); continue; }
            for (;;) {   // modifier prefixes
                if (name.rfind("ctrl+", 0) == 0) { c.mods |= okay::platform::ModCtrl; name.erase(0, 5); }
                else if (name.rfind("shift+", 0) == 0) { c.mods |= okay::platform::ModShift; name.erase(0, 6); }
                else if (name.rfind("alt+", 0) == 0) { c.mods |= okay::platform::ModAlt; name.erase(0, 4); }
                else break;
            }
            c.key = KeyFromName(name);
            if (!c.key) { bad("unknown key '" + name + "'"); continue; }
            if (ls >> mode) { if (mode == "down") c.keyUp = false; else if (mode == "up") c.keyDown = false; else { bad("key mode is down or up"); continue; } }
        } else if (verb == "exit") {
            if (ls >> c.code) {}
        } else if (verb != "quit" && verb != "close") { bad("unknown command '" + verb + "'"); continue; }
        out.push_back(c);
    }
    // stable sort by time
    for (size_t i = 1; i < out.size(); ++i)
        for (size_t j = i; j > 0 && out[j].atMs < out[j - 1].atMs; --j) std::swap(out[j], out[j - 1]);
    return out;
}

/// Turn one command into the platform events to push (not `shot` and `exit`: the app handles those). `windowId` is the main window's Id().
inline std::vector<okay::platform::Event> ToEvents(const Command& c, std::uint32_t windowId) {
    using namespace okay::platform;
    std::vector<Event> v;
    auto mk = [&](EventType t) { Event e; e.type = t; e.windowId = windowId; return e; };
    if (c.verb == "move" || c.verb == "click" || c.verb == "down" || c.verb == "up") {
        Event m = mk(EventType::MouseMove); m.x = c.x; m.y = c.y; v.push_back(m);
        if (c.verb != "move") {
            if (c.verb != "up") { Event d = mk(EventType::MouseButtonDown); d.button = c.button; d.clicks = 1; d.x = c.x; d.y = c.y; v.push_back(d); }
            if (c.verb != "down") { Event u = mk(EventType::MouseButtonUp); u.button = c.button; u.clicks = 1; u.x = c.x; u.y = c.y; v.push_back(u); }
        }
    } else if (c.verb == "wheel") { Event w = mk(EventType::MouseWheel); w.wheelY = (float)c.y; v.push_back(w); }
    else if (c.verb == "type") { Event t = mk(EventType::TextInput); t.text = c.text; v.push_back(t); }
    else if (c.verb == "key") {
        if (c.keyDown) { Event d = mk(EventType::KeyDown); d.key = (Key)c.key; d.mods = c.mods; v.push_back(d); }
        if (c.keyUp) { Event u = mk(EventType::KeyUp); u.key = (Key)c.key; u.mods = c.mods; v.push_back(u); }
    } else if (c.verb == "drop") { Event d = mk(EventType::DropFile); d.text = c.text; v.push_back(d); }
    else if (c.verb == "quit") { Event q; q.type = EventType::Quit; v.push_back(q); }
    else if (c.verb == "close") { v.push_back(mk(EventType::WindowClose)); }
    return v;
}

/// Save what the painter drew this frame as a PNG (RGBA read-back). False when the painter cannot read pixels or the file cannot be written.
inline bool SavePicture(okay::platform::Painter& p, const std::string& path) {
    std::vector<std::uint8_t> px; int w = 0, h = 0;
    if (!p.ReadPixels(px, w, h) || w <= 0 || h <= 0) return false;
    okay::Image img(w, h);
    std::memcpy(img.Data(), px.data(), (std::size_t)w * (std::size_t)h * 4);
    return img.SavePNG(path);
}

/// The runner an app owns. Call Frame(...) once per frame before it polls events, and Shots(...) after the frame is drawn, before the present.
class Runner {
public:
    /// Read OKAY_TEST_SCRIPT. False (and nothing happens) when it is not set or the file cannot be read.
    bool Start(okay::platform::Backend* backend, std::uint32_t windowId) {
        const char* path = std::getenv("OKAY_TEST_SCRIPT");
        if (!path || !*path) return false;
        std::ifstream f(path);
        if (!f) { std::fprintf(stderr, "[test-hook] cannot read OKAY_TEST_SCRIPT=%s\n", path); return false; }
        std::stringstream ss; ss << f.rdbuf();
        std::vector<std::string> errs;
        m_cmds = ParseScript(ss.str(), &errs);
        for (const std::string& e : errs) std::fprintf(stderr, "[test-hook] %s\n", e.c_str());
        m_backend = backend; m_win = windowId; m_active = true; m_start = (std::uint64_t)okay::platform::TicksMs();
        std::fprintf(stderr, "[test-hook] %zu commands from %s\n", m_cmds.size(), path);
        return true;
    }
    bool Active() const { return m_active; }
    /// Push the events of every command that is due. Returns the `shot` commands that are due (the app saves them after drawing).
    void Frame() {
        if (!m_active || !m_backend) return;
        const std::uint64_t now = (std::uint64_t)okay::platform::TicksMs() - m_start;
        while (m_next < m_cmds.size() && m_cmds[m_next].atMs <= now) {
            const Command& c = m_cmds[m_next++];
            if (c.verb == "shot") m_shots.push_back(c.text);
            else if (c.verb == "exit") { m_quit = true; m_code = c.code; }
            else for (const okay::platform::Event& e : ToEvents(c, m_win)) m_backend->PushEvent(e);
            std::fprintf(stderr, "[test-hook] %llu ms: %s\n", (unsigned long long)now, c.verb.c_str());
        }
    }
    /// The picture paths asked for since the last call.
    std::vector<std::string> TakeShots() { std::vector<std::string> v; v.swap(m_shots); return v; }
    bool QuitNow() const { return m_quit; }
    int ExitCode() const { return m_code; }
    bool Finished() const { return m_next >= m_cmds.size(); }
private:
    okay::platform::Backend* m_backend = nullptr;
    std::uint32_t m_win = 0;
    std::vector<Command> m_cmds;
    std::size_t m_next = 0;
    std::vector<std::string> m_shots;
    std::uint64_t m_start = 0;
    bool m_active = false, m_quit = false;
    int m_code = 0;
};

} // namespace okay::testhooks
