// Shared by the okay-ms language tests (test_round21.cpp onward): build a C++ program with okay-cl at /O0, /O1 and
// /O2, link it, run it under Wine, then build the same text with MinGW g++ and run that too. Each program returns 0
// when all of its checks hold (CK(n, cond) returns n when cond is false). One copy of this for every test
// (docs/engine/shared_code.md).
#pragma once

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/file_io.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace okayms_test {

inline int run_shell(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

// TMPDIR is left out: Wine 9 crashed at start in a sandbox with it set ("free(): invalid pointer", 2026-11-23).
inline int wine_exit(const std::string& exe) {
    return run_shell("env -u TMPDIR WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + exe + " >/dev/null 2>&1");
}

inline bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::string error;
    return okayms::write_file(path, bytes, &error);
}

inline bool has_symbol(const okayms::ObjectFile& object, const std::string& name) {
    for (const auto& symbol : object.symbols)
        if (symbol.name == name) return true;
    return false;
}

inline const char* check_macro() { return "#define CK(n, c) if (!(c)) return n;\n"; }

// What one test file passes to run_both: its tag ("r23": the folder /tmp/okayms-r23 and the "r23 program" line), the
// kernel32 functions its programs may import ("dll:name" for another DLL), and whether g++ needs run-time type
// information.
struct ProgramRun {
    std::string tag;
    std::vector<std::string> kernel32;
    bool rtti = false;  // g++ builds the reference with run-time type information (a program that uses typeid)
    // The program throws and catches (Round 31): okay-ms links its own exception runtime, heap and string functions
    // (runtime/eh.c, mem.c and string.c, so the program does not use bump_alloc), and g++ builds the reference with
    // exceptions.
    bool eh = false;
};

// Compiles one file of okay-ms's own C runtime (tools/okayms/runtime, read from the folder the tests run in).
inline bool compile_runtime_file(const std::string& name, okayms::ObjectFile* out) {
    std::string error;
    const std::vector<uint8_t> bytes = okayms::read_file("runtime/" + name, &error);
    if (!error.empty()) {
        std::fprintf(stderr, "  runtime %s: %s\n", name.c_str(), error.c_str());
        return false;
    }
    okayms::CompileInput in;
    in.source.assign(bytes.begin(), bytes.end());
    in.name = name;
    in.include_paths.push_back("runtime");
    const auto compiled = okayms::compile_c(in);
    if (!compiled.ok()) {
        std::fprintf(stderr, "  runtime %s: %s\n", name.c_str(), compiled.error.c_str());
        return false;
    }
    *out = compiled.value.object;
    return true;
}

// The import libraries the g++ reference links for the DLLs a program names ("winhttp.dll:WinHttpCrackUrl" gives
// -lwinhttp). kernel32 and the C runtime are linked by default.
inline std::string gpp_dll_libs(const ProgramRun& run) {
    std::string libs;
    for (const std::string& fn : run.kernel32) {
        const size_t colon = fn.find(':');
        if (colon == std::string::npos) continue;
        std::string dll = fn.substr(0, colon);
        if (dll.size() > 4 && dll.compare(dll.size() - 4, 4, ".dll") == 0) dll.resize(dll.size() - 4);
        if (dll == "kernel32" || dll == "msvcrt") continue;
        const std::string flag = " -l" + dll;
        if (libs.find(flag) == std::string::npos) libs += flag;
    }
    return libs;
}

// Builds `source` at each optimization level and runs it; MinGW g++ runs the same text. Both must exit with `want`.
// object_out, when given, receives the /O0 object (for symbol checks).
inline void run_both(const ProgramRun& run, Tally& tally, const std::string& name, const std::string& source, int want,
                     okayms::ObjectFile* object_out = nullptr) {
    std::printf("%s program %s\n", run.tag.c_str(), name.c_str());
    const std::string dir = "/tmp/okayms-" + run.tag;
    run_shell("mkdir -p " + dir);
    for (int level = 0; level <= 2; ++level) {
        okayms::CompileInput in;
        in.source = std::string(check_macro()) + source;
        in.opt_level = level;
        in.optimize = level > 0;
        in.include_paths.push_back("tools/okayms/stdinc");
        in.include_paths.push_back("stdinc");
        const auto compiled = okayms::compile_c(in);
        CHECK(tally, compiled.ok());
        if (!compiled.ok()) {
            std::fprintf(stderr, "  %s /O%d: %s\n", name.c_str(), level, compiled.error.c_str());
            continue;
        }
        if (object_out != nullptr && level == 0) *object_out = compiled.value.object;
        okayms::LinkOptions link_options;
        for (const std::string& fn : run.kernel32) {
            okayms::ImportedSymbol imp;
            // "bcrypt.dll:BCryptGenRandom" names another DLL; a bare name is a kernel32 function.
            const size_t colon = fn.find(':');
            imp.dll = colon == std::string::npos ? "kernel32.dll" : fn.substr(0, colon);
            imp.name = colon == std::string::npos ? fn : fn.substr(colon + 1);
            link_options.imports.push_back(imp);
        }
        std::vector<okayms::ObjectFile> objects{compiled.value.object};
        if (run.eh) {
            okayms::ObjectFile eh_obj;
            okayms::ObjectFile mem_obj;
            okayms::ObjectFile string_obj;
            CHECK(tally, compile_runtime_file("eh.c", &eh_obj) && compile_runtime_file("mem.c", &mem_obj) &&
                             compile_runtime_file("string.c", &string_obj));
            objects.push_back(eh_obj);
            objects.push_back(mem_obj);
            objects.push_back(string_obj);
            for (const char* fn : {"GetProcessHeap", "HeapAlloc", "HeapFree", "GetStdHandle", "WriteFile", "ExitProcess"}) {
                okayms::ImportedSymbol imp;
                imp.dll = "kernel32.dll";
                imp.name = fn;
                link_options.imports.push_back(imp);
            }
        }
        const auto linked = okayms::link_executable(objects, link_options);
        CHECK(tally, linked.ok());
        if (!linked.ok()) {
            std::fprintf(stderr, "  %s\n", linked.error.c_str());
            continue;
        }
        const std::string ok_exe = dir + "/" + name + "-ok" + std::to_string(level) + ".exe";
        CHECK(tally, write_bytes(ok_exe, linked.value));
        const int code = wine_exit(ok_exe);
        CHECK(tally, code == want);
        if (code != want) std::fprintf(stderr, "  %s /O%d: okay exit %d, wanted %d\n", name.c_str(), level, code, want);
    }
    if (run_shell("command -v x86_64-w64-mingw32-g++ >/dev/null") != 0) {
        std::printf("skipping g++ comparison: MinGW g++ is not installed\n");
        return;
    }
    const std::string src = dir + "/" + name + ".cpp";
    {
        std::ofstream out(src.c_str());
        out << check_macro() << "#include <cstdint>\n#include <cstddef>\n" << source << "\nint main() { return entry(); }\n";
    }
    const std::string gpp_exe = dir + "/" + name + "-gpp.exe";
    const int built = run_shell(std::string("x86_64-w64-mingw32-g++ -static ") + (run.eh ? "" : "-fno-exceptions ") +
                                (run.rtti ? "" : "-fno-rtti ") +
                                "-w -o " + gpp_exe + " " + src + gpp_dll_libs(run) +
                                " 2>" + dir + "/" + name + ".log");
    CHECK(tally, built == 0);
    if (built != 0) return;
    const int ref = wine_exit(gpp_exe);
    CHECK(tally, ref == want);
    if (ref != want) std::fprintf(stderr, "  g++ exit %d, wanted %d (the test itself is wrong)\n", ref, want);
}

// A bump allocator for the okay-ms side of a program (no C runtime is linked: malloc and free for <vector>, <string>
// and new); g++ uses the real one.
inline const char* bump_alloc() {
    return "#ifdef __GNUC__\n"
           "#include <stdlib.h>\n"
           "#else\n"
           "char pool_[262144];\n"
           "unsigned long long used_;\n"
           "void *malloc(unsigned long long n) { char *p = pool_ + used_; used_ += ((n + 15) / 16) * 16; return p; }\n"
           "void free(void *p) { }\n"
           "#endif\n";
}

}  // namespace okayms_test
