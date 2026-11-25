// The launcher's command line. See CommandLine.hpp. Our own code: word splitting, completion and history written for
// this page; programs start only through tools/okay-cli/Process.cpp; the command list is tools/okay-cli/CommandTable.cpp.
#include "CommandLine.hpp"

#include "CommandTable.hpp"   // tools/okay-cli: okay's commands and sub-commands

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace okaylcli {
namespace fs = std::filesystem;

// ---- words and arguments ------------------------------------------------------------------------------------------

bool SplitWords(const std::string& line, std::vector<std::string>& words, std::string& error) {
    words.clear();
    std::string cur;
    bool inWord = false;
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) {
            if (c == quote) { quote = 0; continue; }
            if (quote == '"' && c == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) {
                cur += line[++i];
                continue;
            }
            cur += c;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (inWord) { words.push_back(cur); cur.clear(); inWord = false; }
            continue;
        }
        inWord = true;
        if (c == '"' || c == '\'') { quote = c; continue; }
        cur += c;
    }
    if (quote) {
        error = std::string("A ") + (quote == '"' ? "double" : "single") + " quote is not closed.";
        words.clear();
        return false;
    }
    if (inWord) words.push_back(cur);
    return true;
}

namespace {
std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}
bool IsOkayWord(const std::string& w) {
    const std::string l = Lower(w);
    return l == "okay" || l == "okay.exe";
}
} // namespace

LineKind Classify(const std::string& line) {
    std::vector<std::string> words;
    std::string err;
    if (!SplitWords(line, words, err)) return LineKind::Okay;   // BuildArgv says what is wrong
    if (words.empty()) return LineKind::Empty;
    if (words.size() == 1 && (Lower(words[0]) == "clear" || Lower(words[0]) == "cls")) return LineKind::Clear;
    return LineKind::Okay;
}

bool BuildArgv(const std::string& okayExe, const std::string& line, std::vector<std::string>& argv, std::string& error) {
    argv.clear();
    if (okayExe.empty()) {
        error = "The okay program (okay.exe) was not found in the Tools folder. Reinstall or update to get it.";
        return false;
    }
    std::vector<std::string> words;
    if (!SplitWords(line, words, error)) return false;
    if (!words.empty() && IsOkayWord(words[0])) words.erase(words.begin());
    if (words.empty()) { error = "Type a command for okay, for example: help"; return false; }
    argv.push_back(okayExe);
    argv.insert(argv.end(), words.begin(), words.end());
    return true;
}

// ---- Tab completion -----------------------------------------------------------------------------------------------

Completion Complete(const std::string& line) {
    Completion c;
    c.line = line;
    // The words before the one being typed (plain spaces; quoted words never need completing here).
    std::vector<std::string> done;
    std::size_t start = 0;
    {
        std::size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && line[i] == ' ') ++i;
            if (i >= line.size()) { start = line.size(); break; }
            std::size_t j = i;
            while (j < line.size() && line[j] != ' ') ++j;
            if (j >= line.size()) { start = i; break; }   // the last word, still being typed
            done.push_back(line.substr(i, j - i));
            i = j;
            start = line.size();
        }
    }
    const std::string word = line.substr(start);
    if (!done.empty() && IsOkayWord(done[0])) done.erase(done.begin());
    std::vector<std::string> pool;
    if (done.empty()) {
        pool = okaycli::CommandNames();
        pool.push_back("clear");   // the page's own command
    } else if (done.size() == 1) {
        pool = done[0] == "help" ? okaycli::CommandNames() : okaycli::SubCommandNames(done[0]);
    }
    std::vector<std::string> fit;
    for (const std::string& p : pool)
        if (p.compare(0, word.size(), word) == 0) fit.push_back(p);
    if (fit.empty()) return c;
    if (fit.size() == 1) { c.line = line.substr(0, start) + fit[0] + " "; return c; }
    std::string common = fit[0];
    for (const std::string& f : fit) {
        std::size_t k = 0;
        while (k < common.size() && k < f.size() && common[k] == f[k]) ++k;
        common.resize(k);
    }
    c.line = line.substr(0, start) + common;
    c.choices = fit;
    return c;
}

// ---- history ------------------------------------------------------------------------------------------------------

void History::Push(const std::string& line) {
    if (line.empty()) { pos_ = lines_.size(); return; }
    if (lines_.empty() || lines_.back() != line) lines_.push_back(line);
    if (lines_.size() > limit_) lines_.erase(lines_.begin(), lines_.begin() + (long)(lines_.size() - limit_));
    pos_ = lines_.size();
    draft_.clear();
}

bool History::Up(const std::string& current, std::string& line) {
    if (lines_.empty() || pos_ == 0) return false;
    if (pos_ >= lines_.size()) { draft_ = current; pos_ = lines_.size(); }
    --pos_;
    line = lines_[pos_];
    return true;
}

bool History::Down(std::string& line) {
    if (pos_ >= lines_.size()) return false;
    ++pos_;
    line = pos_ == lines_.size() ? draft_ : lines_[pos_];
    return true;
}

// ---- a session ----------------------------------------------------------------------------------------------------

std::string CleanOutput(const std::string& text) {
    std::string o;
    o.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {   // a colour code: ESC [ digits ; ... letter
            std::size_t j = i + 2;
            while (j < text.size() && !((text[j] >= '@' && text[j] <= '~'))) ++j;
            i = j;
            continue;
        }
        if (c == '\r') continue;
        o += c;
    }
    return o;
}

void Session::RebuildText() {
    text_.clear();
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (i) text_ += '\n';
        text_ += lines_[i].text;
    }
}

void Session::AddLine(const std::string& text, OutLine::Kind kind) {
    OutLine l;
    l.text = text;
    l.kind = kind;
    lines_.push_back(l);
    if (lines_.size() > maxLines_) {
        lines_.erase(lines_.begin(), lines_.begin() + (long)(lines_.size() - maxLines_));
        RebuildText();
    } else {
        if (lines_.size() > 1) text_ += '\n';
        text_ += text;
    }
    ++revision_;
}

void Session::Feed(std::string& partial, const std::string& bytes, OutLine::Kind kind, bool flush) {
    partial += CleanOutput(bytes);
    std::size_t at = 0;
    for (;;) {
        const std::size_t nl = partial.find('\n', at);
        if (nl == std::string::npos) break;
        AddLine(partial.substr(at, nl - at), kind);
        at = nl + 1;
    }
    partial.erase(0, at);
    if (flush && !partial.empty()) { AddLine(partial, kind); partial.clear(); }
}

bool Session::Submit(const std::string& line) {
    history_.Push(line);
    switch (Classify(line)) {
    case LineKind::Empty: return false;
    case LineKind::Clear: Clear(); return true;
    case LineKind::Okay: break;
    }
    if (child_.Running()) { Note("A command is still running. Wait for it, or press Stop."); return false; }
    AddLine("> " + line, OutLine::Note);
    std::vector<std::string> argv;
    std::string error;
    if (!BuildArgv(okay_, line, argv, error)) { Note(error); return false; }
    if (!child_.Start(argv, folder_, error)) { Note(error); return false; }
    running_ = true;
    return true;
}

void Session::Poll() {
    if (!running_) return;
    std::string out, err;
    const bool still = child_.Poll(out, err);
    Feed(outPartial_, out, OutLine::Out, !still);
    Feed(errPartial_, err, OutLine::Err, !still);
    if (!still) {
        running_ = false;
        if (child_.Stopped()) Note("Stopped.");
        else if (child_.ExitCode() == 0) Note("Done.");
        else Note("Finished with exit code " + std::to_string(child_.ExitCode()) + ".");
    }
}

void Session::Stop() {
    if (child_.Running()) child_.Stop();
}

void Session::Clear() {
    lines_.clear();
    text_.clear();
    ++revision_;
}

// ---- a terminal window --------------------------------------------------------------------------------------------

std::vector<std::string> TerminalArgv() {
#if defined(_WIN32)
    // cmd.exe from the system folder, never one found on PATH or in the project. /K keeps the window open after the
    // greeting; each word is its own argument, so nothing needs quoting.
    wchar_t dir[MAX_PATH + 1] = {};
    const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n > MAX_PATH) return {};
    const std::string cmd = (fs::path(std::wstring(dir, n)) / "cmd.exe").u8string();
    return {cmd, "/K", "echo", "OkaySpace", "command", "line.", "Type", "okay", "help", "to", "see", "what", "it", "can", "do."};
#else
    for (const char* t : {"x-terminal-emulator", "gnome-terminal", "konsole", "xterm"}) {
        const std::string p = okaycli::FindOnPath(t);
        if (!p.empty()) return {p};
    }
    return {};
#endif
}

bool OpenTerminal(const std::string& folder, const std::string& toolsDir, std::string& error) {
    const std::vector<std::string> argv = TerminalArgv();
    if (argv.empty()) { error = "No terminal program was found on this computer."; return false; }
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(fs::u8path(folder), ec)) { error = "The project folder was not found."; return false; }
    okaycli::DetachOptions o;
    o.pathFirst = toolsDir;
    o.newConsole = true;
    o.title = "OkaySpace command line";
    if (!okaycli::StartDetached(argv, folder, o)) { error = "The terminal window could not be opened."; return false; }
    return true;
}

// ---- the page's layout --------------------------------------------------------------------------------------------

bool Overlap(const Rect& a, const Rect& b) {
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return false;
    return a.x < b.Right() && b.x < a.Right() && a.y < b.Bottom() && b.y < a.Bottom();
}

PageLayout LayoutPage(float w, float h, float frameH, float lineH) {
    PageLayout L;
    const float gap = 8.0f;
    // Top row, right to left: Clear, Copy all, Open command line; the project picker takes the rest.
    const float clearW = 64.0f, copyW = 84.0f, termW = 160.0f;
    float x = w;
    L.clear = {x - clearW, 0, clearW, frameH};
    x -= clearW + gap;
    L.copy = {x - copyW, 0, copyW, frameH};
    x -= copyW + gap;
    L.terminal = {x - termW, 0, termW, frameH};
    x -= termW + gap;
    L.project = {0, 0, std::max(0.0f, x), frameH};
    if (L.project.w < 160.0f) {   // narrow: the picker gets its own row above the buttons
        L.project = {0, 0, w, frameH};
        L.clear.y = L.copy.y = L.terminal.y = frameH + 6.0f;
    }
    const float topBottom = std::max(L.project.Bottom(), L.clear.Bottom());
    L.hint = {0, topBottom + 6.0f, w, lineH};
    // Bottom row: the line, then Run and Stop.
    const float runW = 64.0f, stopW = 64.0f;
    const float rowY = h - frameH;
    L.stop = {w - stopW, rowY, stopW, frameH};
    L.run = {w - stopW - gap - runW, rowY, runW, frameH};
    L.input = {0, rowY, std::max(0.0f, L.run.x - gap), frameH};
    const float outY = L.hint.Bottom() + 8.0f;
    L.output = {0, outY, w, std::max(0.0f, rowY - 8.0f - outY)};
    return L;
}

} // namespace okaylcli
