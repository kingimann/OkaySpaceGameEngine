// Comments out of the source text, and reading an included file.
// Moved out of compile.cpp unchanged (docs/engine/modularization_plan.md, batch T1).
#include "compile_internal.hpp"

namespace okayms {
namespace compiler {
std::string strip_comments(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '"' || in[i] == '\'') {
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
