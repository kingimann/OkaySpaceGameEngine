// Preprocessor gaps found by compiling engine headers: #pragma once, #undef, #error, other
// #pragma lines, includes relative to the including file, and #if with ~ << >> & | ^ and ?:.
// Written from ISO/IEC 14882 [cpp.cond], [cpp.include], [cpp.replace], [cpp.error], [cpp.pragma].

#include "check.hpp"
#include "okayms/compile.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>

using okayms_test::Tally;

namespace {

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path.c_str());
    out << text;
}

okayms::Result<okayms::CompileOutput> build(const std::string& source, const std::string& dir) {
    okayms::CompileInput in;
    in.source = source;
    in.include_paths.push_back(dir);
    return okayms::compile_c(in);
}

}  // namespace

int main() {
    Tally tally;
    const std::string dir = "/tmp/okayms-pp";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir + "/sub").c_str());
    write_text(dir + "/once.h", "#pragma once\nint once_value(void) { return 5; }\n");
    write_text(dir + "/twice.h", "#pragma once\n#include \"once.h\"\n#define TWICE 2\n");
    write_text(dir + "/sub/near.h", "#pragma once\n#include \"far.h\"\n#define NEAR (FAR + 1)\n");
    write_text(dir + "/sub/far.h", "#define FAR 40\n");

    std::printf("test_pp\n");
    {
        // The same header twice, directly and through another header, is read once.
        const auto r = build("#include \"once.h\"\n#include \"twice.h\"\n#include \"once.h\"\nint entry(void) { return once_value() + TWICE; }\n", dir);
        CHECK(tally, r.ok());
        if (!r.ok()) std::fprintf(stderr, "  %s\n", r.error.c_str());
    }
    {
        // A quoted include is found next to the file that includes it.
        const auto r = build("#include \"sub/near.h\"\nint entry(void) { return NEAR; }\n", dir);
        CHECK(tally, r.ok());
        if (!r.ok()) std::fprintf(stderr, "  %s\n", r.error.c_str());
    }
    {
        const auto r = build(
            "#define A 1\n#undef A\n#ifdef A\n#error A is still defined\n#endif\n"
            "#pragma warning(disable : 4996)\n#pragma comment(lib, \"kernel32.lib\")\n"
            "#if (1 << 3) == 8 && (6 & 3) == 2 && (5 | 2) == 7 && (6 ^ 3) == 5 && ~0 == -1 && (1 ? 2 : 3) == 2 && (256 >> 4) == 16\n"
            "int entry(void) { return 1; }\n#else\nint entry(void) { return 2; }\n#endif\n",
            dir);
        CHECK(tally, r.ok());
        if (!r.ok()) std::fprintf(stderr, "  %s\n", r.error.c_str());
    }
    {
        const auto r = build("#error stop here\nint entry(void) { return 0; }\n", dir);
        CHECK(tally, !r.ok());
        CHECK(tally, !r.ok() && r.error.find("stop here") != std::string::npos);
        // Not taken: no error.
        const auto s = build("#if 0\n#error never\n#endif\nint entry(void) { return 0; }\n", dir);
        CHECK(tally, s.ok());
        // A struct layout pragma is not silently ignored.
        const auto p = build("#pragma pack(push, 1)\nint entry(void) { return 0; }\n", dir);
        CHECK(tally, !p.ok());
    }
    return tally.finish(1);
}
