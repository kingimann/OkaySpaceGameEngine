#pragma once
// The preprocessor: macros, #include, #if and the line origin table ([cpp]).
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"

namespace okayms {
namespace compiler {
struct Macro {
    bool function_like = false;
    std::vector<std::string> params;
    std::string body;
};

struct Preprocessor {
    std::map<std::string, Macro> macros;
    std::vector<std::string> includes;
    std::string error;
    int depth = 0;
    // File names (index 0 is the main source) and, for each line of the
    // output, the file and line it came from.
    std::vector<std::string> files;
    std::vector<std::pair<int, int>> origin;

    bool defined(const std::string& name) const { return macros.find(name) != macros.end(); }

    // Integer constant expression for #if / #elif. An undefined name is 0.
    // defined(name) is replaced before macro expansion, as C requires.
    int64_t eval_pp(const std::string& raw, bool* ok) {
        *ok = true;
        std::string step;
        for (size_t i = 0; i < raw.size();) {
            const unsigned char c = static_cast<unsigned char>(raw[i]);
            if (std::isalpha(c) || c == '_') {
                size_t j = i + 1;
                while (j < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[j])) || raw[j] == '_')) ++j;
                const std::string word = raw.substr(i, j - i);
                if (word == "defined") {
                    while (j < raw.size() && std::isspace(static_cast<unsigned char>(raw[j]))) ++j;
                    bool paren = false;
                    if (j < raw.size() && raw[j] == '(') {
                        paren = true;
                        ++j;
                        while (j < raw.size() && std::isspace(static_cast<unsigned char>(raw[j]))) ++j;
                    }
                    size_t k = j;
                    while (k < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[k])) || raw[k] == '_')) ++k;
                    const std::string name = raw.substr(j, k - j);
                    if (paren) {
                        while (k < raw.size() && std::isspace(static_cast<unsigned char>(raw[k]))) ++k;
                        if (k < raw.size() && raw[k] == ')') ++k;
                    }
                    step += defined(name) ? "1" : "0";
                    i = k;
                    continue;
                }
                step += word;
                i = j;
                continue;
            }
            step.push_back(raw[i++]);
        }
        const std::string expanded = expand(step, 0);
        if (!error.empty()) {
            *ok = false;
            return 0;
        }
        struct Tok {
            int kind = 0;
            int64_t num = 0;
            std::string text;
        };
        std::vector<Tok> toks;
        for (size_t i = 0; i < expanded.size();) {
            const unsigned char c = static_cast<unsigned char>(expanded[i]);
            if (std::isspace(c)) {
                ++i;
                continue;
            }
            if (std::isalpha(c) || c == '_') {
                size_t j = i + 1;
                while (j < expanded.size() && (std::isalnum(static_cast<unsigned char>(expanded[j])) || expanded[j] == '_')) ++j;
                Tok tok;
                tok.kind = 2;
                tok.text = expanded.substr(i, j - i);
                toks.push_back(tok);
                i = j;
                continue;
            }
            if (std::isdigit(c)) {
                int base = 10;
                size_t j = i;
                if (expanded[i] == '0' && j + 1 < expanded.size() && (expanded[j + 1] == 'x' || expanded[j + 1] == 'X')) {
                    base = 16;
                    j += 2;
                }
                int64_t value = 0;
                bool any = false;
                while (j < expanded.size()) {
                    const char d = expanded[j];
                    int digit = -1;
                    if (d >= '0' && d <= '9') digit = d - '0';
                    else if (base == 16 && d >= 'a' && d <= 'f') digit = d - 'a' + 10;
                    else if (base == 16 && d >= 'A' && d <= 'F') digit = d - 'A' + 10;
                    else break;
                    if (digit >= base) break;
                    any = true;
                    value = value * base + digit;
                    ++j;
                }
                if (!any) {
                    *ok = false;
                    return 0;
                }
                // An integer suffix (201611L, 1u, 2ULL) does not change the value here ([cpp.cond] 12, [lex.icon]).
                while (j < expanded.size() && (expanded[j] == 'u' || expanded[j] == 'U' || expanded[j] == 'l' ||
                                               expanded[j] == 'L'))
                    ++j;
                Tok tok;
                tok.kind = 1;
                tok.num = value;
                toks.push_back(tok);
                i = j;
                continue;
            }
            Tok tok;
            tok.kind = 3;
            tok.text.assign(1, expanded[i]);
            if (i + 1 < expanded.size()) {
                const char n = expanded[i + 1];
                const char f = expanded[i];
                if ((f == '&' && n == '&') || (f == '|' && n == '|') || (f == '=' && n == '=') || (f == '!' && n == '=') ||
                    (f == '<' && n == '=') || (f == '>' && n == '=') || (f == '<' && n == '<') || (f == '>' && n == '>')) {
                    tok.text.push_back(n);
                    ++i;
                }
            }
            ++i;
            toks.push_back(tok);
        }
        Tok end;
        toks.push_back(end);
        struct Ev {
            const std::vector<Tok>& t;
            size_t at = 0;
            bool* ok = nullptr;
            const Tok& cur() const { return t[at]; }
            void next() { if (at + 1 < t.size()) ++at; }
            int64_t primary() {
                if (cur().kind == 1) {
                    const int64_t value = cur().num;
                    next();
                    return value;
                }
                if (cur().kind == 2) {
                    next();
                    return 0;
                }
                if (cur().kind == 3 && cur().text == "(") {
                    next();
                    const int64_t value = expr();
                    if (cur().kind != 3 || cur().text != ")") *ok = false;
                    else next();
                    return value;
                }
                if (cur().kind == 3 && (cur().text == "!" || cur().text == "-" || cur().text == "+" || cur().text == "~")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t value = primary();
                    if (op == "!") return value == 0 ? 1 : 0;
                    if (op == "~") return ~value;
                    if (op == "-") return -value;
                    return value;
                }
                *ok = false;
                return 0;
            }
            int64_t mul() {
                int64_t left = primary();
                while (cur().kind == 3 && (cur().text == "*" || cur().text == "/" || cur().text == "%")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = primary();
                    if (op != "*" && right == 0) {
                        *ok = false;
                        return 0;
                    }
                    if (op == "*") left *= right;
                    else if (op == "/") left /= right;
                    else left %= right;
                }
                return left;
            }
            int64_t add() {
                int64_t left = mul();
                while (cur().kind == 3 && (cur().text == "+" || cur().text == "-")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = mul();
                    if (op == "+") left += right;
                    else left -= right;
                }
                return left;
            }
            int64_t shift() {
                int64_t left = add();
                while (cur().kind == 3 && (cur().text == "<<" || cur().text == ">>")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = add();
                    if (right < 0 || right > 63) {
                        *ok = false;
                        return 0;
                    }
                    if (op == "<<") left = static_cast<int64_t>(static_cast<uint64_t>(left) << right);
                    else left = left >> right;
                }
                return left;
            }
            int64_t rel() {
                int64_t left = shift();
                while (cur().kind == 3 && (cur().text == "<" || cur().text == ">" || cur().text == "<=" || cur().text == ">=")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = shift();
                    if (op == "<") left = left < right;
                    else if (op == ">") left = left > right;
                    else if (op == "<=") left = left <= right;
                    else left = left >= right;
                }
                return left;
            }
            int64_t eq() {
                int64_t left = rel();
                while (cur().kind == 3 && (cur().text == "==" || cur().text == "!=")) {
                    const std::string op = cur().text;
                    next();
                    const int64_t right = rel();
                    left = op == "==" ? left == right : left != right;
                }
                return left;
            }
            int64_t band() {
                int64_t left = eq();
                while (cur().kind == 3 && cur().text == "&") {
                    next();
                    left &= eq();
                }
                return left;
            }
            int64_t bxor() {
                int64_t left = band();
                while (cur().kind == 3 && cur().text == "^") {
                    next();
                    left ^= band();
                }
                return left;
            }
            int64_t bor() {
                int64_t left = bxor();
                while (cur().kind == 3 && cur().text == "|") {
                    next();
                    left |= bxor();
                }
                return left;
            }
            int64_t and_() {
                int64_t left = bor();
                while (cur().kind == 3 && cur().text == "&&") {
                    next();
                    const int64_t right = bor();
                    left = (left != 0 && right != 0) ? 1 : 0;
                }
                return left;
            }
            int64_t lor() {
                int64_t left = and_();
                while (cur().kind == 3 && cur().text == "||") {
                    next();
                    const int64_t right = and_();
                    left = (left != 0 || right != 0) ? 1 : 0;
                }
                return left;
            }
            int64_t expr() {
                const int64_t cond = lor();
                if (cur().kind == 3 && cur().text == "?") {
                    next();
                    const int64_t yes = expr();
                    if (cur().kind != 3 || cur().text != ":") {
                        *ok = false;
                        return 0;
                    }
                    next();
                    const int64_t no = expr();
                    return cond != 0 ? yes : no;
                }
                return cond;
            }
        };
        Ev ev{toks, 0, ok};
        const int64_t value = ev.expr();
        if (ev.cur().kind != 0) *ok = false;
        return value;
    }

    // The line of the file that is being expanded: what __LINE__ stands for.
    int current_line = 0;
    std::string expand(const std::string& line, int nest) {
        if (nest > 20) {
            error = "macro expansion is too deep";
            return line;
        }
        std::string out;
        bool in_string = false;
        char quote = 0;
        for (size_t i = 0; i < line.size();) {
            const char c = line[i];
            if (in_string) {
                out.push_back(c);
                if (c == '\\' && i + 1 < line.size()) out.push_back(line[++i]);
                else if (c == quote) in_string = false;
                ++i;
                continue;
            }
            if ((c == '"' || c == '\'') && !is_digit_separator(line, i)) {
                in_string = true;
                quote = c;
                out.push_back(c);
                ++i;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                size_t j = i + 1;
                while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
                const std::string word = line.substr(i, j - i);
                if (word == "__LINE__" && macros.find(word) == macros.end()) {
                    out += std::to_string(current_line);
                    i = j;
                    continue;
                }
                const auto it = macros.find(word);
                if (it == macros.end()) {
                    out += word;
                    i = j;
                    continue;
                }
                if (!it->second.function_like) {
                    std::string body = it->second.body;
                    // a ## b in an object-like macro joins the two pieces ([cpp.concat])
                    for (size_t h = body.find("##"); h != std::string::npos; h = body.find("##", h)) {
                        size_t a = h, b = h + 2;
                        while (a > 0 && std::isspace(static_cast<unsigned char>(body[a - 1]))) --a;
                        while (b < body.size() && std::isspace(static_cast<unsigned char>(body[b]))) ++b;
                        body.erase(a, b - a);
                        h = a;
                    }
                    out += expand(body, nest + 1);
                    i = j;
                    continue;
                }
                size_t k = j;
                while (k < line.size() && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
                if (k >= line.size() || line[k] != '(') {
                    out += word;
                    i = j;
                    continue;
                }
                ++k;
                std::vector<std::string> args;
                std::string cur_arg;
                int depth_paren = 0;
                bool in_arg_string = false;
                char arg_quote = 0;
                for (; k < line.size(); ++k) {
                    const char ch = line[k];
                    if (in_arg_string) {
                        cur_arg.push_back(ch);
                        if (ch == '\\' && k + 1 < line.size()) cur_arg.push_back(line[++k]);
                        else if (ch == arg_quote) in_arg_string = false;
                        continue;
                    }
                    if ((ch == '"' || ch == '\'') && !is_digit_separator(line, k)) {
                        in_arg_string = true;
                        arg_quote = ch;
                        cur_arg.push_back(ch);
                        continue;
                    }
                    if (ch == '(') {
                        ++depth_paren;
                        cur_arg.push_back(ch);
                        continue;
                    }
                    if (ch == ')' && depth_paren == 0) {
                        args.push_back(cur_arg);
                        ++k;
                        break;
                    }
                    if (ch == ')' ) {
                        --depth_paren;
                        cur_arg.push_back(ch);
                        continue;
                    }
                    if (ch == ',' && depth_paren == 0) {
                        args.push_back(cur_arg);
                        cur_arg.clear();
                        continue;
                    }
                    cur_arg.push_back(ch);
                }
                {
                    // The arguments from the variadic parameter on are one argument, commas kept ([cpp.replace] 12).
                    const std::vector<std::string>& ps = it->second.params;
                    if (!ps.empty() && ps.back() == "__VA_ARGS__" && args.size() >= ps.size()) {
                        std::string joined = args[ps.size() - 1];
                        for (size_t a = ps.size(); a < args.size(); ++a) joined += "," + args[a];
                        args.resize(ps.size());
                        args.back() = joined;
                    } else if (!ps.empty() && ps.back() == "__VA_ARGS__" && args.size() + 1 == ps.size()) {
                        args.push_back(std::string());
                    }
                }
                // Every parameter is replaced in one pass over the body, so an argument that happens to contain another
                // parameter's name (CHECK_NEAR(f(a, b), 1, 2) with parameters a and b) is not substituted a second time.
                // An argument is macro-expanded before it goes in ([cpp.subst] 1), except next to # and ## where its
                // spelling is used: #p makes a string literal of it ([cpp.stringize]) and a ## b joins the two pieces
                // into one token ([cpp.concat]). The result is scanned again for macros.
                std::string substituted;
                {
                    const std::string& body = it->second.body;
                    const std::vector<std::string>& ps = it->second.params;
                    auto param_index = [&](const std::string& w) -> int {
                        for (size_t p = 0; p < ps.size() && p < args.size(); ++p)
                            if (ps[p] == w) return static_cast<int>(p);
                        return -1;
                    };
                    auto trim = [](const std::string& t) {
                        size_t a = 0, b = t.size();
                        while (a < b && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
                        while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1]))) --b;
                        return t.substr(a, b - a);
                    };
                    bool paste_next = false;  // the word after a ## is used as written
                    for (size_t s = 0; s < body.size();) {
                        const char bc = body[s];
                        if ((bc == '"' || bc == '\'') && !is_digit_separator(body, s)) {
                            // a literal in the body is copied as it is
                            size_t e = s + 1;
                            while (e < body.size() && body[e] != bc) e += body[e] == '\\' ? 2 : 1;
                            substituted += body.substr(s, std::min(e + 1, body.size()) - s);
                            s = e + 1;
                            continue;
                        }
                        if (bc == '#' && s + 1 < body.size() && body[s + 1] == '#') {
                            while (!substituted.empty() && std::isspace(static_cast<unsigned char>(substituted.back())))
                                substituted.pop_back();
                            s += 2;
                            while (s < body.size() && std::isspace(static_cast<unsigned char>(body[s]))) ++s;
                            paste_next = true;
                            continue;
                        }
                        if (bc == '#') {
                            size_t w = s + 1;
                            while (w < body.size() && std::isspace(static_cast<unsigned char>(body[w]))) ++w;
                            size_t e = w;
                            while (e < body.size() && (std::isalnum(static_cast<unsigned char>(body[e])) || body[e] == '_')) ++e;
                            const int p = param_index(body.substr(w, e - w));
                            if (p >= 0) {
                                const std::string text = trim(args[static_cast<size_t>(p)]);
                                substituted.push_back('"');
                                for (char ch : text) {
                                    if (ch == '"' || ch == '\\') substituted.push_back('\\');
                                    substituted.push_back(ch);
                                }
                                substituted.push_back('"');
                                s = e;
                                continue;
                            }
                        }
                        if (std::isalpha(static_cast<unsigned char>(bc)) || bc == '_') {
                            size_t e = s + 1;
                            while (e < body.size() && (std::isalnum(static_cast<unsigned char>(body[e])) || body[e] == '_')) ++e;
                            const std::string word = body.substr(s, e - s);
                            const int p = param_index(word);
                            if (p < 0) {
                                substituted += word;
                            } else {
                                size_t after = e;
                                while (after < body.size() && std::isspace(static_cast<unsigned char>(body[after]))) ++after;
                                const bool pasted = paste_next || (after + 1 < body.size() && body[after] == '#' &&
                                                                   body[after + 1] == '#');
                                substituted += pasted ? trim(args[static_cast<size_t>(p)])
                                                      : expand(args[static_cast<size_t>(p)], nest + 1);
                            }
                            paste_next = false;
                            s = e;
                            continue;
                        }
                        if (!std::isspace(static_cast<unsigned char>(bc))) paste_next = false;
                        substituted.push_back(body[s++]);
                    }
                }
                out += expand(substituted, nest + 1);
                i = k;
                continue;
            }
            out.push_back(c);
            ++i;
        }
        return out;
    }

    // Files named by '#pragma once' (by the path they were found at).
    std::set<std::string> once_files;
    // The path of each file being read, innermost last (the main source has none).
    std::vector<std::string> path_stack;

    // True when the line has a function-like macro name followed by '(' whose ')' is not on the line (string and
    // character literals are skipped).
    bool open_macro_call(const std::string& line) const {
        int depth = 0;
        bool in_call = false;
        for (size_t k = 0; k < line.size();) {
            const char c = line[k];
            if (c == '"' || c == '\'') {
                const char q = c;
                ++k;
                while (k < line.size() && line[k] != q) k += line[k] == '\\' ? 2 : 1;
                ++k;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                size_t e = k;
                while (e < line.size() && (std::isalnum(static_cast<unsigned char>(line[e])) || line[e] == '_')) ++e;
                if (!in_call) {
                    const auto found = macros.find(line.substr(k, e - k));
                    if (found != macros.end() && found->second.function_like) {
                        size_t p = e;
                        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
                        if (p < line.size() && line[p] == '(') {
                            in_call = true;
                            depth = 0;
                        }
                    }
                }
                k = e;
                continue;
            }
            if (in_call && c == '(') ++depth;
            if (in_call && c == ')' && --depth == 0) in_call = false;
            ++k;
        }
        return in_call && depth > 0;
    }

    // Quoted names are looked up next to the including file first ([cpp.include]), then in the
    // include paths. *found is the path the text was read from.
    std::string find_include(const std::string& name, bool quote, const std::string& dir, std::string* found) {
        if (quote && !dir.empty()) {
            const std::string path = dir + "/" + name;
            std::string text;
            if (read_file(path, &text)) {
                *found = path;
                return text;
            }
        }
        for (const std::string& inc : includes) {
            const std::string path = inc + "/" + name;
            std::string text;
            if (read_file(path, &text)) {
                *found = path;
                return text;
            }
        }
        return "";
    }

    // A backslash directly before a newline joins the two lines ([lex.phases] 2). The joined line is
    // followed by one empty line for each line it took in, so the line count stays the same.
    static std::string splice_lines(const std::string& in) {
        if (in.find('\\') == std::string::npos) return in;
        std::string out;
        size_t i = 0;
        int pending = 0;
        while (i < in.size()) {
            if (in[i] == '\\' && i + 1 < in.size() && in[i + 1] == '\n') {
                i += 2;
                ++pending;
                continue;
            }
            if (in[i] == '\\' && i + 2 < in.size() && in[i + 1] == '\r' && in[i + 2] == '\n') {
                i += 3;
                ++pending;
                continue;
            }
            out.push_back(in[i]);
            if (in[i] == '\n') {
                out.append(static_cast<size_t>(pending), '\n');
                pending = 0;
            }
            ++i;
        }
        out.append(static_cast<size_t>(pending), '\n');
        return out;
    }

    std::string run(const std::string& raw_text, const std::string& dir, int file = 0) {
        const std::string text = splice_lines(raw_text);
        if (depth > 256) {  // [implimits] suggests 256 nested includes; engine include chains are deeper than 16
            error = "include nesting is too deep";
            return "";
        }
        ++depth;
        struct Frame {
            bool parent;
            bool taken;
            bool any;
            bool saw_else;
        };
        std::vector<Frame> stack;
        auto active = [&]() {
            for (const Frame& frame : stack) {
                if (!frame.parent || !frame.taken) return false;
            }
            return true;
        };
        std::string out;
        size_t i = 0;
        int ln = 0;
        while (i < text.size()) {
            ++ln;
            size_t end = text.find('\n', i);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(i, end - i);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            i = end < text.size() ? end + 1 : end;
            size_t s = 0;
            while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
            if (s < line.size() && line[s] == '#') {
                std::string rest = line.substr(s + 1);
                size_t a = 0;
                while (a < rest.size() && (rest[a] == ' ' || rest[a] == '\t')) ++a;
                rest = rest.substr(a);
                auto word = [&](const char* w) { return rest.compare(0, std::strlen(w), w) == 0; };
                // Every output line has an origin. A directive leaves a blank
                // line, except an include, which is replaced by the file's lines.
                if (!(word("include") && active())) {
                    out += "\n";
                    origin.push_back({file, ln});
                }
                if (word("include")) {
                    if (!active()) continue;
                    const size_t q1 = rest.find_first_of("\"<");
                    const size_t q2 = rest.find_last_of("\">");
                    if (q1 == std::string::npos || q2 <= q1) {
                        error = "bad include";
                        --depth;
                        return "";
                    }
                    const bool quote = rest[q1] == '"';
                    const std::string name = rest.substr(q1 + 1, q2 - q1 - 1);
                    std::string found;
                    const std::string body = find_include(name, quote, dir, &found);
                    if (body.empty() && found.empty()) {
                        error = "cannot open include " + name;
                        --depth;
                        return "";
                    }
                    if (once_files.count(found) > 0) continue;
                    files.push_back(name);
                    const size_t slash = found.rfind('/');
                    path_stack.push_back(found);
                    out += run(strip_comments(body), slash == std::string::npos ? std::string() : found.substr(0, slash),
                               static_cast<int>(files.size()) - 1);
                    path_stack.pop_back();
                    if (!error.empty()) {
                        --depth;
                        return "";
                    }
                } else if (word("define")) {
                    if (!active()) continue;
                    std::string body = rest.substr(6);
                    size_t b = 0;
                    while (b < body.size() && (body[b] == ' ' || body[b] == '\t')) ++b;
                    body = body.substr(b);
                    size_t n = 0;
                    while (n < body.size() && (std::isalnum(static_cast<unsigned char>(body[n])) || body[n] == '_')) ++n;
                    if (n == 0) {
                        error = "bad define";
                        --depth;
                        return "";
                    }
                    Macro macro;
                    std::string rest = n < body.size() ? body.substr(n) : "";
                    if (!rest.empty() && rest[0] == '(') {
                        macro.function_like = true;
                        size_t p = 1;
                        while (p < rest.size() && rest[p] != ')') {
                            while (p < rest.size() && (rest[p] == ' ' || rest[p] == '\t' || rest[p] == ',')) ++p;
                            if (rest.compare(p, 3, "...") == 0) {
                                // A variadic macro ([cpp.replace] 12): the rest of the arguments are __VA_ARGS__.
                                macro.params.push_back("__VA_ARGS__");
                                p += 3;
                                continue;
                            }
                            size_t e = p;
                            while (e < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[e])) || rest[e] == '_')) ++e;
                            if (e > p) macro.params.push_back(rest.substr(p, e - p));
                            else if (p < rest.size() && rest[p] != ')') ++p;  // anything else: step over it (no endless loop)
                            p = std::max(p, e);
                        }
                        if (p < rest.size() && rest[p] == ')') ++p;
                        while (p < rest.size() && (rest[p] == ' ' || rest[p] == '\t')) ++p;
                        macro.body = rest.substr(p);
                    } else {
                        size_t v = 0;
                        while (v < rest.size() && (rest[v] == ' ' || rest[v] == '\t')) ++v;
                        macro.body = rest.substr(v);
                    }
                    macros[body.substr(0, n)] = macro;
                } else if (word("ifdef") || word("ifndef")) {
                    const bool neg = word("ifndef");
                    std::string name = rest.substr(neg ? 6 : 5);
                    size_t b = 0;
                    while (b < name.size() && (name[b] == ' ' || name[b] == '\t')) ++b;
                    name = name.substr(b);
                    size_t n = 0;
                    while (n < name.size() && (std::isalnum(static_cast<unsigned char>(name[n])) || name[n] == '_')) ++n;
                    name = name.substr(0, n);
                    const bool parent = active();
                    bool taken = parent && defined(name);
                    if (neg) taken = parent && !defined(name);
                    stack.push_back({parent, taken, taken, false});
                } else if (word("elif") || (rest.compare(0, 2, "if") == 0 &&
                                            (rest.size() == 2 || rest[2] == ' ' || rest[2] == '\t' || rest[2] == '('))) {
                    const bool is_elif = word("elif");
                    std::string expr = rest.substr(is_elif ? 4 : 2);
                    size_t b = 0;
                    while (b < expr.size() && (expr[b] == ' ' || expr[b] == '\t')) ++b;
                    expr = expr.substr(b);
                    bool good = true;
                    const int64_t value = eval_pp(expr, &good);
                    if (!good) {
                        error = error.empty() ? "bad preprocessor expression: #if " + expr : error;
                        --depth;
                        return "";
                    }
                    if (is_elif) {
                        if (stack.empty() || stack.back().saw_else) {
                            error = "elif without if";
                            --depth;
                            return "";
                        }
                        const bool taken = stack.back().parent && !stack.back().any && value != 0;
                        stack.back().taken = taken;
                        if (taken) stack.back().any = true;
                    } else {
                        const bool parent = active();
                        const bool taken = parent && value != 0;
                        stack.push_back({parent, taken, taken, false});
                    }
                } else if (word("undef")) {
                    if (!active()) continue;
                    size_t b = 5;
                    while (b < rest.size() && (rest[b] == ' ' || rest[b] == '\t')) ++b;
                    size_t n = b;
                    while (n < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[n])) || rest[n] == '_')) ++n;
                    macros.erase(rest.substr(b, n - b));
                } else if (word("error")) {
                    if (!active()) continue;
                    error = "#error" + rest.substr(5);
                    --depth;
                    return "";
                } else if (word("pragma")) {
                    if (!active()) continue;
                    size_t b = 6;
                    while (b < rest.size() && (rest[b] == ' ' || rest[b] == '\t')) ++b;
                    const std::string what = rest.substr(b);
                    if (what.compare(0, 4, "once") == 0) {
                        if (!path_stack.empty()) once_files.insert(path_stack.back());
                    } else if (what.compare(0, 4, "pack") == 0) {
                        // Ignoring it would change the layout of a struct without a word.
                        error = "#pragma pack is not supported";
                        --depth;
                        return "";
                    }
                    // Any other pragma (warning, comment, ...) has no effect on the code made here.
                } else if (rest.empty() || word("line")) {
                    // A null directive and #line change nothing.
                } else if (word("else")) {
                    if (stack.empty() || stack.back().saw_else) {
                        error = "else without if";
                        --depth;
                        return "";
                    }
                    stack.back().saw_else = true;
                    stack.back().taken = stack.back().parent && !stack.back().any;
                    stack.back().any = true;
                } else if (word("endif")) {
                    if (stack.empty()) {
                        error = "endif without if";
                        --depth;
                        return "";
                    }
                    stack.pop_back();
                } else if (active()) {
                    error = "unknown preprocessor directive #" + rest.substr(0, rest.find_first_of(" \t("));
                    --depth;
                    return "";
                }
                continue;
            }
            current_line = ln;
            // A call of a function-like macro whose arguments go on over the next lines ([cpp.replace] 10: the
            // arguments may span lines): those lines are joined to this one, and each joined line leaves a blank line
            // behind so that every output line keeps its origin.
            int joined = 0;
            while (active() && open_macro_call(line) && i < text.size()) {
                size_t next_end = text.find('\n', i);
                if (next_end == std::string::npos) next_end = text.size();
                std::string more = text.substr(i, next_end - i);
                if (!more.empty() && more.back() == '\r') more.pop_back();
                size_t m = 0;
                while (m < more.size() && (more[m] == ' ' || more[m] == '\t')) ++m;
                if (m < more.size() && more[m] == '#') break;
                line += " " + more;
                i = next_end < text.size() ? next_end + 1 : next_end;
                ++joined;
            }
            out += active() ? expand(line, 0) + "\n" : std::string("\n");
            origin.push_back({file, ln});
            for (int k = 0; k < joined; ++k) {
                ++ln;
                out += "\n";
                origin.push_back({file, ln});
            }
        }
        --depth;
        return out;
    }
};


}  // namespace compiler
}  // namespace okayms
