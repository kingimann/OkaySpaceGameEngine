#pragma once
// Greedy word wrap, shared by Game UI (TextRenderer's wrap, the item tooltip card) and OkayUI's wrapped text.
// Written from the usual greedy line-filling method: put each word on the current line while the line still fits,
// otherwise start a new line with it. The caller says how wide a piece of text is (pixels, characters, ...).
#include <string>
#include <vector>

namespace okay {
namespace textwrap {

struct Options {
    /// true: a run of spaces between two words becomes one space and spaces at a line start are dropped.
    /// false: every space is kept while the line fits (OkayUI's wrapped text); spaces at a break are dropped.
    bool collapseSpaces = true;
    /// Also split words at tabs (TextRenderer does; OkayUI keeps a tab inside the word).
    bool tabsSeparate = false;
};

/// Wrap `text` into `lines` (appended). '\n' always ends a line, even an empty one. A line breaks before the word
/// that would make `measure(line)` exceed `maxWidth`; a word wider than `maxWidth` on its own stays whole.
/// `measure(const std::string&) -> float`. Returns true when the last line ended at a '\n' (nothing but spaces
/// after it), so a caller that joins the lines with '\n' can add the final one back.
template <class Measure>
bool Greedy(const std::string& text, float maxWidth, Measure&& measure, std::vector<std::string>& lines,
            const Options& opt = {}) {
    auto isSpace = [&](char c) { return c == ' ' || (opt.tabsSeparate && c == '\t'); };
    std::string line, trial;
    bool endedAtNewline = false;
    std::size_t p = 0;
    const std::size_t n = text.size();
    while (p < n) {
        std::size_t we = p;
        while (we < n && !isSpace(text[we]) && text[we] != '\n') ++we;
        if (we > p || !opt.collapseSpaces) {
            trial.assign(line);
            if (!line.empty()) trial += ' ';
            trial.append(text, p, we - p);
            if (!line.empty() && measure(trial) > maxWidth) {
                lines.push_back(line);
                line.clear();
                endedAtNewline = false;
                continue;   // the same word again, at the start of the new line
            }
            line.swap(trial);
            if (we > p) endedAtNewline = false;
        }
        p = we;
        if (p < n && text[p] == '\n') {
            lines.push_back(line);
            line.clear();
            endedAtNewline = true;
            ++p;
        } else if (p < n) {
            ++p;   // the space after the word
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
        endedAtNewline = false;
    }
    return endedAtNewline;
}

} // namespace textwrap
} // namespace okay
