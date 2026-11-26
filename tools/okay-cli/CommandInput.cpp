// okay CLI: the rules of a typed command line. See CommandInput.hpp. Our own code (moved from launcher/CommandLine.cpp so
// the launcher's Command line page and the editor's Command Line panel share it); the command list is CommandTable.cpp.
#include "CommandInput.hpp"

#include "CommandTable.hpp"
#include "okay/Core/StringUtil.hpp"

namespace okaycli {

using okay::str::LowerAscii;   // engine/include/okay/Core/StringUtil.hpp: the one lower-case helper (header only)

// ---- words ---------------------------------------------------------------------------------------------------------

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

bool IsOkayWord(const std::string& word) {
    const std::string l = LowerAscii(word);
    return l == "okay" || l == "okay.exe";
}

LineKind Classify(const std::string& line) {
    std::vector<std::string> words;
    std::string err;
    if (!SplitWords(line, words, err)) return LineKind::Okay;   // the caller says what is wrong
    if (words.empty()) return LineKind::Empty;
    if (words.size() == 1 && (LowerAscii(words[0]) == "clear" || LowerAscii(words[0]) == "cls")) return LineKind::Clear;
    return LineKind::Okay;
}

// ---- Tab completion ------------------------------------------------------------------------------------------------

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
        pool = CommandNames();
        pool.push_back("clear");   // the window's own command
    } else if (done.size() == 1) {
        pool = done[0] == "help" ? CommandNames() : SubCommandNames(done[0]);
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

// ---- history -------------------------------------------------------------------------------------------------------

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

// ---- output lines --------------------------------------------------------------------------------------------------

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

void OutputLog::RebuildText() {
    text_.clear();
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        if (i) text_ += '\n';
        text_ += lines_[i].text;
    }
}

void OutputLog::Add(const std::string& text, OutLine::Kind kind) {
    OutLine l;
    l.text = text;
    l.kind = kind;
    lines_.push_back(l);
    if (lines_.size() > maxLines_) {
        // The oldest line goes: cut it off the front of the joined text too (its length and one newline), so a long
        // output does not rebuild the whole text for every new line.
        const std::size_t drop = lines_.size() - maxLines_;
        std::size_t cut = 0;
        for (std::size_t i = 0; i < drop; ++i) cut += lines_[i].text.size() + 1;
        lines_.erase(lines_.begin(), lines_.begin() + (long)drop);
        text_ += '\n';
        text_ += text;
        if (cut <= text_.size()) text_.erase(0, cut);
        else RebuildText();
    } else {
        if (lines_.size() > 1) text_ += '\n';
        text_ += text;
    }
    ++revision_;
}

void OutputLog::Feed(std::string& partial, const std::string& bytes, OutLine::Kind kind, bool flush) {
    partial += CleanOutput(bytes);
    std::size_t at = 0;
    for (;;) {
        const std::size_t nl = partial.find('\n', at);
        if (nl == std::string::npos) break;
        Add(partial.substr(at, nl - at), kind);
        at = nl + 1;
    }
    partial.erase(0, at);
    if (flush && !partial.empty()) { Add(partial, kind); partial.clear(); }
}

void OutputLog::Clear() {
    lines_.clear();
    text_.clear();
    ++revision_;
}

} // namespace okaycli
