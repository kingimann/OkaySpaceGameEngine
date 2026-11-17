#include "diag.hpp"
#include "lexer.hpp"
#include "token.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string readAll(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void usage(std::ostream& out) {
    out << "okay-cc --lex [file]\n"
        << "  Read a C translation unit and print its tokens.\n"
        << "  With no file, read standard input. Later stages are not in this build yet.\n";
}

}  // namespace

int main(int argc, char** argv) {
    bool lex = false;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--lex") {
            lex = true;
        } else if (a == "-h" || a == "--help") {
            usage(std::cout);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "unknown flag " << a << "\n";
            usage(std::cerr);
            return 2;
        } else {
            path = a;
        }
    }
    if (!lex) {
        usage(std::cerr);
        return 2;
    }

    std::string file = path.empty() ? "<stdin>" : path;
    std::string src;
    if (path.empty()) {
        src = readAll(std::cin);
    } else {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::cerr << "cannot open " << path << "\n";
            return 1;
        }
        src = readAll(in);
    }

    okaycc::Diagnostics diags;
    okaycc::Lexer lexer(file, src, diags);
    for (;;) {
        okaycc::Token t = lexer.next();
        std::cout << okaycc::formatToken(t) << "\n";
        if (t.kind == okaycc::Tok::End) break;
    }
    if (!diags.ok()) {
        std::cerr << diags.format();
        return 1;
    }
    return 0;
}
