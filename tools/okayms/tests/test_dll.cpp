// A DLL export table, an import library, and a program that calls the DLL.
// The last check is the same program built with okay-cl /LD.

#include "check.hpp"
#include "okayms/archive.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/wait.h>

namespace {

using okayms_test::Tally;

uint32_t u32at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));
}
uint16_t u16at(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

int wine_exe(const std::string& path) {
    const std::string cmd = "WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + path +
                            " >/tmp/okayms-dll-wine.txt 2>&1";
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

okayms::ObjectFile add_dll_object() {
    okayms::ObjectFile object;
    okayms::Section text;
    text.name = ".text";
    // add: mov eax, ecx; add eax, edx; ret
    // entry: mov eax, 1; ret
    text.data = {0x89, 0xC8, 0x01, 0xD0, 0xC3, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};
    text.virtual_size = static_cast<uint32_t>(text.data.size());
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead;
    object.sections.push_back(text);
    okayms::Symbol add;
    add.name = "add";
    add.section = 1;
    add.type = 0x20;
    add.storage = okayms::kClassExternal;
    object.symbols.push_back(add);
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.value = 5;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    object.symbols.push_back(entry);
    return object;
}

okayms::Archive import_lib(const char* dll, const char* symbol) {
    okayms::Archive archive;
    okayms::ArchiveMember member;
    member.name = symbol;
    member.is_import = true;
    member.import.symbol = symbol;
    member.import.dll = dll;
    member.import.import_type = okayms::kImportCode;
    member.import.name_type = okayms::kImportByName;
    archive.members.push_back(member);
    return archive;
}

void test_export_bytes(Tally& tally) {
    std::printf("test_export_bytes\n");
    okayms::LinkOptions options;
    options.dll = true;
    options.image_name = "math.dll";
    options.exports.push_back("add");
    const auto linked = okayms::link_executable({add_dll_object()}, options);
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link: %s\n", linked.error.c_str());
        return;
    }
    const std::vector<uint8_t>& image = linked.value;
    CHECK(tally, u16at(image, 0x56) == 0x2022);
    CHECK(tally, u32at(image, 0xC8) == 0x2000);
    CHECK(tally, u32at(image, 0xCC) == 40);
    CHECK(tally, std::memcmp(image.data() + 0x170, ".edata", 6) == 0);
    CHECK(tally, u32at(image, 0x400 + 40) == 0x1000);
    CHECK(tally, std::memcmp(image.data() + 0x400 + 50, "add", 4) == 0);
    CHECK(tally, std::memcmp(image.data() + 0x400 + 54, "math.dll", 9) == 0);
}

void test_wine_calls_dll(Tally& tally) {
    std::printf("test_wine_calls_dll\n");
    const std::string dir = "/tmp/okayms-dll";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    okayms::LinkOptions dll_opt;
    dll_opt.dll = true;
    dll_opt.image_base = 0x180000000ULL;
    dll_opt.image_name = "math.dll";
    dll_opt.exports.push_back("add");
    const auto dll = okayms::link_executable({add_dll_object()}, dll_opt);
    CHECK(tally, dll.ok());
    if (!dll.ok()) return;
    CHECK(tally, write_bytes(dir + "/math.dll", dll.value));

    okayms::ObjectFile exe;
    okayms::Section text;
    text.name = ".text";
    text.data = {0x48, 0x83, 0xEC, 0x28, 0xB9, 0x14, 0x00, 0x00, 0x00, 0xBA, 0x16, 0x00, 0x00, 0x00,
                 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x89, 0xC1, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00};
    text.virtual_size = static_cast<uint32_t>(text.data.size());
    text.characteristics = okayms::kScnCntCode | okayms::kScnMemExecute | okayms::kScnMemRead | okayms::kScnAlign16;
    okayms::Relocation add_rel;
    add_rel.offset = 16;
    add_rel.symbol = 0;
    add_rel.type = okayms::kRelAmd64Rel32;
    okayms::Relocation exit_rel;
    exit_rel.offset = 24;
    exit_rel.symbol = 1;
    exit_rel.type = okayms::kRelAmd64Rel32;
    text.relocations.push_back(add_rel);
    text.relocations.push_back(exit_rel);
    exe.sections.push_back(text);
    auto undef = [](const char* name) {
        okayms::Symbol symbol;
        symbol.name = name;
        symbol.storage = okayms::kClassExternal;
        return symbol;
    };
    exe.symbols.push_back(undef("__imp_add"));
    exe.symbols.push_back(undef("__imp_ExitProcess"));
    okayms::Symbol entry;
    entry.name = "entry";
    entry.section = 1;
    entry.type = 0x20;
    entry.storage = okayms::kClassExternal;
    exe.symbols.push_back(entry);
    okayms::LinkOptions exe_opt;
    exe_opt.imports.push_back({"math.dll", "add", 0});
    exe_opt.imports.push_back({"kernel32.dll", "ExitProcess", 0});
    const auto image = okayms::link_executable({exe}, exe_opt);
    CHECK(tally, image.ok());
    if (!image.ok()) {
        std::fprintf(stderr, "  exe: %s\n", image.error.c_str());
        return;
    }
    CHECK(tally, write_bytes(dir + "/use.exe", image.value));
    const int code = wine_exe(dir + "/use.exe");
    CHECK(tally, code == 42);
    if (code != 42) std::fprintf(stderr, "  wine exit %d\n", code);
}

void test_cl_dll(Tally& tally, const std::string& cl) {
    std::printf("test_cl_dll\n");
    const std::string dir = "/tmp/okayms-dll-cl";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    {
        std::ofstream src((dir + "/math.c").c_str());
        src << "__declspec(dllexport) int add(int a, int b) { return a + b; }\n"
               "int entry(void) { return 1; }\n";
    }
    {
        std::ofstream src((dir + "/use.c").c_str());
        src << "__declspec(dllimport) int add(int a, int b);\n"
               "__declspec(dllimport) void ExitProcess(unsigned int code);\n"
               "int entry(void) { ExitProcess(add(20, 22)); return 0; }\n";
    }
    const auto math = import_lib("math.dll", "add");
    const auto kernel = import_lib("kernel32.dll", "ExitProcess");
    const auto math_bytes = okayms::write_archive(math);
    const auto kernel_bytes = okayms::write_archive(kernel);
    CHECK(tally, math_bytes.ok() && kernel_bytes.ok());
    CHECK(tally, write_bytes(dir + "/math.lib", math_bytes.value));
    CHECK(tally, write_bytes(dir + "/kernel32.lib", kernel_bytes.value));
    const int dll_rc = std::system((cl + " /LD /Fe" + dir + "/math.dll " + dir + "/math.c >/tmp/okayms-dll-cl.txt 2>&1").c_str());
    CHECK(tally, dll_rc != -1 && WIFEXITED(dll_rc) && WEXITSTATUS(dll_rc) == 0);
    const int exe_rc = std::system((cl + " /Fe" + dir + "/use.exe " + dir + "/use.c " + dir + "/math.lib " + dir +
                                    "/kernel32.lib >/tmp/okayms-dll-use.txt 2>&1")
                                       .c_str());
    CHECK(tally, exe_rc != -1 && WIFEXITED(exe_rc) && WEXITSTATUS(exe_rc) == 0);
    if (!(exe_rc != -1 && WIFEXITED(exe_rc) && WEXITSTATUS(exe_rc) == 0)) {
        std::ifstream err("/tmp/okayms-dll-use.txt");
        std::string text((std::istreambuf_iterator<char>(err)), {});
        std::fprintf(stderr, "  cl: %s\n", text.c_str());
        return;
    }
    const int code = wine_exe(dir + "/use.exe");
    CHECK(tally, code == 42);
    if (code != 42) {
        std::ifstream err("/tmp/okayms-dll-cl.txt");
        std::string text((std::istreambuf_iterator<char>(err)), {});
        std::fprintf(stderr, "  wine %d\n%s\n", code, text.c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_export_bytes(tally);
    test_wine_calls_dll(tally);
    test_cl_dll(tally, root + "/tools/okayms/build/okay-cl");
    return tally.finish(3);
}
