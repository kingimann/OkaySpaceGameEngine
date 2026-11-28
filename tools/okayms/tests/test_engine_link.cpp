// Round 22: link and run. Real engine files (engine/src/Crypto/Sha512.cpp, Blake2b.cpp, ChaCha20.cpp, Util.cpp and the
// header-only math: Mathf, Vec3, Quat, Mat4, Easing) are compiled by okay-cl at /O0, /O1 and /O2 together with a test
// program (tools/okayms/linktests/), linked with okay-ms's own C runtime (tools/okayms/runtime, built here into okayrt.lib)
// and a kernel32 import library written here, and run under Wine. MinGW g++ builds the same sources with its own library
// and runs them under Wine too. The exit code must be 0 for all four builds and the printed text (digests, numbers in
// millionths) must be the same for all four.
#include "check.hpp"
#include "okayms/archive.hpp"
#include "okayms/coff.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

using okayms_test::Tally;

int run(const std::string& cmd, const std::string& log) {
    const int rc = std::system((cmd + " >" + log + " 2>&1").c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

std::string slurp(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

// The folder for this run: its own per process (Round 31). A fixed /tmp/okayms-el was shared by every run on the machine,
// and a second run started at the same time truncated this run's output files while they were read ("output differs
// from g++" with exit code 0, found in the Round 30 full suite and reproduced with two runs started 5 seconds apart).
const std::string kDir = "/tmp/okayms-el-" + std::to_string(static_cast<long>(getpid()));

// The C runtime as a static library, and the kernel32 imports it and the programs use.
bool build_runtime(Tally& tally, const std::string& cl, const std::string& root) {
    const std::string rt = root + "/tools/okayms/runtime";
    okayms::Archive archive;
    for (const char* name : {"string", "printf", "startup", "mem", "eh"}) {
        const std::string obj = std::string(kDir) + "/rt_" + name + ".obj";
        const int rc = run(cl + " /I" + rt + " /c /Fo" + obj + " " + rt + "/" + name + ".c", std::string(kDir) + "/rt.log");
        CHECK(tally, rc == 0);
        if (rc != 0) {
            std::fprintf(stderr, "%s\n", slurp(std::string(kDir) + "/rt.log").c_str());
            return false;
        }
        const std::string bytes = slurp(obj);
        const auto object = okayms::read_coff(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        CHECK(tally, object.ok());
        if (!object.ok()) return false;
        okayms::ArchiveMember member;
        member.name = std::string(name) + ".obj";
        member.object = object.value;
        archive.members.push_back(member);
    }
    const auto lib = okayms::write_archive(archive);
    CHECK(tally, lib.ok() && write_bytes(std::string(kDir) + "/okayrt.lib", lib.value));
    okayms::Archive kernel;
    for (const char* name : {"ExitProcess", "GetStdHandle", "WriteFile", "GetProcessHeap", "HeapAlloc", "HeapFree",
                             "QueryPerformanceCounter", "QueryPerformanceFrequency", "GetSystemTimeAsFileTime",
                             "CreateThread", "WaitForSingleObject", "CloseHandle",
                             // Round 36: the engine's lock (CoreThreads.hpp) in StringId.cpp
                             "InitializeCriticalSection", "EnterCriticalSection", "LeaveCriticalSection",
                             "DeleteCriticalSection"}) {
        okayms::ArchiveMember member;
        member.name = name;
        member.is_import = true;
        member.import.symbol = name;
        member.import.dll = "kernel32.dll";
        kernel.members.push_back(member);
    }
    const auto kbytes = okayms::write_archive(kernel);
    CHECK(tally, kbytes.ok() && write_bytes(std::string(kDir) + "/kernel32.lib", kbytes.value));
    return lib.ok() && kbytes.ok();
}

struct Program {
    const char* name;
    std::vector<std::string> engine_sources;  // relative to the repository root
    // g++ builds the reference with exceptions (an engine header throws in a function the program does not call;
    // okay-cl's objects then name the exception runtime only from functions that cannot run; Round 35).
    bool gpp_exceptions = false;
};

// The text the program printed, with the carriage returns that a MinGW text-mode stdout adds taken out.
std::string run_wine(const std::string& exe, int* code) {
    const std::string out = exe + ".out";
    *code = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 120 wine " + exe, out);
    std::string text = slurp(out);
    std::string plain;
    for (char c : text)
        if (c != '\r') plain.push_back(c);
    return plain;
}

void check_program(Tally& tally, const std::string& root, const Program& program) {
    std::printf("link %s\n", program.name);
    const std::string cl = root + "/tools/okayms/build/okay-cl";
    const std::string dir = kDir;
    const std::string includes = " /I" + root + "/tools/okayms/linktests /I" + root + "/engine/include /I" + root +
                                 "/engine/src /I" + root + "/shared /I" + root + "/tests /I" + root + "/tools/okayms/stdinc";
    const std::string test_src = root + "/tools/okayms/linktests/" + program.name + ".cpp";
    std::string reference_text;
    bool have_reference = false;
    const bool mingw = run("command -v x86_64-w64-mingw32-g++", dir + "/which.log") == 0;
    if (mingw) {
        std::string sources = test_src;
        for (const std::string& s : program.engine_sources) sources += " " + root + "/" + s;
        const std::string exe = dir + "/" + program.name + "-gpp.exe";
        const int built = run(std::string("x86_64-w64-mingw32-g++ -static -O2 -w ") +
                                  (program.gpp_exceptions ? "" : "-fno-exceptions ") + "-fno-rtti -std=c++17 -I" + root +
                                  "/tools/okayms/linktests -I" + root + "/engine/include -I" + root + "/engine/src -I" + root +
                                  "/shared -I" + root + "/tests -o " + exe + " " + sources,
                              dir + "/" + program.name + "-gpp.log");
        CHECK(tally, built == 0);
        if (built == 0) {
            int code = -1;
            reference_text = run_wine(exe, &code);
            CHECK(tally, code == 0);
            CHECK(tally, reference_text.find(" 0 failures") != std::string::npos);
            if (code != 0) std::fprintf(stderr, "  %s: g++ exit %d\n%s\n", program.name, code, reference_text.c_str());
            have_reference = true;
        } else {
            std::fprintf(stderr, "%s\n", slurp(dir + "/" + program.name + "-gpp.log").c_str());
        }
    }
    for (int level = 0; level <= 2; ++level) {
        const std::string flag = level == 0 ? "" : level == 1 ? " /O1" : " /O2";
        std::string objects;
        std::vector<std::string> sources = program.engine_sources;
        for (std::string& s : sources) s = root + "/" + s;
        sources.push_back(test_src);
        bool ok = true;
        for (size_t i = 0; i < sources.size() && ok; ++i) {
            const std::string obj = dir + "/" + program.name + "-" + std::to_string(level) + "-" + std::to_string(i) + ".obj";
            const int rc = run(cl + " /c" + flag + " /Fo" + obj + " " + sources[i] + includes,
                               dir + "/" + program.name + ".cl.log");
            CHECK(tally, rc == 0);
            if (rc != 0) {
                std::fprintf(stderr, "  %s /O%d: %s", program.name, level, slurp(dir + "/" + program.name + ".cl.log").c_str());
                ok = false;
            }
            objects += " " + obj;
        }
        if (!ok) continue;
        const std::string exe = dir + "/" + program.name + "-ok" + std::to_string(level) + ".exe";
        const int linked = run(cl + " /ENTRY:mainCRTStartup /Fe" + exe + objects + " " + dir + "/okayrt.lib " + dir +
                                   "/kernel32.lib",
                               dir + "/" + program.name + ".link.log");
        CHECK(tally, linked == 0);
        if (linked != 0) {
            std::fprintf(stderr, "  %s /O%d link: %s", program.name, level, slurp(dir + "/" + program.name + ".link.log").c_str());
            continue;
        }
        int code = -1;
        const std::string text = run_wine(exe, &code);
        CHECK(tally, code == 0);
        if (code != 0) std::fprintf(stderr, "  %s /O%d: exit %d\n%s\n", program.name, level, code, text.c_str());
        if (have_reference) {
            CHECK(tally, text == reference_text);
            if (text != reference_text)
                std::fprintf(stderr, "  %s /O%d: output differs from g++\n--- okay-cl\n%s--- g++\n%s", program.name, level,
                             text.c_str(), reference_text.c_str());
        } else {
            CHECK(tally, text.find(" 0 failures") != std::string::npos);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    std::system((std::string("rm -rf ") + kDir + " && mkdir -p " + kDir).c_str());
    if (build_runtime(tally, root + "/tools/okayms/build/okay-cl", root)) {
        check_program(tally, root, {"link_crypto",
                                    {"engine/src/Crypto/Sha512.cpp", "engine/src/Crypto/Blake2b.cpp",
                                     "engine/src/Crypto/ChaCha20.cpp", "engine/src/Crypto/Util.cpp"}});
        check_program(tally, root, {"link_math", {}});
        // Round 35: an engine file whose namespace-scope std::string is made by the .CRT$XCU start-up table.
        check_program(tally, root, {"link_scene_lists", {"engine/src/Scene/ProjectLists.cpp"}, true});
        // Round 35: thread_local objects of class type in an object without main, made by the runtime's TLS callback.
        check_program(tally, root, {"link_thread_local", {"tools/okayms/linktests/tls_part.cpp"}});
        // Round 36: inline functions' static locals are one object in the program (COMDAT sections, [dcl.inline] 6).
        check_program(tally, root, {"link_inline_static", {"tools/okayms/linktests/inline_part.cpp"}});
        // Round 36: a bigger engine slice, the command-line parser and the interned names.
        check_program(tally, root, {"link_core_slice", {"engine/src/Core/CommandLine.cpp", "engine/src/Core/StringId.cpp"}});
    }
    // The folder goes when every check passed; a failed run keeps its files for a look.
    if (tally.fail == 0) std::system(("rm -rf " + kDir).c_str());
    else std::fprintf(stderr, "files kept in %s\n", kDir.c_str());
    return tally.finish(2);
}
