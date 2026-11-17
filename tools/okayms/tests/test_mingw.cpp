// MinGW's assembler and gcc -c are test inputs only. The object is read
// and linked by our code. The '$' in a section name is dropped, which is
// the COFF grouping rule, so gcc's .rdata$zzz fits in an image section.

#include "check.hpp"
#include "okayms/archive.hpp"
#include "okayms/coff.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sys/wait.h>

namespace {

using okayms_test::Tally;

int run_cmd(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

void test_dollar_group(Tally& tally) {
    std::printf("test_dollar_group\n");
    okayms::ObjectFile object;
    okayms::Section text;
    text.name = ".text";
    text.virtual_size = 1;
    text.data = {0xC3};
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead;
    object.sections.push_back(text);
    okayms::Section rdata;
    rdata.name = ".rdata$zzz";
    rdata.virtual_size = 1;
    rdata.data = {0xAB};
    rdata.characteristics = okayms::kScnCntInitData | okayms::kScnMemRead;
    object.sections.push_back(rdata);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    object.symbols.push_back(entry);
    const auto linked = okayms::link_executable({object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link error: %s\n", linked.error.c_str());
        return;
    }
    CHECK(tally, std::memcmp(linked.value.data() + 0x170, ".rdata", 6) == 0);
    CHECK(tally, linked.value[0x170 + 6] == 0);
    CHECK(tally, linked.value[0x400] == 0xAB);
}

void link_and_run(Tally& tally, const std::string& driver, const std::string& dir, const std::string& object_path,
                  const char* label) {
    std::printf("%s\n", label);
    const std::string exe = dir + "/out.exe";
    const std::string cmd = driver + " /OUT:" + exe + " /ENTRY:entry /SUBSYSTEM:CONSOLE /LIBPATH:" + dir +
                            " " + object_path + " kernel32.lib";
    const int linked = run_cmd(cmd + " >/tmp/okayms-mingw-link.txt 2>&1");
    CHECK(tally, linked == 0);
    if (linked != 0) {
        std::ifstream in("/tmp/okayms-mingw-link.txt");
        std::string text((std::istreambuf_iterator<char>(in)), {});
        std::fprintf(stderr, "  link: %s\n", text.c_str());
        return;
    }
    const int wine = run_cmd("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + exe +
                             " >/tmp/okayms-mingw-wine.txt 2>&1");
    CHECK(tally, wine == 42);
    if (wine != 42) std::fprintf(stderr, "  wine exit %d\n", wine);
}

void test_mingw_inputs(Tally& tally, const std::string& driver) {
    const int have_as = run_cmd("command -v x86_64-w64-mingw32-as >/dev/null");
    const int have_gcc = run_cmd("command -v x86_64-w64-mingw32-gcc >/dev/null");
    CHECK(tally, have_as == 0);
    CHECK(tally, have_gcc == 0);
    if (have_as != 0 || have_gcc != 0) {
        std::fprintf(stderr, "  MinGW cross tools are not installed\n");
        return;
    }
    const std::string dir = "/tmp/okayms-mingw";
    run_cmd("rm -rf " + dir + " && mkdir -p " + dir);
    okayms::Archive archive;
    okayms::ArchiveMember member;
    member.name = "ExitProcess";
    member.is_import = true;
    member.import.symbol = "ExitProcess";
    member.import.dll = "kernel32.dll";
    member.import.import_type = okayms::kImportCode;
    member.import.name_type = okayms::kImportByName;
    archive.members.push_back(member);
    const auto lib = okayms::write_archive(archive);
    CHECK(tally, lib.ok());
    if (!lib.ok()) return;
    CHECK(tally, write_bytes(dir + "/kernel32.lib", lib.value));

    const std::string asm_src = dir + "/call.s";
    {
        std::ofstream out(asm_src.c_str());
        out << ".intel_syntax noprefix\n.text\n.globl entry\nentry:\n"
               "sub rsp, 40\nmov ecx, 42\ncall ExitProcess\n";
    }
    CHECK(tally, run_cmd("x86_64-w64-mingw32-as -o " + dir + "/call.obj " + asm_src) == 0);
    {
        std::ifstream in((dir + "/call.obj").c_str(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        const auto object = okayms::read_coff(bytes.data(), bytes.size());
        CHECK(tally, object.ok());
        if (object.ok()) {
            bool saw = false;
            for (const auto& symbol : object.value.symbols) {
                if (symbol.name == "ExitProcess" && symbol.section == 0) saw = true;
            }
            CHECK(tally, saw);
        }
    }
    link_and_run(tally, driver, dir, dir + "/call.obj", "test_mingw_as");

    const std::string c_src = dir + "/call.c";
    {
        std::ofstream out(c_src.c_str());
        out << "__attribute__((dllimport)) void ExitProcess(unsigned int code);\n"
               "void entry(void) { ExitProcess(42); }\n";
    }
    CHECK(tally, run_cmd("x86_64-w64-mingw32-gcc -c -nostdlib -O2 -o " + dir + "/call-gcc.obj " + c_src) == 0);
    link_and_run(tally, driver, dir, dir + "/call-gcc.obj", "test_mingw_gcc");
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_dollar_group(tally);
    test_mingw_inputs(tally, root + "/tools/okayms/build/okay-link");
    return tally.finish(3);
}
