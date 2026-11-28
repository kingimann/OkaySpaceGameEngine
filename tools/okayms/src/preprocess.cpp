// Comments out of the source text, and reading an included file.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"

namespace okayms {
namespace compiler {
// Raw string literals ([lex.string] 1, [lex.pptoken] 3.1: R"delim(...)delim", also after u8, u, U or L) written as
// ordinary string literals with escapes, before anything else reads the text (Round 33: they were not read at all, and
// SceneSerializerInternal.hpp and Browser/Script.cpp use them). The text between the parentheses is taken as it is: a
// backslash, a quote and a newline become \\, \" and \n. The newlines are put back after the literal, so every
// later line keeps its number. Comments, ordinary strings and character literals are passed over.
static bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
std::string lower_raw_strings(const std::string& in) {
    if (in.find("R\"") == std::string::npos) return in;
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        const char c = in[i];
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            const size_t end = in.find('\n', i);
            const size_t stop = end == std::string::npos ? in.size() : end;
            out.append(in, i, stop - i);
            i = stop;
            continue;
        }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            const size_t end = in.find("*/", i + 2);
            const size_t stop = end == std::string::npos ? in.size() : end + 2;
            out.append(in, i, stop - i);
            i = stop;
            continue;
        }
        if (c == 'R' && i + 1 < in.size() && in[i + 1] == '"') {
            // The prefix in front (u8, u, U, L) stays as written; it must start a token.
            size_t start = i;
            if (start >= 2 && in[start - 2] == 'u' && in[start - 1] == '8') start -= 2;
            else if (start >= 1 && (in[start - 1] == 'u' || in[start - 1] == 'U' || in[start - 1] == 'L')) start -= 1;
            const bool token_start = start == 0 || !ident_char(in[start - 1]);
            size_t open = i + 2;
            while (open < in.size() && open - (i + 2) <= 16 && in[open] != '(' && in[open] != '"' && in[open] != '\\' &&
                   in[open] != ' ' && in[open] != ')' && in[open] != '\n')
                ++open;
            if (token_start && open < in.size() && in[open] == '(') {
                const std::string delim = in.substr(i + 2, open - (i + 2));
                const std::string close = ")" + delim + "\"";
                const size_t end = in.find(close, open + 1);
                if (end != std::string::npos) {
                    out.push_back('"');
                    int newlines = 0;
                    for (size_t k = open + 1; k < end; ++k) {
                        const char ch = in[k];
                        if (ch == '\\') out += "\\\\";
                        else if (ch == '"') out += "\\\"";
                        else if (ch == '\n') {
                            out += "\\n";
                            ++newlines;
                        } else if (ch == '\r') {
                            out += "\\r";
                        } else {
                            out.push_back(ch);
                        }
                    }
                    out.push_back('"');
                    out.append(static_cast<size_t>(newlines), '\n');
                    i = end + close.size();
                    continue;
                }
            }
        }
        if ((c == '"' || c == '\'') && !is_digit_separator(in, i)) {
            out.push_back(in[i++]);
            while (i < in.size() && in[i] != c && in[i] != '\n') {
                if (in[i] == '\\' && i + 1 < in.size()) out.push_back(in[i++]);
                out.push_back(in[i++]);
            }
            if (i < in.size()) out.push_back(in[i++]);
            continue;
        }
        out.push_back(in[i++]);
    }
    return out;
}

std::string strip_comments(const std::string& raw_in) {
    const std::string in = lower_raw_strings(raw_in);
    std::string out;
    for (size_t i = 0; i < in.size();) {
        if ((in[i] == '"' || in[i] == '\'') && !is_digit_separator(in, i)) {
            const char quote = in[i];
            out.push_back(in[i++]);
            while (i < in.size() && in[i] != quote) {
                if (in[i] == '\\' && i + 1 < in.size()) out.push_back(in[i++]);
                out.push_back(in[i++]);
            }
            if (i < in.size()) out.push_back(in[i++]);
            continue;
        }
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            i += 2;
            while (i < in.size() && in[i] != '\n') ++i;
            continue;
        }
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            i += 2;
            // Keep the newlines so a line number still names its source line.
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) {
                if (in[i] == '\n') out.push_back('\n');
                ++i;
            }
            if (i + 1 < in.size()) i += 2;
            continue;
        }
        out.push_back(in[i++]);
    }
    return out;
}

bool read_file(const std::string& path, std::string* text) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    text->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}


}  // namespace compiler
}  // namespace okayms
