// Checks that the internal plan exists and names every stage and source
// document the brief requires. This test does not compile or link anything.

#include <fstream>
#include <iostream>
#include <string>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const char* what) {
    if (ok) {
        ++g_pass;
        return;
    }
    std::cerr << "FAIL: " << what << "\n";
    ++g_fail;
}

std::string read_file(const std::string& path, std::string* error) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        *error = "cannot open " + path;
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: test_plan <repo-root>\n";
        return 2;
    }
    const std::string path = std::string(argv[1]) + "/docs/own_msvc_plan.md";
    std::string error;
    const std::string text = read_file(path, &error);
    check(error.empty(), error.empty() ? "plan file opens" : error.c_str());
    if (!error.empty()) {
        std::cout << "checks passed " << g_pass << ", failed " << g_fail << "\n";
        return 1;
    }

    // Stage headings and the work the brief lists, in the words the plan uses.
    const char* need[] = {
        "COFF object writer",
        "COFF object reader",
        "PE32+",
        "console",
        "kernel32.dll",
        "ExitProcess",
        ".lib",
        "/c",
        "/Fo",
        "/Fe",
        "/I",
        "/D",
        "/O2",
        "/link",
        "#987",
        "okaycc",
        "off by default",
        "not a replacement",
        "years",
        "PE Format",
        "x64 calling convention",
        "x64 exception handling",
        "unwind",
        "ISO/IEC 9899",
        "ISO/IEC 14882",
        "Intel 64 and IA-32",
        "AMD64 Architecture Programmer's Manual",
        "Linkers and Loaders",
        "not run on Windows",
        "Wine",
    };
    for (const char* phrase : need) {
        const bool found = text.find(phrase) != std::string::npos;
        check(found, phrase);
    }

    std::cout << "checks passed " << g_pass << ", failed " << g_fail << "\n";
    return g_fail == 0 ? 0 : 1;
}
