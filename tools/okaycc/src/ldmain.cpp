#include "link.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static std::vector<unsigned char> readAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    std::string outPath;
    std::vector<std::string> ins;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "okay-ld: -o needs a path\n";
                return 2;
            }
            outPath = argv[++i];
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "okay-ld: unknown flag " << a << "\n";
            return 2;
        } else {
            ins.push_back(a);
        }
    }
    if (outPath.empty() || ins.empty()) {
        std::cerr << "okay-ld -o outfile a.o b.o ...\n";
        return 2;
    }
    std::vector<std::vector<unsigned char>> objs;
    for (const std::string& p : ins) {
        auto b = readAll(p);
        if (b.empty()) {
            std::cerr << "okay-ld: cannot read " << p << "\n";
            return 1;
        }
        objs.push_back(std::move(b));
    }
    std::string error;
    auto exe = okaycc::linkObjects(objs, error);
    if (exe.empty()) {
        std::cerr << "okay-ld: " << error << "\n";
        return 1;
    }
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::cerr << "okay-ld: cannot write " << outPath << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char*>(exe.data()), static_cast<std::streamsize>(exe.size()));
    return 0;
}
