#include "codegen.hpp"
#include "diag.hpp"
#include "elf.hpp"
#include "files.hpp"
#include "ir.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "preproc.hpp"
#include "token.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readAll(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void usage(std::ostream& out) {
    out << "okay-cc --lex [file]\n"
        << "okay-cc --pp [-I dir]... [file]\n"
        << "okay-cc --ast [-I dir]... [file]\n"
        << "okay-cc --check [file]\n"
        << "okay-cc --ir [file]\n"
        << "okay-cc --asm [file]\n"
        << "okay-cc --elf outfile [file]\n"
        << "okay-cc --obj outfile [file]\n"
        << "  --lex tokens, --pp preprocessor, --ast tree, --check types,\n"
        << "  --ir three-address code, --asm System V x86-64 assembly,\n"
        << "  --elf a static executable, --obj a relocatable object.\n"
        << "  With no file, read standard input.\n";
}

}  // namespace

int main(int argc, char** argv) {
    bool lex = false;
    bool pp = false;
    bool ast = false;
    bool check = false;
    bool ir = false;
    bool asmm = false;
    bool elf = false;
    bool obj = false;
    std::string elfOut;
    std::string path;
    std::vector<std::string> includes;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--lex") {
            lex = true;
        } else if (a == "--pp") {
            pp = true;
        } else if (a == "--ast") {
            ast = true;
        } else if (a == "--check") {
            check = true;
        } else if (a == "--ir") {
            ir = true;
        } else if (a == "--asm") {
            asmm = true;
        } else if (a == "--elf") {
            elf = true;
            if (i + 1 >= argc) {
                std::cerr << "--elf needs an output path\n";
                return 2;
            }
            elfOut = argv[++i];
        } else if (a == "--obj") {
            obj = true;
            if (i + 1 >= argc) {
                std::cerr << "--obj needs an output path\n";
                return 2;
            }
            elfOut = argv[++i];
        } else if (a == "-I") {
            if (i + 1 >= argc) {
                std::cerr << "-I needs a directory\n";
                return 2;
            }
            includes.push_back(argv[++i]);
        } else if (a.size() > 2 && a[0] == '-' && a[1] == 'I') {
            includes.push_back(a.substr(2));
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
    int modes = static_cast<int>(lex) + static_cast<int>(pp) + static_cast<int>(ast) + static_cast<int>(check) +
                static_cast<int>(ir) + static_cast<int>(asmm) + static_cast<int>(elf) + static_cast<int>(obj);
    if (modes != 1) {
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
    std::string name = path.empty() ? "<stdin>" : path;
    if (elf || obj) {
        std::vector<unsigned char> bin = obj ? okaycc::compileToObject(src, diags, name, includes)
                                             : okaycc::compileToElf(src, diags, name, includes);
        if (diags.ok()) {
            std::ofstream out(elfOut, std::ios::binary);
            if (!out) {
                std::cerr << "cannot write " << elfOut << "\n";
                return 1;
            }
            out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
        }
    } else if (asmm) {
        std::cout << okaycc::compileToAssembly(src, diags, name);
    } else if (ir) {
        std::cout << okaycc::irText(src, diags, name);
    } else if (check) {
        okaycc::checkSource(src, diags, name);
    } else if (ast) {
        if (path.empty()) {
            okaycc::MemoryFiles mem;
            mem.add("<stdin>", src);
            std::cout << okaycc::astOfFile(mem, "<stdin>", includes, diags);
        } else {
            okaycc::DiskFiles disk;
            std::cout << okaycc::astOfFile(disk, path, includes, diags);
        }
    } else if (lex) {
        okaycc::Lexer lexer(file, src, diags);
        for (;;) {
            okaycc::Token t = lexer.next();
            std::cout << okaycc::formatToken(t) << "\n";
            if (t.kind == okaycc::Tok::End) break;
        }
    } else if (path.empty()) {
        okaycc::MemoryFiles mem;
        mem.add("<stdin>", src);
        std::cout << okaycc::preprocess(mem, "<stdin>", includes, diags);
    } else {
        okaycc::DiskFiles disk;
        std::cout << okaycc::preprocess(disk, path, includes, diags);
    }
    if (!diags.ok()) {
        std::cerr << diags.format();
        return 1;
    }
    return 0;
}
